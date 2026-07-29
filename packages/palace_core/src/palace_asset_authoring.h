#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace palace {

class AssetAuthoringStore;
class VerifiedAssetStore;

struct AssetAuthoringAssetV1 {
    std::string handle;
    std::string label;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint64_t byteLength = 0U;
    std::string reviewState = "pending";
    std::string publishedCid;
};

struct AssetAuthoringPropAssignmentV1 {
    std::string propId;
    std::string handle;
    std::uint32_t anchorX = 0U;
    std::uint32_t anchorY = 0U;
    std::string layer;
};

struct AssetAuthoringStateV1 {
    std::map<std::string, AssetAuthoringAssetV1> assets;
    std::map<std::string, std::string> roomAssignments;
    std::optional<AssetAuthoringPropAssignmentV1> propAssignment;
    bool bundleLocked = false;
};

struct AssetAuthoringResult {
    bool accepted = false;
    std::string reason;
    std::string sessionId;
    std::string handle;
    std::uint64_t nextSequence = 0U;
    std::uint64_t byteLength = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
};

// Owns the production authoring boundary for administrator-selected PNG
// assets. Bytes enter only through bounded, ordered base64 chunks and become
// visible only after complete PNG verification and atomic persistence.
class AssetAuthoringCatalog {
public:
    static constexpr std::uint64_t MaximumChunkBytes = 32U * 1024U;
    static constexpr std::uint64_t MaximumAssetBytes = 10U * 1024U * 1024U;
    static constexpr std::size_t MaximumSessions = 4U;
    static constexpr std::size_t MaximumAssets = 128U;

    AssetAuthoringCatalog();
    ~AssetAuthoringCatalog();
    AssetAuthoringCatalog(AssetAuthoringCatalog&&) noexcept;
    AssetAuthoringCatalog& operator=(AssetAuthoringCatalog&&) noexcept;
    AssetAuthoringCatalog(const AssetAuthoringCatalog&) = delete;
    AssetAuthoringCatalog& operator=(const AssetAuthoringCatalog&) = delete;

    bool initialize(const std::string& instanceRoot,
                    const VerifiedAssetStore& verifiedAssets);
    bool ready() const;

    AssetAuthoringResult begin(const std::string& label);
    AssetAuthoringResult append(const std::string& sessionId,
                                std::uint64_t sequence,
                                const std::string& canonicalBase64);
    AssetAuthoringResult commit(const std::string& sessionId);
    AssetAuthoringResult cancel(const std::string& sessionId);
    AssetAuthoringResult review(const std::string& handle,
                                const std::string& decision);
    AssetAuthoringResult recordPublishedCid(
        const std::string& handle,
        const std::string& cid);
    AssetAuthoringResult assign(const std::string& roomId,
                                const std::string& handle);
    AssetAuthoringResult assignProp(
        const std::string& propId,
        const std::string& handle,
        std::uint32_t anchorX,
        std::uint32_t anchorY,
        const std::string& layer);
    bool lockAssignments();

    std::string handleForRoom(const std::string& roomId) const;
    const AssetAuthoringAssetV1* asset(
        const std::string& handle) const;
    std::vector<AssetAuthoringAssetV1> assets() const;
    const AssetAuthoringStateV1& state() const;
    std::size_t sessionCount() const;

private:
    struct Session {
        std::string label;
        std::string bytes;
        std::uint64_t nextSequence = 0U;
    };

    bool persist(const AssetAuthoringStateV1& candidate);
    std::string nextSessionId() const;

    const VerifiedAssetStore* m_verifiedAssets = nullptr;
    std::unique_ptr<AssetAuthoringStore> m_store;
    AssetAuthoringStateV1 m_state;
    std::map<std::string, Session> m_sessions;
    bool m_ready = false;
};

} // namespace palace
