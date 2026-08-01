#include <logos_test.h>

#include "palace_storage_module_session.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr char kNativeBase58CidV1[] =
    "zb2rhWeJ5rsKhbfi31kikfdS3hE2pvWzhW27Es4Jmc863vFdx";
constexpr char kOverlongVersionCidV1[] =
    "z5KmBFEjTba7gnFbbavM3Vt1u54hssGFq7Fb6hcN1KCZ6t64Rh1p";

palace::StorageModuleSessionConfigV1 config()
{
    palace::StorageModuleSessionConfigV1 value;
    value.initializationConfig =
        R"({"data-dir":"/tmp/logos-palace-storage"})";
    value.operationIdPrefix = "palace-test";
    return value;
}

palace::StorageModuleSessionConfigV1 externallyManagedConfig()
{
    palace::StorageModuleSessionConfigV1 value = config();
    value.initializationConfig.clear();
    value.externallyManaged = true;
    value.operationIdPrefix = "palace-external";
    return value;
}

std::string cid(char suffix)
{
    std::vector<std::uint8_t> bytes = {0x01U, 0x55U, 0x12U, 0x20U};
    for (std::uint8_t index = 0U; index < 32U; ++index) {
        bytes.push_back(static_cast<std::uint8_t>(
            static_cast<unsigned char>(suffix) + index));
    }
    static constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz234567";
    std::string encoded = "b";
    std::uint32_t accumulator = 0U;
    unsigned int bitCount = 0U;
    for (std::uint8_t byte : bytes) {
        accumulator = (accumulator << 8U) | byte;
        bitCount += 8U;
        while (bitCount >= 5U) {
            bitCount -= 5U;
            encoded.push_back(alphabet[
                (accumulator >> bitCount) & 0x1fU]);
            accumulator &= bitCount == 0U
                ? 0U : ((1U << bitCount) - 1U);
        }
    }
    if (bitCount != 0U) {
        encoded.push_back(alphabet[
            (accumulator << (5U - bitCount)) & 0x1fU]);
    }
    return encoded;
}

std::vector<palace::StorageLifecycleAction> actions(
    palace::StorageNodeState state)
{
    switch (state) {
    case palace::StorageNodeState::Uninitialized:
        return {palace::StorageLifecycleAction::Initialize};
    case palace::StorageNodeState::Stopped:
        return {
            palace::StorageLifecycleAction::Start,
            palace::StorageLifecycleAction::Destroy,
        };
    case palace::StorageNodeState::Starting:
    case palace::StorageNodeState::Running:
        return {palace::StorageLifecycleAction::Stop};
    case palace::StorageNodeState::Initializing:
    case palace::StorageNodeState::Stopping:
    case palace::StorageNodeState::Destroying:
        return {};
    }
    return {};
}

palace::StorageNodeSnapshotV1 snapshot(
    palace::StorageNodeState state,
    std::uint64_t epoch,
    std::uint64_t sequence,
    const std::string& instanceId = "storage-instance-1")
{
    palace::StorageNodeSnapshotV1 value;
    value.schema = "logos.managed_node_lifecycle.snapshot";
    value.version = 1U;
    value.instanceId = instanceId;
    value.epoch = epoch;
    value.sequence = sequence;
    value.scopeKind = "storage";
    value.state = state;
    value.supportedActions = actions(state);
    return value;
}

palace::StorageNodeActionAcknowledgementV1 acknowledgement(
    const palace::StorageModuleCommand& command,
    palace::StorageNodeState state,
    std::uint64_t sequence,
    bool accepted = true)
{
    palace::StorageNodeActionAcknowledgementV1 value;
    value.schema = "logos.managed_node_lifecycle.ack";
    value.version = 1U;
    value.operationId = command.lifecycleOperationId;
    value.accepted = accepted;
    value.instanceId = command.expectedInstanceId;
    value.epoch = command.expectedEpoch;
    value.sequence = sequence;
    value.state = state;
    if (!accepted)
        value.errorCode = "state_mismatch";
    return value;
}

palace::StorageNodeChangedV1 lifecycleEvent(
    const palace::StorageModuleCommand& command,
    const std::string& phase,
    const std::string& outcome,
    palace::StorageNodeState previousState,
    palace::StorageNodeSnapshotV1 status)
{
    palace::StorageNodeChangedV1 value;
    value.schema = "logos.managed_node_lifecycle.event";
    value.version = 1U;
    value.instanceId = status.instanceId;
    value.epoch = status.epoch;
    value.sequence = status.sequence;
    value.scopeKind = "storage";
    value.operationId = command.lifecycleOperationId;
    value.action = command.lifecycleAction;
    value.phase = phase;
    value.outcome = outcome;
    value.previousState = previousState;
    value.status = std::move(status);
    if (outcome == "failed")
        value.errorCode = "lifecycle_action_failed";
    return value;
}

