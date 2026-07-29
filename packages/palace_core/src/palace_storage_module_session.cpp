#include "palace_storage_module_session.h"

#include "palace_storage_cid.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <type_traits>
#include <utility>

namespace palace {
namespace {

namespace fs = std::filesystem;

constexpr const char* kLifecycleSnapshotSchema =
    "logos.managed_node_lifecycle.snapshot";
constexpr const char* kLifecycleAckSchema =
    "logos.managed_node_lifecycle.ack";
constexpr const char* kLifecycleEventSchema =
    "logos.managed_node_lifecycle.event";
constexpr const char* kDownloadProtocol = "logos.storage.download";
constexpr std::uint32_t kLifecycleVersion = 1U;
constexpr std::uint32_t kDownloadVersion = 2U;
constexpr std::size_t kMaximumInitializationConfigBytes = 49152U;
constexpr std::size_t kMaximumOperationIdBytes = 128U;
constexpr std::size_t kMaximumPathBytes = 4096U;
constexpr std::size_t kMaximumErrorBytes = 4096U;
constexpr std::size_t kAbsoluteMaximumPendingTransfers = 256U;
constexpr std::size_t kAbsoluteMaximumCompletedTransfers = 4096U;
constexpr std::size_t kAbsoluteMaximumQueuedCallbacks = 1024U;
constexpr std::size_t kAbsoluteMaximumQueuedCallbackBytes = 4U * 1024U * 1024U;
constexpr std::size_t kAbsoluteMaximumCallbackPayloadBytes = 256U * 1024U;
constexpr std::uint64_t kModuleMaximumDownloadBytes = 1024U * 1024U * 1024U;
constexpr std::uint32_t kModuleMaximumChunkBytes = 1024U * 1024U;

bool isAsciiAlphaNumeric(char character)
{
    return (character >= '0' && character <= '9')
        || (character >= 'a' && character <= 'z')
        || (character >= 'A' && character <= 'Z');
}

bool isSafeIdentifier(const std::string& value, std::size_t maximumBytes)
{
    return !value.empty() && value.size() <= maximumBytes
        && std::all_of(value.begin(), value.end(), [](char character) {
            return isAsciiAlphaNumeric(character)
                || character == '_' || character == '-'
                || character == '.' || character == ':';
        });
}

bool isPrintableModuleIdentifier(const std::string& value)
{
    return !value.empty() && value.size() <= kMaximumOperationIdBytes
        && std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return character >= 0x21U && character <= 0x7eU
                && std::isspace(character) == 0;
        });
}

bool isAbsoluteNormalizedPath(const std::string& value)
{
    if (value.empty() || value.size() > kMaximumPathBytes
        || value.find('\0') != std::string::npos) {
        return false;
    }
    const fs::path path(value);
    return path.is_absolute() && path.has_filename()
        && path.lexically_normal() == path;
}

bool validLifecycleAction(StorageLifecycleAction action)
{
    switch (action) {
    case StorageLifecycleAction::Initialize:
    case StorageLifecycleAction::Start:
    case StorageLifecycleAction::Stop:
    case StorageLifecycleAction::Destroy:
        return true;
    }
    return false;
}

bool validNodeState(StorageNodeState state)
{
    switch (state) {
    case StorageNodeState::Uninitialized:
    case StorageNodeState::Initializing:
    case StorageNodeState::Stopped:
    case StorageNodeState::Starting:
    case StorageNodeState::Running:
    case StorageNodeState::Stopping:
    case StorageNodeState::Destroying:
        return true;
    }
    return false;
}

std::vector<StorageLifecycleAction> expectedActions(StorageNodeState state)
{
    switch (state) {
    case StorageNodeState::Uninitialized:
        return {StorageLifecycleAction::Initialize};
    case StorageNodeState::Stopped:
        return {
            StorageLifecycleAction::Start,
            StorageLifecycleAction::Destroy,
        };
    case StorageNodeState::Starting:
    case StorageNodeState::Running:
        return {StorageLifecycleAction::Stop};
    case StorageNodeState::Initializing:
    case StorageNodeState::Stopping:
    case StorageNodeState::Destroying:
        return {};
    }
    return {};
}

bool validCompletedOutcome(const std::string& outcome)
{
    return outcome == "succeeded" || outcome == "failed"
        || outcome == "no_op" || outcome == "rejected";
}

bool validSnapshot(const StorageNodeSnapshotV1& snapshot)
{
    if (snapshot.schema != kLifecycleSnapshotSchema
        || snapshot.version != kLifecycleVersion
        || !isPrintableModuleIdentifier(snapshot.instanceId)
        || snapshot.scopeKind != "storage"
        || !validNodeState(snapshot.state)
        || snapshot.supportedActions != expectedActions(snapshot.state)) {
        return false;
    }
    if (snapshot.pendingOperation.has_value()) {
        if (!isPrintableModuleIdentifier(
                snapshot.pendingOperation->operationId)
            || !validLifecycleAction(snapshot.pendingOperation->action)) {
            return false;
        }
    }
    if (snapshot.lastCompletedOperation.has_value()) {
        if (!isPrintableModuleIdentifier(
                snapshot.lastCompletedOperation->operationId)
            || !validLifecycleAction(
                snapshot.lastCompletedOperation->action)
            || !validCompletedOutcome(
                snapshot.lastCompletedOperation->outcome)) {
            return false;
        }
    }
    return true;
}

bool validNodeEventShape(const StorageNodeChangedV1& event)
{
    if (event.schema != kLifecycleEventSchema
        || event.version != kLifecycleVersion
        || event.scopeKind != "storage"
        || !validLifecycleAction(event.action)
        || !validNodeState(event.previousState)
        || !validSnapshot(event.status)
        || event.instanceId != event.status.instanceId
        || event.epoch != event.status.epoch
        || event.sequence != event.status.sequence
        || (!event.operationId.empty()
            && !isPrintableModuleIdentifier(event.operationId))
        || event.errorCode.size() > 128U) {
        return false;
    }
    if (event.phase == "accepted") {
        if (event.outcome != "accepted" || !event.errorCode.empty())
            return false;
        return event.operationId.empty()
            || (event.status.pendingOperation.has_value()
                && event.status.pendingOperation->operationId
                    == event.operationId
                && event.status.pendingOperation->action == event.action);
    }
    if (event.phase != "settled" || !validCompletedOutcome(event.outcome))
        return false;
    if ((event.outcome == "succeeded" || event.outcome == "no_op")
        && !event.errorCode.empty()) {
        return false;
    }
    return event.operationId.empty()
        || (event.status.lastCompletedOperation.has_value()
            && event.status.lastCompletedOperation->operationId
                == event.operationId
            && event.status.lastCompletedOperation->action == event.action
            && event.status.lastCompletedOperation->outcome
                == event.outcome);
}

