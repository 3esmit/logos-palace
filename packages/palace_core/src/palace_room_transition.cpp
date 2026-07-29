#include "palace_room_transition.h"

#include "palace_sha256.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
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

constexpr const char* kJournalFileName = "room-transition-v1";
constexpr const char* kTemporaryFileName = "room-transition-v1.next";
constexpr std::size_t kMaximumDirectoryBytes = 4096U;
constexpr std::size_t kMaximumSessionStateBytes =
    3U * 1024U * 1024U;
constexpr std::size_t kMaximumProjectionStateBytes = 1024U;
constexpr std::size_t kMaximumRoomIdBytes = 16U;
constexpr std::size_t kMaximumRecordBytes =
    kMaximumSessionStateBytes
    + kMaximumProjectionStateBytes
    + kMaximumRoomIdBytes
    + 256U;

bool validRoomId(const std::string& roomId)
{
    return roomId == "atrium" || roomId == "lounge";
}

bool validIntent(const PalaceRoomTransitionIntentV1& intent)
{
    return validRoomId(intent.logicalRoomId)
        && !intent.deliverySessionState.empty()
        && intent.deliverySessionState.size()
            <= kMaximumSessionStateBytes
        && !intent.projectionState.empty()
        && intent.projectionState.size()
            <= kMaximumProjectionStateBytes;
}

bool lowerHexChecksum(const std::string& value)
{
    return value.size() == 64U
        && std::all_of(
            value.begin(), value.end(),
            [](const unsigned char character) {
                return (character >= '0' && character <= '9')
                    || (character >= 'a' && character <= 'f');
            });
}

bool parseCanonicalSize(
    const std::string& value,
    const std::size_t maximum,
    std::size_t& parsed)
{
    if (value.empty() || value.size() > 20U
        || (value.size() > 1U && value.front() == '0')) {
        return false;
    }
    std::uint64_t wide = 0U;
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), wide);
    if (result.ec != std::errc()
        || result.ptr != value.data() + value.size()
        || wide > maximum) {
        return false;
    }
    parsed = static_cast<std::size_t>(wide);
    return value == std::to_string(parsed);
}

std::string serializePayload(
    const PalaceRoomTransitionIntentV1& intent)
{
    return "version=1\nroom-bytes="
        + std::to_string(intent.logicalRoomId.size())
        + "\nsession-bytes="
        + std::to_string(intent.deliverySessionState.size())
        + "\nprojection-bytes="
        + std::to_string(intent.projectionState.size())
        + "\n"
        + intent.logicalRoomId
        + intent.deliverySessionState
        + intent.projectionState;
}

std::string serializeRecord(
    const PalaceRoomTransitionIntentV1& intent)
{
    const std::string payload = serializePayload(intent);
    return crypto::sha256Hex(payload) + "\n" + payload;
}

bool takeHeader(
    const std::string& payload,
    std::size_t& offset,
    const std::string& prefix,
    std::string& value)
{
    if (payload.compare(offset, prefix.size(), prefix) != 0)
        return false;
    const std::size_t start = offset + prefix.size();
    const std::size_t newline = payload.find('\n', start);
    if (newline == std::string::npos || newline - start > 20U)
        return false;
    value = payload.substr(start, newline - start);
    offset = newline + 1U;
    return true;
}

