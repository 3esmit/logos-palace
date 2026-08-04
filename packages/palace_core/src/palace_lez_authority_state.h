#pragma once

#include "palace_authority.h"
#include "palace_lez.h"
#include "palace_lez_authority_bundle_store.h"
#include "palace_lez_authority_projection.h"
#include "palace_lez_explorer_finality.h"
#include "palace_lez_explorer_history.h"
#include "palace_lez_finality_expectation.h"

#include <string>

namespace palace {

struct PalaceLezAuthorityStateUpdateV1 {
    bool accepted = false;
    std::string reason;
};

// Strictly decodes and projects one complete durable bundle. Scope and an
// optional externally pinned checkpoint are checked before destination changes.
PalaceLezAuthorityStateUpdateV1
restoreFinalizedLezAuthorityStateV1(
    AuthorityProjection& destination,
    const PalaceLezFinalizedAuthorityBundleV1& bundle,
    const PalaceLezAuthorityBundleExpectationV1& expectation);

// Applies exactly one sequential finalized action to a complete prior bundle.
// Initialize(0) is the sole exception: it seeds an empty, explicitly scoped
// bundle with no prior checkpoint. The account batch is in transaction-plan
// order and must match the immutable explorer certificate byte-for-byte by
// account owner and data digest. destination and bundle remain unchanged on
// rejection.
PalaceLezAuthorityStateUpdateV1
applyFinalizedLezAuthorityActionV1(
    AuthorityProjection& destination,
    PalaceLezFinalizedAuthorityBundleV1& bundle,
    const PalaceLezAuthorityBundleExpectationV1& priorExpectation,
    const PalaceLezTrackedTransaction& finalizedTransaction,
    const PalaceLezExplorerFinalityCertificateV1& certificate,
    const PalaceLezStableAccountBatchV1& stableAccounts);

// Rebuilds a complete bundle after a strict finalized history scan. Responses
// are in history.uniqueAccountIdsHex order. The stable read height, supplied
// checkpoint, and history's newest finalized block must identify one state.
// destination and bundle remain unchanged on rejection.
PalaceLezAuthorityStateUpdateV1
rebuildFinalizedLezAuthorityStateV1(
    AuthorityProjection& destination,
    PalaceLezFinalizedAuthorityBundleV1& bundle,
    const PalaceLezAuthorityBundleScopeV1& scope,
    const PalaceLezExplorerHistoryResultV1& history,
    const PalaceLezStableAccountBatchV1& stableAccounts,
    const PalaceLezAuthorityBundleCheckpointV1& checkpoint);

} // namespace palace
