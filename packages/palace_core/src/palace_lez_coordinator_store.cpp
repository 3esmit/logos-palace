#include "palace_lez_coordinator_store.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
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

constexpr char kRecordFileName[] = "lez-coordinator-store-v1";
constexpr char kTemporaryPrefix[] = "lez-coordinator-store-v1.next.";
constexpr std::array<std::uint8_t, 8> kRecordMagic{{
    'P', 'L', 'Z', 'S', 'T', 'O', 'R', 'E',
}};
constexpr std::uint32_t kRecordVersion = 1U;
constexpr std::size_t kChecksumBytes = 64U;
constexpr std::size_t kHeaderBytes =
    kRecordMagic.size() + sizeof(std::uint32_t) + sizeof(std::uint32_t);
constexpr std::size_t kMaximumCoordinatorTransactions = 128U;
constexpr std::size_t kMaximumInstructionWords = 1024U;
constexpr std::size_t kMaximumTransactionAccounts = 8U;
constexpr std::size_t kMaximumSnapshotBytes =
    8U
    + kMaximumCoordinatorTransactions
        * (1U + 8U + 8U + 4U * 32U + 2U
           + kMaximumInstructionWords * 4U + 1U
           + kMaximumTransactionAccounts * (32U + 1U));
constexpr std::size_t kMaximumRecordBytes =
    kHeaderBytes + kMaximumSnapshotBytes + kChecksumBytes;

std::atomic<std::uint64_t> temporarySequence{0U};

