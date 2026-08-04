#include <logos_test.h>

#include "palace_vm_finality.h"
#include "palace_vm_engine.h"
#include "palace_vm_impl.h"
#include "palace_sha256.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <unistd.h>

namespace {

namespace fs = std::filesystem;

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

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        std::array<char, 64> pattern {};
        const std::string value = "/tmp/palace-vm-finality-XXXXXX";
        std::copy(value.begin(), value.end(), pattern.begin());
        if (::mkdtemp(pattern.data()) == nullptr)
            throw std::runtime_error("mkdtemp failed");
        m_path = pattern.data();
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        fs::remove_all(m_path, ignored);
    }

    const std::string& path() const { return m_path; }

private:
    std::string m_path;
};

const std::string kTrackedScript =
    "ON SELECT door\nSET door_open 1\nGOTOROOM lounge\n";

std::string issueTrackedTurn(PalaceVmImpl& vm, const std::string& actionId)
{
    return vm.executeProvisionalTurn(
        actionId,
        kTrackedScript,
        "bundle-1",
        "7",
        "SELECT:door",
        "door_open=0",
        "atrium,lounge",
        false,
        true,
        8);
}

std::string promoteTrackedTurn(
    PalaceVmImpl& vm,
    const std::string& actionId,
    const std::string& provisionalReceipt,
    const std::string& script = kTrackedScript,
    const std::string& priorState = "door_open=0")
{
    return vm.promoteFinalizedTurn(
        actionId,
        provisionalReceipt,
        script,
        "bundle-1",
        "7",
        "SELECT:door",
        priorState,
        "atrium,lounge",
        false,
        true,
        8);
}

