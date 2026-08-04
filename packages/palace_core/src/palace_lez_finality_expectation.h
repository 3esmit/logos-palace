#pragma once

#include "palace_lez.h"
#include "palace_lez_explorer_finality.h"

#include <cstdint>
#include <string>
#include <vector>

namespace palace {

struct PalaceLezStableAccountBatchV1 {
    std::int64_t heightBefore = -1;
    std::int64_t heightAfter = -1;
    // Exact module responses in transaction-plan account order.
    std::vector<std::string> accountResponseJson;
};

struct PalaceLezFinalityExpectationBuildResultV1 {
    bool accepted = false;
    std::string reason;
    PalaceLezExplorerTransactionExpectationV1 expectation;
};

// Converts one height-stable module read into the immutable explorer
// expectation. It binds every account in plan order, including signer/system
// accounts, and refuses to infer an owner or post-state.
PalaceLezFinalityExpectationBuildResultV1
buildPalaceLezFinalityExpectationV1(
    const PalaceLezTrackedTransaction& observedTransaction,
    const PalaceLezStableAccountBatchV1& stableAccounts);

} // namespace palace