bool validNodeAcknowledgementShape(
    const StorageNodeActionAcknowledgementV1& acknowledgement)
{
    return acknowledgement.schema == kLifecycleAckSchema
        && acknowledgement.version == kLifecycleVersion
        && isPrintableModuleIdentifier(acknowledgement.operationId)
        && isPrintableModuleIdentifier(acknowledgement.instanceId)
        && validNodeState(acknowledgement.state)
        && acknowledgement.errorCode.size() <= 128U
        && (acknowledgement.accepted
                ? acknowledgement.errorCode.empty()
                : !acknowledgement.errorCode.empty());
}

bool validConfig(const StorageModuleSessionConfigV1& config)
{
    return !config.initializationConfig.empty()
        && config.initializationConfig.size()
            <= kMaximumInitializationConfigBytes
        && config.initializationConfig.find('\0') == std::string::npos
        && isSafeIdentifier(config.operationIdPrefix, 48U)
        && config.maxPendingTransfers > 0U
        && config.maxPendingTransfers
            <= kAbsoluteMaximumPendingTransfers
        && config.maxCompletedTransfers > 0U
        && config.maxCompletedTransfers
            <= kAbsoluteMaximumCompletedTransfers
        && config.maxQueuedCallbacks > 0U
        && config.maxQueuedCallbacks
            <= kAbsoluteMaximumQueuedCallbacks
        && config.maxQueuedCallbackBytes > 0U
        && config.maxQueuedCallbackBytes
            <= kAbsoluteMaximumQueuedCallbackBytes
        && config.maxCallbackPayloadBytes > 0U
        && config.maxCallbackPayloadBytes
            <= kAbsoluteMaximumCallbackPayloadBytes
        && config.maxCallbackPayloadBytes
            <= config.maxQueuedCallbackBytes
        && config.maxTransferBytes > 0U
        && config.maxTransferBytes <= kModuleMaximumDownloadBytes
        && config.defaultChunkBytes > 0U
        && config.defaultChunkBytes <= kModuleMaximumChunkBytes;
}

bool validTransferKind(StorageTransferKind kind)
{
    switch (kind) {
    case StorageTransferKind::Upload:
    case StorageTransferKind::NetworkFetch:
    case StorageTransferKind::LocalVerification:
    case StorageTransferKind::BootstrapFetch:
        return true;
    }
    return false;
}

bool isDownloadKind(StorageTransferKind kind)
{
    return kind == StorageTransferKind::NetworkFetch
        || kind == StorageTransferKind::LocalVerification
        || kind == StorageTransferKind::BootstrapFetch;
}

bool isStableNodeState(StorageNodeState state)
{
    return state == StorageNodeState::Uninitialized
        || state == StorageNodeState::Stopped
        || state == StorageNodeState::Running;
}

std::size_t nodeEventStringBytes(const StorageNodeChangedV1& event)
{
    std::size_t bytes = event.schema.size() + event.instanceId.size()
        + event.scopeKind.size() + event.operationId.size()
        + event.phase.size() + event.outcome.size()
        + event.errorCode.size() + event.status.schema.size()
        + event.status.instanceId.size() + event.status.scopeKind.size();
    if (event.status.pendingOperation.has_value())
        bytes += event.status.pendingOperation->operationId.size();
    if (event.status.lastCompletedOperation.has_value()) {
        bytes += event.status.lastCompletedOperation->operationId.size()
            + event.status.lastCompletedOperation->outcome.size();
    }
    return bytes;
}

} // namespace

std::string storageModuleSessionStateName(StorageModuleSessionState state)
{
    switch (state) {
    case StorageModuleSessionState::Unconfigured: return "unconfigured";
    case StorageModuleSessionState::Offline: return "offline";
    case StorageModuleSessionState::Reconciling: return "reconciling";
    case StorageModuleSessionState::Initializing: return "initializing";
    case StorageModuleSessionState::Stopped: return "stopped";
    case StorageModuleSessionState::Starting: return "starting";
    case StorageModuleSessionState::Running: return "running";
    case StorageModuleSessionState::Stopping: return "stopping";
    case StorageModuleSessionState::Recovering: return "recovering";
    case StorageModuleSessionState::ReconciliationRequired:
        return "reconciliation_required";
    }
    return "reconciliation_required";
}

std::string storageLifecycleActionName(StorageLifecycleAction action)
{
    switch (action) {
    case StorageLifecycleAction::Initialize: return "initialize";
    case StorageLifecycleAction::Start: return "start";
    case StorageLifecycleAction::Stop: return "stop";
    case StorageLifecycleAction::Destroy: return "destroy";
    }
    return {};
}

bool PalaceStorageModuleSession::configure(
    const StorageModuleSessionConfigV1& config)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!validConfig(config)
        || m_state != StorageModuleSessionState::Unconfigured
        || !m_transfers.empty() || m_lifecycleOperation.has_value()
        || !m_statusQueryCommandId.empty()) {
        return false;
    }

    m_config = config;
    m_configured = true;
    m_state = StorageModuleSessionState::Offline;
    m_targetRunning = false;
    m_recoveryRestart = false;
    m_recoveryStopRequired = false;
    m_callbackOverflowed = false;
    m_nextIdentifier = 0U;
    m_snapshot.reset();
    m_completedTransfers.clear();
    m_callbacks.clear();
    m_queuedCallbackBytes = 0U;
    return true;
}

bool PalaceStorageModuleSession::hasConfiguration() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_configured;
}

StorageModuleSessionTransition PalaceStorageModuleSession::transitionLocked(
    bool accepted,
    std::string reason) const
{
    StorageModuleSessionTransition transition;
    transition.accepted = accepted;
    transition.reason = std::move(reason);
    return transition;
}

