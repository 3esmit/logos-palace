#include "palace_asset_authoring.h"

#include "palace_asset_authoring_store.h"
#include "palace_storage_cid.h"
#include "palace_verified_asset_store.h"

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QUuid>

#include <algorithm>
#include <cctype>
#include <utility>

namespace palace {
namespace {

AssetAuthoringResult reject(const std::string& reason)
{
    AssetAuthoringResult result;
    result.reason = reason;
    return result;
}

AssetAuthoringResult accept(const std::string& reason)
{
    AssetAuthoringResult result;
    result.accepted = true;
    result.reason = reason;
    return result;
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

bool isSessionId(const std::string& value)
{
    return value.size() == 32U
        && std::all_of(
            value.begin(), value.end(),
            [](const unsigned char character) {
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

bool decodeCanonicalChunk(
    const std::string& canonicalBase64,
    QByteArray& decoded)
{
    constexpr std::size_t kMaximumEncodedChunk =
        ((AssetAuthoringCatalog::MaximumChunkBytes + 2U) / 3U)
        * 4U;
    if (canonicalBase64.empty()
        || canonicalBase64.size() > kMaximumEncodedChunk) {
        return false;
    }
    const QByteArray encoded(
        canonicalBase64.data(),
        static_cast<qsizetype>(canonicalBase64.size()));
    const QByteArray::FromBase64Result result =
        QByteArray::fromBase64Encoding(
            encoded,
            QByteArray::Base64Encoding
                | QByteArray::AbortOnBase64DecodingErrors);
    if (!result
        || result.decoded.isEmpty()
        || result.decoded.size()
            > static_cast<qsizetype>(
                AssetAuthoringCatalog::MaximumChunkBytes)
        || result.decoded.toBase64(
               QByteArray::Base64Encoding)
            != encoded) {
        return false;
    }
    decoded = result.decoded;
    return true;
}

} // namespace

AssetAuthoringCatalog::AssetAuthoringCatalog() = default;
AssetAuthoringCatalog::~AssetAuthoringCatalog() = default;
AssetAuthoringCatalog::AssetAuthoringCatalog(
    AssetAuthoringCatalog&&) noexcept = default;
AssetAuthoringCatalog& AssetAuthoringCatalog::operator=(
    AssetAuthoringCatalog&&) noexcept = default;

bool AssetAuthoringCatalog::initialize(
    const std::string& instanceRoot,
    const VerifiedAssetStore& verifiedAssets)
{
    m_ready = false;
    m_sessions.clear();
    m_state = {};
    m_verifiedAssets = &verifiedAssets;
    m_store =
        std::make_unique<AssetAuthoringStore>(instanceRoot);

    AssetAuthoringStateV1 loaded;
    const AssetAuthoringStoreStatus status =
        m_store->load(loaded);
    if (status != AssetAuthoringStoreStatus::Loaded
        && status != AssetAuthoringStoreStatus::NotFound) {
        return false;
    }
    if (status == AssetAuthoringStoreStatus::Loaded) {
        for (const auto& [handle, asset] : loaded.assets) {
            const auto path =
                verifiedAssets.verifiedPngPath(handle);
            if (!path.has_value()) {
                return false;
            }
            QFile input(QString::fromStdString(*path));
            if (!input.open(QIODevice::ReadOnly)) {
                return false;
            }
            const QByteArray bytes = input.read(
                static_cast<qint64>(MaximumAssetBytes + 1U));
            if (!input.atEnd()
                || bytes.isEmpty()
                || static_cast<std::uint64_t>(bytes.size())
                    != asset.byteLength) {
                return false;
            }
            const VerifiedAsset verified =
                verifiedAssets.stagePngBytes(
                    std::string(
                        bytes.constData(),
                        static_cast<std::size_t>(bytes.size())));
            if (!verified.accepted
                || verified.handle != handle
                || verified.width != asset.width
                || verified.height != asset.height) {
                return false;
            }
        }
        m_state = std::move(loaded);
    }
    m_ready = true;
    return true;
}

bool AssetAuthoringCatalog::ready() const
{
    return m_ready;
}

AssetAuthoringResult AssetAuthoringCatalog::begin(
    const std::string& label)
{
    if (!m_ready)
        return reject("asset-state-unavailable");
    if (!validLabel(label))
        return reject("asset-label-invalid");
    if (m_sessions.size() >= MaximumSessions)
        return reject("asset-session-limit");
    const std::string sessionId = nextSessionId();
    if (sessionId.empty())
        return reject("asset-session-id");

    Session session;
    session.label = label;
    if (!m_sessions.emplace(sessionId, std::move(session)).second)
        return reject("asset-session-id");
    AssetAuthoringResult result =
        accept("asset-session-begun");
    result.sessionId = sessionId;
    return result;
}

AssetAuthoringResult AssetAuthoringCatalog::append(
    const std::string& sessionId,
    std::uint64_t sequence,
    const std::string& canonicalBase64)
{
    if (!m_ready)
        return reject("asset-state-unavailable");
    if (!isSessionId(sessionId))
        return reject("asset-session-invalid");
    const auto found = m_sessions.find(sessionId);
    if (found == m_sessions.end())
        return reject("asset-session-unknown");
    if (sequence != found->second.nextSequence)
        return reject("asset-chunk-sequence");

    QByteArray decoded;
    if (!decodeCanonicalChunk(canonicalBase64, decoded))
        return reject("asset-chunk-base64");
    if (found->second.bytes.size()
            > MaximumAssetBytes
                - static_cast<std::uint64_t>(decoded.size())) {
        return reject("asset-too-large");
    }

    found->second.bytes.append(
        decoded.constData(),
        static_cast<std::size_t>(decoded.size()));
    ++found->second.nextSequence;
    AssetAuthoringResult result =
        accept("asset-chunk-appended");
    result.sessionId = sessionId;
    result.nextSequence = found->second.nextSequence;
    result.byteLength = found->second.bytes.size();
    return result;
}

AssetAuthoringResult AssetAuthoringCatalog::commit(
    const std::string& sessionId)
{
    if (!m_ready || !m_verifiedAssets || !m_store)
        return reject("asset-state-unavailable");
    if (!isSessionId(sessionId))
        return reject("asset-session-invalid");
    const auto found = m_sessions.find(sessionId);
    if (found == m_sessions.end())
        return reject("asset-session-unknown");
    if (found->second.bytes.empty())
        return reject("asset-empty");

    const VerifiedAsset verified =
        m_verifiedAssets->stagePngBytes(found->second.bytes);
    if (!verified.accepted)
        return reject("asset-png-" + verified.reason);

    AssetAuthoringStateV1 candidate = m_state;
    const auto existing =
        candidate.assets.find(verified.handle);
    if (existing == candidate.assets.end()) {
        if (candidate.assets.size() >= MaximumAssets)
            return reject("asset-limit");
        AssetAuthoringAssetV1 asset;
        asset.handle = verified.handle;
        asset.label = found->second.label;
        asset.width = verified.width;
        asset.height = verified.height;
        asset.byteLength = found->second.bytes.size();
        candidate.assets.emplace(asset.handle, std::move(asset));
    } else if (existing->second.width != verified.width
               || existing->second.height != verified.height
               || existing->second.byteLength
                   != found->second.bytes.size()) {
        return reject("asset-existing-metadata");
    }
    if (!persist(candidate))
        return reject("asset-state-persistence");

    const std::uint64_t byteLength =
        found->second.bytes.size();
    m_sessions.erase(found);
    AssetAuthoringResult result =
        accept("asset-staged");
    result.handle = verified.handle;
    result.width = verified.width;
    result.height = verified.height;
    result.byteLength = byteLength;
    return result;
}

AssetAuthoringResult AssetAuthoringCatalog::cancel(
    const std::string& sessionId)
{
    if (!m_ready)
        return reject("asset-state-unavailable");
    if (!isSessionId(sessionId))
        return reject("asset-session-invalid");
    if (m_sessions.erase(sessionId) != 1U)
        return reject("asset-session-unknown");
    AssetAuthoringResult result =
        accept("asset-session-cancelled");
    result.sessionId = sessionId;
    return result;
}

AssetAuthoringResult AssetAuthoringCatalog::review(
    const std::string& handle,
    const std::string& decision)
{
    if (!m_ready)
        return reject("asset-state-unavailable");
    const auto found = m_state.assets.find(handle);
    if (found == m_state.assets.end())
        return reject("asset-unknown");
    const std::string reviewState =
        decision == "approve"
        ? "approved"
        : decision == "reject" ? "rejected" : std::string{};
    if (reviewState.empty())
        return reject("asset-review-decision");
    if (!found->second.publishedCid.empty()
        && found->second.reviewState != reviewState) {
        return reject("asset-review-locked");
    }
    if (found->second.reviewState == reviewState)
        return accept(reviewState);

    AssetAuthoringStateV1 candidate = m_state;
    candidate.assets.at(handle).reviewState = reviewState;
    if (!persist(candidate))
        return reject("asset-state-persistence");
    return accept(reviewState);
}

AssetAuthoringResult AssetAuthoringCatalog::recordPublishedCid(
    const std::string& handle,
    const std::string& cid)
{
    if (!m_ready)
        return reject("asset-state-unavailable");
    const auto found = m_state.assets.find(handle);
    if (found == m_state.assets.end())
        return reject("asset-unknown");
    if (found->second.reviewState != "approved")
        return reject("asset-not-approved");
    std::string digest;
    if (!canonicalStorageCidV1Sha256(cid, digest)
        || digest != handle) {
        return reject("asset-cid-invalid");
    }
    if (found->second.publishedCid == cid)
        return accept("asset-published");
    if (!found->second.publishedCid.empty())
        return reject("asset-publication-locked");

    AssetAuthoringStateV1 candidate = m_state;
    candidate.assets.at(handle).publishedCid = cid;
    if (!persist(candidate))
        return reject("asset-state-persistence");
    return accept("asset-published");
}

AssetAuthoringResult AssetAuthoringCatalog::assign(
    const std::string& roomId,
    const std::string& handle)
{
    if (!m_ready)
        return reject("asset-state-unavailable");
    if (!AssetAuthoringStore::isKnownRoom(roomId))
        return reject("room-unknown");
    const auto asset = m_state.assets.find(handle);
    if (asset == m_state.assets.end())
        return reject("asset-unknown");
    if (asset->second.reviewState != "approved")
        return reject("asset-not-approved");
    if (asset->second.publishedCid.empty())
        return reject("asset-not-published");
    const auto current = m_state.roomAssignments.find(roomId);
    if (current != m_state.roomAssignments.end()
        && current->second == handle) {
        return accept("background-assigned");
    }
    if (m_state.bundleLocked)
        return reject("asset-assignment-locked");

    AssetAuthoringStateV1 candidate = m_state;
    candidate.roomAssignments[roomId] = handle;
    if (!persist(candidate))
        return reject("asset-state-persistence");
    return accept("background-assigned");
}

AssetAuthoringResult AssetAuthoringCatalog::assignProp(
    const std::string& propId,
    const std::string& handle,
    std::uint32_t anchorX,
    std::uint32_t anchorY,
    const std::string& layer)
{
    if (!m_ready)
        return reject("asset-state-unavailable");
    if (!isIdentifier(propId) || !isIdentifier(layer))
        return reject("prop-assignment-invalid");
    const auto asset = m_state.assets.find(handle);
    if (asset == m_state.assets.end())
        return reject("asset-unknown");
    if (asset->second.reviewState != "approved")
        return reject("asset-not-approved");
    if (asset->second.publishedCid.empty())
        return reject("asset-not-published");
    if (anchorX >= asset->second.width
        || anchorY >= asset->second.height) {
        return reject("prop-anchor-invalid");
    }

    AssetAuthoringPropAssignmentV1 assignment;
    assignment.propId = propId;
    assignment.handle = handle;
    assignment.anchorX = anchorX;
    assignment.anchorY = anchorY;
    assignment.layer = layer;
    if (m_state.propAssignment.has_value()
        && m_state.propAssignment->propId == assignment.propId
        && m_state.propAssignment->handle == assignment.handle
        && m_state.propAssignment->anchorX
            == assignment.anchorX
        && m_state.propAssignment->anchorY
            == assignment.anchorY
        && m_state.propAssignment->layer == assignment.layer) {
        return accept("prop-assigned");
    }
    if (m_state.bundleLocked)
        return reject("asset-assignment-locked");

    AssetAuthoringStateV1 candidate = m_state;
    candidate.propAssignment = std::move(assignment);
    if (!persist(candidate))
        return reject("asset-state-persistence");
    return accept("prop-assigned");
}

bool AssetAuthoringCatalog::lockAssignments()
{
    if (!m_ready
        || m_state.roomAssignments.size() != 2U
        || m_state.roomAssignments.find("atrium")
            == m_state.roomAssignments.end()
        || m_state.roomAssignments.find("lounge")
            == m_state.roomAssignments.end()) {
        return false;
    }
    if (m_state.bundleLocked)
        return true;
    AssetAuthoringStateV1 candidate = m_state;
    candidate.bundleLocked = true;
    return persist(candidate);
}

std::string AssetAuthoringCatalog::handleForRoom(
    const std::string& roomId) const
{
    const auto found = m_state.roomAssignments.find(roomId);
    return found == m_state.roomAssignments.end()
        ? std::string{} : found->second;
}

const AssetAuthoringAssetV1* AssetAuthoringCatalog::asset(
    const std::string& handle) const
{
    const auto found = m_state.assets.find(handle);
    return found == m_state.assets.end()
        ? nullptr : &found->second;
}

std::vector<AssetAuthoringAssetV1>
AssetAuthoringCatalog::assets() const
{
    std::vector<AssetAuthoringAssetV1> result;
    result.reserve(m_state.assets.size());
    for (const auto& [handle, asset] : m_state.assets) {
        (void)handle;
        result.push_back(asset);
    }
    return result;
}

const AssetAuthoringStateV1&
AssetAuthoringCatalog::state() const
{
    return m_state;
}

std::size_t AssetAuthoringCatalog::sessionCount() const
{
    return m_sessions.size();
}

bool AssetAuthoringCatalog::persist(
    const AssetAuthoringStateV1& candidate)
{
    if (!m_store
        || m_store->save(candidate)
            != AssetAuthoringStoreStatus::Saved) {
        m_ready = false;
        return false;
    }
    m_state = candidate;
    return true;
}

std::string AssetAuthoringCatalog::nextSessionId() const
{
    for (std::size_t attempt = 0U; attempt < 8U; ++attempt) {
        QString value =
            QUuid::createUuid().toString(QUuid::WithoutBraces);
        value.remove(QLatin1Char('-'));
        const std::string candidate = value.toStdString();
        if (isSessionId(candidate)
            && m_sessions.find(candidate) == m_sessions.end()) {
            return candidate;
        }
    }
    return {};
}

} // namespace palace
