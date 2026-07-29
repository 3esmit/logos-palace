#include <logos_test.h>

#include "palace_lez_authority_projection.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kCapModerateUser = 1U << 0U;
constexpr std::uint32_t kCapModerateAsset = 1U << 1U;
constexpr std::uint32_t kCapSetRoomLock = 1U << 2U;
constexpr std::uint32_t kCapWriteSharedState = 1U << 3U;
constexpr std::uint32_t kCapRoomEdit = 1U << 4U;
constexpr std::uint32_t kAllCapabilities =
    kCapModerateUser | kCapModerateAsset | kCapSetRoomLock
    | kCapWriteSharedState | kCapRoomEdit;
const std::string kProgramOwner(64U, 'a');

palace::PalaceLezBytes32 bytes(const std::uint8_t value)
{
    palace::PalaceLezBytes32 result{};
    result.fill(value);
    return result;
}

std::string hex(const palace::PalaceLezBytes32& value)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64U);
    for (const std::uint8_t byte : value) {
        result.push_back(kHex[byte >> 4U]);
        result.push_back(kHex[byte & 0x0fU]);
    }
    return result;
}

palace::PalaceLezScopeV3 palaceScope()
{
    return {};
}

palace::PalaceLezScopeV3 roomScope(const palace::PalaceLezBytes32& roomId)
{
    palace::PalaceLezScopeV3 result;
    result.kind = palace::PalaceLezScopeKindV3::Room;
    result.roomId = roomId;
    return result;
}

palace::PalaceLezPublicAccountV3 account(
    const palace::PalaceLezRecordTypeV3 type,
    palace::PalaceLezRecordV3 record)
{
    palace::PalaceLezPublicAccountV3 result;
    result.accepted = true;
    result.reason = "accepted";
    result.programOwnerHex = kProgramOwner;
    result.balanceLeHex = std::string(32U, '0');
    result.nonceLeHex = std::string(32U, '0');
    result.recordType = type;
    result.record = std::move(record);
    result.dataSha256Hex =
        palace::canonicalPalaceLezRecordDigestV3(result.recordType, result.record);
    return result;
}

palace::PalaceLezNamedFinalizedAccountV1 namedRoot(
    palace::PalaceLezPublicAccountV3 value)
{
    palace::PalaceLezNamedFinalizedAccountV1 result;
    result.accountIdHex =
        palace::PalaceLezCodec::deriveRootPda(value.programOwnerHex);
    result.account = std::move(value);
    return result;
}

palace::PalaceLezBytes32 stableId(
    const palace::PalaceLezPublicAccountV3& value)
{
    using namespace palace;
    switch (value.recordType) {
    case PalaceLezRecordTypeV3::UserProfile:
        return std::get<PalaceLezUserProfileRecordV3>(value.record).userId;
    case PalaceLezRecordTypeV3::Room:
        return std::get<PalaceLezRoomRecordV3>(value.record).roomId;
    case PalaceLezRecordTypeV3::CapabilityGrant:
        return std::get<PalaceLezCapabilityGrantRecordV3>(value.record).grantId;
    case PalaceLezRecordTypeV3::Ban:
        return std::get<PalaceLezBanRecordV3>(value.record).banId;
    case PalaceLezRecordTypeV3::RoomSharedState:
        return std::get<PalaceLezRoomSharedStateRecordV3>(
            value.record).sharedStateId;
    case PalaceLezRecordTypeV3::PalaceRoot:
    default:
        return {};
    }
}