std::string PalaceStorageModuleSession::nextIdentifierLocked(
    const std::string& purpose)
{
    if (m_nextIdentifier == std::numeric_limits<std::uint64_t>::max())
        return {};
    ++m_nextIdentifier;
    const std::string identifier = m_config.operationIdPrefix + "-"
        + purpose + "-" + std::to_string(m_nextIdentifier);
    return identifier.size() <= kMaximumOperationIdBytes
        ? identifier : std::string{};
}

bool PalaceStorageModuleSession::issueStatusQueryLocked(
    StorageModuleSessionTransition& transition)
{
    if (!m_statusQueryCommandId.empty())
        return false;
    const std::string commandId = nextIdentifierLocked("status");
    if (commandId.empty()) {
        m_state = StorageModuleSessionState::ReconciliationRequired;
        transition.accepted = false;
        transition.reason = "identifier-space-exhausted";
        return false;
    }
    m_statusQueryCommandId = commandId;
    StorageModuleCommand command;
    command.kind = StorageModuleCommandKind::QueryNodeStatus;
    command.commandId = commandId;
    transition.commands.push_back(std::move(command));
    return true;
}

StorageModuleSessionTransition PalaceStorageModuleSession::start()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_configured)
        return transitionLocked(false, "session-not-configured");
    if (m_state == StorageModuleSessionState::Running
        && !m_recoveryStopRequired) {
        return transitionLocked(true, "already-running");
    }
    if (!m_statusQueryCommandId.empty()
        || m_lifecycleOperation.has_value()) {
        return transitionLocked(false, "lifecycle-operation-pending");
    }

    m_targetRunning = true;
    m_recoveryRestart = false;
    m_recoveryStopRequired = false;
    m_state = StorageModuleSessionState::Reconciling;
    StorageModuleSessionTransition transition =
        transitionLocked(true, "query-node-status");
    issueStatusQueryLocked(transition);
    return transition;
}

StorageModuleSessionTransition PalaceStorageModuleSession::refreshStatus()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_configured)
        return transitionLocked(false, "session-not-configured");
    if (!m_statusQueryCommandId.empty())
        return transitionLocked(false, "status-query-pending");
    StorageModuleSessionTransition transition =
        transitionLocked(true, "query-node-status");
    issueStatusQueryLocked(transition);
    return transition;
}

StorageModuleSessionTransition PalaceStorageModuleSession::interrupt(
    bool recoverable)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_configured)
        return transitionLocked(false, "session-not-configured");

    m_callbacks.clear();
    m_queuedCallbackBytes = 0U;
    m_callbackOverflowed = false;
    StorageModuleSessionTransition transition =
        transitionLocked(true, recoverable
            ? "recoverable-interrupt" : "interrupted");
    failAllTransfersLocked("session-interrupted", transition);

    m_targetRunning = recoverable;
    m_recoveryRestart = recoverable;
    m_recoveryStopRequired = recoverable;
    if (recoverable) {
        m_state = StorageModuleSessionState::Recovering;
        if (m_lifecycleOperation.has_value()) {
            issueStatusQueryLocked(transition);
        } else {
            advanceLifecycleLocked(transition);
        }
        return transition;
    }

    if (m_lifecycleOperation.has_value()) {
        m_state = StorageModuleSessionState::Stopping;
        issueStatusQueryLocked(transition);
    } else if (m_snapshot.has_value()
               && m_snapshot->state == StorageNodeState::Running) {
        issueLifecycleActionLocked(StorageLifecycleAction::Stop, transition);
    } else {
        m_state = StorageModuleSessionState::Offline;
    }
    return transition;
}

StorageModuleSessionState PalaceStorageModuleSession::state() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

bool PalaceStorageModuleSession::running() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state == StorageModuleSessionState::Running;
}

bool PalaceStorageModuleSession::reconciliationRequired() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state
        == StorageModuleSessionState::ReconciliationRequired;
}

std::size_t PalaceStorageModuleSession::pendingTransferCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_transfers.size();
}

std::size_t PalaceStorageModuleSession::queuedCallbackCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_callbacks.size();
}

StorageTransferStatus PalaceStorageModuleSession::transferStatus(
    const std::string& domainOperationId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    StorageTransferStatus status;
    const auto active = m_transfers.find(domainOperationId);
    if (active != m_transfers.end()) {
        status.found = true;
        status.kind = active->second.kind;
        status.cid = active->second.cid;
        status.path = active->second.path;
        status.localOnly = active->second.localOnly;
        return status;
    }
    const auto completed = m_completedTransfers.find(domainOperationId);
    if (completed != m_completedTransfers.end()) {
        status.found = true;
        status.terminal = true;
        status.kind = completed->second.kind;
        status.outcome = completed->second.outcome;
        status.cid = completed->second.cid;
        status.path = completed->second.path;
        status.localOnly = completed->second.localOnly;
    }
    return status;
}

bool PalaceStorageModuleSession::issueLifecycleActionLocked(
    StorageLifecycleAction action,
    StorageModuleSessionTransition& transition)
{
    if (!m_snapshot.has_value() || m_lifecycleOperation.has_value()
        || action == StorageLifecycleAction::Destroy) {
        return false;
    }
    const std::string operationId = nextIdentifierLocked("lifecycle");
    if (operationId.empty()) {
        m_state = StorageModuleSessionState::ReconciliationRequired;
        transition.accepted = false;
        transition.reason = "identifier-space-exhausted";
        return false;
    }

    LifecycleOperation operation;
    operation.action = action;
    operation.commandId = operationId;
    operation.moduleOperationId = operationId;
    operation.expectedInstanceId = m_snapshot->instanceId;
    operation.expectedEpoch = m_snapshot->epoch;
    operation.expectedSequence = m_snapshot->sequence;
    m_lifecycleOperation = operation;

    StorageModuleCommand command;
    command.kind = StorageModuleCommandKind::NodeAction;
    command.commandId = operationId;
    command.lifecycleAction = action;
    command.lifecycleOperationId = operationId;
    command.expectedInstanceId = operation.expectedInstanceId;
    command.expectedEpoch = operation.expectedEpoch;
    command.expectedSequence = operation.expectedSequence;
    if (action == StorageLifecycleAction::Initialize)
        command.initializationConfig = m_config.initializationConfig;
    transition.commands.push_back(std::move(command));

    switch (action) {
    case StorageLifecycleAction::Initialize:
        m_state = StorageModuleSessionState::Initializing;
        break;
    case StorageLifecycleAction::Start:
        m_state = StorageModuleSessionState::Starting;
        break;
    case StorageLifecycleAction::Stop:
        m_state = m_recoveryRestart
            ? StorageModuleSessionState::Recovering
            : StorageModuleSessionState::Stopping;
        break;
    case StorageLifecycleAction::Destroy:
        break;
    }
    return true;
}

