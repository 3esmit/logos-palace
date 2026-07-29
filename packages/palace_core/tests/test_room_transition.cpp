#include <logos_test.h>

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "palace_authority.h"
#include "palace_room_transition.h"
#include "palace_sha256.h"

namespace {

palace::AuthoritySnapshotV1 authoritySnapshot()
{
    palace::AuthoritySnapshotV1 snapshot;
    snapshot.palaceId = "palace-1";
    snapshot.ownerUserId = "alice";
    snapshot.entryRoomId = "atrium";
    snapshot.users = {
        {"alice", "alice-key", 1},
        {"carol", "carol-key", 3},
    };
    snapshot.rooms = {
        {"atrium", false, "", 9},
        {"lounge", false, "", 4},
    };
    return snapshot;
}

palace::DeliverySessionConfigV1 sessionConfig()
{
    palace::DeliverySessionConfigV1 config;
    config.networkId = "logos.test";
    config.palaceId = "palace-1";
    config.roomId = "atrium";
    config.roomEpoch = 9;
    config.senderUserId = "carol";
    config.senderKeyEpoch = 3;
    return config;
}

palace::PalaceDeliverySession configuredSession(
    const palace::AuthorityProjection& authority)
{
    palace::PalaceDeliverySession session(authority);
    LOGOS_ASSERT_TRUE(session.configure(sessionConfig()));
    LOGOS_ASSERT_TRUE(session.start().accepted);
    LOGOS_ASSERT_TRUE(session.callbacksRegistered(true).accepted);
    LOGOS_ASSERT_TRUE(session.nodeStarted(true).accepted);
    LOGOS_ASSERT_TRUE(
        session.connectionStateChanged(
            palace::DeliveryConnectionState::Connected).accepted);
    LOGOS_ASSERT_TRUE(session.subscriptionResult(true).accepted);
    return session;
}

struct DesiredRoom {
    palace::PalaceDeliverySession session;
    palace::PalaceProjection projection;
    std::vector<palace::DeliverySessionCommand> commands;
};

DesiredRoom desiredLounge(
    const palace::PalaceDeliverySession& current)
{
    DesiredRoom desired{
        current,
        palace::PalaceProjection{},
        {},
    };
    LOGOS_ASSERT_TRUE(desired.projection.enterRoom("lounge"));
    const palace::DeliverySessionTransition transition =
        desired.session.switchRoom("lounge", 4);
    LOGOS_ASSERT_TRUE(transition.accepted);
    desired.commands = transition.commands;
    return desired;
}

class MemoryDurability final
    : public palace::PalaceRoomTransitionDurability {
public:
    palace::PalaceRoomTransitionJournalStatus loadIntent(
        palace::PalaceRoomTransitionIntentV1& loaded) const override
    {
        if (invalidIntent)
            return palace::PalaceRoomTransitionJournalStatus::
                InvalidRecord;
        if (!intent.has_value()) {
            return palace::PalaceRoomTransitionJournalStatus::
                NotFound;
        }
        loaded = *intent;
        return palace::PalaceRoomTransitionJournalStatus::Loaded;
    }

    palace::PalaceRoomTransitionJournalStatus saveIntent(
        const palace::PalaceRoomTransitionIntentV1& saved) override
    {
        calls.push_back("intent");
        if (failIntent)
            return palace::PalaceRoomTransitionJournalStatus::IoError;
        if (intent.has_value()) {
            return palace::PalaceRoomTransitionJournalStatus::
                Conflict;
        }
        intent = saved;
        return palace::PalaceRoomTransitionJournalStatus::Loaded;
    }

    bool saveDeliverySession(
        const palace::PalaceDeliverySession& session) override
    {
        calls.push_back("session");
        if (failSession)
            return false;
        sessionState = session.canonicalState();
        return true;
    }

    bool saveProjection(
        const palace::PalaceProjection& projection) override
    {
        calls.push_back("projection");
        if (failProjection)
            return false;
        projectionState = projection.canonicalLocalState();
        return true;
    }

    bool clearIntent() override
    {
        calls.push_back("clear");
        if (failClear)
            return false;
        intent.reset();
        return true;
    }

    bool failIntent = false;
    bool failSession = false;
    bool failProjection = false;
    bool failClear = false;
    bool invalidIntent = false;
    std::optional<palace::PalaceRoomTransitionIntentV1> intent;
    std::string sessionState;
    std::string projectionState;
    std::vector<std::string> calls;
};

void assertRecoveredLounge(
    MemoryDurability& durability,
    const palace::PalaceDeliverySession& authorityBoundSession)
{
    durability.failIntent = false;
    durability.failSession = false;
    durability.failProjection = false;
    durability.failClear = false;
    durability.calls.clear();

    palace::PalaceRoomTransitionCoordinator restarted(
        durability);
    palace::PalaceRoomTransitionResult recovered =
        restarted.recover(authorityBoundSession);
    LOGOS_ASSERT_TRUE(recovered.completed());
    LOGOS_ASSERT_TRUE(recovered.recovered);
    LOGOS_ASSERT_EQ(
        recovered.committedSession->configuration().roomId,
        std::string("lounge"));
    LOGOS_ASSERT_EQ(
        recovered.committedSession->configuration().roomEpoch,
        static_cast<std::int64_t>(4));
    LOGOS_ASSERT_EQ(
        recovered.committedProjection->currentRoomId(),
        std::string("lounge"));
    LOGOS_ASSERT_EQ(
        palace::deliverySessionStateName(
            recovered.committedSession->state()),
        std::string("recovering"));
    LOGOS_ASSERT_FALSE(durability.intent.has_value());
    LOGOS_ASSERT_TRUE(
        durability.calls
        == std::vector<std::string>({
            "session", "projection", "clear"}));
}

} // namespace

