#pragma once

#include <string>

namespace palace {

enum class PalaceHumanModerationTargetV1 {
    User,
    AssetCid,
};

struct PalaceHumanModerationRequestV1 {
    PalaceHumanModerationTargetV1 targetKind =
        PalaceHumanModerationTargetV1::User;
    std::string actionId;
    std::string programIdHex;
    std::string rootAccountIdHex;
    std::string issuerAccountIdHex;
    std::string grantIdHex;
    std::string target;
};

struct PalaceHumanModerationCommandV1 {
    bool accepted = false;
    std::string reason;
    std::string actionId;
    std::string banIdHex;
    std::string transitionJson;
    std::string transitionSha256Hex;
};

// Builds one exact Palace-scope schema-v3 ban transition from typed UI input.
// The ban ID is deterministic for the action and authority context, making a
// retry byte-identical without allowing QML to construct protocol JSON.
PalaceHumanModerationCommandV1 buildPalaceHumanModerationCommandV1(
    const PalaceHumanModerationRequestV1& request);

} // namespace palace