bool parsePayload(
    const std::string& payload,
    PalaceRoomTransitionIntentV1& intent)
{
    static const std::string kVersion = "version=1\n";
    if (payload.rfind(kVersion, 0U) != 0)
        return false;

    std::size_t offset = kVersion.size();
    std::string roomSizeText;
    std::string sessionSizeText;
    std::string projectionSizeText;
    if (!takeHeader(
            payload, offset, "room-bytes=", roomSizeText)
        || !takeHeader(
            payload, offset, "session-bytes=", sessionSizeText)
        || !takeHeader(
            payload, offset, "projection-bytes=",
            projectionSizeText)) {
        return false;
    }

    std::size_t roomSize = 0U;
    std::size_t sessionSize = 0U;
    std::size_t projectionSize = 0U;
    if (!parseCanonicalSize(
            roomSizeText, kMaximumRoomIdBytes, roomSize)
        || !parseCanonicalSize(
            sessionSizeText,
            kMaximumSessionStateBytes,
            sessionSize)
        || !parseCanonicalSize(
            projectionSizeText,
            kMaximumProjectionStateBytes,
            projectionSize)
        || roomSize > payload.size() - offset
        || sessionSize > payload.size() - offset - roomSize
        || projectionSize
            != payload.size() - offset - roomSize - sessionSize) {
        return false;
    }

    PalaceRoomTransitionIntentV1 parsed;
    parsed.logicalRoomId =
        payload.substr(offset, roomSize);
    offset += roomSize;
    parsed.deliverySessionState =
        payload.substr(offset, sessionSize);
    offset += sessionSize;
    parsed.projectionState =
        payload.substr(offset, projectionSize);
    if (!validIntent(parsed)
        || serializePayload(parsed) != payload) {
        return false;
    }
    intent = std::move(parsed);
    return true;
}

bool parseRecord(
    const std::string& record,
    PalaceRoomTransitionIntentV1& intent)
{
    if (record.empty() || record.size() > kMaximumRecordBytes)
        return false;
    const std::size_t newline = record.find('\n');
    if (newline != 64U)
        return false;
    const std::string checksum = record.substr(0U, newline);
    const std::string payload = record.substr(newline + 1U);
    return lowerHexChecksum(checksum)
        && checksum == crypto::sha256Hex(payload)
        && parsePayload(payload, intent);
}

bool flushDirectory(const fs::path& path)
{
#if defined(__unix__) || defined(__APPLE__)
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
    const int descriptor = ::open(path.c_str(), flags);
    if (descriptor < 0)
        return false;
    const bool flushed = ::fsync(descriptor) == 0;
    ::close(descriptor);
    return flushed;
#else
    (void)path;
    return true;
#endif
}

bool existingSafeDirectory(
    const std::string& value,
    fs::path& directory)
{
    if (value.empty() || value.size() > kMaximumDirectoryBytes)
        return false;
    directory = fs::path(value);
#if defined(__unix__) || defined(__APPLE__)
    struct stat status {};
    return ::lstat(directory.c_str(), &status) == 0
        && S_ISDIR(status.st_mode)
        && status.st_uid == ::geteuid()
        && (status.st_mode & (S_IWGRP | S_IWOTH)) == 0;
#else
    std::error_code error;
    const fs::file_status status =
        fs::symlink_status(directory, error);
    return !error && fs::is_directory(status)
        && !fs::is_symlink(status);
#endif
}

bool writeExclusiveOwnerOnly(
    const fs::path& path,
    const std::string& record)
{
#if defined(__unix__) || defined(__APPLE__)
    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int descriptor = ::open(
        path.c_str(), flags, S_IRUSR | S_IWUSR);
    if (descriptor < 0)
        return false;

    std::size_t offset = 0U;
    bool written = true;
    while (offset < record.size()) {
        const ssize_t count = ::write(
            descriptor,
            record.data() + offset,
            record.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            written = false;
            break;
        }
        offset += static_cast<std::size_t>(count);
    }
    const bool flushed = written && ::fsync(descriptor) == 0;
    const bool closed = ::close(descriptor) == 0;
    if (flushed && closed)
        return true;
    std::error_code ignored;
    fs::remove(path, ignored);
    return false;
#else
    std::error_code error;
    if (fs::exists(path, error) || error)
        return false;
    std::ofstream output(
        path,
        std::ios::binary | std::ios::trunc);
    output.write(
        record.data(),
        static_cast<std::streamsize>(record.size()));
    output.flush();
    if (!output.good())
        return false;
    fs::permissions(
        path,
        fs::perms::owner_read | fs::perms::owner_write,
        fs::perm_options::replace,
        error);
    return !error;
#endif
}