void PalaceStorageModuleSession::advanceLifecycleLocked(
    StorageModuleSessionTransition& transition)
{
    if (m_lifecycleOperation.has_value()
        || !m_statusQueryCommandId.empty()) {
        return;
    }
    if (!m_snapshot.has_value()) {
        m_state = StorageModuleSessionState::Reconciling;
        issueStatusQueryLocked(transition);
        return;
    }
    if (m_snapshot->pendingOperation.has_value()
        || !isStableNodeState(m_snapshot->state)) {
        m_state = StorageModuleSessionState::ReconciliationRequired;
        transition.reason = "unowned-lifecycle-operation";
        return;
    }

    if (m_recoveryStopRequired) {
        if (m_snapshot->state == StorageNodeState::Running) {
            issueLifecycleActionLocked(
                StorageLifecycleAction::Stop, transition);
            return;
        }
        if (m_snapshot->state == StorageNodeState::Stopped
            || m_snapshot->state == StorageNodeState::Uninitialized) {
            m_recoveryStopRequired = false;
        }
    }

    if (!m_targetRunning) {
        m_state = m_snapshot->state == StorageNodeState::Stopped
            ? StorageModuleSessionState::Stopped
            : StorageModuleSessionState::Offline;
        m_recoveryRestart = false;
        return;
    }

    switch (m_snapshot->state) {
    case StorageNodeState::Uninitialized:
        issueLifecycleActionLocked(
            StorageLifecycleAction::Initialize, transition);
        return;
    case StorageNodeState::Stopped:
        issueLifecycleActionLocked(
            StorageLifecycleAction::Start, transition);
        return;
    case StorageNodeState::Running:
        m_state = StorageModuleSessionState::Running;
        m_recoveryRestart = false;
        m_recoveryStopRequired = false;
        return;
    case StorageNodeState::Initializing:
    case StorageNodeState::Starting:
    case StorageNodeState::Stopping:
    case StorageNodeState::Destroying:
        m_state = StorageModuleSessionState::ReconciliationRequired;
        transition.reason = "unowned-lifecycle-operation";
        return;
    }
}

void PalaceStorageModuleSession::finalizeLifecycleLocked(
    StorageModuleSessionTransition& transition)
{
    if (!m_lifecycleOperation.has_value()
        || !m_lifecycleOperation->acknowledgementReceived
        || !m_lifecycleOperation->settledReceived) {
        return;
    }
    const LifecycleOperation completed = *m_lifecycleOperation;
    m_lifecycleOperation.reset();
    m_snapshot = completed.settledSnapshot;

    if (!completed.acknowledgementAccepted
        || !completed.settledSucceeded) {
        m_targetRunning = false;
        m_recoveryRestart = false;
        m_recoveryStopRequired = false;
        m_state = StorageModuleSessionState::ReconciliationRequired;
        transition.reason = "lifecycle-action-failed";
        return;
    }

    if (completed.action == StorageLifecycleAction::Stop)
        m_recoveryStopRequired = false;
    advanceLifecycleLocked(transition);
}

StorageModuleSessionTransition PalaceStorageModuleSession::nodeStatusResult(
    const std::string& commandId,
    bool callSucceeded,
    const StorageNodeSnapshotV1& snapshot)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (commandId.empty() || commandId != m_statusQueryCommandId)
        return transitionLocked(false, "unknown-status-command");
    m_statusQueryCommandId.clear();
    StorageModuleSessionTransition transition =
        transitionLocked(true, "status-reconciled");

    if (!callSucceeded || !validSnapshot(snapshot)) {
        m_state = StorageModuleSessionState::ReconciliationRequired;
        transition.accepted = false;
        transition.reason = callSucceeded
            ? "invalid-node-status" : "node-status-call-failed";
        return transition;
    }

    if (m_snapshot.has_value()
        && m_snapshot->instanceId == snapshot.instanceId
        && (snapshot.epoch < m_snapshot->epoch
            || snapshot.sequence < m_snapshot->sequence)) {
        m_state = StorageModuleSessionState::ReconciliationRequired;
        transition.accepted = false;
        transition.reason = "stale-node-status";
        return transition;
    }

    if (m_snapshot.has_value()
        && m_snapshot->instanceId != snapshot.instanceId) {
        failAllTransfersLocked("storage-instance-changed", transition);
        m_lifecycleOperation.reset();
    }
    m_snapshot = snapshot;

    if (m_lifecycleOperation.has_value()) {
        const LifecycleOperation& operation = *m_lifecycleOperation;
        if (snapshot.pendingOperation.has_value()
            && snapshot.pendingOperation->operationId
                == operation.moduleOperationId
            && snapshot.pendingOperation->action == operation.action) {
            switch (snapshot.state) {
            case StorageNodeState::Initializing:
                m_state = StorageModuleSessionState::Initializing;
                break;
            case StorageNodeState::Starting:
                m_state = StorageModuleSessionState::Starting;
                break;
            case StorageNodeState::Stopping:
                m_state = m_recoveryRestart
                    ? StorageModuleSessionState::Recovering
                    : StorageModuleSessionState::Stopping;
                break;
            case StorageNodeState::Uninitialized:
            case StorageNodeState::Stopped:
            case StorageNodeState::Running:
            case StorageNodeState::Destroying:
                m_state =
                    StorageModuleSessionState::ReconciliationRequired;
                transition.accepted = false;
                transition.reason = "invalid-pending-lifecycle-state";
                break;
            }
            return transition;
        }
        if (snapshot.lastCompletedOperation.has_value()
            && snapshot.lastCompletedOperation->operationId
                == operation.moduleOperationId
            && snapshot.lastCompletedOperation->action
                == operation.action) {
            m_lifecycleOperation->acknowledgementReceived = true;
            m_lifecycleOperation->acknowledgementAccepted =
                snapshot.lastCompletedOperation->outcome == "succeeded"
                || snapshot.lastCompletedOperation->outcome == "no_op";
            m_lifecycleOperation->settledReceived = true;
            m_lifecycleOperation->settledSucceeded =
                m_lifecycleOperation->acknowledgementAccepted;
            m_lifecycleOperation->settledSnapshot = snapshot;
            finalizeLifecycleLocked(transition);
            return transition;
        }

        m_lifecycleOperation.reset();
        m_targetRunning = false;
        m_recoveryRestart = false;
        m_recoveryStopRequired = false;
        m_state = StorageModuleSessionState::ReconciliationRequired;
        transition.accepted = false;
        transition.reason = "lost-lifecycle-correlation";
        return transition;
    }

    advanceLifecycleLocked(transition);
    return transition;
}

