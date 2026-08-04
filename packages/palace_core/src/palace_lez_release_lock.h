#pragma once

#include <string>

#include "palace_lez.h"
#include "palace_lez_explorer_finality.h"

namespace palace {

class PalaceLezReleaseLock {
public:
    static const PalaceLezNetworkFingerprint& network();
    static const PalaceLezExplorerNetworkFingerprintV1& explorer();
    static const std::string& walletConfigJson();
    static const std::string& expectedModuleName();
    static const std::string& expectedSequencerOrigin();

    static bool acceptsLiveModule(
        const std::string& moduleName,
        const std::string& moduleVersion,
        const std::string& sequencerOrigin);
};

} // namespace palace
