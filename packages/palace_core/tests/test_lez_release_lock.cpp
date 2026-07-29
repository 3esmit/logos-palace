#include <logos_test.h>

#include "palace_lez_release_lock.h"

LOGOS_TEST(lez_release_lock_binds_wallet_module_runtime_program_and_explorer)
{
    const palace::PalaceLezNetworkFingerprint& network =
        palace::PalaceLezReleaseLock::network();
    LOGOS_ASSERT_EQ(
        network.programIdHex,
        std::string(
            "e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa"
            "0d188ec5a4b25c61"));
    LOGOS_ASSERT_EQ(network.programBytecodeSha256Hex.size(), 64U);
    LOGOS_ASSERT_EQ(network.moduleRevision.size(), 40U);
    LOGOS_ASSERT_EQ(network.runtimeRevision.size(), 40U);
    LOGOS_ASSERT_EQ(network.publicContractRevision.size(), 40U);

    const auto& explorer = palace::PalaceLezReleaseLock::explorer();
    LOGOS_ASSERT_EQ(explorer.networkId, network.networkId);
    LOGOS_ASSERT_EQ(explorer.channelIdHex.size(), 64U);
    LOGOS_ASSERT_EQ(
        explorer.explorerOrigin,
        std::string("https://explorer.testnet.lez.logos.co"));
    LOGOS_ASSERT_EQ(
        explorer.serverFunctionSuffix,
        std::string("3022937127152978530"));

    LOGOS_ASSERT_TRUE(palace::PalaceLezReleaseLock::acceptsLiveModule(
        "lez_core", "0.4.0-alpha.2",
        "https://testnet.lez.logos.co"));
}

LOGOS_TEST(lez_release_lock_rejects_any_live_surface_drift)
{
    LOGOS_ASSERT_FALSE(palace::PalaceLezReleaseLock::acceptsLiveModule(
        "lez-core", "0.4.0-alpha.2",
        "https://testnet.lez.logos.co"));
    LOGOS_ASSERT_FALSE(palace::PalaceLezReleaseLock::acceptsLiveModule(
        "lez_core", "0.4.0-alpha.3",
        "https://testnet.lez.logos.co"));
    LOGOS_ASSERT_FALSE(palace::PalaceLezReleaseLock::acceptsLiveModule(
        "lez_core", "0.4.0-alpha.2",
        "https://other.example"));
    LOGOS_ASSERT_TRUE(
        palace::PalaceLezReleaseLock::walletConfigJson().find(
            palace::PalaceLezReleaseLock::expectedSequencerOrigin())
        != std::string::npos);
}
