#include "palace_human_moderation.h"

#include "palace_lez.h"
#include "palace_lez_intent.h"
#include "palace_sha256.h"
#include "palace_storage.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <variant>

namespace palace {
namespace {

bool isNonzeroLowerHex64(const std::string& value)
{
    return value.size() == 64U
        && value != std::string(64U, '0')
        && std::all_of(
            value.begin(),
            value.end(),
            [](const unsigned char character) {
                return (character >= '0' && character <= '9')
                    || (character >= 'a' && character <= 'f');
            });
}

std::string lengthEncoded(const std::string& value)
{
    return std::to_string(value.size()) + ":" + value;
}

PalaceHumanModerationCommandV1 rejected(const std::string& reason)
{
    PalaceHumanModerationCommandV1 result;
    result.reason = reason;
    return result;
}

} // namespace

PalaceHumanModerationCommandV1 buildPalaceHumanModerationCommandV1(
    const PalaceHumanModerationRequestV1& request)
{
    std::uint64_t orderedActionId = 0U;
    if (!PalaceLezCodec::parseOrderedActionId(
            request.actionId, orderedActionId)
        || orderedActionId == 0U
        || !isNonzeroLowerHex64(request.programIdHex)
        || !isNonzeroLowerHex64(request.rootAccountIdHex)
        || !isNonzeroLowerHex64(request.issuerAccountIdHex)
        || !isNonzeroLowerHex64(request.grantIdHex)) {
        return rejected("invalid-moderation-context");
    }
    if (request.targetKind == PalaceHumanModerationTargetV1::User) {
        if (!isNonzeroLowerHex64(request.target))
            return rejected("invalid-moderation-user");
    } else if (!isSafePalaceCid(request.target)) {
        return rejected("invalid-moderation-asset");
    }

    const std::string kind =
        request.targetKind == PalaceHumanModerationTargetV1::User
        ? "user" : "asset";
    const std::string identifierMaterial =
        "logos-palace-human-ban-v1;"
        "kind=" + lengthEncoded(kind)
        + ";action=" + lengthEncoded(request.actionId)
        + ";program=" + lengthEncoded(request.programIdHex)
        + ";root=" + lengthEncoded(request.rootAccountIdHex)
        + ";issuer=" + lengthEncoded(request.issuerAccountIdHex)
        + ";target=" + lengthEncoded(request.target)
        + ";scope=" + lengthEncoded("palace");
    const std::string banIdHex =
        crypto::sha256Hex(identifierMaterial);
    if (!isNonzeroLowerHex64(banIdHex))
        return rejected("moderation-ban-id");

    const std::string transitionJson =
        request.targetKind == PalaceHumanModerationTargetV1::User
        ? "{\"kind\":\"create_user_ban\",\"grant_id_hex\":\""
            + request.grantIdHex + "\",\"ban_id_hex\":\""
            + banIdHex + "\",\"subject_user_id_hex\":\""
            + request.target + "\",\"scope\":{\"kind\":\"palace\"}}"
        : "{\"kind\":\"create_asset_ban\",\"grant_id_hex\":\""
            + request.grantIdHex + "\",\"ban_id_hex\":\""
            + banIdHex + "\",\"cid\":\"" + request.target
            + "\",\"scope\":{\"kind\":\"palace\"}}";

    const PalaceLezIntentParseResult parsed =
        PalaceLezIntentParser::parse(
            request.actionId, transitionJson);
    if (!parsed.accepted)
        return rejected("moderation-transition-" + parsed.reason);
    if (request.targetKind == PalaceHumanModerationTargetV1::User) {
        const auto* instruction =
            std::get_if<PalaceLezCreateUserBanV3>(
                &parsed.instruction.payload);
        PalaceLezBytes32 expectedGrant{};
        PalaceLezBytes32 expectedBan{};
        PalaceLezBytes32 expectedTarget{};
        if (instruction == nullptr
            || !PalaceLezCodec::parseBytes32Hex(
                request.grantIdHex, expectedGrant)
            || !PalaceLezCodec::parseBytes32Hex(
                banIdHex, expectedBan)
            || !PalaceLezCodec::parseBytes32Hex(
                request.target, expectedTarget)
            || instruction->orderedActionId != orderedActionId
            || instruction->grantId != expectedGrant
            || instruction->banId != expectedBan
            || instruction->subjectUserId != expectedTarget
            || instruction->scope.kind
                != PalaceLezScopeKindV3::Palace) {
            return rejected("moderation-transition-mismatch");
        }
    } else {
        const auto* instruction =
            std::get_if<PalaceLezCreateAssetBanV3>(
                &parsed.instruction.payload);
        PalaceLezBytes32 expectedGrant{};
        PalaceLezBytes32 expectedBan{};
        if (instruction == nullptr
            || !PalaceLezCodec::parseBytes32Hex(
                request.grantIdHex, expectedGrant)
            || !PalaceLezCodec::parseBytes32Hex(
                banIdHex, expectedBan)
            || instruction->orderedActionId != orderedActionId
            || instruction->grantId != expectedGrant
            || instruction->banId != expectedBan
            || instruction->cid != request.target
            || instruction->scope.kind
                != PalaceLezScopeKindV3::Palace) {
            return rejected("moderation-transition-mismatch");
        }
    }

    return {
        true,
        "accepted",
        request.actionId,
        banIdHex,
        transitionJson,
        crypto::sha256Hex(transitionJson),
    };
}

} // namespace palace
