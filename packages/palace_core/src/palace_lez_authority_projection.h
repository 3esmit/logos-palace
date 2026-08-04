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

// A decoded account named by its exact program-derived address. Evidence
// source belongs to the atomic AuthorityProjection replacement, not here.
struct PalaceLezNamedAuthorityAccountV1 {
    std::string accountIdHex;
    PalaceLezPublicAccountV3 account;
};

// Compatibility spelling for the public-finality path. New local committed
// code must use PalaceLezNamedAuthorityAccountV1 instead.
using PalaceLezNamedFinalizedAccountV1 = PalaceLezNamedAuthorityAccountV1;

// Runtime authority materialization. It deliberately has generic checkpoint
// names and an explicit source, so local committed data has no path through
// public-finality persistence types.
struct PalaceLezAuthorityMaterializationV1 {
    AuthoritySnapshotSource source = AuthoritySnapshotSource::None;
    std::string networkId;
    std::string programIdHex;
    std::string rootAccountIdHex;
    std::uint64_t committedBlockId = 0U;
    std::string committedBlockHashHex;
    std::uint64_t lastOrderedActionId = 0U;
    std::vector<PalaceLezNamedAuthorityAccountV1> accounts;
};

// Returns the canonical schema-v3 record digest used by decoded LEZ accounts.
// An empty result means the record type and variant do not match.
std::string canonicalPalaceLezRecordDigestV3(
    PalaceLezRecordTypeV3 recordType,
    const PalaceLezRecordV3& record);

// Projects only a complete, internally consistent set of decoded accounts.
// Each decoded record must be named by its exact program-derived account ID.
// The root is supplied separately; root records in children are
// rejected. A room may have no shared-state record yet; more than one record
// for the same room is ambiguous and rejected.
PalaceLezAuthorityProjectionResultV1 projectLezAuthorityV1(
    const PalaceLezNamedAuthorityAccountV1& root,
    const std::vector<PalaceLezNamedAuthorityAccountV1>& children);

// Projection and replacement are one logical operation. The source remains
// explicit so locally committed state can never be presented as finality.
PalaceLezAuthorityProjectionResultV1 replaceLezAuthorityV1(
    AuthorityProjection& destination,
    const PalaceLezNamedAuthorityAccountV1& root,
    const std::vector<PalaceLezNamedAuthorityAccountV1>& children,
    AuthoritySnapshotSource source,
    std::int64_t committedAt);

// Public-finality compatibility entry points. Keep existing callers stable;
// local committed paths must use the source-explicit functions above.
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
