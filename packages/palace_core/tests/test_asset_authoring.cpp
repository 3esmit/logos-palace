#include <logos_test.h>

#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>

#include "palace_asset_authoring.h"
#include "palace_core_impl.h"
#include "palace_sha256.h"
#include "palace_verified_asset_store.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

std::string encodedPng(
    int width = 3,
    int height = 2,
    QRgb color = qRgb(0x21, 0x43, 0x65))
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

std::string base64(const std::string& bytes)
{
    return QByteArray(
               bytes.data(),
               static_cast<qsizetype>(bytes.size()))
        .toBase64()
        .toStdString();
}

std::string legacyV1EmptyRecord()
{
    const std::string body =
        "logos-palace-asset-authoring-v1\n"
        "version=1\n"
        "locked=0\n"
        "assets=0\n"
        "assignment=atrium;-\n"
        "assignment=lounge;-\n"
        "prop=-\n";
    return body + "checksum="
        + palace::crypto::sha256Hex(body) + '\n';
}

std::uint8_t hexNibble(char value)
{
    if (value >= '0' && value <= '9')
        return static_cast<std::uint8_t>(value - '0');
    return static_cast<std::uint8_t>(value - 'a' + 10);
}

std::string cidForDigest(const std::string& digest)
{
    std::vector<std::uint8_t> bytes{
        0x01U, 0x55U, 0x12U, 0x20U,
    };
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
    for (const std::uint8_t byte : bytes) {
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
            (accumulator << (5U - bitCount))
            & 0x1fU]);
    }
    return encoded;
}

struct Fixture {
    QTemporaryDir temporary;
    QString instanceRoot;
    palace::VerifiedAssetStore verified;
    palace::AssetAuthoringCatalog catalog;

    Fixture()
        : instanceRoot(
            temporary.path() + QStringLiteral("/instance"))
        , verified(instanceRoot.toStdString())
    {
        LOGOS_ASSERT_TRUE(temporary.isValid());
        LOGOS_ASSERT_TRUE(QDir().mkpath(instanceRoot));
        verified =
            palace::VerifiedAssetStore(instanceRoot.toStdString());
        LOGOS_ASSERT_TRUE(catalog.initialize(
            instanceRoot.toStdString(), verified));
    }

    palace::AssetAuthoringResult stage(
        const std::string& label,
        const std::string& png)
    {
        const palace::AssetAuthoringResult begun =
            catalog.begin(label);
        LOGOS_ASSERT_TRUE(begun.accepted);
        const palace::AssetAuthoringResult appended =
            catalog.append(
                begun.sessionId, 0U, base64(png));
        LOGOS_ASSERT_TRUE(appended.accepted);
        return catalog.commit(begun.sessionId);
    }
};

} // namespace

LOGOS_TEST(asset_authoring_stages_generic_png_in_chunks)
{
    Fixture fixture;
    const std::string png =
        encodedPng(7, 5, qRgb(0x91, 0x52, 0x33));
    const palace::AssetAuthoringResult begun =
        fixture.catalog.begin("Operator selected image");
    LOGOS_ASSERT_TRUE(begun.accepted);
    LOGOS_ASSERT_EQ(begun.sessionId.size(), 32U);
    LOGOS_ASSERT_EQ(begun.nextSequence, 0U);

    const std::size_t split = png.size() / 2U;
    const palace::AssetAuthoringResult first =
        fixture.catalog.append(
            begun.sessionId, 0U,
            base64(png.substr(0U, split)));
    LOGOS_ASSERT_TRUE(first.accepted);
    LOGOS_ASSERT_EQ(first.nextSequence, 1U);
    const palace::AssetAuthoringResult second =
        fixture.catalog.append(
            begun.sessionId, 1U,
            base64(png.substr(split)));
    LOGOS_ASSERT_TRUE(second.accepted);
    LOGOS_ASSERT_EQ(second.byteLength, png.size());

    const palace::AssetAuthoringResult committed =
        fixture.catalog.commit(begun.sessionId);
    LOGOS_ASSERT_TRUE(committed.accepted);
    LOGOS_ASSERT_EQ(
        committed.handle,
        palace::crypto::sha256Hex(png));
    LOGOS_ASSERT_EQ(committed.width, 7U);
    LOGOS_ASSERT_EQ(committed.height, 5U);
    LOGOS_ASSERT_EQ(committed.byteLength, png.size());
    LOGOS_ASSERT_EQ(fixture.catalog.assets().size(), 1U);
    LOGOS_ASSERT_TRUE(
        fixture.verified.verifiedPngPath(
            committed.handle).has_value());
    LOGOS_ASSERT_EQ(fixture.catalog.sessionCount(), 0U);
}

