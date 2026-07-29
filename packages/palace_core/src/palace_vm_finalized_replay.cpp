#include "palace_vm_finalized_replay.h"

#include "palace_sha256.h"
#include "palace_storage.h"

#include <algorithm>
#include <charconv>
#include <string>
#include <variant>

namespace palace {
namespace {

constexpr std::uint32_t kWriteSharedState = 1U << 3U;

bool isNonzeroLowerHex64(const std::string& value)
{
    return value.size() == 64U
        && value != std::string(64U, '0')
        && std::all_of(
            value.begin(),
            value.end(),
            [](const unsigned char character) {
                return (character >= '0' && character <= '9')
                    || (character >= 'a' && character <= 'f');
            });
}

bool parseCanonicalU64(
    const std::string& value,
    std::uint64_t& parsed)
{
    if (value.empty() || value.size() > 20U
        || (value.size() > 1U && value.front() == '0')) {
        return false;
    }
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    return result.ec == std::errc()
        && result.ptr == value.data() + value.size()
        && value == std::to_string(parsed);
}

bool sameTransactionPlan(
    const PalaceLezTransactionPlanV3& first,
    const PalaceLezTransactionPlanV3& second)
{
    return first.accepted && second.accepted
        && first.programIdHex == second.programIdHex
        && first.rootAccountIdHex == second.rootAccountIdHex
        && first.accountIdsHex == second.accountIdsHex
        && first.signingRequirements == second.signingRequirements
        && first.instructionWords == second.instructionWords;
}

PalaceVmFinalizedReplayPlanV1 rejected(
    const std::string& reason)
{
    PalaceVmFinalizedReplayPlanV1 result;
    result.reason = reason;
    return result;
}

} // namespace

PalaceVmFinalizedReplayPlanV1
buildPalaceVmFinalizedReplayPlanV1(
    const PalaceVmFinalizedReplayInputV1& input)
{
    static const std::string kScript =
        "ON SELECT door\n"
        "SET door_open 1\n"
        "GOTOROOM lounge\n";
    static const std::string kResultingState =
        "door_open=1";
    static const std::string kPriorState =
        "door_open=0";
    static const std::string kExpectedStateRoot =
        crypto::sha256Hex(kResultingState);
    static const std::string kPriorStateRoot =
        crypto::sha256Hex(kPriorState);

    std::uint64_t actionId = 0U;
    std::uint64_t roomEpoch = 0U;
    if (!input.authorityVerified
        || !input.identityVerified
        || input.actionId != "10"
        || !parseCanonicalU64(input.actionId, actionId)
        || input.authorityCheckpointActionId != actionId) {
        return rejected("finalized-replay-authority");
    }
    if (input.journalStatus.durableStage
            != DurableActionStage::Finalized
        || !isNonzeroLowerHex64(
            input.journalStatus.transactionHash)) {
        return rejected("finalized-replay-journal");
    }
    if (input.priorSharedJournalStatus.durableStage
            != DurableActionStage::Finalized
        || !isNonzeroLowerHex64(
            input.priorSharedJournalStatus
                .transactionHash)) {
        return rejected("finalized-replay-prior-journal");
    }

    const PalaceLezTrackedTransaction* tracked = nullptr;
    std::size_t actionMatches = 0U;
    for (const PalaceLezTrackedTransaction& candidate
         : input.trackedTransactions) {
        if (candidate.orderedActionId != actionId
            || candidate.plan.rootAccountIdHex
                != input.content.rootAccountIdHex) {
            continue;
        }
        ++actionMatches;
        if (candidate.transactionHash
            == input.journalStatus.transactionHash) {
            tracked = &candidate;
        }
    }
    if (actionMatches != 1U || tracked == nullptr
        || tracked->stage
            != PalaceLezTransactionStage::Finalized
        || !isNonzeroLowerHex64(tracked->transactionHash)
        || !isNonzeroLowerHex64(
            tracked->expectedRootDataSha256Hex)
        || !tracked->plan.accepted) {
        return rejected("finalized-replay-coordinator");
    }

    const auto* update =
        std::get_if<PalaceLezUpdateSharedStateV3>(
            &tracked->plan.instruction.payload);
    if (update == nullptr
        || tracked->orderedActionId != actionId
        || update->orderedActionId != actionId
        || tracked->plan.accountIdsHex.size() != 5U
        || tracked->plan.accountIdsHex[1]
            != input.callerAccountIdHex) {
        return rejected("finalized-replay-plan");
    }
    const PalaceLezTransactionPlanV3 rebuilt =
        PalaceLezCodec::buildTransaction(
            tracked->plan.programIdHex,
            input.callerAccountIdHex,
            tracked->plan.instruction);
    if (!sameTransactionPlan(rebuilt, tracked->plan)) {
        return rejected("finalized-replay-plan");
    }

    const std::string grantIdHex =
        PalaceLezCodec::bytes32Hex(update->grantId);
    const std::string sharedStateIdHex =
        PalaceLezCodec::bytes32Hex(update->sharedStateId);
    const std::string roomIdHex =
        PalaceLezCodec::bytes32Hex(update->roomId);
    const std::string stateRootHex =
        PalaceLezCodec::bytes32Hex(update->stateRoot);
    if (update->stateRevision != 3U
        || std::string(
               update->value.begin(), update->value.end())
            != "1"
        || stateRootHex != kExpectedStateRoot) {
        return rejected("finalized-replay-plan");
    }

    const PalaceLezTrackedTransaction* prior = nullptr;
    std::size_t priorMatches = 0U;
    for (const PalaceLezTrackedTransaction& candidate
         : input.trackedTransactions) {
        if (candidate.orderedActionId != 7U
            || candidate.plan.rootAccountIdHex
                != tracked->plan.rootAccountIdHex) {
            continue;
        }
        ++priorMatches;
        if (candidate.transactionHash
            == input.priorSharedJournalStatus
                   .transactionHash) {
            prior = &candidate;
        }
    }
    const auto* priorUpdate =
        prior != nullptr
        ? std::get_if<PalaceLezUpdateSharedStateV3>(
              &prior->plan.instruction.payload)
        : nullptr;
    if (priorMatches != 1U || prior == nullptr
        || prior->stage
            != PalaceLezTransactionStage::Finalized
        || !isNonzeroLowerHex64(prior->transactionHash)
        || !isNonzeroLowerHex64(
            prior->expectedRootDataSha256Hex)
        || !prior->plan.accepted
        || priorUpdate == nullptr
        || priorUpdate->orderedActionId != 7U
        || priorUpdate->grantId != update->grantId
        || priorUpdate->sharedStateId
            != update->sharedStateId
        || priorUpdate->roomId != update->roomId
        || priorUpdate->stateRevision != 2U
        || std::string(
               priorUpdate->value.begin(),
               priorUpdate->value.end())
            != "0"
        || PalaceLezCodec::bytes32Hex(
               priorUpdate->stateRoot)
            != kPriorStateRoot
        || prior->plan.programIdHex
            != tracked->plan.programIdHex
        || prior->plan.rootAccountIdHex
            != tracked->plan.rootAccountIdHex
        || prior->plan.accountIdsHex.size() != 5U
        || prior->plan.accountIdsHex[1]
            != input.callerAccountIdHex) {
        return rejected("finalized-replay-prior-plan");
    }
    const PalaceLezTransactionPlanV3 rebuiltPrior =
        PalaceLezCodec::buildTransaction(
            prior->plan.programIdHex,
            input.callerAccountIdHex,
            prior->plan.instruction);
    if (!sameTransactionPlan(rebuiltPrior, prior->plan)) {
        return rejected("finalized-replay-prior-plan");
    }

    const PalaceVmFinalizedReplayContentV1& content =
        input.content;
    if (!content.verified
        || !isNonzeroLowerHex64(content.palaceIdHex)
        || !isNonzeroLowerHex64(content.ownerAccountIdHex)
        || !isNonzeroLowerHex64(content.programIdHex)
        || !isNonzeroLowerHex64(content.rootAccountIdHex)
        || !isNonzeroLowerHex64(
            content.rootDataSha256Hex)
        || !isNonzeroLowerHex64(content.atriumRoomIdHex)
        || !isNonzeroLowerHex64(content.loungeRoomIdHex)
        || content.atriumRoomIdHex == content.loungeRoomIdHex
        || content.programIdHex
            != tracked->plan.programIdHex
        || content.rootAccountIdHex
            != tracked->plan.rootAccountIdHex
        || content.rootDataSha256Hex
            != tracked->expectedRootDataSha256Hex
        || content.atriumRoomIdHex != roomIdHex
        || content.script != kScript
        || !isSafePalaceCid(content.scriptBundleCid)
        || content.atriumLocked || content.loungeLocked
        || !parseCanonicalU64(content.roomEpoch, roomEpoch)) {
        return rejected("finalized-replay-content");
    }

    const PalaceVmFinalizedReplaySharedStateV1& shared =
        input.shared;
    if (!shared.verified
        || shared.palaceIdHex != content.palaceIdHex
        || shared.sharedStateIdHex != sharedStateIdHex
        || shared.roomIdHex != roomIdHex
        || shared.key != "door_open"
        || shared.value != "1"
        || shared.stateRootHex != kExpectedStateRoot
        || shared.revision != update->stateRevision
        || shared.lastOrderedActionId != actionId) {
        return rejected("finalized-replay-shared");
    }

    const PalaceVmFinalizedReplayGrantV1& grant =
        input.grant;
    if (!grant.verified
        || grant.palaceIdHex != content.palaceIdHex
        || grant.grantIdHex != grantIdHex
        || grant.subjectUserIdHex
            != input.callerAccountIdHex
        || grant.issuedByHex != content.ownerAccountIdHex
        || !grant.allowsAtrium
        || (grant.capabilities & kWriteSharedState)
            != kWriteSharedState
        || grant.validThroughActionId < actionId
        || grant.revoked) {
        return rejected("finalized-replay-grant");
    }

    PalaceVmFinalizedReplayPlanV1 result;
    result.accepted = true;
    result.reason = "accepted";
    result.actionId = input.actionId;
    result.palaceIdHex = content.palaceIdHex;
    result.rootAccountIdHex = content.rootAccountIdHex;
    result.callerAccountIdHex = input.callerAccountIdHex;
    result.programIdHex = content.programIdHex;
    result.grantIdHex = grantIdHex;
    result.sharedStateIdHex = sharedStateIdHex;
    result.roomIdHex = roomIdHex;
    result.script = content.script;
    result.scriptBundleCid = content.scriptBundleCid;
    result.roomEpoch = content.roomEpoch;
    result.trigger = "SELECT:door";
    result.priorState = kPriorState;
    result.allowedRooms = "atrium,lounge";
    result.resultingState = kResultingState;
    result.expectedStateRootHex = kExpectedStateRoot;
    result.navigateRoom = "lounge";
    result.stateRevision = update->stateRevision;
    result.instructionBudget = 8U;
    result.roomLocked = false;
    result.canMutateSharedState = true;
    return result;
}

bool samePalaceVmFinalizedReplayPlanV1(
    const PalaceVmFinalizedReplayPlanV1& first,
    const PalaceVmFinalizedReplayPlanV1& second)
{
    return first.accepted && second.accepted
        && first.actionId == second.actionId
        && first.palaceIdHex == second.palaceIdHex
        && first.rootAccountIdHex == second.rootAccountIdHex
        && first.callerAccountIdHex == second.callerAccountIdHex
        && first.programIdHex == second.programIdHex
        && first.grantIdHex == second.grantIdHex
        && first.sharedStateIdHex == second.sharedStateIdHex
        && first.roomIdHex == second.roomIdHex
        && first.script == second.script
        && first.scriptBundleCid == second.scriptBundleCid
        && first.roomEpoch == second.roomEpoch
        && first.trigger == second.trigger
        && first.priorState == second.priorState
        && first.allowedRooms == second.allowedRooms
        && first.resultingState == second.resultingState
        && first.expectedStateRootHex
            == second.expectedStateRootHex
        && first.navigateRoom == second.navigateRoom
        && first.stateRevision == second.stateRevision
        && first.instructionBudget == second.instructionBudget
        && first.roomLocked == second.roomLocked
        && first.canMutateSharedState
            == second.canMutateSharedState;
}

bool palaceVmFinalizedNavigationMatchesProjectionV1(
    bool navigationApplied,
    const std::string& currentRoom,
    const std::string& targetRoom)
{
    return navigationApplied
        && !targetRoom.empty()
        && currentRoom == targetRoom;
}

PalaceVmFinalizedNavigationResultV1
coordinatePalaceVmFinalizedNavigationV1(
    const bool navigationApplied,
    const std::string& currentRoom,
    const std::string& targetRoom,
    const bool coldReplay,
    const std::function<
        std::string(const std::string&)>& enterRoom)
{
    if (!coldReplay
        && palaceVmFinalizedNavigationMatchesProjectionV1(
            navigationApplied, currentRoom, targetRoom)) {
        return {true, false, "promoted"};
    }
    if (!enterRoom) {
        return {
            false,
            false,
            coldReplay
                ? "finalized-navigation-persistence-failed"
                : "finalized-navigation-failed",
        };
    }
    const std::string entered = enterRoom(targetRoom);
    if (entered.rfind("ok;", 0U) != 0U) {
        return {
            false,
            true,
            coldReplay
                ? "finalized-navigation-persistence-failed"
                : "finalized-navigation-failed",
        };
    }
    return {true, true, "navigated"};
}

} // namespace palace
