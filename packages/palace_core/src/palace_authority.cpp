#include "palace_authority.h"

#include <algorithm>
#include <set>

namespace palace {
namespace {

bool hasRoom(const AuthoritySnapshotV1& snapshot, const std::string& roomId)
{
    return std::any_of(snapshot.rooms.begin(), snapshot.rooms.end(), [&](const RoomV1& room) {
        return room.roomId == roomId;
    });
}

bool scopeMatches(const std::string& scopeRoomId, const std::string& roomId)
{
    return scopeRoomId.empty() || scopeRoomId == roomId;
}

bool grantActive(const CapabilityGrantV1& grant, std::int64_t now)
{
    return !grant.revoked && (grant.expiresAt == 0 || grant.expiresAt > now);
}

bool hasCapability(const AuthoritySnapshotV1& snapshot,
                   const std::string& subjectUserId,
                   CapabilityKind capability,
                   const std::string& roomId,
                   std::int64_t now)
{
    if (subjectUserId == snapshot.ownerUserId)
        return true;
    return std::any_of(snapshot.grants.begin(), snapshot.grants.end(), [&](const CapabilityGrantV1& grant) {
        return grant.subjectUserId == subjectUserId
            && grant.capability == capability
            && scopeMatches(grant.roomId, roomId)
            && grantActive(grant, now);
    });
}

bool uniqueAndNonEmpty(const std::vector<std::string>& values)
{
    std::set<std::string> seen;
    for (const std::string& value : values) {
        if (value.empty() || !seen.insert(value).second)
            return false;
    }
    return true;
}

bool validateSnapshot(const AuthoritySnapshotV1& snapshot, std::int64_t finalizedAt)
{
    if (snapshot.palaceId.empty() || snapshot.ownerUserId.empty() || finalizedAt <= 0)
        return false;

    std::vector<std::string> roomIds;
    for (const RoomV1& room : snapshot.rooms) {
        if (room.roomId.empty() || room.roomEpoch < 0 || room.sharedStateRoot.empty())
            return false;
        roomIds.push_back(room.roomId);
    }
    if (!uniqueAndNonEmpty(roomIds) || !hasRoom(snapshot, snapshot.entryRoomId))
        return false;

    std::vector<std::string> userIds;
    for (const UserProfileV1& user : snapshot.users) {
        if (user.userId.empty() || user.deliverySigningPublicKey.empty() || user.keyEpoch < 0)
            return false;
        userIds.push_back(user.userId);
    }
    if (!uniqueAndNonEmpty(userIds)
        || std::find(userIds.begin(), userIds.end(), snapshot.ownerUserId) == userIds.end()) {
        return false;
    }

    std::vector<std::string> grantIds;
    for (const CapabilityGrantV1& grant : snapshot.grants) {
        if (grant.grantId.empty() || grant.palaceId != snapshot.palaceId
            || grant.subjectUserId.empty() || grant.capability == CapabilityKind::PalaceOwner
            || (!grant.roomId.empty() && !hasRoom(snapshot, grant.roomId))) {
            return false;
        }
        grantIds.push_back(grant.grantId);
    }
    if (!uniqueAndNonEmpty(grantIds))
        return false;

    std::vector<std::string> banIds;
    for (const BanV1& ban : snapshot.bans) {
        const bool namesUser = !ban.subjectUserId.empty();
        const bool namesAsset = !ban.assetCid.empty();
        if (ban.banId.empty() || ban.palaceId != snapshot.palaceId || ban.issuedBy.empty()
            || namesUser == namesAsset || (!ban.roomId.empty() && !hasRoom(snapshot, ban.roomId))) {
            return false;
        }
        if (namesUser && !hasCapability(snapshot, ban.issuedBy, CapabilityKind::ModerateUser,
                                        ban.roomId, finalizedAt)) {
            return false;
        }
        if (namesAsset && !hasCapability(snapshot, ban.issuedBy, CapabilityKind::ModerateAsset,
                                         ban.roomId, finalizedAt)) {
            return false;
        }
        banIds.push_back(ban.banId);
    }
    return uniqueAndNonEmpty(banIds);
}

} // namespace

bool AuthorityProjection::replaceFinalized(const AuthoritySnapshotV1& snapshot,
                                           std::int64_t finalizedAt)
{
    if (!validateSnapshot(snapshot, finalizedAt))
        return false;
    m_snapshot = snapshot;
    m_finalizedAt = finalizedAt;
    m_hasSnapshot = true;
    return true;
}

bool AuthorityProjection::can(const std::string& subjectUserId,
                              CapabilityKind capability,
                              const std::string& roomId,
                              std::int64_t now) const
{
    return m_hasSnapshot && hasCapability(m_snapshot, subjectUserId, capability, roomId, now);
}

bool AuthorityProjection::isUserBanned(const std::string& subjectUserId,
                                        const std::string& roomId) const
{
    return m_hasSnapshot && std::any_of(m_snapshot.bans.begin(), m_snapshot.bans.end(),
        [&](const BanV1& ban) {
            return ban.active && ban.subjectUserId == subjectUserId && scopeMatches(ban.roomId, roomId);
        });
}

bool AuthorityProjection::isAssetBanned(const std::string& assetCid,
                                         const std::string& roomId) const
{
    return m_hasSnapshot && std::any_of(m_snapshot.bans.begin(), m_snapshot.bans.end(),
        [&](const BanV1& ban) {
            return ban.active && ban.assetCid == assetCid && scopeMatches(ban.roomId, roomId);
        });
}

bool AuthorityProjection::isRoomLocked(const std::string& roomId) const
{
    if (!m_hasSnapshot)
        return true;
    const auto found = std::find_if(m_snapshot.rooms.begin(), m_snapshot.rooms.end(),
        [&](const RoomV1& room) { return room.roomId == roomId; });
    return found == m_snapshot.rooms.end() || found->locked;
}

std::string AuthorityProjection::deliveryKeyFor(const std::string& userId,
                                                 std::int64_t keyEpoch) const
{
    if (!m_hasSnapshot)
        return {};
    const auto found = std::find_if(m_snapshot.users.begin(), m_snapshot.users.end(),
        [&](const UserProfileV1& user) { return user.userId == userId && user.keyEpoch == keyEpoch; });
    return found == m_snapshot.users.end() ? std::string{} : found->deliverySigningPublicKey;
}

const std::string& AuthorityProjection::palaceId() const
{
    static const std::string kEmpty;
    return m_hasSnapshot ? m_snapshot.palaceId : kEmpty;
}

std::int64_t AuthorityProjection::finalizedAt() const
{
    return m_finalizedAt;
}

} // namespace palace
