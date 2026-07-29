#include "palace_storage_module_codec.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

constexpr char kNativeBase58CidV1[] =
    "zb2rhWeJ5rsKhbfi31kikfdS3hE2pvWzhW27Es4Jmc863vFdx";
constexpr char kOverlongVersionCidV1[] =
    "z5KmBFEjTba7gnFbbavM3Vt1u54hssGFq7Fb6hcN1KCZ6t64Rh1p";

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::cerr << __FILE__ << ':' << __LINE__                         \
                      << ": check failed: " #condition "\n";                 \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

std::string runningSnapshot(
    const std::string& epoch = "7",
    const std::string& sequence = "9")
{
    return std::string{
        R"({"schema":"logos.managed_node_lifecycle.snapshot","version":1,"instance_id":"storage-instance-1","epoch":)"}
        + epoch
        + R"(,"sequence":)" + sequence
        + R"(,"scope":{"kind":"storage"},"state":"running","health":"unknown","supported_actions":["stop"],"pending_operation":null,"last_completed_operation":null,"last_error":null,"updated_at_ms":9223372036854775807})";
}

std::string startingSnapshot(const bool pending)
{
    return std::string{
        R"({"schema":"logos.managed_node_lifecycle.snapshot","version":1,"instance_id":"storage-instance-1","epoch":7,"sequence":10,"scope":{"kind":"storage"},"state":"starting","health":"unknown","supported_actions":["stop"],"pending_operation":)"}
        + (pending
                ? R"({"operation_id":"palace:lifecycle:1","action":"start"})"
                : "null")
        + R"(,"last_completed_operation":null,"last_error":null,"updated_at_ms":1000})";
}

std::string failedSnapshot()
{
    return R"({"schema":"logos.managed_node_lifecycle.snapshot","version":1,"instance_id":"storage-instance-1","epoch":7,"sequence":11,"scope":{"kind":"storage"},"state":"stopped","health":"degraded","supported_actions":["start","destroy"],"pending_operation":null,"last_completed_operation":{"operation_id":"palace:lifecycle:1","action":"start","outcome":"failed"},"last_error":{"code":"start_failed","message":"Storage start failed.","occurred_at_ms":1001},"updated_at_ms":1001})";
}

void testNodeActionEncoding()
{
    palace::StorageModuleCommand command;
    command.kind = palace::StorageModuleCommandKind::NodeAction;
    command.lifecycleAction = palace::StorageLifecycleAction::Initialize;
    command.lifecycleOperationId = "palace:lifecycle:1";
    command.expectedInstanceId = "storage-instance-1";
    command.expectedEpoch = 4U;
    command.expectedSequence = 9U;
    command.initializationConfig = R"({"data-dir":"C:\\storage"})";

    const palace::StorageModuleEncodedCommand encoded =
        palace::encodeStorageModuleNodeAction(command);
    CHECK(encoded.accepted);
    CHECK(encoded.reason.empty());
    CHECK(
        encoded.payload
        == R"({"schema":"logos.managed_node_lifecycle.command","version":1,"operation_id":"palace:lifecycle:1","action":"initialize","expected":{"instance_id":"storage-instance-1","epoch":4,"sequence":9},"parameters":{"config":"{\"data-dir\":\"C:\\\\storage\"}"}})");

    command.lifecycleAction = palace::StorageLifecycleAction::Start;
    command.initializationConfig.clear();
    const auto start = palace::encodeStorageModuleNodeAction(command);
    CHECK(start.accepted);
    CHECK(start.payload.find(R"("action":"start")") != std::string::npos);
    CHECK(start.payload.find(R"("parameters":{})") != std::string::npos);

    command.kind = palace::StorageModuleCommandKind::QueryNodeStatus;
    CHECK(!palace::encodeStorageModuleNodeAction(command).accepted);
    command.kind = palace::StorageModuleCommandKind::NodeAction;
    command.lifecycleOperationId.clear();
    CHECK(!palace::encodeStorageModuleNodeAction(command).accepted);
    command.lifecycleOperationId = "palace:lifecycle:1";
    command.initializationConfig = "not-allowed";
    CHECK(!palace::encodeStorageModuleNodeAction(command).accepted);
}