std::string recordTag(const palace::PalaceLezRecordTypeV3 type)
{
    using palace::PalaceLezRecordTypeV3;
    switch (type) {
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

palace::PalaceLezNamedFinalizedAccountV1 namedChild(
    palace::PalaceLezPublicAccountV3 value,
    const std::string& rootAccountIdHex)
{
    palace::PalaceLezNamedFinalizedAccountV1 result;
    result.accountIdHex = palace::PalaceLezCodec::deriveRecordPda(
        value.programOwnerHex, recordTag(value.recordType),
        rootAccountIdHex, stableId(value));
    result.account = std::move(value);
    return result;
}

void refresh(palace::PalaceLezNamedFinalizedAccountV1& value)
{
    value.account.dataSha256Hex =
        palace::canonicalPalaceLezRecordDigestV3(
            value.account.recordType, value.account.record);
}

struct Fixture {
    palace::PalaceLezNamedFinalizedAccountV1 root;
    std::vector<palace::PalaceLezNamedFinalizedAccountV1> children;
};

Fixture finalizedFixture()
{
    using namespace palace;
    const PalaceLezBytes32 palaceId = bytes(0x10U);
    const PalaceLezBytes32 owner = bytes(0x20U);
    const PalaceLezBytes32 moderator = bytes(0x21U);
    const PalaceLezBytes32 member = bytes(0x22U);
    const PalaceLezBytes32 entryRoom = bytes(0x30U);
    const PalaceLezBytes32 secondRoom = bytes(0x40U);

    PalaceLezRootRecordV3 root;
    root.palaceId = palaceId;
    root.title = "Logos Palace";
    root.owner = owner;
    root.entryRoomId = entryRoom;
    root.roomIds = {entryRoom, secondRoom};
    root.activeManifestCid = "bafyroot";
    root.userCount = 3U;
    root.grantCount = 2U;
    root.banCount = 2U;
    root.sharedStateCount = 2U;
    root.revision = 10U;
    root.lastOrderedActionId = 10U;

    PalaceLezUserProfileRecordV3 ownerProfile;
    ownerProfile.palaceId = palaceId;
    ownerProfile.userId = owner;
    ownerProfile.displayName = "Owner";
    ownerProfile.deliveryKey = bytes(0x90U);
    ownerProfile.keyEpoch = 2U;
    ownerProfile.profileRevision = 1U;

    PalaceLezUserProfileRecordV3 moderatorProfile;
    moderatorProfile.palaceId = palaceId;
    moderatorProfile.userId = moderator;
    moderatorProfile.displayName = "Moderator";
    moderatorProfile.deliveryKey = bytes(0x91U);
    moderatorProfile.keyEpoch = 3U;
    moderatorProfile.profileRevision = 1U;

    PalaceLezUserProfileRecordV3 memberProfile;
    memberProfile.palaceId = palaceId;
    memberProfile.userId = member;
    memberProfile.displayName = "Member";
    memberProfile.deliveryKey = bytes(0x92U);
    memberProfile.keyEpoch = 4U;

    PalaceLezRoomRecordV3 entry;
    entry.palaceId = palaceId;
    entry.roomId = entryRoom;
    entry.title = "Atrium";
    entry.manifestCid = "bafyatrium";
    entry.scriptBundleCid = "bafyatriumscript";
    entry.locked = false;
    entry.revision = 7U;

    PalaceLezRoomRecordV3 second;
    second.palaceId = palaceId;
    second.roomId = secondRoom;
    second.title = "Lounge";
    second.manifestCid = "bafylounge";
    second.scriptBundleCid = "bafyloungescript";
    second.locked = true;
    second.revision = 3U;

    PalaceLezCapabilityGrantRecordV3 ownerGrant;
    ownerGrant.palaceId = palaceId;
    ownerGrant.grantId = bytes(0x50U);
    ownerGrant.subjectUserId = owner;
    ownerGrant.issuedBy = owner;
    ownerGrant.scope = palaceScope();
    ownerGrant.capabilities = kAllCapabilities;
    ownerGrant.delegable = true;
    ownerGrant.validThroughActionId = std::numeric_limits<std::uint64_t>::max();

    PalaceLezCapabilityGrantRecordV3 moderatorGrant;
    moderatorGrant.palaceId = palaceId;
    moderatorGrant.grantId = bytes(0x51U);
    moderatorGrant.subjectUserId = moderator;
    moderatorGrant.issuedBy = owner;
    moderatorGrant.scope = palaceScope();
    moderatorGrant.capabilities = kCapModerateUser | kCapModerateAsset
        | kCapSetRoomLock | kCapRoomEdit;
    moderatorGrant.validThroughActionId = 100U;

    PalaceLezBanRecordV3 userBan;
    userBan.palaceId = palaceId;
    userBan.banId = bytes(0x60U);
    userBan.targetKind = PalaceLezBanTargetKindV3::User;
    userBan.targetUserId = member;
    userBan.issuer = owner;
    userBan.scope = palaceScope();
    userBan.active = true;
    userBan.revision = 1U;

    PalaceLezBanRecordV3 assetBan;
    assetBan.palaceId = palaceId;
    assetBan.banId = bytes(0x61U);
    assetBan.targetKind = PalaceLezBanTargetKindV3::AssetCid;
    assetBan.targetAssetCid = "bafyblocked";
    assetBan.issuer = moderator;
    assetBan.scope = roomScope(entryRoom);
    assetBan.active = true;
    assetBan.revision = 1U;

    PalaceLezRoomSharedStateRecordV3 entryState;
    entryState.palaceId = palaceId;
    entryState.sharedStateId = bytes(0x70U);
    entryState.roomId = entryRoom;
    entryState.key = "door";
    entryState.value = {0x01U, 0x02U};
    entryState.stateRoot = bytes(0x80U);
    entryState.revision = 2U;
    entryState.lastOrderedActionId = 9U;

    PalaceLezRoomSharedStateRecordV3 secondState;
    secondState.palaceId = palaceId;
    secondState.sharedStateId = bytes(0x71U);
    secondState.roomId = secondRoom;
    secondState.key = "door";
    secondState.value = {0x03U, 0x04U};
    secondState.stateRoot = bytes(0x81U);
    secondState.revision = 1U;
    secondState.lastOrderedActionId = 8U;

    Fixture fixture;
    fixture.root = namedRoot(
        account(PalaceLezRecordTypeV3::PalaceRoot, root));
    fixture.children = {
        namedChild(
            account(PalaceLezRecordTypeV3::Room, second),
            fixture.root.accountIdHex),
        namedChild(
            account(PalaceLezRecordTypeV3::UserProfile, memberProfile),
            fixture.root.accountIdHex),
        namedChild(
            account(PalaceLezRecordTypeV3::CapabilityGrant, moderatorGrant),
            fixture.root.accountIdHex),
        namedChild(
            account(PalaceLezRecordTypeV3::Ban, assetBan),
            fixture.root.accountIdHex),
        namedChild(
            account(PalaceLezRecordTypeV3::RoomSharedState, secondState),
            fixture.root.accountIdHex),
        namedChild(
            account(PalaceLezRecordTypeV3::UserProfile, ownerProfile),
            fixture.root.accountIdHex),
        namedChild(
            account(PalaceLezRecordTypeV3::Room, entry),
            fixture.root.accountIdHex),
        namedChild(
            account(PalaceLezRecordTypeV3::CapabilityGrant, ownerGrant),
            fixture.root.accountIdHex),
        namedChild(
            account(PalaceLezRecordTypeV3::Ban, userBan),
            fixture.root.accountIdHex),
        namedChild(
            account(PalaceLezRecordTypeV3::RoomSharedState, entryState),
            fixture.root.accountIdHex),
        namedChild(
            account(PalaceLezRecordTypeV3::UserProfile, moderatorProfile),
            fixture.root.accountIdHex),
    };
    return fixture;
}

template<typename Record>
palace::PalaceLezNamedFinalizedAccountV1* findRecord(
    std::vector<palace::PalaceLezNamedFinalizedAccountV1>& children)
{
    const auto found = std::find_if(
        children.begin(), children.end(),
        [](palace::PalaceLezNamedFinalizedAccountV1& value) {
            return std::holds_alternative<Record>(value.account.record);
        });
    return found == children.end() ? nullptr : &*found;
}

} // namespace

LOGOS_TEST(lez_finalized_authority_projects_owner_moderator_rooms_and_bans) {
    using namespace palace;
    const Fixture fixture = finalizedFixture();
    const PalaceLezAuthorityProjectionResultV1 projected =
        projectFinalizedLezAuthorityV1(fixture.root, fixture.children);
    LOGOS_ASSERT_TRUE(projected.accepted);
    LOGOS_ASSERT_EQ(projected.reason, std::string("accepted"));
    LOGOS_ASSERT_EQ(projected.snapshot.palaceId, hex(bytes(0x10U)));
    LOGOS_ASSERT_EQ(projected.snapshot.ownerUserId, hex(bytes(0x20U)));
    LOGOS_ASSERT_EQ(projected.snapshot.entryRoomId, hex(bytes(0x30U)));
    LOGOS_ASSERT_EQ(projected.snapshot.rooms.size(), static_cast<std::size_t>(2U));
    LOGOS_ASSERT_EQ(projected.snapshot.rooms[0].roomId, hex(bytes(0x30U)));
    LOGOS_ASSERT_EQ(projected.snapshot.rooms[0].roomEpoch, 7);
    LOGOS_ASSERT_EQ(projected.snapshot.rooms[0].sharedStateRoot, hex(bytes(0x80U)));
    LOGOS_ASSERT_EQ(projected.snapshot.rooms[1].roomId, hex(bytes(0x40U)));
    LOGOS_ASSERT_TRUE(projected.snapshot.rooms[1].locked);
    LOGOS_ASSERT_EQ(projected.snapshot.rooms[1].sharedStateRoot, hex(bytes(0x81U)));

    AuthorityProjection authority;
    const PalaceLezAuthorityProjectionResultV1 replaced =
        replaceFinalizedLezAuthorityV1(
            authority, fixture.root, fixture.children, 500);
    LOGOS_ASSERT_TRUE(replaced.accepted);
    LOGOS_ASSERT_TRUE(authority.can(
        hex(bytes(0x20U)), CapabilityKind::RoomEdit,
        hex(bytes(0x40U)), 500));
    LOGOS_ASSERT_TRUE(authority.can(
        hex(bytes(0x21U)), CapabilityKind::ModerateUser,
        hex(bytes(0x40U)), 500));
    LOGOS_ASSERT_TRUE(authority.can(
        hex(bytes(0x21U)), CapabilityKind::ModerateAsset,
        hex(bytes(0x30U)), 500));
    LOGOS_ASSERT_EQ(
        authority.deliveryKeyFor(hex(bytes(0x21U)), 3),
        hex(bytes(0x91U)));
    LOGOS_ASSERT_TRUE(
        authority.isUserBanned(hex(bytes(0x22U)), hex(bytes(0x40U))));
    LOGOS_ASSERT_TRUE(authority.isAssetBanned(
        "bafyblocked", hex(bytes(0x30U))));
    LOGOS_ASSERT_FALSE(authority.isAssetBanned(
        "bafyblocked", hex(bytes(0x40U))));
}

LOGOS_TEST(lez_finalized_authority_requires_exact_root_and_child_pdas) {
    using namespace palace;

    Fixture wrongRoot = finalizedFixture();
    wrongRoot.root.accountIdHex = std::string(64U, 'f');
    LOGOS_ASSERT_EQ(
        projectFinalizedLezAuthorityV1(
            wrongRoot.root, wrongRoot.children).reason,
        std::string("invalid-finalized-root"));

    Fixture wrongChild = finalizedFixture();
    wrongChild.children[0].accountIdHex = std::string(64U, 'f');
    LOGOS_ASSERT_EQ(
        projectFinalizedLezAuthorityV1(
            wrongChild.root, wrongChild.children).reason,
        std::string("invalid-or-duplicate-child"));

    Fixture swapped = finalizedFixture();
    std::swap(
        swapped.children[0].accountIdHex,
        swapped.children[1].accountIdHex);
    LOGOS_ASSERT_EQ(
        projectFinalizedLezAuthorityV1(
            swapped.root, swapped.children).reason,
        std::string("invalid-or-duplicate-child"));

    Fixture duplicate = finalizedFixture();
    duplicate.children[1] = duplicate.children[0];
    LOGOS_ASSERT_EQ(
        projectFinalizedLezAuthorityV1(
            duplicate.root, duplicate.children).reason,
        std::string("invalid-or-duplicate-child"));
}

LOGOS_TEST(lez_finalized_authority_rejects_counts_duplicates_confusion_and_unknown_bits) {
    using namespace palace;

    Fixture countMismatch = finalizedFixture();
    auto* root =
        std::get_if<PalaceLezRootRecordV3>(
            &countMismatch.root.account.record);
    LOGOS_ASSERT_TRUE(root != nullptr);
    ++root->userCount;
    refresh(countMismatch.root);
    LOGOS_ASSERT_FALSE(projectFinalizedLezAuthorityV1(
        countMismatch.root, countMismatch.children).accepted);

    Fixture duplicateRoom = finalizedFixture();
    std::vector<PalaceLezNamedFinalizedAccountV1*> roomAccounts;
    for (PalaceLezNamedFinalizedAccountV1& child : duplicateRoom.children) {
        if (std::holds_alternative<PalaceLezRoomRecordV3>(
                child.account.record)) {
            roomAccounts.push_back(&child);
        }
    }
    LOGOS_ASSERT_EQ(roomAccounts.size(), static_cast<std::size_t>(2U));
    auto* firstRoom = std::get_if<PalaceLezRoomRecordV3>(
        &roomAccounts[0]->account.record);
    auto* secondRoom = std::get_if<PalaceLezRoomRecordV3>(
        &roomAccounts[1]->account.record);
    LOGOS_ASSERT_TRUE(firstRoom != nullptr);
    LOGOS_ASSERT_TRUE(secondRoom != nullptr);
    secondRoom->roomId = firstRoom->roomId;
    refresh(*roomAccounts[1]);
    LOGOS_ASSERT_FALSE(projectFinalizedLezAuthorityV1(
        duplicateRoom.root, duplicateRoom.children).accepted);

    Fixture confused = finalizedFixture();
    confused.children[0].account.recordType =
        PalaceLezRecordTypeV3::UserProfile;
    refresh(confused.children[0]);
    LOGOS_ASSERT_FALSE(projectFinalizedLezAuthorityV1(
        confused.root, confused.children).accepted);

    Fixture unknownBit = finalizedFixture();
    PalaceLezNamedFinalizedAccountV1* grantAccount =
        findRecord<PalaceLezCapabilityGrantRecordV3>(unknownBit.children);
    LOGOS_ASSERT_TRUE(grantAccount != nullptr);
    auto* grant =
        std::get_if<PalaceLezCapabilityGrantRecordV3>(
            &grantAccount->account.record);
    LOGOS_ASSERT_TRUE(grant != nullptr);
    grant->capabilities |= 1U << 5U;
    refresh(*grantAccount);
    LOGOS_ASSERT_FALSE(projectFinalizedLezAuthorityV1(
        unknownBit.root, unknownBit.children).accepted);

    Fixture ambiguousShared = finalizedFixture();
    const auto shared = std::find_if(
        ambiguousShared.children.begin(), ambiguousShared.children.end(),
        [](const PalaceLezNamedFinalizedAccountV1& value) {
            const auto* record =
                std::get_if<PalaceLezRoomSharedStateRecordV3>(
                    &value.account.record);
            return record != nullptr && record->roomId == bytes(0x40U);
        });
    LOGOS_ASSERT_TRUE(shared != ambiguousShared.children.end());
    auto* sharedRecord =
        std::get_if<PalaceLezRoomSharedStateRecordV3>(
            &shared->account.record);
    LOGOS_ASSERT_TRUE(sharedRecord != nullptr);
    sharedRecord->roomId = bytes(0x30U);
    refresh(*shared);
    LOGOS_ASSERT_EQ(
        projectFinalizedLezAuthorityV1(
            ambiguousShared.root, ambiguousShared.children).reason,
        std::string("ambiguous-room-shared-state"));
}

LOGOS_TEST(lez_finalized_authority_allows_rooms_before_shared_state_exists) {
    using namespace palace;

    Fixture oneShared = finalizedFixture();
    const auto secondShared = std::find_if(
        oneShared.children.begin(), oneShared.children.end(),
        [](const PalaceLezNamedFinalizedAccountV1& value) {
            const auto* record =
                std::get_if<PalaceLezRoomSharedStateRecordV3>(
                    &value.account.record);
            return record != nullptr && record->roomId == bytes(0x40U);
        });
    LOGOS_ASSERT_TRUE(secondShared != oneShared.children.end());
    oneShared.children.erase(secondShared);
    auto* oneSharedRoot =
        std::get_if<PalaceLezRootRecordV3>(
            &oneShared.root.account.record);
    LOGOS_ASSERT_TRUE(oneSharedRoot != nullptr);
    oneSharedRoot->sharedStateCount = 1U;
    refresh(oneShared.root);

    const PalaceLezAuthorityProjectionResultV1 projectedOne =
        projectFinalizedLezAuthorityV1(
            oneShared.root, oneShared.children);
    LOGOS_ASSERT_TRUE(projectedOne.accepted);
    LOGOS_ASSERT_EQ(
        projectedOne.snapshot.rooms[0].sharedStateRoot,
        hex(bytes(0x80U)));
    LOGOS_ASSERT_TRUE(
        projectedOne.snapshot.rooms[1].sharedStateRoot.empty());
    AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(
        projectedOne.snapshot, 600));

    Fixture noShared = oneShared;
    const auto firstShared = std::find_if(
        noShared.children.begin(), noShared.children.end(),
        [](const PalaceLezNamedFinalizedAccountV1& value) {
            return std::holds_alternative<
                PalaceLezRoomSharedStateRecordV3>(value.account.record);
        });
    LOGOS_ASSERT_TRUE(firstShared != noShared.children.end());
    noShared.children.erase(firstShared);
    auto* noSharedRoot =
        std::get_if<PalaceLezRootRecordV3>(
            &noShared.root.account.record);
    LOGOS_ASSERT_TRUE(noSharedRoot != nullptr);
    noSharedRoot->sharedStateCount = 0U;
    refresh(noShared.root);

    const PalaceLezAuthorityProjectionResultV1 projectedNone =
        projectFinalizedLezAuthorityV1(
            noShared.root, noShared.children);
    LOGOS_ASSERT_TRUE(projectedNone.accepted);
    LOGOS_ASSERT_TRUE(
        projectedNone.snapshot.rooms[0].sharedStateRoot.empty());
    LOGOS_ASSERT_TRUE(
        projectedNone.snapshot.rooms[1].sharedStateRoot.empty());
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(
        projectedNone.snapshot, 601));
}

