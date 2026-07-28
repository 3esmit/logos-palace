#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace palace {

struct VmContext {
    std::string profileId;
    std::string scriptBundleCid;
    std::string roomEpoch;
    std::string trigger;
    std::map<std::string, std::int64_t> priorState;
    std::vector<std::string> allowedRooms;
    bool roomLocked = false;
    bool canMutateSharedState = false;
    std::size_t instructionBudget = 0;
    std::size_t maxScriptBytes = 4096;
    std::size_t maxEffects = 32;
    std::size_t maxOutputBytes = 4096;
    std::size_t maxStateEntries = 32;
};

struct VmReceipt {
    bool accepted = false;
    std::vector<std::string> localEffects;
    std::vector<std::string> sharedIntents;
    std::vector<std::string> rejectedEffects;
    std::map<std::string, std::int64_t> resultingState;
    std::string stateRoot;
    std::string executionReceipt;

    std::string canonical() const;
};

// The VM has no filesystem, network, clock, random, key, QML, or module API.
// Its caller supplies all state and policy required for one bounded turn.
class PalaceVmEngine {
public:
    VmReceipt execute(const std::string& script, const VmContext& context) const;
};

std::map<std::string, std::int64_t> parseCanonicalState(const std::string& value);
std::vector<std::string> parseCanonicalRooms(const std::string& value);
std::string canonicalState(const std::map<std::string, std::int64_t>& state);

} // namespace palace
