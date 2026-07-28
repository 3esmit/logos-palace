#include <logos_test.h>

#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include "palace_sha256.h"
#include "palace_verified_asset_store.h"

namespace {

std::string encodedPng()
{
    QImage image(2, 1, QImage::Format_RGBA8888);
    image.fill(QColor(0x34, 0x68, 0x9c));

    QBuffer buffer;
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        return {};
    return {buffer.data().constData(), static_cast<std::size_t>(buffer.data().size())};
}

palace::AssetRefV1 assetRef(const std::string& encoded)
{
    palace::AssetRefV1 reference;
    reference.sourceCid = "cid-source";
    reference.derivativeCid = "cid-derivative";
    reference.byteLength = encoded.size();
    reference.mediaType = "image/png";
    reference.width = 2;
    reference.height = 1;
    reference.technicalProfile = "palace-png-v1";
    reference.contentSha256 = palace::crypto::sha256Hex(encoded);
    return reference;
}

} // namespace

LOGOS_TEST(verified_asset_store_stages_valid_png_under_core_instance_root) {
    QTemporaryDir temporary;
    LOGOS_ASSERT_TRUE(temporary.isValid());

    const QString instanceRoot = temporary.path() + QStringLiteral("/instance");
    LOGOS_ASSERT_TRUE(QDir().mkpath(instanceRoot));
    const std::string encoded = encodedPng();
    LOGOS_ASSERT_FALSE(encoded.empty());

    palace::VerifiedAssetStore store(instanceRoot.toStdString());
    const palace::VerifiedAsset staged = store.stagePngDerivative(assetRef(encoded), encoded);
    LOGOS_ASSERT_TRUE(staged.accepted);
    LOGOS_ASSERT_EQ(staged.handle, palace::crypto::sha256Hex(encoded));

    const QString expected = QDir::cleanPath(
        instanceRoot + QStringLiteral("/verified_assets/logos_palace_ui/")
        + QString::fromStdString(staged.handle) + QStringLiteral(".png"));
    QFile file(expected);
    LOGOS_ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray stored = file.readAll();
    LOGOS_ASSERT_EQ(palace::crypto::sha256Hex(
                            std::string(stored.constData(), static_cast<std::size_t>(stored.size()))),
                        staged.handle);

    LOGOS_ASSERT_TRUE(store.stagePngDerivative(assetRef(encoded), encoded).accepted);
}

LOGOS_TEST(verified_asset_store_does_not_create_a_file_for_an_invalid_reference) {
    QTemporaryDir temporary;
    LOGOS_ASSERT_TRUE(temporary.isValid());

    const QString instanceRoot = temporary.path() + QStringLiteral("/instance");
    LOGOS_ASSERT_TRUE(QDir().mkpath(instanceRoot));
    const std::string encoded = encodedPng();
    palace::AssetRefV1 reference = assetRef(encoded);
    reference.contentSha256.assign(64U, '0');

    palace::VerifiedAssetStore store(instanceRoot.toStdString());
    const palace::VerifiedAsset rejected = store.stagePngDerivative(reference, encoded);
    LOGOS_ASSERT_FALSE(rejected.accepted);
    LOGOS_ASSERT_EQ(rejected.reason, std::string("byte-length-or-digest-mismatch"));
    LOGOS_ASSERT_FALSE(QDir(QString::fromStdString(store.directory())).exists());
}
