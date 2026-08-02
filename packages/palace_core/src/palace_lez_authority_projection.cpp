#include "palace_lez_authority_projection.h"

#include "palace_sha256.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <type_traits>
#include <utility>

namespace palace {
namespace {

constexpr std::uint16_t kSchemaVersion = 3U;
constexpr std::size_t kMaxTitleBytes = 64U;
constexpr std::size_t kMaxDisplayNameBytes = 48U;
constexpr std::size_t kMaxCidBytes = 128U;
constexpr std::size_t kMaxSharedKeyBytes = 32U;
constexpr std::size_t kMaxSharedValueBytes = 512U;
constexpr std::uint32_t kMaxUsers = 64U;
constexpr std::uint32_t kMaxGrants = 64U;
constexpr std::uint32_t kMaxBans = 128U;
constexpr std::uint32_t kMaxSharedStates = 128U;
constexpr std::uint32_t kCapModerateUser = 1U << 0U;
constexpr std::uint32_t kCapModerateAsset = 1U << 1U;
constexpr std::uint32_t kCapSetRoomLock = 1U << 2U;
constexpr std::uint32_t kCapWriteSharedState = 1U << 3U;
constexpr std::uint32_t kCapRoomEdit = 1U << 4U;
constexpr std::uint32_t kAllCapabilities =
    kCapModerateUser | kCapModerateAsset | kCapSetRoomLock
    | kCapWriteSharedState | kCapRoomEdit;
constexpr std::size_t kMaxAccountDataBytes = 4096U;

class BorshWriter {
public:
    void u8(const std::uint8_t value)
    {
        if (bytes.size() >= kMaxAccountDataBytes) {
            valid = false;
            return;
        }
        bytes.push_back(value);
    }

    void u16(const std::uint16_t value)
    {
        u8(static_cast<std::uint8_t>(value));
        u8(static_cast<std::uint8_t>(value >> 8U));
    }

    void u32(const std::uint32_t value)
    {
        for (unsigned shift = 0U; shift < 32U; shift += 8U)
            u8(static_cast<std::uint8_t>(value >> shift));
    }

    void u64(const std::uint64_t value)
    {
        for (unsigned shift = 0U; shift < 64U; shift += 8U)
            u8(static_cast<std::uint8_t>(value >> shift));
    }

    void boolean(const bool value) { u8(value ? 1U : 0U); }

    void fixedBytes(const PalaceLezBytes32& value)
    {
        if (value.size() > kMaxAccountDataBytes - bytes.size()) {
            valid = false;
            return;
        }
        bytes.insert(bytes.end(), value.begin(), value.end());
    }

    void string(const std::string& value)
    {
        if (value.size() > std::numeric_limits<std::uint32_t>::max()
            || value.size() + 4U > kMaxAccountDataBytes - bytes.size()) {
            valid = false;
            return;
        }
        u32(static_cast<std::uint32_t>(value.size()));
        bytes.insert(bytes.end(), value.begin(), value.end());
    }

    void optionalString(const std::optional<std::string>& value)
    {
        u8(value.has_value() ? 1U : 0U);
        if (value.has_value())
            string(*value);
    }

    void byteVector(const std::vector<std::uint8_t>& value)
    {
        if (value.size() > std::numeric_limits<std::uint32_t>::max()
            || value.size() + 4U > kMaxAccountDataBytes - bytes.size()) {
            valid = false;
            return;
        }
        u32(static_cast<std::uint32_t>(value.size()));
        bytes.insert(bytes.end(), value.begin(), value.end());
    }

    void scope(const PalaceLezScopeV3& value)
    {
        u8(static_cast<std::uint8_t>(value.kind));
        if (value.kind == PalaceLezScopeKindV3::Room)
            fixedBytes(value.roomId);
    }

    std::vector<std::uint8_t> bytes;
    bool valid = true;
};

bool isNonzero(const PalaceLezBytes32& value)
{
    return std::any_of(value.begin(), value.end(), [](const std::uint8_t byte) {
        return byte != 0U;
    });
}

bool isZero(const PalaceLezBytes32& value)
{
    return !isNonzero(value);
}

bool isLowerHex(const std::string& value, const std::size_t length)
{
    return value.size() == length
        && std::all_of(value.begin(), value.end(), [](const unsigned char character) {
            return (character >= '0' && character <= '9')
                || (character >= 'a' && character <= 'f');
        });
}

std::string bytesHex(const PalaceLezBytes32& value)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2U);
    for (const std::uint8_t byte : value) {
        result.push_back(kHex[byte >> 4U]);
        result.push_back(kHex[byte & 0x0fU]);
    }
    return result;
}

