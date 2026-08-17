#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "logos_module_context.h"

#include "palace_vm_finality.h"

// One narrow module boundary: caller supplies a bounded, canonical turn and
// receives deterministic typed-effect data encoded as a canonical receipt.
class PalaceVmImpl : public LogosModuleContext {
public:
    std::string executeTurn(const std::string& script,
                            const std::string& scriptBundleCid,
                            const std::string& roomEpoch,
                            const std::string& trigger,
                            const std::string& priorState,
                            const std::string& allowedRooms,
                            bool roomLocked,
                            bool canMutateSharedState,
                            int64_t instructionBudget);

    // Compatibility entry point retained for callers compiled against the
    // initial phase API. It rejects because direct finalized execution lacks
    // the provisional action/receipt binding required for navigation.
    std::string executeFinalizedTurn(const std::string& script,
                                     const std::string& scriptBundleCid,
                                     const std::string& roomEpoch,
                                     const std::string& trigger,
                                     const std::string& priorState,
                                     const std::string& allowedRooms,
                                     bool roomLocked,
                                     bool canMutateSharedState,
                                     int64_t instructionBudget);

    // Issues and durably records a provisional turn under one ordered LEZ
    // action. Provisional execution never emits navigation.
    std::string executeProvisionalTurn(
        const std::string& actionId,
        const std::string& script,
        const std::string& scriptBundleCid,
        const std::string& roomEpoch,
        const std::string& trigger,
        const std::string& priorState,
        const std::string& allowedRooms,
        bool roomLocked,
        bool canMutateSharedState,
        int64_t instructionBudget);

    // Palace Core calls this only after LEZ finality. The exact action,
    // provisional receipt, script, policy, and state must match the durable
    // pending record. Successful promotion is one-shot and persisted before
    // the receipt containing navigation is returned.
    std::string promoteFinalizedTurn(
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
        int64_t instructionBudget);

    std::string finalityStatus(const std::string& actionId);
    std::string vmTurnMetrics(const std::string& actionId,
                              const std::string& phase);

protected:
    void onContextReady() override;

private:
    std::string executeTurnWithPhase(const std::string& script,
                                     const std::string& scriptBundleCid,
                                     const std::string& roomEpoch,
                                     const std::string& trigger,
                                     const std::string& priorState,
                                     const std::string& allowedRooms,
                                     bool roomLocked,
                                     bool canMutateSharedState,
                                     bool orderedFinalized,
                                     std::int64_t instructionBudget,
                                     const std::string& boundaryError = {},
                                     const std::string& metricActionId = {},
                                     const std::string& metricPhase = {});
    palace::PalaceVmTurnInput trackedInput(
        const std::string& actionId,
        const std::string& script,
        const std::string& scriptBundleCid,
        const std::string& roomEpoch,
        const std::string& trigger,
        const std::string& priorState,
        const std::string& allowedRooms,
        bool roomLocked,
        bool canMutateSharedState,
        std::int64_t instructionBudget) const;
    std::string rejectTrackedTurn(
        const palace::PalaceVmTurnInput& input,
        const std::string& reason);

    struct ExecuteTurnMetric {
        std::uint64_t durationNanoseconds = 0U;
        std::string receiptSha256;
    };

    std::mutex m_finalityMutex;
    std::mutex m_metricMutex;
    std::map<std::string, ExecuteTurnMetric> m_executeTurnMetrics;
    palace::PalaceVmFinalityJournal m_finalityJournal;
    std::unique_ptr<palace::PalaceVmFinalityStore> m_finalityStore;
    bool m_finalityStoreReady = false;
};
