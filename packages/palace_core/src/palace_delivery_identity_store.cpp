#include "palace_delivery_identity_store.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
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

constexpr char kRecordFileName[] = "delivery-identity-v1";
constexpr char kRegistrationRecordFileName[] =
    "delivery-identity-registration-v1";
constexpr char kRegistrationRecordHeader[] =
    "logos-palace-delivery-identity-registration-v1";
constexpr std::size_t kMaximumRegistrationRecordBytes = 768U;
constexpr std::array<unsigned char, 8> kRecordMagic{{
    'P', 'A', 'L', 'I', 'D', '0', '0', '1',
}};
constexpr std::uint32_t kRecordVersion = 1U;
constexpr std::size_t kMaximumAccountIdBytes = 64U;
constexpr std::size_t kMaximumDisplayNameBytes = 48U;
constexpr std::size_t kPrivateSeedBytes = 32U;
constexpr std::size_t kPublicKeyBytes = 32U;
constexpr std::size_t kChecksumBytes = 32U;
constexpr std::size_t kFixedRecordBytes =
    kRecordMagic.size() + sizeof(std::uint32_t)
    + 2U * sizeof(std::uint16_t) + sizeof(std::uint64_t)
    + kPrivateSeedBytes + kPublicKeyBytes + kChecksumBytes;
constexpr std::size_t kMaximumRecordBytes =
    kFixedRecordBytes + kMaximumAccountIdBytes + kMaximumDisplayNameBytes;

struct PkeyDeleter {
    void operator()(EVP_PKEY* value) const
    {
        EVP_PKEY_free(value);
    }
};

struct PkeyContextDeleter {
    void operator()(EVP_PKEY_CTX* value) const
    {
        EVP_PKEY_CTX_free(value);
    }
};

struct MdContextDeleter {
    void operator()(EVP_MD_CTX* value) const
    {
        EVP_MD_CTX_free(value);
    }
};

class SensitiveBytes {
public:
    ~SensitiveBytes()
    {
        if (!bytes.empty())
            OPENSSL_cleanse(bytes.data(), bytes.size());
    }

    std::vector<unsigned char> bytes;
};

struct SensitiveMaterial {
    ~SensitiveMaterial()
    {
        OPENSSL_cleanse(privateSeed.data(), privateSeed.size());
        OPENSSL_cleanse(publicKey.data(), publicKey.size());
    }

    DeliveryIdentityMetadataV1 metadata;
    std::array<unsigned char, kPrivateSeedBytes> privateSeed{};
    std::array<unsigned char, kPublicKeyBytes> publicKey{};
};

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
        if (this == &other)
            return *this;
        if (value_ >= 0)
            ::close(value_);
        value_ = other.value_;
        other.value_ = -1;
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
#endif

bool isAsciiIdentifier(const std::string& value)
{
    return !value.empty() && value.size() <= kMaximumAccountIdBytes
        && std::all_of(
            value.begin(), value.end(), [](const unsigned char character) {
                return (character >= '0' && character <= '9')
                    || (character >= 'A' && character <= 'Z')
                    || (character >= 'a' && character <= 'z')
                    || character == '_' || character == '-';
            });
}

bool decodeUtf8CodePoint(
    const std::string& value,
    std::size_t& cursor,
    std::uint32_t& codePoint)
{
    if (cursor >= value.size())
        return false;
    const auto first = static_cast<unsigned char>(value[cursor]);
    if (first <= 0x7fU) {
        codePoint = first;
        ++cursor;
        return true;
    }

    std::size_t continuationCount = 0U;
    std::uint32_t minimum = 0U;
    if (first >= 0xc2U && first <= 0xdfU) {
        continuationCount = 1U;
        codePoint = first & 0x1fU;
        minimum = 0x80U;
    } else if (first >= 0xe0U && first <= 0xefU) {
        continuationCount = 2U;
        codePoint = first & 0x0fU;
        minimum = 0x800U;
    } else if (first >= 0xf0U && first <= 0xf4U) {
        continuationCount = 3U;
        codePoint = first & 0x07U;
        minimum = 0x10000U;
    } else {
        return false;
    }
    if (continuationCount > value.size() - cursor - 1U)
        return false;
    for (std::size_t index = 0U; index < continuationCount; ++index) {
        const auto continuation =
            static_cast<unsigned char>(value[cursor + index + 1U]);
        if ((continuation & 0xc0U) != 0x80U)
            return false;
        codePoint = (codePoint << 6U) | (continuation & 0x3fU);
    }
    cursor += continuationCount + 1U;
    return codePoint >= minimum && codePoint <= 0x10ffffU
        && !(codePoint >= 0xd800U && codePoint <= 0xdfffU);
}

bool isDisplayName(const std::string& value)
{
    if (value.empty() || value.size() > kMaximumDisplayNameBytes)
        return false;
    std::size_t cursor = 0U;
    while (cursor < value.size()) {
        std::uint32_t codePoint = 0U;
        if (!decodeUtf8CodePoint(value, cursor, codePoint)
            || codePoint <= 0x1fU
            || (codePoint >= 0x7fU && codePoint <= 0x9fU)) {
            return false;
        }
    }
    return true;
}

bool isMetadata(const DeliveryIdentityMetadataV1& metadata)
{
    return isAsciiIdentifier(metadata.accountId)
        && isDisplayName(metadata.displayName)
        && metadata.deliveryKeyEpoch > 0;
}

void appendU16(
    std::vector<unsigned char>& output,
    const std::uint16_t value)
{
    output.push_back(static_cast<unsigned char>(value));
    output.push_back(static_cast<unsigned char>(value >> 8U));
}

void appendU32(
    std::vector<unsigned char>& output,
    const std::uint32_t value)
{
    for (unsigned int shift = 0U; shift < 32U; shift += 8U)
        output.push_back(static_cast<unsigned char>(value >> shift));
}

void appendU64(
    std::vector<unsigned char>& output,
    const std::uint64_t value)
{
    for (unsigned int shift = 0U; shift < 64U; shift += 8U)
        output.push_back(static_cast<unsigned char>(value >> shift));
}

bool readU16(
    const std::vector<unsigned char>& input,
    std::size_t& cursor,
    std::uint16_t& value)
{
    if (input.size() - cursor < sizeof(value))
        return false;
    value = static_cast<std::uint16_t>(input[cursor])
        | static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(input[cursor + 1U]) << 8U);
    cursor += sizeof(value);
    return true;
}

bool readU32(
    const std::vector<unsigned char>& input,
    std::size_t& cursor,
    std::uint32_t& value)
{
    if (input.size() - cursor < sizeof(value))
        return false;
    value = 0U;
    for (unsigned int shift = 0U; shift < 32U; shift += 8U)
        value |= static_cast<std::uint32_t>(input[cursor++]) << shift;
    return true;
}

bool readU64(
    const std::vector<unsigned char>& input,
    std::size_t& cursor,
    std::uint64_t& value)
{
    if (input.size() - cursor < sizeof(value))
        return false;
    value = 0U;
    for (unsigned int shift = 0U; shift < 64U; shift += 8U)
        value |= static_cast<std::uint64_t>(input[cursor++]) << shift;
    return true;
}

