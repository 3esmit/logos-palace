#include "logos_test.h"

#include "palace_asset_authoring.h"
#include "palace_sha256.h"
#include "palace_storage_mvp.h"

#include <QBuffer>
#include <QColor>
#include <QImage>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr char kTestPropId[] = "test_prop-1";

std::string encodedPng(
    int width,
    int height,
    QRgb color)
{
    QImage image(width, height, QImage::Format_RGBA8888);
    image.fill(color);
    QBuffer buffer;
    if (!buffer.open(QIODevice::WriteOnly)
        || !image.save(&buffer, "PNG")) {
        return {};
    }
    return {
        buffer.data().constData(),
        static_cast<std::size_t>(buffer.data().size()),
    };
}

struct Backgrounds {
    std::string atrium;
    std::string lounge;
    std::string prop;
};

Backgrounds backgrounds()
{
    return {
        encodedPng(11, 7, qRgb(0x10, 0x30, 0x50)),
        encodedPng(13, 9, qRgb(0x60, 0x40, 0x20)),
        encodedPng(8, 8, qRgba(0xff, 0x44, 0x22, 0x80)),
    };
}

bool initialize(
    palace::PalaceStorageMvpBundle& bundle,
    const Backgrounds& images)
{
    return bundle.initialize(
        images.atrium,
        images.lounge,
        images.prop,
        kTestPropId,
        8U,
        8U,
        4U,
        7U,
        "head");
}

bool initializeWithoutProp(
    palace::PalaceStorageMvpBundle& bundle,
    const Backgrounds& images)
{
    return bundle.initialize(images.atrium, images.lounge);
}

std::string propManifestObjectId()
{
    return std::string("prop-") + kTestPropId;
}

std::string propImageObjectId()
{
    return propManifestObjectId() + "-image";
}

std::string propMetadataObjectId()
{
    return propManifestObjectId() + "-metadata";
}

std::uint8_t hexNibble(char value)
{
    if (value >= '0' && value <= '9')
        return static_cast<std::uint8_t>(value - '0');
    return static_cast<std::uint8_t>(value - 'a' + 10);
}

