#include <logos_test.h>

#include <string>

#include "palace_human_moderation.h"

namespace {

palace::PalaceHumanModerationRequestV1 request(
    const palace::PalaceHumanModerationTargetV1 kind,
    const std::string& target)
{
    palace::PalaceHumanModerationRequestV1 value;
    value.targetKind = kind;
    value.actionId = "8";
    value.programIdHex = std::string(64U, '1');
    value.rootAccountIdHex = std::string(64U, '2');
    value.issuerAccountIdHex = std::string(64U, '3');
    value.grantIdHex = std::string(64U, '4');
    value.target = target;
    return value;
}

} // namespace

LOGOS_TEST(human_moderation_builds_exact_deterministic_typed_user_ban) {
    const palace::PalaceHumanModerationRequestV1 input =
        request(
            palace::PalaceHumanModerationTargetV1::User,
            std::string(64U, '5'));
    const palace::PalaceHumanModerationCommandV1 first =
        palace::buildPalaceHumanModerationCommandV1(input);
    const palace::PalaceHumanModerationCommandV1 repeated =
        palace::buildPalaceHumanModerationCommandV1(input);

    LOGOS_ASSERT_TRUE(first.accepted);
    LOGOS_ASSERT_EQ(first.reason, std::string("accepted"));
    LOGOS_ASSERT_EQ(first.actionId, std::string("8"));
    LOGOS_ASSERT_EQ(first.banIdHex.size(), 64U);
    LOGOS_ASSERT_EQ(
        first.banIdHex,
        std::string(
            "f1b9dbf72638bf8787720e696ef16ae5"
            "302804fea5880643fd0eca8f039313c3"));
    LOGOS_ASSERT_EQ(first.banIdHex, repeated.banIdHex);
    LOGOS_ASSERT_EQ(first.transitionJson, repeated.transitionJson);
    LOGOS_ASSERT_EQ(
        first.transitionJson,
        "{\"kind\":\"create_user_ban\",\"grant_id_hex\":\""
            + std::string(64U, '4')
            + "\",\"ban_id_hex\":\"" + first.banIdHex
            + "\",\"subject_user_id_hex\":\""
            + std::string(64U, '5')
            + "\",\"scope\":{\"kind\":\"palace\"}}");
    LOGOS_ASSERT_EQ(first.transitionSha256Hex.size(), 64U);

    palace::PalaceHumanModerationRequestV1 next = input;
    next.actionId = "9";
    const palace::PalaceHumanModerationCommandV1 changed =
        palace::buildPalaceHumanModerationCommandV1(next);
    LOGOS_ASSERT_TRUE(changed.accepted);
    LOGOS_ASSERT_NE(changed.banIdHex, first.banIdHex);
}

LOGOS_TEST(human_moderation_builds_asset_ban_and_rejects_untyped_input) {
    const palace::PalaceHumanModerationCommandV1 asset =
        palace::buildPalaceHumanModerationCommandV1(
            request(
                palace::PalaceHumanModerationTargetV1::AssetCid,
                "bafybeigdyrzt5sfp7udm7hu76uh7y26nf3k4q"));
    LOGOS_ASSERT_TRUE(asset.accepted);
    LOGOS_ASSERT_TRUE(
        asset.transitionJson.rfind(
            "{\"kind\":\"create_asset_ban\",", 0U)
        == 0U);

    palace::PalaceHumanModerationRequestV1 invalidUser =
        request(
            palace::PalaceHumanModerationTargetV1::User,
            std::string(63U, '5'));
    LOGOS_ASSERT_FALSE(
        palace::buildPalaceHumanModerationCommandV1(
            invalidUser).accepted);

    palace::PalaceHumanModerationRequestV1 invalidAction =
        request(
            palace::PalaceHumanModerationTargetV1::User,
            std::string(64U, '5'));
    invalidAction.actionId = "0";
    LOGOS_ASSERT_FALSE(
        palace::buildPalaceHumanModerationCommandV1(
            invalidAction).accepted);

    palace::PalaceHumanModerationRequestV1 invalidAsset =
        request(
            palace::PalaceHumanModerationTargetV1::AssetCid,
            "not a cid");
    LOGOS_ASSERT_FALSE(
        palace::buildPalaceHumanModerationCommandV1(
            invalidAsset).accepted);
}