bool safeDirectory(
    const std::string& value,
    fs::path& directory)
{
    if (value.empty() || value.size() > kMaximumDirectoryBytes)
        return false;
    directory = fs::path(value);
    if (existingSafeDirectory(value, directory))
        return true;
    std::error_code error;
    const fs::file_status existing =
        fs::symlink_status(directory, error);
    if (!error && fs::exists(existing))
        return false;
    error.clear();
    fs::create_directories(directory, error);
    if (error)
        return false;
    return existingSafeDirectory(value, directory);
}

bool regularNonSymlink(
    const fs::path& path,
    std::error_code& error)
{
    const fs::file_status status =
        fs::symlink_status(path, error);
    return !error && fs::is_regular_file(status)
        && !fs::is_symlink(status);
}

bool pathPresent(
    const fs::path& path,
    fs::file_status& status,
    std::error_code& error)
{
    status = fs::symlink_status(path, error);
    if (error
        == std::errc::no_such_file_or_directory) {
        error.clear();
        status = fs::file_status(fs::file_type::not_found);
    }
    return !error && fs::exists(status);
}

#if !defined(__unix__) && !defined(__APPLE__)
bool ownerOnlyRecord(
    const fs::path& path,
    std::error_code& error)
{
    const fs::file_status status =
        fs::symlink_status(path, error);
    if (error || !fs::is_regular_file(status)
        || fs::is_symlink(status)) {
        return false;
    }
    const fs::perms forbidden =
        fs::perms::group_all | fs::perms::others_all;
    return (status.permissions() & forbidden)
        == fs::perms::none;
}
#endif

bool readOwnerOnlyRecord(
    const fs::path& path,
    std::string& record)
{
#if defined(__unix__) || defined(__APPLE__)
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int descriptor = ::open(path.c_str(), flags);
    if (descriptor < 0)
        return false;

    struct stat status {};
    if (::fstat(descriptor, &status) != 0
        || !S_ISREG(status.st_mode)
        || status.st_nlink != 1
        || status.st_uid != ::geteuid()
        || (status.st_mode & 0777)
            != (S_IRUSR | S_IWUSR)
        || status.st_size <= 0
        || static_cast<std::uintmax_t>(status.st_size)
            > kMaximumRecordBytes) {
        ::close(descriptor);
        return false;
    }

    record.assign(
        static_cast<std::size_t>(status.st_size), '\0');
    std::size_t offset = 0U;
    while (offset < record.size()) {
        const ssize_t count = ::read(
            descriptor,
            record.data() + offset,
            record.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            ::close(descriptor);
            return false;
        }
        offset += static_cast<std::size_t>(count);
    }
    char trailing = '\0';
    ssize_t trailingCount = -1;
    do {
        trailingCount = ::read(descriptor, &trailing, 1U);
    } while (trailingCount < 0 && errno == EINTR);
    const bool exact = trailingCount == 0;
    const bool closed = ::close(descriptor) == 0;
    return exact && closed;
#else
    std::error_code error;
    if (!ownerOnlyRecord(path, error))
        return false;
    const std::uintmax_t size = fs::file_size(path, error);
    if (error || size == 0U || size > kMaximumRecordBytes)
        return false;
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    record.assign(static_cast<std::size_t>(size), '\0');
    input.read(
        record.data(),
        static_cast<std::streamsize>(record.size()));
    return input
        && input.peek() == std::ifstream::traits_type::eof();
#endif
}

} // namespace

bool palaceRoomTransitionAlreadyLive(
    const DeliverySessionTransition& deliveryPlan,
    const PalaceProjection& currentProjection,
    const std::string& logicalRoomId)
{
    return deliveryPlan.accepted
        && deliveryPlan.reason == "room-unchanged"
        && currentProjection.currentRoomId()
            == logicalRoomId;
}

std::uint64_t
PalaceRoomTransitionNativeCallGate::generation() const
{
    return m_generation;
}