LOGOS_TEST(asset_authoring_rejects_malformed_chunks_and_order)
{
    Fixture fixture;
    const palace::AssetAuthoringResult begun =
        fixture.catalog.begin("chunk checks");
    LOGOS_ASSERT_TRUE(begun.accepted);

    const palace::AssetAuthoringResult outOfOrder =
        fixture.catalog.append(
            begun.sessionId, 1U, "YQ==");
    LOGOS_ASSERT_FALSE(outOfOrder.accepted);
    LOGOS_ASSERT_EQ(
        outOfOrder.reason,
        std::string("asset-chunk-sequence"));

    for (const std::string invalid : {
             std::string("YQ"),
             std::string("YQ==\n"),
             std::string("****"),
             std::string(),
         }) {
        const palace::AssetAuthoringResult rejected =
            fixture.catalog.append(
                begun.sessionId, 0U, invalid);
        LOGOS_ASSERT_FALSE(rejected.accepted);
        LOGOS_ASSERT_EQ(
            rejected.reason,
            std::string("asset-chunk-base64"));
    }
    LOGOS_ASSERT_TRUE(
        fixture.catalog.cancel(begun.sessionId).accepted);
    LOGOS_ASSERT_FALSE(
        fixture.catalog.commit(begun.sessionId).accepted);
}

LOGOS_TEST(asset_authoring_enforces_chunk_total_and_session_limits)
{
    Fixture fixture;
    std::vector<std::string> sessions;
    for (std::size_t index = 0U;
         index < palace::AssetAuthoringCatalog::MaximumSessions;
         ++index) {
        const palace::AssetAuthoringResult begun =
            fixture.catalog.begin(
                "session " + std::to_string(index));
        LOGOS_ASSERT_TRUE(begun.accepted);
        sessions.push_back(begun.sessionId);
    }
    const palace::AssetAuthoringResult saturated =
        fixture.catalog.begin("one too many");
    LOGOS_ASSERT_FALSE(saturated.accepted);
    LOGOS_ASSERT_EQ(
        saturated.reason,
        std::string("asset-session-limit"));

    const std::string fullChunk(
        palace::AssetAuthoringCatalog::MaximumChunkBytes,
        'x');
    for (std::uint64_t sequence = 0U;
         sequence < palace::AssetAuthoringCatalog::
             MaximumAssetBytes
             / palace::AssetAuthoringCatalog::
                 MaximumChunkBytes;
         ++sequence) {
        const palace::AssetAuthoringResult appended =
            fixture.catalog.append(
                sessions[0], sequence, base64(fullChunk));
        LOGOS_ASSERT_TRUE(appended.accepted);
    }
    const palace::AssetAuthoringResult oversized =
        fixture.catalog.append(
            sessions[0], 320U, "eA==");
    LOGOS_ASSERT_FALSE(oversized.accepted);
    LOGOS_ASSERT_EQ(
        oversized.reason,
        std::string("asset-too-large"));

    const std::string oversizedChunk(
        palace::AssetAuthoringCatalog::MaximumChunkBytes + 1U,
        'x');
    const palace::AssetAuthoringResult chunkRejected =
        fixture.catalog.append(
            sessions[1], 0U, base64(oversizedChunk));
    LOGOS_ASSERT_FALSE(chunkRejected.accepted);
    LOGOS_ASSERT_EQ(
        chunkRejected.reason,
        std::string("asset-chunk-base64"));
}

