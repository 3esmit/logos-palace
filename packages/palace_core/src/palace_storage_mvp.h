#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "palace_storage_catalog_session.h"

namespace palace {

struct AssetAuthoringStateV1;

enum class PalaceStorageMvpArtifactType : std::uint8_t {
    BackgroundPng = 0U,
    PropPng = 1U,
    PropMetadata = 2U,
    ScriptBundle = 3U,
    RoomMetadata = 4U,
    PropManifest = 5U,
    RoomManifest = 6U,
    PalaceManifest = 7U,
};

enum class PalaceStorageMvpFetchSource : std::uint8_t {
    Cache = 0U,
    Network = 1U,
};

struct PalaceStorageMvpArtifactV1 {
    std::string objectId;
    PalaceStorageMvpArtifactType type =
        PalaceStorageMvpArtifactType::BackgroundPng;
    std::string mediaType;
    std::string bytes;
    StorageCatalogObjectSpecV2 specification;
    std::string cid;
};

struct PalaceStorageMvpPropAssetV1 {
    std::string propId;
    std::string handle;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t anchorX = 0U;
    std::uint32_t anchorY = 0U;
    std::string layer;
};

// Optional user-authored prop input. The MVP graph contains no prop leaf or
// manifest until an administrator selects, verifies, publishes, and assigns
// one through the authoring boundary.
struct PalaceStorageMvpPropInputV1 {
    std::string png;
    std::string propId;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t anchorX = 0U;
    std::uint32_t anchorY = 0U;
    std::string layer;
};

const char* palaceStorageMvpArtifactTypeName(
    PalaceStorageMvpArtifactType type);

const char* palaceStorageMvpFetchSourceName(
    PalaceStorageMvpFetchSource source);

// Local verification is valid only when native Storage proves every exact
// catalog CID exists before any transfer is dispatched.
std::optional<PalaceStorageMvpFetchSource>
selectPalaceStorageMvpFetchSource(
    const std::vector<std::optional<bool>>& nativeCidAvailability,
    std::size_t expectedArtifactCount);

// Exact, bounded content graph. Media leaves come only from verified
// administrator-authored assignments. Metadata and script leaves use the
// protocol's canonical templates. Derived manifests are created only after
// their child uploads return exact CIDs.
class PalaceStorageMvpBundle {
public:
    bool initialize(
        const std::string& atriumPng,
        const std::string& loungePng,
        const std::optional<PalaceStorageMvpPropInputV1>& prop =
            std::nullopt);
    // Compatibility overload for callers that already have an explicit
    // user-authored prop assignment.
    bool initialize(
        const std::string& atriumPng,
        const std::string& loungePng,
        const std::string& propPng,
        const std::string& propId,
        std::uint32_t propWidth,
        std::uint32_t propHeight,
        std::uint32_t anchorX,
        std::uint32_t anchorY,
        const std::string& layer);
    bool initialized() const;
    bool complete() const;
    std::size_t artifactCount() const;
    std::size_t publishedCount() const;

    std::vector<std::string> stageableObjectIds();
    const PalaceStorageMvpArtifactV1* artifact(
        const std::string& objectId) const;
    std::vector<PalaceStorageMvpArtifactV1> artifacts() const;
    // Resolves a published PNG leaf only after every catalog object has
    // passed exact byte verification. The returned artifact remains owned by
    // this bundle and binds bytes to the published source CID.
    const PalaceStorageMvpArtifactV1* fetchedPngArtifactForCid(
        const std::string& sourceCid) const;
    // A locally restored catalog is transport data, never creator authority.
    // Before draft initialization, require each assigned administrator asset
    // to match the exact verified PNG leaf and published Storage CID.
    bool matchesAuthoringAssignments(
        const AssetAuthoringStateV1& authoring) const;
    bool assignPublicationCid(
        const std::string& objectId,
        const std::string& cid);
    // Validates exact catalog bytes. Visitor leaf placeholders acquire bytes
    // only after their length and digest match the restored graph.
    bool acceptFetchedBytes(
        const std::string& objectId,
        const std::string& bytes);
    bool fetchedContentValid() const;
    std::string propId() const;
    // Derived exclusively from the user-authored prop identifier accepted by
    // initialize/validated from a restored graph. Callers must not recreate
    // prop object IDs from application-owned names.
    std::string propManifestObjectId() const;
    std::optional<PalaceStorageMvpPropAssetV1>
    propAsset() const;

    // Canonical catalog is an untrusted transport object, not authority.
    // Restore can start without administrator-local authored bytes. It creates
    // content-verified leaf placeholders for the catalog's validated graph,
    // reconstructs every derived
    // manifest, then requires exact graph and canonical-byte matches.
    std::string canonicalCatalog() const;
    bool restoreCanonicalCatalog(const std::string& encoded);

private:
    bool refreshDerivedArtifacts();
    bool addLeaf(
        const std::string& objectId,
        PalaceStorageMvpArtifactType type,
        const std::string& mediaType,
        const std::string& bytes);
    bool addManifest(
        const std::string& objectId,
        PalaceStorageMvpArtifactType type,
        StorageCatalogObjectKind catalogKind,
        const std::vector<std::string>& childObjectIds);

    std::map<std::string, PalaceStorageMvpArtifactV1> m_artifacts;
    std::optional<std::string> m_graphPropId;
    bool m_initialized = false;
};

} // namespace palace