LOGOS_TEST(room_transition_startup_write_gate_fails_closed_on_races) {
    const auto released =
        [](const palace::PalaceRoomTransitionJournalStatus preflight,
           const palace::PalaceRoomTransitionStatus recovery,
           const bool projectionPersistenceComplete = true) {
            palace::PalaceRoomTransitionParticipantWriteGate gate;
            auto transitionWrite =
                gate.acquireTransitionWrite();
            if (palace::
                    palaceRoomTransitionStartupMayUnblockParticipantWrites(
                        preflight,
                        recovery,
                        projectionPersistenceComplete)) {
                gate.unblockParticipantWrites(
                    transitionWrite);
            }
            return !gate.participantWritesBlocked();
        };

    LOGOS_ASSERT_FALSE(released(
        palace::PalaceRoomTransitionJournalStatus::InvalidRecord,
        palace::PalaceRoomTransitionStatus::InvalidIntent));
    LOGOS_ASSERT_FALSE(released(
        palace::PalaceRoomTransitionJournalStatus::NotFound,
        palace::PalaceRoomTransitionStatus::Completed));
    LOGOS_ASSERT_FALSE(released(
        palace::PalaceRoomTransitionJournalStatus::Loaded,
        palace::PalaceRoomTransitionStatus::NoPendingIntent));
    LOGOS_ASSERT_FALSE(released(
        palace::PalaceRoomTransitionJournalStatus::Loaded,
        palace::PalaceRoomTransitionStatus::InvalidIntent));
    LOGOS_ASSERT_TRUE(released(
        palace::PalaceRoomTransitionJournalStatus::NotFound,
        palace::PalaceRoomTransitionStatus::NoPendingIntent));
    LOGOS_ASSERT_TRUE(released(
        palace::PalaceRoomTransitionJournalStatus::Loaded,
        palace::PalaceRoomTransitionStatus::Completed));
    LOGOS_ASSERT_FALSE(released(
        palace::PalaceRoomTransitionJournalStatus::NotFound,
        palace::PalaceRoomTransitionStatus::NoPendingIntent,
        false));
    LOGOS_ASSERT_FALSE(released(
        palace::PalaceRoomTransitionJournalStatus::Loaded,
        palace::PalaceRoomTransitionStatus::Completed,
        false));
}

