#include "palace_storage_mvp_catalog_store.h"

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

#include "palace_sha256.h"
#include "palace_storage_cid.h"

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

constexpr char kRecordFileName[] = "storage-mvp-catalog-v1";
constexpr char kTemporaryPrefix[] = "storage-mvp-catalog-v1.next.";
constexpr std::array<std::uint8_t, 8> kRecordMagic{{
    'P',
    'L',
    'Z',
    'C',
    'A',
    'T',
    '0',
    '1',
}};
constexpr std::uint32_t kRecordVersion = 1U;
constexpr std::size_t kChecksumBytes = 64U;
constexpr std::size_t kHeaderBytes =
    kRecordMagic.size() + sizeof(std::uint32_t) + sizeof(std::uint32_t);
constexpr std::size_t kMaximumNetworkIdBytes = 128U;
constexpr std::size_t kMaximumRootManifestCidBytes = 128U;
constexpr std::size_t kMaximumPayloadBytes =
    PalaceStorageMvpCatalogStore::MaximumCatalogBytes + 1024U;
constexpr std::size_t kMaximumRecordBytes =
    kHeaderBytes + kMaximumPayloadBytes + kChecksumBytes;

std::atomic<std::uint64_t> temporarySequence{0U};

bool isLowerHex(const std::string &value, const std::size_t size) {
  return value.size() == size &&
         std::all_of(value.begin(), value.end(), [](const unsigned char byte) {
           return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
         });
}

bool isNonzeroHex64(const std::string &value) {
  return isLowerHex(value, 64U) && value != std::string(64U, '0');
}

bool validNetworkId(const std::string &value) {
  return !value.empty() && value.size() <= kMaximumNetworkIdBytes &&
         std::all_of(value.begin(), value.end(), [](const unsigned char byte) {
           return byte >= 0x21U && byte <= 0x7eU;
         });
}

bool validRootManifestCid(const std::string &value) {
  std::string digest;
  return value.size() <= kMaximumRootManifestCidBytes &&
         canonicalStorageCidSha256(value, digest) && isLowerHex(digest, 64U);
}

bool validBinding(const PalaceStorageMvpCatalogBindingV1 &binding) {
  return validNetworkId(binding.networkId) &&
         isNonzeroHex64(binding.programIdHex) &&
         isNonzeroHex64(binding.rootAccountIdHex) &&
         isNonzeroHex64(binding.finalizedHash) &&
         validRootManifestCid(binding.rootManifestCid);
}

bool equalBinding(const PalaceStorageMvpCatalogBindingV1 &left,
                  const PalaceStorageMvpCatalogBindingV1 &right) {
  return left.networkId == right.networkId &&
         left.programIdHex == right.programIdHex &&
         left.rootAccountIdHex == right.rootAccountIdHex &&
         left.finalizedCheckpoint == right.finalizedCheckpoint &&
         left.finalizedHash == right.finalizedHash &&
         left.rootManifestCid == right.rootManifestCid;
}

bool equalLocalCommittedBinding(
    const PalaceStorageMvpCatalogBindingV1 &stored,
    const PalaceStorageMvpCatalogBindingV1 &current) {
  if (stored.networkId != current.networkId ||
      stored.programIdHex != current.programIdHex ||
      stored.rootAccountIdHex != current.rootAccountIdHex ||
      stored.rootManifestCid != current.rootManifestCid ||
      stored.finalizedCheckpoint > current.finalizedCheckpoint) {
    return false;
  }
  // Equal heights are not harmless advancement. Retain the normal exact
  // hash fence so a competing same-height local snapshot cannot reuse the
  // catalog.
  return stored.finalizedCheckpoint != current.finalizedCheckpoint ||
         stored.finalizedHash == current.finalizedHash;
}

bool validCanonicalCatalog(const std::string &catalog) {
  if (catalog.empty() ||
      catalog.size() >= PalaceStorageMvpCatalogStore::MaximumCatalogBytes ||
      catalog.back() != '\n') {
    return false;
  }

  std::size_t lineStart = 0U;
  for (std::size_t index = 0U; index < catalog.size(); ++index) {
    const unsigned char byte = static_cast<unsigned char>(catalog[index]);
    if (byte == '\n') {
      if (index == lineStart)
        return false;
      lineStart = index + 1U;
    } else if (byte < 0x20U || byte > 0x7eU) {
      return false;
    }
  }
  return lineStart == catalog.size();
}

