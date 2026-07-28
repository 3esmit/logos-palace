#include <logos_test.h>

#include <filesystem>

#include "palace_projection.h"

LOGOS_TEST(projection_store_restores_room_and_explicit_offline_health) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path()
        / "logos-palace-projection-contract";
    palace::PalaceProjection projection;
    LOGOS_ASSERT_TRUE(projection.enterRoom("lounge"));
    projection.setSyncHealth(palace::SyncHealth::Offline);

    palace::ProjectionStore store(directory.string());
    LOGOS_ASSERT_TRUE(store.save(projection));

    palace::PalaceProjection restored;
    LOGOS_ASSERT_TRUE(store.load(restored));
    LOGOS_ASSERT_EQ(restored.currentRoomId(), std::string("lounge"));
    LOGOS_ASSERT_EQ(restored.currentRoomTitle(), std::string("Lounge"));
    LOGOS_ASSERT_EQ(palace::syncHealthName(restored.syncHealth()), std::string("offline"));
}

LOGOS_TEST(projection_rejects_bad_local_state_without_partial_mutation) {
    palace::PalaceProjection projection;
    projection.setSyncHealth(palace::SyncHealth::Degraded);
    LOGOS_ASSERT_FALSE(projection.restoreCanonicalLocalState("version=1;room=lounge;sync=unsafe"));
    LOGOS_ASSERT_EQ(projection.currentRoomId(), std::string("atrium"));
    LOGOS_ASSERT_EQ(palace::syncHealthName(projection.syncHealth()), std::string("degraded"));
    LOGOS_ASSERT_FALSE(projection.enterRoom("outside"));
}