LOGOS_TEST(room_transition_waits_for_in_flight_projection_write) {
    palace::PalaceRoomTransitionParticipantWriteGate gate;
    {
        auto startupWrite = gate.acquireTransitionWrite();
        LOGOS_ASSERT_TRUE(startupWrite);
        LOGOS_ASSERT_TRUE(
            gate.unblockParticipantWrites(
                startupWrite));
    }

    std::mutex coordinationMutex;
    std::condition_variable coordination;
    bool writerHolding = false;
    bool releaseWriter = false;
    bool transitionBlocked = false;
    std::atomic_bool writerAdmitted{false};
    std::atomic_bool transitionAcquired{false};
    std::atomic_bool overlapObserved{false};
    std::atomic_bool racedWriterAdmitted{true};
    std::atomic_int activeProjectionWrites{0};
    palace::PalaceProjection latestProjection;
    palace::PalaceProjection stagedProjection;
    bool stagedLatestProjection = false;

    std::thread writer([&]() {
        auto participantWrite =
            gate.acquireParticipantWrite();
        writerAdmitted.store(
            static_cast<bool>(participantWrite),
            std::memory_order_release);
        if (participantWrite) {
            latestProjection.setSyncHealth(
                palace::SyncHealth::Degraded);
            activeProjectionWrites.fetch_add(
                1, std::memory_order_acq_rel);
            {
                std::lock_guard<std::mutex> lock(
                    coordinationMutex);
                writerHolding = true;
            }
            coordination.notify_all();
            {
                std::unique_lock<std::mutex> lock(
                    coordinationMutex);
                coordination.wait(
                    lock,
                    [&releaseWriter]() {
                        return releaseWriter;
                    });
            }
            activeProjectionWrites.fetch_sub(
                1, std::memory_order_acq_rel);
        }
    });
    {
        std::unique_lock<std::mutex> lock(
            coordinationMutex);
        coordination.wait(
            lock,
            [&writerHolding]() {
                return writerHolding;
            });
    }

    std::thread transition([&]() {
        gate.blockParticipantWrites();
        {
            std::lock_guard<std::mutex> lock(
                coordinationMutex);
            transitionBlocked = true;
        }
        coordination.notify_all();
        auto transitionWrite =
            gate.acquireTransitionWrite();
        transitionAcquired.store(
            static_cast<bool>(transitionWrite),
            std::memory_order_release);
        overlapObserved.store(
            activeProjectionWrites.load(
                std::memory_order_acquire) != 0,
            std::memory_order_release);
        if (transitionWrite) {
            stagedProjection = latestProjection;
            stagedLatestProjection =
                stagedProjection.enterRoom("lounge");
        }
    });
    {
        std::unique_lock<std::mutex> lock(
            coordinationMutex);
        coordination.wait(
            lock,
            [&transitionBlocked]() {
                return transitionBlocked;
            });
    }

    std::thread racedWriter([&]() {
        auto participantWrite =
            gate.acquireParticipantWrite();
        racedWriterAdmitted.store(
            static_cast<bool>(participantWrite),
            std::memory_order_release);
    });

    LOGOS_ASSERT_TRUE(gate.participantWritesBlocked());
    LOGOS_ASSERT_FALSE(
        transitionAcquired.load(
            std::memory_order_acquire));
    {
        std::lock_guard<std::mutex> lock(
            coordinationMutex);
        releaseWriter = true;
    }
    coordination.notify_all();

    writer.join();
    transition.join();
    racedWriter.join();
    LOGOS_ASSERT_TRUE(
        writerAdmitted.load(
            std::memory_order_acquire));
    LOGOS_ASSERT_TRUE(
        transitionAcquired.load(
            std::memory_order_acquire));
    LOGOS_ASSERT_FALSE(
        overlapObserved.load(
            std::memory_order_acquire));
    LOGOS_ASSERT_FALSE(
        racedWriterAdmitted.load(
            std::memory_order_acquire));
    LOGOS_ASSERT_TRUE(stagedLatestProjection);
    LOGOS_ASSERT_EQ(
        stagedProjection.currentRoomId(),
        std::string("lounge"));
    LOGOS_ASSERT_TRUE(
        stagedProjection.syncHealth()
        == palace::SyncHealth::Degraded);
}