bool sha256(
    const unsigned char* bytes,
    const std::size_t size,
    std::array<unsigned char, kChecksumBytes>& digest)
{
    const std::unique_ptr<EVP_MD_CTX, MdContextDeleter> context(
        EVP_MD_CTX_new());
    unsigned int digestSize = 0U;
    return context
        && EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) == 1
        && EVP_DigestUpdate(context.get(), bytes, size) == 1
        && EVP_DigestFinal_ex(
               context.get(), digest.data(), &digestSize)
            == 1
        && digestSize == digest.size();
}

bool derivePublicKey(
    const std::array<unsigned char, kPrivateSeedBytes>& privateSeed,
    std::array<unsigned char, kPublicKeyBytes>& publicKey)
{
    const std::unique_ptr<EVP_PKEY, PkeyDeleter> key(
        EVP_PKEY_new_raw_private_key(
            EVP_PKEY_ED25519,
            nullptr,
            privateSeed.data(),
            privateSeed.size()));
    std::size_t publicKeySize = publicKey.size();
    return key
        && EVP_PKEY_get_raw_public_key(
               key.get(), publicKey.data(), &publicKeySize)
            == 1
        && publicKeySize == publicKey.size();
}

bool generateKey(
    std::array<unsigned char, kPrivateSeedBytes>& privateSeed,
    std::array<unsigned char, kPublicKeyBytes>& publicKey)
{
    const std::unique_ptr<EVP_PKEY_CTX, PkeyContextDeleter> context(
        EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr));
    if (!context || EVP_PKEY_keygen_init(context.get()) != 1)
        return false;

    EVP_PKEY* rawKey = nullptr;
    if (EVP_PKEY_keygen(context.get(), &rawKey) != 1)
        return false;
    const std::unique_ptr<EVP_PKEY, PkeyDeleter> key(rawKey);
    std::size_t privateSeedSize = privateSeed.size();
    std::size_t publicKeySize = publicKey.size();
    return EVP_PKEY_get_raw_private_key(
               key.get(), privateSeed.data(), &privateSeedSize)
            == 1
        && EVP_PKEY_get_raw_public_key(
               key.get(), publicKey.data(), &publicKeySize)
            == 1
        && privateSeedSize == privateSeed.size()
        && publicKeySize == publicKey.size();
}

bool serializeRecord(
    const DeliveryIdentityMetadataV1& metadata,
    const std::array<unsigned char, kPrivateSeedBytes>& privateSeed,
    const std::array<unsigned char, kPublicKeyBytes>& publicKey,
    SensitiveBytes& record)
{
    record.bytes.clear();
    record.bytes.reserve(
        kFixedRecordBytes + metadata.accountId.size()
        + metadata.displayName.size());
    record.bytes.insert(
        record.bytes.end(), kRecordMagic.begin(), kRecordMagic.end());
    appendU32(record.bytes, kRecordVersion);
    appendU16(
        record.bytes,
        static_cast<std::uint16_t>(metadata.accountId.size()));
    appendU16(
        record.bytes,
        static_cast<std::uint16_t>(metadata.displayName.size()));
    appendU64(
        record.bytes,
        static_cast<std::uint64_t>(metadata.deliveryKeyEpoch));
    record.bytes.insert(
        record.bytes.end(), metadata.accountId.begin(), metadata.accountId.end());
    record.bytes.insert(
        record.bytes.end(), metadata.displayName.begin(), metadata.displayName.end());
    record.bytes.insert(
        record.bytes.end(), privateSeed.begin(), privateSeed.end());
    record.bytes.insert(
        record.bytes.end(), publicKey.begin(), publicKey.end());

    std::array<unsigned char, kChecksumBytes> checksum{};
    if (!sha256(record.bytes.data(), record.bytes.size(), checksum))
        return false;
    record.bytes.insert(
        record.bytes.end(), checksum.begin(), checksum.end());
    return record.bytes.size()
        == kFixedRecordBytes + metadata.accountId.size()
            + metadata.displayName.size();
}

DeliveryIdentityOpenStatus parseRecord(
    const SensitiveBytes& record,
    SensitiveMaterial& material)
{
    if (record.bytes.size() < kFixedRecordBytes + 2U
        || record.bytes.size() > kMaximumRecordBytes
        || !std::equal(
            kRecordMagic.begin(), kRecordMagic.end(), record.bytes.begin())) {
        return DeliveryIdentityOpenStatus::InvalidRecord;
    }

    std::size_t cursor = kRecordMagic.size();
    std::uint32_t version = 0U;
    std::uint16_t accountIdSize = 0U;
    std::uint16_t displayNameSize = 0U;
    std::uint64_t deliveryKeyEpoch = 0U;
    if (!readU32(record.bytes, cursor, version)
        || !readU16(record.bytes, cursor, accountIdSize)
        || !readU16(record.bytes, cursor, displayNameSize)
        || !readU64(record.bytes, cursor, deliveryKeyEpoch)
        || version != kRecordVersion
        || accountIdSize == 0U
        || accountIdSize > kMaximumAccountIdBytes
        || displayNameSize == 0U
        || displayNameSize > kMaximumDisplayNameBytes
        || deliveryKeyEpoch
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())
        || record.bytes.size()
            != kFixedRecordBytes
                + static_cast<std::size_t>(accountIdSize)
                + static_cast<std::size_t>(displayNameSize)) {
        return DeliveryIdentityOpenStatus::InvalidRecord;
    }

    const std::size_t checksumOffset =
        record.bytes.size() - kChecksumBytes;
    std::array<unsigned char, kChecksumBytes> checksum{};
    if (!sha256(record.bytes.data(), checksumOffset, checksum))
        return DeliveryIdentityOpenStatus::CryptoError;
    if (CRYPTO_memcmp(
            checksum.data(),
            record.bytes.data() + checksumOffset,
            checksum.size())
        != 0) {
        return DeliveryIdentityOpenStatus::InvalidRecord;
    }

    material.metadata.accountId.assign(
        reinterpret_cast<const char*>(record.bytes.data() + cursor),
        accountIdSize);
    cursor += accountIdSize;
    material.metadata.displayName.assign(
        reinterpret_cast<const char*>(record.bytes.data() + cursor),
        displayNameSize);
    cursor += displayNameSize;
    material.metadata.deliveryKeyEpoch =
        static_cast<std::int64_t>(deliveryKeyEpoch);
    std::copy_n(
        record.bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
        material.privateSeed.size(),
        material.privateSeed.begin());
    cursor += material.privateSeed.size();
    std::copy_n(
        record.bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
        material.publicKey.size(),
        material.publicKey.begin());

    std::array<unsigned char, kPublicKeyBytes> derivedPublicKey{};
    if (!isMetadata(material.metadata)
        || !derivePublicKey(material.privateSeed, derivedPublicKey)) {
        OPENSSL_cleanse(
            derivedPublicKey.data(), derivedPublicKey.size());
        return !isMetadata(material.metadata)
            ? DeliveryIdentityOpenStatus::InvalidRecord
            : DeliveryIdentityOpenStatus::CryptoError;
    }
    const bool publicKeyMatches =
        CRYPTO_memcmp(
            material.publicKey.data(),
            derivedPublicKey.data(),
            derivedPublicKey.size())
        == 0;
    OPENSSL_cleanse(derivedPublicKey.data(), derivedPublicKey.size());
    return publicKeyMatches
        ? DeliveryIdentityOpenStatus::Loaded
        : DeliveryIdentityOpenStatus::InvalidRecord;
}

