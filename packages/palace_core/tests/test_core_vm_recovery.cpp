#include <logos_test.h>

#include "palace_core_impl.h"
#include "palace_vm_finalized_replay.h"

#include <string>
#include <vector>

namespace {

constexpr const char* kPromotedStatus =
    "status=promoted;action=51;"
    "turn_input_digest="
    "8bdae8f6d7703fa5807dce60b26ede042e3f0c133efd8fc7db502abfc3d0134e;"
    "provisional_receipt_sha256="
    "ae56865804a35b96ded001555948b3b3dd56026faf0342006521d6086e633893;"
    "finalized_receipt_sha256="
    "97c60589c7c94fcf8d340cc893bb515e4e61b04fdfe869397b2f193a1ef08b27";

palace::core_detail::PalaceVmPromotionRecoveryInputV1
recoveryInput()
{
    return {
        "51",
        "ON SELECT door\n"
        "SET door_open 1\n"
        "GOTOROOM lounge\n",
        "bafytrackedscript",
        "7",
        "SELECT:door",
        "door_open=0",
        "atrium,lounge",
        "accepted=1;local=;shared=15:set:door_open=1;"
        "deferred=30:await-finality:navigate:lounge;"
        "rejected=;state=door_open=1;"
        "state_root="
        "c997f3a62bbf251a6a0f00d064120226e01b7e24a2244805ad49c7ed0b5f7cc3;"
        "receipt="
        "1d769ab7f027efd72032b1c09995323e177723c56e1bfbc90145e492829fe3ff",
        "accepted=1;local=15:navigate:lounge;"
        "shared=15:set:door_open=1;deferred=;"
        "rejected=;state=door_open=1;"
        "state_root="
        "c997f3a62bbf251a6a0f00d064120226e01b7e24a2244805ad49c7ed0b5f7cc3;"
        "receipt="
        "bd58bfe194673509fd5736d33b9d974ccba413139aace43e7ac34e2957b76b86",
        8U,
        false,
        true,
    };
}

palace::PalaceVmFinalizedReplayInputV1 finalizedReplayInput()
{
    const std::string programIdHex(64U, '1');
    const std::string callerAccountIdHex(64U, '2');
    palace::PalaceLezUpdateSharedStateV3 update;
    update.orderedActionId = 10U;
    palace::PalaceLezCodec::parseBytes32Hex(
        std::string(64U, '3'), update.grantId);
    palace::PalaceLezCodec::parseBytes32Hex(
        std::string(64U, '4'), update.sharedStateId);
    palace::PalaceLezCodec::parseBytes32Hex(
        std::string(64U, '5'), update.roomId);
    update.stateRevision = 3U;
    update.value = {'1'};
    palace::PalaceLezCodec::parseBytes32Hex(
        palace::crypto::sha256Hex("door_open=1"),
        update.stateRoot);

    palace::PalaceLezInstructionV3 instruction;
    instruction.payload = update;
    const palace::PalaceLezTransactionPlanV3 plan =
        palace::PalaceLezCodec::buildTransaction(
            programIdHex, callerAccountIdHex, instruction);

    palace::PalaceLezTrackedTransaction tracked;
    tracked.stage =
        palace::PalaceLezTransactionStage::Finalized;
    tracked.transactionHash = std::string(64U, '9');
    tracked.orderedActionId = 10U;
    tracked.observedBlockHeight = 42U;
    tracked.expectedRootDataSha256Hex =
        std::string(64U, 'a');
    tracked.plan = plan;

    palace::PalaceLezUpdateSharedStateV3 priorUpdate =
        update;
    priorUpdate.orderedActionId = 7U;
    priorUpdate.stateRevision = 2U;
    priorUpdate.value = {'0'};
    palace::PalaceLezCodec::parseBytes32Hex(
        palace::crypto::sha256Hex("door_open=0"),
        priorUpdate.stateRoot);
    palace::PalaceLezInstructionV3 priorInstruction;
    priorInstruction.payload = priorUpdate;
    palace::PalaceLezTrackedTransaction prior;
    prior.stage =
        palace::PalaceLezTransactionStage::Finalized;
    prior.transactionHash = std::string(64U, '8');
    prior.orderedActionId = 7U;
    prior.observedBlockHeight = 39U;
    prior.expectedRootDataSha256Hex =
        std::string(64U, 'b');
    prior.plan = palace::PalaceLezCodec::buildTransaction(
        programIdHex,
        callerAccountIdHex,
        priorInstruction);

    palace::PalaceVmFinalizedReplayInputV1 input;
    input.authorityVerified = true;
    input.identityVerified = true;
    input.actionId = "10";
    input.authorityCheckpointActionId = 10U;
    input.callerAccountIdHex = callerAccountIdHex;
    input.journalStatus.durableStage =
        palace::DurableActionStage::Finalized;
    input.journalStatus.transactionHash =
        tracked.transactionHash;
    input.priorSharedJournalStatus.durableStage =
        palace::DurableActionStage::Finalized;
    input.priorSharedJournalStatus.transactionHash =
        prior.transactionHash;
    input.trackedTransactions = {prior, tracked};
    input.content = {
        true,
        std::string(64U, '7'),
        std::string(64U, '8'),
        programIdHex,
        plan.rootAccountIdHex,
        tracked.expectedRootDataSha256Hex,
        "ON SELECT door\n"
        "SET door_open 1\n"
        "GOTOROOM lounge\n",
        "bafytrackedscript",
        std::string(64U, '5'),
        std::string(64U, '6'),
        "1",
        false,
        false,
    };
    input.shared = {
        true,
        input.content.palaceIdHex,
        std::string(64U, '4'),
        std::string(64U, '5'),
        "door_open",
        "1",
        palace::crypto::sha256Hex("door_open=1"),
        3U,
        10U,
    };
    input.grant = {
        true,
        input.content.palaceIdHex,
        std::string(64U, '3'),
        callerAccountIdHex,
        input.content.ownerAccountIdHex,
        true,
        1U << 3U,
        10U,
        false,
    };
    return input;
}

} // namespace

