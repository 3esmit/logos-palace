#include "palace_asset_authoring_store.h"

#include "palace_sha256.h"
#include "palace_storage_cid.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <charconv>
#include <sstream>
#include <utility>
#include <vector>

namespace palace {
namespace {

constexpr char kFileName[] = "asset-authoring-v1";
constexpr char kHeader[] = "logos-palace-asset-authoring-v1";
constexpr qsizetype kMaximumRecordBytes = 128 * 1024;
constexpr std::uint32_t kMaximumDimension = 4096U;
constexpr std::uint64_t kMaximumPixels = 16U * 1024U * 1024U;

bool isUnder(const QString& path, const QString& root)
{
    return !path.isEmpty() && !root.isEmpty()
        && (path == root
            || path.startsWith(root + QLatin1Char('/')));
}

bool isLowerHex64(const std::string& value)
{
    return value.size() == 64U
        && std::all_of(
            value.begin(), value.end(),
            [](const unsigned char character) {
                return (character >= '0' && character <= '9')
                    || (character >= 'a' && character <= 'f');
            });
}

bool validLabel(const std::string& label)
{
    if (label.empty() || label.size() > 128U
        || label.find('\0') != std::string::npos) {
        return false;
    }
    const QByteArray encoded(
        label.data(), static_cast<qsizetype>(label.size()));
    if (QString::fromUtf8(encoded).toUtf8() != encoded)
        return false;
    return std::none_of(
        label.begin(), label.end(), [](const unsigned char value) {
            return value < 0x20U || value == 0x7fU;
        });
}

bool validReview(const std::string& value)
{
    return value == "pending"
        || value == "approved"
        || value == "rejected";
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

bool parseUnsigned(
    const std::string& encoded,
    std::uint64_t& value)
{
    if (encoded.empty())
        return false;
    const auto parsed = std::from_chars(
        encoded.data(),
        encoded.data() + encoded.size(),
        value);
    return parsed.ec == std::errc()
        && parsed.ptr == encoded.data() + encoded.size()
        && encoded == std::to_string(value);
}

std::vector<std::string> splitExact(
    const std::string& value,
    char delimiter,
    std::size_t expected)
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
    return fields.size() == expected
        ? fields : std::vector<std::string>{};
}

std::vector<std::string> lines(const std::string& encoded)
{
    if (encoded.empty() || encoded.back() != '\n')
        return {};
    std::vector<std::string> result;
    std::size_t cursor = 0U;
    while (cursor < encoded.size()) {
        const std::size_t next = encoded.find('\n', cursor);
        if (next == std::string::npos || next == cursor)
            return {};
        result.push_back(encoded.substr(cursor, next - cursor));
        cursor = next + 1U;
    }
    return result;
}

std::string encodeLabel(const std::string& label)
{
    return QByteArray(
               label.data(),
               static_cast<qsizetype>(label.size()))
        .toBase64(
            QByteArray::Base64UrlEncoding
                | QByteArray::OmitTrailingEquals)
        .toStdString();
}

bool decodeLabel(
    const std::string& encoded,
    std::string& label)
{
    if (encoded.empty() || encoded.size() > 172U)
        return false;
    const QByteArray input(
        encoded.data(), static_cast<qsizetype>(encoded.size()));
    const QByteArray::FromBase64Result decoded =
        QByteArray::fromBase64Encoding(
            input,
            QByteArray::Base64UrlEncoding
                | QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded
        || decoded.decoded.toBase64(
               QByteArray::Base64UrlEncoding
                   | QByteArray::OmitTrailingEquals)
            != input) {
        return false;
    }
    label.assign(
        decoded.decoded.constData(),
        static_cast<std::size_t>(decoded.decoded.size()));
    return validLabel(label);
}

bool validate(const AssetAuthoringStateV1& state)
{
    if (state.assets.size()
            > AssetAuthoringCatalog::MaximumAssets
        || state.roomAssignments.size() > 2U) {
        return false;
    }
    for (const auto& [handle, asset] : state.assets) {
        std::string cidDigest;
        if (handle != asset.handle
            || !isLowerHex64(handle)
            || !validLabel(asset.label)
            || asset.width == 0U
            || asset.height == 0U
            || asset.width > kMaximumDimension
            || asset.height > kMaximumDimension
            || static_cast<std::uint64_t>(asset.width)
                    * asset.height
                > kMaximumPixels
            || asset.byteLength == 0U
            || asset.byteLength
                > AssetAuthoringCatalog::MaximumAssetBytes
            || !validReview(asset.reviewState)
            || (!asset.publishedCid.empty()
                && (asset.reviewState != "approved"
                    || !canonicalStorageCidV1Sha256(
                        asset.publishedCid, cidDigest)
                    || cidDigest != handle))) {
            return false;
        }
    }
    for (const auto& [roomId, handle] : state.roomAssignments) {
        const auto asset = state.assets.find(handle);
        if (!AssetAuthoringStore::isKnownRoom(roomId)
            || asset == state.assets.end()
            || asset->second.reviewState != "approved"
            || asset->second.publishedCid.empty()) {
            return false;
        }
    }
    if (state.propAssignment.has_value()) {
        const AssetAuthoringPropAssignmentV1& prop =
            *state.propAssignment;
        const auto asset = state.assets.find(prop.handle);
        if (!isIdentifier(prop.propId)
            || !isIdentifier(prop.layer)
            || asset == state.assets.end()
            || asset->second.reviewState != "approved"
            || asset->second.publishedCid.empty()
            || prop.anchorX >= asset->second.width
            || prop.anchorY >= asset->second.height) {
            return false;
        }
    }
    if (state.bundleLocked
        && (state.roomAssignments.size() != 2U
            || state.roomAssignments.find("atrium")
                == state.roomAssignments.end()
            || state.roomAssignments.find("lounge")
                == state.roomAssignments.end())) {
        return false;
    }
    return true;
}

std::string serialize(const AssetAuthoringStateV1& state)
{
    if (!validate(state))
        return {};
    std::ostringstream body;
    body << kHeader << '\n'
         << "version=1\n"
         << "locked=" << (state.bundleLocked ? 1 : 0) << '\n'
         << "assets=" << state.assets.size() << '\n';
    for (const auto& [handle, asset] : state.assets) {
        body << "asset=" << handle
             << ';' << encodeLabel(asset.label)
             << ';' << asset.width
             << ';' << asset.height
             << ';' << asset.byteLength
             << ';' << asset.reviewState
             << ';' << (asset.publishedCid.empty()
                    ? "-" : asset.publishedCid)
             << '\n';
    }
    for (const std::string roomId : {
             std::string("atrium"),
             std::string("lounge"),
         }) {
        const auto assigned = state.roomAssignments.find(roomId);
        body << "assignment=" << roomId << ';'
             << (assigned == state.roomAssignments.end()
                    ? "-" : assigned->second)
             << '\n';
    }
    if (!state.propAssignment.has_value()) {
        body << "prop=-\n";
    } else {
        const AssetAuthoringPropAssignmentV1& prop =
            *state.propAssignment;
        body << "prop=" << prop.propId
             << ';' << prop.handle
             << ';' << prop.anchorX
             << ';' << prop.anchorY
             << ';' << encodeLabel(prop.layer)
             << '\n';
    }
    const std::string content = body.str();
    return content + "checksum="
        + crypto::sha256Hex(content) + '\n';
}

bool parse(
    const std::string& encoded,
    AssetAuthoringStateV1& state)
{
    const std::vector<std::string> recordLines = lines(encoded);
    if (recordLines.size() < 8U
        || recordLines[0] != kHeader
        || recordLines[1] != "version=1"
        || (recordLines[2] != "locked=0"
            && recordLines[2] != "locked=1")
        || recordLines[3].rfind("assets=", 0U) != 0U
        || recordLines.back().rfind("checksum=", 0U) != 0U) {
        return false;
    }
    std::uint64_t assetCount = 0U;
    if (!parseUnsigned(
            recordLines[3].substr(7U), assetCount)
        || assetCount
            > AssetAuthoringCatalog::MaximumAssets
        || recordLines.size() != assetCount + 8U) {
        return false;
    }
    const std::size_t checksumOffset =
        encoded.rfind("checksum=");
    if (checksumOffset == std::string::npos
        || recordLines.back().substr(9U)
            != crypto::sha256Hex(
                encoded.substr(0U, checksumOffset))) {
        return false;
    }

    AssetAuthoringStateV1 candidate;
    candidate.bundleLocked = recordLines[2] == "locked=1";
    for (std::uint64_t index = 0U;
         index < assetCount; ++index) {
        const std::string& line = recordLines[index + 4U];
        if (line.rfind("asset=", 0U) != 0U)
            return false;
        const std::vector<std::string> fields =
            splitExact(line.substr(6U), ';', 7U);
        AssetAuthoringAssetV1 asset;
        std::uint64_t width = 0U;
        std::uint64_t height = 0U;
        if (fields.empty()
            || !decodeLabel(fields[1], asset.label)
            || !parseUnsigned(fields[2], width)
            || !parseUnsigned(fields[3], height)
            || width > UINT32_MAX
            || height > UINT32_MAX
            || !parseUnsigned(fields[4], asset.byteLength)) {
            return false;
        }
        asset.handle = fields[0];
        asset.width = static_cast<std::uint32_t>(width);
        asset.height = static_cast<std::uint32_t>(height);
        asset.reviewState = fields[5];
        if (fields[6] != "-")
            asset.publishedCid = fields[6];
        if (!candidate.assets.emplace(
                asset.handle, std::move(asset)).second) {
            return false;
        }
    }
    for (std::size_t offset = 0U; offset < 2U; ++offset) {
        const std::string& line =
            recordLines[assetCount + 4U + offset];
        if (line.rfind("assignment=", 0U) != 0U)
            return false;
        const std::vector<std::string> fields =
            splitExact(line.substr(11U), ';', 2U);
        const std::string expected =
            offset == 0U ? "atrium" : "lounge";
        if (fields.empty() || fields[0] != expected)
            return false;
        if (fields[1] != "-")
            candidate.roomAssignments.emplace(fields[0], fields[1]);
    }
    const std::string& propLine =
        recordLines[assetCount + 6U];
    if (propLine == "prop=-") {
        candidate.propAssignment.reset();
    } else {
        if (propLine.rfind("prop=", 0U) != 0U)
            return false;
        const std::vector<std::string> fields =
            splitExact(propLine.substr(5U), ';', 5U);
        AssetAuthoringPropAssignmentV1 prop;
        std::uint64_t anchorX = 0U;
        std::uint64_t anchorY = 0U;
        if (fields.empty()
            || !parseUnsigned(fields[2], anchorX)
            || !parseUnsigned(fields[3], anchorY)
            || anchorX > UINT32_MAX
            || anchorY > UINT32_MAX
            || !decodeLabel(fields[4], prop.layer)) {
            return false;
        }
        prop.propId = fields[0];
        prop.handle = fields[1];
        prop.anchorX =
            static_cast<std::uint32_t>(anchorX);
        prop.anchorY =
            static_cast<std::uint32_t>(anchorY);
        candidate.propAssignment = std::move(prop);
    }
    if (!validate(candidate))
        return false;
    state = std::move(candidate);
    return true;
}

bool ownerOnly(const QFileInfo& file)
{
    const QFileDevice::Permissions permissions =
        file.permissions();
    constexpr QFileDevice::Permissions required =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    constexpr QFileDevice::Permissions forbidden =
        QFileDevice::ExeOwner
        | QFileDevice::ReadGroup | QFileDevice::WriteGroup
        | QFileDevice::ExeGroup | QFileDevice::ReadOther
        | QFileDevice::WriteOther | QFileDevice::ExeOther;
    return (permissions & required) == required
        && (permissions & forbidden) == 0;
}

} // namespace

AssetAuthoringStore::AssetAuthoringStore(
    std::string instanceRoot)
{
    const QString canonical =
        QDir(QString::fromStdString(instanceRoot))
            .canonicalPath();
    if (!canonical.isEmpty())
        m_instanceRoot = canonical.toStdString();
}

AssetAuthoringStoreStatus AssetAuthoringStore::load(
    AssetAuthoringStateV1& state) const
{
    if (m_instanceRoot.empty())
        return AssetAuthoringStoreStatus::InvalidArgument;
    const QString root = QString::fromStdString(m_instanceRoot);
    if (QDir(root).canonicalPath() != root)
        return AssetAuthoringStoreStatus::InsecurePath;
    const QString path =
        root + QLatin1Char('/') + QString::fromLatin1(kFileName);
    const QFileInfo file(path);
    if (file.isSymLink())
        return AssetAuthoringStoreStatus::InsecurePath;
    if (!file.exists())
        return AssetAuthoringStoreStatus::NotFound;
    const QString canonical = file.canonicalFilePath();
    if (!file.isFile() || !isUnder(canonical, root)
        || !ownerOnly(file)) {
        return AssetAuthoringStoreStatus::InsecurePath;
    }

    QFile input(canonical);
    if (!input.open(QIODevice::ReadOnly))
        return AssetAuthoringStoreStatus::IoError;
    const QByteArray bytes =
        input.read(kMaximumRecordBytes + 1);
    if (!input.atEnd()
        || bytes.size() > kMaximumRecordBytes) {
        return AssetAuthoringStoreStatus::InvalidRecord;
    }
    AssetAuthoringStateV1 candidate;
    if (!parse(
            std::string(
                bytes.constData(),
                static_cast<std::size_t>(bytes.size())),
            candidate)) {
        return AssetAuthoringStoreStatus::InvalidRecord;
    }
    state = std::move(candidate);
    return AssetAuthoringStoreStatus::Loaded;
}

AssetAuthoringStoreStatus AssetAuthoringStore::save(
    const AssetAuthoringStateV1& state) const
{
    const std::string encoded = serialize(state);
    if (m_instanceRoot.empty() || encoded.empty()
        || encoded.size()
            > static_cast<std::size_t>(kMaximumRecordBytes)) {
        return AssetAuthoringStoreStatus::InvalidArgument;
    }
    const QString root = QString::fromStdString(m_instanceRoot);
    if (QDir(root).canonicalPath() != root)
        return AssetAuthoringStoreStatus::InsecurePath;
    const QString path =
        root + QLatin1Char('/') + QString::fromLatin1(kFileName);
    const QFileInfo prior(path);
    if (prior.isSymLink()
        || (prior.exists()
            && (!prior.isFile()
                || !isUnder(
                    prior.canonicalFilePath(), root)))) {
        return AssetAuthoringStoreStatus::InsecurePath;
    }

    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)
        || !output.setPermissions(
            QFileDevice::ReadOwner
                | QFileDevice::WriteOwner)) {
        output.cancelWriting();
        return AssetAuthoringStoreStatus::IoError;
    }
    if (output.write(
            encoded.data(),
            static_cast<qint64>(encoded.size()))
            != static_cast<qint64>(encoded.size())
        || !output.commit()) {
        return AssetAuthoringStoreStatus::IoError;
    }
    const QFileInfo committed(path);
    if (committed.isSymLink() || !committed.isFile()
        || !isUnder(committed.canonicalFilePath(), root)
        || !ownerOnly(committed)) {
        return AssetAuthoringStoreStatus::InsecurePath;
    }
    return AssetAuthoringStoreStatus::Saved;
}

bool AssetAuthoringStore::isKnownRoom(
    const std::string& roomId)
{
    return roomId == "atrium" || roomId == "lounge";
}

const char* assetAuthoringStoreStatusName(
    AssetAuthoringStoreStatus status)
{
    switch (status) {
    case AssetAuthoringStoreStatus::Saved:
        return "saved";
    case AssetAuthoringStoreStatus::Loaded:
        return "loaded";
    case AssetAuthoringStoreStatus::NotFound:
        return "not-found";
    case AssetAuthoringStoreStatus::InvalidArgument:
        return "invalid-argument";
    case AssetAuthoringStoreStatus::InvalidRecord:
        return "invalid-record";
    case AssetAuthoringStoreStatus::InsecurePath:
        return "insecure-path";
    case AssetAuthoringStoreStatus::IoError:
        return "io-error";
    }
    return "invalid-record";
}

} // namespace palace