#if defined(__unix__) || defined(__APPLE__)
bool writeAll(
    const int descriptor,
    const unsigned char* bytes,
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
    unsigned char* bytes,
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

bool reachedEndOfFile(const int descriptor)
{
    unsigned char extra = 0U;
    for (;;) {
        const ssize_t count = ::read(descriptor, &extra, 1U);
        if (count == 0)
            return true;
        if (count < 0 && errno == EINTR)
            continue;
        return false;
    }
}

bool flushDirectory(const fs::path& directory)
{
    int flags = O_RDONLY;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    FileDescriptor descriptor(::open(directory.c_str(), flags));
    return descriptor.get() >= 0 && ::fsync(descriptor.get()) == 0;
}

DeliveryIdentityOpenStatus readRecord(
    const fs::path& path,
    SensitiveBytes& record)
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
    FileDescriptor descriptor(::open(path.c_str(), flags));
    if (descriptor.get() < 0) {
        if (errno == ENOENT)
            return DeliveryIdentityOpenStatus::NotFound;
        if (errno == ELOOP)
            return DeliveryIdentityOpenStatus::InvalidRecord;
        return DeliveryIdentityOpenStatus::IoError;
    }

    struct stat status {};
    if (::fstat(descriptor.get(), &status) != 0)
        return DeliveryIdentityOpenStatus::IoError;
    if (!S_ISREG(status.st_mode))
        return DeliveryIdentityOpenStatus::InvalidRecord;
    if ((status.st_mode & 0777) != (S_IRUSR | S_IWUSR)
        || status.st_uid != ::geteuid()) {
        return DeliveryIdentityOpenStatus::InsecurePermissions;
    }
    if (status.st_size < 0
        || static_cast<std::uintmax_t>(status.st_size)
            > kMaximumRecordBytes) {
        return DeliveryIdentityOpenStatus::InvalidRecord;
    }
    record.bytes.resize(static_cast<std::size_t>(status.st_size));
    return readAll(
               descriptor.get(), record.bytes.data(), record.bytes.size())
            && reachedEndOfFile(descriptor.get())
        ? DeliveryIdentityOpenStatus::Loaded
        : DeliveryIdentityOpenStatus::InvalidRecord;
}

DeliveryIdentityOpenStatus writeRecord(
    const fs::path& directory,
    const SensitiveBytes& record)
{
    const fs::path destination = directory / kRecordFileName;
    std::string temporaryTemplate =
        (directory / "delivery-identity-v1.next.XXXXXX").string();
    std::vector<char> temporaryBytes(
        temporaryTemplate.begin(), temporaryTemplate.end());
    temporaryBytes.push_back('\0');
    FileDescriptor descriptor(::mkstemp(temporaryBytes.data()));
    if (descriptor.get() < 0)
        return DeliveryIdentityOpenStatus::IoError;
    const fs::path temporary(temporaryBytes.data());

    bool accepted = ::fchmod(
                        descriptor.get(), S_IRUSR | S_IWUSR)
            == 0
        && writeAll(
            descriptor.get(), record.bytes.data(), record.bytes.size())
        && ::fsync(descriptor.get()) == 0
        && descriptor.close();
    if (!accepted) {
        ::unlink(temporary.c_str());
        return DeliveryIdentityOpenStatus::IoError;
    }

    if (::link(temporary.c_str(), destination.c_str()) != 0) {
        const int linkError = errno;
        ::unlink(temporary.c_str());
        return linkError == EEXIST
            ? DeliveryIdentityOpenStatus::AlreadyExists
            : DeliveryIdentityOpenStatus::IoError;
    }
    if (::unlink(temporary.c_str()) != 0
        || !flushDirectory(directory)) {
        return DeliveryIdentityOpenStatus::IoError;
    }
    return DeliveryIdentityOpenStatus::Created;
}
#else
DeliveryIdentityOpenStatus readRecord(
    const fs::path& path,
    SensitiveBytes& record)
{
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error) {
        return error == std::errc::no_such_file_or_directory
            ? DeliveryIdentityOpenStatus::NotFound
            : DeliveryIdentityOpenStatus::IoError;
    }
    if (!fs::is_regular_file(status))
        return DeliveryIdentityOpenStatus::InvalidRecord;
    const std::uintmax_t size = fs::file_size(path, error);
    if (error || size > kMaximumRecordBytes)
        return DeliveryIdentityOpenStatus::InvalidRecord;

    std::ifstream input(path, std::ios::binary);
    if (!input)
        return DeliveryIdentityOpenStatus::IoError;
    record.bytes.resize(static_cast<std::size_t>(size));
    input.read(
        reinterpret_cast<char*>(record.bytes.data()),
        static_cast<std::streamsize>(record.bytes.size()));
    return input && input.peek() == std::ifstream::traits_type::eof()
        ? DeliveryIdentityOpenStatus::Loaded
        : DeliveryIdentityOpenStatus::InvalidRecord;
}

DeliveryIdentityOpenStatus writeRecord(
    const fs::path& directory,
    const SensitiveBytes& record)
{
    const fs::path temporary =
        directory / "delivery-identity-v1.next";
    const fs::path destination = directory / kRecordFileName;
    std::error_code error;
    if (fs::exists(destination, error))
        return DeliveryIdentityOpenStatus::AlreadyExists;
    if (error || fs::exists(temporary, error))
        return DeliveryIdentityOpenStatus::IoError;

    {
        std::ofstream output(
            temporary, std::ios::binary | std::ios::trunc);
        output.write(
            reinterpret_cast<const char*>(record.bytes.data()),
            static_cast<std::streamsize>(record.bytes.size()));
        output.flush();
        if (!output) {
            fs::remove(temporary, error);
            return DeliveryIdentityOpenStatus::IoError;
        }
    }
    fs::rename(temporary, destination, error);
    if (error) {
        fs::remove(temporary, error);
        return DeliveryIdentityOpenStatus::IoError;
    }
    return DeliveryIdentityOpenStatus::Created;
}
#endif

std::string encodeHex(
    const unsigned char* bytes,
    const std::size_t size)
{
    constexpr char alphabet[] = "0123456789abcdef";
    std::string encoded(size * 2U, '0');
    for (std::size_t index = 0U; index < size; ++index) {
        encoded[index * 2U] = alphabet[bytes[index] >> 4U];
        encoded[index * 2U + 1U] = alphabet[bytes[index] & 0x0fU];
    }
    return encoded;
}

bool isLowerHex64(const std::string& value)
{
    return value.size() == 64U
        && std::all_of(
            value.begin(), value.end(), [](const unsigned char character) {
                return (character >= '0' && character <= '9')
                    || (character >= 'a' && character <= 'f');
            });
}

