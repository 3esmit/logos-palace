#include <logos_test.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "palace_asset_authoring_store.h"
#include "palace_sha256.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

struct Fixture {
    QTemporaryDir temporary;
    QString instanceRoot;

    Fixture()
        : instanceRoot(
            temporary.path() + QStringLiteral("/instance"))
    {
        LOGOS_ASSERT_TRUE(temporary.isValid());
        LOGOS_ASSERT_TRUE(QDir().mkpath(instanceRoot));
    }
};

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
            (accumulator << (5U - bitCount)) & 0x1fU]);
    }
    return encoded;
}

palace::AssetAuthoringStateV1 lockedRoomState()
{
    palace::AssetAuthoringStateV1 state;
    palace::AssetAuthoringAssetV1 asset;
    asset.handle = std::string(64U, 'a');
    asset.label = "room image";
    asset.width = 2U;
    asset.height = 1U;
    asset.byteLength = 12U;
    asset.reviewState = "approved";
    asset.publishedCid = cidForDigest(asset.handle);
    state.assets.emplace(asset.handle, asset);
    state.roomAssignments.emplace("atrium", asset.handle);
    state.roomAssignments.emplace("lounge", asset.handle);
    state.bundleLocked = true;
    return state;
}

QString recordPath(const Fixture& fixture)
{
    return fixture.instanceRoot
        + QStringLiteral("/asset-authoring-v1");
}

} // namespace

LOGOS_TEST(asset_authoring_store_is_atomic_owner_only_and_sealed)
{
    Fixture fixture;
    palace::AssetAuthoringStore store(
        fixture.instanceRoot.toStdString());
    palace::AssetAuthoringStateV1 empty;
    LOGOS_ASSERT_TRUE(
        store.save(empty)
        == palace::AssetAuthoringStoreStatus::Saved);

    const QString path = recordPath(fixture);
    const QFileInfo record(path);
    LOGOS_ASSERT_TRUE(record.isFile());
    LOGOS_ASSERT_FALSE(record.isSymLink());
    LOGOS_ASSERT_TRUE(
        (record.permissions()
         & (QFileDevice::ReadGroup
            | QFileDevice::WriteGroup
            | QFileDevice::ReadOther
            | QFileDevice::WriteOther))
        == 0);

    palace::AssetAuthoringStateV1 restored;
    LOGOS_ASSERT_TRUE(
        store.load(restored)
        == palace::AssetAuthoringStoreStatus::Loaded);
    LOGOS_ASSERT_TRUE(restored.assets.empty());

    QFile tampered(path);
    LOGOS_ASSERT_TRUE(
        tampered.open(QIODevice::ReadWrite));
    LOGOS_ASSERT_TRUE(tampered.seek(0));
    LOGOS_ASSERT_EQ(tampered.write("X", 1), 1);
    tampered.close();
    LOGOS_ASSERT_TRUE(
        store.load(restored)
        == palace::AssetAuthoringStoreStatus::
            InvalidRecord);
}

LOGOS_TEST(asset_authoring_store_rejects_symlink)
{
    Fixture fixture;
    const QString path = recordPath(fixture);
    const QString target =
        fixture.instanceRoot + QStringLiteral("/target");
    QFile targetFile(target);
    LOGOS_ASSERT_TRUE(
        targetFile.open(QIODevice::WriteOnly));
    LOGOS_ASSERT_EQ(targetFile.write("target"), 6);
    targetFile.close();
    std::error_code error;
    std::filesystem::create_symlink(
        target.toStdString(),
        path.toStdString(),
        error);
    LOGOS_ASSERT_FALSE(static_cast<bool>(error));

    palace::AssetAuthoringStore store(
        fixture.instanceRoot.toStdString());
    palace::AssetAuthoringStateV1 restored;
    LOGOS_ASSERT_TRUE(
        store.load(restored)
        == palace::AssetAuthoringStoreStatus::
            InsecurePath);
    LOGOS_ASSERT_TRUE(
        store.save({})
        == palace::AssetAuthoringStoreStatus::
            InsecurePath);
}