void testNodeStatus()
{
    palace::StorageNodeSnapshotV1 snapshot;
    const auto result = palace::parseStorageModuleNodeStatus(
        runningSnapshot("18446744073709551615", "9007199254740993"),
        snapshot);
    CHECK(result.accepted);
    CHECK(snapshot.epoch == UINT64_MAX);
    CHECK(snapshot.sequence == 9007199254740993ULL);
    CHECK(snapshot.instanceId == "storage-instance-1");
    CHECK(snapshot.state == palace::StorageNodeState::Running);
    CHECK(
        snapshot.supportedActions
        == std::vector<palace::StorageLifecycleAction>{
            palace::StorageLifecycleAction::Stop});

    palace::StorageNodeSnapshotV1 unchanged;
    unchanged.instanceId = "sentinel";
    CHECK(
        !palace::parseStorageModuleNodeStatus(
             runningSnapshot("1.0"),
             unchanged)
             .accepted);
    CHECK(unchanged.instanceId == "sentinel");
    CHECK(
        !palace::parseStorageModuleNodeStatus(
             runningSnapshot("1e0"),
             unchanged)
             .accepted);
    CHECK(
        !palace::parseStorageModuleNodeStatus(
             runningSnapshot("-1"),
             unchanged)
             .accepted);

    std::string duplicate = runningSnapshot();
    const std::string needle = R"("version":1,)";
    duplicate.replace(
        duplicate.find(needle),
        needle.size(),
        R"("version":1,"version":1,)");
    const auto duplicateResult =
        palace::parseStorageModuleNodeStatus(duplicate, unchanged);
    CHECK(!duplicateResult.accepted);
    CHECK(duplicateResult.reason == "duplicate-json-key");

    std::string extra = runningSnapshot();
    extra.insert(extra.size() - 1U, R"(,"unexpected":true)");
    CHECK(
        !palace::parseStorageModuleNodeStatus(extra, unchanged).accepted);

    std::string wrongActions = runningSnapshot();
    wrongActions.replace(
        wrongActions.find(R"(["stop"])"),
        std::string{R"(["stop"])"}.size(),
        R"(["start"])");
    CHECK(
        !palace::parseStorageModuleNodeStatus(
             wrongActions,
             unchanged)
             .accepted);

    std::string invalidUtf8 = runningSnapshot();
    invalidUtf8.insert(invalidUtf8.find("storage-instance-1"), 1U, '\xff');
    CHECK(
        !palace::parseStorageModuleNodeStatus(
             invalidUtf8,
             unchanged)
             .accepted);
    CHECK(
        !palace::parseStorageModuleNodeStatus(
             std::string(65537U, ' '),
             unchanged)
             .accepted);
}

void testNodeActionAcknowledgement()
{
    const std::string acceptedPayload =
        R"({"schema":"logos.managed_node_lifecycle.ack","version":1,"operation_id":"palace:lifecycle:1","accepted":true,"duplicate":false,"instance_id":"storage-instance-1","epoch":7,"sequence":10,"state":"starting","error":null})";
    palace::StorageNodeActionAcknowledgementV1 acknowledgement;
    CHECK(
        palace::parseStorageModuleNodeActionAcknowledgement(
            acceptedPayload,
            acknowledgement)
            .accepted);
    CHECK(acknowledgement.operationId == "palace:lifecycle:1");
    CHECK(acknowledgement.accepted);
    CHECK(!acknowledgement.duplicate);

    const std::string rejectedPayload =
        R"({"schema":"logos.managed_node_lifecycle.ack","version":1,"operation_id":"palace:lifecycle:1","accepted":false,"duplicate":false,"instance_id":"storage-instance-1","epoch":7,"sequence":10,"state":"stopped","error":{"code":"state_mismatch","message":"Snapshot stale.","occurred_at_ms":1000}})";
    CHECK(
        palace::parseStorageModuleNodeActionAcknowledgement(
            rejectedPayload,
            acknowledgement)
            .accepted);
    CHECK(!acknowledgement.accepted);
    CHECK(acknowledgement.errorCode == "state_mismatch");

    std::string confused = acceptedPayload;
    confused.replace(
        confused.find("null"),
        4U,
        R"({"code":"bad","message":"bad","occurred_at_ms":1})");
    CHECK(
        !palace::parseStorageModuleNodeActionAcknowledgement(
             confused,
             acknowledgement)
             .accepted);

    std::string missingError = rejectedPayload;
    const std::size_t errorStart = missingError.find(
        R"({"code":"state_mismatch")");
    missingError.replace(
        errorStart,
        missingError.size() - errorStart - 1U,
        "null");
    CHECK(
        !palace::parseStorageModuleNodeActionAcknowledgement(
             missingError,
             acknowledgement)
             .accepted);

    std::string nullOperation = acceptedPayload;
    const std::string operation =
        R"("operation_id":"palace:lifecycle:1")";
    nullOperation.replace(
        nullOperation.find(operation),
        operation.size(),
        R"("operation_id":null)");
    CHECK(
        !palace::parseStorageModuleNodeActionAcknowledgement(
             nullOperation,
             acknowledgement)
             .accepted);
}

