#pragma once

#include "palace_authority.h"
#include "palace_lez.h"

#include <cstdint>
#include <string>
#include <vector>

namespace palace {

struct PalaceLezAuthorityProjectionResultV1 {
    bool accepted = false;
    std::string reason;
    AuthoritySnapshotV1 snapshot;
};

struct PalaceLezNamedFinalizedAccountV1 {
    std::string accountIdHex;
    PalaceLezPublicAccountV3 account;
};

// Returns the canonical schema-v3 record digest used by decoded LEZ accounts.
// An empty result means the record type and variant do not match.
std::string canonicalPalaceLezRecordDigestV3(
    PalaceLezRecordTypeV3 recordType,
    const PalaceLezRecordV3& record);

// Projects only a complete, internally consistent set of finalized accounts.
// Each decoded record must be named by its exact program-derived account ID.
// The root is supplied separately; root records in finalizedChildren are
// rejected. A room may have no shared-state record yet; more than one record
// for the same room is ambiguous and rejected.
PalaceLezAuthorityProjectionResultV1 projectFinalizedLezAuthorityV1(
    const PalaceLezNamedFinalizedAccountV1& finalizedRoot,
    const std::vector<PalaceLezNamedFinalizedAccountV1>& finalizedChildren);

// Projection and AuthorityProjection replacement are one logical operation:
// failures leave the existing finalized projection untouched.
PalaceLezAuthorityProjectionResultV1 replaceFinalizedLezAuthorityV1(
    AuthorityProjection& destination,
    const PalaceLezNamedFinalizedAccountV1& finalizedRoot,
    const std::vector<PalaceLezNamedFinalizedAccountV1>& finalizedChildren,
    std::int64_t finalizedAt);

} // namespace palace
