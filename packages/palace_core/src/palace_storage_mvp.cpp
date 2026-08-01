#include "palace_storage_mvp.h"

#include "palace_sha256.h"
#include "palace_storage.h"
#include "palace_storage_cid.h"

#include <algorithm>
#include <charconv>
#include <optional>
#include <sstream>
#include <utility>

namespace palace {
namespace {

constexpr char kCatalogHeader[] =
    "logos-palace-mvp-storage-catalog-v1";
constexpr std::size_t kMaximumCatalogBytes = 16U * 1024U;
constexpr std::size_t kMaximumLeafBytes = 10U * 1024U * 1024U;

std::string roomMetadata(
    const std::string& roomId,
    const std::string& backgroundObject,
    const std::string& targetRoomId,
    const std::optional<std::string>& propId)
{
    std::ostringstream encoded;
    encoded
        << "logos-palace-room-v1\n"
        << "room=" << roomId << '\n'
        << "canvas_width=640\n"
        << "canvas_height=480\n"
        << "background_object=" << backgroundObject << '\n';
    if (propId.has_value())
        encoded << "allowed_prop_set=" << *propId << '\n';
    encoded
        << "script_bundle=script-door\n"
        << "spot=door;type=door;x=288;y=96;width=64;height=128;target="
        << targetRoomId << '\n';
    return encoded.str();
}

std::string doorScript()
{
    return
        "ON SELECT door\n"
        "SET door_open 1\n"
        "GOTOROOM lounge\n";
}

bool parseSize(const std::string& encoded, std::size_t& value)
{
    if (encoded.empty())
        return false;
    const auto parsed = std::from_chars(
        encoded.data(), encoded.data() + encoded.size(), value);
    return parsed.ec == std::errc()
        && parsed.ptr == encoded.data() + encoded.size()
        && encoded == std::to_string(value);
}

std::vector<std::string> splitExact(
    const std::string& value,
    char delimiter,
    std::size_t expectedFields)
{
    std::vector<std::string> fields;
    std::size_t cursor = 0U;
    while (true) {
        const std::size_t next = value.find(delimiter, cursor);
        fields.push_back(value.substr(cursor, next - cursor));
        if (next == std::string::npos)
            break;
        cursor = next + 1U;
    }
    return fields.size() == expectedFields
        ? fields : std::vector<std::string>{};
}

std::vector<std::string> canonicalLines(const std::string& value)
{
    if (value.empty() || value.back() != '\n')
        return {};
    std::vector<std::string> lines;
    std::size_t cursor = 0U;
    while (cursor < value.size()) {
        const std::size_t next = value.find('\n', cursor);
        if (next == std::string::npos || next == cursor)
            return {};
        lines.push_back(value.substr(cursor, next - cursor));
        cursor = next + 1U;
    }
    return lines;
}

bool isDigest(const std::string& value)
{
    return value.size() == 64U
        && std::all_of(
            value.begin(), value.end(), [](unsigned char character) {
                return (character >= '0' && character <= '9')
                    || (character >= 'a' && character <= 'f');
            });
}

bool isIdentifier(const std::string& value)
{
    if (value.empty() || value.size() > 64U
        || value.front() < 'a' || value.front() > 'z') {
        return false;
    }
    return std::all_of(
        value.begin() + 1, value.end(),
        [](const unsigned char character) {
            return (character >= 'a' && character <= 'z')
                || (character >= '0' && character <= '9')
                || character == '-' || character == '_';
        });
}

// The prop's graph identity is derived from the administrator-authored prop
// identifier. Protocol-owned room and script object identifiers stay stable;
// no compiled asset identity participates in the graph.
struct MvpStorageObjectIds {
    std::optional<std::string> propId;
    std::string propImage;
    std::string propMetadata;
    std::string propManifest;
    std::vector<std::string> leafOrder;
    std::vector<std::pair<
        PalaceStorageMvpArtifactType,
        std::string>> leafSpecifications;
    std::vector<std::string> canonicalOrder;
};

std::optional<MvpStorageObjectIds> objectIdsForProp(
    const std::optional<std::string>& propId)
{
    if (propId.has_value() && !isIdentifier(*propId))
        return std::nullopt;

    MvpStorageObjectIds ids;
    ids.propId = propId;
    ids.leafOrder = {
        "background-atrium",
        "background-lounge",
    };
    ids.leafSpecifications = {
        {
            PalaceStorageMvpArtifactType::BackgroundPng,
            "image/png",
        },
        {
            PalaceStorageMvpArtifactType::BackgroundPng,
            "image/png",
        },
    };
    if (propId.has_value()) {
        ids.propManifest = "prop-" + *propId;
        ids.propImage = ids.propManifest + "-image";
        ids.propMetadata = ids.propManifest + "-metadata";
        ids.leafOrder.insert(
            ids.leafOrder.end(), {
            ids.propImage,
            ids.propMetadata,
        });
        ids.leafSpecifications.insert(
            ids.leafSpecifications.end(), {
            {
                PalaceStorageMvpArtifactType::PropPng,
                "image/png",
            },
            {
                PalaceStorageMvpArtifactType::PropMetadata,
                "application/vnd.logos-palace.prop-v1",
            },
        });
    }
    ids.leafOrder.insert(
        ids.leafOrder.end(), {
        "room-atrium-metadata",
        "room-lounge-metadata",
        "script-door",
    });
    ids.leafSpecifications.insert(
        ids.leafSpecifications.end(), {
        {
            PalaceStorageMvpArtifactType::RoomMetadata,
            "application/vnd.logos-palace.room-v1",
        },
        {
            PalaceStorageMvpArtifactType::RoomMetadata,
            "application/vnd.logos-palace.room-v1",
        },
        {
            PalaceStorageMvpArtifactType::ScriptBundle,
            "application/vnd.logos-palace.script-v1",
        },
    });
    ids.canonicalOrder = ids.leafOrder;
    if (propId.has_value()) {
        ids.canonicalOrder.push_back(ids.propManifest);
    }
    ids.canonicalOrder.insert(
        ids.canonicalOrder.end(), {
        "room-atrium",
        "room-lounge",
        "palace-1",
    });
    return ids;
}

bool propIdForImageObject(
    const std::string& objectId,
    std::string& propId)
{
    static constexpr char kPrefix[] = "prop-";
    static constexpr char kSuffix[] = "-image";
    if (objectId.size()
            <= sizeof(kPrefix) - 1U + sizeof(kSuffix) - 1U
        || objectId.rfind(kPrefix, 0U) != 0U
        || objectId.compare(
               objectId.size() - (sizeof(kSuffix) - 1U),
               sizeof(kSuffix) - 1U, kSuffix)
            != 0) {
        return false;
    }
    const std::string candidate = objectId.substr(
        sizeof(kPrefix) - 1U,
        objectId.size()
            - (sizeof(kPrefix) - 1U)
            - (sizeof(kSuffix) - 1U));
    const auto ids = objectIdsForProp(
        std::optional<std::string>{candidate});
    if (!ids.has_value() || ids->propImage != objectId)
        return false;
    propId = candidate;
    return true;
}

std::string propMetadata(
    const std::string& propId,
    const std::string& imageObjectId,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t anchorX,
    std::uint32_t anchorY,
    const std::string& layer)
{
    std::ostringstream encoded;
    encoded
        << "logos-palace-prop-v1\n"
        << "prop=" << propId << '\n'
        << "image_object=" << imageObjectId << '\n'
        << "width=" << width << '\n'
        << "height=" << height << '\n'
        << "anchor_x=" << anchorX << '\n'
        << "anchor_y=" << anchorY << '\n'
        << "layer=" << layer << '\n'
        << "alpha=straight\n"
        << "technical_profile=palace-png-v1\n";
    return encoded.str();
}

bool pngDimensions(
    const std::string& bytes,
    std::uint32_t& width,
    std::uint32_t& height)
{
    static constexpr unsigned char signature[] = {
        0x89U, 0x50U, 0x4eU, 0x47U,
        0x0dU, 0x0aU, 0x1aU, 0x0aU,
    };
    if (bytes.size() < 24U
        || !std::equal(
            std::begin(signature), std::end(signature),
            reinterpret_cast<const unsigned char*>(
                bytes.data()))
        || bytes.substr(12U, 4U) != "IHDR") {
        return false;
    }
    const auto read =
        [&bytes](std::size_t offset) {
            return
                (static_cast<std::uint32_t>(
                    static_cast<unsigned char>(
                        bytes[offset])) << 24U)
                | (static_cast<std::uint32_t>(
                    static_cast<unsigned char>(
                        bytes[offset + 1U])) << 16U)
                | (static_cast<std::uint32_t>(
                    static_cast<unsigned char>(
                        bytes[offset + 2U])) << 8U)
                | static_cast<std::uint32_t>(
                    static_cast<unsigned char>(
                        bytes[offset + 3U]));
        };
    width = read(16U);
    height = read(20U);
    return width > 0U && height > 0U;
}

struct PropMetadataV1 {
    std::string propId;
    std::string imageObjectId;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t anchorX = 0U;
    std::uint32_t anchorY = 0U;
    std::string layer;
};

bool parsePropMetadata(
    const std::string& bytes,
    PropMetadataV1& metadata)
{
    const std::vector<std::string> decoded =
        canonicalLines(bytes);
    if (decoded.size() != 10U
        || decoded[0] != "logos-palace-prop-v1"
        || decoded[1].rfind("prop=", 0U) != 0U
        || decoded[2].rfind("image_object=", 0U) != 0U
        || decoded[3].rfind("width=", 0U) != 0U
        || decoded[4].rfind("height=", 0U) != 0U
        || decoded[5].rfind("anchor_x=", 0U) != 0U
        || decoded[6].rfind("anchor_y=", 0U) != 0U
        || decoded[7].rfind("layer=", 0U) != 0U
        || decoded[8] != "alpha=straight"
        || decoded[9]
            != "technical_profile=palace-png-v1") {
        return false;
    }
    std::size_t width = 0U;
    std::size_t height = 0U;
    std::size_t anchorX = 0U;
    std::size_t anchorY = 0U;
    metadata.propId = decoded[1].substr(5U);
    metadata.imageObjectId = decoded[2].substr(13U);
    metadata.layer = decoded[7].substr(6U);
    const auto ids = objectIdsForProp(
        std::optional<std::string>{metadata.propId});
    if (!ids.has_value()
        || metadata.imageObjectId != ids->propImage
        || !isIdentifier(metadata.layer)
        || !parseSize(decoded[3].substr(6U), width)
        || !parseSize(decoded[4].substr(7U), height)
        || !parseSize(decoded[5].substr(9U), anchorX)
        || !parseSize(decoded[6].substr(9U), anchorY)
        || width > UINT32_MAX || height > UINT32_MAX
        || anchorX > UINT32_MAX || anchorY > UINT32_MAX) {
        return false;
    }
    metadata.width = static_cast<std::uint32_t>(width);
    metadata.height = static_cast<std::uint32_t>(height);
    metadata.anchorX =
        static_cast<std::uint32_t>(anchorX);
    metadata.anchorY =
        static_cast<std::uint32_t>(anchorY);
    return metadata.anchorX < metadata.width
        && metadata.anchorY < metadata.height
        && bytes == propMetadata(
            metadata.propId,
            metadata.imageObjectId,
            metadata.width,
            metadata.height,
            metadata.anchorX,
            metadata.anchorY,
            metadata.layer);
}

bool parseArtifactType(
    const std::string& value,
    PalaceStorageMvpArtifactType& type)
{
    for (unsigned int raw = 0U; raw <= 7U; ++raw) {
        const auto candidate =
            static_cast<PalaceStorageMvpArtifactType>(raw);
        if (value == palaceStorageMvpArtifactTypeName(candidate)) {
            type = candidate;
            return true;
        }
    }
    return false;
}

struct CatalogRecord {
    std::string objectId;
    PalaceStorageMvpArtifactType type =
        PalaceStorageMvpArtifactType::BackgroundPng;
    std::string mediaType;
    std::string cid;
    std::size_t byteLength = 0U;
    std::string contentSha256;
};

} // namespace

const char* palaceStorageMvpFetchSourceName(
    PalaceStorageMvpFetchSource source)
{
    switch (source) {
    case PalaceStorageMvpFetchSource::Cache:
        return "cache";
    case PalaceStorageMvpFetchSource::Network:
        return "network";
    }
    return "network";
}

std::optional<PalaceStorageMvpFetchSource>
selectPalaceStorageMvpFetchSource(
    const std::vector<std::optional<bool>>& nativeCidAvailability,
    std::size_t expectedArtifactCount)
{
    if (expectedArtifactCount == 0U
        || nativeCidAvailability.size()
            != expectedArtifactCount
        || std::any_of(
            nativeCidAvailability.begin(),
            nativeCidAvailability.end(),
            [](const std::optional<bool>& available) {
                return !available.has_value();
            })) {
        return std::nullopt;
    }
    if (!std::all_of(
            nativeCidAvailability.begin(),
            nativeCidAvailability.end(),
            [](const std::optional<bool>& available) {
                return *available;
            })) {
        return PalaceStorageMvpFetchSource::Network;
    }
    return PalaceStorageMvpFetchSource::Cache;
}

const char* palaceStorageMvpArtifactTypeName(
    PalaceStorageMvpArtifactType type)
{
    switch (type) {
    case PalaceStorageMvpArtifactType::BackgroundPng:
        return "background_png";
    case PalaceStorageMvpArtifactType::PropPng:
        return "prop_png";
    case PalaceStorageMvpArtifactType::PropMetadata:
        return "prop_metadata";
    case PalaceStorageMvpArtifactType::ScriptBundle:
        return "script_bundle";
    case PalaceStorageMvpArtifactType::RoomMetadata:
        return "room_metadata";
    case PalaceStorageMvpArtifactType::PropManifest:
        return "prop_manifest";
    case PalaceStorageMvpArtifactType::RoomManifest:
        return "room_manifest";
    case PalaceStorageMvpArtifactType::PalaceManifest:
        return "palace_manifest";
    }
    return "invalid";
}

bool PalaceStorageMvpBundle::initialize(
    const std::string& atriumPng,
    const std::string& loungePng,
    const std::optional<PalaceStorageMvpPropInputV1>& prop)
{
    std::uint32_t decodedPropWidth = 0U;
    std::uint32_t decodedPropHeight = 0U;
    const std::optional<std::string> propId = prop.has_value()
        ? std::optional<std::string>{prop->propId}
        : std::nullopt;
    const auto ids = objectIdsForProp(propId);
    if (m_initialized || !m_artifacts.empty()
        || atriumPng.empty() || loungePng.empty()
        || atriumPng.size() > kMaximumLeafBytes
        || loungePng.size() > kMaximumLeafBytes
        || !ids.has_value()
        || (prop.has_value()
            && (prop->png.empty()
                || prop->png.size() > kMaximumLeafBytes
                || !isIdentifier(prop->layer)
                || !pngDimensions(
                    prop->png, decodedPropWidth,
                    decodedPropHeight)
                || decodedPropWidth != prop->width
                || decodedPropHeight != prop->height
                || prop->anchorX >= prop->width
                || prop->anchorY >= prop->height))) {
        return false;
    }

    m_graphPropId = ids->propId;
    bool accepted =
        addLeaf(
            "background-atrium",
            PalaceStorageMvpArtifactType::BackgroundPng,
            "image/png",
            atriumPng)
        && addLeaf(
            "background-lounge",
            PalaceStorageMvpArtifactType::BackgroundPng,
            "image/png",
            loungePng);
    if (accepted && prop.has_value()) {
        accepted = addLeaf(
            ids->propImage,
            PalaceStorageMvpArtifactType::PropPng,
            "image/png",
            prop->png)
            && addLeaf(
                ids->propMetadata,
                PalaceStorageMvpArtifactType::PropMetadata,
                "application/vnd.logos-palace.prop-v1",
                propMetadata(
                    *ids->propId, ids->propImage,
                    prop->width, prop->height,
                    prop->anchorX, prop->anchorY,
                    prop->layer));
    }
    accepted = accepted && addLeaf(
            "room-atrium-metadata",
            PalaceStorageMvpArtifactType::RoomMetadata,
            "application/vnd.logos-palace.room-v1",
            roomMetadata(
                "atrium", "background-atrium",
                "lounge", ids->propId))
        && addLeaf(
            "room-lounge-metadata",
            PalaceStorageMvpArtifactType::RoomMetadata,
            "application/vnd.logos-palace.room-v1",
            roomMetadata(
                "lounge", "background-lounge",
                "atrium", ids->propId))
        && addLeaf(
            "script-door",
            PalaceStorageMvpArtifactType::ScriptBundle,
            "application/vnd.logos-palace.script-v1",
            doorScript());
    if (!accepted) {
        m_artifacts.clear();
        m_graphPropId.reset();
        return false;
    }
    m_initialized = true;
    return true;
}

bool PalaceStorageMvpBundle::initialize(
    const std::string& atriumPng,
    const std::string& loungePng,
    const std::string& propPng,
    const std::string& propId,
    std::uint32_t propWidth,
    std::uint32_t propHeight,
    std::uint32_t anchorX,
    std::uint32_t anchorY,
    const std::string& layer)
{
    PalaceStorageMvpPropInputV1 input;
    input.png = propPng;
    input.propId = propId;
    input.width = propWidth;
    input.height = propHeight;
    input.anchorX = anchorX;
    input.anchorY = anchorY;
    input.layer = layer;
    return initialize(atriumPng, loungePng, input);
}

bool PalaceStorageMvpBundle::initialized() const
{
    return m_initialized;
}

bool PalaceStorageMvpBundle::complete() const
{
    const auto ids = objectIdsForProp(m_graphPropId);
    if (!m_initialized || !ids.has_value()
        || m_artifacts.size() != ids->canonicalOrder.size()) {
        return false;
    }
    return std::all_of(
        m_artifacts.begin(),
        m_artifacts.end(),
        [](const auto& entry) { return !entry.second.cid.empty(); });
}

std::size_t PalaceStorageMvpBundle::artifactCount() const
{
    return m_artifacts.size();
}

std::size_t PalaceStorageMvpBundle::publishedCount() const
{
    return static_cast<std::size_t>(std::count_if(
        m_artifacts.begin(),
        m_artifacts.end(),
        [](const auto& entry) { return !entry.second.cid.empty(); }));
}

std::vector<std::string>
PalaceStorageMvpBundle::stageableObjectIds()
{
    if (!m_initialized || !refreshDerivedArtifacts())
        return {};
    const auto ids = objectIdsForProp(m_graphPropId);
    if (!ids.has_value())
        return {};
    std::vector<std::string> result;
    for (const std::string& objectId : ids->canonicalOrder) {
        const auto found = m_artifacts.find(objectId);
        if (found != m_artifacts.end() && found->second.cid.empty())
            result.push_back(objectId);
    }
    return result;
}

const PalaceStorageMvpArtifactV1*
PalaceStorageMvpBundle::artifact(const std::string& objectId) const
{
    const auto found = m_artifacts.find(objectId);
    return found == m_artifacts.end() ? nullptr : &found->second;
}

std::vector<PalaceStorageMvpArtifactV1>
PalaceStorageMvpBundle::artifacts() const
{
    std::vector<PalaceStorageMvpArtifactV1> result;
    result.reserve(m_artifacts.size());
    const auto ids = objectIdsForProp(m_graphPropId);
    if (!ids.has_value())
        return result;
    for (const std::string& objectId : ids->canonicalOrder) {
        const auto found = m_artifacts.find(objectId);
        if (found != m_artifacts.end())
            result.push_back(found->second);
    }
    return result;
}

const PalaceStorageMvpArtifactV1*
PalaceStorageMvpBundle::fetchedPngArtifactForCid(
    const std::string& sourceCid) const
{
    if (!complete() || !fetchedContentValid()
        || !isCanonicalStorageCid(sourceCid)) {
        return nullptr;
    }

    for (const auto& entry : m_artifacts) {
        const PalaceStorageMvpArtifactV1& artifact = entry.second;
        if (artifact.cid != sourceCid)
            continue;
        if ((artifact.type
                != PalaceStorageMvpArtifactType::BackgroundPng
                && artifact.type
                    != PalaceStorageMvpArtifactType::PropPng)
            || artifact.mediaType != "image/png"
            || artifact.specification.kind
                != StorageCatalogObjectKind::Blob
            || artifact.bytes.empty()) {
            return nullptr;
        }
        return &artifact;
    }
    return nullptr;
}

bool PalaceStorageMvpBundle::assignPublicationCid(
    const std::string& objectId,
    const std::string& cid)
{
    const auto found = m_artifacts.find(objectId);
    if (found == m_artifacts.end()
        || !found->second.cid.empty()
        || !isCanonicalStorageCid(cid)
        || std::any_of(
            m_artifacts.begin(),
            m_artifacts.end(),
            [&cid](const auto& entry) {
                return entry.second.cid == cid;
            })) {
        return false;
    }
    found->second.cid = cid;
    return refreshDerivedArtifacts();
}

bool PalaceStorageMvpBundle::acceptFetchedBytes(
    const std::string& objectId,
    const std::string& bytes)
{
    const auto found = m_artifacts.find(objectId);
    if (found == m_artifacts.end()
        || bytes.empty()
        || bytes.size()
            != found->second.specification.byteLength
        || crypto::sha256Hex(bytes)
            != found->second.specification.contentSha256) {
        return false;
    }
    if (!found->second.bytes.empty())
        return found->second.bytes == bytes;
    if (found->second.specification.kind
        != StorageCatalogObjectKind::Blob) {
        return false;
    }
    found->second.bytes = bytes;
    return true;
}

bool PalaceStorageMvpBundle::fetchedContentValid() const
{
    const auto ids = objectIdsForProp(m_graphPropId);
    if (!ids.has_value())
        return false;
    const auto lookup =
        [this](const std::string& objectId)
            -> const PalaceStorageMvpArtifactV1* {
            const auto found = m_artifacts.find(objectId);
            return found == m_artifacts.end()
                ? nullptr : &found->second;
        };
    const PalaceStorageMvpArtifactV1* atrium =
        lookup("background-atrium");
    const PalaceStorageMvpArtifactV1* lounge =
        lookup("background-lounge");
    const PalaceStorageMvpArtifactV1* atriumMetadata =
        lookup("room-atrium-metadata");
    const PalaceStorageMvpArtifactV1* loungeMetadata =
        lookup("room-lounge-metadata");
    const PalaceStorageMvpArtifactV1* script =
        lookup("script-door");
    if (atrium == nullptr || lounge == nullptr
        || atriumMetadata == nullptr
        || loungeMetadata == nullptr || script == nullptr
        || atrium->bytes.empty() || lounge->bytes.empty()
        || atriumMetadata->bytes.empty()
        || loungeMetadata->bytes.empty()
        || script->bytes.empty()) {
        return false;
    }

    std::uint32_t atriumWidth = 0U;
    std::uint32_t atriumHeight = 0U;
    std::uint32_t loungeWidth = 0U;
    std::uint32_t loungeHeight = 0U;
    if (!pngDimensions(
               atrium->bytes, atriumWidth, atriumHeight)
        || !pngDimensions(
               lounge->bytes, loungeWidth, loungeHeight)
        || script->bytes != doorScript()) {
        return false;
    }
    if (!ids->propId.has_value()) {
        return atriumMetadata->bytes
                == roomMetadata(
                    "atrium", "background-atrium",
                    "lounge", ids->propId)
            && loungeMetadata->bytes
                == roomMetadata(
                    "lounge", "background-lounge",
                    "atrium", ids->propId);
    }

    const PalaceStorageMvpArtifactV1* propImage =
        lookup(ids->propImage);
    const PalaceStorageMvpArtifactV1* metadataArtifact =
        lookup(ids->propMetadata);
    if (propImage == nullptr || metadataArtifact == nullptr
        || propImage->bytes.empty()
        || metadataArtifact->bytes.empty()) {
        return false;
    }
    std::uint32_t propWidth = 0U;
    std::uint32_t propHeight = 0U;
    PropMetadataV1 metadata;
    return pngDimensions(
               propImage->bytes, propWidth, propHeight)
        && parsePropMetadata(
               metadataArtifact->bytes, metadata)
        && metadata.propId == *ids->propId
        && metadata.imageObjectId == ids->propImage
        && metadata.width == propWidth
        && metadata.height == propHeight
        && atriumMetadata->bytes
            == roomMetadata(
                "atrium", "background-atrium",
                "lounge", ids->propId)
        && loungeMetadata->bytes
            == roomMetadata(
                "lounge", "background-lounge",
                "atrium", ids->propId);
}

std::string PalaceStorageMvpBundle::propId() const
{
    const auto prop = propAsset();
    return prop.has_value()
        ? prop->propId : std::string{};
}

std::string PalaceStorageMvpBundle::propManifestObjectId() const
{
    const auto ids = objectIdsForProp(m_graphPropId);
    return ids.has_value()
        ? ids->propManifest : std::string{};
}

std::optional<PalaceStorageMvpPropAssetV1>
PalaceStorageMvpBundle::propAsset() const
{
    if (!fetchedContentValid())
        return std::nullopt;
    const auto ids = objectIdsForProp(m_graphPropId);
    if (!ids.has_value() || !ids->propId.has_value())
        return std::nullopt;
    const auto image =
        m_artifacts.find(ids->propImage);
    const auto found =
        m_artifacts.find(ids->propMetadata);
    PropMetadataV1 metadata;
    if (image == m_artifacts.end()
        || found == m_artifacts.end()
        || !parsePropMetadata(found->second.bytes, metadata)
        || metadata.propId != *ids->propId
        || metadata.imageObjectId != ids->propImage
        || !isDigest(
            image->second.specification.contentSha256)) {
        return std::nullopt;
    }
    PalaceStorageMvpPropAssetV1 result;
    result.propId = metadata.propId;
    result.handle =
        image->second.specification.contentSha256;
    result.width = metadata.width;
    result.height = metadata.height;
    result.anchorX = metadata.anchorX;
    result.anchorY = metadata.anchorY;
    result.layer = metadata.layer;
    return result;
}

std::string PalaceStorageMvpBundle::canonicalCatalog() const
{
    if (!complete())
        return {};
    const auto ids = objectIdsForProp(m_graphPropId);
    if (!ids.has_value())
        return {};
    std::ostringstream encoded;
    encoded
        << kCatalogHeader << '\n'
        << "version=1\n"
        << "root=palace-1\n"
        << "objects=" << m_artifacts.size() << '\n';
    for (const std::string& objectId : ids->canonicalOrder) {
        const PalaceStorageMvpArtifactV1& value =
            m_artifacts.at(objectId);
        encoded
            << "object=" << value.objectId
            << ';' << palaceStorageMvpArtifactTypeName(value.type)
            << ';' << value.mediaType
            << ';' << value.cid
            << ';' << value.specification.byteLength
            << ';' << value.specification.contentSha256
            << '\n';
    }
    const std::string body = encoded.str();
    return body + "checksum=" + crypto::sha256Hex(body) + '\n';
}

bool PalaceStorageMvpBundle::restoreCanonicalCatalog(
    const std::string& encoded)
{
    const auto localIds = objectIdsForProp(m_graphPropId);
    const bool localLeaves = m_initialized
        && localIds.has_value()
        && m_artifacts.size() == localIds->leafOrder.size();
    const bool visitorRestore =
        !m_initialized && m_artifacts.empty();
    if ((!localLeaves && !visitorRestore)
        || encoded.size() > kMaximumCatalogBytes) {
        return false;
    }
    const std::vector<std::string> lines = canonicalLines(encoded);
    if (lines.size() < 5U || lines[0] != kCatalogHeader
        || lines[1] != "version=1"
        || lines[2] != "root=palace-1"
        || lines[3].rfind("objects=", 0U) != 0U
        || lines.back().rfind("checksum=", 0U) != 0U
        || !isDigest(lines.back().substr(9U))) {
        return false;
    }
    std::size_t catalogObjectCount = 0U;
    if (!parseSize(lines[3].substr(8U), catalogObjectCount)
        || catalogObjectCount < 3U
        || catalogObjectCount > lines.size() - 5U
        || lines.size() - 5U != catalogObjectCount) {
        return false;
    }
    const std::size_t checksumLine =
        encoded.rfind("checksum=");
    if (checksumLine == std::string::npos
        || crypto::sha256Hex(encoded.substr(0U, checksumLine))
            != lines.back().substr(9U)) {
        return false;
    }

    const std::string firstVariantRecord =
        lines[4U + 2U];
    if (firstVariantRecord.rfind("object=", 0U) != 0U)
        return false;
    const std::vector<std::string> firstVariantFields =
        splitExact(firstVariantRecord.substr(7U), ';', 6U);
    if (firstVariantFields.empty())
        return false;
    std::string parsedPropId;
    std::optional<std::string> catalogPropId;
    if (propIdForImageObject(
            firstVariantFields[0], parsedPropId)) {
        catalogPropId = parsedPropId;
    } else if (firstVariantFields[0]
               != "room-atrium-metadata") {
        return false;
    }
    const auto catalogIds = objectIdsForProp(catalogPropId);
    if (!catalogIds.has_value()
        || catalogObjectCount
            != catalogIds->canonicalOrder.size()
        || (localLeaves
            && localIds->propId != catalogIds->propId)) {
        return false;
    }

    std::vector<CatalogRecord> records;
    records.reserve(catalogObjectCount);
    for (std::size_t index = 0U;
         index < catalogObjectCount; ++index) {
        if (lines[index + 4U].rfind("object=", 0U) != 0U)
            return false;
        const std::vector<std::string> fields =
            splitExact(lines[index + 4U].substr(7U), ';', 6U);
        CatalogRecord record;
        if (fields.empty()
            || fields[0] != catalogIds->canonicalOrder[index]
            || !parseArtifactType(fields[1], record.type)
            || fields[2].empty() || fields[2].size() > 96U
            || !isCanonicalStorageCid(fields[3])
            || !parseSize(fields[4], record.byteLength)
            || record.byteLength == 0U
            || record.byteLength > kMaximumLeafBytes
            || !isDigest(fields[5])) {
            return false;
        }
        record.objectId = fields[0];
        record.mediaType = fields[2];
        record.cid = fields[3];
        record.contentSha256 = fields[5];
        records.push_back(std::move(record));
    }

    PalaceStorageMvpBundle restored;
    if (localLeaves) {
        const auto atrium =
            m_artifacts.find("background-atrium");
        const auto lounge =
            m_artifacts.find("background-lounge");
        if (atrium == m_artifacts.end()
            || lounge == m_artifacts.end()) {
            return false;
        }
        if (!localIds->propId.has_value()) {
            if (!restored.initialize(
                    atrium->second.bytes,
                    lounge->second.bytes)) {
                return false;
            }
        } else {
            const auto propImage =
                m_artifacts.find(localIds->propImage);
            const auto propMetadataArtifact =
                m_artifacts.find(localIds->propMetadata);
            PropMetadataV1 metadata;
            if (propImage == m_artifacts.end()
                || propMetadataArtifact == m_artifacts.end()
                || !parsePropMetadata(
                    propMetadataArtifact->second.bytes,
                    metadata)
                || metadata.propId != *localIds->propId
                || metadata.imageObjectId != localIds->propImage
                || !restored.initialize(
                    atrium->second.bytes,
                    lounge->second.bytes,
                    propImage->second.bytes,
                    metadata.propId,
                    metadata.width,
                    metadata.height,
                    metadata.anchorX,
                    metadata.anchorY,
                    metadata.layer)) {
                return false;
            }
        }
    } else {
        if (catalogIds->leafSpecifications.size()
            != catalogIds->leafOrder.size()) {
            return false;
        }
        restored.m_graphPropId = catalogIds->propId;
        const auto addLeafPlaceholder =
            [&restored](
                const CatalogRecord& record,
                const std::pair<
                    PalaceStorageMvpArtifactType,
                    std::string>& expected) {
                if (record.type != expected.first
                    || record.mediaType != expected.second)
                    return false;
                PalaceStorageMvpArtifactV1 artifact;
                artifact.objectId = record.objectId;
                artifact.type = record.type;
                artifact.mediaType = record.mediaType;
                artifact.specification.objectId =
                    record.objectId;
                artifact.specification.kind =
                    StorageCatalogObjectKind::Blob;
                artifact.specification.byteLength =
                    record.byteLength;
                artifact.specification.contentSha256 =
                    record.contentSha256;
                return restored.m_artifacts.emplace(
                    artifact.objectId,
                    std::move(artifact)).second;
            };
        for (std::size_t index = 0U;
             index < catalogIds->leafOrder.size(); ++index) {
            if (!addLeafPlaceholder(
                    records[index],
                    catalogIds->leafSpecifications[index])) {
                return false;
            }
        }
        restored.m_initialized = true;
    }
    for (const CatalogRecord& record : records) {
        const PalaceStorageMvpArtifactV1* expected =
            restored.artifact(record.objectId);
        if (expected == nullptr
            || expected->type != record.type
            || expected->mediaType != record.mediaType
            || expected->specification.byteLength
                != record.byteLength
            || expected->specification.contentSha256
                != record.contentSha256
            || !restored.assignPublicationCid(
                record.objectId, record.cid)) {
            return false;
        }
    }
    if (!restored.complete()
        || restored.canonicalCatalog() != encoded) {
        return false;
    }
    *this = std::move(restored);
    return true;
}

bool PalaceStorageMvpBundle::refreshDerivedArtifacts()
{
    const auto ids = objectIdsForProp(m_graphPropId);
    if (!m_initialized || !ids.has_value())
        return false;
    const auto published = [this](const std::string& objectId) {
        const auto found = m_artifacts.find(objectId);
        return found != m_artifacts.end()
            && !found->second.cid.empty();
    };

    bool propReady = !ids->propId.has_value();
    if (ids->propId.has_value()) {
        if (m_artifacts.find(ids->propManifest)
                == m_artifacts.end()
            && published(ids->propImage)
            && published(ids->propMetadata)
            && !addManifest(
                ids->propManifest,
                PalaceStorageMvpArtifactType::PropManifest,
                StorageCatalogObjectKind::PropManifest,
                {ids->propImage, ids->propMetadata})) {
            return false;
        }
        propReady = published(ids->propManifest);
    }
    if (propReady) {
        std::vector<std::string> atriumChildren{
            "background-atrium",
            "room-atrium-metadata",
            "script-door",
        };
        std::vector<std::string> loungeChildren{
            "background-lounge",
            "room-lounge-metadata",
            "script-door",
        };
        if (ids->propId.has_value()) {
            atriumChildren.push_back(ids->propManifest);
            loungeChildren.push_back(ids->propManifest);
        }
        if (m_artifacts.find("room-atrium") == m_artifacts.end()
            && published("background-atrium")
            && published("room-atrium-metadata")
            && published("script-door")
            && !addManifest(
                "room-atrium",
                PalaceStorageMvpArtifactType::RoomManifest,
                StorageCatalogObjectKind::RoomManifest,
                atriumChildren)) {
            return false;
        }
        if (m_artifacts.find("room-lounge") == m_artifacts.end()
            && published("background-lounge")
            && published("room-lounge-metadata")
            && published("script-door")
            && !addManifest(
                "room-lounge",
                PalaceStorageMvpArtifactType::RoomManifest,
                StorageCatalogObjectKind::RoomManifest,
                loungeChildren)) {
            return false;
        }
    }
    if (m_artifacts.find("palace-1") == m_artifacts.end()
        && published("room-atrium")
        && published("room-lounge")
        && !addManifest(
            "palace-1",
            PalaceStorageMvpArtifactType::PalaceManifest,
            StorageCatalogObjectKind::PalaceManifest,
            {"room-atrium", "room-lounge"})) {
        return false;
    }
    return true;
}

bool PalaceStorageMvpBundle::addLeaf(
    const std::string& objectId,
    PalaceStorageMvpArtifactType type,
    const std::string& mediaType,
    const std::string& bytes)
{
    if (objectId.empty() || mediaType.empty() || bytes.empty()
        || bytes.size() > kMaximumLeafBytes) {
        return false;
    }
    PalaceStorageMvpArtifactV1 artifact;
    artifact.objectId = objectId;
    artifact.type = type;
    artifact.mediaType = mediaType;
    artifact.bytes = bytes;
    artifact.specification.objectId = objectId;
    artifact.specification.kind = StorageCatalogObjectKind::Blob;
    artifact.specification.byteLength = bytes.size();
    artifact.specification.contentSha256 =
        crypto::sha256Hex(bytes);
    return m_artifacts.emplace(
        objectId, std::move(artifact)).second;
}

bool PalaceStorageMvpBundle::addManifest(
    const std::string& objectId,
    PalaceStorageMvpArtifactType type,
    StorageCatalogObjectKind catalogKind,
    const std::vector<std::string>& childObjectIds)
{
    std::vector<StorageCatalogManifestChildV1> children;
    children.reserve(childObjectIds.size());
    for (const std::string& childObjectId : childObjectIds) {
        const auto found = m_artifacts.find(childObjectId);
        if (found == m_artifacts.end() || found->second.cid.empty())
            return false;
        StorageCatalogManifestChildV1 child;
        child.objectId = childObjectId;
        child.cid = found->second.cid;
        child.byteLength = found->second.specification.byteLength;
        child.contentSha256 =
            found->second.specification.contentSha256;
        children.push_back(std::move(child));
    }
    std::sort(
        children.begin(),
        children.end(),
        [](const auto& left, const auto& right) {
            return left.objectId < right.objectId;
        });
    const std::string bytes = canonicalStorageCatalogManifestV1(
        catalogKind, objectId, children);
    if (bytes.empty())
        return false;

    PalaceStorageMvpArtifactV1 artifact;
    artifact.objectId = objectId;
    artifact.type = type;
    artifact.mediaType =
        "application/vnd.logos-palace.catalog-manifest-v1";
    artifact.bytes = bytes;
    artifact.specification.objectId = objectId;
    artifact.specification.kind = catalogKind;
    artifact.specification.byteLength = bytes.size();
    artifact.specification.contentSha256 =
        crypto::sha256Hex(bytes);
    artifact.specification.children = std::move(children);
    return m_artifacts.emplace(
        objectId, std::move(artifact)).second;
}

} // namespace palace
