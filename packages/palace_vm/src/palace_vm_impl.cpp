#include "palace_vm_impl.h"

#include "palace_sha256.h"
#include "palace_vm_engine.h"

#include <charconv>
#include <chrono>

namespace {

constexpr std::size_t kMaximumExecuteTurnMetrics = 64U;

std::uint64_t normalizedInstructionBudget(std::int64_t instructionBudget)
{
    return instructionBudget < 0
        ? 0U
        : static_cast<std::uint64_t>(instructionBudget);
}

bool canonicalActionId(const std::string& actionId)
{
    if (actionId.empty() || actionId.size() > 20U
        || (actionId.size() > 1U && actionId.front() == '0')) {
        return false;
    }
    std::uint64_t parsed = 0U;
    const auto result = std::from_chars(
        actionId.data(), actionId.data() + actionId.size(), parsed);
    return result.ec == std::errc()
        && result.ptr == actionId.data() + actionId.size()
        && actionId == std::to_string(parsed);
}

bool metricPhase(const std::string& phase)
{
    return phase == "provisional" || phase == "finalized";
}

std::string metricKey(const std::string& actionId,
                      const std::string& phase)
{
    return actionId + ':' + phase;
}

} // namespace

std::string PalaceVmImpl::executeTurn(const std::string& script,
                                      const std::string& scriptBundleCid,
                                      const std::string& roomEpoch,
                                      const std::string& trigger,
                                      const std::string& priorState,
                                      const std::string& allowedRooms,
                                      bool roomLocked,
                                      bool canMutateSharedState,
                                      std::int64_t instructionBudget)
{
    return executeTurnWithPhase(
        script,
        scriptBundleCid,
        roomEpoch,
        trigger,
        priorState,
        allowedRooms,
        roomLocked,
        canMutateSharedState,
        false,
        instructionBudget,
        {});
}

std::string PalaceVmImpl::executeFinalizedTurn(
    const std::string& script,
    const std::string& scriptBundleCid,
    const std::string& roomEpoch,
    const std::string& trigger,
    const std::string& priorState,
    const std::string& allowedRooms,
    bool roomLocked,
    bool canMutateSharedState,
    std::int64_t instructionBudget)
{
    return executeTurnWithPhase(
        script,
        scriptBundleCid,
        roomEpoch,
        trigger,
        priorState,
        allowedRooms,
        roomLocked,
        canMutateSharedState,
        false,
        instructionBudget,
        "tracked-finality-required");
}

std::string PalaceVmImpl::executeProvisionalTurn(
    const std::string& actionId,
    const std::string& script,
    const std::string& scriptBundleCid,
    const std::string& roomEpoch,
    const std::string& trigger,
    const std::string& priorState,
    const std::string& allowedRooms,
    bool roomLocked,
    bool canMutateSharedState,
    std::int64_t instructionBudget)
{
    const palace::PalaceVmTurnInput input = trackedInput(
        actionId,
        script,
        scriptBundleCid,
        roomEpoch,
        trigger,
        priorState,
        allowedRooms,
        roomLocked,
        canMutateSharedState,
        instructionBudget);
    const std::string receipt = executeTurnWithPhase(
        script,
        scriptBundleCid,
        roomEpoch,
        trigger,
        priorState,
        allowedRooms,
        roomLocked,
        canMutateSharedState,
        false,
        instructionBudget,
        {},
        actionId,
        "provisional");
    if (receipt.rfind("accepted=1;", 0U) != 0U)
        return receipt;

    std::lock_guard<std::mutex> lock(m_finalityMutex);
    if (!m_finalityStoreReady || !m_finalityStore)
        return rejectTrackedTurn(input, "finality-journal-unavailable");

    palace::PalaceVmFinalityJournal candidate = m_finalityJournal;
    const palace::PalaceVmFinalityResult recorded =
        candidate.recordProvisional(input, receipt);
    if (!recorded.accepted)
        return rejectTrackedTurn(input, recorded.reason);
    if (recorded.changed && !m_finalityStore->save(candidate))
        return rejectTrackedTurn(input, "finality-journal-save-failed");
    if (recorded.changed)
        m_finalityJournal = std::move(candidate);
    return receipt;
}