palace::StorageNodeChangedV1 acceptedEvent(
    const palace::StorageModuleCommand& command,
    palace::StorageNodeState previousState,
    palace::StorageNodeState pendingState,
    std::uint64_t epoch,
    std::uint64_t sequence)
{
    palace::StorageNodeSnapshotV1 status =
        snapshot(pendingState, epoch, sequence, command.expectedInstanceId);
    status.pendingOperation = palace::StorageNodePendingOperationV1{
        command.lifecycleOperationId,
        command.lifecycleAction,
    };
    return lifecycleEvent(
        command,
        "accepted",
        "accepted",
        previousState,
        std::move(status));
}

palace::StorageNodeChangedV1 settledEvent(
    const palace::StorageModuleCommand& command,
    palace::StorageNodeState previousState,
    palace::StorageNodeState settledState,
    std::uint64_t epoch,
    std::uint64_t sequence,
    const std::string& outcome = "succeeded")
{
    palace::StorageNodeSnapshotV1 status =
        snapshot(settledState, epoch, sequence, command.expectedInstanceId);
    status.lastCompletedOperation =
        palace::StorageNodeCompletedOperationV1{
            command.lifecycleOperationId,
            command.lifecycleAction,
            outcome,
        };
    return lifecycleEvent(
        command,
        "settled",
        outcome,
        previousState,
        std::move(status));
}

palace::StorageModuleCommand configureExistingRunning(
    palace::PalaceStorageModuleSession& session,
    palace::StorageModuleSessionConfigV1 sessionConfig = config())
{
    LOGOS_ASSERT_TRUE(session.configure(sessionConfig));
    const palace::StorageModuleSessionTransition started = session.start();
    LOGOS_ASSERT_TRUE(started.accepted);
    LOGOS_ASSERT_EQ(started.commands.size(), static_cast<std::size_t>(1));
    const palace::StorageModuleCommand query = started.commands.front();
    LOGOS_ASSERT_EQ(
        static_cast<int>(query.kind),
        static_cast<int>(
            palace::StorageModuleCommandKind::QueryNodeStatus));
    const palace::StorageModuleSessionTransition reconciled =
        session.nodeStatusResult(
            query.commandId,
            true,
            snapshot(palace::StorageNodeState::Running, 2U, 10U));
    LOGOS_ASSERT_TRUE(reconciled.accepted);
    LOGOS_ASSERT_TRUE(reconciled.commands.empty());
    LOGOS_ASSERT_TRUE(session.running());
    return query;
}

palace::StorageDownloadAcknowledgementV2 downloadAcknowledgement(
    const palace::StorageModuleCommand& command)
{
    palace::StorageDownloadAcknowledgementV2 value;
    value.protocol = "logos.storage.download";
    value.version = 2U;
    value.accepted = true;
    value.moduleOperationId = command.moduleOperationId;
    value.cid = command.cid;
    return value;
}

palace::StorageDownloadDoneV2 downloadTerminal(
    const palace::StorageModuleCommand& command,
    palace::StorageTransferOutcome outcome =
        palace::StorageTransferOutcome::Succeeded)
{
    palace::StorageDownloadDoneV2 value;
    value.protocol = "logos.storage.download";
    value.version = 2U;
    value.moduleOperationId = command.moduleOperationId;
    value.cid = command.cid;
    value.outcome = outcome;
    if (outcome == palace::StorageTransferOutcome::Failed)
        value.error = "download failed";
    return value;
}

palace::StorageModuleSessionTransition finishDownload(
    palace::PalaceStorageModuleSession& session,
    const palace::StorageModuleCommand& command)
{
    LOGOS_ASSERT_TRUE(session.downloadToUrlV2Result(
        command.commandId,
        true,
        downloadAcknowledgement(command)).accepted);
    LOGOS_ASSERT_TRUE(
        session.enqueueDownloadDone(downloadTerminal(command), 512U).accepted);
    return session.drainCallbacks();
}

} // namespace

