#include "palace_storage_mvp.h"

#include "palace_sha256.h"
#include "palace_storage.h"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <utility>

namespace palace {
namespace {

constexpr char kCatalogHeader[] =
    "logos-palace-mvp-storage-catalog-v1";
constexpr char kAtriumDigest[] =
    "3bd13dc41f3e27a7eabf45c73188498b95e3e5e475e6fcb967308477afd522be";
constexpr char kLoungeDigest[] =
    "d2068f9cc4848b29882e532580c2b455ef5d243e7b38f16d590937fbef720486";
constexpr std::size_t kArtifactCount = 11U;
constexpr std::size_t kMaximumCatalogBytes = 16U * 1024U;
constexpr std::size_t kMaximumLeafBytes = 10U * 1024U * 1024U;

const std::vector<std::string>& canonicalObjectOrder()
{
    static const std::vector<std::string> order{
        "background-atrium",
        "background-lounge",
        "prop-hat-image",
        "prop-hat-metadata",
        "room-atrium-metadata",
        "room-lounge-metadata",
        "script-door",
        "prop-hat",
        "room-atrium",
        "room-lounge",
        "palace-1",
    };
    return order;
}

std::string propPng()
{
    static constexpr unsigned char bytes[] = {
        0x89U, 0x50U, 0x4eU, 0x47U, 0x0dU, 0x0aU, 0x1aU, 0x0aU,
        0x00U, 0x00U, 0x00U, 0x0dU, 0x49U, 0x48U, 0x44U, 0x52U,
        0x00U, 0x00U, 0x00U, 0x08U, 0x00U, 0x00U, 0x00U, 0x08U,
        0x08U, 0x06U, 0x00U, 0x00U, 0x00U, 0xc4U, 0x0fU, 0xbeU,
        0x8bU, 0x00U, 0x00U, 0x00U, 0x20U, 0x63U, 0x48U, 0x52U,
        0x4dU, 0x00U, 0x00U, 0x7aU, 0x26U, 0x00U, 0x00U, 0x80U,
        0x84U, 0x00U, 0x00U, 0xfaU, 0x00U, 0x00U, 0x00U, 0x80U,
        0xe8U, 0x00U, 0x00U, 0x75U, 0x30U, 0x00U, 0x00U, 0xeaU,
        0x60U, 0x00U, 0x00U, 0x3aU, 0x98U, 0x00U, 0x00U, 0x17U,
        0x70U, 0x9cU, 0xbaU, 0x51U, 0x3cU, 0x00U, 0x00U, 0x00U,
        0x06U, 0x62U, 0x4bU, 0x47U, 0x44U, 0x00U, 0xffU, 0x00U,
        0xffU, 0x00U, 0xffU, 0xa0U, 0xbdU, 0xa7U, 0x93U, 0x00U,
        0x00U, 0x00U, 0x0fU, 0x49U, 0x44U, 0x41U, 0x54U, 0x18U,
        0xd3U, 0x63U, 0x60U, 0x18U, 0x05U, 0x0cU, 0x0cU, 0x0cU,
        0x0cU, 0x00U, 0x01U, 0x08U, 0x00U, 0x01U, 0xc4U, 0x3aU,
        0x19U, 0x89U, 0x00U, 0x00U, 0x00U, 0x00U, 0x49U, 0x45U,
        0x4eU, 0x44U, 0xaeU, 0x42U, 0x60U, 0x82U,
    };
    return {
        reinterpret_cast<const char*>(bytes),
        sizeof(bytes),
    };
}

std::string propMetadata()
{
    return
        "logos-palace-prop-v1\n"
        "prop=hat\n"
        "image_object=prop-hat-image\n"
        "width=8\n"
        "height=8\n"
        "anchor_x=4\n"
        "anchor_y=7\n"
        "layer=head\n"
        "alpha=straight\n"
        "technical_profile=palace-png-v1\n";
}

std::string roomMetadata(
    const std::string& roomId,
    const std::string& backgroundObject,
    const std::string& targetRoomId)
{
    std::ostringstream encoded;
    encoded
        << "logos-palace-room-v1\n"
        << "room=" << roomId << '\n'
        << "canvas_width=640\n"
        << "canvas_height=480\n"
        << "background_object=" << backgroundObject << '\n'
        << "allowed_prop_set=prop-hat\n"
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
    const std::string& loungePng)
{
    if (m_initialized || !m_artifacts.empty()
        || atriumPng.empty() || loungePng.empty()
        || atriumPng.size() > kMaximumLeafBytes
        || loungePng.size() > kMaximumLeafBytes
        || crypto::sha256Hex(atriumPng) != kAtriumDigest
        || crypto::sha256Hex(loungePng) != kLoungeDigest) {
        return false;
    }

    const bool accepted =
        addLeaf(
            "background-atrium",
            PalaceStorageMvpArtifactType::BackgroundPng,
            "image/png",
            atriumPng)
        && addLeaf(
            "background-lounge",
            PalaceStorageMvpArtifactType::BackgroundPng,
            "image/png",
            loungePng)
        && addLeaf(
            "prop-hat-image",
            PalaceStorageMvpArtifactType::PropPng,
            "image/png",
            propPng())
        && addLeaf(
            "prop-hat-metadata",
            PalaceStorageMvpArtifactType::PropMetadata,
            "application/vnd.logos-palace.prop-v1",
            propMetadata())
        && addLeaf(
            "room-atrium-metadata",
            PalaceStorageMvpArtifactType::RoomMetadata,
            "application/vnd.logos-palace.room-v1",
            roomMetadata(
                "atrium", "background-atrium", "lounge"))
        && addLeaf(
            "room-lounge-metadata",
            PalaceStorageMvpArtifactType::RoomMetadata,
            "application/vnd.logos-palace.room-v1",
            roomMetadata(
                "lounge", "background-lounge", "atrium"))
        && addLeaf(
            "script-door",
            PalaceStorageMvpArtifactType::ScriptBundle,
            "application/vnd.logos-palace.script-v1",
            doorScript());
    if (!accepted) {
        m_artifacts.clear();
        return false;
    }
    m_initialized = true;
    return true;
}

bool PalaceStorageMvpBundle::initialized() const
{
    return m_initialized;
}

bool PalaceStorageMvpBundle::complete() const
{
    if (!m_initialized || m_artifacts.size() != kArtifactCount)
        return false;
    return std::all_of(
        m_artifacts.begin(),
        m_artifacts.end(),
        [](const auto& entry) { return !entry.second.cid.empty(); });
}

std::size_t PalaceStorageMvpBundle::artifactCount() const
{
    return kArtifactCount;
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
    std::vector<std::string> result;
    for (const std::string& objectId : canonicalObjectOrder()) {
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
    for (const std::string& objectId : canonicalObjectOrder()) {
        const auto found = m_artifacts.find(objectId);
        if (found != m_artifacts.end())
            result.push_back(found->second);
    }
    return result;
}

bool PalaceStorageMvpBundle::assignPublicationCid(
    const std::string& objectId,
    const std::string& cid)
{
    const auto found = m_artifacts.find(objectId);
    if (found == m_artifacts.end()
        || !found->second.cid.empty()
        || !isSafePalaceCid(cid)
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

std::string PalaceStorageMvpBundle::canonicalCatalog() const
{
    if (!complete())
        return {};
    std::ostringstream encoded;
    encoded
        << kCatalogHeader << '\n'
        << "version=1\n"
        << "root=palace-1\n"
        << "objects=" << kArtifactCount << '\n';
    for (const std::string& objectId : canonicalObjectOrder()) {
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
    if (!m_initialized || m_artifacts.size() != 7U
        || encoded.size() > kMaximumCatalogBytes) {
        return false;
    }
    const std::vector<std::string> lines = canonicalLines(encoded);
    if (lines.size() != 5U + kArtifactCount
        || lines[0] != kCatalogHeader
        || lines[1] != "version=1"
        || lines[2] != "root=palace-1"
        || lines[3] != "objects=11"
        || lines.back().rfind("checksum=", 0U) != 0U
        || !isDigest(lines.back().substr(9U))) {
        return false;
    }
    const std::size_t checksumLine =
        encoded.rfind("checksum=");
    if (checksumLine == std::string::npos
        || crypto::sha256Hex(encoded.substr(0U, checksumLine))
            != lines.back().substr(9U)) {
        return false;
    }

    std::vector<CatalogRecord> records;
    records.reserve(kArtifactCount);
    for (std::size_t index = 0U; index < kArtifactCount; ++index) {
        if (lines[index + 4U].rfind("object=", 0U) != 0U)
            return false;
        const std::vector<std::string> fields =
            splitExact(lines[index + 4U].substr(7U), ';', 6U);
        CatalogRecord record;
        if (fields.empty()
            || fields[0] != canonicalObjectOrder()[index]
            || !parseArtifactType(fields[1], record.type)
            || fields[2].empty() || fields[2].size() > 96U
            || !isSafePalaceCid(fields[3])
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
    const auto atrium = m_artifacts.find("background-atrium");
    const auto lounge = m_artifacts.find("background-lounge");
    if (atrium == m_artifacts.end()
        || lounge == m_artifacts.end()
        || !restored.initialize(
            atrium->second.bytes, lounge->second.bytes)) {
        return false;
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
    if (!m_initialized)
        return false;
    const auto published = [this](const std::string& objectId) {
        const auto found = m_artifacts.find(objectId);
        return found != m_artifacts.end()
            && !found->second.cid.empty();
    };

    if (m_artifacts.find("prop-hat") == m_artifacts.end()
        && published("prop-hat-image")
        && published("prop-hat-metadata")
        && !addManifest(
            "prop-hat",
            PalaceStorageMvpArtifactType::PropManifest,
            StorageCatalogObjectKind::PropManifest,
            {"prop-hat-image", "prop-hat-metadata"})) {
        return false;
    }
    if (published("prop-hat")) {
        if (m_artifacts.find("room-atrium") == m_artifacts.end()
            && published("background-atrium")
            && published("room-atrium-metadata")
            && published("script-door")
            && !addManifest(
                "room-atrium",
                PalaceStorageMvpArtifactType::RoomManifest,
                StorageCatalogObjectKind::RoomManifest,
                {
                    "background-atrium",
                    "prop-hat",
                    "room-atrium-metadata",
                    "script-door",
                })) {
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
                {
                    "background-lounge",
                    "prop-hat",
                    "room-lounge-metadata",
                    "script-door",
                })) {
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