std::string catalogChecksum(const std::string &catalog) {
  return crypto::sha256Hex(catalog);
}

bool validRecord(const PalaceStorageMvpCatalogRecordV1 &record) {
  return validBinding(record.binding) &&
         validCanonicalCatalog(record.canonicalCatalog) &&
         isLowerHex(record.catalogChecksumHex, kChecksumBytes) &&
         record.catalogChecksumHex == catalogChecksum(record.canonicalCatalog);
}

void appendU32(std::vector<std::uint8_t> &output, const std::uint32_t value) {
  for (unsigned int shift = 0U; shift < 32U; shift += 8U)
    output.push_back(static_cast<std::uint8_t>(value >> shift));
}

void appendU64(std::vector<std::uint8_t> &output, const std::uint64_t value) {
  for (unsigned int shift = 0U; shift < 64U; shift += 8U)
    output.push_back(static_cast<std::uint8_t>(value >> shift));
}

bool appendString(std::vector<std::uint8_t> &output, const std::string &value) {
  if (value.size() >
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    return false;
  }
  appendU32(output, static_cast<std::uint32_t>(value.size()));
  output.insert(output.end(), value.begin(), value.end());
  return output.size() <= kMaximumPayloadBytes;
}

bool readU32(const std::vector<std::uint8_t> &input, std::size_t &cursor,
             std::uint32_t &value) {
  if (cursor > input.size() || input.size() - cursor < sizeof(std::uint32_t)) {
    return false;
  }
  value = 0U;
  for (unsigned int shift = 0U; shift < 32U; shift += 8U)
    value |= static_cast<std::uint32_t>(input[cursor++]) << shift;
  return true;
}

bool readU64(const std::vector<std::uint8_t> &input, std::size_t &cursor,
             std::uint64_t &value) {
  if (cursor > input.size() || input.size() - cursor < sizeof(std::uint64_t)) {
    return false;
  }
  value = 0U;
  for (unsigned int shift = 0U; shift < 64U; shift += 8U)
    value |= static_cast<std::uint64_t>(input[cursor++]) << shift;
  return true;
}

bool readString(const std::vector<std::uint8_t> &input, std::size_t &cursor,
                const std::size_t maximum, std::string &value) {
  std::uint32_t size = 0U;
  if (!readU32(input, cursor, size) || size > maximum ||
      cursor > input.size() || input.size() - cursor < size) {
    return false;
  }
  value.assign(input.begin() + static_cast<std::ptrdiff_t>(cursor),
               input.begin() + static_cast<std::ptrdiff_t>(
                                   cursor + static_cast<std::size_t>(size)));
  cursor += static_cast<std::size_t>(size);
  return true;
}

bool serializeRecord(const PalaceStorageMvpCatalogRecordV1 &record,
                     std::vector<std::uint8_t> &payload) {
  if (!validRecord(record))
    return false;

  payload.clear();
  payload.reserve(record.canonicalCatalog.size() + 512U);
  if (!appendString(payload, record.binding.networkId) ||
      !appendString(payload, record.binding.programIdHex) ||
      !appendString(payload, record.binding.rootAccountIdHex)) {
    return false;
  }
  appendU64(payload, record.binding.finalizedCheckpoint);
  if (!appendString(payload, record.binding.finalizedHash) ||
      !appendString(payload, record.binding.rootManifestCid) ||
      !appendString(payload, record.canonicalCatalog) ||
      !appendString(payload, record.catalogChecksumHex)) {
    return false;
  }
  return !payload.empty() && payload.size() <= kMaximumPayloadBytes;
}

