#include <logos_test.h>

#include "palace_vm_engine.h"

namespace {

palace::VmContext selectDoorContext()
{
    palace::VmContext context;
    context.profileId = "classic-mvp-v1";
    context.scriptBundleCid = "bundle-1";
    context.roomEpoch = "7";
    context.trigger = "SELECT:door";
    context.priorState = {{"door_open", 0}};
    context.allowedRooms = {"atrium", "lounge"};
    context.roomLocked = false;
    context.canMutateSharedState = true;
    context.instructionBudget = 8;
    return context;
}

} // namespace

LOGOS_TEST(identical_turns_produce_identical_receipts) {
    const std::string script = "ON SELECT door\nSAY Welcome\nSET door_open 1\nGOTOROOM lounge\n";
    const palace::PalaceVmEngine vm;

    const palace::VmReceipt first = vm.execute(script, selectDoorContext());
    const palace::VmReceipt second = vm.execute(script, selectDoorContext());

    LOGOS_ASSERT_TRUE(first.accepted);
    LOGOS_ASSERT_EQ(first.canonical(), second.canonical());
    LOGOS_ASSERT_EQ(first.stateRoot, second.stateRoot);
    LOGOS_ASSERT_EQ(first.executionReceipt, second.executionReceipt);
    LOGOS_ASSERT_EQ(first.resultingState.at("door_open"), 1);
    LOGOS_ASSERT_EQ(first.localEffects.at(1), std::string("navigate:lounge"));
}

LOGOS_TEST(enter_event_and_resource_limits_are_bounded_deterministically) {
    palace::VmContext context = selectDoorContext();
    context.trigger = "ENTER";
    context.maxEffects = 1;
    const palace::VmReceipt accepted = palace::PalaceVmEngine().execute(
        "ON ENTER\nSAY Welcome\n", context);
    LOGOS_ASSERT_TRUE(accepted.accepted);
    LOGOS_ASSERT_EQ(accepted.localEffects.at(0), std::string("say:Welcome"));

    const palace::VmReceipt overEffects = palace::PalaceVmEngine().execute(
        "ON ENTER\nSAY One\nSAY Two\n", context);
    LOGOS_ASSERT_FALSE(overEffects.accepted);
    LOGOS_ASSERT_EQ(overEffects.rejectedEffects.at(0), std::string("effect-budget-exhausted"));
}

LOGOS_TEST(locked_room_rejects_navigation_without_changing_shared_state) {
    palace::VmContext context = selectDoorContext();
    context.roomLocked = true;
    const palace::VmReceipt receipt = palace::PalaceVmEngine().execute(
        "ON SELECT door\nSET door_open 1\nGOTOROOM lounge\n", context);

    LOGOS_ASSERT_TRUE(receipt.accepted);
    LOGOS_ASSERT_EQ(receipt.resultingState.at("door_open"), 1);
    LOGOS_ASSERT_EQ(receipt.rejectedEffects.at(0), std::string("room-locked:navigate:lounge"));
}

LOGOS_TEST(capability_denied_state_effect_is_not_emitted) {
    palace::VmContext context = selectDoorContext();
    context.canMutateSharedState = false;
    const palace::VmReceipt receipt = palace::PalaceVmEngine().execute(
        "ON SELECT door\nSET door_open 1\n", context);

    LOGOS_ASSERT_TRUE(receipt.accepted);
    LOGOS_ASSERT_EQ(receipt.resultingState.at("door_open"), 0);
    LOGOS_ASSERT_TRUE(receipt.sharedIntents.empty());
    LOGOS_ASSERT_EQ(receipt.rejectedEffects.at(0), std::string("capability-denied:set:door_open"));
}

LOGOS_TEST(over_budget_and_nondeterministic_forms_fail_closed) {
    palace::VmContext context = selectDoorContext();
    context.instructionBudget = 1;
    const palace::VmReceipt overBudget = palace::PalaceVmEngine().execute(
        "ON SELECT door\nSAY one\n", context);
    LOGOS_ASSERT_FALSE(overBudget.accepted);
    LOGOS_ASSERT_EQ(overBudget.rejectedEffects.at(0), std::string("instruction-budget-exhausted"));

    context.instructionBudget = 8;
    const palace::VmReceipt nondeterministic = palace::PalaceVmEngine().execute(
        "ON SELECT door\nRANDOM\n", context);
    LOGOS_ASSERT_FALSE(nondeterministic.accepted);
    LOGOS_ASSERT_EQ(nondeterministic.rejectedEffects.at(0), std::string("unsupported-instruction"));
}
