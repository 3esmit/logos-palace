#include "palace_vm_finality.h"

#include "palace_sha256.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace palace {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kSnapshotMagic = "PALACE_VM_FINALITY_V1\n";
constexpr const char* kStoreFileName = "palace_vm_finality_v1.bin";
constexpr std::size_t kChecksumSize = 64U;
constexpr std::size_t kMaxSnapshotBytes = 1024U * 1024U;
constexpr std::size_t kMaxRecords = 1024U;
constexpr std::size_t kMaxReceiptBytes = 8192U;

void appendU32(std::string& output, std::uint32_t value)
{
    output.push_back(static_cast<char>((value >> 24U) & 0xffU));
    output.push_back(static_cast<char>((value >> 16U) & 0xffU));
    output.push_back(static_cast<char>((value >> 8U) & 0xffU));
    output.push_back(static_cast<char>(value & 0xffU));
}

bool appendString(std::string& output, const std::string& value)
{
    if (value.size() > std::numeric_limits<std::uint32_t>::max())
        return false;
    appendU32(output, static_cast<std::uint32_t>(value.size()));
    output += value;
    return true;
}

class SnapshotReader {
public:
    explicit SnapshotReader(std::string_view input)
        : m_input(input)
    {
    }

    bool readU32(std::uint32_t& value)
    {
        if (m_position > m_input.size()
            || m_input.size() - m_position < 4U) {
            return false;
        }
        const auto byte = [this](std::size_t offset) {
            return static_cast<std::uint32_t>(
                static_cast<unsigned char>(m_input[m_position + offset]));
        };
        value = (byte(0U) << 24U) | (byte(1U) << 16U)
            | (byte(2U) << 8U) | byte(3U);
        m_position += 4U;
        return true;
    }

    bool readByte(std::uint8_t& value)
    {
        if (m_position >= m_input.size())
            return false;
        value = static_cast<std::uint8_t>(
            static_cast<unsigned char>(m_input[m_position++]));
        return true;
    }

    bool readString(std::string& value, std::size_t maximum)
    {
        std::uint32_t size = 0U;
        if (!readU32(size) || size > maximum
            || m_position > m_input.size()
            || m_input.size() - m_position < size) {
            return false;
        }
        value.assign(m_input.data() + m_position, size);
        m_position += size;
        return true;
    }

    bool atEnd() const { return m_position == m_input.size(); }

private:
    std::string_view m_input;
    std::size_t m_position = 0U;
};

bool isLowerHexDigest(const std::string& value)
{
    return value.size() == kChecksumSize
        && std::all_of(value.begin(), value.end(), [](unsigned char value) {
               return (value >= '0' && value <= '9')
                   || (value >= 'a' && value <= 'f');
           });
}

bool isAcceptedReceipt(const std::string& receipt)
{
    return !receipt.empty() && receipt.size() <= kMaxReceiptBytes
        && receipt.rfind("accepted=1;", 0U) == 0U;
}

void appendDigestField(
    std::string& canonical,
    const char* name,
    const std::string& value)
{
    canonical += name;
    canonical += '=';
    canonical += std::to_string(value.size());
    canonical += ':';
    canonical += value;
    canonical += ';';
}

