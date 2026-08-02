#include "palace_lez_release_lock.h"

namespace palace {

const PalaceLezNetworkFingerprint& PalaceLezReleaseLock::network()
{
    static const PalaceLezNetworkFingerprint value{
        "logos-lez-testnet-v0.2.0",
        "0.4.0-alpha.2",
        "10c6c1dd76107cb96e99f651fec3f61c35e09901",
        "3a96a23f7feecfc32b3f31beafd4f1768c691e42",
        "palace-schema-v3",
        "2b67563baf590c32dd82e50e3252815ec56bdaec",
        "e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61",
        "69099492e8f860ab338846f33762f5fb563ffc40121779ec1e15ef0b35da7171",
    };
    return value;
}

const PalaceLezExplorerNetworkFingerprintV1&
PalaceLezReleaseLock::explorer()
{
    static const PalaceLezExplorerNetworkFingerprintV1 value{
        1U,
        "logos-lez-testnet-v0.2.0",
        "0101010101010101010101010101010101010101010101010101010101010101",
        "https://explorer.testnet.lez.logos.co",
        "3022937127152978530",
        1U,
    };
    return value;
}

const std::string& PalaceLezReleaseLock::walletConfigJson()
{
    static const std::string value =
        "{\"sequencer_addr\":"
        "\"https://testnet.lez.logos.co\","
        "\"seq_poll_timeout\":\"2s\","
        "\"seq_tx_poll_max_blocks\":30,"
        "\"seq_poll_max_retries\":10,"
        "\"seq_block_poll_max_amount\":100}\n";
    return value;
}

const std::string& PalaceLezReleaseLock::expectedModuleName()
{
    static const std::string value = "lez_core";
    return value;
}

const std::string& PalaceLezReleaseLock::expectedSequencerOrigin()
{
    static const std::string value = "https://testnet.lez.logos.co";
    return value;
}

bool PalaceLezReleaseLock::acceptsLiveModule(
    const std::string& moduleName,
    const std::string& moduleVersion,
    const std::string& sequencerOrigin)
{
    const std::string& expectedOrigin = expectedSequencerOrigin();
    // The LEZ wallet reports an origin and may retain the URI root slash.
    // Treat only that spelling as equivalent; paths, ports, schemes, and
    // hosts remain exact release-lock inputs.
    const bool originMatches = sequencerOrigin == expectedOrigin
        || sequencerOrigin == expectedOrigin + "/";
    return moduleName == expectedModuleName()
        && moduleVersion == network().moduleApiVersion
        && originMatches;
}

} // namespace palace
