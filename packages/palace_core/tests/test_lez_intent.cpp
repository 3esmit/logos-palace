#include <logos_test.h>

#include <array>
#include <string>
#include <vector>

#include "palace_lez_intent.h"

namespace {

std::string id(const char digit)
{
    return std::string(64U, digit);
}

std::string quote(const std::string& value)
{
    return "\"" + value + "\"";
}

std::string profile(const char key)
{
    return "{\"display_name\":\"Alice\",\"delivery_key_hex\":"
        + quote(id(key))
        + ",\"key_epoch\":\"1\",\"avatar_manifest_cid\":null}";
}

std::string room(const std::string& title)
{
    return "{\"title\":" + quote(title)
        + ",\"manifest_cid\":\"bafyroom\",\"script_bundle_cid\":"
          "\"bafyscript\",\"vm_profile\":\"iptscrae_mvp_v1\"}";
}

} // namespace

LOGOS_TEST(lez_intent_accepts_all_schema_v3_variants)
{
    using palace::PalaceLezIntentParser;
    using palace::PalaceLezCodec;

    const std::string initialize =
        "{\"kind\":\"initialize\",\"palace_id_hex\":" + quote(id('1'))
        + ",\"title\":\"Palace\",\"active_manifest_cid\":\"bafypalace\","
          "\"owner_profile\":" + profile('2')
        + ",\"owner_grant_id_hex\":" + quote(id('3'))
        + ",\"entry_room_id_hex\":" + quote(id('4'))
        + ",\"entry_room\":" + room("Atrium")
        + ",\"secondary_room_id_hex\":" + quote(id('5'))
        + ",\"secondary_room\":" + room("Lounge") + "}";

    struct Vector {
        std::string action;
        std::string json;
    };
    const std::vector<Vector> vectors{
        {"0", initialize},
        {"1", "{\"kind\":\"register_user\",\"profile\":" + profile('6') + "}"},
        {"2", "{\"kind\":\"update_user_profile\",\"display_name\":\"Alice 2\","
              "\"avatar_manifest_cid\":\"bafyavatar\"}"},
        {"3", "{\"kind\":\"rotate_delivery_key\",\"delivery_key_hex\":"
              + quote(id('7')) + ",\"key_epoch\":\"2\"}"},
        {"4", "{\"kind\":\"publish_manifest\",\"cid\":\"bafyupdated\"}"},
        {"5", "{\"kind\":\"update_room\",\"grant_id_hex\":" + quote(id('3'))
              + ",\"room_id_hex\":" + quote(id('4'))
              + ",\"config\":" + room("Atrium 2") + "}"},
        {"6", "{\"kind\":\"grant_capability\",\"grant_id_hex\":" + quote(id('8'))
              + ",\"subject_user_id_hex\":" + quote(id('6'))
              + ",\"scope\":{\"kind\":\"room\",\"room_id_hex\":" + quote(id('4'))
              + "},\"capabilities\":\"5\",\"delegable\":false,"
                "\"valid_through_action_id\":\"20\"}"},
        {"7", "{\"kind\":\"revoke_capability\",\"grant_id_hex\":" + quote(id('8'))
              + "}"},
        {"8", "{\"kind\":\"set_room_locked\",\"grant_id_hex\":" + quote(id('3'))
              + ",\"room_id_hex\":" + quote(id('4')) + ",\"locked\":true}"},
        {"9", "{\"kind\":\"create_user_ban\",\"grant_id_hex\":" + quote(id('3'))
              + ",\"ban_id_hex\":" + quote(id('9'))
              + ",\"subject_user_id_hex\":" + quote(id('6'))
              + ",\"scope\":{\"kind\":\"palace\"}}"},
        {"10", "{\"kind\":\"create_asset_ban\",\"grant_id_hex\":" + quote(id('3'))
               + ",\"ban_id_hex\":" + quote(std::string(64U, 'a'))
               + ",\"cid\":\"bafyblocked\",\"scope\":{\"kind\":\"room\","
                 "\"room_id_hex\":" + quote(id('4')) + "}}"},
        {"11", "{\"kind\":\"set_ban_active\",\"grant_id_hex\":" + quote(id('3'))
               + ",\"ban_id_hex\":" + quote(std::string(64U, 'a'))
               + ",\"active\":false}"},
        {"12", "{\"kind\":\"create_shared_state\",\"grant_id_hex\":"
               + quote(id('3')) + ",\"shared_state_id_hex\":"
               + quote(std::string(64U, 'b')) + ",\"room_id_hex\":"
               + quote(id('4')) + ",\"key\":\"door\",\"value_hex\":\"aabb\","
                 "\"state_root_hex\":" + quote(std::string(64U, 'c')) + "}"},
        {"13", "{\"kind\":\"update_shared_state\",\"grant_id_hex\":"
               + quote(id('3')) + ",\"shared_state_id_hex\":"
               + quote(std::string(64U, 'b')) + ",\"room_id_hex\":"
               + quote(id('4')) + ",\"state_revision\":\"2\","
                 "\"value_hex\":\"01\",\"state_root_hex\":"
               + quote(std::string(64U, 'd')) + "}"},
    };

    for (const Vector& vector : vectors) {
        const auto parsed = PalaceLezIntentParser::parse(
            vector.action, vector.json);
        LOGOS_ASSERT_TRUE(parsed.accepted);
        const auto wire = PalaceLezCodec::encodeInstruction(parsed.instruction);
        LOGOS_ASSERT_TRUE(wire.accepted);
    }
}

LOGOS_TEST(lez_intent_rejects_ambiguous_or_noncanonical_commands)
{
    using palace::PalaceLezIntentParser;

    const std::array<std::pair<std::string, std::string>, 9> vectors{{
        {"1", R"({"kind":"publish_manifest","kind":"publish_manifest","cid":"bafy"})"},
        {"1", R"({"\u006b\u0069\u006e\u0064":"publish_manifest","kind":"publish_manifest","cid":"bafy"})"},
        {"01", R"({"kind":"publish_manifest","cid":"bafy"})"},
        {"0", R"({"kind":"publish_manifest","cid":"bafy"})"},
        {"1", R"({"kind":"publish_manifest","cid":"bafy","extra":false})"},
        {"1", R"({"kind":"rotate_delivery_key","delivery_key_hex":"1111111111111111111111111111111111111111111111111111111111111111","key_epoch":2})"},
        {"1", R"({"kind":"grant_capability","grant_id_hex":"1111111111111111111111111111111111111111111111111111111111111111","subject_user_id_hex":"2222222222222222222222222222222222222222222222222222222222222222","scope":{"kind":"palace","room_id_hex":"3333333333333333333333333333333333333333333333333333333333333333"},"capabilities":"1","delegable":false,"valid_through_action_id":"2"})"},
        {"1", R"({"kind":"create_shared_state","grant_id_hex":"1111111111111111111111111111111111111111111111111111111111111111","shared_state_id_hex":"2222222222222222222222222222222222222222222222222222222222222222","room_id_hex":"3333333333333333333333333333333333333333333333333333333333333333","key":"door","value_hex":"AA","state_root_hex":"4444444444444444444444444444444444444444444444444444444444444444"})"},
        {"1", R"({"kind":"not_a_transition"})"},
    }};

    for (const auto& vector : vectors) {
        LOGOS_ASSERT_FALSE(
            PalaceLezIntentParser::parse(vector.first, vector.second).accepted);
    }
}