const char* registrationPhaseName(
    const DeliveryIdentityRegistrationPhase phase)
{
    switch (phase) {
    case DeliveryIdentityRegistrationPhase::Prepared:
        return "prepared";
    case DeliveryIdentityRegistrationPhase::Pending:
        return "pending";
    case DeliveryIdentityRegistrationPhase::
        SubmittedPendingWalletSave:
        return "submitted_pending_wallet_save";
    case DeliveryIdentityRegistrationPhase::Submitted:
        return "submitted";
    }
    return "invalid";
}

bool validRegistrationRecord(
    const DeliveryIdentityRegistrationRecordV1& record)
{
    if (!isLowerHex64(record.accountId))
        return false;
    switch (record.phase) {
    case DeliveryIdentityRegistrationPhase::Prepared:
    case DeliveryIdentityRegistrationPhase::Pending:
        return record.transactionHash.empty();
    case DeliveryIdentityRegistrationPhase::
        SubmittedPendingWalletSave:
    case DeliveryIdentityRegistrationPhase::Submitted:
        return isLowerHex64(record.transactionHash);
    }
    return false;
}

bool sameRegistrationRecord(
    const DeliveryIdentityRegistrationRecordV1& first,
    const DeliveryIdentityRegistrationRecordV1& second)
{
    return first.accountId == second.accountId
        && first.phase == second.phase
        && first.minimumFinalizedBlockExclusive
            == second.minimumFinalizedBlockExclusive
        && first.transactionHash == second.transactionHash;
}

bool validRegistrationTransition(
    const DeliveryIdentityRegistrationRecordV1& existing,
    const DeliveryIdentityRegistrationRecordV1& next)
{
    if (existing.accountId != next.accountId)
        return false;
    if (existing.minimumFinalizedBlockExclusive
        != next.minimumFinalizedBlockExclusive) {
        return false;
    }
    if (sameRegistrationRecord(existing, next))
        return true;
    switch (existing.phase) {
    case DeliveryIdentityRegistrationPhase::Prepared:
        return next.phase
            == DeliveryIdentityRegistrationPhase::Pending;
    case DeliveryIdentityRegistrationPhase::Pending:
        return next.phase
            == DeliveryIdentityRegistrationPhase::
                SubmittedPendingWalletSave;
    case DeliveryIdentityRegistrationPhase::
        SubmittedPendingWalletSave:
        return next.phase
                == DeliveryIdentityRegistrationPhase::Submitted
            && next.transactionHash == existing.transactionHash;
    case DeliveryIdentityRegistrationPhase::Submitted:
        return false;
    }
    return false;
}

std::string registrationChecksum(const std::string& body)
{
    std::array<unsigned char, kChecksumBytes> checksum{};
    if (!sha256(
            reinterpret_cast<const unsigned char*>(body.data()),
            body.size(),
            checksum)) {
        return {};
    }
    return encodeHex(checksum.data(), checksum.size());
}

std::string serializeRegistrationRecord(
    const DeliveryIdentityRegistrationRecordV1& record)
{
    if (!validRegistrationRecord(record))
        return {};
    const std::string body =
        std::string(kRegistrationRecordHeader) + "\n"
        + "version=2\n"
        + "account_id=" + record.accountId + "\n"
        + "minimum_finalized_block_exclusive="
        + std::to_string(record.minimumFinalizedBlockExclusive) + "\n"
        + "phase=" + registrationPhaseName(record.phase) + "\n"
        + "transaction_hash=" + record.transactionHash + "\n";
    const std::string checksum = registrationChecksum(body);
    return checksum.empty()
        ? std::string{}
        : body + "checksum=" + checksum + "\n";
}

std::vector<std::string> registrationLines(const std::string& encoded)
{
    if (encoded.empty() || encoded.back() != '\n')
        return {};
    std::vector<std::string> lines;
    std::size_t cursor = 0U;
    while (cursor < encoded.size()) {
        const std::size_t next = encoded.find('\n', cursor);
        if (next == std::string::npos || next == cursor)
            return {};
        lines.push_back(encoded.substr(cursor, next - cursor));
        cursor = next + 1U;
    }
    return lines;
}

bool parseRegistrationRecord(
    const std::string& encoded,
    const std::string& expectedAccountId,
    DeliveryIdentityRegistrationRecordV1& record)
{
    if (encoded.size() > kMaximumRegistrationRecordBytes
        || !isLowerHex64(expectedAccountId)) {
        return false;
    }
    const std::vector<std::string> lines =
        registrationLines(encoded);
    const bool versionOne =
        lines.size() == 6U && lines[1] == "version=1";
    const bool versionTwo =
        lines.size() == 7U && lines[1] == "version=2";
    if ((!versionOne && !versionTwo)
        || lines[0] != kRegistrationRecordHeader
        || lines[2].rfind("account_id=", 0U) != 0U
        || (versionTwo
            && lines[3].rfind(
                "minimum_finalized_block_exclusive=", 0U) != 0U)) {
        return false;
    }

    DeliveryIdentityRegistrationRecordV1 candidate;
    candidate.accountId = lines[2].substr(11U);
    const std::size_t phaseLine = versionTwo ? 4U : 3U;
    const std::size_t transactionLine = versionTwo ? 5U : 4U;
    const std::size_t checksumLine = versionTwo ? 6U : 5U;
    if (lines[phaseLine].rfind("phase=", 0U) != 0U
        || lines[transactionLine].rfind(
            "transaction_hash=", 0U) != 0U
        || lines[checksumLine].rfind("checksum=", 0U) != 0U
        || !isLowerHex64(lines[checksumLine].substr(9U))) {
        return false;
    }
    if (versionTwo) {
        const std::string checkpoint = lines[3].substr(34U);
        if (checkpoint.empty()
            || (checkpoint.size() > 1U
                && checkpoint.front() == '0')
            || !std::all_of(
                checkpoint.begin(),
                checkpoint.end(),
                [](const unsigned char character) {
                    return character >= '0' && character <= '9';
                })) {
            return false;
        }
        try {
            std::size_t consumed = 0U;
            candidate.minimumFinalizedBlockExclusive =
                std::stoull(checkpoint, &consumed, 10);
            if (consumed != checkpoint.size())
                return false;
        } catch (...) {
            return false;
        }
    }
    const std::string phase = lines[phaseLine].substr(6U);
    if (phase == "prepared" && versionTwo) {
        candidate.phase = DeliveryIdentityRegistrationPhase::Prepared;
    } else if (phase == "pending") {
        candidate.phase = DeliveryIdentityRegistrationPhase::Pending;
    } else if (phase == "submitted_pending_wallet_save") {
        candidate.phase = DeliveryIdentityRegistrationPhase::
            SubmittedPendingWalletSave;
    } else if (phase == "submitted") {
        candidate.phase = DeliveryIdentityRegistrationPhase::Submitted;
    } else {
        return false;
    }
    candidate.transactionHash =
        lines[transactionLine].substr(17U);
    const std::size_t checksumOffset = encoded.rfind("checksum=");
    if (!validRegistrationRecord(candidate)
        || candidate.accountId != expectedAccountId
        || checksumOffset == std::string::npos
        || registrationChecksum(encoded.substr(0U, checksumOffset))
            != lines[checksumLine].substr(9U)
        || (versionTwo
            && serializeRegistrationRecord(candidate) != encoded)) {
        return false;
    }
    record = std::move(candidate);
    return true;
}