bool PalaceRoomTransitionNativeCallGate::admitNativeCall(
    const std::uint64_t expectedGeneration,
    const bool transitionHealthy,
    PalaceRoomTransitionNativeCallToken& token)
{
    if (!transitionHealthy || expectedGeneration != m_generation
        || m_nextSerial
            == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    ++m_nextSerial;
    m_activeNativeCalls.insert(m_nextSerial);
    token = {m_generation, m_nextSerial};
    return true;
}

bool PalaceRoomTransitionNativeCallGate::completeNativeCall(
    const PalaceRoomTransitionNativeCallToken& token,
    const bool transitionHealthy)
{
    if (token.generation != m_generation)
        return false;
    const auto active =
        m_activeNativeCalls.find(token.serial);
    if (active == m_activeNativeCalls.end())
        return false;
    m_activeNativeCalls.erase(active);
    return transitionHealthy;
}

bool PalaceRoomTransitionNativeCallGate::canBeginTransition(
    const bool transitionHealthy) const
{
    return transitionHealthy
        && m_activeNativeCalls.empty();
}

bool PalaceRoomTransitionNativeCallGate::beginTransition(
    const bool transitionHealthy)
{
    if (!canBeginTransition(transitionHealthy)
        || m_generation
            == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    ++m_generation;
    m_nextSerial = 0U;
    return true;
}

std::size_t
PalaceRoomTransitionNativeCallGate::nativeCallsInFlight() const
{
    return m_activeNativeCalls.size();
}

const char* palaceRoomTransitionJournalStatusName(
    PalaceRoomTransitionJournalStatus status)
{
    switch (status) {
    case PalaceRoomTransitionJournalStatus::Loaded:
        return "loaded";
    case PalaceRoomTransitionJournalStatus::NotFound:
        return "not-found";
    case PalaceRoomTransitionJournalStatus::InvalidArgument:
        return "invalid-argument";
    case PalaceRoomTransitionJournalStatus::Conflict:
        return "conflict";
    case PalaceRoomTransitionJournalStatus::IoError:
        return "io-error";
    case PalaceRoomTransitionJournalStatus::InvalidRecord:
        return "invalid-record";
    }
    return "invalid-record";
}

PalaceRoomTransitionJournalStore::
PalaceRoomTransitionJournalStore(std::string directory)
    : m_directory(std::move(directory))
{
}

PalaceRoomTransitionJournalStatus
PalaceRoomTransitionJournalStore::save(
    const PalaceRoomTransitionIntentV1& intent) const
{
    if (!validIntent(intent))
        return PalaceRoomTransitionJournalStatus::InvalidArgument;

    fs::path directory;
    if (!safeDirectory(m_directory, directory))
        return PalaceRoomTransitionJournalStatus::IoError;

    std::error_code error;
    const fs::path destination = directory / kJournalFileName;
    fs::file_status destinationStatus;
    if (pathPresent(
            destination, destinationStatus, error))
        return PalaceRoomTransitionJournalStatus::Conflict;
    if (error)
        return PalaceRoomTransitionJournalStatus::IoError;

    const fs::path temporary = directory / kTemporaryFileName;
    error.clear();
    fs::file_status temporaryStatus;
    if (pathPresent(temporary, temporaryStatus, error)) {
        if (error || !regularNonSymlink(temporary, error)
            || !fs::remove(temporary, error) || error) {
            return PalaceRoomTransitionJournalStatus::IoError;
        }
    } else if (error) {
        return PalaceRoomTransitionJournalStatus::IoError;
    }

    const std::string record = serializeRecord(intent);
    if (!writeExclusiveOwnerOnly(temporary, record))
        return PalaceRoomTransitionJournalStatus::IoError;

    error.clear();
    fs::rename(temporary, destination, error);
    if (error || !flushDirectory(directory))
        return PalaceRoomTransitionJournalStatus::IoError;
    return PalaceRoomTransitionJournalStatus::Loaded;
}

PalaceRoomTransitionJournalStatus
PalaceRoomTransitionJournalStore::load(
    PalaceRoomTransitionIntentV1& intent) const
{
    if (m_directory.empty()
        || m_directory.size() > kMaximumDirectoryBytes) {
        return PalaceRoomTransitionJournalStatus::InvalidArgument;
    }
    fs::path directory;
    if (!existingSafeDirectory(m_directory, directory)) {
        std::error_code directoryError;
        const fs::file_status status =
            fs::symlink_status(fs::path(m_directory), directoryError);
        if (directoryError
                == std::errc::no_such_file_or_directory
            || (!directoryError && !fs::exists(status))) {
            return PalaceRoomTransitionJournalStatus::NotFound;
        }
        return PalaceRoomTransitionJournalStatus::InvalidRecord;
    }
    const fs::path source = directory / kJournalFileName;
    std::error_code error;
    fs::file_status sourceStatus;
    if (!pathPresent(source, sourceStatus, error))
        return error
            ? PalaceRoomTransitionJournalStatus::IoError
            : PalaceRoomTransitionJournalStatus::NotFound;
    std::string record;
    if (!readOwnerOnlyRecord(source, record))
        return PalaceRoomTransitionJournalStatus::InvalidRecord;

    PalaceRoomTransitionIntentV1 parsed;
    if (!parseRecord(record, parsed))
        return PalaceRoomTransitionJournalStatus::InvalidRecord;
    intent = std::move(parsed);
    return PalaceRoomTransitionJournalStatus::Loaded;
}

bool PalaceRoomTransitionJournalStore::clear() const
{
    if (m_directory.empty()
        || m_directory.size() > kMaximumDirectoryBytes) {
        return false;
    }
    fs::path directory;
    if (!existingSafeDirectory(m_directory, directory))
        return false;
    const fs::path destination = directory / kJournalFileName;
    std::error_code error;
    if (!regularNonSymlink(destination, error)
        || !fs::remove(destination, error) || error) {
        return false;
    }
    return flushDirectory(directory);
}

PalaceRoomTransitionFileDurability::
PalaceRoomTransitionFileDurability(
    std::string directory,
    const DeliverySessionStore& deliverySessionStore,
    const ProjectionStore& projectionStore)
    : m_journal(std::move(directory))
    , m_deliverySessionStore(deliverySessionStore)
    , m_projectionStore(projectionStore)
{
}

PalaceRoomTransitionJournalStatus
PalaceRoomTransitionFileDurability::loadIntent(
    PalaceRoomTransitionIntentV1& intent) const
{
    return m_journal.load(intent);
}

PalaceRoomTransitionJournalStatus
PalaceRoomTransitionFileDurability::saveIntent(
    const PalaceRoomTransitionIntentV1& intent)
{
    return m_journal.save(intent);
}

bool PalaceRoomTransitionFileDurability::saveDeliverySession(
    const PalaceDeliverySession& session)
{
    return m_deliverySessionStore.save(session);
}

bool PalaceRoomTransitionFileDurability::saveProjection(
    const PalaceProjection& projection)
{
    return m_projectionStore.save(projection);
}

bool PalaceRoomTransitionFileDurability::clearIntent()
{
    return m_journal.clear();
}

const char* palaceRoomTransitionStatusName(
    PalaceRoomTransitionStatus status)
{
    switch (status) {
    case PalaceRoomTransitionStatus::Completed:
        return "completed";
    case PalaceRoomTransitionStatus::NoPendingIntent:
        return "no-pending-intent";
    case PalaceRoomTransitionStatus::InvalidArgument:
        return "invalid-argument";
    case PalaceRoomTransitionStatus::PendingIntentConflict:
        return "pending-intent-conflict";
    case PalaceRoomTransitionStatus::InvalidIntent:
        return "invalid-intent";
    case PalaceRoomTransitionStatus::IntentPersistenceFailed:
        return "intent-persistence-failed";
    case PalaceRoomTransitionStatus::DeliveryPersistenceFailed:
        return "delivery-persistence-failed";
    case PalaceRoomTransitionStatus::ProjectionPersistenceFailed:
        return "projection-persistence-failed";
    case PalaceRoomTransitionStatus::IntentClearFailed:
        return "intent-clear-failed";
    }
    return "invalid-intent";
}

bool PalaceRoomTransitionResult::completed() const
{
    return status == PalaceRoomTransitionStatus::Completed
        && committedSession
        && committedProjection.has_value();
}

PalaceRoomTransitionParticipantWriteGate::Lease::Lease(
    PalaceRoomTransitionParticipantWriteGate* owner,
    const bool transition,
    std::unique_lock<std::mutex> lock)
    : m_owner(owner)
    , m_transition(transition)
    , m_lock(std::move(lock))
{
}

PalaceRoomTransitionParticipantWriteGate::Lease::
operator bool() const
{
    return m_owner != nullptr && m_lock.owns_lock();
}

bool PalaceRoomTransitionParticipantWriteGate::
participantWritesBlocked() const
{
    return m_blocked.load(std::memory_order_acquire);
}

void PalaceRoomTransitionParticipantWriteGate::
blockParticipantWrites()
{
    m_blocked.store(true, std::memory_order_release);
}

PalaceRoomTransitionParticipantWriteGate::Lease
PalaceRoomTransitionParticipantWriteGate::
acquireParticipantWrite()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (participantWritesBlocked())
        return {};
    return Lease(this, false, std::move(lock));
}

PalaceRoomTransitionParticipantWriteGate::Lease
PalaceRoomTransitionParticipantWriteGate::
acquireTransitionWrite()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!participantWritesBlocked())
        return {};
    return Lease(this, true, std::move(lock));
}

