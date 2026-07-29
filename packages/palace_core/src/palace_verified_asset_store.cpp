#include "palace_verified_asset_store.h"

#include "palace_sha256.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSaveFile>

#include <algorithm>
#include <utility>

namespace palace {
namespace {

constexpr qint64 kMaxDecodedPixels = 16LL * 1024 * 1024;

bool isUnder(const QString& path, const QString& root)
{
    return !path.isEmpty() && !root.isEmpty()
        && (path == root || path.startsWith(root + QLatin1Char('/')));
}

bool hasSafeDecodedSize(const QSize& size)
{
    if (size.width() <= 0 || size.height() <= 0)
        return false;
    return static_cast<qint64>(size.width()) * size.height() <= kMaxDecodedPixels;
}

bool isLowerHexDigest(const std::string& value)
{
    return value.size() == 64U
        && std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return (character >= '0' && character <= '9')
                   || (character >= 'a' && character <= 'f');
           });
}

class QtPngDecoder final : public BoundedRasterDecoder {
public:
    bool decodePng(const std::string& encoded,
                   std::uint32_t& decodedWidth,
                   std::uint32_t& decodedHeight) const override
    {
        const QByteArray bytes(encoded.data(), static_cast<qsizetype>(encoded.size()));
        QBuffer buffer;
        buffer.setData(bytes);
        if (!buffer.open(QIODevice::ReadOnly))
            return false;

        QImageReader reader(&buffer);
        reader.setAutoTransform(false);
        if (!reader.canRead() || reader.format().toLower() != QByteArrayLiteral("png")
            || !hasSafeDecodedSize(reader.size())) {
            return false;
        }
        const QImage image = reader.read();
        if (image.isNull() || !hasSafeDecodedSize(image.size()))
            return false;
        decodedWidth = static_cast<std::uint32_t>(image.width());
        decodedHeight = static_cast<std::uint32_t>(image.height());
        return true;
    }
};

VerifiedAsset reject(const std::string& reason)
{
    return {false, {}, reason, 0U, 0U};
}

} // namespace

VerifiedAssetStore::VerifiedAssetStore(std::string instancePersistencePath)
    : m_instancePersistencePath(std::move(instancePersistencePath))
{
    const QString canonicalInstanceRoot = QDir(QString::fromStdString(m_instancePersistencePath)).canonicalPath();
    if (!canonicalInstanceRoot.isEmpty()) {
        m_instancePersistencePath = canonicalInstanceRoot.toStdString();
        m_directory = QDir::cleanPath(
            canonicalInstanceRoot + QStringLiteral("/verified_assets/logos_palace_ui")).toStdString();
    }
}

VerifiedAsset VerifiedAssetStore::stagePngBytes(
    const std::string& encoded) const
{
    constexpr std::size_t kMaximumPngBytes =
        10U * 1024U * 1024U;
    if (encoded.empty()
        || encoded.size() > kMaximumPngBytes) {
        return reject("encoded-png-size");
    }
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    if (!QtPngDecoder{}.decodePng(encoded, width, height))
        return reject("raster-decoder-rejected");

    const std::string digest = crypto::sha256Hex(encoded);
    AssetRefV1 reference;
    reference.sourceCid = digest;
    reference.derivativeCid = digest;
    reference.byteLength = encoded.size();
    reference.mediaType = "image/png";
    reference.width = width;
    reference.height = height;
    reference.technicalProfile = "palace-png-v1";
    reference.contentSha256 = digest;
    return stagePngDerivative(reference, encoded);
}

VerifiedAsset VerifiedAssetStore::stagePngDerivative(const AssetRefV1& reference,
                                                     const std::string& encoded) const
{
    if (m_instancePersistencePath.empty() || m_directory.empty())
        return reject("missing-instance-persistence-path");

    const VerifiedAsset verified = AssetCovenant().verifyPngDerivative(
        reference, encoded, QtPngDecoder{});
    if (!verified.accepted)
        return verified;

    const QString instanceRoot = QString::fromStdString(m_instancePersistencePath);
    const QString assetDirectory = QString::fromStdString(m_directory);
    if (!QDir().mkpath(assetDirectory))
        return reject("asset-directory-create-failed");

    const QString canonicalAssetDirectory = QDir(assetDirectory).canonicalPath();
    if (!isUnder(canonicalAssetDirectory, instanceRoot))
        return reject("asset-directory-escaped-instance-root");

    const QString destination = canonicalAssetDirectory + QLatin1Char('/') + QString::fromStdString(verified.handle)
        + QStringLiteral(".png");
    const QFileInfo existing(destination);
    if (existing.isSymLink())
        return reject("asset-destination-symlink");
    if (existing.exists()) {
        const QString canonicalExisting = existing.canonicalFilePath();
        if (!existing.isFile() || !isUnder(canonicalExisting, canonicalAssetDirectory))
            return reject("asset-destination-escaped-store");
        QFile file(canonicalExisting);
        if (!file.open(QIODevice::ReadOnly))
            return reject("asset-destination-read-failed");
        const QByteArray existingBytes = file.readAll();
        if (crypto::sha256Hex(std::string(existingBytes.constData(), static_cast<std::size_t>(existingBytes.size())))
            != verified.handle) {
            return reject("asset-destination-digest-mismatch");
        }
        return verified;
    }

    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(encoded.data(), static_cast<qint64>(encoded.size())) != static_cast<qint64>(encoded.size())
        || !output.commit()) {
        return reject("asset-atomic-stage-failed");
    }
    return verified;
}

std::optional<std::string> VerifiedAssetStore::verifiedPngPath(const std::string& handle) const
{
    if (m_instancePersistencePath.empty() || m_directory.empty() || !isLowerHexDigest(handle))
        return std::nullopt;

    const QString instanceRoot = QString::fromStdString(m_instancePersistencePath);
    const QString assetDirectory = QDir(QString::fromStdString(m_directory)).canonicalPath();
    if (!isUnder(assetDirectory, instanceRoot))
        return std::nullopt;

    const QFileInfo candidate(assetDirectory + QLatin1Char('/')
                              + QString::fromStdString(handle) + QStringLiteral(".png"));
    const QString canonicalPath = candidate.canonicalFilePath();
    if (candidate.isSymLink() || !candidate.isFile() || !isUnder(canonicalPath, assetDirectory))
        return std::nullopt;

    QFile input(canonicalPath);
    if (!input.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray encoded = input.read(10 * 1024 * 1024 + 1);
    if (!input.atEnd() || encoded.size() > 10 * 1024 * 1024)
        return std::nullopt;
    const std::string bytes(encoded.constData(), static_cast<std::size_t>(encoded.size()));
    if (crypto::sha256Hex(bytes) != handle)
        return std::nullopt;
    return canonicalPath.toStdString();
}

const std::string& VerifiedAssetStore::directory() const
{
    return m_directory;
}

} // namespace palace
