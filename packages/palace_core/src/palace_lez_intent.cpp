#include "palace_lez_intent.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>

namespace palace {
namespace {

constexpr std::size_t kMaximumIntentBytes = 64U * 1024U;
constexpr std::size_t kMaximumJsonDepth = 32U;
constexpr std::size_t kMaximumSharedValueBytes = 512U;

class UniqueKeyJsonScanner {
public:
    explicit UniqueKeyJsonScanner(const QByteArray& input)
        : input_(input)
    {
    }

    bool scan()
    {
        skipWhitespace();
        return parseValue(0U) && !duplicate_
            && (skipWhitespace(), cursor_ == input_.size());
    }

    bool duplicate() const
    {
        return duplicate_;
    }

private:
    bool parseValue(const std::size_t depth)
    {
        if (depth > kMaximumJsonDepth)
            return false;
        skipWhitespace();
        if (cursor_ >= input_.size())
            return false;
        switch (input_.at(cursor_)) {
        case '{':
            return parseObject(depth + 1U);
        case '[':
            return parseArray(depth + 1U);
        case '"':
            return parseString(nullptr);
        case 't':
            return consumeLiteral("true");
        case 'f':
            return consumeLiteral("false");
        case 'n':
            return consumeLiteral("null");
        default:
            return parseNumber();
        }
    }

    bool parseObject(const std::size_t depth)
    {
        ++cursor_;
        skipWhitespace();
        if (consume('}'))
            return true;

        std::set<QString> keys;
        for (;;) {
            QString key;
            if (!parseString(&key))
                return false;
            if (!keys.insert(key).second) {
                duplicate_ = true;
                return false;
            }
            skipWhitespace();
            if (!consume(':') || !parseValue(depth))
                return false;
            skipWhitespace();
            if (consume('}'))
                return true;
            if (!consume(','))
                return false;
            skipWhitespace();
        }
    }

    bool parseArray(const std::size_t depth)
    {
        ++cursor_;
        skipWhitespace();
        if (consume(']'))
            return true;
        for (;;) {
            if (!parseValue(depth))
                return false;
            skipWhitespace();
            if (consume(']'))
                return true;
            if (!consume(','))
                return false;
            skipWhitespace();
        }
    }

    bool parseString(QString* decoded)
    {
        if (!consume('"'))
            return false;
        const qsizetype start = cursor_ - 1;
        while (cursor_ < input_.size()) {
            const unsigned char character =
                static_cast<unsigned char>(input_.at(cursor_++));
            if (character == '"') {
                if (decoded != nullptr) {
                    const QByteArray raw =
                        input_.mid(start, cursor_ - start);
                    const QJsonDocument wrapper =
                        QJsonDocument::fromJson("[" + raw + "]");
                    if (!wrapper.isArray()
                        || wrapper.array().size() != 1
                        || !wrapper.array().at(0).isString()) {
                        return false;
                    }
                    *decoded = wrapper.array().at(0).toString();
                }
                return true;
            }
            if (character < 0x20U)
                return false;
            if (character != '\\')
                continue;
            if (cursor_ >= input_.size())
                return false;
            const char escaped = input_.at(cursor_++);
            if (escaped == 'u') {
                if (input_.size() - cursor_ < 4)
                    return false;
                for (int index = 0; index < 4; ++index) {
                    const unsigned char hex =
                        static_cast<unsigned char>(input_.at(cursor_++));
                    if (!std::isxdigit(hex))
                        return false;
                }
            } else if (escaped != '"' && escaped != '\\'
                       && escaped != '/' && escaped != 'b'
                       && escaped != 'f' && escaped != 'n'
                       && escaped != 'r' && escaped != 't') {
                return false;
            }
        }
        return false;
    }