void appendU32(
    std::vector<std::uint8_t>& output,
    const std::uint32_t value)
{
    for (unsigned int shift = 0U; shift < 32U; shift += 8U)
        output.push_back(static_cast<std::uint8_t>(value >> shift));
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

std::string checksum(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t size)
{
    if (size > bytes.size())
        return {};
    return crypto::sha256Hex(std::string(
        reinterpret_cast<const char*>(bytes.data()),
        size));
}

bool frameSnapshot(
    const PalaceLezCoordinatorSnapshot& snapshot,
    std::vector<std::uint8_t>& record)
{
    if (!snapshot.accepted || snapshot.bytes.empty()
        || snapshot.bytes.size() > kMaximumSnapshotBytes
        || snapshot.bytes.size()
            > static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }

    record.clear();
    record.reserve(kHeaderBytes + snapshot.bytes.size() + kChecksumBytes);
    record.insert(record.end(), kRecordMagic.begin(), kRecordMagic.end());
    appendU32(record, kRecordVersion);
    appendU32(
        record, static_cast<std::uint32_t>(snapshot.bytes.size()));
    record.insert(
        record.end(), snapshot.bytes.begin(), snapshot.bytes.end());
    const std::string digest = checksum(record, record.size());
    if (digest.size() != kChecksumBytes)
        return false;
    record.insert(record.end(), digest.begin(), digest.end());
    return record.size()
        == kHeaderBytes + snapshot.bytes.size() + kChecksumBytes;
}

bool parseRecord(
    const std::vector<std::uint8_t>& record,
    std::vector<std::uint8_t>& snapshot)
{
    if (record.size() < kHeaderBytes + kChecksumBytes
        || record.size() > kMaximumRecordBytes
        || !std::equal(
            kRecordMagic.begin(), kRecordMagic.end(), record.begin())) {
        return false;
    }

    std::size_t cursor = kRecordMagic.size();
    std::uint32_t version = 0U;
    std::uint32_t snapshotSize = 0U;
    if (!readU32(record, cursor, version)
        || !readU32(record, cursor, snapshotSize)
        || version != kRecordVersion
        || snapshotSize == 0U
        || snapshotSize > kMaximumSnapshotBytes) {
        return false;
    }
    const std::size_t payloadSize =
        static_cast<std::size_t>(snapshotSize);
    const std::size_t checksumOffset = kHeaderBytes + payloadSize;
    if (record.size() != checksumOffset + kChecksumBytes)
        return false;

    const std::string expected = checksum(record, checksumOffset);
    const std::string actual(
        record.begin() + static_cast<std::ptrdiff_t>(checksumOffset),
        record.end());
    if (expected.size() != kChecksumBytes || actual != expected)
        return false;

    snapshot.assign(
        record.begin() + static_cast<std::ptrdiff_t>(kHeaderBytes),
        record.begin() + static_cast<std::ptrdiff_t>(checksumOffset));
    return true;
}

PalaceLezCoordinatorStoreStatus prepareRoot(
    const fs::path& root,
    const bool create)
{
    if (root.empty())
        return PalaceLezCoordinatorStoreStatus::InvalidArgument;

    std::error_code error;
    fs::file_status status = fs::symlink_status(root, error);
    if (error && error != std::errc::no_such_file_or_directory)
        return PalaceLezCoordinatorStoreStatus::IoError;
    if (!error && fs::is_symlink(status))
        return PalaceLezCoordinatorStoreStatus::UnsafePath;
    if (!error && status.type() != fs::file_type::not_found
        && !fs::is_directory(status)) {
        return PalaceLezCoordinatorStoreStatus::UnsafePath;
    }
    if (!create && (error || status.type() == fs::file_type::not_found))
        return PalaceLezCoordinatorStoreStatus::NotFound;

    if (create && (error || status.type() == fs::file_type::not_found)) {
        error.clear();
        fs::create_directories(root, error);
        if (error)
            return PalaceLezCoordinatorStoreStatus::IoError;
    }

    error.clear();
    status = fs::symlink_status(root, error);
    if (error)
        return PalaceLezCoordinatorStoreStatus::IoError;
    if (fs::is_symlink(status) || !fs::is_directory(status))
        return PalaceLezCoordinatorStoreStatus::UnsafePath;
    return create
        ? PalaceLezCoordinatorStoreStatus::Saved
        : PalaceLezCoordinatorStoreStatus::Loaded;
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

PalaceLezCoordinatorStoreStatus openRoot(
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
            return PalaceLezCoordinatorStoreStatus::NotFound;
        if (errno == ELOOP || errno == ENOTDIR)
            return PalaceLezCoordinatorStoreStatus::UnsafePath;
        return PalaceLezCoordinatorStoreStatus::IoError;
    }

    struct stat status {};
    if (::fstat(opened.get(), &status) != 0)
        return PalaceLezCoordinatorStoreStatus::IoError;
    if (!S_ISDIR(status.st_mode))
        return PalaceLezCoordinatorStoreStatus::UnsafePath;
    descriptor = std::move(opened);
    return PalaceLezCoordinatorStoreStatus::Loaded;
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

PalaceLezCoordinatorStoreStatus validateExistingDestination(
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
            ? PalaceLezCoordinatorStoreStatus::NotFound
            : PalaceLezCoordinatorStoreStatus::IoError;
    }
    if (!S_ISREG(status.st_mode) || status.st_nlink != 1)
        return PalaceLezCoordinatorStoreStatus::UnsafePath;
    if (status.st_uid != ::geteuid()
        || (status.st_mode & 0777) != (S_IRUSR | S_IWUSR)) {
        return PalaceLezCoordinatorStoreStatus::InsecurePermissions;
    }
    return PalaceLezCoordinatorStoreStatus::Loaded;
}

PalaceLezCoordinatorStoreStatus writeRecord(
    const fs::path& root,
    const std::vector<std::uint8_t>& record)
{
    FileDescriptor directory;
    const PalaceLezCoordinatorStoreStatus opened =
        openRoot(root, directory);
    if (opened != PalaceLezCoordinatorStoreStatus::Loaded)
        return opened;

    const PalaceLezCoordinatorStoreStatus existing =
        validateExistingDestination(directory.get());
    if (existing != PalaceLezCoordinatorStoreStatus::Loaded
        && existing != PalaceLezCoordinatorStoreStatus::NotFound) {
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
            ? PalaceLezCoordinatorStoreStatus::UnsafePath
            : PalaceLezCoordinatorStoreStatus::IoError;
    }

    const bool persisted =
        ::fchmod(output.get(), S_IRUSR | S_IWUSR) == 0
        && writeAll(output.get(), record.data(), record.size())
        && ::fsync(output.get()) == 0
        && output.close();
    if (!persisted) {
        ::unlinkat(directory.get(), temporary.c_str(), 0);
        return PalaceLezCoordinatorStoreStatus::IoError;
    }

    if (::renameat(
            directory.get(),
            temporary.c_str(),
            directory.get(),
            kRecordFileName)
            != 0
        || ::fsync(directory.get()) != 0) {
        ::unlinkat(directory.get(), temporary.c_str(), 0);
        return PalaceLezCoordinatorStoreStatus::IoError;
    }
    return PalaceLezCoordinatorStoreStatus::Saved;
}

PalaceLezCoordinatorStoreStatus readRecord(
    const fs::path& root,
    std::vector<std::uint8_t>& record)
{
    FileDescriptor directory;
    const PalaceLezCoordinatorStoreStatus opened =
        openRoot(root, directory);
    if (opened != PalaceLezCoordinatorStoreStatus::Loaded)
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
            return PalaceLezCoordinatorStoreStatus::NotFound;
        if (errno == ELOOP)
            return PalaceLezCoordinatorStoreStatus::UnsafePath;
        return PalaceLezCoordinatorStoreStatus::IoError;
    }

    struct stat status {};
    if (::fstat(input.get(), &status) != 0)
        return PalaceLezCoordinatorStoreStatus::IoError;
    if (!S_ISREG(status.st_mode) || status.st_nlink != 1)
        return PalaceLezCoordinatorStoreStatus::UnsafePath;
    if (status.st_uid != ::geteuid()
        || (status.st_mode & 0777) != (S_IRUSR | S_IWUSR)) {
        return PalaceLezCoordinatorStoreStatus::InsecurePermissions;
    }
    if (status.st_size < 0
        || static_cast<std::uintmax_t>(status.st_size)
            > kMaximumRecordBytes) {
        return PalaceLezCoordinatorStoreStatus::InvalidRecord;
    }

    record.resize(static_cast<std::size_t>(status.st_size));
    if (!readAll(input.get(), record.data(), record.size()))
        return PalaceLezCoordinatorStoreStatus::InvalidRecord;
    std::uint8_t trailing = 0U;
    ssize_t count = -1;
    do {
        count = ::read(input.get(), &trailing, 1U);
    } while (count < 0 && errno == EINTR);
    return count == 0
        ? PalaceLezCoordinatorStoreStatus::Loaded
        : PalaceLezCoordinatorStoreStatus::InvalidRecord;
}
#else
PalaceLezCoordinatorStoreStatus writeRecord(
    const fs::path& root,
    const std::vector<std::uint8_t>& record)
{
    const fs::path destination = root / kRecordFileName;
    std::error_code error;
    const fs::file_status destinationStatus =
        fs::symlink_status(destination, error);
    if (!error && destinationStatus.type() != fs::file_type::not_found
        && !fs::is_regular_file(destinationStatus)) {
        return PalaceLezCoordinatorStoreStatus::UnsafePath;
    }
    if (error && error != std::errc::no_such_file_or_directory)
        return PalaceLezCoordinatorStoreStatus::IoError;

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
            return PalaceLezCoordinatorStoreStatus::IoError;
        }
    }