StorageModuleSessionTransition PalaceStorageModuleSession::nodeActionResult(
    const std::string& commandId,
    const StorageNodeActionAcknowledgementV1& acknowledgement)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_lifecycleOperation.has_value()
        || commandId != m_lifecycleOperation->commandId) {
        return transitionLocked(false, "unknown-lifecycle-command");
    }
    LifecycleOperation& operation = *m_lifecycleOperation;
    if (operation.acknowledgementReceived)
        return transitionLocked(false, "duplicate-lifecycle-acknowledgement");
    if (!validNodeAcknowledgementShape(acknowledgement)
        || acknowledgement.operationId != operation.moduleOperationId
        || acknowledgement.instanceId != operation.expectedInstanceId
        || acknowledgement.epoch < operation.expectedEpoch
        || acknowledgement.sequence < operation.expectedSequence) {
        StorageModuleSessionTransition transition =
            transitionLocked(false, "lifecycle-acknowledgement-mismatch");
        m_lifecycleOperation.reset();
        m_state = StorageModuleSessionState::ReconciliationRequired;
        m_targetRunning = false;
        m_recoveryRestart = false;
        m_recoveryStopRequired = false;
        return transition;
    }

    operation.acknowledgementReceived = true;
    operation.acknowledgementAccepted = acknowledgement.accepted;
    StorageModuleSessionTransition transition =
        transitionLocked(true, acknowledgement.duplicate
            ? "lifecycle-acknowledgement-replayed"
            : "lifecycle-acknowledged");
    if (!acknowledgement.accepted) {
        m_lifecycleOperation.reset();
        m_state = StorageModuleSessionState::Reconciling;
        issueStatusQueryLocked(transition);
        return transition;
    }
    finalizeLifecycleLocked(transition);
    return transition;
}

void PalaceStorageModuleSession::completeTransferLocked(
    std::map<std::string, Transfer>::iterator transfer,
    StorageTransferOutcome outcome,
    const std::string& cid,
    const std::string& reason,
    StorageModuleSessionTransition& transition)
{
    const Transfer completed = transfer->second;
    if (!completed.commandId.empty())
        m_transferByCommand.erase(completed.commandId);
    if (completed.kind == StorageTransferKind::Upload) {
        if (!completed.moduleId.empty())
            m_uploadByModuleSession.erase(completed.moduleId);
    } else {
        if (!completed.moduleId.empty())
            m_downloadByModuleOperation.erase(completed.moduleId);
        m_downloadByCid.erase(completed.cid);
        m_downloadByDestination.erase(completed.path);
    }

    CompletedTransfer history;
    history.kind = completed.kind;
    history.outcome = outcome;
    history.cid = cid;
    history.path = completed.path;
    history.localOnly = completed.localOnly;
    m_completedTransfers.emplace(completed.domainOperationId, history);

    StorageTransferTerminal terminal;
    terminal.domainOperationId = completed.domainOperationId;
    terminal.kind = completed.kind;
    terminal.outcome = outcome;
    terminal.cid = cid;
    terminal.path = completed.path;
    terminal.localOnly = completed.localOnly;
    terminal.reason = reason;
    transition.terminals.push_back(std::move(terminal));
    m_transfers.erase(transfer);
}

void PalaceStorageModuleSession::failAllTransfersLocked(
    const std::string& reason,
    StorageModuleSessionTransition& transition)
{
    while (!m_transfers.empty()) {
        auto transfer = m_transfers.begin();
        if (transfer->second.phase == TransferPhase::Active
            && !transfer->second.moduleId.empty()) {
            StorageModuleCommand cancel;
            cancel.commandId = nextIdentifierLocked("cancel");
            cancel.domainOperationId =
                transfer->second.domainOperationId;
            if (transfer->second.kind == StorageTransferKind::Upload) {
                cancel.kind = StorageModuleCommandKind::UploadCancel;
                cancel.moduleSessionId = transfer->second.moduleId;
            } else {
                cancel.kind =
                    StorageModuleCommandKind::DownloadCancelV2;
                cancel.moduleOperationId = transfer->second.moduleId;
            }
            if (!cancel.commandId.empty())
                transition.commands.push_back(std::move(cancel));
        }
        completeTransferLocked(
            transfer,
            StorageTransferOutcome::Interrupted,
            transfer->second.cid,
            reason,
            transition);
    }
}

void PalaceStorageModuleSession::enterRecoveryLocked(
    const std::string& reason,
    StorageModuleSessionTransition& transition,
    bool restart)
{
    failAllTransfersLocked(reason, transition);
    m_targetRunning = restart;
    m_recoveryRestart = restart;
    m_recoveryStopRequired = restart;
    m_state = restart
        ? StorageModuleSessionState::Recovering
        : StorageModuleSessionState::Stopping;
    transition.reason = reason;

    if (m_lifecycleOperation.has_value()) {
        issueStatusQueryLocked(transition);
        return;
    }
    advanceLifecycleLocked(transition);
}