bool parseRecordPayload(const std::vector<std::uint8_t> &payload,
                        PalaceStorageMvpCatalogRecordV1 &record) {
  if (payload.empty() || payload.size() > kMaximumPayloadBytes)
    return false;

  PalaceStorageMvpCatalogRecordV1 parsed;
  std::size_t cursor = 0U;
  if (!readString(payload, cursor, kMaximumNetworkIdBytes,
                  parsed.binding.networkId) ||
      !readString(payload, cursor, 64U, parsed.binding.programIdHex) ||
      !readString(payload, cursor, 64U, parsed.binding.rootAccountIdHex) ||
      !readU64(payload, cursor, parsed.binding.finalizedCheckpoint) ||
      !readString(payload, cursor, 64U, parsed.binding.finalizedHash) ||
      !readString(payload, cursor, kMaximumRootManifestCidBytes,
                  parsed.binding.rootManifestCid) ||
      !readString(payload, cursor,
                  PalaceStorageMvpCatalogStore::MaximumCatalogBytes,
                  parsed.canonicalCatalog) ||
      !readString(payload, cursor, kChecksumBytes, parsed.catalogChecksumHex) ||
      cursor != payload.size() || !validRecord(parsed)) {
    return false;
  }
  record = std::move(parsed);
  return true;
}

std::string checksum(const std::vector<std::uint8_t> &bytes,
                     const std::size_t size) {
  if (size > bytes.size())
    return {};
  return crypto::sha256Hex(
      std::string(reinterpret_cast<const char *>(bytes.data()), size));
}

