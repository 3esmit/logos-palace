#pragma once

#include <string>

#include "palace_lez.h"

namespace palace {

struct PalaceLezIntentParseResult {
    bool accepted = false;
    std::string reason;
    PalaceLezInstructionV3 instruction;
};

// Strict UI-to-Core command boundary for schema-v3 Palace transitions.
// Ordered action IDs come from the durable journal, never from JSON.
class PalaceLezIntentParser {
public:
    static PalaceLezIntentParseResult parse(
        const std::string& orderedActionId,
        const std::string& transitionJson);
};

} // namespace palace