bool decodeUtf8CodePoint(const std::string& value, std::size_t& cursor, std::uint32_t& codePoint)
{
    const auto first = static_cast<std::uint8_t>(value[cursor]);
    std::size_t continuationCount = 0U;
    std::uint32_t minimum = 0U;
    if (first <= 0x7fU) {
        codePoint = first;
        ++cursor;
        return true;
    }
    if ((first & 0xe0U) == 0xc0U) {
        continuationCount = 1U;
        codePoint = first & 0x1fU;
        minimum = 0x80U;
    } else if ((first & 0xf0U) == 0xe0U) {
        continuationCount = 2U;
        codePoint = first & 0x0fU;
        minimum = 0x800U;
    } else if ((first & 0xf8U) == 0xf0U) {
        continuationCount = 3U;
        codePoint = first & 0x07U;
        minimum = 0x10000U;
    } else {
        return false;
    }
    if (continuationCount > value.size() - cursor - 1U)
        return false;
    for (std::size_t index = 0; index < continuationCount; ++index) {
        const auto continuation =
            static_cast<std::uint8_t>(value[cursor + index + 1U]);
        if ((continuation & 0xc0U) != 0x80U)
            return false;
        codePoint = (codePoint << 6U) | (continuation & 0x3fU);
    }
    cursor += continuationCount + 1U;
    return codePoint >= minimum && codePoint <= 0x10ffffU
        && !(codePoint >= 0xd800U && codePoint <= 0xdfffU);
}

bool isText(const std::string& value, const std::size_t maximumBytes)
{
    if (value.empty() || value.size() > maximumBytes)
        return false;
    std::size_t cursor = 0U;
    while (cursor < value.size()) {
        std::uint32_t codePoint = 0U;
        if (!decodeUtf8CodePoint(value, cursor, codePoint)
            || codePoint <= 0x1fU || (codePoint >= 0x7fU && codePoint <= 0x9fU)) {
            return false;
        }
    }
    return true;
}

bool isAsciiAlphanumeric(const unsigned char character)
{
    return (character >= '0' && character <= '9')
        || (character >= 'A' && character <= 'Z')
        || (character >= 'a' && character <= 'z');
}

bool isCid(const std::string& value)
{
    return value.size() >= 4U && value.size() <= kMaxCidBytes
        && std::all_of(value.begin(), value.end(), isAsciiAlphanumeric);
}

bool isAsciiIdentifier(const std::string& value, const std::size_t maximumBytes)
{
    return !value.empty() && value.size() <= maximumBytes
        && std::all_of(value.begin(), value.end(), [](const unsigned char character) {
            return isAsciiAlphanumeric(character) || character == '_'
                || character == '-';
        });
}

bool validScope(const PalaceLezScopeV3& scope)
{
    return (scope.kind == PalaceLezScopeKindV3::Palace && isZero(scope.roomId))
        || (scope.kind == PalaceLezScopeKindV3::Room && isNonzero(scope.roomId));
}

bool scopeNamesRoom(
    const PalaceLezScopeV3& scope,
    const std::array<PalaceLezBytes32, 2>& roomIds)
{
    return validScope(scope)
        && (scope.kind == PalaceLezScopeKindV3::Palace
            || scope.roomId == roomIds[0] || scope.roomId == roomIds[1]);
}

bool scopeCovers(const PalaceLezScopeV3& grant, const PalaceLezScopeV3& requested)
{
    return grant.kind == PalaceLezScopeKindV3::Palace
        || (grant.kind == PalaceLezScopeKindV3::Room
            && requested.kind == PalaceLezScopeKindV3::Room
            && grant.roomId == requested.roomId);
}

bool fitsSigned(const std::uint64_t value)
{
    return value <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
}

