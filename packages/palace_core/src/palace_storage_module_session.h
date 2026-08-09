#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace palace {

enum class StorageModuleSessionState : std::uint8_t {
    Unconfigured = 0U,
    Offline = 1U,
    Reconciling = 2U,
    Initializing = 3U,
    Stopped = 4U,
    Starting = 5U,
    Running = 6U,
    Stopping = 7U,
    Recovering = 8U,
    ReconciliationRequired = 9U,
};

enum class StorageNodeState : std::uint8_t {
    Uninitialized = 0U,
    Initializing = 1U,
    Stopped = 2U,
    Starting = 3U,
    Running = 4U,
    Stopping = 5U,
    Destroying = 6U,
};

enum class StorageLifecycleAction : std::uint8_t {
    Initialize = 0U,
    Start = 1U,
    Stop = 2U,
    Destroy = 3U,
};

enum class StorageModuleCommandKind : std::uint8_t {
    QueryNodeStatus = 0U,
    NodeAction = 1U,
    UploadUrl = 2U,
    UploadCancel = 3U,
    DownloadToUrlV2 = 4U,
    DownloadCancelV2 = 5U,
};

enum class StorageTransferKind : std::uint8_t {
    Upload = 0U,
    NetworkFetch = 1U,
    LocalVerification = 2U,
    BootstrapFetch = 3U,
};

enum class StorageTransferOutcome : std::uint8_t {
    Succeeded = 0U,
    Failed = 1U,
    Canceled = 2U,
    Interrupted = 3U,
};

struct StorageModuleSessionConfigV1 {
    // Passed only to the exact `initialize` nodeAction. Never persisted here.
    std::string initializationConfig;
    // When true, this session observes and uses an already-running node owned
    // by another visible control surface. It never initializes, starts, or
    // stops that node, and therefore has no initialization config.
    bool externallyManaged = false;
    // Printable stable prefix unique to this host process/session.
    std::string operationIdPrefix;
    std::size_t maxPendingTransfers = 128U;
    std::size_t maxCompletedTransfers = 512U;
    std::size_t maxQueuedCallbacks = 256U;
    std::size_t maxQueuedCallbackBytes = 1024U * 1024U;
    std::size_t maxCallbackPayloadBytes = 64U * 1024U;
    std::uint64_t maxTransferBytes = 64U * 1024U * 1024U;
    std::uint32_t defaultChunkBytes = 64U * 1024U;
};

struct StorageNodePendingOperationV1 {
    std::string operationId;
    StorageLifecycleAction action = StorageLifecycleAction::Initialize;
};

struct StorageNodeCompletedOperationV1 {
    std::string operationId;
    StorageLifecycleAction action = StorageLifecycleAction::Initialize;
    std::string outcome;
};

struct StorageNodeSnapshotV1 {
    std::string schema;
    std::uint32_t version = 0U;
    std::string instanceId;
    std::uint64_t epoch = 0U;
    std::uint64_t sequence = 0U;
    std::string scopeKind;
    StorageNodeState state = StorageNodeState::Uninitialized;
    std::vector<StorageLifecycleAction> supportedActions;
    std::optional<StorageNodePendingOperationV1> pendingOperation;
    std::optional<StorageNodeCompletedOperationV1> lastCompletedOperation;
};

struct StorageNodeActionAcknowledgementV1 {
    std::string schema;
    std::uint32_t version = 0U;
    std::string operationId;
    bool accepted = false;
    bool duplicate = false;
    std::string instanceId;
    std::uint64_t epoch = 0U;
    std::uint64_t sequence = 0U;
    StorageNodeState state = StorageNodeState::Uninitialized;
    std::string errorCode;
};

struct StorageNodeChangedV1 {
    std::string schema;
    std::uint32_t version = 0U;
    std::string instanceId;
    std::uint64_t epoch = 0U;
    std::uint64_t sequence = 0U;
    std::string scopeKind;
    std::string operationId;
    StorageLifecycleAction action = StorageLifecycleAction::Initialize;
    std::string phase;
    std::string outcome;
    StorageNodeState previousState = StorageNodeState::Uninitialized;
    StorageNodeSnapshotV1 status;
    std::string errorCode;
};

struct StorageDownloadAcknowledgementV2 {
    std::string protocol;
    std::uint32_t version = 0U;
    bool accepted = false;
    std::string moduleOperationId;
    std::string cid;
};

struct StorageUploadDoneV1 {
    bool succeeded = false;
    std::string moduleSessionId;
    std::string cid;
    std::string error;
};

struct StorageDownloadDoneV2 {
    std::string protocol;
    std::uint32_t version = 0U;
    std::string moduleOperationId;
    std::string cid;
    StorageTransferOutcome outcome = StorageTransferOutcome::Failed;
    std::string error;
};

struct StorageModuleCommand {
    StorageModuleCommandKind kind =
        StorageModuleCommandKind::QueryNodeStatus;
    std::string commandId;
    std::string domainOperationId;