LOGOS_TEST(core_vm_recovers_exact_promoted_receipt_after_crash) {
    const auto input = recoveryInput();
    const auto recovered =
        palace::core_detail::
            recoverPromotedPalaceVmReceiptV1(
                kPromotedStatus, input);
    LOGOS_ASSERT_TRUE(recovered.accepted);
    LOGOS_ASSERT_EQ(
        recovered.reason,
        std::string("promotion-recovered"));
    LOGOS_ASSERT_EQ(
        recovered.finalizedReceipt,
        input.expectedFinalizedReceipt);
}

LOGOS_TEST(core_vm_restart_repairs_navigation_flag_projection_divergence) {
    LOGOS_ASSERT_TRUE(
        palace::palaceVmFinalizedNavigationMatchesProjectionV1(
            true, "lounge", "lounge"));
    LOGOS_ASSERT_FALSE(
        palace::palaceVmFinalizedNavigationMatchesProjectionV1(
            true, "atrium", "lounge"));
    LOGOS_ASSERT_FALSE(
        palace::palaceVmFinalizedNavigationMatchesProjectionV1(
            false, "lounge", "lounge"));
}

LOGOS_TEST(core_vm_cold_replay_uses_configured_room_entry_path) {
    bool deliveryConfigured = true;
    bool roomEntryCalled = false;
    std::string enteredRoom;
    const palace::PalaceVmFinalizedNavigationResultV1
        coordinated =
            palace::coordinatePalaceVmFinalizedNavigationV1(
                true,
                "lounge",
                "lounge",
                true,
                [&deliveryConfigured,
                 &roomEntryCalled,
                 &enteredRoom](
                    const std::string& roomId) {
                    roomEntryCalled =
                        deliveryConfigured;
                    enteredRoom = roomId;
                    return std::string(
                        "ok;room=lounge");
                });
    LOGOS_ASSERT_TRUE(coordinated.accepted);
    LOGOS_ASSERT_TRUE(
        coordinated.roomEntryAttempted);
    LOGOS_ASSERT_TRUE(roomEntryCalled);
    LOGOS_ASSERT_EQ(
        enteredRoom, std::string("lounge"));

    const palace::PalaceVmFinalizedNavigationResultV1
        failed =
            palace::coordinatePalaceVmFinalizedNavigationV1(
                false,
                "atrium",
                "lounge",
                true,
                [](const std::string&) {
                    return std::string(
                        "rejected=room-transition-recovery");
                });
    LOGOS_ASSERT_FALSE(failed.accepted);
    LOGOS_ASSERT_EQ(
        failed.reason,
        std::string(
            "finalized-navigation-persistence-failed"));
}