bool encodeRecord(
    const PalaceLezRecordTypeV3 recordType,
    const PalaceLezRecordV3& record,
    std::vector<std::uint8_t>& output)
{
    BorshWriter writer;
    writer.u8(static_cast<std::uint8_t>(recordType));
    writer.u16(kSchemaVersion);
    bool matched = true;
    switch (recordType) {
    case PalaceLezRecordTypeV3::PalaceRoot: {
        const auto* value = std::get_if<PalaceLezRootRecordV3>(&record);
        if (value == nullptr) {
            matched = false;
            break;
        }
        writer.fixedBytes(value->palaceId);
        writer.string(value->title);
        writer.fixedBytes(value->owner);
        writer.fixedBytes(value->entryRoomId);
        writer.fixedBytes(value->roomIds[0]);
        writer.fixedBytes(value->roomIds[1]);
        writer.string(value->activeManifestCid);
        writer.u32(value->userCount);
        writer.u32(value->grantCount);
        writer.u32(value->banCount);
        writer.u32(value->sharedStateCount);
        writer.u64(value->revision);
        writer.u64(value->lastOrderedActionId);
        break;
    }
    case PalaceLezRecordTypeV3::UserProfile: {
        const auto* value = std::get_if<PalaceLezUserProfileRecordV3>(&record);
        if (value == nullptr) {
            matched = false;
            break;
        }
        writer.fixedBytes(value->palaceId);
        writer.fixedBytes(value->userId);
        writer.string(value->displayName);
        writer.fixedBytes(value->deliveryKey);
        writer.u64(value->keyEpoch);
        writer.optionalString(value->avatarManifestCid);
        writer.u64(value->profileRevision);
        break;
    }
    case PalaceLezRecordTypeV3::Room: {
        const auto* value = std::get_if<PalaceLezRoomRecordV3>(&record);
        if (value == nullptr) {
            matched = false;
            break;
        }
        writer.fixedBytes(value->palaceId);
        writer.fixedBytes(value->roomId);
        writer.string(value->title);
        writer.string(value->manifestCid);
        writer.string(value->scriptBundleCid);
        writer.u8(static_cast<std::uint8_t>(value->vmProfile));
        writer.boolean(value->locked);
        writer.u64(value->revision);
        break;
    }
    case PalaceLezRecordTypeV3::CapabilityGrant: {
        const auto* value =
            std::get_if<PalaceLezCapabilityGrantRecordV3>(&record);
        if (value == nullptr) {
            matched = false;
            break;
        }
        writer.fixedBytes(value->palaceId);
        writer.fixedBytes(value->grantId);
        writer.fixedBytes(value->subjectUserId);
        writer.fixedBytes(value->issuedBy);
        writer.scope(value->scope);
        writer.u32(value->capabilities);
        writer.boolean(value->delegable);
        writer.u64(value->validThroughActionId);
        writer.boolean(value->revoked);
        writer.u64(value->revision);
        break;
    }
    case PalaceLezRecordTypeV3::Ban: {
        const auto* value = std::get_if<PalaceLezBanRecordV3>(&record);
        if (value == nullptr) {
            matched = false;
            break;
        }
        writer.fixedBytes(value->palaceId);
        writer.fixedBytes(value->banId);
        writer.u8(static_cast<std::uint8_t>(value->targetKind));
        if (value->targetKind == PalaceLezBanTargetKindV3::User)
            writer.fixedBytes(value->targetUserId);
        else if (value->targetKind == PalaceLezBanTargetKindV3::AssetCid)
            writer.string(value->targetAssetCid);
        else
            matched = false;
        if (!matched)
            break;
        writer.fixedBytes(value->issuer);
        writer.scope(value->scope);
        writer.boolean(value->active);
        writer.u64(value->revision);
        break;
    }
    case PalaceLezRecordTypeV3::RoomSharedState: {
        const auto* value =
            std::get_if<PalaceLezRoomSharedStateRecordV3>(&record);
        if (value == nullptr) {
            matched = false;
            break;
        }
        writer.fixedBytes(value->palaceId);
        writer.fixedBytes(value->sharedStateId);
        writer.fixedBytes(value->roomId);
        writer.string(value->key);
        writer.byteVector(value->value);
        writer.fixedBytes(value->stateRoot);
        writer.u64(value->revision);
        writer.u64(value->lastOrderedActionId);
        break;
    }
    default:
        matched = false;
        break;
    }
    if (!matched || !writer.valid || writer.bytes.size() > kMaxAccountDataBytes)
        return false;
    output = std::move(writer.bytes);
    return true;
}

bool validRoot(const PalaceLezRootRecordV3& value)
{
    return isNonzero(value.palaceId) && isText(value.title, kMaxTitleBytes)
        && isNonzero(value.owner) && isNonzero(value.entryRoomId)
        && isNonzero(value.roomIds[0]) && isNonzero(value.roomIds[1])
        && value.roomIds[0] != value.roomIds[1]
        && (value.entryRoomId == value.roomIds[0]
            || value.entryRoomId == value.roomIds[1])
        && isCid(value.activeManifestCid)
        && value.userCount >= 1U && value.userCount <= kMaxUsers
        && value.grantCount >= 1U && value.grantCount <= kMaxGrants
        && value.banCount <= kMaxBans
        && value.sharedStateCount <= kMaxSharedStates
        && value.revision == value.lastOrderedActionId;
}