LOGOS_TEST(room_transition_projection_entry_owns_first_delivery_config) {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path()
        / "logos-palace-room-entry-owner-contract";
    std::filesystem::remove_all(directory);
    palace::ProjectionStore projectionStore(
        directory.string());
    palace::PalaceProjection projection;
    LOGOS_ASSERT_TRUE(projectionStore.save(projection));

    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(
        authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::PalaceDeliverySession session(authority);
    palace::PalaceRoomTransitionParticipantWriteGate gate;
    {
        auto startupWrite = gate.acquireTransitionWrite();
        LOGOS_ASSERT_TRUE(
            gate.unblockParticipantWrites(
                startupWrite));
    }

    std::mutex deliveryOwner;
    std::mutex coordinationMutex;
    std::condition_variable coordination;
    bool projectionEntryHoldingDelivery = false;
    bool releaseProjectionEntry = false;
    bool deliveryConfigWaiting = false;
    std::atomic_bool projectionWriteAdmitted{false};
    std::atomic_bool projectionSaved{false};
    std::atomic_bool deliveryConfigured{false};

    std::thread projectionEntry([&]() {
        std::lock_guard<std::mutex> ownerLock(
            deliveryOwner);
        auto participantWrite =
            gate.acquireParticipantWrite();
        {
            std::lock_guard<std::mutex> lock(
                coordinationMutex);
            projectionEntryHoldingDelivery = true;
        }
        projectionWriteAdmitted.store(
            static_cast<bool>(participantWrite),
            std::memory_order_release);
        coordination.notify_all();
        {
            std::unique_lock<std::mutex> lock(
                coordinationMutex);
            coordination.wait(
                lock,
                [&releaseProjectionEntry]() {
                    return releaseProjectionEntry;
                });
        }
        projectionSaved.store(
            participantWrite
                && projectionStore.enterRoomDurably(
                    projection, "lounge"),
            std::memory_order_release);
    });
    {
        std::unique_lock<std::mutex> lock(
            coordinationMutex);
        coordination.wait(
            lock,
            [&projectionEntryHoldingDelivery]() {
                return projectionEntryHoldingDelivery;
            });
    }
    LOGOS_ASSERT_TRUE(
        projectionWriteAdmitted.load(
            std::memory_order_acquire));

    std::thread firstDeliveryConfig([&]() {
        {
            std::lock_guard<std::mutex> lock(
                coordinationMutex);
            deliveryConfigWaiting = true;
        }
        coordination.notify_all();
        std::lock_guard<std::mutex> ownerLock(
            deliveryOwner);
        palace::DeliverySessionConfigV1 config =
            sessionConfig();
        config.roomId = projection.currentRoomId();
        config.roomEpoch =
            authority.roomEpoch(config.roomId);
        deliveryConfigured.store(
            session.configure(config),
            std::memory_order_release);
    });
    {
        std::unique_lock<std::mutex> lock(
            coordinationMutex);
        coordination.wait(
            lock,
            [&deliveryConfigWaiting]() {
                return deliveryConfigWaiting;
            });
    }
    LOGOS_ASSERT_FALSE(
        deliveryConfigured.load(
            std::memory_order_acquire));
    {
        std::lock_guard<std::mutex> lock(
            coordinationMutex);
        releaseProjectionEntry = true;
    }
    coordination.notify_all();

    projectionEntry.join();
    firstDeliveryConfig.join();
    LOGOS_ASSERT_TRUE(
        projectionSaved.load(
            std::memory_order_acquire));
    LOGOS_ASSERT_TRUE(
        deliveryConfigured.load(
            std::memory_order_acquire));
    LOGOS_ASSERT_EQ(
        session.configuration().roomId,
        std::string("lounge"));
    LOGOS_ASSERT_EQ(
        session.configuration().roomEpoch,
        static_cast<std::int64_t>(4));
    palace::PalaceProjection restored;
    LOGOS_ASSERT_TRUE(projectionStore.load(restored));
    LOGOS_ASSERT_EQ(
        restored.currentRoomId(),
        session.configuration().roomId);
    std::filesystem::remove_all(directory);
}

LOGOS_TEST(room_transition_native_call_gate_requires_quiescence) {
    palace::PalaceRoomTransitionNativeCallGate gate;
    const std::uint64_t generation = gate.generation();
    palace::PalaceRoomTransitionNativeCallToken rejected;
    palace::PalaceRoomTransitionNativeCallToken first;
    palace::PalaceRoomTransitionNativeCallToken second;

    LOGOS_ASSERT_TRUE(gate.canBeginTransition(true));
    LOGOS_ASSERT_FALSE(
        gate.admitNativeCall(
            generation, false, rejected));
    LOGOS_ASSERT_TRUE(
        gate.admitNativeCall(
            generation, true, first));
    LOGOS_ASSERT_TRUE(
        gate.admitNativeCall(
            generation, true, second));
    LOGOS_ASSERT_EQ(
        gate.nativeCallsInFlight(),
        static_cast<std::size_t>(2));
    LOGOS_ASSERT_FALSE(gate.canBeginTransition(true));
    LOGOS_ASSERT_FALSE(gate.beginTransition(true));

    LOGOS_ASSERT_TRUE(
        gate.completeNativeCall(first, true));
    LOGOS_ASSERT_FALSE(gate.canBeginTransition(true));
    LOGOS_ASSERT_FALSE(
        gate.completeNativeCall(first, true));
    LOGOS_ASSERT_EQ(
        gate.nativeCallsInFlight(),
        static_cast<std::size_t>(1));
    LOGOS_ASSERT_TRUE(
        gate.completeNativeCall(second, true));
    LOGOS_ASSERT_TRUE(gate.canBeginTransition(true));
}

LOGOS_TEST(room_transition_native_call_gate_discards_unhealthy_result) {
    palace::PalaceRoomTransitionNativeCallGate gate;
    const std::uint64_t staleGeneration = gate.generation();
    palace::PalaceRoomTransitionNativeCallToken stale;

    LOGOS_ASSERT_TRUE(
        gate.admitNativeCall(
            staleGeneration, true, stale));
    LOGOS_ASSERT_FALSE(
        gate.completeNativeCall(stale, false));
    LOGOS_ASSERT_EQ(
        gate.nativeCallsInFlight(),
        static_cast<std::size_t>(0));
    LOGOS_ASSERT_FALSE(gate.canBeginTransition(false));
    LOGOS_ASSERT_TRUE(gate.beginTransition(true));
    palace::PalaceRoomTransitionNativeCallToken rejected;
    LOGOS_ASSERT_FALSE(
        gate.admitNativeCall(
            staleGeneration, true, rejected));
    palace::PalaceRoomTransitionNativeCallToken current;
    LOGOS_ASSERT_TRUE(
        gate.admitNativeCall(
            gate.generation(), true, current));
    LOGOS_ASSERT_FALSE(
        gate.completeNativeCall(stale, true));
    LOGOS_ASSERT_EQ(
        gate.nativeCallsInFlight(),
        static_cast<std::size_t>(1));
    LOGOS_ASSERT_TRUE(
        gate.completeNativeCall(current, true));
}

LOGOS_TEST(room_transition_native_call_batch_stays_busy_between_commands) {
    palace::PalaceRoomTransitionNativeCallGate gate;
    const std::uint64_t batchGeneration = gate.generation();
    palace::PalaceRoomTransitionNativeCallToken batch;
    std::size_t nativeCalls = 0U;
    const auto fakeNativeCall = [&gate, &nativeCalls]() {
        ++nativeCalls;
        LOGOS_ASSERT_FALSE(gate.beginTransition(true));
    };

    LOGOS_ASSERT_TRUE(
        gate.admitNativeCall(
            batchGeneration, true, batch));
    fakeNativeCall();
    fakeNativeCall();
    LOGOS_ASSERT_EQ(
        nativeCalls, static_cast<std::size_t>(2));
    LOGOS_ASSERT_TRUE(
        gate.completeNativeCall(batch, true));
    LOGOS_ASSERT_TRUE(gate.beginTransition(true));
}

LOGOS_TEST(room_transition_rejected_plan_keeps_batch_generation_admissible) {
    palace::PalaceRoomTransitionNativeCallGate gate;
    const std::uint64_t plannedGeneration = gate.generation();
    palace::PalaceRoomTransitionNativeCallToken batch;

    LOGOS_ASSERT_TRUE(gate.canBeginTransition(true));
    const bool roomPlanAccepted = false;
    LOGOS_ASSERT_FALSE(roomPlanAccepted);
    LOGOS_ASSERT_EQ(gate.generation(), plannedGeneration);
    LOGOS_ASSERT_TRUE(
        gate.admitNativeCall(
            plannedGeneration, true, batch));
    LOGOS_ASSERT_TRUE(
        gate.completeNativeCall(batch, true));
}

LOGOS_TEST(room_transition_repeated_enter_keeps_queued_batch_admissible) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(
        authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::PalaceDeliverySession current =
        configuredSession(authority);
    palace::PalaceProjection currentProjection;
    const palace::DeliverySessionTransition repeated =
        current.switchRoom("atrium", 9);
    LOGOS_ASSERT_TRUE(
        palace::palaceRoomTransitionAlreadyLive(
            repeated, currentProjection, "atrium"));

    palace::PalaceRoomTransitionParticipantWriteGate
        participantWrites;
    {
        auto startupWrite =
            participantWrites.acquireTransitionWrite();
        LOGOS_ASSERT_TRUE(
            participantWrites.unblockParticipantWrites(
                startupWrite));
    }
    palace::PalaceRoomTransitionNativeCallGate gate;
    const std::uint64_t queuedGeneration =
        gate.generation();
    palace::PalaceRoomTransitionNativeCallToken queued;
    LOGOS_ASSERT_TRUE(
        gate.admitNativeCall(
            queuedGeneration, true, queued));
    LOGOS_ASSERT_TRUE(
        gate.completeNativeCall(queued, true));
    participantWrites.blockParticipantWrites();
    auto noOpWrite =
        participantWrites.acquireTransitionWrite();
    LOGOS_ASSERT_TRUE(noOpWrite);
    LOGOS_ASSERT_TRUE(
        participantWrites.unblockParticipantWrites(
            noOpWrite));
    LOGOS_ASSERT_FALSE(
        participantWrites.participantWritesBlocked());
    LOGOS_ASSERT_EQ(
        gate.generation(), queuedGeneration);

    LOGOS_ASSERT_TRUE(currentProjection.enterRoom("lounge"));
    LOGOS_ASSERT_FALSE(
        palace::palaceRoomTransitionAlreadyLive(
            repeated, currentProjection, "atrium"));
}

LOGOS_TEST(room_transition_intent_precedes_both_participant_writes) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(
        authority.replaceFinalized(authoritySnapshot(), 1000));
    const palace::PalaceDeliverySession current =
        configuredSession(authority);
    const DesiredRoom desired = desiredLounge(current);
    LOGOS_ASSERT_FALSE(desired.commands.empty());
    MemoryDurability durability;
    palace::PalaceRoomTransitionCoordinator coordinator(
        durability);

    palace::PalaceRoomTransitionResult completed =
        coordinator.transition(
            "lounge", desired.session, desired.projection);

    LOGOS_ASSERT_TRUE(completed.completed());
    LOGOS_ASSERT_FALSE(completed.recovered);
    LOGOS_ASSERT_TRUE(
        durability.calls
        == std::vector<std::string>({
            "intent", "session", "projection", "clear"}));
    LOGOS_ASSERT_EQ(
        completed.committedSession->configuration().roomId,
        std::string("lounge"));
    LOGOS_ASSERT_EQ(
        completed.committedProjection->currentRoomId(),
        std::string("lounge"));
}

