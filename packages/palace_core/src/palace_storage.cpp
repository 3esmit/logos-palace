#include "palace_storage.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <limits>

namespace palace {
namespace {

constexpr std::uint64_t kMaxEncodedBytes = 10U * 1024U * 1024U;
constexpr std::uint32_t kMaxDimension = 4096U;
constexpr std::uint64_t kMaxPixels = 16U * 1024U * 1024U;

bool isLowerHexDigest(const std::string &value) {
  if (value.size() != 64U)
    return false;
  for (const unsigned char character : value) {
    if (!((character >= '0' && character <= '9') ||
          (character >= 'a' && character <= 'f'))) {
      return false;
    }
  }
  return true;
}

} // namespace

bool isSafePalaceCid(const std::string &value) {
  if (value.size() < 2U || value.size() > 128U)
    return false;
  for (const unsigned char character : value) {
    if (!((character >= '0' && character <= '9')
          || (character >= 'A' && character <= 'Z')
          || (character >= 'a' && character <= 'z')))
      return false;
  }
  return true;
}

StorageUploadTerminal parseStorageUploadDone(const std::string &payload) {
  const QJsonDocument document =
      QJsonDocument::fromJson(QByteArray::fromStdString(payload));
  if (!document.isObject())
    return {};

  const QJsonObject object = document.object();
  const QJsonValue success = object.value(QStringLiteral("success"));
  const QJsonValue session = object.value(QStringLiteral("sessionId"));
  if (!success.isBool() || !session.isString())
    return {};
  const std::string sessionId = session.toString().toStdString();
  if (sessionId.empty() || sessionId.size() > 256U)
    return {};

  if (!success.toBool())
    return {true, false, sessionId, {}};
  const QJsonValue cid = object.value(QStringLiteral("cid"));
  if (!cid.isString() || !isSafePalaceCid(cid.toString().toStdString()))
    return {};
  return {true, true, sessionId, cid.toString().toStdString()};
}

namespace {

bool isExpectedPng(const AssetRefV1 &reference) {
  if (!isSafePalaceCid(reference.sourceCid) || !isSafePalaceCid(reference.derivativeCid) ||
      reference.byteLength == 0U || reference.byteLength > kMaxEncodedBytes ||
      reference.mediaType != "image/png" ||
      reference.technicalProfile != "palace-png-v1" ||
      !isLowerHexDigest(reference.contentSha256) || reference.width == 0U ||
      reference.height == 0U || reference.width > kMaxDimension ||
      reference.height > kMaxDimension) {
    return false;
  }
  return static_cast<std::uint64_t>(reference.width) * reference.height <=
         kMaxPixels;
}

} // namespace

std::optional<PendingStorageAsset>
StorageAssetQueue::begin(const AssetRefV1 &reference,
                         const std::string &destinationDirectory) {
  if (!isExpectedPng(reference) || destinationDirectory.empty() ||
      m_pendingByDerivativeCid.find(reference.derivativeCid) !=
          m_pendingByDerivativeCid.end() ||
      m_nextOperation == std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }

  PendingStorageAsset pending;
  pending.operationId = "palace-asset-" + std::to_string(m_nextOperation++);
  pending.reference = reference;
  pending.networkCid = reference.sourceCid;
  pending.destinationPath =
      destinationDirectory + "/" + pending.operationId + ".png";
  m_pending.emplace(pending.operationId, pending);
  m_pendingByDerivativeCid.emplace(reference.derivativeCid,
                                   pending.operationId);
  return pending;
}

std::optional<PendingStorageAsset>
StorageAssetQueue::take(const std::string &operationId) {
  const auto found = m_pending.find(operationId);
  if (found == m_pending.end())
    return std::nullopt;

  PendingStorageAsset pending = found->second;
  m_pendingByDerivativeCid.erase(pending.reference.derivativeCid);
  m_pending.erase(found);
  return pending;
}

bool StorageAssetQueue::cancel(const std::string &operationId) {
  return take(operationId).has_value();
}

} // namespace palace