bool validProfile(const PalaceLezUserProfileRecordV3& value)
{
    return isNonzero(value.palaceId) && isNonzero(value.userId)
        && isText(value.displayName, kMaxDisplayNameBytes)
        && isNonzero(value.deliveryKey) && value.keyEpoch != 0U
        && fitsSigned(value.keyEpoch)
        && (!value.avatarManifestCid.has_value()
            || isCid(*value.avatarManifestCid));
}

bool validRoom(const PalaceLezRoomRecordV3& value)
{
    return isNonzero(value.palaceId) && isNonzero(value.roomId)
        && isText(value.title, kMaxTitleBytes) && isCid(value.manifestCid)
        && isCid(value.scriptBundleCid)
        && value.vmProfile == PalaceLezVmProfileV3::IptScraeMvpV1
        && fitsSigned(value.revision);
}

bool validGrant(const PalaceLezCapabilityGrantRecordV3& value)
{
    return isNonzero(value.palaceId) && isNonzero(value.grantId)
        && isNonzero(value.subjectUserId) && isNonzero(value.issuedBy)
        && validScope(value.scope) && value.capabilities != 0U
        && (value.capabilities & ~kAllCapabilities) == 0U
        && value.validThroughActionId != 0U;
}

bool validBan(const PalaceLezBanRecordV3& value)
{
    if (!isNonzero(value.palaceId) || !isNonzero(value.banId)
        || !isNonzero(value.issuer) || !validScope(value.scope)) {
        return false;
    }
    if (value.targetKind == PalaceLezBanTargetKindV3::User)
        return isNonzero(value.targetUserId) && value.targetAssetCid.empty();
    if (value.targetKind == PalaceLezBanTargetKindV3::AssetCid)
        return isZero(value.targetUserId) && isCid(value.targetAssetCid);
    return false;
}

bool validSharedState(const PalaceLezRoomSharedStateRecordV3& value)
{
    return isNonzero(value.palaceId) && isNonzero(value.sharedStateId)
        && isNonzero(value.roomId)
        && isAsciiIdentifier(value.key, kMaxSharedKeyBytes)
        && value.value.size() <= kMaxSharedValueBytes
        && isNonzero(value.stateRoot) && value.revision != 0U
        && value.lastOrderedActionId != 0U
        && value.revision <= value.lastOrderedActionId;
}

bool accountMetadataValid(
    const PalaceLezPublicAccountV3& account,
    const std::string& programOwnerHex)
{
    if (!account.accepted || account.reason != "accepted"
        || !isLowerHex(account.programOwnerHex, 64U)
        || !isLowerHex(account.balanceLeHex, 32U)
        || !isLowerHex(account.nonceLeHex, 32U)
        || !isLowerHex(account.dataSha256Hex, 64U)
        || account.programOwnerHex != programOwnerHex) {
        return false;
    }
    std::vector<std::uint8_t> canonical;
    if (!encodeRecord(account.recordType, account.record, canonical))
        return false;
    const std::string bytes(canonical.begin(), canonical.end());
    return account.dataSha256Hex == crypto::sha256Hex(bytes);
}

bool namedAccountIdValid(
    const PalaceLezNamedFinalizedAccountV1& named,
    const std::string& expectedAccountIdHex)
{
    return isLowerHex(named.accountIdHex, 64U)
        && named.accountIdHex != std::string(64U, '0')
        && !expectedAccountIdHex.empty()
        && named.accountIdHex == expectedAccountIdHex;
}

std::string recordTag(const PalaceLezRecordTypeV3 recordType)
{
    switch (recordType) {
    case PalaceLezRecordTypeV3::UserProfile:
        return "profile";
    case PalaceLezRecordTypeV3::Room:
        return "room";
    case PalaceLezRecordTypeV3::CapabilityGrant:
        return "grant";
    case PalaceLezRecordTypeV3::Ban:
        return "ban";
    case PalaceLezRecordTypeV3::RoomSharedState:
        return "shared";
    case PalaceLezRecordTypeV3::PalaceRoot:
    default:
        return {};
    }
}