bool canonicalRegistrationRoot(const fs::path& root)
{
    if (!root.is_absolute() || root.lexically_normal() != root)
        return false;
    std::error_code error;
    const fs::path canonical = fs::canonical(root, error);
    return !error && canonical == root;
}

#if defined(__unix__) || defined(__APPLE__)
DeliveryIdentityRegistrationStoreStatus openRegistrationRoot(
    const fs::path& root,
    FileDescriptor& directory)
{
    if (!canonicalRegistrationRoot(root))
        return DeliveryIdentityRegistrationStoreStatus::InvalidArgument;
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
        return DeliveryIdentityRegistrationStoreStatus::IoError;
    struct stat status {};
    if (::fstat(candidate.get(), &status) != 0)
        return DeliveryIdentityRegistrationStoreStatus::IoError;
    if (!S_ISDIR(status.st_mode))
        return DeliveryIdentityRegistrationStoreStatus::InvalidArgument;
    if (status.st_uid != ::geteuid()
        || (status.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        return DeliveryIdentityRegistrationStoreStatus::
            InsecurePermissions;
    }
    directory = std::move(candidate);
    return DeliveryIdentityRegistrationStoreStatus::Loaded;
}

DeliveryIdentityRegistrationStoreStatus readRegistrationRecordAt(
    const int directory,
    const std::string& expectedAccountId,
    DeliveryIdentityRegistrationRecordV1& record)
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
        ::openat(
            directory,
            kRegistrationRecordFileName,
            flags));
    if (descriptor.get() < 0) {
        if (errno == ENOENT)
            return DeliveryIdentityRegistrationStoreStatus::NotFound;
        if (errno == ELOOP)
            return DeliveryIdentityRegistrationStoreStatus::InvalidRecord;
        return DeliveryIdentityRegistrationStoreStatus::IoError;
    }

    struct stat status {};
    if (::fstat(descriptor.get(), &status) != 0)
        return DeliveryIdentityRegistrationStoreStatus::IoError;
    if (!S_ISREG(status.st_mode) || status.st_nlink != 1)
        return DeliveryIdentityRegistrationStoreStatus::InvalidRecord;
    if ((status.st_mode & 07777) != (S_IRUSR | S_IWUSR)
        || status.st_uid != ::geteuid()) {
        return DeliveryIdentityRegistrationStoreStatus::
            InsecurePermissions;
    }
    if (status.st_size <= 0
        || static_cast<std::uintmax_t>(status.st_size)
            > kMaximumRegistrationRecordBytes) {
        return DeliveryIdentityRegistrationStoreStatus::InvalidRecord;
    }

    std::vector<unsigned char> bytes(
        static_cast<std::size_t>(status.st_size));
    if (!readAll(
            descriptor.get(), bytes.data(), bytes.size())
        || !reachedEndOfFile(descriptor.get())) {
        return DeliveryIdentityRegistrationStoreStatus::InvalidRecord;
    }
    const std::string encoded(bytes.begin(), bytes.end());
    DeliveryIdentityRegistrationRecordV1 candidate;
    if (!parseRegistrationRecord(
            encoded, expectedAccountId, candidate)) {
        return DeliveryIdentityRegistrationStoreStatus::InvalidRecord;
    }
    record = std::move(candidate);
    return DeliveryIdentityRegistrationStoreStatus::Loaded;
}

DeliveryIdentityRegistrationStoreStatus readRegistrationRecord(
    const fs::path& root,
    const std::string& expectedAccountId,
    DeliveryIdentityRegistrationRecordV1& record)
{
    FileDescriptor directory;
    const DeliveryIdentityRegistrationStoreStatus opened =
        openRegistrationRoot(root, directory);
    return opened == DeliveryIdentityRegistrationStoreStatus::Loaded
        ? readRegistrationRecordAt(
              directory.get(), expectedAccountId, record)
        : opened;
}

DeliveryIdentityRegistrationStoreStatus writeRegistrationRecord(
    const fs::path& root,
    const DeliveryIdentityRegistrationRecordV1& record)
{
    FileDescriptor directory;
    const DeliveryIdentityRegistrationStoreStatus opened =
        openRegistrationRoot(root, directory);
    if (opened != DeliveryIdentityRegistrationStoreStatus::Loaded)
        return opened;

    DeliveryIdentityRegistrationRecordV1 existing;
    const DeliveryIdentityRegistrationStoreStatus existingStatus =
        readRegistrationRecordAt(
            directory.get(), record.accountId, existing);
    if (existingStatus
        == DeliveryIdentityRegistrationStoreStatus::Loaded) {
        if (!validRegistrationTransition(existing, record)) {
            return DeliveryIdentityRegistrationStoreStatus::
                InvalidArgument;
        }
        if (sameRegistrationRecord(existing, record)) {
            return DeliveryIdentityRegistrationStoreStatus::Saved;
        }
    } else if (existingStatus
               == DeliveryIdentityRegistrationStoreStatus::NotFound) {
        if (record.phase
            != DeliveryIdentityRegistrationPhase::Prepared) {
            return DeliveryIdentityRegistrationStoreStatus::InvalidArgument;
        }
    } else {
        return existingStatus;
    }

    const std::string encoded = serializeRegistrationRecord(record);
    if (encoded.empty())
        return DeliveryIdentityRegistrationStoreStatus::InvalidArgument;
    static std::atomic<std::uint64_t> temporarySequence{0U};
    std::string temporaryName;
    FileDescriptor temporary;
    for (std::size_t attempt = 0U;
         attempt < 128U && temporary.get() < 0;
         ++attempt) {
        temporaryName =
            ".delivery-identity-registration-v1.next."
            + std::to_string(static_cast<unsigned long>(::getpid()))
            + "."
            + std::to_string(
                temporarySequence.fetch_add(
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
                directory.get(),
                temporaryName.c_str(),
                flags,
                S_IRUSR | S_IWUSR));
        if (candidate.get() >= 0) {
            temporary = std::move(candidate);
        } else if (errno != EEXIST) {
            return DeliveryIdentityRegistrationStoreStatus::IoError;
        }
    }
    if (temporary.get() < 0)
        return DeliveryIdentityRegistrationStoreStatus::IoError;

    bool accepted =
        ::fchmod(temporary.get(), S_IRUSR | S_IWUSR) == 0;
    accepted = accepted
        && writeAll(
            temporary.get(),
            reinterpret_cast<const unsigned char*>(encoded.data()),
            encoded.size())
        && ::fsync(temporary.get()) == 0
        && temporary.close();
    if (!accepted) {
        ::unlinkat(
            directory.get(), temporaryName.c_str(), 0);
        return DeliveryIdentityRegistrationStoreStatus::IoError;
    }

    if (::renameat(
            directory.get(),
            temporaryName.c_str(),
            directory.get(),
            kRegistrationRecordFileName)
            != 0
        || ::fsync(directory.get()) != 0) {
        ::unlinkat(
            directory.get(), temporaryName.c_str(), 0);
        return DeliveryIdentityRegistrationStoreStatus::IoError;
    }
    return DeliveryIdentityRegistrationStoreStatus::Saved;
}
#else
DeliveryIdentityRegistrationStoreStatus readRegistrationRecord(
    const fs::path& root,
    const std::string& expectedAccountId,
    DeliveryIdentityRegistrationRecordV1& record)
{
    if (!canonicalRegistrationRoot(root))
        return DeliveryIdentityRegistrationStoreStatus::InvalidArgument;
    const fs::path path = root / kRegistrationRecordFileName;
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error) {
        return error == std::errc::no_such_file_or_directory
            ? DeliveryIdentityRegistrationStoreStatus::NotFound
            : DeliveryIdentityRegistrationStoreStatus::IoError;
    }
    if (!fs::is_regular_file(status))
        return DeliveryIdentityRegistrationStoreStatus::InvalidRecord;
    const std::uintmax_t size = fs::file_size(path, error);
    if (error || size == 0U || size > kMaximumRegistrationRecordBytes)
        return DeliveryIdentityRegistrationStoreStatus::InvalidRecord;
    std::ifstream input(path, std::ios::binary);
    const std::string encoded{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    DeliveryIdentityRegistrationRecordV1 candidate;
    if (!input.eof()
        || !parseRegistrationRecord(
            encoded, expectedAccountId, candidate)) {
        return DeliveryIdentityRegistrationStoreStatus::InvalidRecord;
    }
    record = std::move(candidate);
    return DeliveryIdentityRegistrationStoreStatus::Loaded;
}