LOGOS_TEST(lez_finalized_authority_rejects_unknown_users_and_unauthorized_bans) {
    using namespace palace;

    Fixture unknownSubject = finalizedFixture();
    PalaceLezNamedFinalizedAccountV1* grantAccount =
        findRecord<PalaceLezCapabilityGrantRecordV3>(unknownSubject.children);
    LOGOS_ASSERT_TRUE(grantAccount != nullptr);
    auto* grant =
        std::get_if<PalaceLezCapabilityGrantRecordV3>(
            &grantAccount->account.record);
    LOGOS_ASSERT_TRUE(grant != nullptr);
    grant->subjectUserId = bytes(0xa1U);
    refresh(*grantAccount);
    LOGOS_ASSERT_EQ(
        projectFinalizedLezAuthorityV1(
            unknownSubject.root, unknownSubject.children).reason,
        std::string("grant-user-mismatch"));

    Fixture unauthorized = finalizedFixture();
    PalaceLezNamedFinalizedAccountV1* banAccount = nullptr;
    for (PalaceLezNamedFinalizedAccountV1& child : unauthorized.children) {
        auto* ban = std::get_if<PalaceLezBanRecordV3>(
            &child.account.record);
        if (ban != nullptr
            && ban->targetKind == PalaceLezBanTargetKindV3::AssetCid) {
            banAccount = &child;
            ban->issuer = bytes(0x22U);
            break;
        }
    }
    LOGOS_ASSERT_TRUE(banAccount != nullptr);
    refresh(*banAccount);
    LOGOS_ASSERT_EQ(
        projectFinalizedLezAuthorityV1(
            unauthorized.root, unauthorized.children).reason,
        std::string("unauthorized-or-unknown-ban"));
}

