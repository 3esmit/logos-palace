#include "palace_delivery_bridge.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <utility>

namespace palace {
namespace {

constexpr std::size_t kMaximumRecoveryStopAttempts = 3U;
constexpr std::size_t kMaximumRecoveryStatusAttempts = 3U;

bool boundedPrintable(const std::string& value, std::size_t maximum)
{
    return !value.empty() && value.size() <= maximum
        && std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return character >= 0x20U && character != 0x7fU;
        });
}

std::string lengthEncoded(const std::string& value)
{
    return std::to_string(value.size()) + ":" + value;
}

std::string normalizedState(const std::string& value)
{
    std::string normalized;
    normalized.reserve(value.size());
    for (const unsigned char character : value) {
        if (std::isalnum(character) != 0)
            normalized.push_back(static_cast<char>(std::tolower(character)));
    }
    return normalized;
}

} // namespace

std::optional<DeliveryNativeNodeState> parseDeliveryNativeNodeStateName(
    const std::string& value)
{
    if (value == "uninitialized" || value == "stopped")
        return DeliveryNativeNodeState::Stopped;
    if (value == "starting")
        return DeliveryNativeNodeState::Starting;
    if (value == "running")
        return DeliveryNativeNodeState::Running;
    if (value == "stopping")
        return DeliveryNativeNodeState::Stopping;
    if (value == "initializing" || value == "destroying")
        return DeliveryNativeNodeState::Unknown;
    return std::nullopt;
}

DeliveryRequestCorrelation::DeliveryRequestCorrelation(
    std::size_t maximumEntries)
    : m_maximumEntries(maximumEntries)
{
}

bool DeliveryRequestCorrelation::bind(
    const std::string& moduleRequestId,
    const std::string& logicalRequestId)
{
    if (m_maximumEntries == 0U
        || m_logicalByModuleRequest.size() >= m_maximumEntries
        || !boundedPrintable(moduleRequestId, 256U)
        || !boundedPrintable(logicalRequestId, 64U)
        || m_logicalByModuleRequest.find(moduleRequestId)
            != m_logicalByModuleRequest.end()
        || std::any_of(
            m_logicalByModuleRequest.begin(),
            m_logicalByModuleRequest.end(),
            [&](const auto& entry) {
                return entry.second == logicalRequestId;
            })) {
        return false;
    }
    m_logicalByModuleRequest.emplace(moduleRequestId, logicalRequestId);
    return true;
}

std::optional<std::string> DeliveryRequestCorrelation::logicalRequestId(
    const std::string& moduleRequestId) const
{
    const auto found = m_logicalByModuleRequest.find(moduleRequestId);
    if (found == m_logicalByModuleRequest.end())
        return std::nullopt;
    return found->second;
}

std::optional<std::string> DeliveryRequestCorrelation::take(
    const std::string& moduleRequestId)
{
    const auto found = m_logicalByModuleRequest.find(moduleRequestId);
    if (found == m_logicalByModuleRequest.end())
        return std::nullopt;
    std::string logicalRequestId = found->second;
    m_logicalByModuleRequest.erase(found);
    return logicalRequestId;
}

void DeliveryRequestCorrelation::clear()
{
    m_logicalByModuleRequest.clear();
}

std::size_t DeliveryRequestCorrelation::size() const
{
    return m_logicalByModuleRequest.size();
}

DeliveryRecoveryTransition DeliveryRecoveryCoordinator::transition(
    bool accepted,
    std::string reason) const
{
    DeliveryRecoveryTransition result;
    result.accepted = accepted;
    result.reason = std::move(reason);
    return result;
}

bool DeliveryRecoveryCoordinator::issueAction(
    DeliveryRecoveryActionKind kind,
    const std::string& purpose,
    DeliveryRecoveryTransition& result)
{
    if (m_nextCommand == std::numeric_limits<std::uint64_t>::max()) {
        requireReconciliation(result, "recovery-command-id-exhausted");
        return false;
    }

    ++m_nextCommand;
    m_pendingCommandId = "delivery-recovery-"
        + std::to_string(m_epoch) + "-" + purpose + "-"
        + std::to_string(m_nextCommand);
    result.actions.push_back(
        {kind, m_epoch, m_pendingCommandId});
    return true;
}

