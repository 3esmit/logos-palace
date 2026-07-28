#include <logos_test.h>

#include <QDir>
#include <QTemporaryDir>

#include "palace_room_backgrounds.h"
#include "palace_verified_asset_store.h"

LOGOS_TEST(room_background_catalog_stages_two_stable_verified_handles) {
    QTemporaryDir temporary;
    LOGOS_ASSERT_TRUE(temporary.isValid());
    const QString instanceRoot = temporary.path() + QStringLiteral("/instance");
    LOGOS_ASSERT_TRUE(QDir().mkpath(instanceRoot));

    palace::VerifiedAssetStore store(instanceRoot.toStdString());
    palace::RoomBackgroundCatalog first;
    LOGOS_ASSERT_TRUE(first.stageBuiltInFixtures(store));
    LOGOS_ASSERT_EQ(
        first.handleForRoom("atrium"),
        std::string("3bd13dc41f3e27a7eabf45c73188498b95e3e5e475e6fcb967308477afd522be"));
    LOGOS_ASSERT_EQ(
        first.handleForRoom("lounge"),
        std::string("d2068f9cc4848b29882e532580c2b455ef5d243e7b38f16d590937fbef720486"));
    LOGOS_ASSERT_TRUE(store.verifiedPngPath(first.handleForRoom("atrium")).has_value());
    LOGOS_ASSERT_TRUE(store.verifiedPngPath(first.handleForRoom("lounge")).has_value());
    LOGOS_ASSERT_TRUE(first.handleForRoom("unknown").empty());

    palace::RoomBackgroundCatalog restarted;
    LOGOS_ASSERT_TRUE(restarted.stageBuiltInFixtures(store));
    LOGOS_ASSERT_EQ(restarted.handleForRoom("atrium"), first.handleForRoom("atrium"));
    LOGOS_ASSERT_EQ(restarted.handleForRoom("lounge"), first.handleForRoom("lounge"));
}

LOGOS_TEST(room_background_catalog_exposes_no_handle_when_staging_fails) {
    QTemporaryDir temporary;
    LOGOS_ASSERT_TRUE(temporary.isValid());
    palace::VerifiedAssetStore store(
        (temporary.path() + QStringLiteral("/missing/instance")).toStdString());
    palace::RoomBackgroundCatalog catalog;

    LOGOS_ASSERT_FALSE(catalog.stageBuiltInFixtures(store));
    LOGOS_ASSERT_TRUE(catalog.handleForRoom("atrium").empty());
    LOGOS_ASSERT_TRUE(catalog.handleForRoom("lounge").empty());
}