LOGOS_TEST(core_vm_recovery_rejects_any_promoted_evidence_mismatch) {
    const auto exact = recoveryInput();

    auto wrongAction = exact;
    wrongAction.actionId = "52";
    LOGOS_ASSERT_FALSE(
        palace::core_detail::
            recoverPromotedPalaceVmReceiptV1(
                kPromotedStatus, wrongAction).accepted);

    auto wrongInput = exact;
    wrongInput.script += "SAY changed\n";
    LOGOS_ASSERT_FALSE(
        palace::core_detail::
            recoverPromotedPalaceVmReceiptV1(
                kPromotedStatus, wrongInput).accepted);

    auto wrongProvisional = exact;
    wrongProvisional.provisionalReceipt += "x";
    LOGOS_ASSERT_FALSE(
        palace::core_detail::
            recoverPromotedPalaceVmReceiptV1(
                kPromotedStatus, wrongProvisional).accepted);

    auto wrongFinalized = exact;
    wrongFinalized.expectedFinalizedReceipt += "x";
    LOGOS_ASSERT_FALSE(
        palace::core_detail::
            recoverPromotedPalaceVmReceiptV1(
                kPromotedStatus, wrongFinalized).accepted);

    LOGOS_ASSERT_FALSE(
        palace::core_detail::
            recoverPromotedPalaceVmReceiptV1(
                std::string(kPromotedStatus) + ";extra=1",
                exact).accepted);
}

LOGOS_TEST(core_vm_finalized_replay_builds_one_exact_idempotent_plan) {
    const palace::PalaceVmFinalizedReplayInputV1 input =
        finalizedReplayInput();
    const palace::PalaceVmFinalizedReplayPlanV1 first =
        palace::buildPalaceVmFinalizedReplayPlanV1(input);
    const palace::PalaceVmFinalizedReplayPlanV1 repeated =
        palace::buildPalaceVmFinalizedReplayPlanV1(input);

    LOGOS_ASSERT_TRUE(first.accepted);
    LOGOS_ASSERT_EQ(first.reason, std::string("accepted"));
    LOGOS_ASSERT_EQ(first.actionId, std::string("10"));
    LOGOS_ASSERT_EQ(
        first.callerAccountIdHex,
        input.callerAccountIdHex);
    LOGOS_ASSERT_EQ(
        first.sharedStateIdHex,
        input.shared.sharedStateIdHex);
    LOGOS_ASSERT_EQ(
        first.expectedStateRootHex,
        palace::crypto::sha256Hex("door_open=1"));
    LOGOS_ASSERT_EQ(
        first.navigateRoom, std::string("lounge"));
    LOGOS_ASSERT_TRUE(
        palace::samePalaceVmFinalizedReplayPlanV1(
            first, repeated));
}