LOGOS_TEST(room_transition_restart_converges_after_intent_only) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(
        authority.replaceFinalized(authoritySnapshot(), 1000));
    const palace::PalaceDeliverySession current =
        configuredSession(authority);
    const DesiredRoom desired = desiredLounge(current);
    MemoryDurability durability;
    durability.failSession = true;
    palace::PalaceRoomTransitionCoordinator coordinator(
        durability);

    palace::PalaceRoomTransitionResult interrupted =
        coordinator.transition(
            "lounge", desired.session, desired.projection);

    LOGOS_ASSERT_TRUE(
        interrupted.status
        == palace::PalaceRoomTransitionStatus::
            DeliveryPersistenceFailed);
    LOGOS_ASSERT_FALSE(interrupted.committedSession);
    LOGOS_ASSERT_FALSE(
        interrupted.committedProjection.has_value());
    LOGOS_ASSERT_TRUE(durability.sessionState.empty());
    LOGOS_ASSERT_TRUE(durability.projectionState.empty());
    assertRecoveredLounge(durability, current);
}

LOGOS_TEST(room_transition_restart_converges_after_session_write) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(
        authority.replaceFinalized(authoritySnapshot(), 1000));
    const palace::PalaceDeliverySession current =
        configuredSession(authority);
    const DesiredRoom desired = desiredLounge(current);
    MemoryDurability durability;
    durability.failProjection = true;
    palace::PalaceRoomTransitionCoordinator coordinator(
        durability);

    palace::PalaceRoomTransitionResult interrupted =
        coordinator.transition(
            "lounge", desired.session, desired.projection);

    LOGOS_ASSERT_TRUE(
        interrupted.status
        == palace::PalaceRoomTransitionStatus::
            ProjectionPersistenceFailed);
    LOGOS_ASSERT_FALSE(durability.sessionState.empty());
    LOGOS_ASSERT_TRUE(durability.projectionState.empty());
    LOGOS_ASSERT_FALSE(interrupted.committedSession);
    assertRecoveredLounge(durability, current);
}

