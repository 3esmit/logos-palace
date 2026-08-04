#include <logos_test.h>

#include <variant>

#include "palace_initial_authoring.h"
#include "palace_sha256.h"

namespace {

palace::PalaceInitialAuthoringInputV1 validInput()
{
    return {
        "Open Palace",
        "1111111111111111111111111111111111111111111111111111111111111111",
        "Alice",
        "2222222222222222222222222222222222222222222222222222222222222222",
        1U,
        "bafypalacemanifest",
        "bafyatriummanifest",
        "bafyloungemanifest",
        "bafydoorscript",
    };
}

const palace::PalaceLezInitializeV3& initialize(
    const palace::PalaceInitialAuthoringResultV1& result)
{
    const auto* value = std::get_if<palace::PalaceLezInitializeV3>(
        &result.instruction.payload);
    LOGOS_ASSERT_TRUE(value != nullptr);
    return *value;
}

const palace::PalaceLezCreateSharedStateV3& initialRoomState(
    const palace::PalaceInitialRoomStateResultV1& result)
{
    const auto* value = std::get_if<palace::PalaceLezCreateSharedStateV3>(
        &result.instruction.payload);
    LOGOS_ASSERT_TRUE(value != nullptr);
    return *value;
}

} // namespace

LOGOS_TEST(initial_authoring_builds_a_typed_two_room_initialize)
{
    const palace::PalaceInitialAuthoringResultV1 first =
        palace::buildPalaceInitialAuthoringV1(validInput());
    const palace::PalaceInitialAuthoringResultV1 second =
        palace::buildPalaceInitialAuthoringV1(validInput());

    LOGOS_ASSERT_TRUE(first.accepted);
    LOGOS_ASSERT_TRUE(second.accepted);
    const palace::PalaceLezInitializeV3& initialized = initialize(first);
    const palace::PalaceLezInitializeV3& retried = initialize(second);
    LOGOS_ASSERT_EQ(initialized.title, std::string("Open Palace"));
    LOGOS_ASSERT_EQ(initialized.entryRoom.title, std::string("Atrium"));
    LOGOS_ASSERT_EQ(initialized.secondaryRoom.title, std::string("Lounge"));
    LOGOS_ASSERT_EQ(
        initialized.activeManifestCid,
        std::string("bafypalacemanifest"));
    LOGOS_ASSERT_EQ(
        initialized.entryRoom.manifestCid,
        std::string("bafyatriummanifest"));
    LOGOS_ASSERT_EQ(
        initialized.secondaryRoom.manifestCid,
        std::string("bafyloungemanifest"));
    LOGOS_ASSERT_EQ(
        initialized.entryRoom.scriptBundleCid,
        std::string("bafydoorscript"));
    LOGOS_ASSERT_EQ(
        initialized.secondaryRoom.scriptBundleCid,
        std::string("bafydoorscript"));
    LOGOS_ASSERT_TRUE(initialized.entryRoomId != initialized.secondaryRoomId);
    LOGOS_ASSERT_TRUE(initialized.palaceId == retried.palaceId);
    LOGOS_ASSERT_TRUE(initialized.ownerGrantId == retried.ownerGrantId);
    LOGOS_ASSERT_TRUE(
        palace::PalaceLezCodec::encodeInstruction(first.instruction).accepted);
}

LOGOS_TEST(initial_authoring_binds_ids_to_user_authored_graph)
{
    palace::PalaceInitialAuthoringInputV1 changed = validInput();
    changed.title = "A different Palace";
    const auto first = palace::buildPalaceInitialAuthoringV1(validInput());
    const auto second = palace::buildPalaceInitialAuthoringV1(changed);

    LOGOS_ASSERT_TRUE(first.accepted);
    LOGOS_ASSERT_TRUE(second.accepted);
    LOGOS_ASSERT_TRUE(initialize(first).palaceId != initialize(second).palaceId);
    LOGOS_ASSERT_TRUE(
        initialize(first).entryRoomId != initialize(second).entryRoomId);
}