LOGOS_TEST(storage_module_session_initializes_and_starts_once_with_synchronous_callbacks) {
    palace::PalaceStorageModuleSession session;
    LOGOS_ASSERT_TRUE(session.configure(config()));
    LOGOS_ASSERT_FALSE(session.configure(config()));
    const auto started = session.start();
    LOGOS_ASSERT_TRUE(started.accepted);
    const palace::StorageModuleCommand query = started.commands.front();

    const auto initialized = session.nodeStatusResult(
        query.commandId,
        true,
        snapshot(palace::StorageNodeState::Uninitialized, 0U, 0U));
    LOGOS_ASSERT_TRUE(initialized.accepted);
    LOGOS_ASSERT_EQ(initialized.commands.size(), static_cast<std::size_t>(1));
    const palace::StorageModuleCommand initialize =
        initialized.commands.front();
    LOGOS_ASSERT_EQ(
        static_cast<int>(initialize.lifecycleAction),
        static_cast<int>(palace::StorageLifecycleAction::Initialize));
    LOGOS_ASSERT_EQ(
        initialize.initializationConfig,
        config().initializationConfig);

    // initialize() emits both events synchronously before nodeAction returns.
    LOGOS_ASSERT_TRUE(session.enqueueNodeChanged(
        acceptedEvent(
            initialize,
            palace::StorageNodeState::Uninitialized,
            palace::StorageNodeState::Initializing,
            0U,
            1U),
        1024U).accepted);
    LOGOS_ASSERT_TRUE(session.enqueueNodeChanged(
        settledEvent(
            initialize,
            palace::StorageNodeState::Uninitialized,
            palace::StorageNodeState::Stopped,
            1U,
            2U),
        1024U).accepted);
    LOGOS_ASSERT_TRUE(session.nodeActionResult(
        initialize.commandId,
        acknowledgement(
            initialize,
            palace::StorageNodeState::Initializing,
            1U)).accepted);
    const auto initializedCallbacks = session.drainCallbacks();
    LOGOS_ASSERT_EQ(
        initializedCallbacks.processedCallbacks,
        static_cast<std::size_t>(2));
    LOGOS_ASSERT_EQ(
        initializedCallbacks.commands.size(),
        static_cast<std::size_t>(1));
    const palace::StorageModuleCommand start =
        initializedCallbacks.commands.front();
    LOGOS_ASSERT_EQ(
        static_cast<int>(start.lifecycleAction),
        static_cast<int>(palace::StorageLifecycleAction::Start));

    LOGOS_ASSERT_TRUE(session.enqueueNodeChanged(
        acceptedEvent(
            start,
            palace::StorageNodeState::Stopped,
            palace::StorageNodeState::Starting,
            1U,
            3U),
        1024U).accepted);
    LOGOS_ASSERT_TRUE(session.enqueueNodeChanged(
        settledEvent(
            start,
            palace::StorageNodeState::Stopped,
            palace::StorageNodeState::Running,
            1U,
            4U),
        1024U).accepted);
    LOGOS_ASSERT_TRUE(session.nodeActionResult(
        start.commandId,
        acknowledgement(
            start,
            palace::StorageNodeState::Starting,
            3U)).accepted);
    LOGOS_ASSERT_TRUE(session.drainCallbacks().accepted);
    LOGOS_ASSERT_TRUE(session.running());
    const auto duplicateStart = session.start();
    LOGOS_ASSERT_TRUE(duplicateStart.accepted);
    LOGOS_ASSERT_TRUE(duplicateStart.commands.empty());
}