LOGOS_TEST(room_transition_restart_converges_after_projection_write) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(
        authority.replaceFinalized(authoritySnapshot(), 1000));
    const palace::PalaceDeliverySession current =
        configuredSession(authority);
    const DesiredRoom desired = desiredLounge(current);
    MemoryDurability durability;
    durability.failClear = true;
    palace::PalaceRoomTransitionCoordinator coordinator(
        durability);

    palace::PalaceRoomTransitionResult interrupted =
        coordinator.transition(
            "lounge", desired.session, desired.projection);

    LOGOS_ASSERT_TRUE(
        interrupted.status
        == palace::PalaceRoomTransitionStatus::
            IntentClearFailed);
    LOGOS_ASSERT_FALSE(durability.sessionState.empty());
    LOGOS_ASSERT_FALSE(durability.projectionState.empty());
    LOGOS_ASSERT_FALSE(interrupted.committedSession);
    assertRecoveredLounge(durability, current);
}

LOGOS_TEST(room_transition_failures_expose_no_live_state_or_commands) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(
        authority.replaceFinalized(authoritySnapshot(), 1000));
    const palace::PalaceDeliverySession current =
        configuredSession(authority);
    const std::string currentSessionState =
        current.canonicalState();
    palace::PalaceProjection currentProjection;
    const std::string currentProjectionState =
        currentProjection.canonicalLocalState();
    const DesiredRoom desired = desiredLounge(current);
    LOGOS_ASSERT_FALSE(desired.commands.empty());
    MemoryDurability durability;
    durability.failIntent = true;
    palace::PalaceRoomTransitionCoordinator coordinator(
        durability);
    std::size_t executedCommands = 0U;

    palace::PalaceRoomTransitionResult rejected =
        coordinator.transition(
            "lounge", desired.session, desired.projection);
    if (rejected.completed())
        executedCommands += desired.commands.size();

    LOGOS_ASSERT_TRUE(
        rejected.status
        == palace::PalaceRoomTransitionStatus::
            IntentPersistenceFailed);
    LOGOS_ASSERT_FALSE(rejected.committedSession);
    LOGOS_ASSERT_EQ(executedCommands, static_cast<std::size_t>(0));
    LOGOS_ASSERT_EQ(current.canonicalState(), currentSessionState);
    LOGOS_ASSERT_EQ(
        currentProjection.canonicalLocalState(),
        currentProjectionState);
    LOGOS_ASSERT_TRUE(
        durability.calls
        == std::vector<std::string>({"intent"}));
}