bool DeliveryRecoveryCoordinator::issueStop(
    DeliveryRecoveryTransition& result)
{
    if (m_stopAttempts >= kMaximumRecoveryStopAttempts) {
        requireReconciliation(result, "delivery-stop-attempts-exhausted");
        return false;
    }

    ++m_stopAttempts;
    m_state = DeliveryRecoveryState::StopDispatchPending;
    return issueAction(
        DeliveryRecoveryActionKind::StopNode, "stop", result);
}

bool DeliveryRecoveryCoordinator::issueStatusQuery(
    DeliveryRecoveryTransition& result)
{
    if (m_statusAttempts >= kMaximumRecoveryStatusAttempts) {
        requireReconciliation(result, "delivery-status-attempts-exhausted");
        return false;
    }

    ++m_statusAttempts;
    m_state = DeliveryRecoveryState::StatusQueryPending;
    return issueAction(
        DeliveryRecoveryActionKind::QueryNodeStatus, "status", result);
}

bool DeliveryRecoveryCoordinator::issueRestart(
    DeliveryRecoveryTransition& result)
{
    m_state = DeliveryRecoveryState::RestartDispatchPending;
    return issueAction(
        DeliveryRecoveryActionKind::RestartSession, "restart", result);
}

void DeliveryRecoveryCoordinator::requireReconciliation(
    DeliveryRecoveryTransition& result,
    const std::string& reason)
{
    m_state = DeliveryRecoveryState::ReconciliationRequired;
    m_pendingCommandId.clear();
    result.actions.clear();
    result.reason = reason;
}

bool DeliveryRecoveryCoordinator::beginEpoch(
    bool nativeMayBeRunning,
    DeliveryRecoveryTransition& result)
{
    if (m_epoch == std::numeric_limits<std::uint64_t>::max()) {
        requireReconciliation(result, "recovery-epoch-exhausted");
        return false;
    }

    ++m_epoch;
    m_nextCommand = 0;
    m_stopAttempts = 0;
    m_statusAttempts = 0;
    m_pendingCommandId.clear();
    return nativeMayBeRunning
        ? issueStop(result)
        : issueStatusQuery(result);
}

bool DeliveryRecoveryCoordinator::matchesPending(
    DeliveryRecoveryState expected,
    const std::string& commandId) const
{
    return m_state == expected
        && !commandId.empty()
        && commandId == m_pendingCommandId;
}

DeliveryRecoveryTransition
DeliveryRecoveryCoordinator::callbackQueueOverflow(
    bool nativeMayBeRunning)
{
    DeliveryRecoveryTransition result =
        transition(true, "callback-queue-overflow");
    result.interruptPendingWork = true;

    if (m_state == DeliveryRecoveryState::Idle) {
        result.terminal = true;
        beginEpoch(nativeMayBeRunning, result);
        return result;
    }

    if (m_state == DeliveryRecoveryState::WaitingForNodeStopped
        || m_state == DeliveryRecoveryState::WaitingForNodeStarted) {
        issueStatusQuery(result);
    }
    return result;
}

DeliveryRecoveryTransition DeliveryRecoveryCoordinator::resume(
    bool nativeMayBeRunning)
{
    DeliveryRecoveryTransition result =
        transition(true, "delivery-recovery-resume");

    if (m_state == DeliveryRecoveryState::Idle
        || m_state == DeliveryRecoveryState::ReconciliationRequired) {
        result.interruptPendingWork = true;
        beginEpoch(nativeMayBeRunning, result);
        return result;
    }

    if (m_state == DeliveryRecoveryState::WaitingForNodeStopped
        || m_state == DeliveryRecoveryState::WaitingForNodeStarted) {
        issueStatusQuery(result);
    }
    return result;
}

