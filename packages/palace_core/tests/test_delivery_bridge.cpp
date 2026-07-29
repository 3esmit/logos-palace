#include <logos_test.h>

#include <type_traits>

#include "palace_delivery_acceptance_fixture.h"
#include "palace_delivery_bridge.h"

LOGOS_TEST(delivery_request_correlation_is_bounded_and_terminally_consumed) {
    palace::DeliveryRequestCorrelation correlation(2);
    LOGOS_ASSERT_TRUE(correlation.bind("module-1", "logical-1"));
    LOGOS_ASSERT_FALSE(correlation.bind("module-1", "logical-2"));
    LOGOS_ASSERT_FALSE(correlation.bind("module-2", "logical-1"));
    LOGOS_ASSERT_TRUE(correlation.bind("module-2", "logical-2"));
    LOGOS_ASSERT_FALSE(correlation.bind("module-3", "logical-3"));
    LOGOS_ASSERT_EQ(
        *correlation.logicalRequestId("module-1"),
        std::string("logical-1"));
    LOGOS_ASSERT_EQ(
        *correlation.take("module-1"),
        std::string("logical-1"));
    LOGOS_ASSERT_FALSE(correlation.logicalRequestId("module-1").has_value());
    LOGOS_ASSERT_EQ(correlation.size(), static_cast<std::size_t>(1));
}

LOGOS_TEST(delivery_bridge_normalizes_only_known_connection_states) {
    LOGOS_ASSERT_EQ(
        static_cast<int>(*palace::parseDeliveryConnectionState("Connected")),
        static_cast<int>(palace::DeliveryConnectionState::Connected));
    LOGOS_ASSERT_EQ(
        static_cast<int>(
            *palace::parseDeliveryConnectionState("PartiallyConnected")),
        static_cast<int>(palace::DeliveryConnectionState::Connected));
    LOGOS_ASSERT_EQ(
        static_cast<int>(
            *palace::parseDeliveryConnectionState("FullyConnected")),
        static_cast<int>(palace::DeliveryConnectionState::Connected));
    LOGOS_ASSERT_EQ(
        static_cast<int>(*palace::parseDeliveryConnectionState("not_connected")),
        static_cast<int>(palace::DeliveryConnectionState::Disconnected));
    LOGOS_ASSERT_FALSE(
        palace::parseDeliveryConnectionState("probably-connected").has_value());
}

LOGOS_TEST(delivery_bridge_classifies_only_fixed_rejection_reasons) {
    LOGOS_ASSERT_EQ(
        static_cast<int>(palace::classifyDeliveryRejection(
            "wrong-palace-room-or-epoch")),
        static_cast<int>(palace::DeliveryRejectionClass::Scope));
    LOGOS_ASSERT_EQ(
        static_cast<int>(palace::classifyDeliveryRejection(
            "expired-or-invalid-time")),
        static_cast<int>(palace::DeliveryRejectionClass::Expired));
    LOGOS_ASSERT_EQ(
        static_cast<int>(palace::classifyDeliveryRejection(
            "bad-signature-or-key-binding")),
        static_cast<int>(palace::DeliveryRejectionClass::Signature));
    LOGOS_ASSERT_EQ(
        static_cast<int>(palace::classifyDeliveryRejection(
            "duplicate-or-replayed-sequence")),
        static_cast<int>(palace::DeliveryRejectionClass::Replay));
    LOGOS_ASSERT_EQ(
        static_cast<int>(palace::classifyDeliveryRejection(
            "reorder-gap-exceeded")),
        static_cast<int>(palace::DeliveryRejectionClass::Replay));
    LOGOS_ASSERT_EQ(
        static_cast<int>(palace::classifyDeliveryRejection(
            "invalid-or-banned-payload")),
        static_cast<int>(palace::DeliveryRejectionClass::Payload));
    LOGOS_ASSERT_EQ(
        static_cast<int>(palace::classifyDeliveryRejection(
            "attacker-controlled-reason")),
        static_cast<int>(palace::DeliveryRejectionClass::Other));
    LOGOS_ASSERT_EQ(
        static_cast<int>(palace::classifyDeliveryRejection(
            "reorder-buffer-count-exceeded")),
        static_cast<int>(palace::DeliveryRejectionClass::Other));
}