LOGOS_TEST(asset_authoring_store_restores_locked_rooms_without_a_prop)
{
    Fixture fixture;
    palace::AssetAuthoringStore store(
        fixture.instanceRoot.toStdString());
    const palace::AssetAuthoringStateV1 source = lockedRoomState();
    LOGOS_ASSERT_FALSE(source.propAssignment.has_value());
    LOGOS_ASSERT_TRUE(
        store.save(source)
        == palace::AssetAuthoringStoreStatus::Saved);

    palace::AssetAuthoringStateV1 restored;
    LOGOS_ASSERT_TRUE(
        store.load(restored)
        == palace::AssetAuthoringStoreStatus::Loaded);
    LOGOS_ASSERT_TRUE(restored.bundleLocked);
    LOGOS_ASSERT_FALSE(restored.propAssignment.has_value());
    LOGOS_ASSERT_EQ(
        restored.roomAssignments.at("atrium"),
        source.roomAssignments.at("atrium"));
    LOGOS_ASSERT_EQ(
        restored.roomAssignments.at("lounge"),
        source.roomAssignments.at("lounge"));
}

LOGOS_TEST(asset_authoring_store_rejects_invalid_generic_state)
{
    Fixture fixture;
    palace::AssetAuthoringStore store(
        fixture.instanceRoot.toStdString());
    palace::AssetAuthoringStateV1 state;
    palace::AssetAuthoringAssetV1 asset;
    asset.handle = std::string(64U, 'a');
    asset.label = "asset";
    asset.width = 2U;
    asset.height = 1U;
    asset.byteLength = 12U;
    asset.reviewState = "approved";
    asset.publishedCid = "not-a-cid";
    state.assets.emplace(asset.handle, asset);
    LOGOS_ASSERT_TRUE(
        store.save(state)
        == palace::AssetAuthoringStoreStatus::
            InvalidArgument);

    asset.publishedCid.clear();
    state.assets.clear();
    state.assets.emplace(asset.handle, asset);
    state.roomAssignments.emplace("atrium", asset.handle);
    LOGOS_ASSERT_TRUE(
        store.save(state)
        == palace::AssetAuthoringStoreStatus::
            InvalidArgument);
}

LOGOS_TEST(asset_authoring_store_persists_draft_creator)
{
    Fixture fixture;
    palace::AssetAuthoringStore store(
        fixture.instanceRoot.toStdString());
    palace::AssetAuthoringStateV1 source = lockedRoomState();
    source.bundleLocked = false;
    source.roomAssignments.clear();
    source.draftCreatorAccountId = std::string(64U, 'b');
    LOGOS_ASSERT_TRUE(
        store.save(source)
        == palace::AssetAuthoringStoreStatus::Saved);

    palace::AssetAuthoringStateV1 restored;
    LOGOS_ASSERT_TRUE(
        store.load(restored)
        == palace::AssetAuthoringStoreStatus::Loaded);
    LOGOS_ASSERT_TRUE(restored.draftCreatorAccountId.has_value());
    LOGOS_ASSERT_EQ(
        *restored.draftCreatorAccountId,
        std::string(64U, 'b'));
}

LOGOS_TEST(asset_authoring_store_loads_legacy_v1_without_draft_creator)
{
    Fixture fixture;
    const std::string body =
        "logos-palace-asset-authoring-v1\n"
        "version=1\n"
        "locked=0\n"
        "assets=0\n"
        "assignment=atrium;-\n"
        "assignment=lounge;-\n"
        "prop=-\n";
    const std::string encoded =
        body + "checksum=" + palace::crypto::sha256Hex(body) + '\n';
    QFile output(recordPath(fixture));
    LOGOS_ASSERT_TRUE(output.open(QIODevice::WriteOnly));
    LOGOS_ASSERT_EQ(
        output.write(
            encoded.data(),
            static_cast<qint64>(encoded.size())),
        static_cast<qint64>(encoded.size()));
    output.close();
    LOGOS_ASSERT_TRUE(
        output.setPermissions(
            QFileDevice::ReadOwner
            | QFileDevice::WriteOwner));

    palace::AssetAuthoringStore store(
        fixture.instanceRoot.toStdString());
    palace::AssetAuthoringStateV1 restored;
    LOGOS_ASSERT_TRUE(
        store.load(restored)
        == palace::AssetAuthoringStoreStatus::Loaded);
    LOGOS_ASSERT_FALSE(restored.draftCreatorAccountId.has_value());
    LOGOS_ASSERT_TRUE(restored.assets.empty());
}
