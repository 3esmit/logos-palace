#pragma once

#include "palace_action_journal.h"
#include "palace_lez.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace palace {

struct PalaceVmFinalizedReplayContentV1 {
    bool verified = false;
    std::string palaceIdHex;
    std::string ownerAccountIdHex;
    std::string programIdHex;
    std::string rootAccountIdHex;
    std::string rootDataSha256Hex;
    std::string script;
    std::string scriptBundleCid;
    std::string atriumRoomIdHex;
    std::string loungeRoomIdHex;
    std::string roomEpoch;
    bool atriumLocked = false;
    bool loungeLocked = false;
};

struct PalaceVmFinalizedReplaySharedStateV1 {
    bool verified = false;
    std::string palaceIdHex;
    std::string sharedStateIdHex;
    std::string roomIdHex;
    std::string key;
    std::string value;
    std::string stateRootHex;
    std::uint64_t revision = 0U;
    std::uint64_t lastOrderedActionId = 0U;
};

struct PalaceVmFinalizedReplayGrantV1 {
    bool verified = false;
    std::string palaceIdHex;
    std::string grantIdHex;
    std::string subjectUserIdHex;
    std::string issuedByHex;
    bool allowsAtrium = false;
    std::uint32_t capabilities = 0U;
    std::uint64_t validThroughActionId = 0U;
    bool revoked = false;
};

struct PalaceVmFinalizedReplayInputV1 {
    bool authorityVerified = false;
    bool identityVerified = false;
    std::string actionId;
    std::uint64_t authorityCheckpointActionId = 0U;
    std::string callerAccountIdHex;
    ActionStatus journalStatus;
    ActionStatus priorSharedJournalStatus;
    std::vector<PalaceLezTrackedTransaction> trackedTransactions;
    PalaceVmFinalizedReplayContentV1 content;
    PalaceVmFinalizedReplaySharedStateV1 shared;
    PalaceVmFinalizedReplayGrantV1 grant;
};

struct PalaceVmFinalizedReplayPlanV1 {
    bool accepted = false;
    std::string reason;
    std::string actionId;
    std::string palaceIdHex;
    std::string rootAccountIdHex;
    std::string callerAccountIdHex;
    std::string programIdHex;
    std::string grantIdHex;
    std::string sharedStateIdHex;
    std::string roomIdHex;
    std::string script;
    std::string scriptBundleCid;
    std::string roomEpoch;
    std::string trigger;
    std::string priorState;
    std::string allowedRooms;
    std::string resultingState;
    std::string expectedStateRootHex;
    std::string navigateRoom;
    std::uint64_t stateRevision = 0U;
    std::uint64_t instructionBudget = 0U;
    bool roomLocked = false;
    bool canMutateSharedState = false;
};

// Reconstructs one pinned room-transition VM turn from already-finalized durable
// evidence. It never queues or submits an action.
PalaceVmFinalizedReplayPlanV1
buildPalaceVmFinalizedReplayPlanV1(
    const PalaceVmFinalizedReplayInputV1& input);

bool samePalaceVmFinalizedReplayPlanV1(
    const PalaceVmFinalizedReplayPlanV1& first,
    const PalaceVmFinalizedReplayPlanV1& second);

// A persisted VM flag is complete only while the restored durable projection
// still names the finalized target room.
bool palaceVmFinalizedNavigationMatchesProjectionV1(
    bool navigationApplied,
    const std::string& currentRoom,
    const std::string& targetRoom);

struct PalaceVmFinalizedNavigationResultV1 {
    bool accepted = false;
    bool roomEntryAttempted = false;
    std::string reason;
};

// Both live promotion and cold replay use the owning Palace Core room-entry
// path. That path selects projection-only or paired Delivery/projection
// durability from the configuration it owns under its mutex. Cold replay
// always re-enters because its projection flag cannot prove a configured
// Delivery session names the same room.
PalaceVmFinalizedNavigationResultV1
coordinatePalaceVmFinalizedNavigationV1(
    bool navigationApplied,
    const std::string& currentRoom,
    const std::string& targetRoom,
    bool coldReplay,
    const std::function<
        std::string(const std::string&)>& enterRoom);

} // namespace palace