#if defined(_WIN32)
    if (!::MoveFileExW(
            temporary.c_str(),
            destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(temporary, error);
        return PalaceLezCoordinatorStoreStatus::IoError;
    }
#else
    fs::rename(temporary, destination, error);
    if (error) {
        fs::remove(temporary, error);
        return PalaceLezCoordinatorStoreStatus::IoError;
    }
#endif
    return PalaceLezCoordinatorStoreStatus::Saved;
}

PalaceLezCoordinatorStoreStatus readRecord(
    const fs::path& root,
    std::vector<std::uint8_t>& record)
{
    const fs::path path = root / kRecordFileName;
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error) {
        return error == std::errc::no_such_file_or_directory
            ? PalaceLezCoordinatorStoreStatus::NotFound
            : PalaceLezCoordinatorStoreStatus::IoError;
    }
    if (status.type() == fs::file_type::not_found)
        return PalaceLezCoordinatorStoreStatus::NotFound;
    if (fs::is_symlink(status) || !fs::is_regular_file(status))
        return PalaceLezCoordinatorStoreStatus::UnsafePath;

    const std::uintmax_t size = fs::file_size(path, error);
    if (error || size > kMaximumRecordBytes)
        return PalaceLezCoordinatorStoreStatus::InvalidRecord;
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return PalaceLezCoordinatorStoreStatus::IoError;
    record.resize(static_cast<std::size_t>(size));
    input.read(
        reinterpret_cast<char*>(record.data()),
        static_cast<std::streamsize>(record.size()));
    return input && input.peek() == std::ifstream::traits_type::eof()
        ? PalaceLezCoordinatorStoreStatus::Loaded
        : PalaceLezCoordinatorStoreStatus::InvalidRecord;
}
#endif

} // namespace