LOGOS_TEST(room_transition_rejects_pending_or_invalid_intent_without_writes) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(
        authority.replaceFinalized(authoritySnapshot(), 1000));
    const palace::PalaceDeliverySession current =
        configuredSession(authority);
    const DesiredRoom desired = desiredLounge(current);
    MemoryDurability durability;
    durability.intent =
        palace::PalaceRoomTransitionIntentV1{
            "lounge",
            desired.session.canonicalState(),
            desired.projection.canonicalLocalState(),
        };
    palace::PalaceRoomTransitionCoordinator coordinator(
        durability);

    const palace::PalaceRoomTransitionResult conflict =
        coordinator.transition(
            "lounge", desired.session, desired.projection);
    LOGOS_ASSERT_TRUE(
        conflict.status
        == palace::PalaceRoomTransitionStatus::
            PendingIntentConflict);
    LOGOS_ASSERT_TRUE(durability.calls.empty());

    durability.invalidIntent = true;
    const palace::PalaceRoomTransitionResult invalid =
        coordinator.recover(current);
    LOGOS_ASSERT_TRUE(
        invalid.status
        == palace::PalaceRoomTransitionStatus::InvalidIntent);
    LOGOS_ASSERT_TRUE(durability.calls.empty());
}

LOGOS_TEST(room_transition_recovery_revalidates_finalized_room_authority) {
    {
        palace::AuthorityProjection authority;
        LOGOS_ASSERT_TRUE(
            authority.replaceFinalized(
                authoritySnapshot(), 1000));
        const palace::PalaceDeliverySession current =
            configuredSession(authority);
        const DesiredRoom desired = desiredLounge(current);
        MemoryDurability durability;
        durability.intent =
            palace::PalaceRoomTransitionIntentV1{
                "lounge",
                desired.session.canonicalState(),
                desired.projection.canonicalLocalState(),
            };
        palace::AuthoritySnapshotV1 stale =
            authoritySnapshot();
        stale.rooms[1].roomEpoch = 5;
        LOGOS_ASSERT_TRUE(
            authority.replaceFinalized(stale, 1001));

        palace::PalaceRoomTransitionCoordinator coordinator(
            durability);
        const palace::PalaceRoomTransitionResult rejected =
            coordinator.recover(current);
        LOGOS_ASSERT_TRUE(
            rejected.status
            == palace::PalaceRoomTransitionStatus::
                InvalidIntent);
        LOGOS_ASSERT_TRUE(durability.calls.empty());
    }

    {
        palace::AuthorityProjection authority;
        LOGOS_ASSERT_TRUE(
            authority.replaceFinalized(
                authoritySnapshot(), 1000));
        const palace::PalaceDeliverySession current =
            configuredSession(authority);
        const DesiredRoom desired = desiredLounge(current);
        MemoryDurability durability;
        durability.intent =
            palace::PalaceRoomTransitionIntentV1{
                "lounge",
                desired.session.canonicalState(),
                desired.projection.canonicalLocalState(),
            };
        palace::AuthoritySnapshotV1 locked =
            authoritySnapshot();
        locked.rooms[1].locked = true;
        LOGOS_ASSERT_TRUE(
            authority.replaceFinalized(locked, 1001));

        palace::PalaceRoomTransitionCoordinator coordinator(
            durability);
        const palace::PalaceRoomTransitionResult rejected =
            coordinator.recover(current);
        LOGOS_ASSERT_TRUE(
            rejected.status
            == palace::PalaceRoomTransitionStatus::
                InvalidIntent);
        LOGOS_ASSERT_TRUE(durability.calls.empty());
    }
}