LOGOS_TEST(asset_authoring_review_publish_assignment_and_restart)
{
    Fixture fixture;
    const std::string png = encodedPng();
    const palace::AssetAuthoringResult staged =
        fixture.stage("atrium image", png);
    LOGOS_ASSERT_TRUE(staged.accepted);
    LOGOS_ASSERT_FALSE(
        fixture.catalog.assign(
            "atrium", staged.handle).accepted);
    LOGOS_ASSERT_FALSE(
        fixture.catalog.recordPublishedCid(
            staged.handle,
            cidForDigest(staged.handle)).accepted);

    const palace::AssetAuthoringResult approved =
        fixture.catalog.review(staged.handle, "approve");
    LOGOS_ASSERT_TRUE(approved.accepted);
    LOGOS_ASSERT_EQ(
        approved.reason, std::string("approved"));
    LOGOS_ASSERT_FALSE(
        fixture.catalog.assign(
            "unknown", staged.handle).accepted);
    LOGOS_ASSERT_FALSE(
        fixture.catalog.assignProp(
            "test-prop", staged.handle,
            1U, 1U, "head").accepted);
    LOGOS_ASSERT_FALSE(
        fixture.catalog.recordPublishedCid(
            staged.handle, "not-a-cid").accepted);

    // Native Storage returns a manifest CID, whose digest is not necessarily
    // the raw PNG digest. Core records this only after local byte verification.
    const std::string cid =
        cidForDigest(std::string(64U, '0'));
    LOGOS_ASSERT_NE(cid, cidForDigest(staged.handle));
    LOGOS_ASSERT_TRUE(
        fixture.catalog.recordPublishedCid(
            staged.handle, cid).accepted);
    LOGOS_ASSERT_TRUE(
        fixture.catalog.assign(
            "atrium", staged.handle).accepted);
    LOGOS_ASSERT_TRUE(
        fixture.catalog.assign(
            "lounge", staged.handle).accepted);
    LOGOS_ASSERT_TRUE(
        fixture.catalog.assignProp(
            "test-prop", staged.handle,
            1U, 1U, "head").accepted);
    LOGOS_ASSERT_FALSE(
        fixture.catalog.assignProp(
            "test-prop", staged.handle,
            3U, 0U, "head").accepted);
    LOGOS_ASSERT_TRUE(fixture.catalog.lockAssignments());
    LOGOS_ASSERT_TRUE(
        fixture.catalog.assign(
            "atrium", staged.handle).accepted);
    LOGOS_ASSERT_FALSE(
        fixture.catalog.review(
            staged.handle, "reject").accepted);

    const palace::AssetAuthoringResult replacement =
        fixture.stage(
            "replacement",
            encodedPng(
                4, 3, qRgb(0x11, 0x22, 0x33)));
    LOGOS_ASSERT_TRUE(replacement.accepted);
    LOGOS_ASSERT_TRUE(
        fixture.catalog.review(
            replacement.handle, "approve").accepted);
    LOGOS_ASSERT_TRUE(
        fixture.catalog.recordPublishedCid(
            replacement.handle,
            cidForDigest(replacement.handle)).accepted);
    const palace::AssetAuthoringResult roomLocked =
        fixture.catalog.assign(
            "atrium", replacement.handle);
    LOGOS_ASSERT_FALSE(roomLocked.accepted);
    LOGOS_ASSERT_EQ(
        roomLocked.reason,
        std::string("asset-assignment-locked"));
    const palace::AssetAuthoringResult propLocked =
        fixture.catalog.assignProp(
            "test-prop", replacement.handle,
            1U, 1U, "head");
    LOGOS_ASSERT_FALSE(propLocked.accepted);
    LOGOS_ASSERT_EQ(
        propLocked.reason,
        std::string("asset-assignment-locked"));

    palace::AssetAuthoringCatalog restarted;
    LOGOS_ASSERT_TRUE(restarted.initialize(
        fixture.instanceRoot.toStdString(),
        fixture.verified));
    LOGOS_ASSERT_EQ(restarted.assets().size(), 2U);
    const palace::AssetAuthoringAssetV1* restored =
        restarted.asset(staged.handle);
    LOGOS_ASSERT_TRUE(restored != nullptr);
    LOGOS_ASSERT_EQ(
        restored->label, std::string("atrium image"));
    LOGOS_ASSERT_EQ(
        restored->reviewState, std::string("approved"));
    LOGOS_ASSERT_EQ(restored->publishedCid, cid);
    LOGOS_ASSERT_EQ(
        restarted.handleForRoom("atrium"),
        staged.handle);
    LOGOS_ASSERT_EQ(
        restarted.handleForRoom("lounge"),
        staged.handle);
    LOGOS_ASSERT_TRUE(restarted.state().bundleLocked);
    LOGOS_ASSERT_TRUE(
        restarted.state().propAssignment.has_value());
    LOGOS_ASSERT_EQ(
        restarted.state().propAssignment->propId,
        std::string("test-prop"));
}

