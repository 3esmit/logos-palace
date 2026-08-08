#include "palace_lez_profile.h"

#include "palace_lez_release_lock.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace palace {
namespace {

namespace fs = std::filesystem;

constexpr char kReleaseProfileId[] = "release";
constexpr char kLocalDevelopmentProfileId[] = "local-development";
constexpr char kProfilesDirectoryName[] = "palace-profiles";
constexpr char kBindingFileName[] = "lez-profile-binding-v1";
constexpr char kBindingHeader[] = "logos-palace-lez-profile-binding-v1";
constexpr char kLegacyMigrationFileName[] =
    "lez-profile-legacy-direct-root-migration-v1";
constexpr char kLegacyMigrationHeader[] =
    "logos-palace-lez-profile-legacy-direct-root-migration-v1";
constexpr std::size_t kMaximumBindingBytes = 2048U;
constexpr std::size_t kMaximumLegacyMigrationBytes = 2048U;
constexpr std::size_t kMaximumHostPathBytes = 4096U;

enum class LegacyDirectRootEntryKind {
    RegularFile,
    Directory,
};

struct LegacyDirectRootEntry {
    const char* name;
    LegacyDirectRootEntryKind kind;
};

// These are durable Palace state locations from the pre-profile layout.
// They may be upgraded only into the release profile. Local development must
// never inherit them because it has a different sequencer trust boundary.
constexpr std::array<LegacyDirectRootEntry, 20> kLegacyDirectRootEntries{{
    {"projection-v1", LegacyDirectRootEntryKind::RegularFile},
    {"action-journal-v1", LegacyDirectRootEntryKind::RegularFile},
    {"action-journal-v2", LegacyDirectRootEntryKind::RegularFile},
    {"delivery-session-v1", LegacyDirectRootEntryKind::RegularFile},
    {"room-transition-v1", LegacyDirectRootEntryKind::RegularFile},
    {"delivery-identity-v1", LegacyDirectRootEntryKind::RegularFile},
    {"delivery-identity-registration-v1", LegacyDirectRootEntryKind::RegularFile},
    {"asset-authoring-v1", LegacyDirectRootEntryKind::RegularFile},
    {"lez-coordinator-store-v1", LegacyDirectRootEntryKind::RegularFile},
    {"lez-authority-bundle-v1", LegacyDirectRootEntryKind::RegularFile},
    {"lez-submission-intent-v1", LegacyDirectRootEntryKind::RegularFile},
    {"storage-mvp-catalog-v1", LegacyDirectRootEntryKind::RegularFile},
    {"lez-wallet-config-v1.json", LegacyDirectRootEntryKind::RegularFile},
    {"lez-wallet-storage-v1.json", LegacyDirectRootEntryKind::RegularFile},
    {"palace-core-vm-turn-v1", LegacyDirectRootEntryKind::RegularFile},
    {"verified_assets", LegacyDirectRootEntryKind::Directory},
    {"storage", LegacyDirectRootEntryKind::Directory},
    {"asset_downloads", LegacyDirectRootEntryKind::Directory},
    {"storage_publications", LegacyDirectRootEntryKind::Directory},
    {"storage_catalog_downloads", LegacyDirectRootEntryKind::Directory},
}};

using LegacyDirectRootSelection =
    std::array<bool, kLegacyDirectRootEntries.size()>;

std::atomic<std::uint64_t> temporarySequence{0U};

bool isLowerHex(const std::string& value, const std::size_t length)
{
    return value.size() == length
        && std::all_of(value.begin(), value.end(), [](const unsigned char byte) {
            return (byte >= '0' && byte <= '9')
                || (byte >= 'a' && byte <= 'f');
        });
}

bool isSafeText(const std::string& value, const std::size_t maximum)
{
    return !value.empty() && value.size() <= maximum
        && std::all_of(value.begin(), value.end(), [](const unsigned char byte) {
            return byte >= 0x21U && byte <= 0x7eU;
        });
}

bool isValidNetwork(const PalaceLezNetworkFingerprint& network)
{
    return isSafeText(network.networkId, 64U)
        && isSafeText(network.moduleApiVersion, 32U)
        && isLowerHex(network.moduleRevision, 40U)
        && isLowerHex(network.runtimeRevision, 40U)
        && isSafeText(network.publicContractVersion, 32U)
        && isLowerHex(network.publicContractRevision, 40U)
        && isLowerHex(network.programIdHex, 64U)
        && network.programIdHex != std::string(64U, '0')
        && isLowerHex(network.programBytecodeSha256Hex, 64U)
        && network.programBytecodeSha256Hex != std::string(64U, '0');
}

bool validProfile(const PalaceLezProfileV1& profile)
{
    return (profile.id == kReleaseProfileId
               || profile.id == kLocalDevelopmentProfileId)
        && isValidNetwork(profile.network)
        && !profile.walletConfigJson.empty()
        && profile.walletConfigJson.size() <= 16U * 1024U
        && isSafeText(profile.expectedModuleName, 64U)
        && isSafeText(profile.expectedSequencerOrigin, 256U);
}

PalaceLezProfileV1 releaseProfile()
{
    PalaceLezProfileV1 profile;
    profile.id = kReleaseProfileId;
    profile.network = PalaceLezReleaseLock::network();
    profile.walletConfigJson = PalaceLezReleaseLock::walletConfigJson();
    profile.expectedModuleName = PalaceLezReleaseLock::expectedModuleName();
    profile.expectedSequencerOrigin =
        PalaceLezReleaseLock::expectedSequencerOrigin();
    profile.publicFinalityAvailable = true;
    return profile;
}

PalaceLezProfileV1 localDevelopmentProfile()
{
    // Local development is deployed by the local E2E runner, so it tracks the
    // current guest image independently of the immutable public release lock.
    // It differs from release in its isolated sequencer origin and explicit
    // absence of public finality.
    PalaceLezProfileV1 profile = releaseProfile();
    profile.id = kLocalDevelopmentProfileId;
    profile.network.networkId = "logos-lez-local-development-v1";
    profile.network.programIdHex =
        "6e0676aa8dd6d9e71d9c31b8af441f2c042f30eaa42c4c8b2219eb0dfdab91f8";
    profile.network.programBytecodeSha256Hex =
        "48f1f18721be9f04f2cc5aca9920c64604c65258c66409b4680def7f00e291d4";
    profile.walletConfigJson =
        "{\"sequencer_addr\":"
        "\"http://127.0.0.1:3040\","
        "\"seq_poll_timeout\":\"2s\","
        "\"seq_tx_poll_max_blocks\":30,"
        "\"seq_poll_max_retries\":10,"
        "\"seq_block_poll_max_amount\":100}\n";
    profile.expectedSequencerOrigin = "http://127.0.0.1:3040";
    profile.publicFinalityAvailable = false;
    return profile;
}

std::string bindingRecord(const PalaceLezProfileV1& profile)
{
    if (!validProfile(profile))
        return {};
    return std::string(kBindingHeader) + "\n"
        + "profile_id=" + profile.id + "\n"
        + "network_id=" + profile.network.networkId + "\n"
        + "module_api_version=" + profile.network.moduleApiVersion + "\n"
        + "module_revision=" + profile.network.moduleRevision + "\n"
        + "runtime_revision=" + profile.network.runtimeRevision + "\n"
        + "public_contract_version=" + profile.network.publicContractVersion + "\n"
        + "public_contract_revision=" + profile.network.publicContractRevision + "\n"
        + "program_id_hex=" + profile.network.programIdHex + "\n"
        + "program_bytecode_sha256_hex="
        + profile.network.programBytecodeSha256Hex + "\n"
        + "module_name=" + profile.expectedModuleName + "\n"
        + "sequencer_origin=" + profile.expectedSequencerOrigin + "\n"
        + "public_finality="
        + (profile.publicFinalityAvailable ? "1\n" : "0\n");
}

PalaceLezProfileBindingResultV1 rejected(
    const PalaceLezProfileBindingStatusV1 status,
    const std::string& reason)
{
    PalaceLezProfileBindingResultV1 result;
    result.status = status;
    result.reason = reason;
    return result;
}

bool safeExistingDirectory(
    const fs::path& path,
    std::error_code& error)
{
    const fs::file_status status = fs::symlink_status(path, error);
    return !error && !fs::is_symlink(status) && fs::is_directory(status);
}

bool ownerWritableOnly(const fs::path& path)
{
#if defined(__unix__) || defined(__APPLE__)
    struct stat metadata{};
    if (::stat(path.c_str(), &metadata) != 0)
        return false;
    return metadata.st_uid == ::geteuid()
        && (metadata.st_mode & (S_IWGRP | S_IWOTH)) == 0;
#else
    static_cast<void>(path);
    return true;
#endif
}

bool ownerReadWriteOnly(const fs::path& path)
{
#if defined(__unix__) || defined(__APPLE__)
    struct stat metadata{};
    if (::stat(path.c_str(), &metadata) != 0)
        return false;
    return metadata.st_uid == ::geteuid()
        && (metadata.st_mode & 0777) == 0600;
#else
    static_cast<void>(path);
    return true;
#endif
}

bool createOwnerOnlyDirectory(const fs::path& path, std::error_code& error)
{
    const fs::file_status before = fs::symlink_status(path, error);
    if (!error && (fs::is_symlink(before) || !fs::is_directory(before)))
        return false;
    if (error && error != std::errc::no_such_file_or_directory)
        return false;

    error.clear();
    const bool exists = fs::exists(path, error);
    if (error || (!exists && !fs::create_directory(path, error)))
        return false;
    if (error || !safeExistingDirectory(path, error))
        return false;
#if defined(__unix__) || defined(__APPLE__)
    if (::chmod(path.c_str(), 0700) != 0)
        return false;
#endif
    return ownerWritableOnly(path);
}

enum class LegacyDirectRootState {
    Absent,
    Present,
    Unsafe,
};

enum class LegacyEntryPresence {
    Missing,
    Present,
    Unsafe,
};

std::optional<std::size_t> legacyEntryIndex(const std::string& name)
{
    for (std::size_t index = 0U; index < kLegacyDirectRootEntries.size();
         ++index) {
        if (name == kLegacyDirectRootEntries[index].name)
            return index;
    }
    return std::nullopt;
}

bool safeLegacyEntry(
    const fs::path& path,
    const LegacyDirectRootEntry& entry)
{
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error || fs::is_symlink(status)
        || (entry.kind == LegacyDirectRootEntryKind::RegularFile
            && !fs::is_regular_file(status))
        || (entry.kind == LegacyDirectRootEntryKind::Directory
            && !fs::is_directory(status))
        || !ownerWritableOnly(path)) {
        return false;
    }
    if (entry.kind != LegacyDirectRootEntryKind::Directory)
        return true;