std::string cid(const std::string& digest)
{
    std::vector<std::uint8_t> bytes = {0x01U, 0x55U, 0x12U, 0x20U};
    for (std::size_t index = 0U;
         index < digest.size(); index += 2U) {
        bytes.push_back(static_cast<std::uint8_t>(
            (hexNibble(digest[index]) << 4U)
            | hexNibble(digest[index + 1U])));
    }

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

bool publishAll(palace::PalaceStorageMvpBundle& bundle)
{
    while (!bundle.complete()) {
        const auto stageable = bundle.stageableObjectIds();
        if (stageable.empty())
            return false;
        for (const std::string& objectId : stageable) {
            const palace::PalaceStorageMvpArtifactV1* artifact =
                bundle.artifact(objectId);
            if (artifact == nullptr)
                return false;
            const std::string nativeManifestDigest =
                palace::crypto::sha256Hex(
                    "native-storage-manifest-v1\n"
                    + objectId + "\n"
                    + artifact->specification.contentSha256);
            if (!bundle.assignPublicationCid(
                    objectId, cid(nativeManifestDigest))) {
                return false;
            }
        }
    }
    return true;
}

palace::AssetAuthoringStateV1 authoringStateFor(
    const palace::PalaceStorageMvpBundle& bundle,
    const Backgrounds& images,
    bool includeProp)
{
    palace::AssetAuthoringStateV1 state;
    state.bundleLocked = true;
    const auto addAsset =
        [&state](const std::string& handle,
                 const std::string& png,
                 const std::string& publishedCid) {
            palace::AssetAuthoringAssetV1 asset;
            asset.handle = handle;
            asset.label = "selected-image";
            asset.width = QImage::fromData(
                QByteArray(png.data(), static_cast<qsizetype>(png.size())),
                "PNG").width();
            asset.height = QImage::fromData(
                QByteArray(png.data(), static_cast<qsizetype>(png.size())),
                "PNG").height();
            asset.byteLength = png.size();
            asset.reviewState = "approved";
            asset.publishedCid = publishedCid;
            state.assets.emplace(handle, std::move(asset));
        };

    const auto* atrium = bundle.artifact("background-atrium");
    const auto* lounge = bundle.artifact("background-lounge");
    if (atrium == nullptr || lounge == nullptr)
        return {};
    const std::string atriumHandle = palace::crypto::sha256Hex(images.atrium);
    const std::string loungeHandle = palace::crypto::sha256Hex(images.lounge);
    addAsset(atriumHandle, images.atrium, atrium->cid);
    addAsset(loungeHandle, images.lounge, lounge->cid);
    state.roomAssignments.emplace("atrium", atriumHandle);
    state.roomAssignments.emplace("lounge", loungeHandle);
    if (includeProp) {
        const auto* prop = bundle.artifact(propImageObjectId());
        if (prop == nullptr)
            return {};
        const std::string propHandle = palace::crypto::sha256Hex(images.prop);
        addAsset(propHandle, images.prop, prop->cid);
        state.propAssignment = palace::AssetAuthoringPropAssignmentV1{
            kTestPropId,
            propHandle,
            4U,
            7U,
            "head",
        };
    }
    return state;
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
    const Backgrounds images = backgrounds();

    palace::PalaceStorageMvpBundle bundle;
    LOGOS_ASSERT_TRUE(initialize(bundle, images));
    LOGOS_ASSERT_EQ(bundle.artifactCount(), 7U);
    LOGOS_ASSERT_EQ(bundle.publishedCount(), 0U);
    LOGOS_ASSERT_EQ(bundle.stageableObjectIds().size(), 7U);

    const auto* prop = bundle.artifact(propImageObjectId());
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
    LOGOS_ASSERT_EQ(qAlpha(propImage.pixel(0, 0)), 0x80);

    const auto* metadata = bundle.artifact(propMetadataObjectId());
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
    LOGOS_ASSERT_EQ(bundle.artifactCount(), 11U);
    const auto* palaceManifest = bundle.artifact("palace-1");
    LOGOS_ASSERT_TRUE(palaceManifest != nullptr);
    LOGOS_ASSERT_EQ(
        palaceManifest->specification.children.size(), 2U);
    LOGOS_ASSERT_TRUE(
        palaceManifest->bytes.rfind(
            "logos-palace-catalog-manifest-v1\n", 0U)
        == 0U);
}

LOGOS_TEST(storage_mvp_omits_prop_graph_without_user_assignment)
{
    const Backgrounds images = backgrounds();
    palace::PalaceStorageMvpBundle source;
    LOGOS_ASSERT_TRUE(initializeWithoutProp(source, images));
    LOGOS_ASSERT_EQ(source.artifactCount(), 5U);
    LOGOS_ASSERT_EQ(source.stageableObjectIds().size(), 5U);
    LOGOS_ASSERT_TRUE(source.propManifestObjectId().empty());
    LOGOS_ASSERT_FALSE(source.propAsset().has_value());
    const auto* atriumMetadata =
        source.artifact("room-atrium-metadata");
    LOGOS_ASSERT_TRUE(atriumMetadata != nullptr);
    LOGOS_ASSERT_TRUE(
        atriumMetadata->bytes.find("allowed_prop_set=")
        == std::string::npos);

    LOGOS_ASSERT_TRUE(publishAll(source));
    LOGOS_ASSERT_EQ(source.artifactCount(), 8U);
    LOGOS_ASSERT_TRUE(source.complete());
    LOGOS_ASSERT_TRUE(source.fetchedContentValid());
    const auto* atriumManifest = source.artifact("room-atrium");
    LOGOS_ASSERT_TRUE(atriumManifest != nullptr);
    LOGOS_ASSERT_EQ(
        atriumManifest->specification.children.size(), 3U);

    const std::string catalog = source.canonicalCatalog();
    LOGOS_ASSERT_FALSE(catalog.empty());
    palace::PalaceStorageMvpBundle visitor;
    LOGOS_ASSERT_TRUE(visitor.restoreCanonicalCatalog(catalog));
    LOGOS_ASSERT_TRUE(visitor.complete());
    LOGOS_ASSERT_TRUE(visitor.propManifestObjectId().empty());
    LOGOS_ASSERT_FALSE(visitor.propAsset().has_value());
    for (const palace::PalaceStorageMvpArtifactV1& artifact
         : source.artifacts()) {
        LOGOS_ASSERT_TRUE(visitor.acceptFetchedBytes(
            artifact.objectId, artifact.bytes));
    }
    LOGOS_ASSERT_TRUE(visitor.fetchedContentValid());
    LOGOS_ASSERT_TRUE(visitor.propId().empty());
    LOGOS_ASSERT_FALSE(visitor.propAsset().has_value());
}

LOGOS_TEST(storage_mvp_requires_alpha_leading_authored_prop_identifiers)
{
    const Backgrounds images = backgrounds();
    for (const std::string invalid : {
             std::string("1test_prop-1"),
             std::string("-test_prop-1"),
             std::string("_test_prop-1"),
         }) {
        palace::PalaceStorageMvpBundle bundle;
        LOGOS_ASSERT_FALSE(bundle.initialize(
            images.atrium,
            images.lounge,
            images.prop,
            invalid,
            8U,
            8U,
            4U,
            7U,
            "head"));
    }

    palace::PalaceStorageMvpBundle accepted;
    LOGOS_ASSERT_TRUE(initialize(accepted, images));
    LOGOS_ASSERT_EQ(
        accepted.propId(), std::string("test_prop-1"));
}

LOGOS_TEST(storage_mvp_catalog_roundtrip_reconstructs_every_exact_object)
{
    const Backgrounds images = backgrounds();

    palace::PalaceStorageMvpBundle source;
    LOGOS_ASSERT_TRUE(initialize(source, images));
    const auto creatorProp = source.propAsset();
    LOGOS_ASSERT_TRUE(creatorProp.has_value());
    LOGOS_ASSERT_EQ(
        creatorProp->propId, std::string(kTestPropId));
    LOGOS_ASSERT_EQ(
        creatorProp->handle,
        palace::crypto::sha256Hex(images.prop));
    LOGOS_ASSERT_EQ(creatorProp->width, 8U);
    LOGOS_ASSERT_EQ(creatorProp->height, 8U);
    LOGOS_ASSERT_EQ(creatorProp->anchorX, 4U);
    LOGOS_ASSERT_EQ(creatorProp->anchorY, 7U);
    LOGOS_ASSERT_EQ(
        creatorProp->layer, std::string("head"));
    LOGOS_ASSERT_TRUE(publishAll(source));
    const std::string encoded = source.canonicalCatalog();
    LOGOS_ASSERT_FALSE(encoded.empty());

    palace::PalaceStorageMvpBundle restored;
    LOGOS_ASSERT_TRUE(initialize(restored, images));
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
    LOGOS_ASSERT_TRUE(initialize(rejected, images));
    LOGOS_ASSERT_FALSE(rejected.restoreCanonicalCatalog(tampered));
}

LOGOS_TEST(storage_mvp_catalog_rejects_six_line_record_before_variant_lookup)
{
    const std::string body =
        "logos-palace-mvp-storage-catalog-v1\n"
        "version=1\n"
        "root=palace-1\n"
        "objects=1\n"
        "object=not-a-graph-record\n";
    const std::string malformed =
        body + "checksum=" + palace::crypto::sha256Hex(body) + '\n';

    palace::PalaceStorageMvpBundle visitor;
    LOGOS_ASSERT_FALSE(visitor.restoreCanonicalCatalog(malformed));
}

LOGOS_TEST(storage_mvp_visitor_restores_placeholders_then_validates_bytes)
{
    const Backgrounds images = backgrounds();
    palace::PalaceStorageMvpBundle source;
    LOGOS_ASSERT_TRUE(initialize(source, images));
    const auto sourceProp = source.propAsset();
    LOGOS_ASSERT_TRUE(sourceProp.has_value());
    LOGOS_ASSERT_TRUE(publishAll(source));
    const std::string catalog = source.canonicalCatalog();
    LOGOS_ASSERT_FALSE(catalog.empty());

    palace::PalaceStorageMvpBundle visitor;
    LOGOS_ASSERT_TRUE(
        visitor.restoreCanonicalCatalog(catalog));
    LOGOS_ASSERT_TRUE(visitor.complete());
    LOGOS_ASSERT_TRUE(
        visitor.artifact("background-atrium")->bytes.empty());
    LOGOS_ASSERT_TRUE(
        visitor.artifact(propImageObjectId())->bytes.empty());
    LOGOS_ASSERT_FALSE(
        visitor.artifact("room-atrium")->bytes.empty());
    LOGOS_ASSERT_FALSE(visitor.fetchedContentValid());
    LOGOS_ASSERT_FALSE(visitor.propAsset().has_value());

    for (const palace::PalaceStorageMvpArtifactV1& artifact
         : source.artifacts()) {
        LOGOS_ASSERT_TRUE(visitor.acceptFetchedBytes(
            artifact.objectId, artifact.bytes));
    }
    LOGOS_ASSERT_TRUE(visitor.fetchedContentValid());
    LOGOS_ASSERT_EQ(visitor.propId(), std::string(kTestPropId));
    const auto visitorProp = visitor.propAsset();
    LOGOS_ASSERT_TRUE(visitorProp.has_value());
    LOGOS_ASSERT_EQ(
        visitorProp->handle, sourceProp->handle);
    LOGOS_ASSERT_EQ(visitorProp->width, sourceProp->width);
    LOGOS_ASSERT_EQ(
        visitorProp->anchorX, sourceProp->anchorX);
    LOGOS_ASSERT_EQ(
        visitorProp->layer, sourceProp->layer);
    LOGOS_ASSERT_EQ(visitor.canonicalCatalog(), catalog);

    palace::PalaceStorageMvpBundle rejected;
    LOGOS_ASSERT_TRUE(
        rejected.restoreCanonicalCatalog(catalog));
    std::string changed = images.atrium;
    changed.back() ^= 0x01;
    LOGOS_ASSERT_FALSE(rejected.acceptFetchedBytes(
        "background-atrium", changed));
}

LOGOS_TEST(storage_mvp_resolves_only_fully_fetched_published_png_by_cid)
{
    const Backgrounds images = backgrounds();
    palace::PalaceStorageMvpBundle source;
    LOGOS_ASSERT_TRUE(initialize(source, images));
    LOGOS_ASSERT_TRUE(publishAll(source));
    const auto* sourceAtrium =
        source.artifact("background-atrium");
    const auto* sourceManifest = source.artifact("room-atrium");
    const auto* sourceProp = source.artifact(propImageObjectId());
    LOGOS_ASSERT_TRUE(sourceAtrium != nullptr);
    LOGOS_ASSERT_TRUE(sourceManifest != nullptr);
    LOGOS_ASSERT_TRUE(sourceProp != nullptr);

    palace::PalaceStorageMvpBundle visitor;
    LOGOS_ASSERT_TRUE(visitor.restoreCanonicalCatalog(
        source.canonicalCatalog()));
    LOGOS_ASSERT_TRUE(visitor.complete());
    LOGOS_ASSERT_FALSE(visitor.fetchedContentValid());
    LOGOS_ASSERT_TRUE(visitor.fetchedPngArtifactForCid(
        sourceAtrium->cid) == nullptr);

    for (const palace::PalaceStorageMvpArtifactV1& artifact
         : source.artifacts()) {
        LOGOS_ASSERT_TRUE(visitor.acceptFetchedBytes(
            artifact.objectId, artifact.bytes));
    }
    LOGOS_ASSERT_TRUE(visitor.fetchedContentValid());

    const auto* resolved = visitor.fetchedPngArtifactForCid(
        sourceAtrium->cid);
    LOGOS_ASSERT_TRUE(resolved != nullptr);
    LOGOS_ASSERT_EQ(
        static_cast<int>(resolved->type),
        static_cast<int>(
            palace::PalaceStorageMvpArtifactType::BackgroundPng));
    LOGOS_ASSERT_EQ(resolved->bytes, images.atrium);
    const auto* resolvedProp = visitor.fetchedPngArtifactForCid(
        sourceProp->cid);
    LOGOS_ASSERT_TRUE(resolvedProp != nullptr);
    LOGOS_ASSERT_EQ(
        static_cast<int>(resolvedProp->type),
        static_cast<int>(
            palace::PalaceStorageMvpArtifactType::PropPng));
    LOGOS_ASSERT_EQ(resolvedProp->bytes, images.prop);
    LOGOS_ASSERT_TRUE(visitor.fetchedPngArtifactForCid(
        cid(palace::crypto::sha256Hex("not-in-catalog"))) == nullptr);
    LOGOS_ASSERT_TRUE(visitor.fetchedPngArtifactForCid(
        sourceManifest->cid) == nullptr);
}

LOGOS_TEST(storage_mvp_requires_exact_local_authoring_assignments)
{
    const Backgrounds images = backgrounds();
    palace::PalaceStorageMvpBundle source;
    LOGOS_ASSERT_TRUE(initialize(source, images));
    LOGOS_ASSERT_TRUE(publishAll(source));

    palace::AssetAuthoringStateV1 authored =
        authoringStateFor(source, images, true);
    LOGOS_ASSERT_TRUE(source.matchesAuthoringAssignments(authored));

    palace::AssetAuthoringStateV1 foreignCatalog = authored;
    foreignCatalog.assets.at(
        foreignCatalog.roomAssignments.at("atrium")).publishedCid =
        cid(palace::crypto::sha256Hex("foreign-atrium"));
    LOGOS_ASSERT_FALSE(source.matchesAuthoringAssignments(foreignCatalog));

    palace::AssetAuthoringStateV1 missingProp = authored;
    missingProp.propAssignment.reset();
    LOGOS_ASSERT_FALSE(source.matchesAuthoringAssignments(missingProp));

    palace::PalaceStorageMvpBundle noProp;
    LOGOS_ASSERT_TRUE(initializeWithoutProp(noProp, images));
    LOGOS_ASSERT_TRUE(publishAll(noProp));
    palace::AssetAuthoringStateV1 noPropAuthoring =
        authoringStateFor(noProp, images, false);
    LOGOS_ASSERT_TRUE(noProp.matchesAuthoringAssignments(noPropAuthoring));
}

LOGOS_TEST(storage_mvp_background_and_prop_digests_are_runtime_inputs)
{
    const Backgrounds first = backgrounds();
    Backgrounds second = first;
    second.atrium =
        encodedPng(11, 7, qRgb(0x70, 0x20, 0x10));
    second.lounge =
        encodedPng(13, 9, qRgb(0x11, 0x55, 0x99));
    second.prop =
        encodedPng(8, 8, qRgba(0x10, 0x20, 0x30, 0x40));

    palace::PalaceStorageMvpBundle firstBundle;
    palace::PalaceStorageMvpBundle secondBundle;
    LOGOS_ASSERT_TRUE(initialize(firstBundle, first));
    LOGOS_ASSERT_TRUE(initialize(secondBundle, second));
    LOGOS_ASSERT_EQ(
        firstBundle.artifact("background-atrium")
            ->specification.contentSha256,
        palace::crypto::sha256Hex(first.atrium));
    LOGOS_ASSERT_EQ(
        secondBundle.artifact("background-atrium")
            ->specification.contentSha256,
        palace::crypto::sha256Hex(second.atrium));
    LOGOS_ASSERT_NE(
        firstBundle.artifact("background-atrium")
            ->specification.contentSha256,
        secondBundle.artifact("background-atrium")
            ->specification.contentSha256);
    LOGOS_ASSERT_NE(
        firstBundle.artifact(propImageObjectId())
            ->specification.contentSha256,
        secondBundle.artifact(propImageObjectId())
            ->specification.contentSha256);
}

LOGOS_TEST(storage_mvp_catalog_rejects_duplicate_and_out_of_order_cids)
{
    const Backgrounds images = backgrounds();

    palace::PalaceStorageMvpBundle bundle;
    LOGOS_ASSERT_TRUE(initialize(bundle, images));
    const auto* atrium =
        bundle.artifact("background-atrium");
    const auto* lounge =
        bundle.artifact("background-lounge");
    LOGOS_ASSERT_TRUE(atrium != nullptr);
    LOGOS_ASSERT_TRUE(lounge != nullptr);
    const std::string atriumCid =
        cid(atrium->specification.contentSha256);
    const std::string loungeManifestCid =
        cid(std::string(64U, 'a'));
    LOGOS_ASSERT_TRUE(bundle.assignPublicationCid(
        "background-atrium", atriumCid));
    LOGOS_ASSERT_FALSE(bundle.assignPublicationCid(
        "background-lounge", atriumCid));
    // Publication CIDs identify native Storage manifests. Exact object bytes
    // are verified before this commit, rather than inferred from CID digest.
    LOGOS_ASSERT_TRUE(bundle.assignPublicationCid(
        "background-lounge",
        loungeManifestCid));
    LOGOS_ASSERT_FALSE(bundle.assignPublicationCid(
        propManifestObjectId(), loungeManifestCid));
    LOGOS_ASSERT_FALSE(bundle.assignPublicationCid(
        propManifestObjectId(), "not-a-cid"));
    LOGOS_ASSERT_FALSE(bundle.assignPublicationCid(
        "unknown", atriumCid));
}