bool framePayload(const std::vector<std::uint8_t> &payload,
                  std::vector<std::uint8_t> &record) {
  if (payload.empty() || payload.size() > kMaximumPayloadBytes ||
      payload.size() >
          static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
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
  return record.size() == kHeaderBytes + payload.size() + kChecksumBytes;
}

bool parseFramedRecord(const std::vector<std::uint8_t> &record,
                       std::vector<std::uint8_t> &payload) {
  if (record.size() < kHeaderBytes + kChecksumBytes ||
      record.size() > kMaximumRecordBytes ||
      !std::equal(kRecordMagic.begin(), kRecordMagic.end(), record.begin())) {
    return false;
  }

  std::size_t cursor = kRecordMagic.size();
  std::uint32_t version = 0U;
  std::uint32_t payloadSize = 0U;
  if (!readU32(record, cursor, version) ||
      !readU32(record, cursor, payloadSize) || version != kRecordVersion ||
      payloadSize == 0U || payloadSize > kMaximumPayloadBytes) {
    return false;
  }
  const std::size_t checksumOffset =
      kHeaderBytes + static_cast<std::size_t>(payloadSize);
  if (record.size() != checksumOffset + kChecksumBytes)
    return false;

  const std::string expected = checksum(record, checksumOffset);
  const std::string actual(record.begin() +
                               static_cast<std::ptrdiff_t>(checksumOffset),
                           record.end());
  if (expected.size() != kChecksumBytes || actual != expected)
    return false;

  payload.assign(record.begin() + static_cast<std::ptrdiff_t>(kHeaderBytes),
                 record.begin() + static_cast<std::ptrdiff_t>(checksumOffset));
  return true;
}

PalaceStorageMvpCatalogStoreStatus prepareRoot(const fs::path &root,
                                               const bool create) {
  if (root.empty())
    return PalaceStorageMvpCatalogStoreStatus::InvalidArgument;

  std::error_code error;
  fs::file_status status = fs::symlink_status(root, error);
  if (error && error != std::errc::no_such_file_or_directory)
    return PalaceStorageMvpCatalogStoreStatus::IoError;
  if (!error && fs::is_symlink(status))
    return PalaceStorageMvpCatalogStoreStatus::UnsafePath;
  if (!error && status.type() != fs::file_type::not_found &&
      !fs::is_directory(status)) {
    return PalaceStorageMvpCatalogStoreStatus::UnsafePath;
  }
  if (!create && (error || status.type() == fs::file_type::not_found))
    return PalaceStorageMvpCatalogStoreStatus::NotFound;

  if (create && (error || status.type() == fs::file_type::not_found)) {
    error.clear();
    fs::create_directories(root, error);
    if (error)
      return PalaceStorageMvpCatalogStoreStatus::IoError;
  }

  error.clear();
  status = fs::symlink_status(root, error);
  if (error)
    return PalaceStorageMvpCatalogStoreStatus::IoError;
  if (fs::is_symlink(status) || !fs::is_directory(status))
    return PalaceStorageMvpCatalogStoreStatus::UnsafePath;
  return create ? PalaceStorageMvpCatalogStoreStatus::Saved
                : PalaceStorageMvpCatalogStoreStatus::Loaded;
}

std::string temporaryName() {
#if defined(__unix__) || defined(__APPLE__)
  const std::uint64_t process = static_cast<std::uint64_t>(::getpid());
#else
  const std::uint64_t process = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
#endif
  const std::uint64_t sequence =
      temporarySequence.fetch_add(1U, std::memory_order_relaxed);
  return std::string(kTemporaryPrefix) + std::to_string(process) + "." +
         std::to_string(sequence);
}

#if defined(__unix__) || defined(__APPLE__)
class FileDescriptor {
public:
  explicit FileDescriptor(const int descriptor = -1)
      : descriptor_(descriptor) {}

  ~FileDescriptor() {
    if (descriptor_ >= 0)
      ::close(descriptor_);
  }

  FileDescriptor(const FileDescriptor &) = delete;
  FileDescriptor &operator=(const FileDescriptor &) = delete;

  FileDescriptor(FileDescriptor &&other) noexcept
      : descriptor_(std::exchange(other.descriptor_, -1)) {}

  FileDescriptor &operator=(FileDescriptor &&other) noexcept {
    if (this == &other)
      return *this;
    if (descriptor_ >= 0)
      ::close(descriptor_);
    descriptor_ = std::exchange(other.descriptor_, -1);
    return *this;
  }

  int get() const { return descriptor_; }

  bool close() {
    if (descriptor_ < 0)
      return true;
    const int descriptor = descriptor_;
    descriptor_ = -1;
    return ::close(descriptor) == 0;
  }

private:
  int descriptor_;
};

PalaceStorageMvpCatalogStoreStatus openRoot(const fs::path &root,
                                            FileDescriptor &descriptor) {
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
      return PalaceStorageMvpCatalogStoreStatus::NotFound;
    if (errno == ELOOP || errno == ENOTDIR)
      return PalaceStorageMvpCatalogStoreStatus::UnsafePath;
    return PalaceStorageMvpCatalogStoreStatus::IoError;
  }

  struct stat status{};
  if (::fstat(opened.get(), &status) != 0)
    return PalaceStorageMvpCatalogStoreStatus::IoError;
  if (!S_ISDIR(status.st_mode))
    return PalaceStorageMvpCatalogStoreStatus::UnsafePath;
  if (status.st_uid != ::geteuid() ||
      (status.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
    return PalaceStorageMvpCatalogStoreStatus::InsecurePermissions;
  }
  descriptor = std::move(opened);
  return PalaceStorageMvpCatalogStoreStatus::Loaded;
}

bool writeAll(const int descriptor, const std::uint8_t *bytes,
              const std::size_t size) {
  std::size_t written = 0U;
  while (written < size) {
    const ssize_t count = ::write(descriptor, bytes + written, size - written);
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

bool readAll(const int descriptor, std::uint8_t *bytes,
             const std::size_t size) {
  std::size_t read = 0U;
  while (read < size) {
    const ssize_t count = ::read(descriptor, bytes + read, size - read);
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

PalaceStorageMvpCatalogStoreStatus
validateExistingDestination(const int directory) {
  struct stat status{};
  if (::fstatat(directory, kRecordFileName, &status, AT_SYMLINK_NOFOLLOW) !=
      0) {
    return errno == ENOENT ? PalaceStorageMvpCatalogStoreStatus::NotFound
                           : PalaceStorageMvpCatalogStoreStatus::IoError;
  }
  if (!S_ISREG(status.st_mode) || status.st_nlink != 1)
    return PalaceStorageMvpCatalogStoreStatus::UnsafePath;
  if (status.st_uid != ::geteuid() ||
      (status.st_mode & 0777) != (S_IRUSR | S_IWUSR)) {
    return PalaceStorageMvpCatalogStoreStatus::InsecurePermissions;
  }
  return PalaceStorageMvpCatalogStoreStatus::Loaded;
}

PalaceStorageMvpCatalogStoreStatus
writeRecord(const fs::path &root, const std::vector<std::uint8_t> &record) {
  FileDescriptor directory;
  const PalaceStorageMvpCatalogStoreStatus opened = openRoot(root, directory);
  if (opened != PalaceStorageMvpCatalogStoreStatus::Loaded)
    return opened;

  const PalaceStorageMvpCatalogStoreStatus existing =
      validateExistingDestination(directory.get());
  if (existing != PalaceStorageMvpCatalogStoreStatus::Loaded &&
      existing != PalaceStorageMvpCatalogStoreStatus::NotFound) {
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
  FileDescriptor output(
      ::openat(directory.get(), temporary.c_str(), flags, S_IRUSR | S_IWUSR));
  if (output.get() < 0) {
    return errno == EEXIST || errno == ELOOP
               ? PalaceStorageMvpCatalogStoreStatus::UnsafePath
               : PalaceStorageMvpCatalogStoreStatus::IoError;
  }

  const bool persisted = ::fchmod(output.get(), S_IRUSR | S_IWUSR) == 0 &&
                         writeAll(output.get(), record.data(), record.size()) &&
                         ::fsync(output.get()) == 0 && output.close();
  if (!persisted) {
    ::unlinkat(directory.get(), temporary.c_str(), 0);
    return PalaceStorageMvpCatalogStoreStatus::IoError;
  }

  if (::renameat(directory.get(), temporary.c_str(), directory.get(),
                 kRecordFileName) != 0 ||
      ::fsync(directory.get()) != 0) {
    ::unlinkat(directory.get(), temporary.c_str(), 0);
    return PalaceStorageMvpCatalogStoreStatus::IoError;
  }
  return PalaceStorageMvpCatalogStoreStatus::Saved;
}

PalaceStorageMvpCatalogStoreStatus
readRecord(const fs::path &root, std::vector<std::uint8_t> &record) {
  FileDescriptor directory;
  const PalaceStorageMvpCatalogStoreStatus opened = openRoot(root, directory);
  if (opened != PalaceStorageMvpCatalogStoreStatus::Loaded)
    return opened;

  int flags = O_RDONLY;
#ifdef O_CLOEXEC
  flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
  FileDescriptor input(::openat(directory.get(), kRecordFileName, flags));
  if (input.get() < 0) {
    if (errno == ENOENT)
      return PalaceStorageMvpCatalogStoreStatus::NotFound;
    if (errno == ELOOP)
      return PalaceStorageMvpCatalogStoreStatus::UnsafePath;
    return PalaceStorageMvpCatalogStoreStatus::IoError;
  }

  struct stat status{};
  if (::fstat(input.get(), &status) != 0)
    return PalaceStorageMvpCatalogStoreStatus::IoError;
  if (!S_ISREG(status.st_mode) || status.st_nlink != 1)
    return PalaceStorageMvpCatalogStoreStatus::UnsafePath;
  if (status.st_uid != ::geteuid() ||
      (status.st_mode & 0777) != (S_IRUSR | S_IWUSR)) {
    return PalaceStorageMvpCatalogStoreStatus::InsecurePermissions;
  }
  if (status.st_size < 0 ||
      static_cast<std::uintmax_t>(status.st_size) > kMaximumRecordBytes) {
    return PalaceStorageMvpCatalogStoreStatus::InvalidRecord;
  }

  record.resize(static_cast<std::size_t>(status.st_size));
  if (!readAll(input.get(), record.data(), record.size()))
    return PalaceStorageMvpCatalogStoreStatus::InvalidRecord;
  std::uint8_t trailing = 0U;
  ssize_t count = -1;
  do {
    count = ::read(input.get(), &trailing, 1U);
  } while (count < 0 && errno == EINTR);
  return count == 0 ? PalaceStorageMvpCatalogStoreStatus::Loaded
                    : PalaceStorageMvpCatalogStoreStatus::InvalidRecord;
}
#else
PalaceStorageMvpCatalogStoreStatus
writeRecord(const fs::path &root, const std::vector<std::uint8_t> &record) {
  const fs::path destination = root / kRecordFileName;
  std::error_code error;
  const fs::file_status destinationStatus =
      fs::symlink_status(destination, error);
  if (!error && destinationStatus.type() != fs::file_type::not_found &&
      !fs::is_regular_file(destinationStatus)) {
    return PalaceStorageMvpCatalogStoreStatus::UnsafePath;
  }
  if (error && error != std::errc::no_such_file_or_directory)
    return PalaceStorageMvpCatalogStoreStatus::IoError;

  const fs::path temporary = root / temporaryName();
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(record.data()),
                 static_cast<std::streamsize>(record.size()));
    output.flush();
    if (!output) {
      fs::remove(temporary, error);
      return PalaceStorageMvpCatalogStoreStatus::IoError;
    }
  }
  fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write,
                  fs::perm_options::replace, error);
  if (error) {
    fs::remove(temporary, error);
    return PalaceStorageMvpCatalogStoreStatus::IoError;
  }
#if defined(_WIN32)
  if (!::MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    fs::remove(temporary, error);
    return PalaceStorageMvpCatalogStoreStatus::IoError;
  }
#else
  fs::rename(temporary, destination, error);
  if (error) {
    fs::remove(temporary, error);
    return PalaceStorageMvpCatalogStoreStatus::IoError;
  }
#endif
  return PalaceStorageMvpCatalogStoreStatus::Saved;
}

PalaceStorageMvpCatalogStoreStatus
readRecord(const fs::path &root, std::vector<std::uint8_t> &record) {
  const fs::path path = root / kRecordFileName;
  std::error_code error;
  const fs::file_status status = fs::symlink_status(path, error);
  if (error) {
    return error == std::errc::no_such_file_or_directory
               ? PalaceStorageMvpCatalogStoreStatus::NotFound
               : PalaceStorageMvpCatalogStoreStatus::IoError;
  }
  if (status.type() == fs::file_type::not_found)
    return PalaceStorageMvpCatalogStoreStatus::NotFound;
  if (fs::is_symlink(status) || !fs::is_regular_file(status))
    return PalaceStorageMvpCatalogStoreStatus::UnsafePath;

  const std::uintmax_t size = fs::file_size(path, error);
  if (error || size > kMaximumRecordBytes)
    return PalaceStorageMvpCatalogStoreStatus::InvalidRecord;
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return PalaceStorageMvpCatalogStoreStatus::IoError;
  record.resize(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char *>(record.data()),
             static_cast<std::streamsize>(record.size()));
  return input && input.peek() == std::ifstream::traits_type::eof()
             ? PalaceStorageMvpCatalogStoreStatus::Loaded
             : PalaceStorageMvpCatalogStoreStatus::InvalidRecord;
}
#endif

enum class CatalogBindingComparison {
  Exact,
  LocalCommitted,
};

PalaceStorageMvpCatalogStoreStatus loadBoundRecord(
    const std::string &instancePersistenceRoot,
    const PalaceStorageMvpCatalogBindingV1 &expectedBinding,
    PalaceStorageMvpCatalogRecordV1 &record,
    const CatalogBindingComparison comparison) {
  if (instancePersistenceRoot.empty() || !validBinding(expectedBinding)) {
    return PalaceStorageMvpCatalogStoreStatus::InvalidArgument;
  }

  const fs::path root(instancePersistenceRoot);
  const PalaceStorageMvpCatalogStoreStatus prepared = prepareRoot(root, false);
  if (prepared != PalaceStorageMvpCatalogStoreStatus::Loaded)
    return prepared;

  std::vector<std::uint8_t> bytes;
  const PalaceStorageMvpCatalogStoreStatus read = readRecord(root, bytes);
  if (read != PalaceStorageMvpCatalogStoreStatus::Loaded)
    return read;

  std::vector<std::uint8_t> payload;
  PalaceStorageMvpCatalogRecordV1 parsed;
  if (!parseFramedRecord(bytes, payload) ||
      !parseRecordPayload(payload, parsed)) {
    return PalaceStorageMvpCatalogStoreStatus::InvalidRecord;
  }
  const bool bindingMatches =
      comparison == CatalogBindingComparison::Exact
          ? equalBinding(parsed.binding, expectedBinding)
          : equalLocalCommittedBinding(parsed.binding, expectedBinding);
  if (!bindingMatches)
    return PalaceStorageMvpCatalogStoreStatus::BindingMismatch;

  record = std::move(parsed);
  return PalaceStorageMvpCatalogStoreStatus::Loaded;
}

} // namespace

PalaceStorageMvpCatalogStore::PalaceStorageMvpCatalogStore(
    std::string instancePersistenceRoot)
    : instancePersistenceRoot_(std::move(instancePersistenceRoot)) {}

PalaceStorageMvpCatalogStoreStatus PalaceStorageMvpCatalogStore::save(
    const PalaceStorageMvpCatalogRecordV1 &record) const {
  if (instancePersistenceRoot_.empty()) {
    return PalaceStorageMvpCatalogStoreStatus::InvalidArgument;
  }
  if (!validRecord(record))
    return PalaceStorageMvpCatalogStoreStatus::CatalogRejected;

  std::vector<std::uint8_t> payload;
  std::vector<std::uint8_t> framed;
  if (!serializeRecord(record, payload) || !framePayload(payload, framed)) {
    return PalaceStorageMvpCatalogStoreStatus::CatalogRejected;
  }

  const fs::path root(instancePersistenceRoot_);
  const PalaceStorageMvpCatalogStoreStatus prepared = prepareRoot(root, true);
  if (prepared != PalaceStorageMvpCatalogStoreStatus::Saved)
    return prepared;
  return writeRecord(root, framed);
}

PalaceStorageMvpCatalogStoreStatus PalaceStorageMvpCatalogStore::load(
    const PalaceStorageMvpCatalogBindingV1 &expectedBinding,
    PalaceStorageMvpCatalogRecordV1 &record) const {
  return loadBoundRecord(instancePersistenceRoot_, expectedBinding, record,
                         CatalogBindingComparison::Exact);
}

PalaceStorageMvpCatalogStoreStatus
PalaceStorageMvpCatalogStore::loadLocalCommitted(
    const PalaceStorageMvpCatalogBindingV1 &expectedBinding,
    PalaceStorageMvpCatalogRecordV1 &record) const {
  return loadBoundRecord(instancePersistenceRoot_, expectedBinding, record,
                         CatalogBindingComparison::LocalCommitted);
}

const char *palaceStorageMvpCatalogStoreStatusName(
    const PalaceStorageMvpCatalogStoreStatus status) {
  switch (status) {
  case PalaceStorageMvpCatalogStoreStatus::Saved:
    return "saved";
  case PalaceStorageMvpCatalogStoreStatus::Loaded:
    return "loaded";
  case PalaceStorageMvpCatalogStoreStatus::NotFound:
    return "not_found";
  case PalaceStorageMvpCatalogStoreStatus::InvalidArgument:
    return "invalid_argument";
  case PalaceStorageMvpCatalogStoreStatus::CatalogRejected:
    return "catalog_rejected";
  case PalaceStorageMvpCatalogStoreStatus::InvalidRecord:
    return "invalid_record";
  case PalaceStorageMvpCatalogStoreStatus::BindingMismatch:
    return "binding_mismatch";
  case PalaceStorageMvpCatalogStoreStatus::InsecurePermissions:
    return "insecure_permissions";
  case PalaceStorageMvpCatalogStoreStatus::UnsafePath:
    return "unsafe_path";
  case PalaceStorageMvpCatalogStoreStatus::IoError:
    return "io_error";
  }
  return "unknown";
}

PalaceStorageMvpCatalogRecoveryAction
palaceStorageMvpCatalogRecoveryAction(
    const PalaceStorageMvpCatalogStoreStatus status) {
  switch (status) {
  case PalaceStorageMvpCatalogStoreStatus::Loaded:
    return PalaceStorageMvpCatalogRecoveryAction::Restore;
  case PalaceStorageMvpCatalogStoreStatus::NotFound:
  case PalaceStorageMvpCatalogStoreStatus::BindingMismatch:
    return PalaceStorageMvpCatalogRecoveryAction::Idle;
  case PalaceStorageMvpCatalogStoreStatus::Saved:
  case PalaceStorageMvpCatalogStoreStatus::InvalidArgument:
  case PalaceStorageMvpCatalogStoreStatus::CatalogRejected:
  case PalaceStorageMvpCatalogStoreStatus::InvalidRecord:
  case PalaceStorageMvpCatalogStoreStatus::InsecurePermissions:
  case PalaceStorageMvpCatalogStoreStatus::UnsafePath:
  case PalaceStorageMvpCatalogStoreStatus::IoError:
    return PalaceStorageMvpCatalogRecoveryAction::Degrade;
  }
  return PalaceStorageMvpCatalogRecoveryAction::Degrade;
}

} // namespace palace
