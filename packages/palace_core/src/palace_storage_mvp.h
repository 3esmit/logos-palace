#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "palace_storage_catalog_session.h"

namespace palace {

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

// Exact, bounded Gate-3 content graph. Leaves contain two verified room
// backgrounds, one transparent prop plus anchor/layer metadata, two room
// descriptors, and one deterministic door script. Derived catalog manifests
// are created only after their child uploads return exact CIDs.
class PalaceStorageMvpBundle {
public:
    bool initialize(
        const std::string& atriumPng,
        const std::string& loungePng);
    bool initialized() const;
    bool complete() const;
    std::size_t artifactCount() const;
    std::size_t publishedCount() const;

    std::vector<std::string> stageableObjectIds();
    const PalaceStorageMvpArtifactV1* artifact(
        const std::string& objectId) const;
    std::vector<PalaceStorageMvpArtifactV1> artifacts() const;
    bool assignPublicationCid(
        const std::string& objectId,
        const std::string& cid);

    // Canonical catalog is an untrusted transport object, not authority.
    // restoreCanonicalCatalog reconstructs every fixed leaf and derived
    // manifest, then requires exact type/length/hash/canonical-byte matches.
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
    bool m_initialized = false;
};

} // namespace palace