void testNodeChanged()
{
    const std::string acceptedPayload =
        std::string{
            R"({"schema":"logos.managed_node_lifecycle.event","version":1,"instance_id":"storage-instance-1","epoch":7,"sequence":10,"scope":{"kind":"storage"},"operation_id":"palace:lifecycle:1","action":"start","phase":"accepted","outcome":"accepted","previous_state":"stopped","status":)"}
        + startingSnapshot(true)
        + R"(,"error":null,"emitted_at_ms":1000})";
    palace::StorageNodeChangedV1 event;
    CHECK(
        palace::parseStorageModuleNodeChanged(acceptedPayload, event)
            .accepted);
    CHECK(event.operationId == "palace:lifecycle:1");
    CHECK(event.status.pendingOperation.has_value());
    if (event.status.pendingOperation.has_value()) {
        CHECK(
            event.status.pendingOperation->operationId
            == event.operationId);
    }

    const std::string failedPayload =
        std::string{
            R"({"schema":"logos.managed_node_lifecycle.event","version":1,"instance_id":"storage-instance-1","epoch":7,"sequence":11,"scope":{"kind":"storage"},"operation_id":"palace:lifecycle:1","action":"start","phase":"settled","outcome":"failed","previous_state":"stopped","status":)"}
        + failedSnapshot()
        + R"(,"error":{"code":"start_failed","message":"Storage start failed.","occurred_at_ms":1001},"emitted_at_ms":1001})";
    CHECK(
        palace::parseStorageModuleNodeChanged(failedPayload, event)
            .accepted);
    CHECK(event.errorCode == "start_failed");
    CHECK(event.status.lastCompletedOperation.has_value());

    std::string mismatched = acceptedPayload;
    const std::size_t nested =
        mismatched.find(R"("operation_id":"palace:lifecycle:1")", 200U);
    mismatched.replace(
        nested,
        std::string{R"("operation_id":"palace:lifecycle:1")"}.size(),
        R"("operation_id":"other")");
    CHECK(
        !palace::parseStorageModuleNodeChanged(mismatched, event)
             .accepted);

    std::string confusedError = failedPayload;
    const std::string error =
        R"({"code":"start_failed","message":"Storage start failed.","occurred_at_ms":1001})";
    const std::size_t eventError = confusedError.rfind(error);
    confusedError.replace(eventError, error.size(), "null");
    CHECK(
        !palace::parseStorageModuleNodeChanged(confusedError, event)
             .accepted);

    const std::string legacyPayload =
        std::string{
            R"({"schema":"logos.managed_node_lifecycle.event","version":1,"instance_id":"storage-instance-1","epoch":7,"sequence":10,"scope":{"kind":"storage"},"operation_id":null,"action":"start","phase":"accepted","outcome":"accepted","previous_state":"stopped","status":)"}
        + startingSnapshot(false)
        + R"(,"error":null,"emitted_at_ms":1000})";
    CHECK(
        palace::parseStorageModuleNodeChanged(legacyPayload, event)
            .accepted);
    CHECK(event.operationId.empty());
}