const PalaceLezBytes32* recordStableId(
    const PalaceLezRecordTypeV3 recordType,
    const PalaceLezRecordV3& record)
{
    switch (recordType) {
    case PalaceLezRecordTypeV3::UserProfile: {
        const auto* value =
            std::get_if<PalaceLezUserProfileRecordV3>(&record);
        return value == nullptr ? nullptr : &value->userId;
    }
    case PalaceLezRecordTypeV3::Room: {
        const auto* value = std::get_if<PalaceLezRoomRecordV3>(&record);
        return value == nullptr ? nullptr : &value->roomId;
    }
    case PalaceLezRecordTypeV3::CapabilityGrant: {
        const auto* value =
            std::get_if<PalaceLezCapabilityGrantRecordV3>(&record);
        return value == nullptr ? nullptr : &value->grantId;
    }
    case PalaceLezRecordTypeV3::Ban: {
        const auto* value = std::get_if<PalaceLezBanRecordV3>(&record);
        return value == nullptr ? nullptr : &value->banId;
    }
    case PalaceLezRecordTypeV3::RoomSharedState: {
        const auto* value =
            std::get_if<PalaceLezRoomSharedStateRecordV3>(&record);
        return value == nullptr ? nullptr : &value->sharedStateId;
    }
    case PalaceLezRecordTypeV3::PalaceRoot:
    default:
        return nullptr;
    }
}

PalaceLezAuthorityProjectionResultV1 reject(const std::string& reason)
{
    PalaceLezAuthorityProjectionResultV1 result;
    result.reason = reason;
    return result;
}

std::string roomScopeId(const PalaceLezScopeV3& scope)
{
    return scope.kind == PalaceLezScopeKindV3::Room
        ? bytesHex(scope.roomId)
        : std::string{};
}

bool liveGrant(
    const PalaceLezCapabilityGrantRecordV3& grant,
    const PalaceLezRootRecordV3& root)
{
    return !grant.revoked
        && grant.validThroughActionId >= root.lastOrderedActionId;
}

const PalaceLezCapabilityGrantRecordV3* banAuthorizingGrant(
    const PalaceLezBanRecordV3& ban,
    const PalaceLezRootRecordV3& root,
    const std::vector<PalaceLezCapabilityGrantRecordV3>& grants)
{
    if (ban.issuer == root.owner)
        return nullptr;
    const std::uint32_t needed =
        ban.targetKind == PalaceLezBanTargetKindV3::User
        ? kCapModerateUser
        : kCapModerateAsset;
    // The guest authorized the immutable ban when it was created. A later
    // grant revocation must remove future authority without erasing that
    // finalized moderation decision.
    const auto grant = std::find_if(grants.begin(), grants.end(),
        [&](const PalaceLezCapabilityGrantRecordV3& grant) {
            return grant.subjectUserId == ban.issuer
                && grant.issuedBy == root.owner
                && scopeCovers(grant.scope, ban.scope)
                && (grant.capabilities & needed) == needed;
        });
    return grant == grants.end() ? nullptr : &*grant;
}

bool banAuthorized(
    const PalaceLezBanRecordV3& ban,
    const PalaceLezRootRecordV3& root,
    const std::vector<PalaceLezCapabilityGrantRecordV3>& grants)
{
    return ban.issuer == root.owner
        || banAuthorizingGrant(ban, root, grants) != nullptr;
}

void appendMappedGrant(
    AuthoritySnapshotV1& snapshot,
    const PalaceLezCapabilityGrantRecordV3& grant,
    const PalaceLezRootRecordV3& root,
    const std::uint32_t bit,
    const CapabilityKind kind,
    const char* suffix)
{
    if ((grant.capabilities & bit) == 0U)
        return;
    CapabilityGrantV1 projected;
    projected.grantId = bytesHex(grant.grantId) + suffix;
    projected.palaceId = snapshot.palaceId;
    projected.roomId = roomScopeId(grant.scope);
    projected.subjectUserId = bytesHex(grant.subjectUserId);
    projected.capability = kind;
    projected.delegable = grant.delegable;
    projected.revoked = !liveGrant(grant, root);
    snapshot.grants.push_back(std::move(projected));
}

} // namespace

std::string canonicalPalaceLezRecordDigestV3(
    const PalaceLezRecordTypeV3 recordType,
    const PalaceLezRecordV3& record)
{
    std::vector<std::uint8_t> canonical;
    if (!encodeRecord(recordType, record, canonical))
        return {};
    return crypto::sha256Hex(std::string(canonical.begin(), canonical.end()));
}