LOGOS_TEST(storage_module_session_external_attach_retries_current_status_without_lifecycle_mutation) {
    palace::PalaceStorageModuleSession session;
    LOGOS_ASSERT_TRUE(session.configure(externallyManagedConfig()));
    const auto requested = session.start();
    LOGOS_ASSERT_TRUE(requested.accepted);
    LOGOS_ASSERT_EQ(requested.commands.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(
        static_cast<int>(requested.commands.front().kind),
        static_cast<int>(palace::StorageModuleCommandKind::QueryNodeStatus));

    const auto stopped = session.nodeStatusResult(
        requested.commands.front().commandId,
        true,
        snapshot(palace::StorageNodeState::Stopped, 1U, 1U));
    LOGOS_ASSERT_TRUE(stopped.accepted);
    LOGOS_ASSERT_EQ(stopped.commands.size(), static_cast<std::size_t>(0));
    LOGOS_ASSERT_FALSE(session.running());

    const auto retry = session.start();
    LOGOS_ASSERT_TRUE(retry.accepted);
    LOGOS_ASSERT_EQ(retry.commands.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(
        static_cast<int>(retry.commands.front().kind),
        static_cast<int>(palace::StorageModuleCommandKind::QueryNodeStatus));
    const auto attached = session.nodeStatusResult(
        retry.commands.front().commandId,
        true,
        snapshot(palace::StorageNodeState::Running, 3U, 7U));
    LOGOS_ASSERT_TRUE(attached.accepted);
    LOGOS_ASSERT_EQ(attached.commands.size(), static_cast<std::size_t>(0));
    LOGOS_ASSERT_TRUE(session.running());

    // A repeated explicit attach is a freshness check, not an already-running
    // shortcut. Once queried, a stopped node must no longer be usable.
    const auto recheck = session.start();
    LOGOS_ASSERT_TRUE(recheck.accepted);
    LOGOS_ASSERT_EQ(recheck.commands.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(
        static_cast<int>(recheck.commands.front().kind),
        static_cast<int>(palace::StorageModuleCommandKind::QueryNodeStatus));
    LOGOS_ASSERT_FALSE(session.running());
    const auto stoppedAfterRecheck = session.nodeStatusResult(
        recheck.commands.front().commandId,
        true,
        snapshot(palace::StorageNodeState::Stopped, 3U, 8U));
    LOGOS_ASSERT_TRUE(stoppedAfterRecheck.accepted);
    LOGOS_ASSERT_EQ(
        stoppedAfterRecheck.commands.size(), static_cast<std::size_t>(0));
    LOGOS_ASSERT_FALSE(session.running());

    const auto interrupted = session.interrupt(true);
    LOGOS_ASSERT_TRUE(interrupted.accepted);
    LOGOS_ASSERT_EQ(interrupted.commands.size(), static_cast<std::size_t>(0));
    LOGOS_ASSERT_FALSE(session.running());
}

LOGOS_TEST(storage_module_session_external_attach_rechecks_replaced_or_restarted_node) {
    palace::PalaceStorageModuleSession session;
    LOGOS_ASSERT_TRUE(session.configure(externallyManagedConfig()));

    const auto initial = session.start();
    LOGOS_ASSERT_EQ(initial.commands.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_TRUE(session.nodeStatusResult(
        initial.commands.front().commandId,
        true,
        snapshot(
            palace::StorageNodeState::Running,
            4U,
            10U,
            "storage-instance-1")).accepted);
    LOGOS_ASSERT_TRUE(session.running());

    // A replacement is only usable after the explicit query observes its
    // current state. Do not retain the old instance's running state.
    const auto replacement = session.start();
    LOGOS_ASSERT_EQ(replacement.commands.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(
        static_cast<int>(replacement.commands.front().kind),
        static_cast<int>(palace::StorageModuleCommandKind::QueryNodeStatus));
    LOGOS_ASSERT_FALSE(session.running());
    const auto replacementStopped = session.nodeStatusResult(
        replacement.commands.front().commandId,
        true,
        snapshot(
            palace::StorageNodeState::Stopped,
            0U,
            0U,
            "storage-instance-2"));
    LOGOS_ASSERT_TRUE(replacementStopped.accepted);
    LOGOS_ASSERT_EQ(
        replacementStopped.commands.size(), static_cast<std::size_t>(0));
    LOGOS_ASSERT_FALSE(session.running());

    const auto attachReplacement = session.start();
    LOGOS_ASSERT_EQ(
        attachReplacement.commands.size(), static_cast<std::size_t>(1));
    const auto replacementRunning = session.nodeStatusResult(
        attachReplacement.commands.front().commandId,
        true,
        snapshot(
            palace::StorageNodeState::Running,
            1U,
            1U,
            "storage-instance-2"));
    LOGOS_ASSERT_TRUE(replacementRunning.accepted);
    LOGOS_ASSERT_EQ(
        replacementRunning.commands.size(), static_cast<std::size_t>(0));
    LOGOS_ASSERT_TRUE(session.running());

    // A restart may reset counters without changing the instance identifier.
    // Reject the stale answer and forget it, then permit one explicit retry.
    const auto restart = session.start();
    LOGOS_ASSERT_EQ(restart.commands.size(), static_cast<std::size_t>(1));
    const auto staleRestart = session.nodeStatusResult(
        restart.commands.front().commandId,
        true,
        snapshot(
            palace::StorageNodeState::Stopped,
            0U,
            0U,
            "storage-instance-2"));
    LOGOS_ASSERT_FALSE(staleRestart.accepted);
    LOGOS_ASSERT_EQ(staleRestart.reason, std::string("stale-node-status"));
    LOGOS_ASSERT_EQ(staleRestart.commands.size(), static_cast<std::size_t>(0));
    LOGOS_ASSERT_FALSE(session.running());

    const auto retryAfterRestart = session.start();
    LOGOS_ASSERT_EQ(
        retryAfterRestart.commands.size(), static_cast<std::size_t>(1));
    const auto restartedRunning = session.nodeStatusResult(
        retryAfterRestart.commands.front().commandId,
        true,
        snapshot(
            palace::StorageNodeState::Running,
            1U,
            1U,
            "storage-instance-2"));
    LOGOS_ASSERT_TRUE(restartedRunning.accepted);
    LOGOS_ASSERT_EQ(
        restartedRunning.commands.size(), static_cast<std::size_t>(0));
    LOGOS_ASSERT_TRUE(session.running());
}

LOGOS_TEST(storage_module_session_accepts_settled_before_accepted_and_rejects_duplicates) {
    palace::PalaceStorageModuleSession session;
    LOGOS_ASSERT_TRUE(session.configure(config()));
    const auto initial = session.start();
    const auto reconciled = session.nodeStatusResult(
        initial.commands.front().commandId,
        true,
        snapshot(palace::StorageNodeState::Stopped, 1U, 2U));
    const palace::StorageModuleCommand start = reconciled.commands.front();

    LOGOS_ASSERT_TRUE(session.enqueueNodeChanged(
        settledEvent(
            start,
            palace::StorageNodeState::Stopped,
            palace::StorageNodeState::Running,
            1U,
            4U),
        1024U).accepted);
    LOGOS_ASSERT_TRUE(session.enqueueNodeChanged(
        acceptedEvent(
            start,
            palace::StorageNodeState::Stopped,
            palace::StorageNodeState::Starting,
            1U,
            3U),
        1024U).accepted);
    LOGOS_ASSERT_TRUE(session.nodeActionResult(
        start.commandId,
        acknowledgement(
            start,
            palace::StorageNodeState::Starting,
            3U)).accepted);
    const auto reordered = session.drainCallbacks();
    LOGOS_ASSERT_TRUE(session.running());
    LOGOS_ASSERT_EQ(
        reordered.processedCallbacks,
        static_cast<std::size_t>(2));
    LOGOS_ASSERT_EQ(
        reordered.rejectedCallbacks,
        static_cast<std::size_t>(1));

    LOGOS_ASSERT_TRUE(session.enqueueNodeChanged(
        settledEvent(
            start,
            palace::StorageNodeState::Stopped,
            palace::StorageNodeState::Running,
            1U,
            4U),
        1024U).accepted);
    const auto duplicate = session.drainCallbacks();
    LOGOS_ASSERT_EQ(
        duplicate.rejectedCallbacks,
        static_cast<std::size_t>(1));
    LOGOS_ASSERT_FALSE(session.nodeActionResult(
        start.commandId,
        acknowledgement(
            start,
            palace::StorageNodeState::Starting,
            3U)).accepted);
}

LOGOS_TEST(storage_module_session_fails_closed_then_restarts_after_lifecycle_failure) {
    palace::PalaceStorageModuleSession session;
    LOGOS_ASSERT_TRUE(session.configure(config()));
    const auto initial = session.start();
    const auto reconciled = session.nodeStatusResult(
        initial.commands.front().commandId,
        true,
        snapshot(palace::StorageNodeState::Stopped, 1U, 2U));
    const palace::StorageModuleCommand start = reconciled.commands.front();
    LOGOS_ASSERT_TRUE(session.nodeActionResult(
        start.commandId,
        acknowledgement(
            start,
            palace::StorageNodeState::Starting,
            3U)).accepted);
    LOGOS_ASSERT_TRUE(session.enqueueNodeChanged(
        acceptedEvent(
            start,
            palace::StorageNodeState::Stopped,
            palace::StorageNodeState::Starting,
            1U,
            3U),
        1024U).accepted);
    LOGOS_ASSERT_TRUE(session.enqueueNodeChanged(
        settledEvent(
            start,
            palace::StorageNodeState::Stopped,
            palace::StorageNodeState::Stopped,
            1U,
            4U,
            "failed"),
        1024U).accepted);
    session.drainCallbacks();
    LOGOS_ASSERT_TRUE(session.reconciliationRequired());

    const auto retried = session.start();
    LOGOS_ASSERT_TRUE(retried.accepted);
    LOGOS_ASSERT_EQ(retried.commands.size(), static_cast<std::size_t>(1));
    palace::StorageNodeSnapshotV1 stopped =
        snapshot(palace::StorageNodeState::Stopped, 1U, 4U);
    stopped.lastCompletedOperation =
        palace::StorageNodeCompletedOperationV1{
            start.lifecycleOperationId,
            palace::StorageLifecycleAction::Start,
            "failed",
        };
    const auto retryStatus = session.nodeStatusResult(
        retried.commands.front().commandId,
        true,
        stopped);
    LOGOS_ASSERT_TRUE(retryStatus.accepted);
    LOGOS_ASSERT_EQ(retryStatus.commands.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_NE(
        retryStatus.commands.front().lifecycleOperationId,
        start.lifecycleOperationId);
}

LOGOS_TEST(storage_module_session_correlates_early_upload_terminal_to_domain_operation) {
    palace::PalaceStorageModuleSession session;
    configureExistingRunning(session);
    const auto upload = session.beginUpload(
        "publish-palace",
        "/tmp/logos-palace-storage/palace.manifest",
        4096U);
    LOGOS_ASSERT_TRUE(upload.accepted);
    const palace::StorageModuleCommand command = upload.commands.front();
    LOGOS_ASSERT_NE(command.commandId, command.domainOperationId);

    palace::StorageUploadDoneV1 early;
    early.succeeded = true;
    early.moduleSessionId = "native-upload-session-17";
    early.cid = kNativeBase58CidV1;
    LOGOS_ASSERT_TRUE(session.enqueueUploadDone(early, 512U).accepted);
    const auto deferred = session.drainCallbacks();
    LOGOS_ASSERT_EQ(
        deferred.processedCallbacks,
        static_cast<std::size_t>(0));
    LOGOS_ASSERT_EQ(
        session.queuedCallbackCount(),
        static_cast<std::size_t>(1));

    LOGOS_ASSERT_TRUE(session.uploadUrlResult(
        command.commandId,
        true,
        early.moduleSessionId).accepted);
    const auto terminal = session.drainCallbacks();
    LOGOS_ASSERT_EQ(terminal.terminals.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(
        terminal.terminals.front().domainOperationId,
        std::string("publish-palace"));
    LOGOS_ASSERT_EQ(terminal.terminals.front().cid, early.cid);
    LOGOS_ASSERT_TRUE(
        session.transferStatus("publish-palace").terminal);

    LOGOS_ASSERT_TRUE(session.enqueueUploadDone(early, 512U).accepted);
    const auto duplicate = session.drainCallbacks();
    LOGOS_ASSERT_EQ(
        duplicate.rejectedCallbacks,
        static_cast<std::size_t>(1));
    LOGOS_ASSERT_TRUE(duplicate.terminals.empty());
}

LOGOS_TEST(storage_module_session_rejects_noncanonical_base58btc_cidv1)
{
    palace::PalaceStorageModuleSession session;
    configureExistingRunning(session);
    LOGOS_ASSERT_TRUE(session.beginNetworkFetch(
        "fetch-native-z",
        kNativeBase58CidV1,
        "/tmp/logos-palace-storage/native-z.bin",
        8192U).accepted);
    LOGOS_ASSERT_FALSE(session.beginNetworkFetch(
        "fetch-leading-zero-z",
        std::string("z1") + (kNativeBase58CidV1 + 1),
        "/tmp/logos-palace-storage/leading-zero-z.bin",
        8192U).accepted);

    palace::StorageUploadDoneV1 malformed;
    malformed.succeeded = true;
    malformed.moduleSessionId = "native-upload-session-invalid";
    malformed.cid = kOverlongVersionCidV1;
    LOGOS_ASSERT_FALSE(
        session.enqueueUploadDone(malformed, 512U).accepted);
}

LOGOS_TEST(storage_module_session_rejects_mismatched_ids_cids_and_destinations) {
    palace::PalaceStorageModuleSession session;
    configureExistingRunning(session);
    const auto network = session.beginNetworkFetch(
        "fetch-room",
        cid('b'),
        "/tmp/logos-palace-storage/room.manifest",
        8192U);
    LOGOS_ASSERT_TRUE(network.accepted);
    const palace::StorageModuleCommand command = network.commands.front();
    LOGOS_ASSERT_FALSE(command.localOnly);

    LOGOS_ASSERT_FALSE(session.beginNetworkFetch(
        "fetch-same-cid",
        command.cid,
        "/tmp/logos-palace-storage/other.manifest",
        8192U).accepted);
    LOGOS_ASSERT_FALSE(session.beginNetworkFetch(
        "fetch-same-destination",
        cid('c'),
        command.path,
        8192U).accepted);
    LOGOS_ASSERT_FALSE(session.beginNetworkFetch(
        "fetch-room",
        cid('c'),
        "/tmp/logos-palace-storage/third.manifest",
        8192U).accepted);
    LOGOS_ASSERT_FALSE(session.downloadToUrlV2Result(
        "wrong-command",
        true,
        downloadAcknowledgement(command)).accepted);

    palace::StorageDownloadAcknowledgementV2 wrong =
        downloadAcknowledgement(command);
    wrong.cid = cid('c');
    const auto rejected = session.downloadToUrlV2Result(
        command.commandId,
        true,
        wrong);
    LOGOS_ASSERT_FALSE(rejected.accepted);
    LOGOS_ASSERT_EQ(rejected.terminals.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(
        static_cast<int>(rejected.terminals.front().outcome),
        static_cast<int>(
            palace::StorageTransferOutcome::Interrupted));
    LOGOS_ASSERT_FALSE(session.running());
}

LOGOS_TEST(storage_module_session_enforces_pending_history_and_callback_bounds) {
    palace::StorageModuleSessionConfigV1 limits = config();
    limits.maxPendingTransfers = 1U;
    limits.maxCompletedTransfers = 1U;
    limits.maxQueuedCallbacks = 1U;
    limits.maxQueuedCallbackBytes = 1024U;
    limits.maxCallbackPayloadBytes = 1024U;
    palace::PalaceStorageModuleSession session;
    configureExistingRunning(session, limits);

    const auto first = session.beginUpload(
        "upload-1",
        "/tmp/logos-palace-storage/one",
        10U);
    LOGOS_ASSERT_TRUE(first.accepted);
    LOGOS_ASSERT_FALSE(session.beginUpload(
        "upload-2",
        "/tmp/logos-palace-storage/two",
        10U).accepted);
    const auto failed = session.uploadUrlResult(
        first.commands.front().commandId,
        false,
        {},
        "dispatch failed");
    LOGOS_ASSERT_EQ(failed.terminals.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_FALSE(session.beginUpload(
        "upload-2",
        "/tmp/logos-palace-storage/two",
        10U).accepted);
    LOGOS_ASSERT_FALSE(session.beginUpload(
        "bad/path",
        "/tmp/logos-palace-storage/two",
        10U).accepted);
    LOGOS_ASSERT_FALSE(session.beginBootstrapFetch(
        "bootstrap",
        "not-a-cid",
        "/tmp/logos-palace-storage/bootstrap",
        100U).accepted);

    palace::StorageUploadDoneV1 unknown;
    unknown.succeeded = false;
    unknown.moduleSessionId = "unknown-session";
    unknown.error = "failed";
    LOGOS_ASSERT_FALSE(
        session.enqueueUploadDone(unknown, 1025U).accepted);
    LOGOS_ASSERT_TRUE(
        session.enqueueUploadDone(unknown, 512U).accepted);
    const auto overflow = session.enqueueUploadDone(unknown, 512U);
    LOGOS_ASSERT_FALSE(overflow.accepted);
    LOGOS_ASSERT_TRUE(overflow.overflowed);
}

LOGOS_TEST(storage_module_session_overflow_requires_native_stop_then_restart) {
    palace::StorageModuleSessionConfigV1 limits = config();
    limits.maxQueuedCallbacks = 1U;
    limits.maxQueuedCallbackBytes = 2048U;
    limits.maxCallbackPayloadBytes = 2048U;
    palace::PalaceStorageModuleSession session;
    configureExistingRunning(session, limits);
    const auto fetch = session.beginNetworkFetch(
        "fetch-before-overflow",
        cid('d'),
        "/tmp/logos-palace-storage/before-overflow",
        4096U);
    const palace::StorageModuleCommand download = fetch.commands.front();
    LOGOS_ASSERT_TRUE(session.downloadToUrlV2Result(
        download.commandId,
        true,
        downloadAcknowledgement(download)).accepted);

    LOGOS_ASSERT_TRUE(
        session.enqueueDownloadDone(downloadTerminal(download), 512U)
            .accepted);
    const auto overflowed =
        session.enqueueDownloadDone(downloadTerminal(download), 512U);
    LOGOS_ASSERT_TRUE(overflowed.overflowed);
    const auto recovery = session.drainCallbacks();
    LOGOS_ASSERT_EQ(recovery.terminals.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(
        static_cast<int>(recovery.terminals.front().outcome),
        static_cast<int>(
            palace::StorageTransferOutcome::Interrupted));
    LOGOS_ASSERT_EQ(recovery.commands.size(), static_cast<std::size_t>(2));
    LOGOS_ASSERT_EQ(
        static_cast<int>(recovery.commands.front().kind),
        static_cast<int>(
            palace::StorageModuleCommandKind::DownloadCancelV2));
    const palace::StorageModuleCommand stop = recovery.commands.back();
    LOGOS_ASSERT_EQ(
        static_cast<int>(stop.lifecycleAction),
        static_cast<int>(palace::StorageLifecycleAction::Stop));

    LOGOS_ASSERT_TRUE(session.nodeActionResult(
        stop.commandId,
        acknowledgement(
            stop,
            palace::StorageNodeState::Stopping,
            11U)).accepted);
    LOGOS_ASSERT_TRUE(session.enqueueNodeChanged(
        acceptedEvent(
            stop,
            palace::StorageNodeState::Running,
            palace::StorageNodeState::Stopping,
            2U,
            11U),
        1024U).accepted);
    LOGOS_ASSERT_TRUE(session.enqueueNodeChanged(
        settledEvent(
            stop,
            palace::StorageNodeState::Running,
            palace::StorageNodeState::Stopped,
            2U,
            12U),
        1024U).overflowed);
    // Overflow during recovery discards accepted event and starts another
    // explicit reconciliation cycle.
    const auto secondRecovery = session.drainCallbacks();
    LOGOS_ASSERT_FALSE(secondRecovery.commands.empty());
    LOGOS_ASSERT_EQ(
        static_cast<int>(secondRecovery.commands.back().kind),
        static_cast<int>(
            palace::StorageModuleCommandKind::QueryNodeStatus));

    palace::StorageNodeSnapshotV1 stopped =
        snapshot(palace::StorageNodeState::Stopped, 2U, 12U);
    stopped.lastCompletedOperation =
        palace::StorageNodeCompletedOperationV1{
            stop.lifecycleOperationId,
            palace::StorageLifecycleAction::Stop,
            "succeeded",
        };
    const auto reconciled = session.nodeStatusResult(
        secondRecovery.commands.back().commandId,
        true,
        stopped);
    LOGOS_ASSERT_EQ(reconciled.commands.size(), static_cast<std::size_t>(1));
    const palace::StorageModuleCommand start = reconciled.commands.front();
    LOGOS_ASSERT_EQ(
        static_cast<int>(start.lifecycleAction),
        static_cast<int>(palace::StorageLifecycleAction::Start));

    LOGOS_ASSERT_TRUE(session.nodeActionResult(
        start.commandId,
        acknowledgement(
            start,
            palace::StorageNodeState::Starting,
            13U)).accepted);
    LOGOS_ASSERT_TRUE(session.enqueueNodeChanged(
        settledEvent(
            start,
            palace::StorageNodeState::Stopped,
            palace::StorageNodeState::Running,
            2U,
            14U),
        1024U).accepted);
    session.drainCallbacks();
    LOGOS_ASSERT_TRUE(session.running());
}

LOGOS_TEST(storage_module_session_supports_bootstrap_then_network_and_local_verification) {
    palace::PalaceStorageModuleSession session;
    configureExistingRunning(session);

    const auto bootstrap = session.beginBootstrapFetch(
        "bootstrap-palace-manifest",
        cid('e'),
        "/tmp/logos-palace-storage/bootstrap.next",
        64U * 1024U);
    LOGOS_ASSERT_TRUE(bootstrap.accepted);
    const palace::StorageModuleCommand bootstrapCommand =
        bootstrap.commands.front();
    LOGOS_ASSERT_FALSE(bootstrapCommand.localOnly);
    const auto bootstrapDone = finishDownload(session, bootstrapCommand);
    LOGOS_ASSERT_EQ(
        static_cast<int>(bootstrapDone.terminals.front().kind),
        static_cast<int>(
            palace::StorageTransferKind::BootstrapFetch));

    const auto network = session.beginNetworkFetch(
        "fetch-prop",
        cid('f'),
        "/tmp/logos-palace-storage/prop.network",
        64U * 1024U);
    const palace::StorageModuleCommand networkCommand =
        network.commands.front();
    LOGOS_ASSERT_FALSE(networkCommand.localOnly);
    LOGOS_ASSERT_TRUE(
        finishDownload(session, networkCommand).accepted);

    const auto local = session.beginLocalVerification(
        "verify-prop-local",
        networkCommand.cid,
        "/tmp/logos-palace-storage/prop.local",
        64U * 1024U);
    LOGOS_ASSERT_TRUE(local.accepted);
    const palace::StorageModuleCommand localCommand =
        local.commands.front();
    LOGOS_ASSERT_TRUE(localCommand.localOnly);
    const auto localDone = finishDownload(session, localCommand);
    LOGOS_ASSERT_EQ(localDone.terminals.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(
        static_cast<int>(localDone.terminals.front().kind),
        static_cast<int>(
            palace::StorageTransferKind::LocalVerification));
    LOGOS_ASSERT_TRUE(localDone.terminals.front().localOnly);
}

LOGOS_TEST(storage_module_session_interrupt_clears_transient_work_and_reconciles) {
    palace::PalaceStorageModuleSession session;
    configureExistingRunning(session);
    const auto fetch = session.beginNetworkFetch(
        "fetch-on-interrupt",
        cid('g'),
        "/tmp/logos-palace-storage/interrupted",
        4096U);
    const palace::StorageModuleCommand command = fetch.commands.front();
    LOGOS_ASSERT_TRUE(session.downloadToUrlV2Result(
        command.commandId,
        true,
        downloadAcknowledgement(command)).accepted);
    const auto interrupted = session.interrupt(false);
    LOGOS_ASSERT_EQ(interrupted.terminals.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(interrupted.commands.size(), static_cast<std::size_t>(2));
    LOGOS_ASSERT_EQ(
        static_cast<int>(interrupted.commands.front().kind),
        static_cast<int>(
            palace::StorageModuleCommandKind::DownloadCancelV2));
    LOGOS_ASSERT_EQ(
        static_cast<int>(interrupted.commands.back().lifecycleAction),
        static_cast<int>(palace::StorageLifecycleAction::Stop));
    LOGOS_ASSERT_FALSE(session.beginNetworkFetch(
        "new-work",
        cid('h'),
        "/tmp/logos-palace-storage/new-work",
        4096U).accepted);
}
