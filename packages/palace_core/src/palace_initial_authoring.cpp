#include "palace_initial_authoring.h"

#include <array>
#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "palace_sha256.h"

namespace palace {
namespace {

std::string lengthEncoded(const std::string& value)
{
    return std::to_string(value.size()) + ':' + value;
}

std::optional<PalaceLezBytes32> deriveStableId(
    const std::string& kind,
    const std::vector<std::string>& fields)
{
    std::string material =
        "logos-palace.initial-authoring.v1;kind=" + lengthEncoded(kind);
    for (const std::string& field : fields)
        material += ";field=" + lengthEncoded(field);

    PalaceLezBytes32 id{};
    const std::string digest = crypto::sha256Hex(material);
    if (!PalaceLezCodec::parseBytes32Hex(digest, id))
        return std::nullopt;
    return id;
}

PalaceInitialAuthoringResultV1 rejected(const std::string& reason)
{
    return {false, reason, {}};
}

PalaceInitialRoomStateResultV1 rejectedInitialRoomState(
    const std::string& reason)
{
    return {false, reason, {}};
}

bool nonzero(const PalaceLezBytes32& value)
{
    return std::any_of(
        value.begin(), value.end(),
        [](const std::uint8_t byte) { return byte != 0U; });
}

} // namespace

PalaceInitialAuthoringResultV1 buildPalaceInitialAuthoringV1(
    const PalaceInitialAuthoringInputV1& input)
{
    PalaceLezBytes32 owner{};
    PalaceLezBytes32 deliveryKey{};
    if (!PalaceLezCodec::parseBytes32Hex(input.ownerAccountIdHex, owner)
        || !PalaceLezCodec::parseBytes32Hex(
            input.ownerDeliveryKeyHex, deliveryKey)) {
        return rejected("invalid-owner-identity");
    }

    const std::vector<std::string> palaceFields{
        input.ownerAccountIdHex,
        input.title,
        input.palaceManifestCid,
        input.atriumManifestCid,
        input.loungeManifestCid,
        input.doorScriptCid,
    };
    const auto palaceId = deriveStableId("palace", palaceFields);
    if (!palaceId.has_value())
        return rejected("palace-id-derivation");

    const std::string palaceIdHex = PalaceLezCodec::bytes32Hex(*palaceId);
    const auto ownerGrantId = deriveStableId(
        "owner-grant", {palaceIdHex, input.ownerAccountIdHex});
    const auto atriumId = deriveStableId(
        "room", {palaceIdHex, "atrium"});
    const auto loungeId = deriveStableId(
        "room", {palaceIdHex, "lounge"});
    if (!ownerGrantId.has_value() || !atriumId.has_value()
        || !loungeId.has_value() || *atriumId == *loungeId) {
        return rejected("record-id-derivation");
    }

    PalaceLezInitializeV3 initialize;
    initialize.palaceId = *palaceId;
    initialize.title = input.title;
    initialize.activeManifestCid = input.palaceManifestCid;
    initialize.ownerProfile = {
        input.ownerDisplayName,
        deliveryKey,
        input.ownerDeliveryKeyEpoch,
        std::nullopt,
    };
    initialize.ownerGrantId = *ownerGrantId;
    initialize.entryRoomId = *atriumId;
    initialize.entryRoom = {
        "Atrium",
        input.atriumManifestCid,
        input.doorScriptCid,
        PalaceLezVmProfileV3::IptScraeMvpV1,
    };
    initialize.secondaryRoomId = *loungeId;
    initialize.secondaryRoom = {
        "Lounge",
        input.loungeManifestCid,
        input.doorScriptCid,
        PalaceLezVmProfileV3::IptScraeMvpV1,
    };

    PalaceLezInstructionV3 instruction{initialize};
    const PalaceLezWireInstruction encoded =
        PalaceLezCodec::encodeInstruction(instruction);
    if (!encoded.accepted)
        return rejected("invalid-authoring-" + encoded.reason);
    return {true, "accepted", std::move(instruction)};
}

PalaceInitialRoomStateResultV1 buildPalaceInitialRoomStateV1(
    const PalaceLezRootRecordV3& root,
    const PalaceLezRoomRecordV3& entryRoom,
    const std::uint64_t orderedActionId)
{
    if (!nonzero(root.palaceId) || !nonzero(root.owner)
        || !nonzero(root.entryRoomId)
        || root.entryRoomId != entryRoom.roomId
        || entryRoom.palaceId != root.palaceId
        || root.lastOrderedActionId
            == std::numeric_limits<std::uint64_t>::max()
        || orderedActionId != root.lastOrderedActionId + 1U) {
        return rejectedInitialRoomState("invalid-initial-room-authority");
    }

    const std::string palaceIdHex =
        PalaceLezCodec::bytes32Hex(root.palaceId);
    const std::string ownerIdHex =
        PalaceLezCodec::bytes32Hex(root.owner);
    const std::string roomIdHex =
        PalaceLezCodec::bytes32Hex(entryRoom.roomId);
    const auto ownerGrantId = deriveStableId(
        "owner-grant", {palaceIdHex, ownerIdHex});
    const auto sharedStateId = deriveStableId(
        "initial-room-state",
        {palaceIdHex, roomIdHex, "door_open"});
    PalaceLezBytes32 stateRoot{};
    if (!ownerGrantId.has_value() || !sharedStateId.has_value()
        || !PalaceLezCodec::parseBytes32Hex(
            crypto::sha256Hex("door_open=0"), stateRoot)) {
        return rejectedInitialRoomState("initial-room-state-id-derivation");
    }

    PalaceLezCreateSharedStateV3 state;
    state.orderedActionId = orderedActionId;
    state.grantId = *ownerGrantId;
    state.sharedStateId = *sharedStateId;
    state.roomId = entryRoom.roomId;
    state.key = "door_open";
    state.value = {'0'};
    state.stateRoot = stateRoot;
    PalaceLezInstructionV3 instruction{state};
    const PalaceLezWireInstruction encoded =
        PalaceLezCodec::encodeInstruction(instruction);
    if (!encoded.accepted)
        return rejectedInitialRoomState(
            "invalid-initial-room-state-" + encoded.reason);
    return {true, "accepted", std::move(instruction)};
}

} // namespace palace