PalaceLezAuthorityProjectionResultV1 projectLezAuthorityV1(
    const PalaceLezNamedAuthorityAccountV1& root,
    const std::vector<PalaceLezNamedAuthorityAccountV1>& children)
{
    const PalaceLezPublicAccountV3& rootAccount = root.account;
    if (!isLowerHex(rootAccount.programOwnerHex, 64U)
        || rootAccount.programOwnerHex
            == std::string(rootAccount.programOwnerHex.size(), '0')
        || !namedAccountIdValid(
            root,
            PalaceLezCodec::deriveRootPda(rootAccount.programOwnerHex))
        || !accountMetadataValid(rootAccount, rootAccount.programOwnerHex)
        || rootAccount.recordType != PalaceLezRecordTypeV3::PalaceRoot) {
        return reject("invalid-authority-root");
    }
    const auto* rootRecord =
        std::get_if<PalaceLezRootRecordV3>(&rootAccount.record);
    if (rootRecord == nullptr || !validRoot(*rootRecord))
        return reject("invalid-authority-root-record");

    const std::uint64_t expectedChildren =
        static_cast<std::uint64_t>(rootRecord->userCount)
        + 2U + static_cast<std::uint64_t>(rootRecord->grantCount)
        + static_cast<std::uint64_t>(rootRecord->banCount)
        + static_cast<std::uint64_t>(rootRecord->sharedStateCount);
    if (children.size() != expectedChildren)
        return reject("child-count-mismatch");

    std::vector<PalaceLezUserProfileRecordV3> profiles;
    std::vector<PalaceLezRoomRecordV3> rooms;
    std::vector<PalaceLezCapabilityGrantRecordV3> grants;
    std::vector<PalaceLezBanRecordV3> bans;
    std::vector<PalaceLezRoomSharedStateRecordV3> sharedStates;
    profiles.reserve(rootRecord->userCount);
    rooms.reserve(2U);
    grants.reserve(rootRecord->grantCount);
    bans.reserve(rootRecord->banCount);
    sharedStates.reserve(rootRecord->sharedStateCount);

    std::set<std::string> dataDigests;
    std::set<std::string> accountIds{root.accountIdHex};
    std::set<std::string> stableIds{
        bytesHex(rootRecord->palaceId),
        bytesHex(rootRecord->roomIds[0]),
        bytesHex(rootRecord->roomIds[1]),
    };
    for (const PalaceLezNamedAuthorityAccountV1& namedChild : children) {
        const PalaceLezPublicAccountV3& child = namedChild.account;
        const std::string tag = recordTag(child.recordType);
        const PalaceLezBytes32* stableId =
            recordStableId(child.recordType, child.record);
        const std::string expectedAccountId =
            stableId == nullptr || tag.empty()
            ? std::string{}
            : PalaceLezCodec::deriveRecordPda(
                rootAccount.programOwnerHex, tag,
                root.accountIdHex, *stableId);
        if (!accountMetadataValid(child, rootAccount.programOwnerHex)
            || child.recordType == PalaceLezRecordTypeV3::PalaceRoot
            || !namedAccountIdValid(namedChild, expectedAccountId)
            || !accountIds.insert(namedChild.accountIdHex).second
            || !dataDigests.insert(child.dataSha256Hex).second) {
            return reject("invalid-or-duplicate-child");
        }
        switch (child.recordType) {
        case PalaceLezRecordTypeV3::UserProfile: {
            const auto* value =
                std::get_if<PalaceLezUserProfileRecordV3>(&child.record);
            if (value == nullptr || !validProfile(*value)
                || value->palaceId != rootRecord->palaceId
                || value->profileRevision > rootRecord->revision
                || !stableIds.insert(bytesHex(value->userId)).second) {
                return reject("invalid-user-profile");
            }
            profiles.push_back(*value);
            break;
        }
        case PalaceLezRecordTypeV3::Room: {
            const auto* value = std::get_if<PalaceLezRoomRecordV3>(&child.record);
            if (value == nullptr || !validRoom(*value)
                || value->palaceId != rootRecord->palaceId
                || value->revision > rootRecord->revision) {
                return reject("invalid-room-record");
            }
            const std::string id = bytesHex(value->roomId);
            if (id != bytesHex(rootRecord->roomIds[0])
                && id != bytesHex(rootRecord->roomIds[1])) {
                return reject("unknown-room-record");
            }
            rooms.push_back(*value);
            break;
        }
        case PalaceLezRecordTypeV3::CapabilityGrant: {
            const auto* value =
                std::get_if<PalaceLezCapabilityGrantRecordV3>(&child.record);
            if (value == nullptr || !validGrant(*value)
                || value->palaceId != rootRecord->palaceId
                || value->revision > rootRecord->revision
                || !scopeNamesRoom(value->scope, rootRecord->roomIds)
                || !stableIds.insert(bytesHex(value->grantId)).second) {
                return reject("invalid-capability-grant");
            }
            grants.push_back(*value);
            break;
        }
        case PalaceLezRecordTypeV3::Ban: {
            const auto* value = std::get_if<PalaceLezBanRecordV3>(&child.record);
            if (value == nullptr || !validBan(*value)
                || value->palaceId != rootRecord->palaceId
                || value->revision > rootRecord->revision
                || !scopeNamesRoom(value->scope, rootRecord->roomIds)
                || !stableIds.insert(bytesHex(value->banId)).second) {
                return reject("invalid-ban-record");
            }
            bans.push_back(*value);
            break;
        }
        case PalaceLezRecordTypeV3::RoomSharedState: {
            const auto* value =
                std::get_if<PalaceLezRoomSharedStateRecordV3>(&child.record);
            if (value == nullptr || !validSharedState(*value)
                || value->palaceId != rootRecord->palaceId
                || value->lastOrderedActionId > rootRecord->lastOrderedActionId
                || (value->roomId != rootRecord->roomIds[0]
                    && value->roomId != rootRecord->roomIds[1])
                || !stableIds.insert(bytesHex(value->sharedStateId)).second) {
                return reject("invalid-shared-state-record");
            }
            sharedStates.push_back(*value);
            break;
        }
        case PalaceLezRecordTypeV3::PalaceRoot:
        default:
            return reject("confused-record-type");
        }
    }

    if (profiles.size() != rootRecord->userCount || rooms.size() != 2U
        || grants.size() != rootRecord->grantCount
        || bans.size() != rootRecord->banCount
        || sharedStates.size() != rootRecord->sharedStateCount) {
        return reject("record-count-mismatch");
    }

    const auto roomCount = [&](const PalaceLezBytes32& roomId) {
        return static_cast<std::size_t>(std::count_if(
            rooms.begin(), rooms.end(),
            [&](const PalaceLezRoomRecordV3& room) {
                return room.roomId == roomId;
            }));
    };
    const auto sharedCount = [&](const PalaceLezBytes32& roomId) {
        return static_cast<std::size_t>(std::count_if(
            sharedStates.begin(), sharedStates.end(),
            [&](const PalaceLezRoomSharedStateRecordV3& shared) {
                return shared.roomId == roomId;
            }));
    };
    if (roomCount(rootRecord->roomIds[0]) != 1U
        || roomCount(rootRecord->roomIds[1]) != 1U)
        return reject("room-record-mismatch");
    if (sharedCount(rootRecord->roomIds[0]) > 1U
        || sharedCount(rootRecord->roomIds[1]) > 1U) {
        return reject("ambiguous-room-shared-state");
    }

    const auto knownUser = [&](const PalaceLezBytes32& userId) {
        return std::any_of(profiles.begin(), profiles.end(),
            [&](const PalaceLezUserProfileRecordV3& profile) {
                return profile.userId == userId;
            });
    };
    if (static_cast<std::size_t>(std::count_if(
            profiles.begin(), profiles.end(),
            [&](const PalaceLezUserProfileRecordV3& profile) {
                return profile.userId == rootRecord->owner;
            })) != 1U) {
        return reject("missing-owner-profile");
    }
    for (const PalaceLezCapabilityGrantRecordV3& grant : grants) {
        if (!knownUser(grant.subjectUserId) || !knownUser(grant.issuedBy)
            || grant.issuedBy != rootRecord->owner) {
            return reject("grant-user-mismatch");
        }
    }
    for (const PalaceLezBanRecordV3& ban : bans) {
        if (!knownUser(ban.issuer)
            || (ban.targetKind == PalaceLezBanTargetKindV3::User
                && (!knownUser(ban.targetUserId)
                    || ban.targetUserId == rootRecord->owner))
            || !banAuthorized(ban, *rootRecord, grants)) {
            return reject("unauthorized-or-unknown-ban");
        }
    }

    AuthoritySnapshotV1 snapshot;
    snapshot.palaceId = bytesHex(rootRecord->palaceId);
    snapshot.ownerUserId = bytesHex(rootRecord->owner);
    snapshot.entryRoomId = bytesHex(rootRecord->entryRoomId);
    for (const PalaceLezUserProfileRecordV3& profile : profiles) {
        snapshot.users.push_back({
            bytesHex(profile.userId),
            bytesHex(profile.deliveryKey),
            static_cast<std::int64_t>(profile.keyEpoch),
        });
    }
    for (const PalaceLezBytes32& roomId : rootRecord->roomIds) {
        const auto room = std::find_if(
            rooms.begin(), rooms.end(),
            [&](const PalaceLezRoomRecordV3& value) {
                return value.roomId == roomId;
            });
        const auto shared = std::find_if(
            sharedStates.begin(), sharedStates.end(),
            [&](const PalaceLezRoomSharedStateRecordV3& value) {
                return value.roomId == roomId;
            });
        snapshot.rooms.push_back({
            bytesHex(roomId),
            room->locked,
            shared == sharedStates.end()
                ? std::string{}
                : bytesHex(shared->stateRoot),
            static_cast<std::int64_t>(room->revision),
        });
    }
    for (const PalaceLezCapabilityGrantRecordV3& grant : grants) {
        appendMappedGrant(
            snapshot, grant, *rootRecord, kCapModerateUser,
            CapabilityKind::ModerateUser, ":moderate-user");
        appendMappedGrant(
            snapshot, grant, *rootRecord, kCapModerateAsset,
            CapabilityKind::ModerateAsset, ":moderate-asset");
        appendMappedGrant(
            snapshot, grant, *rootRecord, kCapSetRoomLock,
            CapabilityKind::SetRoomLock, ":set-room-lock");
        appendMappedGrant(
            snapshot, grant, *rootRecord, kCapRoomEdit,
            CapabilityKind::RoomEdit, ":room-edit");
    }
    for (const PalaceLezBanRecordV3& ban : bans) {
        BanV1 projected;
        projected.banId = bytesHex(ban.banId);
        projected.palaceId = snapshot.palaceId;
        projected.roomId = roomScopeId(ban.scope);
        if (ban.targetKind == PalaceLezBanTargetKindV3::User)
            projected.subjectUserId = bytesHex(ban.targetUserId);
        else
            projected.assetCid = ban.targetAssetCid;
        projected.issuedBy = bytesHex(ban.issuer);
        projected.active = ban.active;
        if (ban.issuer != rootRecord->owner) {
            const PalaceLezCapabilityGrantRecordV3* grant =
                banAuthorizingGrant(ban, *rootRecord, grants);
            projected.authorizationGrantId =
                bytesHex(grant->grantId)
                + (ban.targetKind == PalaceLezBanTargetKindV3::User
                        ? ":moderate-user"
                        : ":moderate-asset");
        }
        snapshot.bans.push_back(std::move(projected));
    }

    PalaceLezAuthorityProjectionResultV1 result;
    result.accepted = true;
    result.reason = "accepted";
    result.snapshot = std::move(snapshot);
    return result;
}