    fs::recursive_directory_iterator iterator(
        path, fs::directory_options::none, error);
    const fs::recursive_directory_iterator end;
    while (!error && iterator != end) {
        const fs::path child = iterator->path();
        const fs::file_status childStatus = fs::symlink_status(child, error);
        if (error || fs::is_symlink(childStatus)
            || (!fs::is_regular_file(childStatus)
                && !fs::is_directory(childStatus))
            || !ownerWritableOnly(child)) {
            return false;
        }
        iterator.increment(error);
    }
    return !error;
}

LegacyEntryPresence legacyEntryPresence(
    const fs::path& path,
    const LegacyDirectRootEntry& entry)
{
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory
        || (!error && status.type() == fs::file_type::not_found)) {
        return LegacyEntryPresence::Missing;
    }
    if (error || !safeLegacyEntry(path, entry))
        return LegacyEntryPresence::Unsafe;
    return LegacyEntryPresence::Present;
}

LegacyDirectRootState inspectLegacyDirectRootState(
    const fs::path& hostRoot,
    LegacyDirectRootSelection& selection)
{
    selection.fill(false);
    bool present = false;
    for (std::size_t index = 0U; index < kLegacyDirectRootEntries.size();
         ++index) {
        const LegacyEntryPresence state = legacyEntryPresence(
            hostRoot / kLegacyDirectRootEntries[index].name,
            kLegacyDirectRootEntries[index]);
        if (state == LegacyEntryPresence::Unsafe)
            return LegacyDirectRootState::Unsafe;
        if (state == LegacyEntryPresence::Present) {
            selection[index] = true;
            present = true;
        }
    }
    return present ? LegacyDirectRootState::Present
                   : LegacyDirectRootState::Absent;
}

