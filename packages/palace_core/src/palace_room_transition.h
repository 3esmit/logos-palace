#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>

#include "palace_delivery_session.h"
#include "palace_projection.h"

namespace palace {

struct PalaceRoomTransitionIntentV1 {
    std::string logicalRoomId;
    std::string deliverySessionState;
    std::string projectionState;
};

enum class PalaceRoomTransitionJournalStatus {
    Loaded,
    NotFound,
    InvalidArgument,
    Conflict,
    IoError,
    InvalidRecord,
};

const char* palaceRoomTransitionJournalStatusName(
    PalaceRoomTransitionJournalStatus status);

bool palaceRoomTransitionAlreadyLive(
    const DeliverySessionTransition& deliveryPlan,
    const PalaceProjection& currentProjection,
    const std::string& logicalRoomId);

// Mutex-owned admission seam between Delivery native calls and a room
// transition. A native call remains admitted through result application and
// any immediately derived command chain. Callers may start a transition only
// while this gate is quiescent.
struct PalaceRoomTransitionNativeCallToken {
    std::uint64_t generation = 0U;
    std::uint64_t serial = 0U;
};

class PalaceRoomTransitionNativeCallGate {
public:
    std::uint64_t generation() const;
    bool admitNativeCall(
        std::uint64_t expectedGeneration,
        bool transitionHealthy,
        PalaceRoomTransitionNativeCallToken& token);
    bool completeNativeCall(
        const PalaceRoomTransitionNativeCallToken& token,
        bool transitionHealthy);
    bool canBeginTransition(bool transitionHealthy) const;
    bool beginTransition(bool transitionHealthy);
    std::size_t nativeCallsInFlight() const;

private:
    std::uint64_t m_generation = 0U;
    std::uint64_t m_nextSerial = 0U;
    std::set<std::uint64_t> m_activeNativeCalls;
};

// Single fixed-name, bounded, checksummed write-ahead record. No caller path
// or filename is persisted in the record.
class PalaceRoomTransitionJournalStore {
public:
    explicit PalaceRoomTransitionJournalStore(std::string directory);

    PalaceRoomTransitionJournalStatus save(
        const PalaceRoomTransitionIntentV1& intent) const;
    PalaceRoomTransitionJournalStatus load(
        PalaceRoomTransitionIntentV1& intent) const;
    bool clear() const;

private:
    std::string m_directory;
};

// Small durability seam. Tests can stop after any durable boundary without
// exposing fault injection in Palace Core.
class PalaceRoomTransitionDurability {
public:
    virtual ~PalaceRoomTransitionDurability() = default;

    virtual PalaceRoomTransitionJournalStatus loadIntent(
        PalaceRoomTransitionIntentV1& intent) const = 0;
    virtual PalaceRoomTransitionJournalStatus saveIntent(
        const PalaceRoomTransitionIntentV1& intent) = 0;
    virtual bool saveDeliverySession(
        const PalaceDeliverySession& session) = 0;
    virtual bool saveProjection(
        const PalaceProjection& projection) = 0;
    virtual bool clearIntent() = 0;
};

class PalaceRoomTransitionFileDurability final
    : public PalaceRoomTransitionDurability {
public:
    PalaceRoomTransitionFileDurability(
        std::string directory,
        const DeliverySessionStore& deliverySessionStore,
        const ProjectionStore& projectionStore);

    PalaceRoomTransitionJournalStatus loadIntent(
        PalaceRoomTransitionIntentV1& intent) const override;
    PalaceRoomTransitionJournalStatus saveIntent(
        const PalaceRoomTransitionIntentV1& intent) override;
    bool saveDeliverySession(
        const PalaceDeliverySession& session) override;
    bool saveProjection(
        const PalaceProjection& projection) override;
    bool clearIntent() override;

private:
    PalaceRoomTransitionJournalStore m_journal;
    const DeliverySessionStore& m_deliverySessionStore;
    const ProjectionStore& m_projectionStore;
};

enum class PalaceRoomTransitionStatus {
    Completed,
    NoPendingIntent,
    InvalidArgument,
    PendingIntentConflict,
    InvalidIntent,
    IntentPersistenceFailed,
    DeliveryPersistenceFailed,
    ProjectionPersistenceFailed,
    IntentClearFailed,
};

const char* palaceRoomTransitionStatusName(
    PalaceRoomTransitionStatus status);

struct PalaceRoomTransitionResult {
    PalaceRoomTransitionStatus status =
        PalaceRoomTransitionStatus::InvalidArgument;
    bool recovered = false;
    std::unique_ptr<PalaceDeliverySession> committedSession;
    std::optional<PalaceProjection> committedProjection;

    bool completed() const;
};

// Serializes ordinary projection persistence with both participant writes.
// A transition first blocks new ordinary writes, then waits for any admitted
// write to finish before acquiring its exclusive lease.
class PalaceRoomTransitionParticipantWriteGate {
public:
    class Lease {
    public:
        Lease() = default;
        Lease(Lease&&) noexcept = default;
        Lease& operator=(Lease&&) noexcept = default;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

        explicit operator bool() const;

    private:
        friend class PalaceRoomTransitionParticipantWriteGate;

        Lease(
            PalaceRoomTransitionParticipantWriteGate* owner,
            bool transition,
            std::unique_lock<std::mutex> lock);

        PalaceRoomTransitionParticipantWriteGate* m_owner =
            nullptr;
        bool m_transition = false;
        std::unique_lock<std::mutex> m_lock;
    };

    bool participantWritesBlocked() const;
    void blockParticipantWrites();
    Lease acquireParticipantWrite();
    Lease acquireTransitionWrite();
    bool unblockParticipantWrites(Lease& transitionLease);

private:
    std::atomic_bool m_blocked{true};
    std::mutex m_mutex;
};

// Startup may release participant writes only when both journal observations
// agree. Appearance or disappearance between preflight and recovery is a
// race, even if the second observation is otherwise valid. Any deferred
// projection state must also be durable before public operations resume.
bool palaceRoomTransitionStartupMayUnblockParticipantWrites(
    PalaceRoomTransitionJournalStatus preflight,
    PalaceRoomTransitionStatus recovery,
    bool projectionPersistenceComplete);

// Owns ordering invariant:
// intent -> Delivery state -> projection state -> clear intent -> live state.
// Returned live candidates exist only after every durable step succeeds.
class PalaceRoomTransitionCoordinator {
public:
    explicit PalaceRoomTransitionCoordinator(
        PalaceRoomTransitionDurability& durability);

    PalaceRoomTransitionResult transition(
        const std::string& logicalRoomId,
        const PalaceDeliverySession& desiredSession,
        const PalaceProjection& desiredProjection);
    PalaceRoomTransitionResult recover(
        const PalaceDeliverySession& authorityBoundSession);

private:
    PalaceRoomTransitionResult persistAndCommit(
        const PalaceDeliverySession& durableSession,
        const PalaceProjection& durableProjection,
        bool recovered);
    bool materialize(
        const PalaceRoomTransitionIntentV1& intent,
        const PalaceDeliverySession& authorityBoundSession,
        std::unique_ptr<PalaceDeliverySession>& session,
        PalaceProjection& projection) const;

    PalaceRoomTransitionDurability& m_durability;
};

} // namespace palace