PalaceLezAuthorityProjectionResultV1 replaceLezAuthorityV1(
    AuthorityProjection& destination,
    const PalaceLezNamedAuthorityAccountV1& root,
    const std::vector<PalaceLezNamedAuthorityAccountV1>& children,
    const AuthoritySnapshotSource source,
    const std::int64_t committedAt)
{
    PalaceLezAuthorityProjectionResultV1 result =
        projectLezAuthorityV1(root, children);
    if (!result.accepted)
        return result;
    const bool replaced = source == AuthoritySnapshotSource::Finalized
        ? destination.replaceFinalized(result.snapshot, committedAt)
        : source == AuthoritySnapshotSource::LocalCommitted
        ? destination.replaceLocalCommitted(result.snapshot, committedAt)
        : false;
    if (!replaced) {
        result.accepted = false;
        result.reason = "authority-snapshot-rejected";
    }
    return result;
}

PalaceLezAuthorityProjectionResultV1 projectFinalizedLezAuthorityV1(
    const PalaceLezNamedFinalizedAccountV1& finalizedRoot,
    const std::vector<PalaceLezNamedFinalizedAccountV1>& finalizedChildren)
{
    return projectLezAuthorityV1(finalizedRoot, finalizedChildren);
}

PalaceLezAuthorityProjectionResultV1 replaceFinalizedLezAuthorityV1(
    AuthorityProjection& destination,
    const PalaceLezNamedFinalizedAccountV1& finalizedRoot,
    const std::vector<PalaceLezNamedFinalizedAccountV1>& finalizedChildren,
    const std::int64_t finalizedAt)
{
    return replaceLezAuthorityV1(
        destination,
        finalizedRoot,
        finalizedChildren,
        AuthoritySnapshotSource::Finalized,
        finalizedAt);
}

} // namespace palace