std::string legacyMigrationRecord(const LegacyDirectRootSelection& selection)
{
    std::string record = std::string(kLegacyMigrationHeader) + "\n"
        + "profile_id=" + kReleaseProfileId + "\n";
    bool hasEntry = false;
    for (std::size_t index = 0U; index < selection.size(); ++index) {
        if (!selection[index])
            continue;
        record += "entry=";
        record += kLegacyDirectRootEntries[index].name;
        record += "\n";
        hasEntry = true;
    }
    return hasEntry ? record : std::string{};
}

bool parseLegacyMigrationRecord(
    const std::string& record,
    LegacyDirectRootSelection& selection)
{
    const std::string prefix = std::string(kLegacyMigrationHeader) + "\n"
        + "profile_id=" + kReleaseProfileId + "\n";
    if (record.size() <= prefix.size()
        || record.compare(0U, prefix.size(), prefix) != 0) {
        return false;
    }

    selection.fill(false);
    bool hasEntry = false;
    std::size_t previous = 0U;
    bool first = true;
    std::size_t offset = prefix.size();
    while (offset < record.size()) {
        const std::size_t newline = record.find('\n', offset);
        if (newline == std::string::npos || newline == offset
            || record.compare(offset, 6U, "entry=") != 0) {
            return false;
        }
        const std::optional<std::size_t> index = legacyEntryIndex(
            record.substr(offset + 6U, newline - (offset + 6U)));
        if (!index.has_value() || (!first && *index <= previous)
            || selection[*index]) {
            return false;
        }
        selection[*index] = true;
        previous = *index;
        first = false;
        hasEntry = true;
        offset = newline + 1U;
    }
    return hasEntry;
}

