#include "palace_vm_impl.h"

#include "palace_vm_engine.h"

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
    palace::VmContext context;
    context.profileId = "classic-mvp-v1";
    context.scriptBundleCid = scriptBundleCid;
    context.roomEpoch = roomEpoch;
    context.trigger = trigger;
    context.priorState = palace::parseCanonicalState(priorState);
    context.allowedRooms = palace::parseCanonicalRooms(allowedRooms);
    context.roomLocked = roomLocked;
    context.canMutateSharedState = canMutateSharedState;
    context.instructionBudget = instructionBudget < 0
        ? 0U
        : static_cast<std::size_t>(instructionBudget);
    return palace::PalaceVmEngine().execute(script, context).canonical();
}
