#include "palace_lez_authority_bundle_store.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "palace_lez.h"
#include "palace_sha256.h"

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace palace {
namespace {

namespace fs = std::filesystem;

constexpr char kRecordFileName[] = "lez-authority-bundle-v1";
constexpr char kTemporaryPrefix[] = "lez-authority-bundle-v1.next.";
constexpr std::array<std::uint8_t, 8> kRecordMagic{{
    'P', 'L', 'Z', 'A', 'U', 'T', 'H', '1',
}};
constexpr std::uint32_t kRecordVersion = 1U;
constexpr std::size_t kChecksumBytes = 64U;
constexpr std::size_t kHeaderBytes =
    kRecordMagic.size() + sizeof(std::uint32_t) + sizeof(std::uint32_t);
constexpr std::size_t kMaximumNetworkIdBytes = 128U;
constexpr std::size_t kMaximumAccounts = 512U;
constexpr std::size_t kMaximumAccountJsonBytes = 256U * 1024U;
constexpr std::size_t kMaximumPayloadBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaximumRecordBytes =
    kHeaderBytes + kMaximumPayloadBytes + kChecksumBytes;

std::atomic<std::uint64_t> temporarySequence{0U};

enum class BundleValidation {
    Accepted,
    Invalid,
    BindingMismatch,
    CheckpointMismatch,
};

bool isLowerHex(const std::string& value, const std::size_t size)
{
    return value.size() == size
        && std::all_of(
            value.begin(), value.end(), [](const unsigned char byte) {
                return (byte >= '0' && byte <= '9')
                    || (byte >= 'a' && byte <= 'f');
            });
}

bool isNonzeroHex64(const std::string& value)
{
    return isLowerHex(value, 64U)
        && value != std::string(64U, '0');
}

bool validNetworkId(const std::string& value)
{
    return !value.empty() && value.size() <= kMaximumNetworkIdBytes
        && std::all_of(
            value.begin(), value.end(), [](const unsigned char byte) {
                return byte >= 0x21U && byte <= 0x7eU;
            });
}

bool validScope(const PalaceLezAuthorityBundleScopeV1& scope)
{
    return validNetworkId(scope.networkId)
        && isNonzeroHex64(scope.programIdHex)
        && isNonzeroHex64(scope.rootAccountIdHex);
}

bool equalScope(
    const PalaceLezAuthorityBundleScopeV1& left,
    const PalaceLezAuthorityBundleScopeV1& right)
{
    return left.networkId == right.networkId
        && left.programIdHex == right.programIdHex
        && left.rootAccountIdHex == right.rootAccountIdHex;
}

bool validCheckpoint(
    const PalaceLezAuthorityBundleCheckpointV1& checkpoint)
{
    // The public LEZ explorer defines block_id as canonical chain height.
    return checkpoint.finalizedBlockId > 0U
        && checkpoint.finalizedBlockId
            == checkpoint.finalizedBlockHeight
        && isNonzeroHex64(checkpoint.finalizedBlockHashHex);
}

bool equalCheckpoint(
    const PalaceLezAuthorityBundleCheckpointV1& left,
    const PalaceLezAuthorityBundleCheckpointV1& right)
{
    return left.finalizedBlockId == right.finalizedBlockId
        && left.finalizedBlockHeight == right.finalizedBlockHeight
        && left.finalizedBlockHashHex == right.finalizedBlockHashHex
        && left.lastOrderedActionId == right.lastOrderedActionId;
}

bool validExpectation(
    const PalaceLezAuthorityBundleExpectationV1& expectation)
{
    return validScope(expectation.scope)
        && (!expectation.checkpoint.has_value()
            || validCheckpoint(*expectation.checkpoint));
}

BundleValidation validateBundle(
    const PalaceLezFinalizedAuthorityBundleV1& bundle,
    const PalaceLezAuthorityBundleExpectationV1& expectation)
{
    if (!validScope(bundle.scope))
        return BundleValidation::Invalid;
    if (!equalScope(bundle.scope, expectation.scope))
        return BundleValidation::BindingMismatch;
    if (!validCheckpoint(bundle.checkpoint))
        return BundleValidation::Invalid;
    if (expectation.checkpoint.has_value()
        && !equalCheckpoint(
            bundle.checkpoint, *expectation.checkpoint)) {
        return BundleValidation::CheckpointMismatch;
    }
    if (bundle.accounts.empty()
        || bundle.accounts.size() > kMaximumAccounts) {
        return BundleValidation::Invalid;
    }

    std::set<std::string> accountIds;
    bool foundRoot = false;
    for (const PalaceLezFinalizedAuthorityAccountV1& account :
         bundle.accounts) {
        if (!isNonzeroHex64(account.accountIdHex)
            || account.responseJson.empty()
            || account.responseJson.size() > kMaximumAccountJsonBytes
            || !accountIds.insert(account.accountIdHex).second) {
            return BundleValidation::Invalid;
        }

        const PalaceLezPublicAccountV3 decoded =
            PalaceLezCodec::decodePublicAccount(
                account.responseJson, bundle.scope.programIdHex);
        if (!decoded.accepted)
            return BundleValidation::Invalid;

        if (account.accountIdHex == bundle.scope.rootAccountIdHex) {
            if (foundRoot
                || decoded.recordType
                    != PalaceLezRecordTypeV3::PalaceRoot) {
                return BundleValidation::Invalid;
            }
            const auto* root =
                std::get_if<PalaceLezRootRecordV3>(&decoded.record);
            if (root == nullptr
                || root->lastOrderedActionId
                    != bundle.checkpoint.lastOrderedActionId) {
                return BundleValidation::Invalid;
            }
            foundRoot = true;
        } else if (
            decoded.recordType == PalaceLezRecordTypeV3::PalaceRoot) {
            return BundleValidation::Invalid;
        }
    }
    return foundRoot
        ? BundleValidation::Accepted
        : BundleValidation::Invalid;
}

PalaceLezAuthorityBundleStoreStatus validationStatus(
    const BundleValidation validation,
    const bool loading)
{
    switch (validation) {
    case BundleValidation::Accepted:
        return loading
            ? PalaceLezAuthorityBundleStoreStatus::Loaded
            : PalaceLezAuthorityBundleStoreStatus::Saved;
    case BundleValidation::BindingMismatch:
        return PalaceLezAuthorityBundleStoreStatus::BindingMismatch;
    case BundleValidation::CheckpointMismatch:
        return PalaceLezAuthorityBundleStoreStatus::CheckpointMismatch;
    case BundleValidation::Invalid:
        return loading
            ? PalaceLezAuthorityBundleStoreStatus::InvalidRecord
            : PalaceLezAuthorityBundleStoreStatus::BundleRejected;
    }
    return loading
        ? PalaceLezAuthorityBundleStoreStatus::InvalidRecord
        : PalaceLezAuthorityBundleStoreStatus::BundleRejected;
}

void appendU32(
    std::vector<std::uint8_t>& output,
    const std::uint32_t value)
{
    for (unsigned int shift = 0U; shift < 32U; shift += 8U)
        output.push_back(static_cast<std::uint8_t>(value >> shift));
}

void appendU64(
    std::vector<std::uint8_t>& output,
    const std::uint64_t value)
{
    for (unsigned int shift = 0U; shift < 64U; shift += 8U)
        output.push_back(static_cast<std::uint8_t>(value >> shift));
}

bool appendString(
    std::vector<std::uint8_t>& output,
    const std::string& value)
{
    if (value.size() > static_cast<std::size_t>(
            std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    appendU32(output, static_cast<std::uint32_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
    return output.size() <= kMaximumPayloadBytes;
}

bool readU32(
    const std::vector<std::uint8_t>& input,
    std::size_t& cursor,
    std::uint32_t& value)
{
    if (cursor > input.size()
        || input.size() - cursor < sizeof(std::uint32_t)) {
        return false;
    }
    value = 0U;
    for (unsigned int shift = 0U; shift < 32U; shift += 8U)
        value |= static_cast<std::uint32_t>(input[cursor++]) << shift;
    return true;
}

bool readU64(
    const std::vector<std::uint8_t>& input,
    std::size_t& cursor,
    std::uint64_t& value)
{
    if (cursor > input.size()
        || input.size() - cursor < sizeof(std::uint64_t)) {
        return false;
    }
    value = 0U;
    for (unsigned int shift = 0U; shift < 64U; shift += 8U)
        value |= static_cast<std::uint64_t>(input[cursor++]) << shift;
    return true;
}

bool readString(
    const std::vector<std::uint8_t>& input,
    std::size_t& cursor,
    const std::size_t maximum,
    std::string& value)
{
    std::uint32_t size = 0U;
    if (!readU32(input, cursor, size)
        || size > maximum
        || cursor > input.size()
        || input.size() - cursor < size) {
        return false;
    }
    value.assign(
        input.begin() + static_cast<std::ptrdiff_t>(cursor),
        input.begin()
            + static_cast<std::ptrdiff_t>(
                cursor + static_cast<std::size_t>(size)));
    cursor += static_cast<std::size_t>(size);
    return true;
}

bool serializeBundle(
    const PalaceLezFinalizedAuthorityBundleV1& bundle,
    std::vector<std::uint8_t>& payload)
{
    payload.clear();
    payload.reserve(1024U);
    if (!appendString(payload, bundle.scope.networkId)
        || !appendString(payload, bundle.scope.programIdHex)
        || !appendString(payload, bundle.scope.rootAccountIdHex)) {
        return false;
    }
    appendU64(payload, bundle.checkpoint.finalizedBlockId);
    appendU64(payload, bundle.checkpoint.finalizedBlockHeight);
    if (!appendString(
            payload, bundle.checkpoint.finalizedBlockHashHex)) {
        return false;
    }
    appendU64(payload, bundle.checkpoint.lastOrderedActionId);
    appendU32(
        payload,
        static_cast<std::uint32_t>(bundle.accounts.size()));
    for (const PalaceLezFinalizedAuthorityAccountV1& account :
         bundle.accounts) {
        if (!appendString(payload, account.accountIdHex)
            || !appendString(payload, account.responseJson)) {
            return false;
        }
    }
    return !payload.empty() && payload.size() <= kMaximumPayloadBytes;
}

bool parseBundle(
    const std::vector<std::uint8_t>& payload,
    PalaceLezFinalizedAuthorityBundleV1& bundle)
{
    if (payload.empty() || payload.size() > kMaximumPayloadBytes)
        return false;

    PalaceLezFinalizedAuthorityBundleV1 parsed;
    std::size_t cursor = 0U;
    if (!readString(
            payload,
            cursor,
            kMaximumNetworkIdBytes,
            parsed.scope.networkId)
        || !readString(
            payload, cursor, 64U, parsed.scope.programIdHex)
        || !readString(
            payload, cursor, 64U, parsed.scope.rootAccountIdHex)
        || !readU64(
            payload,
            cursor,
            parsed.checkpoint.finalizedBlockId)
        || !readU64(
            payload,
            cursor,
            parsed.checkpoint.finalizedBlockHeight)
        || !readString(
            payload,
            cursor,
            64U,
            parsed.checkpoint.finalizedBlockHashHex)
        || !readU64(
            payload,
            cursor,
            parsed.checkpoint.lastOrderedActionId)) {
        return false;
    }

    std::uint32_t accountCount = 0U;
    if (!readU32(payload, cursor, accountCount)
        || accountCount == 0U
        || accountCount > kMaximumAccounts) {
        return false;
    }
    parsed.accounts.reserve(accountCount);
    for (std::uint32_t index = 0U; index < accountCount; ++index) {
        PalaceLezFinalizedAuthorityAccountV1 account;
        if (!readString(payload, cursor, 64U, account.accountIdHex)
            || !readString(
                payload,
                cursor,
                kMaximumAccountJsonBytes,
                account.responseJson)) {
            return false;
        }
        parsed.accounts.push_back(std::move(account));
    }
    if (cursor != payload.size())
        return false;
    bundle = std::move(parsed);
    return true;
}

std::string checksum(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t size)
{
    if (size > bytes.size())
        return {};
    return crypto::sha256Hex(std::string(
        reinterpret_cast<const char*>(bytes.data()), size));
}

bool framePayload(
    const std::vector<std::uint8_t>& payload,
    std::vector<std::uint8_t>& record)
{
    if (payload.empty()
        || payload.size() > kMaximumPayloadBytes
        || payload.size() > static_cast<std::size_t>(
            std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }

    record.clear();
    record.reserve(kHeaderBytes + payload.size() + kChecksumBytes);
    record.insert(record.end(), kRecordMagic.begin(), kRecordMagic.end());
    appendU32(record, kRecordVersion);
    appendU32(record, static_cast<std::uint32_t>(payload.size()));
    record.insert(record.end(), payload.begin(), payload.end());
    const std::string digest = checksum(record, record.size());
    if (digest.size() != kChecksumBytes)
        return false;
    record.insert(record.end(), digest.begin(), digest.end());
    return record.size()
        == kHeaderBytes + payload.size() + kChecksumBytes;
}

bool parseRecord(
    const std::vector<std::uint8_t>& record,
    std::vector<std::uint8_t>& payload)
{
    if (record.size() < kHeaderBytes + kChecksumBytes
        || record.size() > kMaximumRecordBytes
        || !std::equal(
            kRecordMagic.begin(), kRecordMagic.end(), record.begin())) {
        return false;
    }

    std::size_t cursor = kRecordMagic.size();
    std::uint32_t version = 0U;
    std::uint32_t payloadSize = 0U;
    if (!readU32(record, cursor, version)
        || !readU32(record, cursor, payloadSize)
        || version != kRecordVersion
        || payloadSize == 0U
        || payloadSize > kMaximumPayloadBytes) {
        return false;
    }
    const std::size_t checksumOffset =
        kHeaderBytes + static_cast<std::size_t>(payloadSize);
    if (record.size() != checksumOffset + kChecksumBytes)
        return false;

    const std::string expected = checksum(record, checksumOffset);
    const std::string actual(
        record.begin() + static_cast<std::ptrdiff_t>(checksumOffset),
        record.end());
    if (expected.size() != kChecksumBytes || actual != expected)
        return false;

    payload.assign(
        record.begin() + static_cast<std::ptrdiff_t>(kHeaderBytes),
        record.begin() + static_cast<std::ptrdiff_t>(checksumOffset));
    return true;
}

PalaceLezAuthorityBundleStoreStatus prepareRoot(
    const fs::path& root,
    const bool create)
{
    if (root.empty())
        return PalaceLezAuthorityBundleStoreStatus::InvalidArgument;

    std::error_code error;
    fs::file_status status = fs::symlink_status(root, error);
    if (error && error != std::errc::no_such_file_or_directory)
        return PalaceLezAuthorityBundleStoreStatus::IoError;
    if (!error && fs::is_symlink(status))
        return PalaceLezAuthorityBundleStoreStatus::UnsafePath;
    if (!error && status.type() != fs::file_type::not_found
        && !fs::is_directory(status)) {
        return PalaceLezAuthorityBundleStoreStatus::UnsafePath;
    }
    if (!create && (error || status.type() == fs::file_type::not_found))
        return PalaceLezAuthorityBundleStoreStatus::NotFound;

    if (create && (error || status.type() == fs::file_type::not_found)) {
        error.clear();
        fs::create_directories(root, error);
        if (error)
            return PalaceLezAuthorityBundleStoreStatus::IoError;
    }

    error.clear();
    status = fs::symlink_status(root, error);
    if (error)
        return PalaceLezAuthorityBundleStoreStatus::IoError;
    if (fs::is_symlink(status) || !fs::is_directory(status))
        return PalaceLezAuthorityBundleStoreStatus::UnsafePath;
    return create
        ? PalaceLezAuthorityBundleStoreStatus::Saved
        : PalaceLezAuthorityBundleStoreStatus::Loaded;
}

std::string temporaryName()
{
#if defined(__unix__) || defined(__APPLE__)
    const std::uint64_t process =
        static_cast<std::uint64_t>(::getpid());
#else
    const std::uint64_t process = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#endif
    const std::uint64_t sequence =
        temporarySequence.fetch_add(1U, std::memory_order_relaxed);
    return std::string(kTemporaryPrefix) + std::to_string(process)
        + "." + std::to_string(sequence);
}

#if defined(__unix__) || defined(__APPLE__)
class FileDescriptor {
public:
    explicit FileDescriptor(const int descriptor = -1)
        : descriptor_(descriptor)
    {
    }

    ~FileDescriptor()
    {
        if (descriptor_ >= 0)
            ::close(descriptor_);
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept
        : descriptor_(std::exchange(other.descriptor_, -1))
    {
    }

    FileDescriptor& operator=(FileDescriptor&& other) noexcept
    {
        if (this == &other)
            return *this;
        if (descriptor_ >= 0)
            ::close(descriptor_);
        descriptor_ = std::exchange(other.descriptor_, -1);
        return *this;
    }

    int get() const
    {
        return descriptor_;
    }

    bool close()
    {
        if (descriptor_ < 0)
            return true;
        const int descriptor = descriptor_;
        descriptor_ = -1;
        return ::close(descriptor) == 0;
    }

private:
    int descriptor_;
};

PalaceLezAuthorityBundleStoreStatus openRoot(
    const fs::path& root,
    FileDescriptor& descriptor)
{
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
    FileDescriptor opened(::open(root.c_str(), flags));
    if (opened.get() < 0) {
        if (errno == ENOENT)
            return PalaceLezAuthorityBundleStoreStatus::NotFound;
        if (errno == ELOOP || errno == ENOTDIR)
            return PalaceLezAuthorityBundleStoreStatus::UnsafePath;
        return PalaceLezAuthorityBundleStoreStatus::IoError;
    }

    struct stat status {};
    if (::fstat(opened.get(), &status) != 0)
        return PalaceLezAuthorityBundleStoreStatus::IoError;
    if (!S_ISDIR(status.st_mode))
        return PalaceLezAuthorityBundleStoreStatus::UnsafePath;
    descriptor = std::move(opened);
    return PalaceLezAuthorityBundleStoreStatus::Loaded;
}

bool writeAll(
    const int descriptor,
    const std::uint8_t* bytes,
    const std::size_t size)
{
    std::size_t written = 0U;
    while (written < size) {
        const ssize_t count =
            ::write(descriptor, bytes + written, size - written);
        if (count > 0) {
            written += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

bool readAll(
    const int descriptor,
    std::uint8_t* bytes,
    const std::size_t size)
{
    std::size_t read = 0U;
    while (read < size) {
        const ssize_t count =
            ::read(descriptor, bytes + read, size - read);
        if (count > 0) {
            read += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

PalaceLezAuthorityBundleStoreStatus validateExistingDestination(
    const int directory)
{
    struct stat status {};
    if (::fstatat(
            directory,
            kRecordFileName,
            &status,
            AT_SYMLINK_NOFOLLOW)
        != 0) {
        return errno == ENOENT
            ? PalaceLezAuthorityBundleStoreStatus::NotFound
            : PalaceLezAuthorityBundleStoreStatus::IoError;
    }
    if (!S_ISREG(status.st_mode) || status.st_nlink != 1)
        return PalaceLezAuthorityBundleStoreStatus::UnsafePath;
    if (status.st_uid != ::geteuid()
        || (status.st_mode & 0777) != (S_IRUSR | S_IWUSR)) {
        return PalaceLezAuthorityBundleStoreStatus::InsecurePermissions;
    }
    return PalaceLezAuthorityBundleStoreStatus::Loaded;
}

PalaceLezAuthorityBundleStoreStatus writeRecord(
    const fs::path& root,
    const std::vector<std::uint8_t>& record)
{
    FileDescriptor directory;
    const PalaceLezAuthorityBundleStoreStatus opened =
        openRoot(root, directory);
    if (opened != PalaceLezAuthorityBundleStoreStatus::Loaded)
        return opened;

    const PalaceLezAuthorityBundleStoreStatus existing =
        validateExistingDestination(directory.get());
    if (existing != PalaceLezAuthorityBundleStoreStatus::Loaded
        && existing != PalaceLezAuthorityBundleStoreStatus::NotFound) {
        return existing;
    }

    const std::string temporary = temporaryName();
    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    FileDescriptor output(::openat(
        directory.get(),
        temporary.c_str(),
        flags,
        S_IRUSR | S_IWUSR));
    if (output.get() < 0) {
        return errno == EEXIST || errno == ELOOP
            ? PalaceLezAuthorityBundleStoreStatus::UnsafePath
            : PalaceLezAuthorityBundleStoreStatus::IoError;
    }

    const bool persisted =
        ::fchmod(output.get(), S_IRUSR | S_IWUSR) == 0
        && writeAll(output.get(), record.data(), record.size())
        && ::fsync(output.get()) == 0
        && output.close();
    if (!persisted) {
        ::unlinkat(directory.get(), temporary.c_str(), 0);
        return PalaceLezAuthorityBundleStoreStatus::IoError;
    }

    if (::renameat(
            directory.get(),
            temporary.c_str(),
            directory.get(),
            kRecordFileName)
            != 0
        || ::fsync(directory.get()) != 0) {
        ::unlinkat(directory.get(), temporary.c_str(), 0);
        return PalaceLezAuthorityBundleStoreStatus::IoError;
    }
    return PalaceLezAuthorityBundleStoreStatus::Saved;
}

PalaceLezAuthorityBundleStoreStatus readRecord(
    const fs::path& root,
    std::vector<std::uint8_t>& record)
{
    FileDescriptor directory;
    const PalaceLezAuthorityBundleStoreStatus opened =
        openRoot(root, directory);
    if (opened != PalaceLezAuthorityBundleStoreStatus::Loaded)
        return opened;

    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    FileDescriptor input(
        ::openat(directory.get(), kRecordFileName, flags));
    if (input.get() < 0) {
        if (errno == ENOENT)
            return PalaceLezAuthorityBundleStoreStatus::NotFound;
        if (errno == ELOOP)
            return PalaceLezAuthorityBundleStoreStatus::UnsafePath;
        return PalaceLezAuthorityBundleStoreStatus::IoError;
    }

    struct stat status {};
    if (::fstat(input.get(), &status) != 0)
        return PalaceLezAuthorityBundleStoreStatus::IoError;
    if (!S_ISREG(status.st_mode) || status.st_nlink != 1)
        return PalaceLezAuthorityBundleStoreStatus::UnsafePath;
    if (status.st_uid != ::geteuid()
        || (status.st_mode & 0777) != (S_IRUSR | S_IWUSR)) {
        return PalaceLezAuthorityBundleStoreStatus::InsecurePermissions;
    }
    if (status.st_size < 0
        || static_cast<std::uintmax_t>(status.st_size)
            > kMaximumRecordBytes) {
        return PalaceLezAuthorityBundleStoreStatus::InvalidRecord;
    }

    record.resize(static_cast<std::size_t>(status.st_size));
    if (!readAll(input.get(), record.data(), record.size()))
        return PalaceLezAuthorityBundleStoreStatus::InvalidRecord;
    std::uint8_t trailing = 0U;
    ssize_t count = -1;
    do {
        count = ::read(input.get(), &trailing, 1U);
    } while (count < 0 && errno == EINTR);
    return count == 0
        ? PalaceLezAuthorityBundleStoreStatus::Loaded
        : PalaceLezAuthorityBundleStoreStatus::InvalidRecord;
}
#else
PalaceLezAuthorityBundleStoreStatus writeRecord(
    const fs::path& root,
    const std::vector<std::uint8_t>& record)
{
    const fs::path destination = root / kRecordFileName;
    std::error_code error;
    const fs::file_status destinationStatus =
        fs::symlink_status(destination, error);
    if (!error && destinationStatus.type() != fs::file_type::not_found
        && !fs::is_regular_file(destinationStatus)) {
        return PalaceLezAuthorityBundleStoreStatus::UnsafePath;
    }
    if (error && error != std::errc::no_such_file_or_directory)
        return PalaceLezAuthorityBundleStoreStatus::IoError;

    const fs::path temporary = root / temporaryName();
    {
        std::ofstream output(
            temporary, std::ios::binary | std::ios::trunc);
        output.write(
            reinterpret_cast<const char*>(record.data()),
            static_cast<std::streamsize>(record.size()));
        output.flush();
        if (!output) {
            fs::remove(temporary, error);
            return PalaceLezAuthorityBundleStoreStatus::IoError;
        }
    }
#if defined(_WIN32)
    if (!::MoveFileExW(
            temporary.c_str(),
            destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(temporary, error);
        return PalaceLezAuthorityBundleStoreStatus::IoError;
    }
#else
    fs::rename(temporary, destination, error);
    if (error) {
        fs::remove(temporary, error);
        return PalaceLezAuthorityBundleStoreStatus::IoError;
    }
#endif
    return PalaceLezAuthorityBundleStoreStatus::Saved;
}

PalaceLezAuthorityBundleStoreStatus readRecord(
    const fs::path& root,
    std::vector<std::uint8_t>& record)
{
    const fs::path path = root / kRecordFileName;
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error) {
        return error == std::errc::no_such_file_or_directory
            ? PalaceLezAuthorityBundleStoreStatus::NotFound
            : PalaceLezAuthorityBundleStoreStatus::IoError;
    }
    if (status.type() == fs::file_type::not_found)
        return PalaceLezAuthorityBundleStoreStatus::NotFound;
    if (fs::is_symlink(status) || !fs::is_regular_file(status))
        return PalaceLezAuthorityBundleStoreStatus::UnsafePath;

    const std::uintmax_t size = fs::file_size(path, error);
    if (error || size > kMaximumRecordBytes)
        return PalaceLezAuthorityBundleStoreStatus::InvalidRecord;
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return PalaceLezAuthorityBundleStoreStatus::IoError;
    record.resize(static_cast<std::size_t>(size));
    input.read(
        reinterpret_cast<char*>(record.data()),
        static_cast<std::streamsize>(record.size()));
    return input && input.peek() == std::ifstream::traits_type::eof()
        ? PalaceLezAuthorityBundleStoreStatus::Loaded
        : PalaceLezAuthorityBundleStoreStatus::InvalidRecord;
}
#endif

} // namespace

PalaceLezAuthorityBundleStore::PalaceLezAuthorityBundleStore(
    std::string instancePersistenceRoot,
    PalaceLezAuthorityBundleExpectationV1 expectation)
    : instancePersistenceRoot_(std::move(instancePersistenceRoot))
    , expectation_(std::move(expectation))
{
}

PalaceLezAuthorityBundleStoreStatus
PalaceLezAuthorityBundleStore::save(
    const PalaceLezFinalizedAuthorityBundleV1& bundle) const
{
    if (instancePersistenceRoot_.empty()
        || !validExpectation(expectation_)) {
        return PalaceLezAuthorityBundleStoreStatus::InvalidArgument;
    }

    const BundleValidation validation =
        validateBundle(bundle, expectation_);
    if (validation != BundleValidation::Accepted)
        return validationStatus(validation, false);

    std::vector<std::uint8_t> payload;
    std::vector<std::uint8_t> record;
    if (!serializeBundle(bundle, payload)
        || !framePayload(payload, record)) {
        return PalaceLezAuthorityBundleStoreStatus::BundleRejected;
    }

    const fs::path root(instancePersistenceRoot_);
    const PalaceLezAuthorityBundleStoreStatus prepared =
        prepareRoot(root, true);
    if (prepared != PalaceLezAuthorityBundleStoreStatus::Saved)
        return prepared;
    return writeRecord(root, record);
}

PalaceLezAuthorityBundleStoreStatus
PalaceLezAuthorityBundleStore::load(
    PalaceLezFinalizedAuthorityBundleV1& bundle) const
{
    if (instancePersistenceRoot_.empty()
        || !validExpectation(expectation_)) {
        return PalaceLezAuthorityBundleStoreStatus::InvalidArgument;
    }

    const fs::path root(instancePersistenceRoot_);
    const PalaceLezAuthorityBundleStoreStatus prepared =
        prepareRoot(root, false);
    if (prepared != PalaceLezAuthorityBundleStoreStatus::Loaded)
        return prepared;

    std::vector<std::uint8_t> record;
    const PalaceLezAuthorityBundleStoreStatus read =
        readRecord(root, record);
    if (read != PalaceLezAuthorityBundleStoreStatus::Loaded)
        return read;

    std::vector<std::uint8_t> payload;
    PalaceLezFinalizedAuthorityBundleV1 parsed;
    if (!parseRecord(record, payload)
        || !parseBundle(payload, parsed)) {
        return PalaceLezAuthorityBundleStoreStatus::InvalidRecord;
    }

    const BundleValidation validation =
        validateBundle(parsed, expectation_);
    if (validation != BundleValidation::Accepted)
        return validationStatus(validation, true);

    bundle = std::move(parsed);
    return PalaceLezAuthorityBundleStoreStatus::Loaded;
}

const char* palaceLezAuthorityBundleStoreStatusName(
    const PalaceLezAuthorityBundleStoreStatus status)
{
    switch (status) {
    case PalaceLezAuthorityBundleStoreStatus::Saved:
        return "saved";
    case PalaceLezAuthorityBundleStoreStatus::Loaded:
        return "loaded";
    case PalaceLezAuthorityBundleStoreStatus::NotFound:
        return "not_found";
    case PalaceLezAuthorityBundleStoreStatus::InvalidArgument:
        return "invalid_argument";
    case PalaceLezAuthorityBundleStoreStatus::BundleRejected:
        return "bundle_rejected";
    case PalaceLezAuthorityBundleStoreStatus::InvalidRecord:
        return "invalid_record";
    case PalaceLezAuthorityBundleStoreStatus::BindingMismatch:
        return "binding_mismatch";
    case PalaceLezAuthorityBundleStoreStatus::CheckpointMismatch:
        return "checkpoint_mismatch";
    case PalaceLezAuthorityBundleStoreStatus::InsecurePermissions:
        return "insecure_permissions";
    case PalaceLezAuthorityBundleStoreStatus::UnsafePath:
        return "unsafe_path";
    case PalaceLezAuthorityBundleStoreStatus::IoError:
        return "io_error";
    }
    return "unknown";
}

} // namespace palace