std::string statusValue(
    const std::string& status,
    const std::string& name)
{
    const std::string prefix = name + '=';
    const std::size_t begin = status.find(prefix);
    if (begin == std::string::npos
        || (begin != 0U && status[begin - 1U] != ';')) {
        return {};
    }
    const std::size_t valueBegin = begin + prefix.size();
    const std::size_t end = status.find(';', valueBegin);
    return status.substr(valueBegin, end - valueBegin);
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
    LOGOS_ASSERT_EQ(first.localEffects.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(
        first.deferredEffects.at(0),
        std::string("await-finality:navigate:lounge"));
    LOGOS_ASSERT_TRUE(
        first.canonical().find("navigate:lounge") != std::string::npos);
}

LOGOS_TEST(shared_door_navigation_emits_only_after_finalized_replay) {
    const std::string script =
        "ON SELECT door\nSET door_open 1\nGOTOROOM lounge\n";
    palace::VmContext provisionalContext = selectDoorContext();
    const palace::VmReceipt provisional =
        palace::PalaceVmEngine().execute(script, provisionalContext);

    LOGOS_ASSERT_TRUE(provisional.accepted);
    LOGOS_ASSERT_TRUE(provisional.localEffects.empty());
    LOGOS_ASSERT_EQ(
        provisional.deferredEffects.at(0),
        std::string("await-finality:navigate:lounge"));

    palace::VmContext finalizedContext = selectDoorContext();
    finalizedContext.orderedFinalized = true;
    const palace::VmReceipt finalized =
        palace::PalaceVmEngine().execute(script, finalizedContext);

    LOGOS_ASSERT_TRUE(finalized.accepted);
    LOGOS_ASSERT_TRUE(finalized.deferredEffects.empty());
    LOGOS_ASSERT_EQ(
        finalized.localEffects.at(0),
        std::string("navigate:lounge"));
    LOGOS_ASSERT_EQ(finalized.stateRoot, provisional.stateRoot);
    LOGOS_ASSERT_TRUE(
        finalized.executionReceipt != provisional.executionReceipt);
}

LOGOS_TEST(provisional_navigation_without_shared_state_is_also_deferred) {
    palace::VmContext context = selectDoorContext();
    context.canMutateSharedState = false;
    const palace::VmReceipt provisional =
        palace::PalaceVmEngine().execute(
            "ON SELECT door\nGOTOROOM lounge\n", context);

    LOGOS_ASSERT_TRUE(provisional.accepted);
    LOGOS_ASSERT_TRUE(provisional.localEffects.empty());
    LOGOS_ASSERT_EQ(
        provisional.deferredEffects.at(0),
        std::string("await-finality:navigate:lounge"));
}

LOGOS_TEST(execution_receipt_binds_provisional_or_finalized_phase) {
    const std::string script = "ON SELECT door\nSAY Welcome\n";
    palace::VmContext provisionalContext = selectDoorContext();
    const palace::VmReceipt provisional =
        palace::PalaceVmEngine().execute(script, provisionalContext);

    palace::VmContext finalizedContext = selectDoorContext();
    finalizedContext.orderedFinalized = true;
    const palace::VmReceipt finalized =
        palace::PalaceVmEngine().execute(script, finalizedContext);

    LOGOS_ASSERT_TRUE(provisional.accepted);
    LOGOS_ASSERT_TRUE(finalized.accepted);
    LOGOS_ASSERT_EQ(provisional.stateRoot, finalized.stateRoot);
    LOGOS_ASSERT_EQ(
        provisional.localEffects.size(),
        finalized.localEffects.size());
    LOGOS_ASSERT_EQ(
        provisional.localEffects.at(0),
        finalized.localEffects.at(0));
    LOGOS_ASSERT_TRUE(
        provisional.executionReceipt != finalized.executionReceipt);
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

    LOGOS_ASSERT_FALSE(receipt.accepted);
    LOGOS_ASSERT_EQ(receipt.resultingState.at("door_open"), 0);
    LOGOS_ASSERT_TRUE(receipt.sharedIntents.empty());
    LOGOS_ASSERT_EQ(
        receipt.rejectedEffects.at(0),
        std::string("capability-denied:shared-state"));
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

LOGOS_TEST(canonical_state_parser_rejects_malformed_input_instead_of_empty_state) {
    const palace::CanonicalStateParseResult empty =
        palace::parseCanonicalState("");
    LOGOS_ASSERT_TRUE(empty.accepted);
    LOGOS_ASSERT_TRUE(empty.value.empty());

    const palace::CanonicalStateParseResult valid =
        palace::parseCanonicalState("a=-1;b=0");
    LOGOS_ASSERT_TRUE(valid.accepted);
    LOGOS_ASSERT_EQ(valid.value.at("a"), -1);
    LOGOS_ASSERT_EQ(valid.value.at("b"), 0);

    const std::vector<std::string> rejected = {
        "missing-separator",
        "=1",
        "a=",
        "a=1;",
        "a=1;;b=2",
        "a=01",
        "a=-0",
        "a=1;a=2",
        "b=2;a=1",
        "a=1=2",
    };
    for (const std::string& value : rejected) {
        const palace::CanonicalStateParseResult parsed =
            palace::parseCanonicalState(value);
        LOGOS_ASSERT_FALSE(parsed.accepted);
        LOGOS_ASSERT_TRUE(parsed.value.empty());
        LOGOS_ASSERT_FALSE(parsed.reason.empty());
    }

    LOGOS_ASSERT_FALSE(
        palace::parseCanonicalState("a=1", 2U, 32U).accepted);
    LOGOS_ASSERT_FALSE(
        palace::parseCanonicalState("a=1;b=2", 4096U, 1U).accepted);
}

LOGOS_TEST(canonical_room_parser_is_bounded_and_duplicate_free) {
    const palace::CanonicalRoomsParseResult empty =
        palace::parseCanonicalRooms("");
    LOGOS_ASSERT_TRUE(empty.accepted);
    LOGOS_ASSERT_TRUE(empty.value.empty());

    const palace::CanonicalRoomsParseResult valid =
        palace::parseCanonicalRooms("atrium,lounge");
    LOGOS_ASSERT_TRUE(valid.accepted);
    LOGOS_ASSERT_EQ(valid.value.size(), static_cast<std::size_t>(2));
    LOGOS_ASSERT_EQ(
        palace::canonicalRooms(valid.value),
        std::string("atrium,lounge"));

    const std::vector<std::string> rejected = {
        "atrium,",
        "atrium,,lounge",
        "atrium,atrium",
        "atrium, lounge",
        "atrium/lounge",
    };
    for (const std::string& value : rejected) {
        const palace::CanonicalRoomsParseResult parsed =
            palace::parseCanonicalRooms(value);
        LOGOS_ASSERT_FALSE(parsed.accepted);
        LOGOS_ASSERT_FALSE(parsed.reason.empty());
    }

    LOGOS_ASSERT_FALSE(
        palace::parseCanonicalRooms("atrium", 3U, 64U).accepted);
    LOGOS_ASSERT_FALSE(
        palace::parseCanonicalRooms("atrium,lounge", 4096U, 1U).accepted);
}

LOGOS_TEST(engine_rejects_explicit_boundary_parse_failure) {
    palace::VmContext context = selectDoorContext();
    context.inputError = "noncanonical-state-encoding";
    const palace::VmReceipt receipt = palace::PalaceVmEngine().execute(
        "ON SELECT door\nGOTOROOM lounge\n", context);

    LOGOS_ASSERT_FALSE(receipt.accepted);
    LOGOS_ASSERT_EQ(
        receipt.rejectedEffects.at(0),
        std::string("noncanonical-state-encoding"));
    LOGOS_ASSERT_TRUE(receipt.localEffects.empty());
}

LOGOS_TEST(tracked_finality_rejects_wrong_action_receipt_script_and_state) {
    TemporaryDirectory temporary;
    PalaceVmImpl vm;
    vm._logosCoreSetContext_({}, "vm-test", temporary.path());
    const std::string provisional = issueTrackedTurn(vm, "41");

    LOGOS_ASSERT_TRUE(provisional.rfind("accepted=1;", 0U) == 0U);
    LOGOS_ASSERT_TRUE(
        provisional.find(";local=;shared=") != std::string::npos);
    LOGOS_ASSERT_TRUE(
        provisional.find(
            ";deferred=30:await-finality:navigate:lounge")
        != std::string::npos);

    const std::string wrongAction =
        promoteTrackedTurn(vm, "42", provisional);
    LOGOS_ASSERT_TRUE(
        wrongAction.find("unknown-action") != std::string::npos);
    LOGOS_ASSERT_TRUE(
        wrongAction.find(";local=;shared=") != std::string::npos);

    const std::string wrongReceipt =
        promoteTrackedTurn(vm, "41", provisional + "tampered");
    LOGOS_ASSERT_TRUE(
        wrongReceipt.find("provisional-receipt-mismatch")
        != std::string::npos);
    LOGOS_ASSERT_TRUE(
        wrongReceipt.find(";local=;shared=") != std::string::npos);

    const std::string wrongScript = promoteTrackedTurn(
        vm,
        "41",
        provisional,
        "ON SELECT door\nSAY changed\nSET door_open 1\n"
        "GOTOROOM lounge\n");
    LOGOS_ASSERT_TRUE(
        wrongScript.find("turn-input-mismatch") != std::string::npos);
    LOGOS_ASSERT_TRUE(
        wrongScript.find(";local=;shared=") != std::string::npos);

    const std::string wrongState = promoteTrackedTurn(
        vm, "41", provisional, kTrackedScript, "door_open=1");
    LOGOS_ASSERT_TRUE(
        wrongState.find("turn-input-mismatch") != std::string::npos);
    LOGOS_ASSERT_TRUE(
        wrongState.find(";local=;shared=") != std::string::npos);

    LOGOS_ASSERT_TRUE(
        vm.finalityStatus("41").rfind("status=pending;", 0U) == 0U);
}

LOGOS_TEST(exact_finality_promotion_is_one_shot_and_restart_safe) {
    TemporaryDirectory temporary;
    std::string provisional;
    {
        PalaceVmImpl beforeRestart;
        beforeRestart._logosCoreSetContext_(
            {}, "vm-test", temporary.path());
        provisional = issueTrackedTurn(beforeRestart, "51");
        LOGOS_ASSERT_TRUE(
            beforeRestart.finalityStatus("51").rfind(
                "status=pending;", 0U)
            == 0U);
    }

    std::string finalized;
    {
        PalaceVmImpl afterRestart;
        afterRestart._logosCoreSetContext_(
            {}, "vm-test", temporary.path());
        LOGOS_ASSERT_TRUE(
            afterRestart.finalityStatus("51").rfind(
                "status=pending;", 0U)
            == 0U);

        finalized =
            promoteTrackedTurn(afterRestart, "51", provisional);
        LOGOS_ASSERT_TRUE(finalized.rfind("accepted=1;", 0U) == 0U);
        LOGOS_ASSERT_TRUE(
            finalized.find(";local=15:navigate:lounge;shared=")
            != std::string::npos);
        LOGOS_ASSERT_TRUE(
            finalized.find(";deferred=;rejected=")
            != std::string::npos);
        LOGOS_ASSERT_TRUE(
            afterRestart.finalityStatus("51").rfind(
                "status=promoted;", 0U)
            == 0U);
    }

    PalaceVmImpl secondRestart;
    secondRestart._logosCoreSetContext_({}, "vm-test", temporary.path());
    LOGOS_ASSERT_TRUE(
        secondRestart.finalityStatus("51").rfind(
            "status=promoted;", 0U)
        == 0U);
    const std::string duplicate =
        promoteTrackedTurn(secondRestart, "51", provisional);
    LOGOS_ASSERT_TRUE(
        duplicate.find("already-promoted") != std::string::npos);
    LOGOS_ASSERT_TRUE(
        duplicate.find(";local=;shared=") != std::string::npos);
}

LOGOS_TEST(untracked_finalized_compatibility_api_cannot_navigate) {
    PalaceVmImpl vm;
    const std::string receipt = vm.executeFinalizedTurn(
        kTrackedScript,
        "bundle-1",
        "7",
        "SELECT:door",
        "door_open=0",
        "atrium,lounge",
        false,
        true,
        8);

    LOGOS_ASSERT_TRUE(
        receipt.find("tracked-finality-required") != std::string::npos);
    LOGOS_ASSERT_TRUE(
        receipt.find(";local=;shared=") != std::string::npos);
}

LOGOS_TEST(vm_turn_metrics_bind_steady_duration_to_action_phase_and_receipt) {
    TemporaryDirectory temporary;
    PalaceVmImpl vm;
    vm._logosCoreSetContext_({}, "vm-test", temporary.path());

    LOGOS_ASSERT_EQ(
        vm.vmTurnMetrics("61", "provisional"),
        std::string(
            "status=unavailable;action=61;phase=provisional;"
            "reason=not-recorded"));
    LOGOS_ASSERT_EQ(
        vm.vmTurnMetrics("061", "provisional"),
        std::string("rejected=vm-turn-metrics-query"));
    LOGOS_ASSERT_EQ(
        vm.vmTurnMetrics("61", "preview"),
        std::string("rejected=vm-turn-metrics-query"));

    const std::string provisional = issueTrackedTurn(vm, "61");
    const std::string provisionalMetrics =
        vm.vmTurnMetrics("61", "provisional");
    LOGOS_ASSERT_EQ(
        statusValue(provisionalMetrics, "status"),
        std::string("available"));
    LOGOS_ASSERT_EQ(
        statusValue(provisionalMetrics, "action"),
        std::string("61"));
    LOGOS_ASSERT_EQ(
        statusValue(provisionalMetrics, "phase"),
        std::string("provisional"));
    LOGOS_ASSERT_EQ(
        statusValue(provisionalMetrics, "clock"),
        std::string("steady_clock"));
    LOGOS_ASSERT_EQ(
        statusValue(provisionalMetrics, "receipt_sha256"),
        palace::crypto::sha256Hex(provisional));
    const std::string provisionalDuration =
        statusValue(provisionalMetrics, "duration_ns");
    std::uint64_t parsedDuration = 0U;
    const auto provisionalParsed = std::from_chars(
        provisionalDuration.data(),
        provisionalDuration.data() + provisionalDuration.size(),
        parsedDuration);
    LOGOS_ASSERT_TRUE(!provisionalDuration.empty());
    LOGOS_ASSERT_TRUE(provisionalParsed.ec == std::errc());
    LOGOS_ASSERT_TRUE(
        provisionalParsed.ptr
        == provisionalDuration.data()
            + provisionalDuration.size());

    const std::string finalized =
        promoteTrackedTurn(vm, "61", provisional);
    const std::string finalizedMetrics =
        vm.vmTurnMetrics("61", "finalized");
    LOGOS_ASSERT_EQ(
        statusValue(finalizedMetrics, "status"),
        std::string("available"));
    LOGOS_ASSERT_EQ(
        statusValue(finalizedMetrics, "phase"),
        std::string("finalized"));
    LOGOS_ASSERT_EQ(
        statusValue(finalizedMetrics, "receipt_sha256"),
        palace::crypto::sha256Hex(finalized));
    LOGOS_ASSERT_TRUE(
        vm.vmTurnMetrics("62", "finalized").rfind(
            "status=unavailable;", 0U) == 0U);
    LOGOS_ASSERT_EQ(
        vm.vmTurnMetrics("61", "provisional"),
        provisionalMetrics);
}
