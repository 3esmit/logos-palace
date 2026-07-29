#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "palace_delivery_session.h"

namespace palace {

enum class DeliveryRejectionClass {
    Scope,
    Expired,
    Signature,
    Replay,
    Payload,
    Other,
    Count,
};

class DeliveryRequestCorrelation {
public:
    explicit DeliveryRequestCorrelation(std::size_t maximumEntries = 256);

    bool bind(const std::string& moduleRequestId,
              const std::string& logicalRequestId);
    std::optional<std::string> logicalRequestId(
        const std::string& moduleRequestId) const;
    std::optional<std::string> take(
        const std::string& moduleRequestId);
    void clear();
    std::size_t size() const;

private:
    std::size_t m_maximumEntries;
    std::map<std::string, std::string> m_logicalByModuleRequest;
};

enum class DeliveryNativeNodeState {
    Unknown,
    Stopped,
    Starting,
    Running,
    Stopping,
};

enum class DeliveryRecoveryState {
    Idle,
    StopDispatchPending,
    WaitingForNodeStopped,
    StatusQueryPending,
    RestartDispatchPending,
    WaitingForNodeStarted,
    ReconciliationRequired,
};

enum class DeliveryRecoveryActionKind {
    StopNode,
    QueryNodeStatus,
    RestartSession,
};

struct DeliveryRecoveryAction {
    DeliveryRecoveryActionKind kind =
        DeliveryRecoveryActionKind::QueryNodeStatus;
    std::uint64_t epoch = 0;
    std::string commandId;
};

struct DeliveryRecoveryTransition {
    bool accepted = false;
    bool terminal = false;
    bool interruptPendingWork = false;
    bool forwardNodeStarted = false;
    std::optional<bool> nativeRunning;
    std::string reason;
    std::vector<DeliveryRecoveryAction> actions;
};

// Transport-neutral recovery owner for callback loss. Native calls represented
// by returned actions are executed after the caller releases its mutex.
class DeliveryRecoveryCoordinator {
public:
    DeliveryRecoveryTransition callbackQueueOverflow(
        bool nativeMayBeRunning);
    DeliveryRecoveryTransition resume(bool nativeMayBeRunning);
    DeliveryRecoveryTransition stopDispatchResult(
        const std::string& commandId,
        bool succeeded);
    DeliveryRecoveryTransition nodeStatusResult(
        const std::string& commandId,
        bool succeeded,
        DeliveryNativeNodeState state);
    DeliveryRecoveryTransition restartSessionResult(
        const std::string& commandId,
        bool succeeded);
    DeliveryRecoveryTransition nodeStopped(bool succeeded);
    DeliveryRecoveryTransition nodeStarted(bool succeeded);

    DeliveryRecoveryState state() const;
    bool recovering() const;
    std::uint64_t epoch() const;
    bool expects(const DeliveryRecoveryAction& action) const;

private:
    DeliveryRecoveryTransition transition(
        bool accepted,
        std::string reason) const;
    bool beginEpoch(
        bool nativeMayBeRunning,
        DeliveryRecoveryTransition& transition);
    bool issueStop(DeliveryRecoveryTransition& transition);
    bool issueStatusQuery(DeliveryRecoveryTransition& transition);
    bool issueRestart(DeliveryRecoveryTransition& transition);
    bool issueAction(
        DeliveryRecoveryActionKind kind,
        const std::string& purpose,
        DeliveryRecoveryTransition& transition);
    bool matchesPending(
        DeliveryRecoveryState expected,
        const std::string& commandId) const;
    void requireReconciliation(
        DeliveryRecoveryTransition& transition,
        const std::string& reason);

    DeliveryRecoveryState m_state = DeliveryRecoveryState::Idle;
    std::uint64_t m_epoch = 0;
    std::uint64_t m_nextCommand = 0;
    std::size_t m_stopAttempts = 0;
    std::size_t m_statusAttempts = 0;
    std::string m_pendingCommandId;
};

std::string deliveryRecoveryStateName(DeliveryRecoveryState state);
std::optional<DeliveryConnectionState> parseDeliveryConnectionState(
    const std::string& value);
DeliveryRejectionClass classifyDeliveryRejection(const std::string& reason);
std::string canonicalParticipantProjection(
    const std::vector<DeliveryParticipantProjectionV1>& participants);

} // namespace palace