bool writeAll(int descriptor, const std::string& bytes)
{
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const ssize_t written = ::write(
            descriptor, bytes.data() + offset, bytes.size() - offset);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (written == 0)
            return false;
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

bool readAll(int descriptor, std::string& bytes, std::size_t size)
{
    bytes.resize(size);
    std::size_t offset = 0U;
    while (offset < size) {
        const ssize_t received = ::read(
            descriptor, bytes.data() + offset, size - offset);
        if (received < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (received == 0)
            return false;
        offset += static_cast<std::size_t>(received);
    }
    return true;
}

bool isSafeRegularFile(const fs::path& path)
{
    struct stat status {};
    if (::lstat(path.c_str(), &status) != 0)
        return errno == ENOENT;
    return S_ISREG(status.st_mode) && status.st_nlink == 1U;
}

} // namespace

std::string palaceVmTurnInputDigest(const PalaceVmTurnInput& input)
{
    std::string canonical = "palace-vm-turn-input-v1;";
    appendDigestField(canonical, "action", input.actionId);
    appendDigestField(canonical, "script", input.script);
    appendDigestField(canonical, "bundle", input.scriptBundleCid);
    appendDigestField(canonical, "epoch", input.roomEpoch);
    appendDigestField(canonical, "trigger", input.trigger);
    appendDigestField(canonical, "prior_state", input.priorState);
    appendDigestField(canonical, "allowed_rooms", input.allowedRooms);
    canonical += std::string("room_locked=")
        + (input.roomLocked ? "1;" : "0;");
    canonical += std::string("can_mutate_shared_state=")
        + (input.canMutateSharedState ? "1;" : "0;");
    canonical += "instruction_budget="
        + std::to_string(input.instructionBudget) + ';';
    return crypto::sha256Hex(canonical);
}

bool isPalaceVmActionId(const std::string& actionId)
{
    if (actionId.empty() || actionId.size() > 20U
        || actionId == "0"
        || (actionId.size() > 1U && actionId.front() == '0')) {
        return false;
    }
    std::uint64_t parsed = 0U;
    const auto result = std::from_chars(
        actionId.data(), actionId.data() + actionId.size(), parsed);
    return result.ec == std::errc()
        && result.ptr == actionId.data() + actionId.size()
        && parsed > 0U;
}

const char* palaceVmFinalityStatusName(PalaceVmFinalityStatus status)
{
    switch (status) {
    case PalaceVmFinalityStatus::Pending: return "pending";
    case PalaceVmFinalityStatus::Promoted: return "promoted";
    }
    return "unknown";
}

PalaceVmFinalityResult PalaceVmFinalityJournal::recordProvisional(
    const PalaceVmTurnInput& input,
    const std::string& provisionalReceipt)
{
    if (!isPalaceVmActionId(input.actionId))
        return {false, false, "invalid-action-id"};
    if (!isAcceptedReceipt(provisionalReceipt))
        return {false, false, "invalid-provisional-receipt"};

    const std::string inputDigest = palaceVmTurnInputDigest(input);
    const auto existing = m_records.find(input.actionId);
    if (existing != m_records.end()) {
        if (existing->second.status == PalaceVmFinalityStatus::Promoted)
            return {false, false, "already-promoted"};
        if (existing->second.turnInputDigest != inputDigest)
            return {false, false, "turn-input-mismatch"};
        if (existing->second.provisionalReceipt != provisionalReceipt)
            return {false, false, "provisional-receipt-mismatch"};
        return {true, false, {}};
    }
    if (m_records.size() >= kMaxRecords)
        return {false, false, "journal-full"};

    PalaceVmFinalityRecord record;
    record.actionId = input.actionId;
    record.turnInputDigest = inputDigest;
    record.provisionalReceipt = provisionalReceipt;
    m_records.emplace(record.actionId, std::move(record));
    return {true, true, {}};
}

PalaceVmFinalityResult PalaceVmFinalityJournal::validatePromotion(
    const PalaceVmTurnInput& input,
    const std::string& provisionalReceipt) const
{
    if (!isPalaceVmActionId(input.actionId))
        return {false, false, "invalid-action-id"};
    const auto record = m_records.find(input.actionId);
    if (record == m_records.end())
        return {false, false, "unknown-action"};
    if (record->second.status == PalaceVmFinalityStatus::Promoted)
        return {false, false, "already-promoted"};
    if (record->second.provisionalReceipt != provisionalReceipt)
        return {false, false, "provisional-receipt-mismatch"};
    if (record->second.turnInputDigest != palaceVmTurnInputDigest(input))
        return {false, false, "turn-input-mismatch"};
    return {true, false, {}};
}

PalaceVmFinalityResult PalaceVmFinalityJournal::recordPromotion(
    const PalaceVmTurnInput& input,
    const std::string& provisionalReceipt,
    const std::string& finalizedReceipt)
{
    const PalaceVmFinalityResult validation =
        validatePromotion(input, provisionalReceipt);
    if (!validation.accepted)
        return validation;
    if (!isAcceptedReceipt(finalizedReceipt))
        return {false, false, "invalid-finalized-receipt"};

    PalaceVmFinalityRecord& record = m_records.at(input.actionId);
    record.status = PalaceVmFinalityStatus::Promoted;
    record.finalizedReceipt = finalizedReceipt;
    return {true, true, {}};
}

const PalaceVmFinalityRecord* PalaceVmFinalityJournal::find(
    const std::string& actionId) const
{
    const auto record = m_records.find(actionId);
    return record == m_records.end() ? nullptr : &record->second;
}

std::string PalaceVmFinalityJournal::statusEvidence(
    const std::string& actionId) const
{
    const PalaceVmFinalityRecord* record = find(actionId);
    if (record == nullptr)
        return "status=unknown;action=" + actionId;
    return "status=" + std::string(palaceVmFinalityStatusName(record->status))
        + ";action=" + record->actionId
        + ";turn_input_digest=" + record->turnInputDigest
        + ";provisional_receipt_sha256="
        + crypto::sha256Hex(record->provisionalReceipt)
        + ";finalized_receipt_sha256="
        + (record->finalizedReceipt.empty()
               ? std::string()
               : crypto::sha256Hex(record->finalizedReceipt));
}

std::string PalaceVmFinalityJournal::snapshot() const
{
    std::string payload;
    appendU32(payload, static_cast<std::uint32_t>(m_records.size()));
    for (const auto& [actionId, record] : m_records) {
        if (!appendString(payload, actionId)
            || !appendString(payload, record.turnInputDigest)
            || !appendString(payload, record.provisionalReceipt)) {
            return {};
        }
        payload.push_back(record.status == PalaceVmFinalityStatus::Pending
                ? static_cast<char>(0)
                : static_cast<char>(1));
        if (!appendString(payload, record.finalizedReceipt))
            return {};
    }
    return std::string(kSnapshotMagic)
        + crypto::sha256Hex(payload) + '\n' + payload;
}

bool PalaceVmFinalityJournal::restore(const std::string& snapshot)
{
    if (snapshot.size() > kMaxSnapshotBytes
        || snapshot.size() < kSnapshotMagic.size() + kChecksumSize + 1U
        || snapshot.compare(0U, kSnapshotMagic.size(), kSnapshotMagic) != 0) {
        return false;
    }
    const std::size_t checksumOffset = kSnapshotMagic.size();
    const std::string checksum =
        snapshot.substr(checksumOffset, kChecksumSize);
    if (!isLowerHexDigest(checksum)
        || snapshot[checksumOffset + kChecksumSize] != '\n') {
        return false;
    }
    const std::string_view payload(
        snapshot.data() + checksumOffset + kChecksumSize + 1U,
        snapshot.size() - checksumOffset - kChecksumSize - 1U);
    if (crypto::sha256Hex(std::string(payload)) != checksum)
        return false;

    SnapshotReader reader(payload);
    std::uint32_t recordCount = 0U;
    if (!reader.readU32(recordCount) || recordCount > kMaxRecords)
        return false;

    std::map<std::string, PalaceVmFinalityRecord> restored;
    for (std::uint32_t index = 0U; index < recordCount; ++index) {
        PalaceVmFinalityRecord record;
        std::uint8_t status = 0U;
        if (!reader.readString(record.actionId, 20U)
            || !reader.readString(record.turnInputDigest, kChecksumSize)
            || !reader.readString(
                record.provisionalReceipt, kMaxReceiptBytes)
            || !reader.readByte(status)
            || !reader.readString(record.finalizedReceipt, kMaxReceiptBytes)
            || !isPalaceVmActionId(record.actionId)
            || !isLowerHexDigest(record.turnInputDigest)
            || !isAcceptedReceipt(record.provisionalReceipt)
            || status > 1U) {
            return false;
        }
        record.status = status == 0U
            ? PalaceVmFinalityStatus::Pending
            : PalaceVmFinalityStatus::Promoted;
        if ((record.status == PalaceVmFinalityStatus::Pending
                && !record.finalizedReceipt.empty())
            || (record.status == PalaceVmFinalityStatus::Promoted
                && !isAcceptedReceipt(record.finalizedReceipt))
            || !restored.emplace(record.actionId, std::move(record)).second) {
            return false;
        }
    }
    if (!reader.atEnd())
        return false;
    m_records = std::move(restored);
    return true;
}

PalaceVmFinalityStore::PalaceVmFinalityStore(
    std::string instancePersistencePath)
    : m_instancePersistencePath(std::move(instancePersistencePath))
{
}

bool PalaceVmFinalityStore::exists() const
{
    if (m_instancePersistencePath.empty())
        return false;
    struct stat status {};
    const fs::path path =
        fs::path(m_instancePersistencePath) / kStoreFileName;
    return ::lstat(path.c_str(), &status) == 0;
}

bool PalaceVmFinalityStore::load(PalaceVmFinalityJournal& journal) const
{
    if (m_instancePersistencePath.empty())
        return false;
    const fs::path path =
        fs::path(m_instancePersistencePath) / kStoreFileName;
    const int descriptor =
        ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0)
        return false;

    struct stat status {};
    const bool valid = ::fstat(descriptor, &status) == 0
        && S_ISREG(status.st_mode)
        && status.st_nlink == 1U
        && status.st_size >= 0
        && static_cast<std::uint64_t>(status.st_size) <= kMaxSnapshotBytes;
    std::string snapshot;
    const bool read = valid
        && readAll(
            descriptor,
            snapshot,
            static_cast<std::size_t>(status.st_size));
    const bool closed = ::close(descriptor) == 0;
    if (!read || !closed)
        return false;

    PalaceVmFinalityJournal restored;
    if (!restored.restore(snapshot))
        return false;
    journal = std::move(restored);
    return true;
}

