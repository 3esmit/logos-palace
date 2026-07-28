#pragma once

#include <cstdint>
#include <string>

#include "logos_module_context.h"

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
                            std::int64_t instructionBudget);
};