std::optional<fs::path> prepareHostRoot(
    const std::string& input,
    PalaceLezProfileBindingResultV1& failure)
{
    if (input.empty() || input.size() > kMaximumHostPathBytes)
        return std::nullopt;

    std::error_code error;
    fs::path host = fs::absolute(fs::path(input), error).lexically_normal();
    if (error || host.empty() || host == host.root_path()) {
        failure = rejected(
            PalaceLezProfileBindingStatusV1::UnsafeHostRoot,
            "unsafe-host-root");
        return std::nullopt;
    }
    const fs::file_status before = fs::symlink_status(host, error);
    if ((!error && (fs::is_symlink(before) || !fs::is_directory(before)))
        || (error && error != std::errc::no_such_file_or_directory)) {
        failure = rejected(
            PalaceLezProfileBindingStatusV1::UnsafeHostRoot,
            "unsafe-host-root");
        return std::nullopt;
    }
    error.clear();
    const bool exists = fs::exists(host, error);
    if (error || (!exists && !fs::create_directories(host, error))) {
        failure = rejected(
            PalaceLezProfileBindingStatusV1::IoError,
            "host-root-create-failed");
        return std::nullopt;
    }
    if (error || !safeExistingDirectory(host, error)) {
        failure = rejected(
            PalaceLezProfileBindingStatusV1::UnsafeHostRoot,
            "unsafe-host-root");
        return std::nullopt;
    }
    const fs::path canonical = fs::canonical(host, error);
    if (error || canonical.empty() || canonical == canonical.root_path()) {
        failure = rejected(
            PalaceLezProfileBindingStatusV1::UnsafeHostRoot,
            "unsafe-host-root");
        return std::nullopt;
    }
    if (!ownerWritableOnly(canonical)) {
        failure = rejected(
            PalaceLezProfileBindingStatusV1::InsecurePermissions,
            "host-root-permissions");
        return std::nullopt;
    }
    return canonical;
}

std::string temporaryRecordName(const char* prefix)
{
    const std::uint64_t sequence =
        temporarySequence.fetch_add(1U, std::memory_order_relaxed);
#if defined(__unix__) || defined(__APPLE__)
    return std::string(prefix)
        + std::to_string(static_cast<std::uint64_t>(::getpid())) + "."
        + std::to_string(sequence);
#else
    return std::string(prefix)
        + std::to_string(
            static_cast<std::uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count()))
        + "." + std::to_string(sequence);
#endif
}