    bool parseNumber()
    {
        const qsizetype start = cursor_;
        consume('-');
        if (consume('0')) {
            if (cursor_ < input_.size()
                && input_.at(cursor_) >= '0'
                && input_.at(cursor_) <= '9') {
                return false;
            }
        } else {
            if (cursor_ >= input_.size()
                || input_.at(cursor_) < '1'
                || input_.at(cursor_) > '9') {
                return false;
            }
            while (cursor_ < input_.size()
                   && input_.at(cursor_) >= '0'
                   && input_.at(cursor_) <= '9') {
                ++cursor_;
            }
        }
        if (consume('.')) {
            const qsizetype digits = cursor_;
            while (cursor_ < input_.size()
                   && input_.at(cursor_) >= '0'
                   && input_.at(cursor_) <= '9') {
                ++cursor_;
            }
            if (digits == cursor_)
                return false;
        }
        if (cursor_ < input_.size()
            && (input_.at(cursor_) == 'e' || input_.at(cursor_) == 'E')) {
            ++cursor_;
            if (cursor_ < input_.size()
                && (input_.at(cursor_) == '+'
                    || input_.at(cursor_) == '-')) {
                ++cursor_;
            }
            const qsizetype digits = cursor_;
            while (cursor_ < input_.size()
                   && input_.at(cursor_) >= '0'
                   && input_.at(cursor_) <= '9') {
                ++cursor_;
            }
            if (digits == cursor_)
                return false;
        }
        return cursor_ != start;
    }

    bool consumeLiteral(const char* literal)
    {
        const qsizetype length =
            static_cast<qsizetype>(std::char_traits<char>::length(literal));
        if (input_.mid(cursor_, length) != literal)
            return false;
        cursor_ += length;
        return true;
    }

    bool consume(const char expected)
    {
        if (cursor_ >= input_.size() || input_.at(cursor_) != expected)
            return false;
        ++cursor_;
        return true;
    }

    void skipWhitespace()
    {
        while (cursor_ < input_.size()) {
            const char character = input_.at(cursor_);
            if (character != ' ' && character != '\t'
                && character != '\r' && character != '\n') {
                break;
            }
            ++cursor_;
        }
    }

