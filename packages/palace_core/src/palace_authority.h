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

// Identifies the evidence that supplied the current authority snapshot.
// Locally committed state is usable only in the local-development profile;
// it must never be presented as public finality.
enum class AuthoritySnapshotSource : std::uint8_t {
    None = 0U,
    Finalized = 1U,
    LocalCommitted = 2U,
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
    // Pins delegated authority proven at issuance; current grant state still
    // controls future actions, not an already-finalized ban.
    std::string authorizationGrantId;
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

// Stores only a fully validated authority projection. Callers must not apply a
// Delivery hint or a pending action through this class. The source stays
// explicit so a local committed snapshot cannot silently become finality.
class AuthorityProjection {
public:
    bool replaceFinalized(const AuthoritySnapshotV1& snapshot, std::int64_t finalizedAt);
    bool replaceLocalCommitted(
        const AuthoritySnapshotV1& snapshot,
        std::int64_t committedAt);

    bool can(const std::string& subjectUserId,
             CapabilityKind capability,
             const std::string& roomId,
             std::int64_t now) const;
    bool isUserBanned(const std::string& subjectUserId, const std::string& roomId) const;
    bool isAssetBanned(const std::string& assetCid, const std::string& roomId) const;
    bool isRoomLocked(const std::string& roomId) const;
    std::string deliveryKeyFor(const std::string& userId, std::int64_t keyEpoch) const;
    const std::string& palaceId() const;
    const std::string& entryRoomId() const;
    std::int64_t roomEpoch(const std::string& roomId) const;
    std::int64_t finalizedAt() const;
    std::int64_t committedAt() const;
    AuthoritySnapshotSource source() const;

private:
    bool replace(
        const AuthoritySnapshotV1& snapshot,
        std::int64_t committedAt,
        AuthoritySnapshotSource source);

    AuthoritySnapshotV1 m_snapshot;
    std::int64_t m_committedAt = 0;
    AuthoritySnapshotSource m_source = AuthoritySnapshotSource::None;
    bool m_hasSnapshot = false;
};

const char* authoritySnapshotSourceName(AuthoritySnapshotSource source);

} // namespace palace
