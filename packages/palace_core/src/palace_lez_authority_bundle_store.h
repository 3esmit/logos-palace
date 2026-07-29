#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace palace {

struct PalaceLezAuthorityBundleScopeV1 {
    std::string networkId;
    std::string programIdHex;
    std::string rootAccountIdHex;
};

struct PalaceLezAuthorityBundleCheckpointV1 {
    std::uint64_t finalizedBlockId = 0U;
    std::uint64_t finalizedBlockHeight = 0U;
    std::string finalizedBlockHashHex;
    std::uint64_t lastOrderedActionId = 0U;
};

struct PalaceLezFinalizedAuthorityAccountV1 {
    std::string accountIdHex;
    // Preserved byte-for-byte. Load validates it with PalaceLezCodec before
    // exposing the bundle, so callers can strictly re-decode and project it.
    std::string responseJson;
};

struct PalaceLezFinalizedAuthorityBundleV1 {
    PalaceLezAuthorityBundleScopeV1 scope;
    PalaceLezAuthorityBundleCheckpointV1 checkpoint;
    std::vector<PalaceLezFinalizedAuthorityAccountV1> accounts;
};

struct PalaceLezAuthorityBundleExpectationV1 {
    PalaceLezAuthorityBundleScopeV1 scope;
    // Supply this when another durable boundary already knows the exact
    // finalized checkpoint. Omit it when opening the latest stored snapshot.
    std::optional<PalaceLezAuthorityBundleCheckpointV1> checkpoint;
};

enum class PalaceLezAuthorityBundleStoreStatus {
    Saved,
    Loaded,
    NotFound,
    InvalidArgument,
    BundleRejected,
    InvalidRecord,
    BindingMismatch,
    CheckpointMismatch,
    InsecurePermissions,
    UnsafePath,
    IoError,
};

// Durable boundary for finalized LEZ authority account snapshots. The fixed
// child filename prevents caller-controlled traversal. Successful load is
// transactional: destination changes only after every account passes strict
// decoding and the root agrees with the finalized checkpoint.
class PalaceLezAuthorityBundleStore {
public:
    PalaceLezAuthorityBundleStore(
        std::string instancePersistenceRoot,
        PalaceLezAuthorityBundleExpectationV1 expectation);

    PalaceLezAuthorityBundleStoreStatus save(
        const PalaceLezFinalizedAuthorityBundleV1& bundle) const;
    PalaceLezAuthorityBundleStoreStatus load(
        PalaceLezFinalizedAuthorityBundleV1& bundle) const;

private:
    std::string instancePersistenceRoot_;
    PalaceLezAuthorityBundleExpectationV1 expectation_;
};

const char* palaceLezAuthorityBundleStoreStatusName(
    PalaceLezAuthorityBundleStoreStatus status);

} // namespace palace