StorageModuleSessionTransition PalaceStorageModuleSession::beginUpload(
    const std::string& domainOperationId,
    const std::string& sourcePath,
    std::uint64_t expectedBytes,
    std::uint32_t chunkBytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state != StorageModuleSessionState::Running)
        return transitionLocked(false, "storage-not-running");
    if (!isSafeIdentifier(domainOperationId, kMaximumOperationIdBytes)
        || m_transfers.find(domainOperationId) != m_transfers.end()
        || m_completedTransfers.find(domainOperationId)
            != m_completedTransfers.end()) {
        return transitionLocked(false, "invalid-or-reused-domain-operation-id");
    }
    if (m_transfers.size() >= m_config.maxPendingTransfers)
        return transitionLocked(false, "pending-transfer-limit");
    if (m_completedTransfers.size() >= m_config.maxCompletedTransfers)
        return transitionLocked(false, "completed-transfer-history-full");
    const std::uint32_t effectiveChunk = chunkBytes == 0U
        ? m_config.defaultChunkBytes : chunkBytes;
    if (!isAbsoluteNormalizedPath(sourcePath)
        || expectedBytes == 0U
        || expectedBytes > m_config.maxTransferBytes
        || effectiveChunk == 0U
        || effectiveChunk > kModuleMaximumChunkBytes) {
        return transitionLocked(false, "invalid-upload-arguments");
    }
    const std::string commandId = nextIdentifierLocked("upload");
    if (commandId.empty())
        return transitionLocked(false, "identifier-space-exhausted");

    Transfer transfer;
    transfer.kind = StorageTransferKind::Upload;
    transfer.domainOperationId = domainOperationId;
    transfer.commandId = commandId;
    transfer.path = sourcePath;
    transfer.maxBytes = expectedBytes;
    m_transfers.emplace(domainOperationId, transfer);
    m_transferByCommand.emplace(commandId, domainOperationId);

    StorageModuleCommand command;
    command.kind = StorageModuleCommandKind::UploadUrl;
    command.commandId = commandId;
    command.domainOperationId = domainOperationId;
    command.path = sourcePath;
    command.chunkBytes = effectiveChunk;
    command.maxBytes = expectedBytes;
    StorageModuleSessionTransition transition =
        transitionLocked(true, "upload-command");
    transition.commands.push_back(std::move(command));
    return transition;
}

StorageModuleSessionTransition PalaceStorageModuleSession::beginDownloadLocked(
    StorageTransferKind kind,
    const std::string& domainOperationId,
    const std::string& cid,
    const std::string& destinationPath,
    std::uint64_t maxBytes,
    std::uint32_t chunkBytes)
{
    if (m_state != StorageModuleSessionState::Running)
        return transitionLocked(false, "storage-not-running");
    if (!validTransferKind(kind) || !isDownloadKind(kind)
        || !isSafeIdentifier(domainOperationId, kMaximumOperationIdBytes)
        || m_transfers.find(domainOperationId) != m_transfers.end()
        || m_completedTransfers.find(domainOperationId)
            != m_completedTransfers.end()) {
        return transitionLocked(false, "invalid-or-reused-domain-operation-id");
    }
    if (m_transfers.size() >= m_config.maxPendingTransfers)
        return transitionLocked(false, "pending-transfer-limit");
    if (m_completedTransfers.size() >= m_config.maxCompletedTransfers)
        return transitionLocked(false, "completed-transfer-history-full");
    const std::uint32_t effectiveChunk = chunkBytes == 0U
        ? m_config.defaultChunkBytes : chunkBytes;
    if (!isCanonicalStorageCid(cid)
        || !isAbsoluteNormalizedPath(destinationPath)
        || maxBytes == 0U || maxBytes > m_config.maxTransferBytes
        || maxBytes > kModuleMaximumDownloadBytes
        || effectiveChunk == 0U
        || effectiveChunk > kModuleMaximumChunkBytes
        || m_downloadByCid.find(cid) != m_downloadByCid.end()
        || m_downloadByDestination.find(destinationPath)
            != m_downloadByDestination.end()) {
        return transitionLocked(false, "invalid-or-conflicting-download");
    }

    const std::string moduleOperationId =
        nextIdentifierLocked("download");
    if (moduleOperationId.empty() || moduleOperationId == cid)
        return transitionLocked(false, "identifier-space-exhausted");

    Transfer transfer;
    transfer.kind = kind;
    transfer.domainOperationId = domainOperationId;
    transfer.commandId = moduleOperationId;
    transfer.moduleId = moduleOperationId;
    transfer.cid = cid;
    transfer.path = destinationPath;
    transfer.localOnly =
        kind == StorageTransferKind::LocalVerification;
    transfer.maxBytes = maxBytes;
    m_transfers.emplace(domainOperationId, transfer);
    m_transferByCommand.emplace(moduleOperationId, domainOperationId);
    m_downloadByModuleOperation.emplace(
        moduleOperationId, domainOperationId);
    m_downloadByCid.emplace(cid, domainOperationId);
    m_downloadByDestination.emplace(
        destinationPath, domainOperationId);

    StorageModuleCommand command;
    command.kind = StorageModuleCommandKind::DownloadToUrlV2;
    command.commandId = moduleOperationId;
    command.domainOperationId = domainOperationId;
    command.moduleOperationId = moduleOperationId;
    command.cid = cid;
    command.path = destinationPath;
    command.localOnly = transfer.localOnly;
    command.chunkBytes = effectiveChunk;
    command.maxBytes = maxBytes;
    StorageModuleSessionTransition transition =
        transitionLocked(true, kind == StorageTransferKind::LocalVerification
            ? "local-verification-command"
            : kind == StorageTransferKind::BootstrapFetch
                ? "bootstrap-fetch-command"
                : "network-fetch-command");
    transition.commands.push_back(std::move(command));
    return transition;
}

StorageModuleSessionTransition PalaceStorageModuleSession::beginNetworkFetch(
    const std::string& domainOperationId,
    const std::string& cid,
    const std::string& destinationPath,
    std::uint64_t maxBytes,
    std::uint32_t chunkBytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return beginDownloadLocked(
        StorageTransferKind::NetworkFetch,
        domainOperationId,
        cid,
        destinationPath,
        maxBytes,
        chunkBytes);
}

StorageModuleSessionTransition
PalaceStorageModuleSession::beginLocalVerification(
    const std::string& domainOperationId,
    const std::string& cid,
    const std::string& destinationPath,
    std::uint64_t maxBytes,
    std::uint32_t chunkBytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return beginDownloadLocked(
        StorageTransferKind::LocalVerification,
        domainOperationId,
        cid,
        destinationPath,
        maxBytes,
        chunkBytes);
}

StorageModuleSessionTransition PalaceStorageModuleSession::beginBootstrapFetch(
    const std::string& domainOperationId,
    const std::string& cid,
    const std::string& temporaryDestinationPath,
    std::uint64_t maxBytes,
    std::uint32_t chunkBytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return beginDownloadLocked(
        StorageTransferKind::BootstrapFetch,
        domainOperationId,
        cid,
        temporaryDestinationPath,
        maxBytes,
        chunkBytes);
}