DeliveryRecoveryTransition
DeliveryRecoveryCoordinator::stopDispatchResult(
    const std::string& commandId,
    bool succeeded)
{
    if (!matchesPending(
            DeliveryRecoveryState::StopDispatchPending, commandId)) {
        return transition(false, "unexpected-delivery-stop-result");
    }

    DeliveryRecoveryTransition result =
        transition(true, succeeded
            ? "delivery-stop-dispatched"
            : "delivery-stop-dispatch-failed");
    m_pendingCommandId.clear();
    if (succeeded) {
        m_state = DeliveryRecoveryState::WaitingForNodeStopped;
        return result;
    }

    issueStatusQuery(result);
    return result;
}

DeliveryRecoveryTransition DeliveryRecoveryCoordinator::nodeStatusResult(
    const std::string& commandId,
    bool succeeded,
    DeliveryNativeNodeState nativeState)
{
    if (!matchesPending(
            DeliveryRecoveryState::StatusQueryPending, commandId)) {
        return transition(false, "unexpected-delivery-status-result");
    }

    DeliveryRecoveryTransition result =
        transition(true, "delivery-node-status");
    m_pendingCommandId.clear();

    if (!succeeded || nativeState == DeliveryNativeNodeState::Unknown) {
        result.reason = succeeded
            ? "unknown-delivery-node-status"
            : "delivery-node-status-failed";
        issueStatusQuery(result);
        return result;
    }

    if (nativeState == DeliveryNativeNodeState::Stopped) {
        result.nativeRunning = false;
        issueRestart(result);
        return result;
    }

    result.nativeRunning = true;
    if (nativeState == DeliveryNativeNodeState::Stopping) {
        m_state = DeliveryRecoveryState::WaitingForNodeStopped;
        result.reason = "delivery-node-stopping";
        return result;
    }

    result.reason = "delivery-node-still-running";
    issueStop(result);
    return result;
}

DeliveryRecoveryTransition
DeliveryRecoveryCoordinator::restartSessionResult(
    const std::string& commandId,
    bool succeeded)
{
    if (!matchesPending(
            DeliveryRecoveryState::RestartDispatchPending, commandId)) {
        return transition(false, "unexpected-delivery-restart-result");
    }

    DeliveryRecoveryTransition result =
        transition(true, succeeded
            ? "delivery-restart-dispatched"
            : "delivery-restart-dispatch-failed");
    m_pendingCommandId.clear();
    if (succeeded) {
        m_state = DeliveryRecoveryState::WaitingForNodeStarted;
        return result;
    }

    requireReconciliation(result, "delivery-restart-dispatch-failed");
    return result;
}

DeliveryRecoveryTransition DeliveryRecoveryCoordinator::nodeStopped(
    bool succeeded)
{
    if (m_state == DeliveryRecoveryState::Idle
        || m_state == DeliveryRecoveryState::ReconciliationRequired) {
        return transition(false, "unexpected-delivery-node-stopped");
    }

    DeliveryRecoveryTransition result =
        transition(true, succeeded
            ? "delivery-node-stopped"
            : "delivery-node-stop-failed");
    if (m_state != DeliveryRecoveryState::StatusQueryPending)
        issueStatusQuery(result);
    return result;
}

DeliveryRecoveryTransition DeliveryRecoveryCoordinator::nodeStarted(
    bool succeeded)
{
    if (m_state == DeliveryRecoveryState::Idle) {
        DeliveryRecoveryTransition result =
            transition(true, succeeded
                ? "delivery-node-started"
                : "delivery-node-start-failed");
        result.forwardNodeStarted = true;
        return result;
    }

    if (m_state != DeliveryRecoveryState::WaitingForNodeStarted)
        return transition(false, "unexpected-delivery-node-started");

    DeliveryRecoveryTransition result =
        transition(true, succeeded
            ? "delivery-recovery-complete"
            : "delivery-recovery-node-start-failed");
    result.forwardNodeStarted = true;
    m_pendingCommandId.clear();
    if (succeeded)
        m_state = DeliveryRecoveryState::Idle;
    else
        requireReconciliation(
            result, "delivery-recovery-node-start-failed");
    return result;
}

DeliveryRecoveryState DeliveryRecoveryCoordinator::state() const
{
    return m_state;
}

