#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace palace {

struct CanonicalStateParseResult {
    bool accepted = false;
    std::string reason;
    std::map<std::string, std::int64_t> value;
};

struct CanonicalRoomsParseResult {
    bool accepted = false;
    std::string reason;
    std::vector<std::string> value;
};

struct VmContext {
    std::string profileId;
    std::string scriptBundleCid;
    std::string roomEpoch;
    std::string trigger;
    std::map<std::string, std::int64_t> priorState;
    std::vector<std::string> allowedRooms;
    bool roomLocked = false;
    bool canMutateSharedState = false;
    // Provisional turns may emit shared intents, but navigation coupled to
    // those intents remains deferred. Finalized replay uses the same bounded
    // inputs with this flag set and may emit the navigation effect.
    bool orderedFinalized = false;
    // Set only by the module boundary when canonical input parsing failed.
    // The engine seals a deterministic rejection instead of substituting an
    // empty state or room set.
    std::string inputError;
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
    std::vector<std::string> deferredEffects;
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

CanonicalStateParseResult parseCanonicalState(
    const std::string& value,
    std::size_t maxBytes = 4096,
    std::size_t maxEntries = 32);
CanonicalRoomsParseResult parseCanonicalRooms(
    const std::string& value,
    std::size_t maxBytes = 4096,
    std::size_t maxRooms = 64);
std::string canonicalState(const std::map<std::string, std::int64_t>& state);
std::string canonicalRooms(const std::vector<std::string>& rooms);

} // namespace palace