LOGOS_TEST(asset_authoring_locks_room_assignments_without_a_prop)
{
    Fixture fixture;
    const palace::AssetAuthoringResult staged =
        fixture.stage("room image", encodedPng());
    LOGOS_ASSERT_TRUE(staged.accepted);
    LOGOS_ASSERT_TRUE(
        fixture.catalog.review(staged.handle, "approve").accepted);
    LOGOS_ASSERT_TRUE(
        fixture.catalog.recordPublishedCid(
            staged.handle, cidForDigest(staged.handle)).accepted);
    LOGOS_ASSERT_TRUE(
        fixture.catalog.assign("atrium", staged.handle).accepted);
    LOGOS_ASSERT_TRUE(
        fixture.catalog.assign("lounge", staged.handle).accepted);
    LOGOS_ASSERT_FALSE(
        fixture.catalog.state().propAssignment.has_value());
    LOGOS_ASSERT_TRUE(fixture.catalog.lockAssignments());
    LOGOS_ASSERT_TRUE(fixture.catalog.state().bundleLocked);

    const palace::AssetAuthoringResult propAfterLock =
        fixture.catalog.assignProp(
            "test-prop", staged.handle, 1U, 1U, "head");
    LOGOS_ASSERT_FALSE(propAfterLock.accepted);
    LOGOS_ASSERT_EQ(
        propAfterLock.reason,
        std::string("asset-assignment-locked"));

    palace::AssetAuthoringCatalog restarted;
    LOGOS_ASSERT_TRUE(restarted.initialize(
        fixture.instanceRoot.toStdString(), fixture.verified));
    LOGOS_ASSERT_TRUE(restarted.state().bundleLocked);
    LOGOS_ASSERT_FALSE(
        restarted.state().propAssignment.has_value());
    LOGOS_ASSERT_EQ(
        restarted.handleForRoom("atrium"), staged.handle);
    LOGOS_ASSERT_EQ(
        restarted.handleForRoom("lounge"), staged.handle);
}

LOGOS_TEST(asset_authoring_requires_alpha_leading_prop_identifiers)
{
    Fixture fixture;
    const palace::AssetAuthoringResult staged =
        fixture.stage("prop image", encodedPng());
    LOGOS_ASSERT_TRUE(staged.accepted);
    LOGOS_ASSERT_TRUE(
        fixture.catalog.review(staged.handle, "approve").accepted);
    LOGOS_ASSERT_TRUE(
        fixture.catalog.recordPublishedCid(
            staged.handle, cidForDigest(staged.handle)).accepted);

    for (const std::string invalid : {
             std::string("1test_prop-1"),
             std::string("-test_prop-1"),
             std::string("_test_prop-1"),
         }) {
        const palace::AssetAuthoringResult rejected =
            fixture.catalog.assignProp(
                invalid, staged.handle, 1U, 1U, "head");
        LOGOS_ASSERT_FALSE(rejected.accepted);
        LOGOS_ASSERT_EQ(
            rejected.reason,
            std::string("prop-assignment-invalid"));
    }

    LOGOS_ASSERT_TRUE(
        fixture.catalog.assignProp(
            "test_prop-1", staged.handle, 1U, 1U, "head")
            .accepted);
    palace::AssetAuthoringCatalog restarted;
    LOGOS_ASSERT_TRUE(restarted.initialize(
        fixture.instanceRoot.toStdString(), fixture.verified));
    LOGOS_ASSERT_TRUE(
        restarted.state().propAssignment.has_value());
    LOGOS_ASSERT_EQ(
        restarted.state().propAssignment->propId,
        std::string("test_prop-1"));
}