DeliveryIdentityRegistrationStoreStatus writeRegistrationRecord(
    const fs::path& root,
    const DeliveryIdentityRegistrationRecordV1& record)
{
    DeliveryIdentityRegistrationRecordV1 existing;
    const DeliveryIdentityRegistrationStoreStatus status =
        readRegistrationRecord(root, record.accountId, existing);
    if (status == DeliveryIdentityRegistrationStoreStatus::Loaded) {
        if (!validRegistrationTransition(existing, record)) {
            return DeliveryIdentityRegistrationStoreStatus::
                InvalidArgument;
        }
        if (sameRegistrationRecord(existing, record)) {
            return DeliveryIdentityRegistrationStoreStatus::Saved;
        }
    } else if (status
               == DeliveryIdentityRegistrationStoreStatus::NotFound) {
        if (record.phase
            != DeliveryIdentityRegistrationPhase::Prepared) {
            return DeliveryIdentityRegistrationStoreStatus::InvalidArgument;
        }
    } else {
        return status;
    }

    const std::string encoded = serializeRegistrationRecord(record);
    if (encoded.empty())
        return DeliveryIdentityRegistrationStoreStatus::InvalidArgument;
    const fs::path temporary =
        root / ".delivery-identity-registration-v1.next";
    const fs::path destination =
        root / kRegistrationRecordFileName;
    std::error_code error;
    {
        std::ofstream output(
            temporary, std::ios::binary | std::ios::trunc);
        output.write(
            encoded.data(),
            static_cast<std::streamsize>(encoded.size()));
        output.flush();
        if (!output) {
            fs::remove(temporary, error);
            return DeliveryIdentityRegistrationStoreStatus::IoError;
        }
    }
    fs::rename(temporary, destination, error);
    if (error) {
        fs::remove(temporary, error);
        return DeliveryIdentityRegistrationStoreStatus::IoError;
    }
    return DeliveryIdentityRegistrationStoreStatus::Saved;
}
#endif

} // namespace

PalaceDeliveryIdentity::~PalaceDeliveryIdentity()
{
    clear();
}

PalaceDeliveryIdentity::PalaceDeliveryIdentity(
    PalaceDeliveryIdentity&& other) noexcept
    : m_metadata(std::move(other.m_metadata))
    , m_publicKey(other.m_publicKey)
    , m_privateSeed(other.m_privateSeed)
    , m_valid(other.m_valid)
{
    other.clear();
}

PalaceDeliveryIdentity& PalaceDeliveryIdentity::operator=(
    PalaceDeliveryIdentity&& other) noexcept
{
    if (this == &other)
        return *this;
    clear();
    m_metadata = std::move(other.m_metadata);
    m_publicKey = other.m_publicKey;
    m_privateSeed = other.m_privateSeed;
    m_valid = other.m_valid;
    other.clear();
    return *this;
}

DeliveryIdentityOpenStatus PalaceDeliveryIdentity::create(
    const std::string& instanceRoot,
    const DeliveryIdentityMetadataV1& metadata,
    PalaceDeliveryIdentity& identity)
{
    if (instanceRoot.empty() || !isMetadata(metadata))
        return DeliveryIdentityOpenStatus::InvalidArgument;

    std::error_code error;
    const fs::path directory(instanceRoot);
    fs::create_directories(directory, error);
    if (error || !fs::is_directory(directory, error) || error)
        return DeliveryIdentityOpenStatus::IoError;

    const fs::path recordPath = directory / kRecordFileName;
    const fs::file_status status = fs::symlink_status(recordPath, error);
    if (!error && status.type() != fs::file_type::not_found)
        return DeliveryIdentityOpenStatus::AlreadyExists;
    if (error && error != std::errc::no_such_file_or_directory)
        return DeliveryIdentityOpenStatus::IoError;

    PalaceDeliveryIdentity candidate;
    candidate.m_metadata = metadata;
    if (!generateKey(candidate.m_privateSeed, candidate.m_publicKey))
        return DeliveryIdentityOpenStatus::CryptoError;
    candidate.m_valid = true;

    SensitiveBytes record;
    if (!serializeRecord(
            candidate.m_metadata,
            candidate.m_privateSeed,
            candidate.m_publicKey,
            record)) {
        return DeliveryIdentityOpenStatus::CryptoError;
    }
    const DeliveryIdentityOpenStatus persisted =
        writeRecord(directory, record);
    if (persisted != DeliveryIdentityOpenStatus::Created)
        return persisted;
    identity = std::move(candidate);
    return DeliveryIdentityOpenStatus::Created;
}

DeliveryIdentityOpenStatus PalaceDeliveryIdentity::load(
    const std::string& instanceRoot,
    PalaceDeliveryIdentity& identity)
{
    if (instanceRoot.empty())
        return DeliveryIdentityOpenStatus::InvalidArgument;

    SensitiveBytes record;
    const DeliveryIdentityOpenStatus read =
        readRecord(fs::path(instanceRoot) / kRecordFileName, record);
    if (read != DeliveryIdentityOpenStatus::Loaded)
        return read;

    SensitiveMaterial material;
    const DeliveryIdentityOpenStatus parsed =
        parseRecord(record, material);
    if (parsed != DeliveryIdentityOpenStatus::Loaded)
        return parsed;

    PalaceDeliveryIdentity candidate;
    candidate.m_metadata = std::move(material.metadata);
    candidate.m_privateSeed = material.privateSeed;
    candidate.m_publicKey = material.publicKey;
    candidate.m_valid = true;
    identity = std::move(candidate);
    return DeliveryIdentityOpenStatus::Loaded;
}

