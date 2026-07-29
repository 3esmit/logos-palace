#pragma once

#include "palace_storage_module_session.h"

#include <string>

namespace palace {

struct StorageModuleCodecResult {
    bool accepted = false;
    std::string reason;
};

struct StorageModuleEncodedCommand {
    bool accepted = false;
    std::string reason;
    std::string payload;
};

StorageModuleEncodedCommand encodeStorageModuleNodeAction(
    const StorageModuleCommand& command);

StorageModuleCodecResult parseStorageModuleNodeStatus(
    const std::string& payload,
    StorageNodeSnapshotV1& snapshot);

StorageModuleCodecResult parseStorageModuleNodeActionAcknowledgement(
    const std::string& payload,
    StorageNodeActionAcknowledgementV1& acknowledgement);

StorageModuleCodecResult parseStorageModuleNodeChanged(
    const std::string& payload,
    StorageNodeChangedV1& event);

StorageModuleCodecResult parseStorageModuleUploadDone(
    const std::string& payload,
    StorageUploadDoneV1& event);

StorageModuleCodecResult parseStorageModuleDownloadAcknowledgementV2(
    const std::string& payload,
    StorageDownloadAcknowledgementV2& acknowledgement);

StorageModuleCodecResult parseStorageModuleDownloadDoneV2(
    const std::string& payload,
    StorageDownloadDoneV2& event);

} // namespace palace
