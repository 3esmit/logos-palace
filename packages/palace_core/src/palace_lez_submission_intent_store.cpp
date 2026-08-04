#include "palace_lez_submission_intent_store.h"

#include "palace_sha256.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace palace {
namespace {

namespace fs = std::filesystem;

constexpr char kFileName[] = "lez-submission-intent-v1";
constexpr char kTemporaryFileName[] = "lez-submission-intent-v1.next";
constexpr char kHeader[] = "logos-palace-lez-submission-intent-v1";
constexpr std::size_t kMaximumRecordBytes = 128U * 1024U;

bool isLowerHex64(const std::string& value)
{
    return value.size() == 64U
        && std::all_of(
            value.begin(), value.end(), [](const unsigned char value) {
                return (value >= '0' && value <= '9')
                    || (value >= 'a' && value <= 'f');
            });
}

bool canonicalU64(
    const std::string& value,
    std::uint64_t& output)
{
    if (value.empty()
        || (value.size() > 1U && value.front() == '0')
        || !std::all_of(
            value.begin(), value.end(), [](const unsigned char value) {
                return value >= '0' && value <= '9';
            })) {
        return false;
    }
    try {
        std::size_t consumed = 0U;
        const unsigned long long parsed =
            std::stoull(value, &consumed, 10);
        if (consumed != value.size()
            || parsed
                > static_cast<unsigned long long>(
                    std::numeric_limits<std::uint64_t>::max())) {
            return false;
        }
        output = static_cast<std::uint64_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

const char* phaseName(const PalaceLezSubmissionIntentPhase phase)
{
    switch (phase) {
    case PalaceLezSubmissionIntentPhase::Prepared:
        return "prepared";
    case PalaceLezSubmissionIntentPhase::MayHaveBeenSubmitted:
        return "may_have_been_submitted";
    case PalaceLezSubmissionIntentPhase::Committed:
        return "committed";
    case PalaceLezSubmissionIntentPhase::Rejected:
        return "rejected";
    }
    return "invalid";
}

bool validPlan(const PalaceLezTransactionPlanV3& plan)
{
    if (!plan.accepted || plan.accountIdsHex.size() < 2U
        || plan.accountIdsHex.size()
            != plan.signingRequirements.size()
        || !isLowerHex64(plan.programIdHex)
        || !isLowerHex64(plan.rootAccountIdHex)
        || plan.instructionWords.empty()) {
        return false;
    }
    const PalaceLezWireInstruction decoded =
        PalaceLezCodec::decodeInstruction(plan.instructionWords);
    if (!decoded.accepted)
        return false;
    const PalaceLezTransactionPlanV3 rebuilt =
        PalaceLezCodec::buildTransaction(
            plan.programIdHex,
            plan.accountIdsHex[1],
            decoded.instruction);
    return rebuilt.accepted
        && rebuilt.programIdHex == plan.programIdHex
        && rebuilt.rootAccountIdHex == plan.rootAccountIdHex
        && rebuilt.accountIdsHex == plan.accountIdsHex
        && rebuilt.signingRequirements
            == plan.signingRequirements
        && rebuilt.instructionWords == plan.instructionWords;
}

bool validIntent(const PalaceLezSubmissionIntentV1& intent)
{
    std::uint64_t actionId = 0U;
    const PalaceLezWireInstruction decoded =
        PalaceLezCodec::decodeInstruction(intent.plan.instructionWords);
    const bool initializesPalace =
        decoded.accepted
        && std::holds_alternative<PalaceLezInitializeV3>(
            decoded.instruction.payload);
    if ((intent.actionId == "0"
            ? !initializesPalace
            : !PalaceLezCodec::parseOrderedActionId(
                intent.actionId, actionId))
        || !validPlan(intent.plan)
        || !isLowerHex64(intent.expectedRootDataSha256Hex)) {
        return false;
    }
    switch (intent.phase) {
    case PalaceLezSubmissionIntentPhase::Prepared:
    case PalaceLezSubmissionIntentPhase::MayHaveBeenSubmitted:
    case PalaceLezSubmissionIntentPhase::Rejected:
        return intent.transactionHash.empty();
    case PalaceLezSubmissionIntentPhase::Committed:
        return isLowerHex64(intent.transactionHash);
    }
    return false;
}

bool validTransition(
    const PalaceLezSubmissionIntentV1& existing,
    const PalaceLezSubmissionIntentV1& next)
{
    if (samePalaceLezSubmissionIntent(existing, next))
        return true;
    if (existing.phase == PalaceLezSubmissionIntentPhase::Committed
        || existing.phase == PalaceLezSubmissionIntentPhase::Rejected) {
        return next.phase
            == PalaceLezSubmissionIntentPhase::Prepared;
    }
    PalaceLezSubmissionIntentV1 expected = existing;
    expected.phase = next.phase;
    expected.transactionHash = next.transactionHash;
    if (expected.actionId != next.actionId
        || expected.minimumFinalizedBlockExclusive
            != next.minimumFinalizedBlockExclusive
        || expected.expectedRootDataSha256Hex
            != next.expectedRootDataSha256Hex
        || expected.plan.programIdHex != next.plan.programIdHex
        || expected.plan.rootAccountIdHex
            != next.plan.rootAccountIdHex
        || expected.plan.accountIdsHex != next.plan.accountIdsHex
        || expected.plan.signingRequirements
            != next.plan.signingRequirements
        || expected.plan.instructionWords
            != next.plan.instructionWords) {
        return false;
    }
    if (existing.phase == PalaceLezSubmissionIntentPhase::Prepared) {
        return next.phase
                == PalaceLezSubmissionIntentPhase::MayHaveBeenSubmitted
            || next.phase == PalaceLezSubmissionIntentPhase::Rejected;
    }
    return existing.phase
            == PalaceLezSubmissionIntentPhase::MayHaveBeenSubmitted
        && (next.phase == PalaceLezSubmissionIntentPhase::Committed
            || next.phase
                == PalaceLezSubmissionIntentPhase::Rejected);
}

template<typename Value>
std::string joinDecimal(const std::vector<Value>& values)
{
    std::string output;
    for (const Value value : values) {
        if (!output.empty())
            output += ',';
        output += std::to_string(value);
    }
    return output;
}

std::string joinAccounts(const std::vector<std::string>& values)
{
    std::string output;
    for (const std::string& value : values) {
        if (!output.empty())
            output += ',';
        output += value;
    }
    return output;
}

std::string joinSigners(const std::vector<bool>& values)
{
    std::string output;
    for (const bool value : values) {
        if (!output.empty())
            output += ',';
        output += value ? '1' : '0';
    }
    return output;
}

std::vector<std::string> split(
    const std::string& value,
    const char delimiter)
{
    if (value.empty())
        return {};
    std::vector<std::string> output;
    std::size_t cursor = 0U;
    while (cursor <= value.size()) {
        const std::size_t next = value.find(delimiter, cursor);
        const std::size_t end =
            next == std::string::npos ? value.size() : next;
        if (end == cursor)
            return {};
        output.push_back(value.substr(cursor, end - cursor));
        if (next == std::string::npos)
            break;
        cursor = next + 1U;
    }
    return output;
}

std::string serialize(const PalaceLezSubmissionIntentV1& intent)
{
    if (!validIntent(intent))
        return {};
    const std::string body =
        std::string(kHeader) + "\n"
        + "version=1\n"
        + "action_id=" + intent.actionId + "\n"
        + "phase=" + phaseName(intent.phase) + "\n"
        + "minimum_finalized_block_exclusive="
        + std::to_string(intent.minimumFinalizedBlockExclusive) + "\n"
        + "program_id=" + intent.plan.programIdHex + "\n"
        + "root_account_id=" + intent.plan.rootAccountIdHex + "\n"
        + "account_ids=" + joinAccounts(intent.plan.accountIdsHex) + "\n"
        + "signers=" + joinSigners(intent.plan.signingRequirements) + "\n"
        + "instruction_words=" + joinDecimal(intent.plan.instructionWords)
        + "\n"
        + "expected_root_data_sha256="
        + intent.expectedRootDataSha256Hex + "\n"
        + "transaction_hash=" + intent.transactionHash + "\n";
    return body + "checksum=" + crypto::sha256Hex(body) + "\n";
}

bool parse(
    const std::string& record,
    PalaceLezSubmissionIntentV1& intent)
{
    if (record.empty() || record.size() > kMaximumRecordBytes
        || record.back() != '\n') {
        return false;
    }
    std::vector<std::string> lines;
    std::size_t cursor = 0U;
    while (cursor < record.size()) {
        const std::size_t next = record.find('\n', cursor);
        if (next == std::string::npos || next == cursor)
            return false;
        lines.push_back(record.substr(cursor, next - cursor));
        cursor = next + 1U;
    }
    if (lines.size() != 13U || lines[0] != kHeader
        || lines[1] != "version=1"
        || lines[2].rfind("action_id=", 0U) != 0U
        || lines[3].rfind("phase=", 0U) != 0U
        || lines[4].rfind(
            "minimum_finalized_block_exclusive=", 0U) != 0U
        || lines[5].rfind("program_id=", 0U) != 0U
        || lines[6].rfind("root_account_id=", 0U) != 0U
        || lines[7].rfind("account_ids=", 0U) != 0U
        || lines[8].rfind("signers=", 0U) != 0U
        || lines[9].rfind("instruction_words=", 0U) != 0U
        || lines[10].rfind(
            "expected_root_data_sha256=", 0U) != 0U
        || lines[11].rfind("transaction_hash=", 0U) != 0U
        || lines[12].rfind("checksum=", 0U) != 0U) {
        return false;
    }
    const std::size_t checksumOffset = record.rfind("checksum=");
    if (checksumOffset == std::string::npos
        || lines[12].substr(9U)
            != crypto::sha256Hex(record.substr(0U, checksumOffset))) {
        return false;
    }

    PalaceLezSubmissionIntentV1 candidate;
    candidate.actionId = lines[2].substr(10U);
    const std::string phase = lines[3].substr(6U);
    if (phase == "prepared")
        candidate.phase = PalaceLezSubmissionIntentPhase::Prepared;
    else if (phase == "may_have_been_submitted")
        candidate.phase =
            PalaceLezSubmissionIntentPhase::MayHaveBeenSubmitted;
    else if (phase == "committed")
        candidate.phase = PalaceLezSubmissionIntentPhase::Committed;
    else if (phase == "rejected")
        candidate.phase = PalaceLezSubmissionIntentPhase::Rejected;
    else
        return false;
    if (!canonicalU64(lines[4].substr(34U),
            candidate.minimumFinalizedBlockExclusive)) {
        return false;
    }
    candidate.plan.accepted = true;
    candidate.plan.programIdHex = lines[5].substr(11U);
    candidate.plan.rootAccountIdHex = lines[6].substr(16U);
    candidate.plan.accountIdsHex =
        split(lines[7].substr(12U), ',');
    const std::vector<std::string> signers =
        split(lines[8].substr(8U), ',');
    for (const std::string& signer : signers) {
        if (signer != "0" && signer != "1")
            return false;
        candidate.plan.signingRequirements.push_back(signer == "1");
    }
    const std::vector<std::string> words =
        split(lines[9].substr(18U), ',');
    for (const std::string& word : words) {
        std::uint64_t parsed = 0U;
        if (!canonicalU64(word, parsed)
            || parsed
                > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        candidate.plan.instructionWords.push_back(
            static_cast<std::uint32_t>(parsed));
    }
    const PalaceLezWireInstruction decoded =
        PalaceLezCodec::decodeInstruction(
            candidate.plan.instructionWords);
    if (!decoded.accepted)
        return false;
    candidate.plan.instruction = decoded.instruction;
    candidate.expectedRootDataSha256Hex =
        lines[10].substr(26U);
    candidate.transactionHash = lines[11].substr(17U);
    if (!validIntent(candidate) || serialize(candidate) != record)
        return false;
    intent = std::move(candidate);
    return true;
}

#if defined(__unix__) || defined(__APPLE__)
class FileDescriptor {
public:
    explicit FileDescriptor(const int value = -1)
        : value_(value)
    {
    }
    ~FileDescriptor()
    {
        if (value_ >= 0)
            ::close(value_);
    }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept
        : value_(other.value_)
    {
        other.value_ = -1;
    }
    FileDescriptor& operator=(FileDescriptor&& other) noexcept
    {
        if (this != &other) {
            if (value_ >= 0)
                ::close(value_);
            value_ = other.value_;
            other.value_ = -1;
        }
        return *this;
    }
    int get() const
    {
        return value_;
    }
    bool close()
    {
        if (value_ < 0)
            return true;
        const int value = value_;
        value_ = -1;
        return ::close(value) == 0;
    }

private:
    int value_;
};

bool canonicalRoot(const fs::path& root)
{
    if (!root.is_absolute() || root.lexically_normal() != root)
        return false;
    std::error_code error;
    const fs::path canonical = fs::canonical(root, error);
    return !error && canonical == root;
}

PalaceLezSubmissionIntentStoreStatus openRoot(
    const fs::path& root,
    const bool create,
    FileDescriptor& directory)
{
    if (create) {
        std::error_code error;
        fs::create_directories(root, error);
        if (error)
            return PalaceLezSubmissionIntentStoreStatus::IoError;
    }
    if (!canonicalRoot(root)) {
        std::error_code error;
        return !fs::exists(root, error) && !error
            ? PalaceLezSubmissionIntentStoreStatus::NotFound
            : PalaceLezSubmissionIntentStoreStatus::InvalidArgument;
    }
    int flags = O_RDONLY;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    FileDescriptor candidate(::open(root.c_str(), flags));
    if (candidate.get() < 0)
        return PalaceLezSubmissionIntentStoreStatus::IoError;
    struct stat status {};
    if (::fstat(candidate.get(), &status) != 0)
        return PalaceLezSubmissionIntentStoreStatus::IoError;
    if (!S_ISDIR(status.st_mode))
        return PalaceLezSubmissionIntentStoreStatus::InvalidArgument;
    if (status.st_uid != ::geteuid()
        || (status.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        return PalaceLezSubmissionIntentStoreStatus::
            InsecurePermissions;
    }
    directory = std::move(candidate);
    return PalaceLezSubmissionIntentStoreStatus::Loaded;
}

bool readAll(
    const int descriptor,
    unsigned char* output,
    const std::size_t size)
{
    std::size_t offset = 0U;
    while (offset < size) {
        const ssize_t received =
            ::read(descriptor, output + offset, size - offset);
        if (received < 0 && errno == EINTR)
            continue;
        if (received <= 0)
            return false;
        offset += static_cast<std::size_t>(received);
    }
    return true;
}

bool writeAll(
    const int descriptor,
    const unsigned char* input,
    const std::size_t size)
{
    std::size_t offset = 0U;
    while (offset < size) {
        const ssize_t written =
            ::write(descriptor, input + offset, size - offset);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return false;
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

PalaceLezSubmissionIntentStoreStatus readAt(
    const int directory,
    PalaceLezSubmissionIntentV1& intent)
{
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
#ifdef O_NONBLOCK
    flags |= O_NONBLOCK;
#endif
    FileDescriptor descriptor(
        ::openat(directory, kFileName, flags));
    if (descriptor.get() < 0) {
        if (errno == ENOENT)
            return PalaceLezSubmissionIntentStoreStatus::NotFound;
        if (errno == ELOOP)
            return PalaceLezSubmissionIntentStoreStatus::InvalidRecord;
        return PalaceLezSubmissionIntentStoreStatus::IoError;
    }
    struct stat status {};
    if (::fstat(descriptor.get(), &status) != 0)
        return PalaceLezSubmissionIntentStoreStatus::IoError;
    if (!S_ISREG(status.st_mode) || status.st_nlink != 1)
        return PalaceLezSubmissionIntentStoreStatus::InvalidRecord;
    if (status.st_uid != ::geteuid()
        || (status.st_mode & 07777) != (S_IRUSR | S_IWUSR)) {
        return PalaceLezSubmissionIntentStoreStatus::
            InsecurePermissions;
    }
    if (status.st_size <= 0
        || static_cast<std::uintmax_t>(status.st_size)
            > kMaximumRecordBytes) {
        return PalaceLezSubmissionIntentStoreStatus::InvalidRecord;
    }
    std::vector<unsigned char> bytes(
        static_cast<std::size_t>(status.st_size));
    unsigned char trailing = 0U;
    if (!readAll(descriptor.get(), bytes.data(), bytes.size())
        || ::read(descriptor.get(), &trailing, 1U) != 0) {
        return PalaceLezSubmissionIntentStoreStatus::InvalidRecord;
    }
    PalaceLezSubmissionIntentV1 candidate;
    if (!parse(
            std::string(bytes.begin(), bytes.end()),
            candidate)) {
        return PalaceLezSubmissionIntentStoreStatus::InvalidRecord;
    }
    intent = std::move(candidate);
    return PalaceLezSubmissionIntentStoreStatus::Loaded;
}

PalaceLezSubmissionIntentStoreStatus writeAt(
    const int directory,
    const PalaceLezSubmissionIntentV1& intent)
{
    PalaceLezSubmissionIntentV1 existing;
    const PalaceLezSubmissionIntentStoreStatus loaded =
        readAt(directory, existing);
    if (loaded == PalaceLezSubmissionIntentStoreStatus::Loaded) {
        if (!validTransition(existing, intent))
            return PalaceLezSubmissionIntentStoreStatus::InvalidArgument;
        if (samePalaceLezSubmissionIntent(existing, intent))
            return PalaceLezSubmissionIntentStoreStatus::Saved;
    } else if (loaded != PalaceLezSubmissionIntentStoreStatus::NotFound) {
        return loaded;
    } else if (intent.phase
               != PalaceLezSubmissionIntentPhase::Prepared) {
        return PalaceLezSubmissionIntentStoreStatus::InvalidArgument;
    }

    const std::string record = serialize(intent);
    if (record.empty())
        return PalaceLezSubmissionIntentStoreStatus::InvalidArgument;
    static std::atomic<std::uint64_t> sequence{0U};
    FileDescriptor temporary;
    std::string temporaryName;
    for (std::size_t attempt = 0U;
         attempt < 128U && temporary.get() < 0;
         ++attempt) {
        temporaryName =
            std::string(".") + kTemporaryFileName + "."
            + std::to_string(
                static_cast<unsigned long>(::getpid()))
            + "."
            + std::to_string(
                sequence.fetch_add(
                    1U, std::memory_order_relaxed));
        int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        FileDescriptor candidate(
            ::openat(
                directory,
                temporaryName.c_str(),
                flags,
                S_IRUSR | S_IWUSR));
        if (candidate.get() >= 0)
            temporary = std::move(candidate);
        else if (errno != EEXIST)
            return PalaceLezSubmissionIntentStoreStatus::IoError;
    }
    if (temporary.get() < 0)
        return PalaceLezSubmissionIntentStoreStatus::IoError;

    bool accepted =
        ::fchmod(temporary.get(), S_IRUSR | S_IWUSR) == 0;
    accepted = accepted
        && writeAll(
            temporary.get(),
            reinterpret_cast<const unsigned char*>(record.data()),
            record.size())
        && ::fsync(temporary.get()) == 0
        && temporary.close();
    if (!accepted) {
        ::unlinkat(directory, temporaryName.c_str(), 0);
        return PalaceLezSubmissionIntentStoreStatus::IoError;
    }

    struct stat destination {};
    errno = 0;
    const int destinationStatus = ::fstatat(
            directory,
            kFileName,
            &destination,
            AT_SYMLINK_NOFOLLOW);
    if (destinationStatus == 0
        && (!S_ISREG(destination.st_mode)
            || destination.st_nlink != 1
            || destination.st_uid != ::geteuid()
            || (destination.st_mode & 07777)
                != (S_IRUSR | S_IWUSR))) {
        ::unlinkat(directory, temporaryName.c_str(), 0);
        return S_ISLNK(destination.st_mode)
            ? PalaceLezSubmissionIntentStoreStatus::InvalidRecord
            : PalaceLezSubmissionIntentStoreStatus::
                  InsecurePermissions;
    }
    if (destinationStatus != 0 && errno != ENOENT) {
        ::unlinkat(directory, temporaryName.c_str(), 0);
        return PalaceLezSubmissionIntentStoreStatus::IoError;
    }
    if (::renameat(
            directory,
            temporaryName.c_str(),
            directory,
            kFileName) != 0
        || ::fsync(directory) != 0) {
        ::unlinkat(directory, temporaryName.c_str(), 0);
        return PalaceLezSubmissionIntentStoreStatus::IoError;
    }
    return PalaceLezSubmissionIntentStoreStatus::Saved;
}
#endif

} // namespace

bool samePalaceLezSubmissionIntent(
    const PalaceLezSubmissionIntentV1& first,
    const PalaceLezSubmissionIntentV1& second)
{
    return first.actionId == second.actionId
        && first.phase == second.phase
        && first.minimumFinalizedBlockExclusive
            == second.minimumFinalizedBlockExclusive
        && first.plan.programIdHex == second.plan.programIdHex
        && first.plan.rootAccountIdHex == second.plan.rootAccountIdHex
        && first.plan.accountIdsHex == second.plan.accountIdsHex
        && first.plan.signingRequirements
            == second.plan.signingRequirements
        && first.plan.instructionWords == second.plan.instructionWords
        && first.expectedRootDataSha256Hex
            == second.expectedRootDataSha256Hex
        && first.transactionHash == second.transactionHash;
}

PalaceLezSubmissionIntentStore::PalaceLezSubmissionIntentStore(
    std::string directory)
    : directory_(std::move(directory))
{
}

PalaceLezSubmissionIntentStoreStatus
PalaceLezSubmissionIntentStore::save(
    const PalaceLezSubmissionIntentV1& intent) const
{
    if (directory_.empty() || !validIntent(intent))
        return PalaceLezSubmissionIntentStoreStatus::InvalidArgument;
#if defined(__unix__) || defined(__APPLE__)
    FileDescriptor directory;
    const PalaceLezSubmissionIntentStoreStatus opened =
        openRoot(fs::path(directory_), true, directory);
    return opened == PalaceLezSubmissionIntentStoreStatus::Loaded
        ? writeAt(directory.get(), intent) : opened;
#else
    return PalaceLezSubmissionIntentStoreStatus::IoError;
#endif
}

PalaceLezSubmissionIntentStoreStatus
PalaceLezSubmissionIntentStore::load(
    PalaceLezSubmissionIntentV1& intent) const
{
    if (directory_.empty())
        return PalaceLezSubmissionIntentStoreStatus::InvalidArgument;
#if defined(__unix__) || defined(__APPLE__)
    FileDescriptor directory;
    const PalaceLezSubmissionIntentStoreStatus opened =
        openRoot(fs::path(directory_), false, directory);
    return opened == PalaceLezSubmissionIntentStoreStatus::Loaded
        ? readAt(directory.get(), intent) : opened;
#else
    return PalaceLezSubmissionIntentStoreStatus::IoError;
#endif
}

const char* palaceLezSubmissionIntentStoreStatusName(
    const PalaceLezSubmissionIntentStoreStatus status)
{
    switch (status) {
    case PalaceLezSubmissionIntentStoreStatus::Saved:
        return "saved";
    case PalaceLezSubmissionIntentStoreStatus::Loaded:
        return "loaded";
    case PalaceLezSubmissionIntentStoreStatus::NotFound:
        return "not_found";
    case PalaceLezSubmissionIntentStoreStatus::InvalidArgument:
        return "invalid_argument";
    case PalaceLezSubmissionIntentStoreStatus::InvalidRecord:
        return "invalid_record";
    case PalaceLezSubmissionIntentStoreStatus::InsecurePermissions:
        return "insecure_permissions";
    case PalaceLezSubmissionIntentStoreStatus::IoError:
        return "io_error";
    }
    return "unknown";
}

} // namespace palace