DeliveryIdentityOpenStatus PalaceDeliveryIdentity::openOrCreate(
    const std::string& instanceRoot,
    const DeliveryIdentityMetadataV1& initialMetadata,
    PalaceDeliveryIdentity& identity)
{
    const DeliveryIdentityOpenStatus loaded =
        load(instanceRoot, identity);
    return loaded == DeliveryIdentityOpenStatus::NotFound
        ? create(instanceRoot, initialMetadata, identity)
        : loaded;
}

bool PalaceDeliveryIdentity::valid() const
{
    return m_valid;
}

const DeliveryIdentityMetadataV1& PalaceDeliveryIdentity::metadata() const
{
    return m_metadata;
}

const std::string& PalaceDeliveryIdentity::accountId() const
{
    return m_metadata.accountId;
}

const std::string& PalaceDeliveryIdentity::displayName() const
{
    return m_metadata.displayName;
}

std::int64_t PalaceDeliveryIdentity::deliveryKeyEpoch() const
{
    return m_metadata.deliveryKeyEpoch;
}

std::string PalaceDeliveryIdentity::publicKey() const
{
    return m_valid
        ? encodeHex(m_publicKey.data(), m_publicKey.size())
        : std::string{};
}

std::string PalaceDeliveryIdentity::sign(
    const std::string& canonicalEnvelope) const
{
    if (!m_valid)
        return {};
    const std::unique_ptr<EVP_PKEY, PkeyDeleter> key(
        EVP_PKEY_new_raw_private_key(
            EVP_PKEY_ED25519,
            nullptr,
            m_privateSeed.data(),
            m_privateSeed.size()));
    const std::unique_ptr<EVP_MD_CTX, MdContextDeleter> context(
        EVP_MD_CTX_new());
    if (!key || !context
        || EVP_DigestSignInit(
               context.get(), nullptr, nullptr, nullptr, key.get())
            != 1) {
        return {};
    }

    std::array<unsigned char, 64> signature{};
    std::size_t signatureSize = signature.size();
    if (EVP_DigestSign(
            context.get(),
            signature.data(),
            &signatureSize,
            reinterpret_cast<const unsigned char*>(
                canonicalEnvelope.data()),
            canonicalEnvelope.size())
            != 1
        || signatureSize != signature.size()) {
        OPENSSL_cleanse(signature.data(), signature.size());
        return {};
    }
    const std::string encoded =
        encodeHex(signature.data(), signature.size());
    OPENSSL_cleanse(signature.data(), signature.size());
    return encoded;
}

void PalaceDeliveryIdentity::clear() noexcept
{
    OPENSSL_cleanse(m_privateSeed.data(), m_privateSeed.size());
    OPENSSL_cleanse(m_publicKey.data(), m_publicKey.size());
    m_metadata = {};
    m_valid = false;
}

const char* deliveryIdentityOpenStatusName(
    const DeliveryIdentityOpenStatus status)
{
    switch (status) {
    case DeliveryIdentityOpenStatus::Created: return "created";
    case DeliveryIdentityOpenStatus::Loaded: return "loaded";
    case DeliveryIdentityOpenStatus::NotFound: return "not_found";
    case DeliveryIdentityOpenStatus::AlreadyExists: return "already_exists";
    case DeliveryIdentityOpenStatus::InvalidArgument: return "invalid_argument";
    case DeliveryIdentityOpenStatus::InvalidRecord: return "invalid_record";
    case DeliveryIdentityOpenStatus::InsecurePermissions:
        return "insecure_permissions";
    case DeliveryIdentityOpenStatus::IoError: return "io_error";
    case DeliveryIdentityOpenStatus::CryptoError: return "crypto_error";
    }
    return "unknown";
}

PalaceDeliveryIdentityRegistrationStore::
    PalaceDeliveryIdentityRegistrationStore(
        std::string instanceRoot)
    : m_instanceRoot(
          fs::path(std::move(instanceRoot))
              .lexically_normal()
              .string())
{
}

DeliveryIdentityRegistrationStoreStatus
PalaceDeliveryIdentityRegistrationStore::load(
    const std::string& expectedAccountId,
    DeliveryIdentityRegistrationRecordV1& record) const
{
    if (!isLowerHex64(expectedAccountId)
        || m_instanceRoot.empty()) {
        return DeliveryIdentityRegistrationStoreStatus::InvalidArgument;
    }
    return readRegistrationRecord(
        fs::path(m_instanceRoot), expectedAccountId, record);
}

DeliveryIdentityRegistrationStoreStatus
PalaceDeliveryIdentityRegistrationStore::save(
    const DeliveryIdentityRegistrationRecordV1& record) const
{
    if (!validRegistrationRecord(record)
        || m_instanceRoot.empty()) {
        return DeliveryIdentityRegistrationStoreStatus::InvalidArgument;
    }
    return writeRegistrationRecord(
        fs::path(m_instanceRoot), record);
}

PalaceDeliveryIdentityRegistration::
    PalaceDeliveryIdentityRegistration(
        std::string instanceRoot)
    : m_store(std::move(instanceRoot))
{
}

DeliveryIdentityRegistrationStoreStatus
PalaceDeliveryIdentityRegistration::restore(
    const std::string& accountId)
{
    DeliveryIdentityRegistrationRecordV1 candidate;
    const DeliveryIdentityRegistrationStoreStatus status =
        m_store.load(accountId, candidate);
    m_accountId = accountId;
    m_restored = true;
    if (status == DeliveryIdentityRegistrationStoreStatus::Loaded) {
        m_record = std::move(candidate);
        m_storeHealthy = true;
    } else if (status
               == DeliveryIdentityRegistrationStoreStatus::NotFound) {
        m_record.reset();
        m_storeHealthy = true;
    } else {
        m_record.reset();
        m_storeHealthy = false;
    }
    return status;
}