bool PalaceRoomTransitionParticipantWriteGate::
unblockParticipantWrites(Lease& transitionLease)
{
    if (transitionLease.m_owner != this
        || !transitionLease.m_transition
        || !transitionLease.m_lock.owns_lock()
        || !participantWritesBlocked()) {
        return false;
    }
    m_blocked.store(false, std::memory_order_release);
    return true;
}

bool palaceRoomTransitionStartupMayUnblockParticipantWrites(
    const PalaceRoomTransitionJournalStatus preflight,
    const PalaceRoomTransitionStatus recovery,
    const bool projectionPersistenceComplete)
{
    return projectionPersistenceComplete
        && ((preflight
                == PalaceRoomTransitionJournalStatus::NotFound
            && recovery
                == PalaceRoomTransitionStatus::NoPendingIntent)
            || (preflight
                == PalaceRoomTransitionJournalStatus::Loaded
            && recovery
                == PalaceRoomTransitionStatus::Completed));
}

PalaceRoomTransitionCoordinator::
PalaceRoomTransitionCoordinator(
    PalaceRoomTransitionDurability& durability)
    : m_durability(durability)
{
}

bool PalaceRoomTransitionCoordinator::materialize(
    const PalaceRoomTransitionIntentV1& intent,
    const PalaceDeliverySession& authorityBoundSession,
    std::unique_ptr<PalaceDeliverySession>& session,
    PalaceProjection& projection) const
{
    if (!validIntent(intent))
        return false;
    auto candidate =
        std::make_unique<PalaceDeliverySession>(
            authorityBoundSession);
    PalaceProjection projected;
    if (!candidate->restoreCanonicalState(
            intent.deliverySessionState)
        || candidate->canonicalState()
            != intent.deliverySessionState
        || !candidate->hasConfiguration()
        || [&candidate]() {
            const DeliverySessionConfigV1& config =
                candidate->configuration();
            const DeliverySessionTransition validation =
                candidate->switchRoom(
                    config.roomId, config.roomEpoch);
            return !validation.accepted
                || validation.reason != "room-unchanged";
        }()
        || !projected.restoreCanonicalLocalState(
            intent.projectionState)
        || projected.canonicalLocalState()
            != intent.projectionState
        || projected.currentRoomId()
            != intent.logicalRoomId) {
        return false;
    }
    session = std::move(candidate);
    projection = std::move(projected);
    return true;
}