LOGOS_TEST(core_vm_finalized_replay_fails_closed_without_exact_evidence) {
    const auto rejectedReason =
        [](const palace::PalaceVmFinalizedReplayInputV1& input) {
            return palace::buildPalaceVmFinalizedReplayPlanV1(
                       input)
                .reason;
        };

    auto missingPlan = finalizedReplayInput();
    missingPlan.trackedTransactions.clear();
    LOGOS_ASSERT_EQ(
        rejectedReason(missingPlan),
        std::string("finalized-replay-coordinator"));

    auto wrongPlan = finalizedReplayInput();
    wrongPlan.trackedTransactions.back()
        .plan.accountIdsHex.back() = std::string(64U, 'b');
    LOGOS_ASSERT_EQ(
        rejectedReason(wrongPlan),
        std::string("finalized-replay-plan"));

    auto wrongShared = finalizedReplayInput();
    wrongShared.shared.value = "0";
    LOGOS_ASSERT_EQ(
        rejectedReason(wrongShared),
        std::string("finalized-replay-shared"));

    auto missingShared = finalizedReplayInput();
    missingShared.shared.verified = false;
    LOGOS_ASSERT_EQ(
        rejectedReason(missingShared),
        std::string("finalized-replay-shared"));

    auto missingContent = finalizedReplayInput();
    missingContent.content.verified = false;
    LOGOS_ASSERT_EQ(
        rejectedReason(missingContent),
        std::string("finalized-replay-content"));

    auto wrongContent = finalizedReplayInput();
    wrongContent.content.script += "SAY changed\n";
    LOGOS_ASSERT_EQ(
        rejectedReason(wrongContent),
        std::string("finalized-replay-content"));

    auto floatingRoot = finalizedReplayInput();
    floatingRoot.content.rootDataSha256Hex =
        std::string(64U, 'c');
    LOGOS_ASSERT_EQ(
        rejectedReason(floatingRoot),
        std::string("finalized-replay-content"));

    auto missingFinality = finalizedReplayInput();
    missingFinality.journalStatus.durableStage =
        palace::DurableActionStage::Observed;
    LOGOS_ASSERT_EQ(
        rejectedReason(missingFinality),
        std::string("finalized-replay-journal"));

    auto wrongFinality = finalizedReplayInput();
    wrongFinality.trackedTransactions.back().stage =
        palace::PalaceLezTransactionStage::Observed;
    LOGOS_ASSERT_EQ(
        rejectedReason(wrongFinality),
        std::string("finalized-replay-coordinator"));

    auto missingPriorFinality = finalizedReplayInput();
    missingPriorFinality.priorSharedJournalStatus.durableStage =
        palace::DurableActionStage::Observed;
    LOGOS_ASSERT_EQ(
        rejectedReason(missingPriorFinality),
        std::string("finalized-replay-prior-journal"));

    auto wrongPriorValue = finalizedReplayInput();
    std::get<palace::PalaceLezUpdateSharedStateV3>(
        wrongPriorValue.trackedTransactions.front()
            .plan.instruction.payload)
        .value = {'1'};
    LOGOS_ASSERT_EQ(
        rejectedReason(wrongPriorValue),
        std::string("finalized-replay-prior-plan"));

    auto wrongPriorRoot = finalizedReplayInput();
    std::get<palace::PalaceLezUpdateSharedStateV3>(
        wrongPriorRoot.trackedTransactions.front()
            .plan.instruction.payload)
        .stateRoot.front() ^= 1U;
    LOGOS_ASSERT_EQ(
        rejectedReason(wrongPriorRoot),
        std::string("finalized-replay-prior-plan"));

    auto wrongPriorCaller = finalizedReplayInput();
    wrongPriorCaller.trackedTransactions.front()
        .plan.accountIdsHex[1] = std::string(64U, 'd');
    LOGOS_ASSERT_EQ(
        rejectedReason(wrongPriorCaller),
        std::string("finalized-replay-prior-plan"));

    auto duplicatePrior = finalizedReplayInput();
    duplicatePrior.trackedTransactions.push_back(
        duplicatePrior.trackedTransactions.front());
    LOGOS_ASSERT_EQ(
        rejectedReason(duplicatePrior),
        std::string("finalized-replay-prior-plan"));

    auto missingAuthority = finalizedReplayInput();
    missingAuthority.authorityVerified = false;
    LOGOS_ASSERT_EQ(
        rejectedReason(missingAuthority),
        std::string("finalized-replay-authority"));

    auto missingGrant = finalizedReplayInput();
    missingGrant.grant.verified = false;
    LOGOS_ASSERT_EQ(
        rejectedReason(missingGrant),
        std::string("finalized-replay-grant"));
}