LOGOS_TEST(lez_finalized_authority_keeps_valid_bans_after_issuer_grant_revocation) {
    using namespace palace;
    Fixture revoked = finalizedFixture();
    PalaceLezNamedFinalizedAccountV1* moderatorGrantAccount = nullptr;
    for (PalaceLezNamedFinalizedAccountV1& child : revoked.children) {
        auto* grant =
            std::get_if<PalaceLezCapabilityGrantRecordV3>(
                &child.account.record);
        if (grant != nullptr && grant->subjectUserId == bytes(0x21U)) {
            moderatorGrantAccount = &child;
            grant->revoked = true;
            grant->revision = 10U;
            break;
        }
    }
    LOGOS_ASSERT_TRUE(moderatorGrantAccount != nullptr);
    refresh(*moderatorGrantAccount);

    const PalaceLezAuthorityProjectionResultV1 projected =
        projectFinalizedLezAuthorityV1(revoked.root, revoked.children);
    LOGOS_ASSERT_TRUE(projected.accepted);
    const auto projectedBan = std::find_if(
        projected.snapshot.bans.begin(),
        projected.snapshot.bans.end(),
        [](const BanV1& ban) { return ban.assetCid == "bafyblocked"; });
    LOGOS_ASSERT_TRUE(projectedBan != projected.snapshot.bans.end());
    LOGOS_ASSERT_EQ(
        projectedBan->authorizationGrantId,
        hex(bytes(0x51U)) + ":moderate-asset");
    AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(projected.snapshot, 900));
    LOGOS_ASSERT_FALSE(authority.can(
        hex(bytes(0x21U)), CapabilityKind::ModerateAsset,
        hex(bytes(0x30U)), 900));
    LOGOS_ASSERT_TRUE(authority.isAssetBanned(
        "bafyblocked", hex(bytes(0x30U))));
}

