#include "palace_projection.h"

#include "palace_sha256.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace palace {
namespace {

namespace fs = std::filesystem;

constexpr const char* kProjectionFileName = "projection-v1";
constexpr const char* kTemporaryFileName = "projection-v1.next";

bool flushFile(const fs::path& path)
{
#if defined(__unix__) || defined(__APPLE__)
    const int descriptor = ::open(path.c_str(), O_RDONLY);
    if (descriptor < 0)
        return false;
    const bool flushed = ::fsync(descriptor) == 0;
    ::close(descriptor);
    return flushed;
#else
    (void)path;
    return true;
#endif
}

bool flushDirectory(const fs::path& path)
{
#if defined(__unix__) || defined(__APPLE__)
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (descriptor < 0)
        return false;
    const bool flushed = ::fsync(descriptor) == 0;
    ::close(descriptor);
    return flushed;
#else
    (void)path;
    return true;
#endif
}

std::string serializeRecord(const PalaceProjection& projection)
{
    const std::string state = projection.canonicalLocalState();
    return crypto::sha256Hex(state) + "\n" + state;
}

bool parseRecord(const std::string& record, std::string& state)
{
    const std::size_t newline = record.find('\n');
    if (newline == std::string::npos)
        return false;
    const std::string checksum = record.substr(0, newline);
    state = record.substr(newline + 1U);
    return checksum.size() == 64U && checksum == crypto::sha256Hex(state);
}

} // namespace

std::string syncHealthName(SyncHealth health)
{
    switch (health) {
    case SyncHealth::FullySynchronized: return "fully_synchronized";
    case SyncHealth::Offline: return "offline";
    case SyncHealth::Degraded: return "degraded";
    case SyncHealth::Recovering: return "recovering";
    }
    return "degraded";
}

bool parseSyncHealth(const std::string& value, SyncHealth& health)
{
    if (value == "fully_synchronized") {
        health = SyncHealth::FullySynchronized;
    } else if (value == "offline") {
        health = SyncHealth::Offline;
    } else if (value == "degraded") {
        health = SyncHealth::Degraded;
    } else if (value == "recovering") {
        health = SyncHealth::Recovering;
    } else {
        return false;
    }
    return true;
}

PalaceProjection::PalaceProjection()
    : m_roomTitles{{"atrium", "Atrium"}, {"lounge", "Lounge"}}
    , m_currentRoomId("atrium")
{
}

bool PalaceProjection::enterRoom(const std::string& roomId)
{
    if (m_roomTitles.find(roomId) == m_roomTitles.end())
        return false;
    m_currentRoomId = roomId;
    return true;
}

const std::string& PalaceProjection::currentRoomId() const
{
    return m_currentRoomId;
}

const std::string& PalaceProjection::currentRoomTitle() const
{
    return m_roomTitles.at(m_currentRoomId);
}

void PalaceProjection::setSyncHealth(SyncHealth health)
{
    m_syncHealth = health;
}

SyncHealth PalaceProjection::syncHealth() const
{
    return m_syncHealth;
}

std::string PalaceProjection::canonicalLocalState() const
{
    return "version=1;room=" + m_currentRoomId + ";sync=" + syncHealthName(m_syncHealth);
}

bool PalaceProjection::restoreCanonicalLocalState(const std::string& serialized)
{
    static constexpr const char* kPrefix = "version=1;room=";
    if (serialized.rfind(kPrefix, 0) != 0)
        return false;
    const std::size_t syncMarker = serialized.find(";sync=", std::char_traits<char>::length(kPrefix));
    if (syncMarker == std::string::npos)
        return false;
    const std::string roomId = serialized.substr(std::char_traits<char>::length(kPrefix),
                                                  syncMarker - std::char_traits<char>::length(kPrefix));
    const std::string healthName = serialized.substr(syncMarker + 6U);
    SyncHealth restoredHealth = SyncHealth::Degraded;
    if (serialized.find(";sync=", syncMarker + 1U) != std::string::npos
        || m_roomTitles.find(roomId) == m_roomTitles.end()
        || !parseSyncHealth(healthName, restoredHealth)) {
        return false;
    }
    m_currentRoomId = roomId;
    m_syncHealth = restoredHealth;
    return true;
}

ProjectionStore::ProjectionStore(std::string directory)
    : m_directory(std::move(directory))
{
}

bool ProjectionStore::save(const PalaceProjection& projection) const
{
    if (m_directory.empty())
        return false;
    std::error_code error;
    const fs::path directory(m_directory);
    fs::create_directories(directory, error);
    if (error)
        return false;

    const fs::path temporary = directory / kTemporaryFileName;
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << serializeRecord(projection);
        output.flush();
        if (!output.good())
            return false;
    }
    if (!flushFile(temporary))
        return false;

    const fs::path destination = directory / kProjectionFileName;
    fs::rename(temporary, destination, error);
    if (error)
        return false;
    return flushDirectory(directory);
}

bool ProjectionStore::load(PalaceProjection& projection) const
{
    if (m_directory.empty())
        return false;
    std::ifstream input(fs::path(m_directory) / kProjectionFileName, std::ios::binary);
    if (!input)
        return false;
    const std::string record((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::string state;
    return parseRecord(record, state) && projection.restoreCanonicalLocalState(state);
}

} // namespace palace