LOGOS_TEST(asset_authoring_duplicate_commit_is_idempotent)
{
    Fixture fixture;
    const std::string png = encodedPng();
    const palace::AssetAuthoringResult first =
        fixture.stage("first label", png);
    LOGOS_ASSERT_TRUE(first.accepted);
    LOGOS_ASSERT_TRUE(
        fixture.catalog.review(
            first.handle, "approve").accepted);

    const palace::AssetAuthoringResult duplicate =
        fixture.stage("replacement label", png);
    LOGOS_ASSERT_TRUE(duplicate.accepted);
    LOGOS_ASSERT_EQ(duplicate.handle, first.handle);
    LOGOS_ASSERT_EQ(fixture.catalog.assets().size(), 1U);
    LOGOS_ASSERT_EQ(
        fixture.catalog.asset(first.handle)->label,
        std::string("first label"));
    LOGOS_ASSERT_EQ(
        fixture.catalog.asset(first.handle)->reviewState,
        std::string("approved"));
}

LOGOS_TEST(asset_authoring_binds_draft_creator_once_and_persists)
{
    Fixture fixture;
    const std::string actor = std::string(64U, 'a');
    const std::string other = std::string(64U, 'b');

    const palace::AssetAuthoringResult bound =
        fixture.catalog.bindDraftCreator(actor);
    LOGOS_ASSERT_TRUE(bound.accepted);
    LOGOS_ASSERT_TRUE(
        fixture.catalog.draftCreatorAccountId().has_value());
    LOGOS_ASSERT_EQ(
        *fixture.catalog.draftCreatorAccountId(),
        actor);
    LOGOS_ASSERT_TRUE(
        fixture.catalog.bindDraftCreator(actor).accepted);

    const palace::AssetAuthoringResult rejected =
        fixture.catalog.bindDraftCreator(other);
    LOGOS_ASSERT_FALSE(rejected.accepted);
    LOGOS_ASSERT_EQ(
        rejected.reason,
        std::string("draft-creator-mismatch"));

    palace::AssetAuthoringCatalog restarted;
    LOGOS_ASSERT_TRUE(restarted.initialize(
        fixture.instanceRoot.toStdString(),
        fixture.verified));
    LOGOS_ASSERT_TRUE(
        restarted.draftCreatorAccountId().has_value());
    LOGOS_ASSERT_EQ(
        *restarted.draftCreatorAccountId(),
        actor);
}