LOGOS_TEST(delivery_recovery_overflow_stops_then_restarts_from_native_truth) {
    palace::DeliveryRecoveryCoordinator recovery;

    const palace::DeliveryRecoveryTransition overflow =
        recovery.callbackQueueOverflow(true);
    LOGOS_ASSERT_TRUE(overflow.accepted);
    LOGOS_ASSERT_TRUE(overflow.terminal);
    LOGOS_ASSERT_TRUE(overflow.interruptPendingWork);
    LOGOS_ASSERT_EQ(overflow.actions.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(
        static_cast<int>(overflow.actions.at(0).kind),
        static_cast<int>(palace::DeliveryRecoveryActionKind::StopNode));
    LOGOS_ASSERT_EQ(overflow.actions.at(0).epoch, static_cast<std::uint64_t>(1));
    const std::string stopCommand = overflow.actions.at(0).commandId;

    const palace::DeliveryRecoveryTransition duplicateOverflow =
        recovery.callbackQueueOverflow(true);
    LOGOS_ASSERT_TRUE(duplicateOverflow.accepted);
    LOGOS_ASSERT_FALSE(duplicateOverflow.terminal);
    LOGOS_ASSERT_EQ(
        duplicateOverflow.actions.size(), static_cast<std::size_t>(0));

    const palace::DeliveryRecoveryTransition stopDispatched =
        recovery.stopDispatchResult(stopCommand, true);
    LOGOS_ASSERT_TRUE(stopDispatched.accepted);
    LOGOS_ASSERT_EQ(
        static_cast<int>(recovery.state()),
        static_cast<int>(
            palace::DeliveryRecoveryState::WaitingForNodeStopped));

    const palace::DeliveryRecoveryTransition reorderedStarted =
        recovery.nodeStarted(true);
    LOGOS_ASSERT_FALSE(reorderedStarted.accepted);
    LOGOS_ASSERT_FALSE(reorderedStarted.forwardNodeStarted);
    LOGOS_ASSERT_EQ(
        reorderedStarted.actions.size(), static_cast<std::size_t>(0));

    const palace::DeliveryRecoveryTransition stopped =
        recovery.nodeStopped(true);
    LOGOS_ASSERT_TRUE(stopped.accepted);
    LOGOS_ASSERT_EQ(stopped.actions.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(
        static_cast<int>(stopped.actions.at(0).kind),
        static_cast<int>(
            palace::DeliveryRecoveryActionKind::QueryNodeStatus));
    const std::string statusCommand = stopped.actions.at(0).commandId;

    const palace::DeliveryRecoveryTransition duplicateStopped =
        recovery.nodeStopped(true);
    LOGOS_ASSERT_TRUE(duplicateStopped.accepted);
    LOGOS_ASSERT_EQ(
        duplicateStopped.actions.size(), static_cast<std::size_t>(0));

    LOGOS_ASSERT_FALSE(
        recovery.nodeStatusResult(
            "delivery-recovery-0-status-stale",
            true,
            palace::DeliveryNativeNodeState::Stopped)
            .accepted);

    const palace::DeliveryRecoveryTransition stoppedStatus =
        recovery.nodeStatusResult(
            statusCommand,
            true,
            palace::DeliveryNativeNodeState::Stopped);
    LOGOS_ASSERT_TRUE(stoppedStatus.accepted);
    LOGOS_ASSERT_TRUE(stoppedStatus.nativeRunning.has_value());
    LOGOS_ASSERT_FALSE(*stoppedStatus.nativeRunning);
    LOGOS_ASSERT_EQ(
        stoppedStatus.actions.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(
        static_cast<int>(stoppedStatus.actions.at(0).kind),
        static_cast<int>(
            palace::DeliveryRecoveryActionKind::RestartSession));
    const std::string restartCommand =
        stoppedStatus.actions.at(0).commandId;

    LOGOS_ASSERT_FALSE(
        recovery.nodeStatusResult(
            statusCommand,
            true,
            palace::DeliveryNativeNodeState::Stopped)
            .accepted);

    const palace::DeliveryRecoveryTransition restartDispatched =
        recovery.restartSessionResult(restartCommand, true);
    LOGOS_ASSERT_TRUE(restartDispatched.accepted);
    LOGOS_ASSERT_EQ(
        static_cast<int>(recovery.state()),
        static_cast<int>(
            palace::DeliveryRecoveryState::WaitingForNodeStarted));

    const palace::DeliveryRecoveryTransition started =
        recovery.nodeStarted(true);
    LOGOS_ASSERT_TRUE(started.accepted);
    LOGOS_ASSERT_TRUE(started.forwardNodeStarted);
    LOGOS_ASSERT_FALSE(started.terminal);
    LOGOS_ASSERT_EQ(started.actions.size(), static_cast<std::size_t>(0));
    LOGOS_ASSERT_EQ(
        static_cast<int>(recovery.state()),
        static_cast<int>(palace::DeliveryRecoveryState::Idle));

    const palace::DeliveryRecoveryTransition duplicateStarted =
        recovery.nodeStarted(true);
    LOGOS_ASSERT_TRUE(duplicateStarted.accepted);
    LOGOS_ASSERT_TRUE(duplicateStarted.forwardNodeStarted);
    LOGOS_ASSERT_FALSE(duplicateStarted.terminal);
    LOGOS_ASSERT_EQ(
        duplicateStarted.actions.size(), static_cast<std::size_t>(0));

    const palace::DeliveryRecoveryTransition lateStopped =
        recovery.nodeStopped(true);
    LOGOS_ASSERT_FALSE(lateStopped.accepted);
    LOGOS_ASSERT_EQ(lateStopped.actions.size(), static_cast<std::size_t>(0));
}

LOGOS_TEST(delivery_recovery_bounds_stop_failures_without_command_storm) {
    palace::DeliveryRecoveryCoordinator recovery;
    palace::DeliveryRecoveryTransition current =
        recovery.callbackQueueOverflow(true);
    LOGOS_ASSERT_EQ(current.actions.size(), static_cast<std::size_t>(1));

    for (std::size_t attempt = 0; attempt < 3U; ++attempt) {
        const std::string stopCommand = current.actions.at(0).commandId;
        current = recovery.stopDispatchResult(stopCommand, false);
        LOGOS_ASSERT_TRUE(current.accepted);
        LOGOS_ASSERT_EQ(current.actions.size(), static_cast<std::size_t>(1));
        LOGOS_ASSERT_EQ(
            static_cast<int>(current.actions.at(0).kind),
            static_cast<int>(
                palace::DeliveryRecoveryActionKind::QueryNodeStatus));

        const std::string statusCommand = current.actions.at(0).commandId;
        current = recovery.nodeStatusResult(
            statusCommand,
            true,
            palace::DeliveryNativeNodeState::Running);
        LOGOS_ASSERT_TRUE(current.accepted);
        LOGOS_ASSERT_TRUE(current.nativeRunning.has_value());
        LOGOS_ASSERT_TRUE(*current.nativeRunning);
        if (attempt < 2U) {
            LOGOS_ASSERT_EQ(
                current.actions.size(), static_cast<std::size_t>(1));
            LOGOS_ASSERT_EQ(
                static_cast<int>(current.actions.at(0).kind),
                static_cast<int>(
                    palace::DeliveryRecoveryActionKind::StopNode));
        }
    }

    LOGOS_ASSERT_EQ(current.actions.size(), static_cast<std::size_t>(0));
    LOGOS_ASSERT_EQ(
        static_cast<int>(recovery.state()),
        static_cast<int>(
            palace::DeliveryRecoveryState::ReconciliationRequired));
    LOGOS_ASSERT_EQ(
        recovery.callbackQueueOverflow(true).actions.size(),
        static_cast<std::size_t>(0));

    const std::uint64_t previousEpoch = recovery.epoch();
    const palace::DeliveryRecoveryTransition resumed = recovery.resume(true);
    LOGOS_ASSERT_TRUE(resumed.accepted);
    LOGOS_ASSERT_TRUE(resumed.interruptPendingWork);
    LOGOS_ASSERT_FALSE(resumed.terminal);
    LOGOS_ASSERT_EQ(resumed.actions.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(recovery.epoch(), previousEpoch + 1U);
    LOGOS_ASSERT_EQ(
        static_cast<int>(resumed.actions.at(0).kind),
        static_cast<int>(palace::DeliveryRecoveryActionKind::StopNode));
    LOGOS_ASSERT_EQ(
        recovery.resume(true).actions.size(), static_cast<std::size_t>(0));
}

LOGOS_TEST(delivery_recovery_bounds_failed_status_queries) {
    palace::DeliveryRecoveryCoordinator recovery;
    palace::DeliveryRecoveryTransition current =
        recovery.callbackQueueOverflow(false);
    LOGOS_ASSERT_TRUE(current.terminal);

    for (std::size_t attempt = 0; attempt < 3U; ++attempt) {
        LOGOS_ASSERT_EQ(current.actions.size(), static_cast<std::size_t>(1));
        LOGOS_ASSERT_EQ(
            static_cast<int>(current.actions.at(0).kind),
            static_cast<int>(
                palace::DeliveryRecoveryActionKind::QueryNodeStatus));
        const std::string statusCommand = current.actions.at(0).commandId;
        current = recovery.nodeStatusResult(
            statusCommand,
            false,
            palace::DeliveryNativeNodeState::Unknown);
        LOGOS_ASSERT_TRUE(current.accepted);
    }

    LOGOS_ASSERT_EQ(current.actions.size(), static_cast<std::size_t>(0));
    LOGOS_ASSERT_EQ(
        static_cast<int>(recovery.state()),
        static_cast<int>(
            palace::DeliveryRecoveryState::ReconciliationRequired));
}

LOGOS_TEST(delivery_recovery_late_stop_result_cannot_authorize_restart) {
    palace::DeliveryRecoveryCoordinator recovery;
    const palace::DeliveryRecoveryTransition overflow =
        recovery.callbackQueueOverflow(true);
    const std::string stopCommand = overflow.actions.at(0).commandId;

    const palace::DeliveryRecoveryTransition stopped =
        recovery.nodeStopped(true);
    LOGOS_ASSERT_TRUE(stopped.accepted);
    LOGOS_ASSERT_EQ(stopped.actions.size(), static_cast<std::size_t>(1));
    const std::string statusCommand = stopped.actions.at(0).commandId;

    LOGOS_ASSERT_FALSE(
        recovery.stopDispatchResult(stopCommand, true).accepted);
    LOGOS_ASSERT_EQ(
        static_cast<int>(recovery.state()),
        static_cast<int>(
            palace::DeliveryRecoveryState::StatusQueryPending));

    const palace::DeliveryRecoveryTransition stopping =
        recovery.nodeStatusResult(
            statusCommand,
            true,
            palace::DeliveryNativeNodeState::Stopping);
    LOGOS_ASSERT_TRUE(stopping.accepted);
    LOGOS_ASSERT_TRUE(*stopping.nativeRunning);
    LOGOS_ASSERT_EQ(stopping.actions.size(), static_cast<std::size_t>(0));

    const palace::DeliveryRecoveryTransition stoppedAgain =
        recovery.nodeStopped(true);
    LOGOS_ASSERT_EQ(stoppedAgain.actions.size(), static_cast<std::size_t>(1));
    const palace::DeliveryRecoveryTransition confirmedStopped =
        recovery.nodeStatusResult(
            stoppedAgain.actions.at(0).commandId,
            true,
            palace::DeliveryNativeNodeState::Stopped);
    LOGOS_ASSERT_TRUE(confirmedStopped.accepted);
    LOGOS_ASSERT_EQ(
        confirmedStopped.actions.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(
        static_cast<int>(confirmedStopped.actions.at(0).kind),
        static_cast<int>(
            palace::DeliveryRecoveryActionKind::RestartSession));
}

LOGOS_TEST(delivery_recovery_late_node_stopped_requires_fresh_native_truth) {
    palace::DeliveryRecoveryCoordinator recovery;
    palace::DeliveryRecoveryTransition current =
        recovery.callbackQueueOverflow(true);
    current = recovery.stopDispatchResult(
        current.actions.at(0).commandId, true);
    LOGOS_ASSERT_EQ(current.actions.size(), static_cast<std::size_t>(0));

    current = recovery.nodeStopped(true);
    current = recovery.nodeStatusResult(
        current.actions.at(0).commandId,
        true,
        palace::DeliveryNativeNodeState::Stopped);
    LOGOS_ASSERT_EQ(current.actions.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(
        static_cast<int>(current.actions.at(0).kind),
        static_cast<int>(
            palace::DeliveryRecoveryActionKind::RestartSession));
    current = recovery.restartSessionResult(
        current.actions.at(0).commandId, true);
    LOGOS_ASSERT_EQ(
        static_cast<int>(recovery.state()),
        static_cast<int>(
            palace::DeliveryRecoveryState::WaitingForNodeStarted));

    const palace::DeliveryRecoveryTransition lateStopped =
        recovery.nodeStopped(true);
    LOGOS_ASSERT_TRUE(lateStopped.accepted);
    LOGOS_ASSERT_EQ(lateStopped.actions.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(
        static_cast<int>(lateStopped.actions.at(0).kind),
        static_cast<int>(
            palace::DeliveryRecoveryActionKind::QueryNodeStatus));
    LOGOS_ASSERT_EQ(
        recovery.nodeStopped(true).actions.size(),
        static_cast<std::size_t>(0));
    LOGOS_ASSERT_FALSE(recovery.nodeStarted(true).accepted);

    current = recovery.nodeStatusResult(
        lateStopped.actions.at(0).commandId,
        true,
        palace::DeliveryNativeNodeState::Running);
    LOGOS_ASSERT_TRUE(current.accepted);
    LOGOS_ASSERT_EQ(current.actions.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(
        static_cast<int>(current.actions.at(0).kind),
        static_cast<int>(palace::DeliveryRecoveryActionKind::StopNode));
}

LOGOS_TEST(delivery_bridge_projection_is_deterministic_and_length_framed) {
    palace::DeliveryParticipantProjectionV1 participant;
    participant.userId = "alice";
    participant.present = true;
    participant.displayName = "Alice; Admin";
    participant.presenceExpiresAt = 100;
    participant.hasMotion = true;
    participant.motionX = 25;
    participant.motionY = 50;
    participant.motionExpiresAt = 90;
    participant.speech = "hello; lounge";
    participant.speechExpiresAt = 80;
    participant.propIds = {"badge", "test-prop"};

    LOGOS_ASSERT_EQ(
        palace::canonicalParticipantProjection({participant}),
        std::string(
            "version=1;participants=1\n"
            "participant=5:alice;present=1;display=12:Alice; Admin"
            ";presence_expires=100;motion=25,50,90"
            ";speech=13:hello; lounge;speech_expires=80"
            ";props=2:5:badge9:test-prop\n"));
}

LOGOS_TEST(delivery_acceptance_fixture_uses_real_bound_ed25519_identities) {
    static_assert(!std::is_copy_constructible_v<palace::Ed25519KeyPair>);
    static_assert(!std::is_copy_assignable_v<palace::Ed25519KeyPair>);
    static_assert(std::is_move_constructible_v<palace::Ed25519KeyPair>);

    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(
        palace::bootstrapDeliveryAcceptanceAuthority(authority));

    palace::DeliveryAcceptanceIdentity alice;
    palace::DeliveryAcceptanceIdentity bob;
    LOGOS_ASSERT_TRUE(
        palace::deliveryAcceptanceIdentity("alice", alice));
    LOGOS_ASSERT_TRUE(
        palace::deliveryAcceptanceIdentity("bob", bob));
    LOGOS_ASSERT_FALSE(
        palace::deliveryAcceptanceIdentity("mallory", bob));
    LOGOS_ASSERT_EQ(
        authority.deliveryKeyFor("alice", 1),
        alice.signer.publicKeyHex());
    LOGOS_ASSERT_EQ(
        authority.deliveryKeyFor("acceptance-injector", 4),
        std::string(
            "278117fc144c72340f67d0f2316e8386"
            "ceffbf2b2428c9c51fef7c597f1d426e"));
    LOGOS_ASSERT_EQ(
        authority.deliveryKeyFor("bob", 2),
        bob.signer.publicKeyHex());

    const std::string message = "gate-2-fixture-envelope";
    palace::Ed25519EnvelopeVerifier verifier;
    LOGOS_ASSERT_TRUE(verifier.verify(
        alice.signer.publicKeyHex(), message,
        alice.signer.signHex(message)));
    LOGOS_ASSERT_FALSE(verifier.verify(
        bob.signer.publicKeyHex(), message,
        alice.signer.signHex(message)));
    LOGOS_ASSERT_EQ(
        palace::deliveryAcceptanceRoomEpoch("atrium"),
        static_cast<std::int64_t>(9));
    LOGOS_ASSERT_TRUE(
        palace::deliveryAcceptanceAllowedProps().empty());
}
