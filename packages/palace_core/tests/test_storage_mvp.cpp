#include "logos_test.h"

#include "palace_room_backgrounds.h"
#include "palace_storage_mvp.h"
#include "palace_verified_asset_store.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

std::string readAll(const std::string& path)
{
    QFile input(QString::fromStdString(path));
    if (!input.open(QIODevice::ReadOnly))
        return {};
    return input.readAll().toStdString();
}

struct Backgrounds {
    std::string atrium;
    std::string lounge;
};

Backgrounds backgrounds(
    const QTemporaryDir& temporary,
    palace::VerifiedAssetStore& store)
{
    const QString instanceRoot =
        temporary.path() + QStringLiteral("/instance");
    LOGOS_ASSERT_TRUE(QDir().mkpath(instanceRoot));
    palace::RoomBackgroundCatalog catalog;
    LOGOS_ASSERT_TRUE(catalog.stageBuiltInFixtures(store));
    const auto atriumPath =
        store.verifiedPngPath(catalog.handleForRoom("atrium"));
    const auto loungePath =
        store.verifiedPngPath(catalog.handleForRoom("lounge"));
    LOGOS_ASSERT_TRUE(atriumPath.has_value());
    LOGOS_ASSERT_TRUE(loungePath.has_value());
    return {readAll(*atriumPath), readAll(*loungePath)};
}

std::string cid(std::uint8_t suffix)
{
    std::vector<std::uint8_t> bytes = {0x01U, 0x55U, 0x12U, 0x20U};
    for (std::uint8_t index = 0U; index < 32U; ++index)
        bytes.push_back(static_cast<std::uint8_t>(suffix + index));

    static constexpr char alphabet[] =
        "abcdefghijklmnopqrstuvwxyz234567";
    std::string encoded = "b";
    std::uint32_t accumulator = 0U;
    unsigned int bitCount = 0U;
    for (std::uint8_t byte : bytes) {
        accumulator = (accumulator << 8U) | byte;
        bitCount += 8U;
        while (bitCount >= 5U) {
            bitCount -= 5U;
            encoded.push_back(alphabet[
                (accumulator >> bitCount) & 0x1fU]);
            accumulator &= bitCount == 0U
                ? 0U : ((1U << bitCount) - 1U);
        }
    }
    if (bitCount != 0U) {
        encoded.push_back(alphabet[
            (accumulator << (5U - bitCount)) & 0x1fU]);
    }
    return encoded;
}

const std::vector<std::string>& order()
{
    static const std::vector<std::string> value{
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
    return value;
}

bool publishAll(palace::PalaceStorageMvpBundle& bundle)
{
    std::uint8_t suffix = 1U;
    for (const std::string& objectId : order()) {
        const auto stageable = bundle.stageableObjectIds();
        if (std::find(
                stageable.begin(), stageable.end(), objectId)
            == stageable.end()) {
            return false;
        }
        if (!bundle.assignPublicationCid(objectId, cid(suffix++)))
            return false;
    }
    return bundle.complete();
}

} // namespace

LOGOS_TEST(storage_mvp_fetch_source_requires_every_exact_native_cid)
{
    using palace::PalaceStorageMvpFetchSource;
    const auto allPresent =
        palace::selectPalaceStorageMvpFetchSource(
            {true, true, true}, 3U);
    LOGOS_ASSERT_TRUE(allPresent.has_value());
    LOGOS_ASSERT_EQ(
        static_cast<int>(*allPresent),
        static_cast<int>(PalaceStorageMvpFetchSource::Cache));
    LOGOS_ASSERT_EQ(
        std::string(palace::palaceStorageMvpFetchSourceName(
            PalaceStorageMvpFetchSource::Cache)),
        std::string("cache"));

    const auto oneMissing =
        palace::selectPalaceStorageMvpFetchSource(
            {true, false, true}, 3U);
    LOGOS_ASSERT_TRUE(oneMissing.has_value());
    LOGOS_ASSERT_EQ(
        static_cast<int>(*oneMissing),
        static_cast<int>(PalaceStorageMvpFetchSource::Network));
    LOGOS_ASSERT_FALSE(
        palace::selectPalaceStorageMvpFetchSource(
            {true, std::nullopt, true}, 3U)
            .has_value());
    LOGOS_ASSERT_FALSE(
        palace::selectPalaceStorageMvpFetchSource(
            {true, true}, 3U)
            .has_value());
    LOGOS_ASSERT_FALSE(
        palace::selectPalaceStorageMvpFetchSource({}, 0U)
            .has_value());
    LOGOS_ASSERT_EQ(
        std::string(palace::palaceStorageMvpFetchSourceName(
            PalaceStorageMvpFetchSource::Network)),
        std::string("network"));
}

