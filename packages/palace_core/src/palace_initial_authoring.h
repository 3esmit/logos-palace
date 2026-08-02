#pragma once

#include <string>

#include "palace_lez.h"

namespace palace {

// Inputs are already owned by Palace Core: local delivery identity plus the
// finalized, user-authored Storage graph. This component deliberately has no
// filesystem, UI, wallet, or transport dependency.
struct PalaceInitialAuthoringInputV1 {
    std::string title;
    std::string ownerAccountIdHex;
    std::string ownerDisplayName;
    std::string ownerDeliveryKeyHex;
    std::uint64_t ownerDeliveryKeyEpoch = 0U;
    std::string palaceManifestCid;
    std::string atriumManifestCid;
    std::string loungeManifestCid;
    std::string doorScriptCid;
};

struct PalaceInitialAuthoringResultV1 {
    bool accepted = false;
    std::string reason;
    PalaceLezInstructionV3 instruction;
};

struct PalaceInitialRoomStateResultV1 {
    bool accepted = false;
    std::string reason;
    PalaceLezInstructionV3 instruction;
};

// Builds the sole initial Palace transition from persisted authoring state.
// Identifiers are deterministically derived from that input so a retry has the
// same intent; no UI-supplied authority or arbitrary raw transition is used.
PalaceInitialAuthoringResultV1 buildPalaceInitialAuthoringV1(
    const PalaceInitialAuthoringInputV1& input);

// Builds the first durable interaction state once the initial Palace action
// has materialized. The root and entry room are LEZ authority records; the UI
// supplies no action identifiers, account ids, or state bytes.
PalaceInitialRoomStateResultV1 buildPalaceInitialRoomStateV1(
    const PalaceLezRootRecordV3& root,
    const PalaceLezRoomRecordV3& entryRoom,
    std::uint64_t orderedActionId);

} // namespace palace
