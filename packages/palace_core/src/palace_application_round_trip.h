#pragma once

#include <cstddef>
#include <string>

namespace palace {

struct PalaceApplicationRoundTripResultV1 {
    bool accepted = false;
    std::string response;
    std::string reason;
    std::size_t utf8Bytes = 0U;
};

// Diagnostic application seam only. It echoes no arbitrary payload size:
// acceptance uses the three exact UTF-8 byte counts exercised by the harness.
PalaceApplicationRoundTripResultV1 applicationRoundTripV1(
    const std::string& payload);

} // namespace palace