LOGOS_TEST(storage_mvp_bundle_builds_exact_bounded_typed_graph)
{
    QTemporaryDir temporary;
    LOGOS_ASSERT_TRUE(temporary.isValid());
    const QString instanceRoot =
        temporary.path() + QStringLiteral("/instance");
    LOGOS_ASSERT_TRUE(QDir().mkpath(instanceRoot));
    palace::VerifiedAssetStore store(instanceRoot.toStdString());
    const Backgrounds images = backgrounds(temporary, store);

    palace::PalaceStorageMvpBundle bundle;
    LOGOS_ASSERT_TRUE(bundle.initialize(images.atrium, images.lounge));
    LOGOS_ASSERT_EQ(bundle.artifactCount(), 11U);
    LOGOS_ASSERT_EQ(bundle.publishedCount(), 0U);
    LOGOS_ASSERT_EQ(bundle.stageableObjectIds().size(), 7U);

    const auto* prop = bundle.artifact("prop-hat-image");
    LOGOS_ASSERT_TRUE(prop != nullptr);
    LOGOS_ASSERT_EQ(
        static_cast<int>(prop->type),
        static_cast<int>(
            palace::PalaceStorageMvpArtifactType::PropPng));
    const QImage propImage = QImage::fromData(
        QByteArray(
            prop->bytes.data(),
            static_cast<qsizetype>(prop->bytes.size())),
        "PNG");
    LOGOS_ASSERT_FALSE(propImage.isNull());
    LOGOS_ASSERT_EQ(propImage.width(), 8);
    LOGOS_ASSERT_EQ(propImage.height(), 8);
    LOGOS_ASSERT_TRUE(propImage.hasAlphaChannel());
    LOGOS_ASSERT_EQ(qAlpha(propImage.pixel(0, 0)), 0);

    const auto* metadata = bundle.artifact("prop-hat-metadata");
    LOGOS_ASSERT_TRUE(metadata != nullptr);
    LOGOS_ASSERT_TRUE(
        metadata->bytes.find("anchor_x=4\n") != std::string::npos);
    LOGOS_ASSERT_TRUE(
        metadata->bytes.find("anchor_y=7\n") != std::string::npos);
    LOGOS_ASSERT_TRUE(
        metadata->bytes.find("layer=head\n") != std::string::npos);

    const auto* script = bundle.artifact("script-door");
    LOGOS_ASSERT_TRUE(script != nullptr);
    LOGOS_ASSERT_TRUE(script->bytes.size() < 1024U);
    LOGOS_ASSERT_EQ(
        script->bytes,
        std::string(
            "ON SELECT door\n"
            "SET door_open 1\n"
            "GOTOROOM lounge\n"));

    LOGOS_ASSERT_TRUE(publishAll(bundle));
    const auto* palaceManifest = bundle.artifact("palace-1");
    LOGOS_ASSERT_TRUE(palaceManifest != nullptr);
    LOGOS_ASSERT_EQ(
        palaceManifest->specification.children.size(), 2U);
    LOGOS_ASSERT_TRUE(
        palaceManifest->bytes.rfind(
            "logos-palace-catalog-manifest-v1\n", 0U)
        == 0U);
}

LOGOS_TEST(storage_mvp_catalog_roundtrip_reconstructs_every_exact_object)
{
    QTemporaryDir temporary;
    LOGOS_ASSERT_TRUE(temporary.isValid());
    const QString instanceRoot =
        temporary.path() + QStringLiteral("/instance");
    LOGOS_ASSERT_TRUE(QDir().mkpath(instanceRoot));
    palace::VerifiedAssetStore store(instanceRoot.toStdString());
    const Backgrounds images = backgrounds(temporary, store);

    palace::PalaceStorageMvpBundle source;
    LOGOS_ASSERT_TRUE(source.initialize(images.atrium, images.lounge));
    LOGOS_ASSERT_TRUE(publishAll(source));
    const std::string encoded = source.canonicalCatalog();
    LOGOS_ASSERT_FALSE(encoded.empty());

    palace::PalaceStorageMvpBundle restored;
    LOGOS_ASSERT_TRUE(restored.initialize(images.atrium, images.lounge));
    LOGOS_ASSERT_TRUE(restored.restoreCanonicalCatalog(encoded));
    LOGOS_ASSERT_TRUE(restored.complete());
    LOGOS_ASSERT_EQ(restored.canonicalCatalog(), encoded);
    LOGOS_ASSERT_EQ(restored.artifacts().size(), 11U);

    std::string tampered = encoded;
    const std::size_t layer =
        tampered.find("application/vnd.logos-palace.prop-v1");
    LOGOS_ASSERT_NE(layer, std::string::npos);
    tampered[layer] = 'A';
    palace::PalaceStorageMvpBundle rejected;
    LOGOS_ASSERT_TRUE(rejected.initialize(images.atrium, images.lounge));
    LOGOS_ASSERT_FALSE(rejected.restoreCanonicalCatalog(tampered));
}

LOGOS_TEST(storage_mvp_catalog_rejects_duplicate_and_out_of_order_cids)
{
    QTemporaryDir temporary;
    LOGOS_ASSERT_TRUE(temporary.isValid());
    const QString instanceRoot =
        temporary.path() + QStringLiteral("/instance");
    LOGOS_ASSERT_TRUE(QDir().mkpath(instanceRoot));
    palace::VerifiedAssetStore store(instanceRoot.toStdString());
    const Backgrounds images = backgrounds(temporary, store);

    palace::PalaceStorageMvpBundle bundle;
    LOGOS_ASSERT_TRUE(bundle.initialize(images.atrium, images.lounge));
    LOGOS_ASSERT_TRUE(bundle.assignPublicationCid(
        "background-atrium", cid(1U)));
    LOGOS_ASSERT_FALSE(bundle.assignPublicationCid(
        "background-lounge", cid(1U)));
    LOGOS_ASSERT_FALSE(bundle.assignPublicationCid(
        "prop-hat", cid(8U)));
    LOGOS_ASSERT_FALSE(bundle.assignPublicationCid(
        "unknown", cid(9U)));
}