LOGOS_TEST(initial_authoring_rejects_invalid_identity_or_graph)
{
    palace::PalaceInitialAuthoringInputV1 badIdentity = validInput();
    badIdentity.ownerDeliveryKeyHex = "not-a-key";
    const auto identityResult =
        palace::buildPalaceInitialAuthoringV1(badIdentity);
    LOGOS_ASSERT_FALSE(identityResult.accepted);
    LOGOS_ASSERT_EQ(identityResult.reason, std::string("invalid-owner-identity"));

    palace::PalaceInitialAuthoringInputV1 badGraph = validInput();
    badGraph.atriumManifestCid = "not a cid";
    const auto graphResult = palace::buildPalaceInitialAuthoringV1(badGraph);
    LOGOS_ASSERT_FALSE(graphResult.accepted);
    LOGOS_ASSERT_TRUE(
        graphResult.reason.rfind("invalid-authoring-", 0U) == 0U);
}

LOGOS_TEST(initial_authoring_builds_entry_door_state_after_initialize)
{
    const palace::PalaceInitialAuthoringResultV1 authored =
        palace::buildPalaceInitialAuthoringV1(validInput());
    LOGOS_ASSERT_TRUE(authored.accepted);
    const palace::PalaceLezInitializeV3& initialized = initialize(authored);

    palace::PalaceLezRootRecordV3 root;
    root.palaceId = initialized.palaceId;
    LOGOS_ASSERT_TRUE(palace::PalaceLezCodec::parseBytes32Hex(
        validInput().ownerAccountIdHex, root.owner));
    root.entryRoomId = initialized.entryRoomId;
    root.lastOrderedActionId = 0U;
    palace::PalaceLezRoomRecordV3 entryRoom;
    entryRoom.palaceId = initialized.palaceId;
    entryRoom.roomId = initialized.entryRoomId;

    const palace::PalaceInitialRoomStateResultV1 first =
        palace::buildPalaceInitialRoomStateV1(root, entryRoom, 1U);
    const palace::PalaceInitialRoomStateResultV1 second =
        palace::buildPalaceInitialRoomStateV1(root, entryRoom, 1U);
    LOGOS_ASSERT_TRUE(first.accepted);
    LOGOS_ASSERT_TRUE(second.accepted);
    const auto& state = initialRoomState(first);
    const auto& retried = initialRoomState(second);
    LOGOS_ASSERT_EQ(state.orderedActionId, 1U);
    LOGOS_ASSERT_TRUE(state.roomId == initialized.entryRoomId);
    LOGOS_ASSERT_EQ(state.key, std::string("door_open"));
    LOGOS_ASSERT_TRUE(
        state.value == std::vector<std::uint8_t>({'0'}));
    LOGOS_ASSERT_TRUE(state.grantId == initialized.ownerGrantId);
    LOGOS_ASSERT_EQ(
        palace::PalaceLezCodec::bytes32Hex(state.stateRoot),
        palace::crypto::sha256Hex("door_open=0"));
    LOGOS_ASSERT_TRUE(state.grantId == retried.grantId);
    LOGOS_ASSERT_TRUE(state.sharedStateId == retried.sharedStateId);
    LOGOS_ASSERT_TRUE(
        palace::PalaceLezCodec::encodeInstruction(first.instruction).accepted);
}

LOGOS_TEST(initial_authoring_rejects_mismatched_entry_room_state)
{
    const palace::PalaceInitialAuthoringResultV1 authored =
        palace::buildPalaceInitialAuthoringV1(validInput());
    LOGOS_ASSERT_TRUE(authored.accepted);
    const palace::PalaceLezInitializeV3& initialized = initialize(authored);

    palace::PalaceLezRootRecordV3 root;
    root.palaceId = initialized.palaceId;
    LOGOS_ASSERT_TRUE(palace::PalaceLezCodec::parseBytes32Hex(
        validInput().ownerAccountIdHex, root.owner));
    root.entryRoomId = initialized.entryRoomId;
    root.lastOrderedActionId = 0U;
    palace::PalaceLezRoomRecordV3 wrongRoom;
    wrongRoom.palaceId = initialized.palaceId;
    wrongRoom.roomId = initialized.secondaryRoomId;

    const palace::PalaceInitialRoomStateResultV1 rejected =
        palace::buildPalaceInitialRoomStateV1(root, wrongRoom, 1U);
    LOGOS_ASSERT_FALSE(rejected.accepted);
    LOGOS_ASSERT_EQ(
        rejected.reason, std::string("invalid-initial-room-authority"));
}
