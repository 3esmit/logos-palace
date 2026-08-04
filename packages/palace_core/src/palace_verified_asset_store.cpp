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

bool ensureCanonicalDirectoryUnderRoot(const QString& root,
                                       const QString& directory)
{
    if (root.isEmpty() || directory.isEmpty())
        return false;
    if (!QDir().mkpath(directory))
        return false;
    const QString canonicalDirectory = QDir(directory).canonicalPath();
    return isUnder(canonicalDirectory, root);
}

bool stageVerifiedPngAt(const QString& instanceRoot,
                        const QString& assetDirectory,
                        const palace::VerifiedAsset& verified,
                        const std::string& encoded,
                        std::string& reason)
{
    reason.clear();
    if (!ensureCanonicalDirectoryUnderRoot(instanceRoot, assetDirectory)) {
        reason = "asset-directory-create-failed";
        return false;
    }

    const QString canonicalAssetDirectory = QDir(assetDirectory).canonicalPath();
    if (!isUnder(canonicalAssetDirectory, instanceRoot)) {
        reason = "asset-directory-escaped-instance-root";
        return false;
    }

    const QString destination =
        canonicalAssetDirectory + QLatin1Char('/')
        + QString::fromStdString(verified.handle)
        + QStringLiteral(".png");
    const QFileInfo existing(destination);
    if (existing.isSymLink()) {
        reason = "asset-destination-symlink";
        return false;
    }
    if (existing.exists()) {
        const QString canonicalExisting = existing.canonicalFilePath();
        if (!existing.isFile()
            || !isUnder(canonicalExisting, canonicalAssetDirectory)) {
            reason = "asset-destination-escaped-store";
            return false;
        }
        QFile file(canonicalExisting);
        if (!file.open(QIODevice::ReadOnly)) {
            reason = "asset-destination-read-failed";
            return false;
        }
        const QByteArray existingBytes = file.readAll();
        if (crypto::sha256Hex(
                std::string(existingBytes.constData(),
                            static_cast<std::size_t>(existingBytes.size())))
            != verified.handle) {
            reason = "asset-destination-digest-mismatch";
            return false;
        }
        return true;
    }

    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(encoded.data(), static_cast<qint64>(encoded.size()))
            != static_cast<qint64>(encoded.size())
        || !output.commit()) {
        reason = "asset-atomic-stage-failed";
        return false;
    }
    return true;
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
    std::string reason;
    if (!stageVerifiedPngAt(
            instanceRoot, assetDirectory, verified, encoded, reason)) {
        return reject(reason);
    }
    return verified;
}

std::optional<std::string> VerifiedAssetStore::verifiedPngPath(const std::string& handle) const
{
    if (m_instancePersistencePath.empty() || m_directory.empty()
        || !isLowerHexDigest(handle))
        return std::nullopt;

    const auto resolveFromDirectory =
        [&](const QString& instanceRoot, const QString& directory)
            -> std::optional<std::string> {
        const QString assetDirectory =
            QDir(directory).canonicalPath();
        if (!isUnder(assetDirectory, instanceRoot))
            return std::nullopt;

        const QFileInfo candidate(
            assetDirectory + QLatin1Char('/')
            + QString::fromStdString(handle)
            + QStringLiteral(".png"));
        const QString canonicalPath = candidate.canonicalFilePath();
        if (candidate.isSymLink() || !candidate.isFile()
            || !isUnder(canonicalPath, assetDirectory)) {
            return std::nullopt;
        }

        QFile input(canonicalPath);
        if (!input.open(QIODevice::ReadOnly))
            return std::nullopt;
        const QByteArray encoded = input.read(10 * 1024 * 1024 + 1);
        if (!input.atEnd() || encoded.size() > 10 * 1024 * 1024)
            return std::nullopt;
        const std::string bytes(
            encoded.constData(), static_cast<std::size_t>(encoded.size()));
        if (crypto::sha256Hex(bytes) != handle)
            return std::nullopt;
        return canonicalPath.toStdString();
    };

    if (const auto primary = resolveFromDirectory(
            QString::fromStdString(m_instancePersistencePath),
            QString::fromStdString(m_directory))) {
        return primary;
    }
    return std::nullopt;
}

const std::string& VerifiedAssetStore::directory() const
{
    return m_directory;
}

} // namespace palace