PalaceRoomTransitionResult
PalaceRoomTransitionCoordinator::persistAndCommit(
    const PalaceDeliverySession& durableSession,
    const PalaceProjection& durableProjection,
    const bool recovered)
{
    if (!m_durability.saveDeliverySession(durableSession)) {
        return {
            PalaceRoomTransitionStatus::
                DeliveryPersistenceFailed,
            recovered,
            {},
            std::nullopt,
        };
    }
    if (!m_durability.saveProjection(durableProjection)) {
        return {
            PalaceRoomTransitionStatus::
                ProjectionPersistenceFailed,
            recovered,
            {},
            std::nullopt,
        };
    }
    if (!m_durability.clearIntent()) {
        return {
            PalaceRoomTransitionStatus::IntentClearFailed,
            recovered,
            {},
            std::nullopt,
        };
    }

    return {
        PalaceRoomTransitionStatus::Completed,
        recovered,
        std::make_unique<PalaceDeliverySession>(
            durableSession),
        durableProjection,
    };
}

PalaceRoomTransitionResult
PalaceRoomTransitionCoordinator::transition(
    const std::string& logicalRoomId,
    const PalaceDeliverySession& desiredSession,
    const PalaceProjection& desiredProjection)
{
    PalaceRoomTransitionIntentV1 existing;
    const PalaceRoomTransitionJournalStatus existingStatus =
        m_durability.loadIntent(existing);
    if (existingStatus
        != PalaceRoomTransitionJournalStatus::NotFound) {
        return {
            existingStatus
                == PalaceRoomTransitionJournalStatus::Loaded
                ? PalaceRoomTransitionStatus::
                      PendingIntentConflict
                : PalaceRoomTransitionStatus::InvalidIntent,
            false,
            {},
            std::nullopt,
        };
    }

    PalaceRoomTransitionIntentV1 intent;
    intent.logicalRoomId = logicalRoomId;
    intent.deliverySessionState =
        desiredSession.canonicalState();
    intent.projectionState =
        desiredProjection.canonicalLocalState();
    std::unique_ptr<PalaceDeliverySession> validatedSession;
    PalaceProjection validatedProjection;
    if (!materialize(
            intent,
            desiredSession,
            validatedSession,
            validatedProjection)) {
        return {
            PalaceRoomTransitionStatus::InvalidArgument,
            false,
            {},
            std::nullopt,
        };
    }

    const PalaceRoomTransitionJournalStatus saved =
        m_durability.saveIntent(intent);
    if (saved != PalaceRoomTransitionJournalStatus::Loaded) {
        return {
            saved == PalaceRoomTransitionJournalStatus::Conflict
                ? PalaceRoomTransitionStatus::
                      PendingIntentConflict
                : PalaceRoomTransitionStatus::
                      IntentPersistenceFailed,
            false,
            {},
            std::nullopt,
        };
    }
    return persistAndCommit(
        desiredSession,
        desiredProjection,
        false);
}

PalaceRoomTransitionResult
PalaceRoomTransitionCoordinator::recover(
    const PalaceDeliverySession& authorityBoundSession)
{
    PalaceRoomTransitionIntentV1 intent;
    const PalaceRoomTransitionJournalStatus loaded =
        m_durability.loadIntent(intent);
    if (loaded == PalaceRoomTransitionJournalStatus::NotFound) {
        return {
            PalaceRoomTransitionStatus::NoPendingIntent,
            false,
            {},
            std::nullopt,
        };
    }
    if (loaded != PalaceRoomTransitionJournalStatus::Loaded) {
        return {
            PalaceRoomTransitionStatus::InvalidIntent,
            true,
            {},
            std::nullopt,
        };
    }

    std::unique_ptr<PalaceDeliverySession> recoveredSession;
    PalaceProjection recoveredProjection;
    if (!materialize(
            intent,
            authorityBoundSession,
            recoveredSession,
            recoveredProjection)) {
        return {
            PalaceRoomTransitionStatus::InvalidIntent,
            true,
            {},
            std::nullopt,
        };
    }
    return persistAndCommit(
        *recoveredSession,
        recoveredProjection,
        true);
}

} // namespace palace
