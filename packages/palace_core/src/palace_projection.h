#pragma once

#include <map>
#include <string>

namespace palace {

enum class SyncHealth {
    FullySynchronized,
    Offline,
    Degraded,
    Recovering,
};

std::string syncHealthName(SyncHealth health);
bool parseSyncHealth(const std::string& value, SyncHealth& health);

class PalaceProjection {
public:
    PalaceProjection();

    bool enterRoom(const std::string& roomId);
    const std::string& currentRoomId() const;
    const std::string& currentRoomTitle() const;
    void setSyncHealth(SyncHealth health);
    SyncHealth syncHealth() const;

    std::string canonicalLocalState() const;
    bool restoreCanonicalLocalState(const std::string& serialized);

private:
    std::map<std::string, std::string> m_roomTitles;
    std::string m_currentRoomId;
    SyncHealth m_syncHealth = SyncHealth::Recovering;
};

// File boundary for module-private projection data. The record is checksummed,
// flushed, atomically renamed, and validated before it becomes live.
class ProjectionStore {
public:
    explicit ProjectionStore(std::string directory);

    bool save(const PalaceProjection& projection) const;
    bool load(PalaceProjection& projection) const;
    bool enterRoomDurably(PalaceProjection& projection,
                          const std::string& roomId) const;

private:
    std::string m_directory;
};

} // namespace palace