std::string PalaceVmImpl::promoteFinalizedTurn(
    const std::string& actionId,
    const std::string& provisionalReceipt,
    const std::string& script,
    const std::string& scriptBundleCid,
    const std::string& roomEpoch,
    const std::string& trigger,
    const std::string& priorState,
    const std::string& allowedRooms,
    bool roomLocked,
    bool canMutateSharedState,
    std::int64_t instructionBudget)
{
    const palace::PalaceVmTurnInput input = trackedInput(
        actionId,
        script,
        scriptBundleCid,
        roomEpoch,
        trigger,
        priorState,
        allowedRooms,
        roomLocked,
        canMutateSharedState,
        instructionBudget);

    std::lock_guard<std::mutex> lock(m_finalityMutex);
    if (!m_finalityStoreReady || !m_finalityStore)
        return rejectTrackedTurn(input, "finality-journal-unavailable");
    const palace::PalaceVmFinalityResult validation =
        m_finalityJournal.validatePromotion(input, provisionalReceipt);
    if (!validation.accepted)
        return rejectTrackedTurn(input, validation.reason);

    const std::string finalizedReceipt = executeTurnWithPhase(
        script,
        scriptBundleCid,
        roomEpoch,
        trigger,
        priorState,
        allowedRooms,
        roomLocked,
        canMutateSharedState,
        true,
        instructionBudget,
        {},
        actionId,
        "finalized");
    if (finalizedReceipt.rfind("accepted=1;", 0U) != 0U)
        return finalizedReceipt;

    palace::PalaceVmFinalityJournal candidate = m_finalityJournal;
    const palace::PalaceVmFinalityResult promoted =
        candidate.recordPromotion(input, provisionalReceipt, finalizedReceipt);
    if (!promoted.accepted)
        return rejectTrackedTurn(input, promoted.reason);
    if (!m_finalityStore->save(candidate))
        return rejectTrackedTurn(input, "finality-journal-save-failed");
    m_finalityJournal = std::move(candidate);
    return finalizedReceipt;
}

std::string PalaceVmImpl::finalityStatus(const std::string& actionId)
{
    std::lock_guard<std::mutex> lock(m_finalityMutex);
    if (!m_finalityStoreReady)
        return "status=unavailable;action=" + actionId;
    return m_finalityJournal.statusEvidence(actionId);
}

std::string PalaceVmImpl::vmTurnMetrics(
    const std::string& actionId,
    const std::string& phase)
{
    if (!canonicalActionId(actionId) || !metricPhase(phase))
        return "rejected=vm-turn-metrics-query";
    std::lock_guard<std::mutex> lock(m_metricMutex);
    const auto found =
        m_executeTurnMetrics.find(metricKey(actionId, phase));
    if (found == m_executeTurnMetrics.end()) {
        return "status=unavailable;action=" + actionId
            + ";phase=" + phase + ";reason=not-recorded";
    }
    return "status=available;action=" + actionId
        + ";phase=" + phase
        + ";clock=steady_clock;duration_ns="
        + std::to_string(found->second.durationNanoseconds)
        + ";receipt_sha256=" + found->second.receiptSha256;
}

void PalaceVmImpl::onContextReady()
{
    {
        std::lock_guard<std::mutex> metricLock(m_metricMutex);
        m_executeTurnMetrics.clear();
    }
    std::lock_guard<std::mutex> lock(m_finalityMutex);
    m_finalityStoreReady = false;
    m_finalityJournal = {};
    m_finalityStore.reset();
    if (instancePersistencePath().empty())
        return;

    auto store = std::make_unique<palace::PalaceVmFinalityStore>(
        instancePersistencePath());
    palace::PalaceVmFinalityJournal restored;
    if (store->exists() && !store->load(restored)) {
        m_finalityStore = std::move(store);
        return;
    }
    m_finalityJournal = std::move(restored);
    m_finalityStore = std::move(store);
    m_finalityStoreReady = true;
}