DeliveryIdentityRegistrationTransitionV1
PalaceDeliveryIdentityRegistration::ensureSubmitted(
    const std::string& accountId,
    const std::uint64_t minimumFinalizedBlockExclusive,
    const RecoveryLookup& recoveryLookup,
    const Submitter& submitter,
    const WalletSaver& walletSaver)
{
    DeliveryIdentityRegistrationTransitionV1 transition;
    if (!isLowerHex64(accountId)
        || !recoveryLookup || !submitter || !walletSaver) {
        transition.reason = "invalid-registration-arguments";
        transition.storeStatus =
            DeliveryIdentityRegistrationStoreStatus::InvalidArgument;
        return transition;
    }

    if (!m_restored || !m_storeHealthy
        || m_accountId != accountId) {
        transition.storeStatus = restore(accountId);
        if (transition.storeStatus
                != DeliveryIdentityRegistrationStoreStatus::Loaded
            && transition.storeStatus
                != DeliveryIdentityRegistrationStoreStatus::NotFound) {
            transition.reason =
                std::string("registration-store-")
                + deliveryIdentityRegistrationStoreStatusName(
                    transition.storeStatus);
            return transition;
        }
    } else {
        transition.storeStatus =
            m_record.has_value()
            ? DeliveryIdentityRegistrationStoreStatus::Loaded
            : DeliveryIdentityRegistrationStoreStatus::NotFound;
    }

    if (ready()) {
        transition.ready = true;
        transition.reason = "submitted";
        return transition;
    }

    if (!m_record.has_value()) {
        DeliveryIdentityRegistrationRecordV1 prepared;
        prepared.accountId = accountId;
        prepared.phase =
            DeliveryIdentityRegistrationPhase::Prepared;
        prepared.minimumFinalizedBlockExclusive =
            minimumFinalizedBlockExclusive;
        transition.storeStatus = m_store.save(prepared);
        if (transition.storeStatus
            != DeliveryIdentityRegistrationStoreStatus::Saved) {
            m_storeHealthy = false;
            transition.reason =
                std::string("registration-store-")
                + deliveryIdentityRegistrationStoreStatusName(
                    transition.storeStatus);
            return transition;
        }
        m_record = std::move(prepared);
    }

    bool submitThisInvocation = false;
    if (m_record->phase
        == DeliveryIdentityRegistrationPhase::Prepared) {
        DeliveryIdentityRegistrationRecordV1 pending = *m_record;
        pending.phase = DeliveryIdentityRegistrationPhase::Pending;
        transition.storeStatus = m_store.save(pending);
        if (transition.storeStatus
            != DeliveryIdentityRegistrationStoreStatus::Saved) {
            m_storeHealthy = false;
            transition.reason =
                std::string("registration-store-")
                + deliveryIdentityRegistrationStoreStatusName(
                    transition.storeStatus);
            return transition;
        }
        m_record = std::move(pending);
        submitThisInvocation = true;
    }

    if (m_record->phase
        == DeliveryIdentityRegistrationPhase::Pending) {
        DeliveryIdentityRegistrationSubmissionV1 submission;
        if (submitThisInvocation) {
            transition.attempted = true;
            submission = submitter();
        } else {
            transition.recoveryAttempted = true;
            const DeliveryIdentityRegistrationRecoveryV1 recovered =
                recoveryLookup();
            if (recovered.outcome
                == DeliveryIdentityRegistrationRecoveryOutcome::
                    Pending) {
                transition.reason = recovered.reason.empty()
                    ? "recovery-pending" : recovered.reason;
                transition.storeStatus =
                    DeliveryIdentityRegistrationStoreStatus::Saved;
                return transition;
            }
            if (recovered.outcome
                == DeliveryIdentityRegistrationRecoveryOutcome::
                    Rejected) {
                transition.reason = recovered.reason.empty()
                    ? "recovery-rejected" : recovered.reason;
                transition.storeStatus =
                    DeliveryIdentityRegistrationStoreStatus::Saved;
                return transition;
            }
            submission.accepted = true;
            submission.transactionHash =
                recovered.transactionHash;
            submission.reason = "recovered";
        }
        if (!submission.accepted) {
            transition.reason = submission.reason.empty()
                ? "registration-rejected" : submission.reason;
            transition.storeStatus =
                DeliveryIdentityRegistrationStoreStatus::Saved;
            return transition;
        }
        if (!isLowerHex64(submission.transactionHash)) {
            transition.reason = "invalid-registration-submission";
            transition.storeStatus =
                DeliveryIdentityRegistrationStoreStatus::Saved;
            return transition;
        }

        DeliveryIdentityRegistrationRecordV1 accepted =
            *m_record;
        accepted.phase =
            DeliveryIdentityRegistrationPhase::
                SubmittedPendingWalletSave;
        accepted.transactionHash = submission.transactionHash;
        transition.storeStatus = m_store.save(accepted);
        if (transition.storeStatus
            != DeliveryIdentityRegistrationStoreStatus::Saved) {
            m_storeHealthy = false;
            transition.reason =
                std::string("registration-store-")
                + deliveryIdentityRegistrationStoreStatusName(
                    transition.storeStatus);
            return transition;
        }
        m_record = std::move(accepted);
    }

    transition.walletSaveAttempted = true;
    if (!walletSaver()) {
        transition.reason = "wallet-save-failed";
        transition.storeStatus =
            DeliveryIdentityRegistrationStoreStatus::Saved;
        return transition;
    }

    DeliveryIdentityRegistrationRecordV1 submitted = *m_record;
    submitted.phase = DeliveryIdentityRegistrationPhase::Submitted;
    transition.storeStatus = m_store.save(submitted);
    if (transition.storeStatus
        != DeliveryIdentityRegistrationStoreStatus::Saved) {
        m_storeHealthy = false;
        transition.reason =
            std::string("registration-store-")
            + deliveryIdentityRegistrationStoreStatusName(
                transition.storeStatus);
        return transition;
    }
    m_record = std::move(submitted);
    m_storeHealthy = true;
    transition.ready = true;
    transition.reason = "submitted";
    return transition;
}

bool PalaceDeliveryIdentityRegistration::ready() const
{
    return m_storeHealthy && m_record.has_value()
        && m_record->phase
            == DeliveryIdentityRegistrationPhase::Submitted
        && isLowerHex64(m_record->transactionHash);
}

bool PalaceDeliveryIdentityRegistration::storeHealthy() const
{
    return m_storeHealthy;
}

std::string PalaceDeliveryIdentityRegistration::phaseName() const
{
    if (!m_restored)
        return "unavailable";
    if (!m_storeHealthy)
        return "invalid";
    return m_record.has_value()
        ? registrationPhaseName(m_record->phase)
        : std::string("missing");
}

std::uint64_t
PalaceDeliveryIdentityRegistration::
    minimumFinalizedBlockExclusive() const
{
    return m_record.has_value()
        ? m_record->minimumFinalizedBlockExclusive : 0U;
}

const std::string&
PalaceDeliveryIdentityRegistration::transactionHash() const
{
    static const std::string empty;
    return m_record.has_value()
        ? m_record->transactionHash : empty;
}

const char* deliveryIdentityRegistrationStoreStatusName(
    const DeliveryIdentityRegistrationStoreStatus status)
{
    switch (status) {
    case DeliveryIdentityRegistrationStoreStatus::Saved:
        return "saved";
    case DeliveryIdentityRegistrationStoreStatus::Loaded:
        return "loaded";
    case DeliveryIdentityRegistrationStoreStatus::NotFound:
        return "not_found";
    case DeliveryIdentityRegistrationStoreStatus::InvalidArgument:
        return "invalid_argument";
    case DeliveryIdentityRegistrationStoreStatus::InvalidRecord:
        return "invalid_record";
    case DeliveryIdentityRegistrationStoreStatus::InsecurePermissions:
        return "insecure_permissions";
    case DeliveryIdentityRegistrationStoreStatus::IoError:
        return "io_error";
    }
    return "unknown";
}

} // namespace palace