PalaceLezCoordinatorStore::PalaceLezCoordinatorStore(
    std::string instancePersistenceRoot)
    : instancePersistenceRoot_(std::move(instancePersistenceRoot))
{
}

PalaceLezCoordinatorStoreStatus PalaceLezCoordinatorStore::save(
    const PalaceLezTransactionCoordinator& coordinator) const
{
    if (instancePersistenceRoot_.empty())
        return PalaceLezCoordinatorStoreStatus::InvalidArgument;

    const PalaceLezCoordinatorSnapshot snapshot = coordinator.snapshot();
    std::vector<std::uint8_t> record;
    if (!frameSnapshot(snapshot, record))
        return PalaceLezCoordinatorStoreStatus::CoordinatorRejected;

    const fs::path root(instancePersistenceRoot_);
    const PalaceLezCoordinatorStoreStatus prepared =
        prepareRoot(root, true);
    if (prepared != PalaceLezCoordinatorStoreStatus::Saved)
        return prepared;
    return writeRecord(root, record);
}

PalaceLezCoordinatorStoreStatus PalaceLezCoordinatorStore::load(
    PalaceLezTransactionCoordinator& coordinator) const
{
    if (instancePersistenceRoot_.empty())
        return PalaceLezCoordinatorStoreStatus::InvalidArgument;

    const fs::path root(instancePersistenceRoot_);
    const PalaceLezCoordinatorStoreStatus prepared =
        prepareRoot(root, false);
    if (prepared != PalaceLezCoordinatorStoreStatus::Loaded)
        return prepared;

    std::vector<std::uint8_t> record;
    const PalaceLezCoordinatorStoreStatus read =
        readRecord(root, record);
    if (read != PalaceLezCoordinatorStoreStatus::Loaded)
        return read;

    std::vector<std::uint8_t> snapshot;
    if (!parseRecord(record, snapshot))
        return PalaceLezCoordinatorStoreStatus::InvalidRecord;

    PalaceLezTransactionCoordinator validated;
    if (!validated.restore(snapshot).accepted)
        return PalaceLezCoordinatorStoreStatus::InvalidRecord;
    return coordinator.restore(snapshot).accepted
        ? PalaceLezCoordinatorStoreStatus::Loaded
        : PalaceLezCoordinatorStoreStatus::CoordinatorRejected;
}

const char* palaceLezCoordinatorStoreStatusName(
    const PalaceLezCoordinatorStoreStatus status)
{
    switch (status) {
    case PalaceLezCoordinatorStoreStatus::Saved:
        return "saved";
    case PalaceLezCoordinatorStoreStatus::Loaded:
        return "loaded";
    case PalaceLezCoordinatorStoreStatus::NotFound:
        return "not_found";
    case PalaceLezCoordinatorStoreStatus::InvalidArgument:
        return "invalid_argument";
    case PalaceLezCoordinatorStoreStatus::InvalidRecord:
        return "invalid_record";
    case PalaceLezCoordinatorStoreStatus::InsecurePermissions:
        return "insecure_permissions";
    case PalaceLezCoordinatorStoreStatus::UnsafePath:
        return "unsafe_path";
    case PalaceLezCoordinatorStoreStatus::IoError:
        return "io_error";
    case PalaceLezCoordinatorStoreStatus::CoordinatorRejected:
        return "coordinator_rejected";
    }
    return "unknown";
}

} // namespace palace