std::string PalaceVmImpl::executeTurnWithPhase(
    const std::string& script,
    const std::string& scriptBundleCid,
    const std::string& roomEpoch,
    const std::string& trigger,
    const std::string& priorState,
    const std::string& allowedRooms,
    bool roomLocked,
    bool canMutateSharedState,
    bool orderedFinalized,
    std::int64_t instructionBudget,
    const std::string& boundaryError,
    const std::string& metricActionId,
    const std::string& measuredPhase)
{
    const auto startedAt = std::chrono::steady_clock::now();
    palace::VmContext context;
    context.profileId = "classic-mvp-v1";
    context.scriptBundleCid = scriptBundleCid;
    context.roomEpoch = roomEpoch;
    context.trigger = trigger;
    const palace::CanonicalStateParseResult parsedState =
        palace::parseCanonicalState(priorState);
    const palace::CanonicalRoomsParseResult parsedRooms =
        palace::parseCanonicalRooms(allowedRooms);
    if (parsedState.accepted) {
        context.priorState = parsedState.value;
    } else {
        context.inputError = parsedState.reason;
    }
    if (parsedRooms.accepted) {
        context.allowedRooms = parsedRooms.value;
    } else if (context.inputError.empty()) {
        context.inputError = parsedRooms.reason;
    }
    if (!boundaryError.empty())
        context.inputError = boundaryError;
    context.roomLocked = roomLocked;
    context.canMutateSharedState = canMutateSharedState;
    context.orderedFinalized = orderedFinalized;
    context.instructionBudget =
        static_cast<std::size_t>(normalizedInstructionBudget(instructionBudget));
    const std::string receipt =
        palace::PalaceVmEngine().execute(script, context).canonical();
    const auto elapsed = std::chrono::duration_cast<
        std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - startedAt).count();
    if (canonicalActionId(metricActionId)
        && metricPhase(measuredPhase)) {
        std::lock_guard<std::mutex> lock(m_metricMutex);
        const std::string key =
            metricKey(metricActionId, measuredPhase);
        if (m_executeTurnMetrics.find(key)
                != m_executeTurnMetrics.end()
            || m_executeTurnMetrics.size()
                < kMaximumExecuteTurnMetrics) {
            m_executeTurnMetrics[key] = {
                elapsed < 0
                    ? 0U
                    : static_cast<std::uint64_t>(elapsed),
                palace::crypto::sha256Hex(receipt),
            };
        }
    }
    return receipt;
}

palace::PalaceVmTurnInput PalaceVmImpl::trackedInput(
    const std::string& actionId,
    const std::string& script,
    const std::string& scriptBundleCid,
    const std::string& roomEpoch,
    const std::string& trigger,
    const std::string& priorState,
    const std::string& allowedRooms,
    bool roomLocked,
    bool canMutateSharedState,
    std::int64_t instructionBudget) const
{
    palace::PalaceVmTurnInput input;
    input.actionId = actionId;
    input.script = script;
    input.scriptBundleCid = scriptBundleCid;
    input.roomEpoch = roomEpoch;
    input.trigger = trigger;
    input.priorState = priorState;
    input.allowedRooms = allowedRooms;
    input.roomLocked = roomLocked;
    input.canMutateSharedState = canMutateSharedState;
    input.instructionBudget =
        normalizedInstructionBudget(instructionBudget);
    return input;
}

std::string PalaceVmImpl::rejectTrackedTurn(
    const palace::PalaceVmTurnInput& input,
    const std::string& reason)
{
    return executeTurnWithPhase(
        input.script,
        input.scriptBundleCid,
        input.roomEpoch,
        input.trigger,
        input.priorState,
        input.allowedRooms,
        input.roomLocked,
        input.canMutateSharedState,
        false,
        static_cast<std::int64_t>(input.instructionBudget),
        reason);
}