bool DeliveryRecoveryCoordinator::recovering() const
{
    return m_state != DeliveryRecoveryState::Idle;
}

std::uint64_t DeliveryRecoveryCoordinator::epoch() const
{
    return m_epoch;
}

bool DeliveryRecoveryCoordinator::expects(
    const DeliveryRecoveryAction& action) const
{
    if (action.epoch != m_epoch
        || action.commandId.empty()
        || action.commandId != m_pendingCommandId) {
        return false;
    }

    switch (action.kind) {
    case DeliveryRecoveryActionKind::StopNode:
        return m_state == DeliveryRecoveryState::StopDispatchPending;
    case DeliveryRecoveryActionKind::QueryNodeStatus:
        return m_state == DeliveryRecoveryState::StatusQueryPending;
    case DeliveryRecoveryActionKind::RestartSession:
        return m_state == DeliveryRecoveryState::RestartDispatchPending;
    }
    return false;
}

std::string deliveryRecoveryStateName(DeliveryRecoveryState state)
{
    switch (state) {
    case DeliveryRecoveryState::Idle: return "idle";
    case DeliveryRecoveryState::StopDispatchPending:
        return "stop_dispatch_pending";
    case DeliveryRecoveryState::WaitingForNodeStopped:
        return "waiting_for_node_stopped";
    case DeliveryRecoveryState::StatusQueryPending:
        return "status_query_pending";
    case DeliveryRecoveryState::RestartDispatchPending:
        return "restart_dispatch_pending";
    case DeliveryRecoveryState::WaitingForNodeStarted:
        return "waiting_for_node_started";
    case DeliveryRecoveryState::ReconciliationRequired:
        return "reconciliation_required";
    }
    return "reconciliation_required";
}

std::optional<DeliveryConnectionState> parseDeliveryConnectionState(
    const std::string& value)
{
    const std::string normalized = normalizedState(value);
    if (normalized == "connected"
        || normalized == "partiallyconnected"
        || normalized == "fullyconnected")
        return DeliveryConnectionState::Connected;
    if (normalized == "connecting")
        return DeliveryConnectionState::Connecting;
    if (normalized == "disconnected" || normalized == "notconnected")
        return DeliveryConnectionState::Disconnected;
    return std::nullopt;
}

DeliveryRejectionClass classifyDeliveryRejection(const std::string& reason)
{
    if (reason == "wrong-palace-room-or-epoch" || reason == "wrong-topic")
        return DeliveryRejectionClass::Scope;
    if (reason == "expired-or-invalid-time")
        return DeliveryRejectionClass::Expired;
    if (reason == "bad-signature-or-key-binding")
        return DeliveryRejectionClass::Signature;
    if (reason == "duplicate-or-replayed-sequence"
        || reason == "reorder-gap-exceeded")
        return DeliveryRejectionClass::Replay;
    if (reason == "invalid-or-banned-payload")
        return DeliveryRejectionClass::Payload;
    return DeliveryRejectionClass::Other;
}

std::string canonicalParticipantProjection(
    const std::vector<DeliveryParticipantProjectionV1>& participants)
{
    std::ostringstream projection;
    projection << "version=1;participants=" << participants.size() << '\n';
    for (const DeliveryParticipantProjectionV1& participant : participants) {
        projection << "participant=" << lengthEncoded(participant.userId)
                   << ";present=" << (participant.present ? '1' : '0')
                   << ";display=" << lengthEncoded(participant.displayName)
                   << ";presence_expires=" << participant.presenceExpiresAt
                   << ";motion=";
        if (participant.hasMotion) {
            projection << participant.motionX << ','
                       << participant.motionY << ','
                       << participant.motionExpiresAt;
        } else {
            projection << '-';
        }
        projection << ";speech=" << lengthEncoded(participant.speech)
                   << ";speech_expires=" << participant.speechExpiresAt
                   << ";props=" << participant.propIds.size() << ':';
        for (const std::string& propId : participant.propIds)
            projection << lengthEncoded(propId);
        projection << '\n';
    }
    return projection.str();
}

} // namespace palace