StorageModuleSessionTransition PalaceStorageModuleSession::uploadUrlResult(
    const std::string& commandId,
    bool callSucceeded,
    const std::string& moduleSessionId,
    const std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto mapped = m_transferByCommand.find(commandId);
    if (mapped == m_transferByCommand.end())
        return transitionLocked(false, "unknown-upload-command");
    auto transfer = m_transfers.find(mapped->second);
    if (transfer == m_transfers.end()
        || transfer->second.kind != StorageTransferKind::Upload
        || transfer->second.phase != TransferPhase::AwaitingDispatch) {
        return transitionLocked(false, "unexpected-upload-result");
    }

    StorageModuleSessionTransition transition =
        transitionLocked(true, "upload-acknowledged");
    m_transferByCommand.erase(mapped);
    transfer->second.commandId.clear();
    if (!callSucceeded) {
        if (!moduleSessionId.empty() || error.size() > kMaximumErrorBytes) {
            transition.accepted = false;
            transition.reason = "invalid-upload-failure-result";
        }
        completeTransferLocked(
            transfer,
            StorageTransferOutcome::Failed,
            {},
            "upload-dispatch-failed",
            transition);
        return transition;
    }
    if (!error.empty()
        || !isPrintableModuleIdentifier(moduleSessionId)
        || m_uploadByModuleSession.find(moduleSessionId)
            != m_uploadByModuleSession.end()) {
        transition.accepted = false;
        transition.reason = "upload-session-mismatch";
        enterRecoveryLocked(
            "upload-session-mismatch", transition, true);
        return transition;
    }
    transfer->second.phase = TransferPhase::Active;
    transfer->second.moduleId = moduleSessionId;
    m_uploadByModuleSession.emplace(
        moduleSessionId, transfer->second.domainOperationId);
    return transition;
}

StorageModuleSessionTransition
PalaceStorageModuleSession::downloadToUrlV2Result(
    const std::string& commandId,
    bool callSucceeded,
    const StorageDownloadAcknowledgementV2& acknowledgement,
    const std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto mapped = m_transferByCommand.find(commandId);
    if (mapped == m_transferByCommand.end())
        return transitionLocked(false, "unknown-download-command");
    auto transfer = m_transfers.find(mapped->second);
    if (transfer == m_transfers.end()
        || !isDownloadKind(transfer->second.kind)
        || transfer->second.phase != TransferPhase::AwaitingDispatch) {
        return transitionLocked(false, "unexpected-download-result");
    }

    StorageModuleSessionTransition transition =
        transitionLocked(true, "download-acknowledged");
    m_transferByCommand.erase(mapped);
    transfer->second.commandId.clear();
    if (!callSucceeded) {
        if (!acknowledgement.moduleOperationId.empty()
            || !acknowledgement.cid.empty()
            || error.size() > kMaximumErrorBytes) {
            transition.accepted = false;
            transition.reason = "invalid-download-failure-result";
        }
        completeTransferLocked(
            transfer,
            StorageTransferOutcome::Failed,
            transfer->second.cid,
            "download-dispatch-failed",
            transition);
        return transition;
    }

    if (acknowledgement.protocol != kDownloadProtocol
        || acknowledgement.version != kDownloadVersion
        || !acknowledgement.accepted
        || acknowledgement.moduleOperationId != transfer->second.moduleId
        || acknowledgement.cid != transfer->second.cid
        || !error.empty()) {
        transition.accepted = false;
        transition.reason = "download-acknowledgement-mismatch";
        enterRecoveryLocked(
            "download-acknowledgement-mismatch",
            transition,
            true);
        return transition;
    }
    transfer->second.phase = TransferPhase::Active;
    return transition;
}

StorageModuleCallbackEnqueue
PalaceStorageModuleSession::enqueueCallbackLocked(Callback callback)
{
    if (!m_configured)
        return {false, false, "session-not-configured"};
    if (callback.encodedBytes == 0U
        || callback.encodedBytes > m_config.maxCallbackPayloadBytes) {
        return {false, false, "invalid-callback-size"};
    }
    if (m_callbacks.size() >= m_config.maxQueuedCallbacks
        || callback.encodedBytes
            > m_config.maxQueuedCallbackBytes - m_queuedCallbackBytes) {
        m_callbacks.clear();
        m_queuedCallbackBytes = 0U;
        m_callbackOverflowed = true;
        return {false, true, "callback-queue-overflow"};
    }
    m_queuedCallbackBytes += callback.encodedBytes;
    m_callbacks.push_back(std::move(callback));
    return {true, false, "callback-queued"};
}

StorageModuleCallbackEnqueue PalaceStorageModuleSession::enqueueNodeChanged(
    const StorageNodeChangedV1& event,
    std::size_t encodedBytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!validNodeEventShape(event)
        || encodedBytes < nodeEventStringBytes(event)) {
        return {false, false, "invalid-node-changed-event"};
    }
    return enqueueCallbackLocked({event, encodedBytes});
}

StorageModuleCallbackEnqueue PalaceStorageModuleSession::enqueueUploadDone(
    const StorageUploadDoneV1& event,
    std::size_t encodedBytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::size_t stringBytes = event.moduleSessionId.size()
        + event.cid.size() + event.error.size();
    if (!isPrintableModuleIdentifier(event.moduleSessionId)
        || event.error.size() > kMaximumErrorBytes
        || encodedBytes < stringBytes
        || (event.succeeded
                ? (!isCanonicalStorageCid(event.cid)
                    || !event.error.empty())
                : (!event.cid.empty() || event.error.empty()))) {
        return {false, false, "invalid-upload-terminal"};
    }
    return enqueueCallbackLocked({event, encodedBytes});
}

StorageModuleCallbackEnqueue PalaceStorageModuleSession::enqueueDownloadDone(
    const StorageDownloadDoneV2& event,
    std::size_t encodedBytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::size_t stringBytes = event.protocol.size()
        + event.moduleOperationId.size() + event.cid.size()
        + event.error.size();
    const bool validOutcome =
        event.outcome == StorageTransferOutcome::Succeeded
        || event.outcome == StorageTransferOutcome::Failed
        || event.outcome == StorageTransferOutcome::Canceled;
    if (event.protocol != kDownloadProtocol
        || event.version != kDownloadVersion
        || !isPrintableModuleIdentifier(event.moduleOperationId)
        || !isCanonicalStorageCid(event.cid)
        || !validOutcome
        || event.error.size() > kMaximumErrorBytes
        || encodedBytes < stringBytes
        || (event.outcome == StorageTransferOutcome::Failed
                ? event.error.empty() : !event.error.empty())) {
        return {false, false, "invalid-download-terminal"};
    }
    return enqueueCallbackLocked({event, encodedBytes});
}