    const QByteArray& input_;
    qsizetype cursor_ = 0;
    bool duplicate_ = false;
};

bool exactKeys(
    const QJsonObject& object,
    const std::initializer_list<const char*> expected)
{
    if (object.size() != static_cast<qsizetype>(expected.size()))
        return false;
    return std::all_of(
        expected.begin(), expected.end(), [&](const char* key) {
            return object.contains(QString::fromLatin1(key));
        });
}

bool parseString(
    const QJsonObject& object,
    const char* key,
    std::string& output)
{
    const QJsonValue value = object.value(QString::fromLatin1(key));
    if (!value.isString())
        return false;
    output = value.toString().toStdString();
    return true;
}

bool parseOptionalString(
    const QJsonObject& object,
    const char* key,
    std::optional<std::string>& output)
{
    const QJsonValue value = object.value(QString::fromLatin1(key));
    if (value.isNull()) {
        output.reset();
        return true;
    }
    if (!value.isString())
        return false;
    output = value.toString().toStdString();
    return true;
}

bool parseBool(const QJsonObject& object, const char* key, bool& output)
{
    const QJsonValue value = object.value(QString::fromLatin1(key));
    if (!value.isBool())
        return false;
    output = value.toBool();
    return true;
}

template <typename Integer>
bool parseDecimal(
    const QJsonObject& object,
    const char* key,
    Integer& output)
{
    const QJsonValue value = object.value(QString::fromLatin1(key));
    if (!value.isString())
        return false;
    const std::string input = value.toString().toStdString();
    if (input.empty()
        || (input.size() > 1U && input.front() == '0')
        || !std::all_of(
            input.begin(), input.end(), [](const unsigned char character) {
                return character >= '0' && character <= '9';
            })) {
        return false;
    }
    Integer parsed = 0;
    const auto [cursor, error] = std::from_chars(
        input.data(), input.data() + input.size(), parsed);
    if (error != std::errc() || cursor != input.data() + input.size())
        return false;
    output = parsed;
    return true;
}

bool parseBytes32(
    const QJsonObject& object,
    const char* key,
    PalaceLezBytes32& output)
{
    std::string encoded;
    return parseString(object, key, encoded)
        && PalaceLezCodec::parseBytes32Hex(encoded, output);
}

bool parseHexBytes(
    const QJsonObject& object,
    const char* key,
    std::vector<std::uint8_t>& output)
{
    std::string encoded;
    if (!parseString(object, key, encoded)
        || encoded.size() % 2U != 0U
        || encoded.size() > kMaximumSharedValueBytes * 2U) {
        return false;
    }
    output.clear();
    output.reserve(encoded.size() / 2U);
    for (std::size_t index = 0U; index < encoded.size(); index += 2U) {
        const auto nibble = [](const char character) -> int {
            if (character >= '0' && character <= '9')
                return character - '0';
            if (character >= 'a' && character <= 'f')
                return character - 'a' + 10;
            return -1;
        };
        const int high = nibble(encoded[index]);
        const int low = nibble(encoded[index + 1U]);
        if (high < 0 || low < 0)
            return false;
        output.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return true;
}

bool parseProfile(
    const QJsonValue& value,
    PalaceLezUserProfileInputV3& output)
{
    if (!value.isObject())
        return false;
    const QJsonObject object = value.toObject();
    return exactKeys(
               object,
               {"display_name", "delivery_key_hex", "key_epoch",
                "avatar_manifest_cid"})
        && parseString(object, "display_name", output.displayName)
        && parseBytes32(object, "delivery_key_hex", output.deliveryKey)
        && parseDecimal(object, "key_epoch", output.keyEpoch)
        && parseOptionalString(
            object, "avatar_manifest_cid", output.avatarManifestCid);
}

bool parseRoomConfig(
    const QJsonValue& value,
    PalaceLezRoomConfigInputV3& output)
{
    if (!value.isObject())
        return false;
    const QJsonObject object = value.toObject();
    std::string profile;
    if (!exactKeys(
            object,
            {"title", "manifest_cid", "script_bundle_cid", "vm_profile"})
        || !parseString(object, "title", output.title)
        || !parseString(object, "manifest_cid", output.manifestCid)
        || !parseString(object, "script_bundle_cid", output.scriptBundleCid)
        || !parseString(object, "vm_profile", profile)) {
        return false;
    }
    if (profile == "no_script")
        output.vmProfile = PalaceLezVmProfileV3::NoScript;
    else if (profile == "iptscrae_mvp_v1")
        output.vmProfile = PalaceLezVmProfileV3::IptScraeMvpV1;
    else
        return false;
    return true;
}

bool parseScope(const QJsonValue& value, PalaceLezScopeV3& output)
{
    if (!value.isObject())
        return false;
    const QJsonObject object = value.toObject();
    std::string kind;
    if (!parseString(object, "kind", kind))
        return false;
    if (kind == "palace") {
        if (!exactKeys(object, {"kind"}))
            return false;
        output = PalaceLezScopeV3{};
        output.kind = PalaceLezScopeKindV3::Palace;
        return true;
    }
    if (kind == "room") {
        if (!exactKeys(object, {"kind", "room_id_hex"})
            || !parseBytes32(object, "room_id_hex", output.roomId)) {
            return false;
        }
        output.kind = PalaceLezScopeKindV3::Room;
        return true;
    }
    return false;
}

PalaceLezIntentParseResult rejected(const std::string& reason)
{
    return {false, reason, {}};
}

} // namespace

PalaceLezIntentParseResult PalaceLezIntentParser::parse(
    const std::string& orderedActionId,
    const std::string& transitionJson)
{
    if (transitionJson.empty()
        || transitionJson.size() > kMaximumIntentBytes) {
        return rejected("invalid-size");
    }

    const QByteArray bytes = QByteArray::fromStdString(transitionJson);
    UniqueKeyJsonScanner scanner(bytes);
    if (!scanner.scan())
        return rejected(scanner.duplicate() ? "duplicate-key" : "invalid-json");

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        return rejected("invalid-json");
    }
    const QJsonObject object = document.object();
    std::string kind;
    if (!parseString(object, "kind", kind))
        return rejected("invalid-kind");

    std::uint64_t action = 0U;
    if (kind == "initialize") {
        if (orderedActionId != "0"
            || !exactKeys(
                object,
                {"kind", "palace_id_hex", "title",
                 "active_manifest_cid", "owner_profile",
                 "owner_grant_id_hex", "entry_room_id_hex", "entry_room",
                 "secondary_room_id_hex", "secondary_room"})) {
            return rejected("invalid-initialize");
        }
        PalaceLezInitializeV3 value;
        if (!parseBytes32(object, "palace_id_hex", value.palaceId)
            || !parseString(object, "title", value.title)
            || !parseString(
                object, "active_manifest_cid", value.activeManifestCid)
            || !parseProfile(object.value("owner_profile"), value.ownerProfile)
            || !parseBytes32(
                object, "owner_grant_id_hex", value.ownerGrantId)
            || !parseBytes32(
                object, "entry_room_id_hex", value.entryRoomId)
            || !parseRoomConfig(
                object.value("entry_room"), value.entryRoom)
            || !parseBytes32(
                object, "secondary_room_id_hex", value.secondaryRoomId)
            || !parseRoomConfig(
                object.value("secondary_room"), value.secondaryRoom)) {
            return rejected("invalid-initialize");
        }
        return {true, "accepted", PalaceLezInstructionV3{value}};
    }