LOGOS_TEST(room_transition_file_journal_round_trip_is_exact_and_checksummed) {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path()
        / "logos-palace-room-transition-journal-contract";
    std::filesystem::remove_all(directory);
    palace::PalaceRoomTransitionJournalStore store(
        directory.string());
    const palace::PalaceRoomTransitionIntentV1 intent{
        "lounge",
        "version=1\nconfig;state\n",
        "version=1;room=lounge;sync=offline",
    };

    LOGOS_ASSERT_TRUE(
        store.save(intent)
        == palace::PalaceRoomTransitionJournalStatus::Loaded);
    LOGOS_ASSERT_TRUE(
        store.save(intent)
        == palace::PalaceRoomTransitionJournalStatus::Conflict);
    palace::PalaceRoomTransitionIntentV1 loaded;
    LOGOS_ASSERT_TRUE(
        store.load(loaded)
        == palace::PalaceRoomTransitionJournalStatus::Loaded);
    LOGOS_ASSERT_EQ(loaded.logicalRoomId, intent.logicalRoomId);
    LOGOS_ASSERT_EQ(
        loaded.deliverySessionState,
        intent.deliverySessionState);
    LOGOS_ASSERT_EQ(
        loaded.projectionState,
        intent.projectionState);
    std::filesystem::permissions(
        directory / "room-transition-v1",
        std::filesystem::perms::group_read,
        std::filesystem::perm_options::add);
    LOGOS_ASSERT_TRUE(
        store.load(loaded)
        == palace::PalaceRoomTransitionJournalStatus::
            InvalidRecord);
    std::filesystem::permissions(
        directory / "room-transition-v1",
        std::filesystem::perms::owner_read
            | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace);

    std::string record;
    {
        std::ifstream input(
            directory / "room-transition-v1",
            std::ios::binary);
        record.assign(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
    }
    const std::size_t checksumLine = record.find('\n');
    std::string payload = record.substr(checksumLine + 1U);
    const std::string canonical = "room-bytes=6";
    const std::size_t roomLength = payload.find(canonical);
    LOGOS_ASSERT_TRUE(roomLength != std::string::npos);
    payload.replace(
        roomLength, canonical.size(), "room-bytes=06");
    {
        std::ofstream output(
            directory / "room-transition-v1",
            std::ios::binary | std::ios::trunc);
        output << palace::crypto::sha256Hex(payload)
               << '\n' << payload;
    }
    LOGOS_ASSERT_TRUE(
        store.load(loaded)
        == palace::PalaceRoomTransitionJournalStatus::
            InvalidRecord);
    LOGOS_ASSERT_TRUE(store.clear());
    LOGOS_ASSERT_TRUE(
        store.load(loaded)
        == palace::PalaceRoomTransitionJournalStatus::NotFound);
    std::filesystem::remove_all(directory);
}

LOGOS_TEST(room_transition_file_journal_rejects_unbounded_or_aliased_paths) {
    palace::PalaceRoomTransitionJournalStore oversized(
        std::string(4097U, 'x'));
    const palace::PalaceRoomTransitionIntentV1 intent{
        "lounge",
        "version=1\nconfig;state\n",
        "version=1;room=lounge;sync=offline",
    };
    LOGOS_ASSERT_TRUE(
        oversized.save(intent)
        == palace::PalaceRoomTransitionJournalStatus::IoError);

    const std::filesystem::path root =
        std::filesystem::temp_directory_path()
        / "logos-palace-room-transition-symlink-contract";
    const std::filesystem::path target = root.string() + "-target";
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(target);
    std::filesystem::create_directories(target);
    std::filesystem::create_directory_symlink(target, root);
    palace::PalaceRoomTransitionJournalStore aliased(root.string());
    LOGOS_ASSERT_TRUE(
        aliased.save(intent)
        == palace::PalaceRoomTransitionJournalStatus::IoError);
    palace::PalaceRoomTransitionIntentV1 aliasedIntent;
    LOGOS_ASSERT_TRUE(
        aliased.load(aliasedIntent)
        == palace::PalaceRoomTransitionJournalStatus::
            InvalidRecord);
    std::filesystem::remove(root);
    std::filesystem::remove_all(target);

    const std::filesystem::path collision =
        std::filesystem::temp_directory_path()
        / "logos-palace-room-transition-temp-collision";
    const std::filesystem::path collisionTarget =
        collision.string() + "-target";
    std::filesystem::remove_all(collision);
    std::filesystem::remove(collisionTarget);
    std::filesystem::create_directories(collision);
    {
        std::ofstream output(collisionTarget);
        output << "unchanged";
    }
    std::filesystem::create_symlink(
        collisionTarget,
        collision / "room-transition-v1.next");
    palace::PalaceRoomTransitionJournalStore collided(
        collision.string());
    LOGOS_ASSERT_TRUE(
        collided.save(intent)
        == palace::PalaceRoomTransitionJournalStatus::IoError);
    std::ifstream preserved(collisionTarget);
    std::string preservedBytes;
    preserved >> preservedBytes;
    LOGOS_ASSERT_EQ(
        preservedBytes, std::string("unchanged"));
    std::filesystem::remove_all(collision);
    std::filesystem::remove(collisionTarget);
}