LOGOS_TEST(lez_finalized_authority_replacement_is_atomic_on_tampered_child) {
    using namespace palace;
    const Fixture fixture = finalizedFixture();
    AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(replaceFinalizedLezAuthorityV1(
        authority, fixture.root, fixture.children, 700).accepted);
    const std::string palaceId = authority.palaceId();
    const std::string moderator = hex(bytes(0x21U));
    const std::string originalKey =
        authority.deliveryKeyFor(moderator, 3);

    Fixture tampered = finalizedFixture();
    PalaceLezNamedFinalizedAccountV1* profileAccount = nullptr;
    for (PalaceLezNamedFinalizedAccountV1& child : tampered.children) {
        auto* profile =
            std::get_if<PalaceLezUserProfileRecordV3>(
                &child.account.record);
        if (profile != nullptr && profile->userId == bytes(0x21U)) {
            profileAccount = &child;
            profile->deliveryKey = bytes(0xeeU);
            break;
        }
    }
    LOGOS_ASSERT_TRUE(profileAccount != nullptr);
    LOGOS_ASSERT_FALSE(replaceFinalizedLezAuthorityV1(
        authority, tampered.root, tampered.children, 701).accepted);
    LOGOS_ASSERT_EQ(authority.palaceId(), palaceId);
    LOGOS_ASSERT_EQ(authority.finalizedAt(), 700);
    LOGOS_ASSERT_EQ(authority.deliveryKeyFor(moderator, 3), originalKey);
    LOGOS_ASSERT_TRUE(authority.can(
        moderator, CapabilityKind::ModerateAsset,
        hex(bytes(0x30U)), 701));
}