    StorageLifecycleAction lifecycleAction =
        StorageLifecycleAction::Initialize;
    std::string lifecycleOperationId;
    std::string initializationConfig;
    std::string expectedInstanceId;
    std::uint64_t expectedEpoch = 0U;
    std::uint64_t expectedSequence = 0U;

    std::string moduleSessionId;
    std::string moduleOperationId;
    std::string cid;
    std::string path;
    bool localOnly = false;
    std::uint32_t chunkBytes = 0U;
    std::uint64_t maxBytes = 0U;
};

struct StorageTransferTerminal {
    std::string domainOperationId;
    StorageTransferKind kind = StorageTransferKind::Upload;
    StorageTransferOutcome outcome = StorageTransferOutcome::Failed;
    std::string cid;
    std::string path;
    bool localOnly = false;
    std::string reason;
};

struct StorageTransferStatus {
    bool found = false;
    bool terminal = false;
    StorageTransferKind kind = StorageTransferKind::Upload;
    StorageTransferOutcome outcome = StorageTransferOutcome::Failed;
    std::string cid;
    std::string path;
    bool localOnly = false;
};

struct StorageModuleSessionTransition {
    bool accepted = false;
    std::string reason;
    std::vector<StorageModuleCommand> commands;
    std::vector<StorageTransferTerminal> terminals;
    std::size_t processedCallbacks = 0U;
    std::size_t rejectedCallbacks = 0U;
};

struct StorageModuleCallbackEnqueue {
    bool accepted = false;
    bool overflowed = false;
    std::string reason;
};

std::string storageModuleSessionStateName(StorageModuleSessionState state);
std::string storageLifecycleActionName(StorageLifecycleAction action);

// Thread-safe, transport-neutral owner of Storage module lifecycle and
// transfer correlation. Every module call is represented by a returned
// command. Callers execute commands only after this object's lock is released,
// then report typed results. Module event callbacks only enqueue bounded data.
//
// State is deliberately not persisted: native instance IDs, lifecycle
// sequences, upload session IDs, and active download IDs are process-local.
// A new process must call start(), reconcile through nodeStatus(), then admit
// new work. A successful local-only terminal proves only that the module
// completed the correlated CID read; it is not a provider-availability or
// durable-possession claim.
class PalaceStorageModuleSession {
public:
    bool configure(const StorageModuleSessionConfigV1& config);
    bool hasConfiguration() const;

    StorageModuleSessionTransition start();
    StorageModuleSessionTransition refreshStatus();
    StorageModuleSessionTransition interrupt(bool recoverable);

    StorageModuleSessionState state() const;
    bool running() const;
    bool reconciliationRequired() const;
    std::size_t pendingTransferCount() const;
    std::size_t queuedCallbackCount() const;
    StorageTransferStatus transferStatus(
        const std::string& domainOperationId) const;

    StorageModuleSessionTransition nodeStatusResult(
        const std::string& commandId,
        bool callSucceeded,
        const StorageNodeSnapshotV1& snapshot);
    StorageModuleSessionTransition nodeActionResult(
        const std::string& commandId,
        const StorageNodeActionAcknowledgementV1& acknowledgement);

    StorageModuleSessionTransition beginUpload(
        const std::string& domainOperationId,
        const std::string& sourcePath,
        std::uint64_t expectedBytes,
        std::uint32_t chunkBytes = 0U);
    StorageModuleSessionTransition beginNetworkFetch(
        const std::string& domainOperationId,
        const std::string& cid,
        const std::string& destinationPath,
        std::uint64_t maxBytes,
        std::uint32_t chunkBytes = 0U);
    StorageModuleSessionTransition beginLocalVerification(
        const std::string& domainOperationId,
        const std::string& cid,
        const std::string& destinationPath,
        std::uint64_t maxBytes,
        std::uint32_t chunkBytes = 0U);
    // Catalog bootstrap seam: fetch a LEZ-finalized Palace manifest CID into a
    // bounded temporary destination before catalog structure is known.
    StorageModuleSessionTransition beginBootstrapFetch(
        const std::string& domainOperationId,
        const std::string& cid,
        const std::string& temporaryDestinationPath,
        std::uint64_t maxBytes,
        std::uint32_t chunkBytes = 0U);

    StorageModuleSessionTransition uploadUrlResult(
        const std::string& commandId,
        bool callSucceeded,
        const std::string& moduleSessionId,
        const std::string& error = {});
    StorageModuleSessionTransition downloadToUrlV2Result(
        const std::string& commandId,
        bool callSucceeded,
        const StorageDownloadAcknowledgementV2& acknowledgement,
        const std::string& error = {});