bool readBinding(
    const fs::path& path,
    std::string& output,
    PalaceLezProfileBindingStatusV1& status)
{
    std::error_code error;
    const fs::file_status fileStatus = fs::symlink_status(path, error);
    if (error || fs::is_symlink(fileStatus) || !fs::is_regular_file(fileStatus)
        || !ownerReadWriteOnly(path)) {
        status = error ? PalaceLezProfileBindingStatusV1::IoError
                       : PalaceLezProfileBindingStatusV1::BindingInvalid;
        return false;
    }
    const std::uintmax_t size = fs::file_size(path, error);
    if (error || size == 0U || size > kMaximumBindingBytes) {
        status = error ? PalaceLezProfileBindingStatusV1::IoError
                       : PalaceLezProfileBindingStatusV1::BindingInvalid;
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    output.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    if (!input.good() && !input.eof() || output.size() != size) {
        status = PalaceLezProfileBindingStatusV1::IoError;
        return false;
    }
    status = PalaceLezProfileBindingStatusV1::Bound;
    return true;
}

bool readLegacyMigrationMarker(
    const fs::path& path,
    LegacyDirectRootSelection& selection)
{
    std::error_code error;
    const fs::file_status fileStatus = fs::symlink_status(path, error);
    if (error || fs::is_symlink(fileStatus) || !fs::is_regular_file(fileStatus)
        || !ownerReadWriteOnly(path)) {
        return false;
    }
    const std::uintmax_t size = fs::file_size(path, error);
    if (error || size == 0U || size > kMaximumLegacyMigrationBytes)
        return false;
    std::ifstream input(path, std::ios::binary);
    const std::string record{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
    return (!input.good() && !input.eof()) || record.size() != size
        ? false
        : parseLegacyMigrationRecord(record, selection);
}

#if defined(__unix__) || defined(__APPLE__)
bool writeAll(const int descriptor, const std::string& value)
{
    std::size_t offset = 0U;
    while (offset < value.size()) {
        const ssize_t written = ::write(
            descriptor,
            value.data() + offset,
            value.size() - offset);
        if (written <= 0)
            return false;
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

bool flushDirectory(const fs::path& path)
{
    int flags = O_RDONLY;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    const int descriptor = ::open(path.c_str(), flags);
    if (descriptor < 0)
        return false;
    return ::fsync(descriptor) == 0 && ::close(descriptor) == 0;
}

bool createRecordAtomically(
    const fs::path& directory,
    const fs::path& destination,
    const char* temporaryPrefix,
    const std::string& expected,
    bool& alreadyExists)
{
    alreadyExists = false;
    const fs::path temporary = directory / temporaryRecordName(temporaryPrefix);
    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int descriptor = ::open(temporary.c_str(), flags, 0600);
    if (descriptor < 0)
        return false;
    const bool wrote = ::fchmod(descriptor, 0600) == 0
        && writeAll(descriptor, expected) && ::fsync(descriptor) == 0;
    const bool closed = ::close(descriptor) == 0;
    if (!wrote || !closed) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }

    if (::link(temporary.c_str(), destination.c_str()) != 0) {
        alreadyExists = errno == EEXIST;
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return alreadyExists;
    }
    std::error_code ignored;
    fs::remove(temporary, ignored);
    return flushDirectory(directory);
}
#else
bool flushDirectory(const fs::path& path)
{
    static_cast<void>(path);
    return true;
}

bool createRecordAtomically(
    const fs::path& directory,
    const fs::path& destination,
    const char* temporaryPrefix,
    const std::string& expected,
    bool& alreadyExists)
{
    std::error_code error;
    if (fs::exists(destination, error)) {
        alreadyExists = !error;
        return alreadyExists;
    }
    if (error)
        return false;
    const fs::path temporary = directory / temporaryRecordName(temporaryPrefix);
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.write(expected.data(), static_cast<std::streamsize>(expected.size()));
    output.close();
    if (!output.good())
        return false;
    fs::rename(temporary, destination, error);
    if (error) {
        alreadyExists = fs::exists(destination, error) && !error;
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return alreadyExists;
    }
    return true;
}
#endif

bool createBindingAtomically(
    const fs::path& profileRoot,
    const std::string& expected,
    bool& alreadyExists)
{
    return createRecordAtomically(
        profileRoot,
        profileRoot / kBindingFileName,
        ".lez-profile-binding-v1.next.",
        expected,
        alreadyExists);
}

bool createLegacyMigrationMarkerAtomically(
    const fs::path& profileRoot,
    const std::string& expected,
    bool& alreadyExists)
{
    return createRecordAtomically(
        profileRoot,
        profileRoot / kLegacyMigrationFileName,
        ".lez-profile-legacy-direct-root-migration-v1.next.",
        expected,
        alreadyExists);
}

bool readLegacyMigrationMarkerIfPresent(
    const fs::path& profileRoot,
    LegacyDirectRootSelection& selection,
    bool& markerPresent)
{
    markerPresent = false;
    std::error_code error;
    const fs::path markerPath = profileRoot / kLegacyMigrationFileName;
    const fs::file_status status = fs::symlink_status(markerPath, error);
    if (error == std::errc::no_such_file_or_directory
        || (!error && status.type() == fs::file_type::not_found)) {
        return true;
    }
    if (error)
        return false;
    markerPresent = true;
    return readLegacyMigrationMarker(markerPath, selection);
}

enum class MigrationProfileRootState {
    Compatible,
    Conflict,
    Unsafe,
};

MigrationProfileRootState migrationProfileRootState(
    const fs::path& profileRoot,
    const LegacyDirectRootSelection& selection)
{
    std::error_code error;
    fs::directory_iterator iterator(profileRoot, error);
    const fs::directory_iterator end;
    while (!error && iterator != end) {
        const fs::path child = iterator->path();
        const std::string name = child.filename().string();
        if (name == kLegacyMigrationFileName) {
            ++iterator;
            continue;
        }
        if (name == kBindingFileName) {
            std::string binding;
            PalaceLezProfileBindingStatusV1 bindingStatus;
            if (!readBinding(child, binding, bindingStatus))
                return MigrationProfileRootState::Unsafe;
            ++iterator;
            continue;
        }
        const std::optional<std::size_t> index = legacyEntryIndex(name);
        if (!index.has_value() || !selection[*index])
            return MigrationProfileRootState::Conflict;
        if (!safeLegacyEntry(child, kLegacyDirectRootEntries[*index]))
            return MigrationProfileRootState::Unsafe;
        ++iterator;
    }
    return error ? MigrationProfileRootState::Unsafe
                 : MigrationProfileRootState::Compatible;
}

bool profileRootEmpty(const fs::path& profileRoot)
{
    std::error_code error;
    const fs::directory_iterator iterator(profileRoot, error);
    return !error && iterator == fs::directory_iterator{};
}

bool moveLegacyEntry(
    const fs::path& hostRoot,
    const fs::path& profileRoot,
    const LegacyDirectRootEntry& entry)
{
    std::error_code error;
    fs::rename(hostRoot / entry.name, profileRoot / entry.name, error);
    return !error && flushDirectory(hostRoot) && flushDirectory(profileRoot);
}

bool removeLegacyMigrationMarker(const fs::path& profileRoot)
{
    const fs::path markerPath = profileRoot / kLegacyMigrationFileName;
    std::error_code error;
    const fs::file_status status = fs::symlink_status(markerPath, error);
    if (error || fs::is_symlink(status) || !fs::is_regular_file(status)
        || !ownerReadWriteOnly(markerPath)) {
        return false;
    }
    if (!fs::remove(markerPath, error) || error)
        return false;
    return flushDirectory(profileRoot);
}

bool releaseMigrationInProgress(
    const fs::path& hostRoot,
    bool& pending)
{
    pending = false;
    std::error_code error;
    const fs::path profilesRoot = hostRoot / kProfilesDirectoryName;
    const fs::file_status profilesStatus = fs::symlink_status(profilesRoot, error);
    if (error == std::errc::no_such_file_or_directory
        || (!error && profilesStatus.type() == fs::file_type::not_found)) {
        return true;
    }
    if (error || fs::is_symlink(profilesStatus)
        || !fs::is_directory(profilesStatus)
        || !ownerWritableOnly(profilesRoot)) {
        return false;
    }
    const fs::path releaseRoot = profilesRoot / kReleaseProfileId;
    error.clear();
    const fs::file_status releaseStatus = fs::symlink_status(releaseRoot, error);
    if (error == std::errc::no_such_file_or_directory
        || (!error && releaseStatus.type() == fs::file_type::not_found)) {
        return true;
    }
    if (error || fs::is_symlink(releaseStatus)
        || !fs::is_directory(releaseStatus)
        || !ownerWritableOnly(releaseRoot)) {
        return false;
    }
    LegacyDirectRootSelection ignored{};
    return readLegacyMigrationMarkerIfPresent(releaseRoot, ignored, pending);
}

bool migrateLegacyDirectRootState(
    const fs::path& hostRoot,
    const fs::path& releaseRoot,
    const std::string& expectedBinding,
    bool& migrationPending,
    std::string& reason)
{
    migrationPending = false;
    LegacyDirectRootSelection selected{};
    bool markerPresent = false;
    if (!readLegacyMigrationMarkerIfPresent(
            releaseRoot, selected, markerPresent)) {
        reason = "legacy-direct-root-migration-invalid";
        return false;
    }

    LegacyDirectRootSelection present{};
    const LegacyDirectRootState directState = inspectLegacyDirectRootState(
        hostRoot, present);
    if (directState == LegacyDirectRootState::Unsafe) {
        reason = "legacy-direct-root-unsafe";
        return false;
    }
    if (!markerPresent) {
        if (directState == LegacyDirectRootState::Absent
            && profileRootEmpty(releaseRoot)) {
            return true;
        }
        if (directState == LegacyDirectRootState::Absent) {
            std::error_code bindingError;
            const fs::file_status bindingStatus = fs::symlink_status(
                releaseRoot / kBindingFileName, bindingError);
            if (bindingError != std::errc::no_such_file_or_directory
                && (bindingError
                    || bindingStatus.type() != fs::file_type::not_found)) {
                return true;
            }
            reason = "legacy-direct-root-conflict";
            return false;
        }
        if (!profileRootEmpty(releaseRoot)) {
            reason = "legacy-direct-root-conflict";
            return false;
        }
        selected = present;
        const std::string record = legacyMigrationRecord(selected);
        bool alreadyExists = false;
        if (record.empty()
            || !createLegacyMigrationMarkerAtomically(
                releaseRoot, record, alreadyExists)) {
            if (!alreadyExists) {
                reason = "legacy-direct-root-migration-create-failed";
                return false;
            }
        }
        if (alreadyExists
            && !readLegacyMigrationMarkerIfPresent(
                releaseRoot, selected, markerPresent)) {
            reason = "legacy-direct-root-migration-invalid";
            return false;
        }
        markerPresent = true;
    }

    for (std::size_t index = 0U; index < present.size(); ++index) {
        if (present[index] && !selected[index]) {
            reason = "legacy-direct-root-conflict";
            return false;
        }
    }
    const MigrationProfileRootState layout = migrationProfileRootState(
        releaseRoot, selected);
    if (layout != MigrationProfileRootState::Compatible) {
        reason = layout == MigrationProfileRootState::Unsafe
            ? "legacy-direct-root-unsafe"
            : "legacy-direct-root-conflict";
        return false;
    }
    std::error_code bindingError;
    const fs::path bindingPath = releaseRoot / kBindingFileName;
    const fs::file_status bindingStatus = fs::symlink_status(
        bindingPath, bindingError);
    if (bindingError != std::errc::no_such_file_or_directory
        && (bindingError || bindingStatus.type() != fs::file_type::not_found)) {
        std::string actualBinding;
        PalaceLezProfileBindingStatusV1 readStatus;
        if (!readBinding(bindingPath, actualBinding, readStatus)) {
            reason = "legacy-direct-root-binding-invalid";
            return false;
        }
        if (actualBinding != expectedBinding) {
            reason = "legacy-direct-root-binding-mismatch";
            return false;
        }
    }

    for (std::size_t index = 0U; index < selected.size(); ++index) {
        if (!selected[index])
            continue;
        const LegacyDirectRootEntry& entry = kLegacyDirectRootEntries[index];
        const LegacyEntryPresence source = legacyEntryPresence(
            hostRoot / entry.name, entry);
        const LegacyEntryPresence destination = legacyEntryPresence(
            releaseRoot / entry.name, entry);
        if (source == LegacyEntryPresence::Unsafe
            || destination == LegacyEntryPresence::Unsafe) {
            reason = "legacy-direct-root-unsafe";
            return false;
        }
        if (source == LegacyEntryPresence::Present
            && destination == LegacyEntryPresence::Present) {
            reason = "legacy-direct-root-conflict";
            return false;
        }
        if (source == LegacyEntryPresence::Missing
            && destination == LegacyEntryPresence::Missing) {
            reason = "legacy-direct-root-migration-incomplete";
            return false;
        }
        if (source == LegacyEntryPresence::Present
            && !moveLegacyEntry(hostRoot, releaseRoot, entry)) {
            reason = "legacy-direct-root-migration-move-failed";
            return false;
        }
    }

    LegacyDirectRootSelection remaining{};
    const LegacyDirectRootState remainingState = inspectLegacyDirectRootState(
        hostRoot, remaining);
    if (remainingState != LegacyDirectRootState::Absent) {
        reason = remainingState == LegacyDirectRootState::Unsafe
            ? "legacy-direct-root-unsafe"
            : "legacy-direct-root-conflict";
        return false;
    }
    for (std::size_t index = 0U; index < selected.size(); ++index) {
        if (selected[index]
            && legacyEntryPresence(
                releaseRoot / kLegacyDirectRootEntries[index].name,
                kLegacyDirectRootEntries[index])
                != LegacyEntryPresence::Present) {
            reason = "legacy-direct-root-migration-incomplete";
            return false;
        }
    }
    migrationPending = true;
    return true;
}

} // namespace

bool PalaceLezProfileV1::acceptsLiveModule(
    const std::string& moduleName,
    const std::string& moduleVersion,
    const std::string& sequencerOrigin) const
{
    return validProfile(*this)
        && moduleName == expectedModuleName
        && moduleVersion == network.moduleApiVersion
        && (sequencerOrigin == expectedSequencerOrigin
            || sequencerOrigin == expectedSequencerOrigin + "/");
}

std::optional<PalaceLezProfileV1> resolvePalaceLezProfileV1(
    const std::string& selector)
{
    PalaceLezProfileV1 profile;
    if (selector.empty() || selector == kReleaseProfileId)
        profile = releaseProfile();
    else if (selector == kLocalDevelopmentProfileId)
        profile = localDevelopmentProfile();
    else
        return std::nullopt;
    return validProfile(profile) ? std::optional<PalaceLezProfileV1>(profile)
                                 : std::nullopt;
}

PalaceLezProfileBindingResultV1 bindPalaceLezProfileV1(
    const std::string& hostRoot,
    const std::string& selector)
{
    const std::optional<PalaceLezProfileV1> profile =
        resolvePalaceLezProfileV1(selector);
    if (!profile.has_value()) {
        return rejected(
            PalaceLezProfileBindingStatusV1::UnknownProfile,
            "unknown-lez-profile");
    }

    PalaceLezProfileBindingResultV1 failure;
    const std::optional<fs::path> host = prepareHostRoot(hostRoot, failure);
    if (!host.has_value()) {
        if (failure.reason.empty()) {
            return rejected(
                PalaceLezProfileBindingStatusV1::InvalidArgument,
                "invalid-host-root");
        }
        return failure;
    }

    LegacyDirectRootSelection directRootState{};
    const LegacyDirectRootState directRootInspection =
        inspectLegacyDirectRootState(*host, directRootState);
    if (directRootInspection == LegacyDirectRootState::Unsafe) {
        return rejected(
            PalaceLezProfileBindingStatusV1::LegacyDirectRootState,
            "legacy-direct-root-unsafe");
    }
    if (profile->id == kLocalDevelopmentProfileId
        && directRootInspection == LegacyDirectRootState::Present) {
        return rejected(
            PalaceLezProfileBindingStatusV1::LegacyDirectRootState,
            "legacy-direct-root-state");
    }
    if (profile->id == kLocalDevelopmentProfileId) {
        bool releaseMigrationPending = false;
        if (!releaseMigrationInProgress(*host, releaseMigrationPending)) {
            return rejected(
                PalaceLezProfileBindingStatusV1::UnsafeHostRoot,
                "unsafe-profile-root");
        }
        if (releaseMigrationPending) {
            return rejected(
                PalaceLezProfileBindingStatusV1::LegacyDirectRootState,
                "legacy-direct-root-migration-in-progress");
        }
    }

    std::error_code error;
    const fs::path profilesRoot = *host / kProfilesDirectoryName;
    if (!createOwnerOnlyDirectory(profilesRoot, error)) {
        return rejected(
            error ? PalaceLezProfileBindingStatusV1::IoError
                  : PalaceLezProfileBindingStatusV1::UnsafeHostRoot,
            error ? "profile-root-create-failed" : "unsafe-profile-root");
    }
    const fs::path profileRoot = profilesRoot / profile->id;
    if (!createOwnerOnlyDirectory(profileRoot, error)) {
        return rejected(
            error ? PalaceLezProfileBindingStatusV1::IoError
                  : PalaceLezProfileBindingStatusV1::UnsafeHostRoot,
            error ? "profile-directory-create-failed"
                  : "unsafe-profile-directory");
    }
    const fs::path canonicalProfileRoot = fs::canonical(profileRoot, error);
    if (error || canonicalProfileRoot.empty()
        || canonicalProfileRoot.parent_path()
            != fs::canonical(profilesRoot, error)
        || error) {
        return rejected(
            PalaceLezProfileBindingStatusV1::UnsafeHostRoot,
            "profile-directory-escaped-root");
    }

    const std::string expected = bindingRecord(*profile);
    if (expected.empty() || expected.size() > kMaximumBindingBytes) {
        return rejected(
            PalaceLezProfileBindingStatusV1::BindingInvalid,
            "profile-binding-encode-failed");
    }
    bool legacyMigrationPending = false;
    if (profile->id == kReleaseProfileId) {
        std::string migrationReason;
        if (!migrateLegacyDirectRootState(
                *host,
                canonicalProfileRoot,
                expected,
                legacyMigrationPending,
                migrationReason)) {
            return rejected(
                PalaceLezProfileBindingStatusV1::LegacyDirectRootState,
                migrationReason);
        }
    }
    const fs::path bindingPath = canonicalProfileRoot / kBindingFileName;
    const fs::file_status bindingStatus = fs::symlink_status(bindingPath, error);
    if (error && error != std::errc::no_such_file_or_directory) {
        return rejected(
            PalaceLezProfileBindingStatusV1::IoError,
            "profile-binding-stat-failed");
    }
    if (!error && bindingStatus.type() != fs::file_type::not_found) {
        std::string actual;
        PalaceLezProfileBindingStatusV1 readStatus;
        if (!readBinding(bindingPath, actual, readStatus)) {
            return rejected(readStatus, "profile-binding-invalid");
        }
        if (actual != expected) {
            return rejected(
                PalaceLezProfileBindingStatusV1::BindingMismatch,
                "profile-binding-mismatch");
        }
    } else {
        bool alreadyExists = false;
        if (!createBindingAtomically(canonicalProfileRoot, expected, alreadyExists)) {
            return rejected(
                PalaceLezProfileBindingStatusV1::IoError,
                "profile-binding-write-failed");
        }
        if (alreadyExists) {
            std::string actual;
            PalaceLezProfileBindingStatusV1 readStatus;
            if (!readBinding(bindingPath, actual, readStatus)) {
                return rejected(readStatus, "profile-binding-invalid");
            }
            if (actual != expected) {
                return rejected(
                    PalaceLezProfileBindingStatusV1::BindingMismatch,
                    "profile-binding-mismatch");
            }
        }
    }

    // Keep the migration marker until the normal profile binding is durable.
    // A restart can then either resume the move or validate this binding and
    // complete marker cleanup without exposing an unbound migrated root.
    if (legacyMigrationPending
        && !removeLegacyMigrationMarker(canonicalProfileRoot)) {
        return rejected(
            PalaceLezProfileBindingStatusV1::IoError,
            "legacy-direct-root-migration-finalize-failed");
    }

    PalaceLezProfileBindingResultV1 result;
    result.status = PalaceLezProfileBindingStatusV1::Bound;
    result.reason = "bound";
    result.profile = *profile;
    result.persistenceRoot = canonicalProfileRoot.string();
    return result;
}

const char* palaceLezProfileBindingStatusNameV1(
    const PalaceLezProfileBindingStatusV1 status)
{
    switch (status) {
    case PalaceLezProfileBindingStatusV1::Bound:
        return "bound";
    case PalaceLezProfileBindingStatusV1::InvalidArgument:
        return "invalid-argument";
    case PalaceLezProfileBindingStatusV1::UnknownProfile:
        return "unknown-profile";
    case PalaceLezProfileBindingStatusV1::UnsafeHostRoot:
        return "unsafe-host-root";
    case PalaceLezProfileBindingStatusV1::InsecurePermissions:
        return "insecure-permissions";
    case PalaceLezProfileBindingStatusV1::LegacyDirectRootState:
        return "legacy-direct-root-state";
    case PalaceLezProfileBindingStatusV1::BindingInvalid:
        return "binding-invalid";
    case PalaceLezProfileBindingStatusV1::BindingMismatch:
        return "binding-mismatch";
    case PalaceLezProfileBindingStatusV1::IoError:
        return "io-error";
    }
    return "unknown";
}

} // namespace palace