PalaceStorageModuleSession::CallbackDisposition
PalaceStorageModuleSession::applyNodeChangedLocked(
    const StorageNodeChangedV1& event,
    StorageModuleSessionTransition& transition)
{
    if (m_snapshot.has_value()) {
        if (event.instanceId != m_snapshot->instanceId) {
            m_lifecycleOperation.reset();
            enterRecoveryLocked(
                "storage-instance-changed", transition, m_targetRunning);
            return CallbackDisposition::Rejected;
        }
        if (event.epoch < m_snapshot->epoch
            || event.sequence <= m_snapshot->sequence) {
            return CallbackDisposition::Rejected;
        }
    }

    if (!m_lifecycleOperation.has_value()
        || event.operationId
            != m_lifecycleOperation->moduleOperationId
        || event.action != m_lifecycleOperation->action) {
        m_snapshot = event.status;
        m_lifecycleOperation.reset();
        enterRecoveryLocked(
            "uncorrelated-lifecycle-event",
            transition,
            m_targetRunning);
        return CallbackDisposition::Rejected;
    }

    m_snapshot = event.status;
    if (event.phase == "accepted")
        return CallbackDisposition::Applied;

    m_lifecycleOperation->settledReceived = true;
    m_lifecycleOperation->settledSucceeded =
        event.outcome == "succeeded" || event.outcome == "no_op";
    m_lifecycleOperation->settledSnapshot = event.status;
    finalizeLifecycleLocked(transition);
    return CallbackDisposition::Applied;
}

PalaceStorageModuleSession::CallbackDisposition
PalaceStorageModuleSession::applyUploadDoneLocked(
    const StorageUploadDoneV1& event,
    StorageModuleSessionTransition& transition)
{
    const auto mapped =
        m_uploadByModuleSession.find(event.moduleSessionId);
    if (mapped == m_uploadByModuleSession.end()) {
        const bool awaitingUpload = std::any_of(
            m_transfers.begin(),
            m_transfers.end(),
            [](const auto& entry) {
                return entry.second.kind
                        == StorageTransferKind::Upload
                    && entry.second.phase
                        == TransferPhase::AwaitingDispatch;
            });
        return awaitingUpload
            ? CallbackDisposition::Deferred
            : CallbackDisposition::Rejected;
    }
    auto transfer = m_transfers.find(mapped->second);
    if (transfer == m_transfers.end()
        || transfer->second.kind != StorageTransferKind::Upload
        || transfer->second.phase != TransferPhase::Active
        || transfer->second.moduleId != event.moduleSessionId) {
        return CallbackDisposition::Rejected;
    }
    completeTransferLocked(
        transfer,
        event.succeeded
            ? StorageTransferOutcome::Succeeded
            : StorageTransferOutcome::Failed,
        event.cid,
        event.succeeded ? "upload-succeeded" : "upload-failed",
        transition);
    return CallbackDisposition::Applied;
}

PalaceStorageModuleSession::CallbackDisposition
PalaceStorageModuleSession::applyDownloadDoneLocked(
    const StorageDownloadDoneV2& event,
    StorageModuleSessionTransition& transition)
{
    const auto mapped =
        m_downloadByModuleOperation.find(event.moduleOperationId);
    if (mapped == m_downloadByModuleOperation.end())
        return CallbackDisposition::Rejected;
    auto transfer = m_transfers.find(mapped->second);
    if (transfer == m_transfers.end()
        || !isDownloadKind(transfer->second.kind)
        || transfer->second.moduleId != event.moduleOperationId) {
        return CallbackDisposition::Rejected;
    }
    if (transfer->second.phase == TransferPhase::AwaitingDispatch)
        return CallbackDisposition::Deferred;
    if (transfer->second.cid != event.cid) {
        enterRecoveryLocked(
            "download-terminal-cid-mismatch",
            transition,
            true);
        return CallbackDisposition::Rejected;
    }
    completeTransferLocked(
        transfer,
        event.outcome,
        event.cid,
        event.outcome == StorageTransferOutcome::Succeeded
            ? "download-succeeded"
            : event.outcome == StorageTransferOutcome::Canceled
                ? "download-canceled" : "download-failed",
        transition);
    return CallbackDisposition::Applied;
}

StorageModuleSessionTransition PalaceStorageModuleSession::drainCallbacks()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_configured)
        return transitionLocked(false, "session-not-configured");
    StorageModuleSessionTransition transition =
        transitionLocked(true, "callbacks-drained");

    if (m_callbackOverflowed) {
        m_callbackOverflowed = false;
        enterRecoveryLocked(
            "callback-queue-overflow", transition, true);
    }

    const std::size_t callbacksToInspect = m_callbacks.size();
    for (std::size_t index = 0U;
         index < callbacksToInspect && !m_callbacks.empty();
         ++index) {
        Callback callback = std::move(m_callbacks.front());
        m_callbacks.pop_front();
        m_queuedCallbackBytes -= callback.encodedBytes;
        const CallbackDisposition disposition = std::visit(
            [this, &transition](const auto& event) {
                using Event = std::decay_t<decltype(event)>;
                if constexpr (std::is_same_v<
                                  Event, StorageNodeChangedV1>) {
                    return applyNodeChangedLocked(event, transition);
                } else if constexpr (std::is_same_v<
                                         Event, StorageUploadDoneV1>) {
                    return applyUploadDoneLocked(event, transition);
                } else {
                    return applyDownloadDoneLocked(event, transition);
                }
            },
            callback.event);
        if (disposition == CallbackDisposition::Deferred) {
            m_queuedCallbackBytes += callback.encodedBytes;
            m_callbacks.push_back(std::move(callback));
            continue;
        }
        ++transition.processedCallbacks;
        if (disposition == CallbackDisposition::Rejected)
            ++transition.rejectedCallbacks;
    }
    if (!m_callbacks.empty())
        transition.reason = "callbacks-deferred";
    return transition;
}

} // namespace palace