    StorageModuleCallbackEnqueue enqueueNodeChanged(
        const StorageNodeChangedV1& event,
        std::size_t encodedBytes);
    StorageModuleCallbackEnqueue enqueueUploadDone(
        const StorageUploadDoneV1& event,
        std::size_t encodedBytes);
    StorageModuleCallbackEnqueue enqueueDownloadDone(
        const StorageDownloadDoneV2& event,
        std::size_t encodedBytes);
    StorageModuleSessionTransition drainCallbacks();

private:
    enum class CallbackDisposition : std::uint8_t {
        Applied = 0U,
        Rejected = 1U,
        Deferred = 2U,
    };

    enum class TransferPhase : std::uint8_t {
        AwaitingDispatch = 0U,
        Active = 1U,
    };

    struct Transfer {
        StorageTransferKind kind = StorageTransferKind::Upload;
        TransferPhase phase = TransferPhase::AwaitingDispatch;
        std::string domainOperationId;
        std::string commandId;
        std::string moduleId;
        std::string cid;
        std::string path;
        bool localOnly = false;
        std::uint64_t maxBytes = 0U;
    };

    struct CompletedTransfer {
        StorageTransferKind kind = StorageTransferKind::Upload;
        StorageTransferOutcome outcome = StorageTransferOutcome::Failed;
        std::string cid;
        std::string path;
        bool localOnly = false;
    };

    struct LifecycleOperation {
        StorageLifecycleAction action = StorageLifecycleAction::Initialize;
        std::string commandId;
        std::string moduleOperationId;
        bool acknowledgementReceived = false;
        bool acknowledgementAccepted = false;
        bool settledReceived = false;
        bool settledSucceeded = false;
        std::string expectedInstanceId;
        std::uint64_t expectedEpoch = 0U;
        std::uint64_t expectedSequence = 0U;
        StorageNodeSnapshotV1 settledSnapshot;
    };

    struct Callback {
        std::variant<
            StorageNodeChangedV1,
            StorageUploadDoneV1,
            StorageDownloadDoneV2> event;
        std::size_t encodedBytes = 0U;
    };

    StorageModuleSessionTransition transitionLocked(
        bool accepted,
        std::string reason) const;
    std::string nextIdentifierLocked(const std::string& purpose);
    bool issueStatusQueryLocked(StorageModuleSessionTransition& transition);
    bool issueLifecycleActionLocked(
        StorageLifecycleAction action,
        StorageModuleSessionTransition& transition);
    void advanceLifecycleLocked(StorageModuleSessionTransition& transition);
    void finalizeLifecycleLocked(StorageModuleSessionTransition& transition);
    void enterRecoveryLocked(
        const std::string& reason,
        StorageModuleSessionTransition& transition,
        bool restart);
    void failAllTransfersLocked(
        const std::string& reason,
        StorageModuleSessionTransition& transition);
    void completeTransferLocked(
        std::map<std::string, Transfer>::iterator transfer,
        StorageTransferOutcome outcome,
        const std::string& cid,
        const std::string& reason,
        StorageModuleSessionTransition& transition);
    StorageModuleSessionTransition beginDownloadLocked(
        StorageTransferKind kind,
        const std::string& domainOperationId,
        const std::string& cid,
        const std::string& destinationPath,
        std::uint64_t maxBytes,
        std::uint32_t chunkBytes);
    StorageModuleCallbackEnqueue enqueueCallbackLocked(Callback callback);
    CallbackDisposition applyNodeChangedLocked(
        const StorageNodeChangedV1& event,
        StorageModuleSessionTransition& transition);
    CallbackDisposition applyUploadDoneLocked(
        const StorageUploadDoneV1& event,
        StorageModuleSessionTransition& transition);
    CallbackDisposition applyDownloadDoneLocked(
        const StorageDownloadDoneV2& event,
        StorageModuleSessionTransition& transition);

    mutable std::mutex m_mutex;
    StorageModuleSessionConfigV1 m_config;
    bool m_configured = false;
    StorageModuleSessionState m_state =
        StorageModuleSessionState::Unconfigured;
    bool m_targetRunning = false;
    bool m_recoveryRestart = false;
    bool m_recoveryStopRequired = false;
    bool m_callbackOverflowed = false;
    std::uint64_t m_nextIdentifier = 0U;

    std::optional<StorageNodeSnapshotV1> m_snapshot;
    std::string m_statusQueryCommandId;
    std::optional<LifecycleOperation> m_lifecycleOperation;

    std::map<std::string, Transfer> m_transfers;
    std::map<std::string, std::string> m_transferByCommand;
    std::map<std::string, std::string> m_uploadByModuleSession;
    std::map<std::string, std::string> m_downloadByModuleOperation;
    std::map<std::string, std::string> m_downloadByCid;
    std::map<std::string, std::string> m_downloadByDestination;
    std::map<std::string, CompletedTransfer> m_completedTransfers;

    std::deque<Callback> m_callbacks;
    std::size_t m_queuedCallbackBytes = 0U;
};

} // namespace palace
