#pragma once

#include <map>
#include <string>

namespace palace {

class VerifiedAssetStore;

// Stages the built-in MVP room art through the same verified-asset covenant
// used for Storage downloads. Only accepted digest handles leave this class.
class RoomBackgroundCatalog {
public:
    bool stageBuiltInFixtures(const VerifiedAssetStore& store);
    std::string handleForRoom(const std::string& roomId) const;

private:
    std::map<std::string, std::string> m_handles;
};

} // namespace palace