void testTransferPayloads()
{
    const std::string contentId = kNativeBase58CidV1;
    palace::StorageUploadDoneV1 upload;
    CHECK(
        palace::parseStorageModuleUploadDone(
            std::string{R"({"success":true,"sessionId":"upload-1","cid":")"}
                + contentId + R"("})",
            upload)
            .accepted);
    CHECK(upload.succeeded);
    CHECK(upload.moduleSessionId == "upload-1");
    CHECK(upload.cid == contentId);

    CHECK(
        palace::parseStorageModuleUploadDone(
            R"({"success":false,"sessionId":"upload-2","error":"disk full"})",
            upload)
            .accepted);
    CHECK(!upload.succeeded);
    CHECK(upload.error == "disk full");
    CHECK(
        !palace::parseStorageModuleUploadDone(
             R"({"success":true,"sessionId":"upload-1","cid":"not-a-cid"})",
             upload)
             .accepted);
    CHECK(
        !palace::parseStorageModuleUploadDone(
             std::string{
                 R"({"success":true,"sessionId":"upload-1","cid":")"}
                 + "z1" + (kNativeBase58CidV1 + 1)
                 + R"("})",
             upload)
             .accepted);
    CHECK(
        !palace::parseStorageModuleUploadDone(
             std::string{
                 R"({"success":true,"sessionId":"upload-1","cid":")"}
                 + kOverlongVersionCidV1 + R"("})",
             upload)
             .accepted);
    CHECK(
        !palace::parseStorageModuleUploadDone(
             R"({"success":false,"sessionId":"upload-2","error":"","cid":"extra"})",
             upload)
             .accepted);

    palace::StorageDownloadAcknowledgementV2 acknowledgement;
    const std::string acknowledgementPayload =
        std::string{
            R"({"protocol":"logos.storage.download","version":2,"accepted":true,"moduleOperationId":"download-1","cid":")"}
        + contentId + R"("})";
    CHECK(
        palace::parseStorageModuleDownloadAcknowledgementV2(
            acknowledgementPayload,
            acknowledgement)
            .accepted);
    CHECK(acknowledgement.moduleOperationId == "download-1");
    CHECK(acknowledgement.cid == contentId);

    std::string nonnative = acknowledgementPayload;
    nonnative.replace(
        nonnative.find(R"("version":2)"),
        std::string{R"("version":2)"}.size(),
        R"("version":2.0)");
    CHECK(
        !palace::parseStorageModuleDownloadAcknowledgementV2(
             nonnative,
             acknowledgement)
             .accepted);

    palace::StorageDownloadDoneV2 terminal;
    const std::string succeeded =
        std::string{
            R"({"protocol":"logos.storage.download","version":2,"moduleOperationId":"download-1","cid":")"}
        + contentId + R"(","outcome":"succeeded"})";
    CHECK(
        palace::parseStorageModuleDownloadDoneV2(succeeded, terminal)
            .accepted);
    CHECK(
        terminal.outcome == palace::StorageTransferOutcome::Succeeded);

    const std::string failed =
        std::string{
            R"({"protocol":"logos.storage.download","version":2,"moduleOperationId":"download-1","cid":")"}
        + contentId + R"(","outcome":"failed","error":"network failed"})";
    CHECK(
        palace::parseStorageModuleDownloadDoneV2(failed, terminal)
            .accepted);
    CHECK(terminal.outcome == palace::StorageTransferOutcome::Failed);
    CHECK(terminal.error == "network failed");

    const std::string canceled =
        std::string{
            R"({"protocol":"logos.storage.download","version":2,"moduleOperationId":"download-1","cid":")"}
        + contentId + R"(","outcome":"canceled"})";
    CHECK(
        palace::parseStorageModuleDownloadDoneV2(canceled, terminal)
            .accepted);
    CHECK(terminal.outcome == palace::StorageTransferOutcome::Canceled);

    std::string extraError = canceled;
    extraError.insert(extraError.size() - 1U, R"(,"error":"wrong")");
    CHECK(
        !palace::parseStorageModuleDownloadDoneV2(
             extraError,
             terminal)
             .accepted);
    CHECK(
        !palace::parseStorageModuleDownloadDoneV2(
             std::string(16385U, ' '),
             terminal)
             .accepted);
}

void testMalformedCorpus()
{
    std::uint32_t state = 0x5a17c9e3U;
    for (std::size_t sample = 0U; sample < 4000U; ++sample) {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        const std::size_t size = state % 257U;
        std::string payload(size, '\0');
        for (char& byte : payload) {
            state ^= state << 13U;
            state ^= state >> 17U;
            state ^= state << 5U;
            byte = static_cast<char>(state & 0xffU);
        }

        palace::StorageNodeSnapshotV1 snapshot;
        palace::StorageNodeActionAcknowledgementV1 nodeAcknowledgement;
        palace::StorageNodeChangedV1 nodeEvent;
        palace::StorageUploadDoneV1 upload;
        palace::StorageDownloadAcknowledgementV2 downloadAcknowledgement;
        palace::StorageDownloadDoneV2 downloadEvent;
        static_cast<void>(
            palace::parseStorageModuleNodeStatus(payload, snapshot));
        static_cast<void>(
            palace::parseStorageModuleNodeActionAcknowledgement(
                payload,
                nodeAcknowledgement));
        static_cast<void>(
            palace::parseStorageModuleNodeChanged(payload, nodeEvent));
        static_cast<void>(
            palace::parseStorageModuleUploadDone(payload, upload));
        static_cast<void>(
            palace::parseStorageModuleDownloadAcknowledgementV2(
                payload,
                downloadAcknowledgement));
        static_cast<void>(
            palace::parseStorageModuleDownloadDoneV2(
                payload,
                downloadEvent));
    }

    palace::StorageNodeSnapshotV1 snapshot;
    CHECK(
        !palace::parseStorageModuleNodeStatus(
             R"({"a":{"b":{"c":{"d":{"e":{"f":{"g":{"h":{"i":null}}}}}}}}})",
             snapshot)
             .accepted);
}

} // namespace

int main()
{
    testNodeActionEncoding();
    testNodeStatus();
    testNodeActionAcknowledgement();
    testNodeChanged();
    testTransferPayloads();
    testMalformedCorpus();
    if (failures != 0) {
        std::cerr << failures << " storage module codec checks failed\n";
        return 1;
    }
    std::cout << "storage module codec checks passed\n";
    return 0;
}