bool PalaceVmFinalityStore::save(
    const PalaceVmFinalityJournal& journal) const
{
    if (m_instancePersistencePath.empty())
        return false;
    const std::string snapshot = journal.snapshot();
    if (snapshot.empty() || snapshot.size() > kMaxSnapshotBytes)
        return false;

    std::error_code error;
    const fs::path directory(m_instancePersistencePath);
    if (!fs::is_directory(directory, error) || error)
        return false;
    const fs::path path = directory / kStoreFileName;
    if (!isSafeRegularFile(path))
        return false;

    std::string temporaryPattern =
        (directory / ".palace_vm_finality_v1.XXXXXX").string();
    std::vector<char> temporary(
        temporaryPattern.begin(), temporaryPattern.end());
    temporary.push_back('\0');
    const int descriptor = ::mkstemp(temporary.data());
    if (descriptor < 0)
        return false;
    const fs::path temporaryPath(temporary.data());

    bool saved = ::fchmod(descriptor, S_IRUSR | S_IWUSR) == 0
        && writeAll(descriptor, snapshot)
        && ::fsync(descriptor) == 0;
    if (::close(descriptor) != 0)
        saved = false;
    if (saved && ::rename(temporaryPath.c_str(), path.c_str()) != 0)
        saved = false;
    if (!saved) {
        std::error_code ignored;
        fs::remove(temporaryPath, ignored);
        return false;
    }

    const int directoryDescriptor =
        ::open(directory.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    if (directoryDescriptor < 0)
        return false;
    const bool synchronized = ::fsync(directoryDescriptor) == 0;
    const bool closed = ::close(directoryDescriptor) == 0;
    return synchronized && closed;
}

} // namespace palace