LOGOS_TEST(asset_authoring_legacy_v1_draft_binds_only_after_valid_mutation)
{
    Fixture fixture;
    QFile output(
        fixture.instanceRoot + QStringLiteral("/asset-authoring-v1"));
    const std::string legacy = legacyV1EmptyRecord();
    LOGOS_ASSERT_TRUE(output.open(QIODevice::WriteOnly));
    LOGOS_ASSERT_EQ(
        output.write(
            legacy.data(),
            static_cast<qint64>(legacy.size())),
        static_cast<qint64>(legacy.size()));
    output.close();
    LOGOS_ASSERT_TRUE(
        output.setPermissions(
            QFileDevice::ReadOwner
            | QFileDevice::WriteOwner));

    palace::AssetAuthoringCatalog legacyCatalog;
    LOGOS_ASSERT_TRUE(legacyCatalog.initialize(
        fixture.instanceRoot.toStdString(),
        fixture.verified));
    LOGOS_ASSERT_FALSE(
        legacyCatalog.draftCreatorAccountId().has_value());

    const auto statusBefore =
        palace::core_detail::ensureDraftAssetAuthoringAuthorityV1(
            legacyCatalog,
            std::string(64U, 'a'));
    LOGOS_ASSERT_TRUE(statusBefore.accepted);
    LOGOS_ASSERT_TRUE(statusBefore.canAuthorAssets);
    LOGOS_ASSERT_TRUE(statusBefore.needsDraftCreatorBinding);
    LOGOS_ASSERT_EQ(
        statusBefore.reason,
        std::string("draft-creator-unclaimed"));

    const std::optional<std::string> actorA{
        std::string(64U, 'a')};
    const std::optional<std::string> actorB{
        std::string(64U, 'b')};

    palace::AssetAuthoringCatalog unavailableCatalog;
    const palace::AssetAuthoringResult unavailable =
        unavailableCatalog.begin("valid label", actorA);
    LOGOS_ASSERT_FALSE(unavailable.accepted);
    LOGOS_ASSERT_EQ(
        unavailable.reason,
        std::string("asset-state-unavailable"));
    LOGOS_ASSERT_FALSE(
        unavailableCatalog.draftCreatorAccountId().has_value());

    LOGOS_ASSERT_FALSE(
        legacyCatalog.begin("", actorA).accepted);
    LOGOS_ASSERT_FALSE(
        legacyCatalog.append(
            "not-a-session", 0U, base64("chunk"), actorA)
            .accepted);
    LOGOS_ASSERT_FALSE(
        legacyCatalog.commit("not-a-session", actorA).accepted);
    LOGOS_ASSERT_FALSE(
        legacyCatalog.cancel("not-a-session", actorA).accepted);
    LOGOS_ASSERT_FALSE(
        legacyCatalog.review(
            std::string(64U, 'c'), "approve", actorA).accepted);
    LOGOS_ASSERT_FALSE(
        legacyCatalog.assign(
            "atrium", std::string(64U, 'c'), actorA).accepted);
    LOGOS_ASSERT_FALSE(
        legacyCatalog.assignProp(
            "invalid prop",
            std::string(64U, 'c'),
            0U,
            0U,
            "layer",
            actorA).accepted);
    LOGOS_ASSERT_FALSE(
        legacyCatalog.lockAssignments(actorA).accepted);
    LOGOS_ASSERT_FALSE(
        legacyCatalog.draftCreatorAccountId().has_value());

    const palace::AssetAuthoringResult begun =
        legacyCatalog.begin("first valid mutation", actorB);
    LOGOS_ASSERT_TRUE(begun.accepted);
    LOGOS_ASSERT_TRUE(
        legacyCatalog.draftCreatorAccountId().has_value());
    LOGOS_ASSERT_EQ(
        *legacyCatalog.draftCreatorAccountId(),
        *actorB);

    palace::AssetAuthoringCatalog restarted;
    LOGOS_ASSERT_TRUE(restarted.initialize(
        fixture.instanceRoot.toStdString(), fixture.verified));
    LOGOS_ASSERT_TRUE(
        restarted.draftCreatorAccountId().has_value());
    LOGOS_ASSERT_EQ(
        *restarted.draftCreatorAccountId(),
        *actorB);

    const auto rejected =
        palace::core_detail::ensureDraftAssetAuthoringAuthorityV1(
            restarted,
            *actorA);
    LOGOS_ASSERT_FALSE(rejected.accepted);
    LOGOS_ASSERT_FALSE(rejected.canAuthorAssets);
    LOGOS_ASSERT_FALSE(rejected.needsDraftCreatorBinding);
    LOGOS_ASSERT_EQ(
        rejected.reason,
        std::string("draft-creator-mismatch"));
}

LOGOS_TEST(asset_authoring_rejects_invalid_png_at_commit)
{
    Fixture fixture;
    const palace::AssetAuthoringResult begun =
        fixture.catalog.begin("not png");
    LOGOS_ASSERT_TRUE(begun.accepted);
    LOGOS_ASSERT_TRUE(
        fixture.catalog.append(
            begun.sessionId, 0U,
            base64("not a png")).accepted);
    const palace::AssetAuthoringResult rejected =
        fixture.catalog.commit(begun.sessionId);
    LOGOS_ASSERT_FALSE(rejected.accepted);
    LOGOS_ASSERT_EQ(
        rejected.reason,
        std::string(
            "asset-png-raster-decoder-rejected"));
    LOGOS_ASSERT_EQ(fixture.catalog.sessionCount(), 1U);
}
