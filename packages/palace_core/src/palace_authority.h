#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace palace {

enum class CapabilityKind {
    PalaceOwner,
    RoomEdit,
    ModerateUser,
    ModerateAsset,
    SetRoomLock,
};

struct UserProfileV1 {
    std::string userId;
    std::string deliverySigningPublicKey;
    std::int64_t keyEpoch = 0;
};

struct RoomV1 {
    std::string roomId;
    bool locked = false;
    std::string sharedStateRoot;
    std::int64_t roomEpoch = 0;
};

struct CapabilityGrantV1 {
    std::string grantId;
    std::string palaceId;
    std::string roomId;
    std::string subjectUserId;
    CapabilityKind capability = CapabilityKind::RoomEdit;
    bool delegable = false;
    std::int64_t expiresAt = 0;
    bool revoked = false;
};

struct BanV1 {
    std::string banId;
    std::string palaceId;
    std::string roomId;
    std::string subjectUserId;
    std::string assetCid;
    std::string issuedBy;
    bool active = true;
};

struct AuthoritySnapshotV1 {
    std::string palaceId;
    std::string ownerUserId;
    std::string entryRoomId;
    std::vector<UserProfileV1> users;
    std::vector<RoomV1> rooms;
    std::vector<CapabilityGrantV1> grants;
    std::vector<BanV1> bans;
};

// Stores only a fully validated, finalized authority projection. Callers must
// not apply a Delivery hint or a pending action through this class.
class AuthorityProjection {
public:
    bool replaceFinalized(const AuthoritySnapshotV1& snapshot, std::int64_t finalizedAt);

    bool can(const std::string& subjectUserId,
             CapabilityKind capability,
             const std::string& roomId,
             std::int64_t now) const;
    bool isUserBanned(const std::string& subjectUserId, const std::string& roomId) const;
    bool isAssetBanned(const std::string& assetCid, const std::string& roomId) const;
    bool isRoomLocked(const std::string& roomId) const;
    std::string deliveryKeyFor(const std::string& userId, std::int64_t keyEpoch) const;
    const std::string& palaceId() const;
    std::int64_t finalizedAt() const;

private:
    AuthoritySnapshotV1 m_snapshot;
    std::int64_t m_finalizedAt = 0;
    bool m_hasSnapshot = false;
};

} // namespace palace