    if (!PalaceLezCodec::parseOrderedActionId(orderedActionId, action))
        return rejected("invalid-ordered-action-id");

    if (kind == "register_user") {
        if (!exactKeys(object, {"kind", "profile"}))
            return rejected("invalid-register-user");
        PalaceLezRegisterUserV3 value;
        value.orderedActionId = action;
        if (!parseProfile(object.value("profile"), value.profile))
            return rejected("invalid-register-user");
        return {true, "accepted", PalaceLezInstructionV3{value}};
    }
    if (kind == "update_user_profile") {
        if (!exactKeys(
                object,
                {"kind", "display_name", "avatar_manifest_cid"})) {
            return rejected("invalid-update-user-profile");
        }
        PalaceLezUpdateUserProfileV3 value;
        value.orderedActionId = action;
        if (!parseString(object, "display_name", value.displayName)
            || !parseOptionalString(
                object, "avatar_manifest_cid", value.avatarManifestCid)) {
            return rejected("invalid-update-user-profile");
        }
        return {true, "accepted", PalaceLezInstructionV3{value}};
    }
    if (kind == "rotate_delivery_key") {
        if (!exactKeys(
                object, {"kind", "delivery_key_hex", "key_epoch"})) {
            return rejected("invalid-rotate-delivery-key");
        }
        PalaceLezRotateDeliveryKeyV3 value;
        value.orderedActionId = action;
        if (!parseBytes32(
                object, "delivery_key_hex", value.deliveryKey)
            || !parseDecimal(object, "key_epoch", value.keyEpoch)) {
            return rejected("invalid-rotate-delivery-key");
        }
        return {true, "accepted", PalaceLezInstructionV3{value}};
    }
    if (kind == "publish_manifest") {
        if (!exactKeys(object, {"kind", "cid"}))
            return rejected("invalid-publish-manifest");
        PalaceLezPublishManifestV3 value;
        value.orderedActionId = action;
        if (!parseString(object, "cid", value.cid))
            return rejected("invalid-publish-manifest");
        return {true, "accepted", PalaceLezInstructionV3{value}};
    }
    if (kind == "update_room") {
        if (!exactKeys(
                object,
                {"kind", "grant_id_hex", "room_id_hex", "config"})) {
            return rejected("invalid-update-room");
        }
        PalaceLezUpdateRoomV3 value;
        value.orderedActionId = action;
        if (!parseBytes32(object, "grant_id_hex", value.grantId)
            || !parseBytes32(object, "room_id_hex", value.roomId)
            || !parseRoomConfig(object.value("config"), value.config)) {
            return rejected("invalid-update-room");
        }
        return {true, "accepted", PalaceLezInstructionV3{value}};
    }
    if (kind == "grant_capability") {
        if (!exactKeys(
                object,
                {"kind", "grant_id_hex", "subject_user_id_hex", "scope",
                 "capabilities", "delegable",
                 "valid_through_action_id"})) {
            return rejected("invalid-grant-capability");
        }
        PalaceLezGrantCapabilityV3 value;
        value.orderedActionId = action;
        if (!parseBytes32(object, "grant_id_hex", value.grantId)
            || !parseBytes32(
                object, "subject_user_id_hex", value.subjectUserId)
            || !parseScope(object.value("scope"), value.scope)
            || !parseDecimal(
                object, "capabilities", value.capabilities)
            || !parseBool(object, "delegable", value.delegable)
            || !parseDecimal(
                object,
                "valid_through_action_id",
                value.validThroughActionId)) {
            return rejected("invalid-grant-capability");
        }
        return {true, "accepted", PalaceLezInstructionV3{value}};
    }
    if (kind == "revoke_capability") {
        if (!exactKeys(object, {"kind", "grant_id_hex"}))
            return rejected("invalid-revoke-capability");
        PalaceLezRevokeCapabilityV3 value;
        value.orderedActionId = action;
        if (!parseBytes32(object, "grant_id_hex", value.grantId))
            return rejected("invalid-revoke-capability");
        return {true, "accepted", PalaceLezInstructionV3{value}};
    }
    if (kind == "set_room_locked") {
        if (!exactKeys(
                object,
                {"kind", "grant_id_hex", "room_id_hex", "locked"})) {
            return rejected("invalid-set-room-locked");
        }
        PalaceLezSetRoomLockedV3 value;
        value.orderedActionId = action;
        if (!parseBytes32(object, "grant_id_hex", value.grantId)
            || !parseBytes32(object, "room_id_hex", value.roomId)
            || !parseBool(object, "locked", value.locked)) {
            return rejected("invalid-set-room-locked");
        }
        return {true, "accepted", PalaceLezInstructionV3{value}};
    }
    if (kind == "create_user_ban") {
        if (!exactKeys(
                object,
                {"kind", "grant_id_hex", "ban_id_hex",
                 "subject_user_id_hex", "scope"})) {
            return rejected("invalid-create-user-ban");
        }
        PalaceLezCreateUserBanV3 value;
        value.orderedActionId = action;
        if (!parseBytes32(object, "grant_id_hex", value.grantId)
            || !parseBytes32(object, "ban_id_hex", value.banId)
            || !parseBytes32(
                object, "subject_user_id_hex", value.subjectUserId)
            || !parseScope(object.value("scope"), value.scope)) {
            return rejected("invalid-create-user-ban");
        }
        return {true, "accepted", PalaceLezInstructionV3{value}};
    }
    if (kind == "create_asset_ban") {
        if (!exactKeys(
                object,
                {"kind", "grant_id_hex", "ban_id_hex", "cid", "scope"})) {
            return rejected("invalid-create-asset-ban");
        }
        PalaceLezCreateAssetBanV3 value;
        value.orderedActionId = action;
        if (!parseBytes32(object, "grant_id_hex", value.grantId)
            || !parseBytes32(object, "ban_id_hex", value.banId)
            || !parseString(object, "cid", value.cid)
            || !parseScope(object.value("scope"), value.scope)) {
            return rejected("invalid-create-asset-ban");
        }
        return {true, "accepted", PalaceLezInstructionV3{value}};
    }
    if (kind == "set_ban_active") {
        if (!exactKeys(
                object,
                {"kind", "grant_id_hex", "ban_id_hex", "active"})) {
            return rejected("invalid-set-ban-active");
        }
        PalaceLezSetBanActiveV3 value;
        value.orderedActionId = action;
        if (!parseBytes32(object, "grant_id_hex", value.grantId)
            || !parseBytes32(object, "ban_id_hex", value.banId)
            || !parseBool(object, "active", value.active)) {
            return rejected("invalid-set-ban-active");
        }
        return {true, "accepted", PalaceLezInstructionV3{value}};
    }
    if (kind == "create_shared_state") {
        if (!exactKeys(
                object,
                {"kind", "grant_id_hex", "shared_state_id_hex",
                 "room_id_hex", "key", "value_hex", "state_root_hex"})) {
            return rejected("invalid-create-shared-state");
        }
        PalaceLezCreateSharedStateV3 value;
        value.orderedActionId = action;
        if (!parseBytes32(object, "grant_id_hex", value.grantId)
            || !parseBytes32(
                object, "shared_state_id_hex", value.sharedStateId)
            || !parseBytes32(object, "room_id_hex", value.roomId)
            || !parseString(object, "key", value.key)
            || !parseHexBytes(object, "value_hex", value.value)
            || !parseBytes32(
                object, "state_root_hex", value.stateRoot)) {
            return rejected("invalid-create-shared-state");
        }
        return {true, "accepted", PalaceLezInstructionV3{value}};
    }
    if (kind == "update_shared_state") {
        if (!exactKeys(
                object,
                {"kind", "grant_id_hex", "shared_state_id_hex",
                 "room_id_hex", "state_revision", "value_hex",
                 "state_root_hex"})) {
            return rejected("invalid-update-shared-state");
        }
        PalaceLezUpdateSharedStateV3 value;
        value.orderedActionId = action;
        if (!parseBytes32(object, "grant_id_hex", value.grantId)
            || !parseBytes32(
                object, "shared_state_id_hex", value.sharedStateId)
            || !parseBytes32(object, "room_id_hex", value.roomId)
            || !parseDecimal(
                object, "state_revision", value.stateRevision)
            || !parseHexBytes(object, "value_hex", value.value)
            || !parseBytes32(
                object, "state_root_hex", value.stateRoot)) {
            return rejected("invalid-update-shared-state");
        }
        return {true, "accepted", PalaceLezInstructionV3{value}};
    }

    return rejected("unknown-kind");
}

} // namespace palace
