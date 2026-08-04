#include "palace_storage_module_codec.h"

#include "palace_storage_cid.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace palace {
namespace {

constexpr std::string_view kLifecycleSnapshotSchema =
    "logos.managed_node_lifecycle.snapshot";
constexpr std::string_view kLifecycleCommandSchema =
    "logos.managed_node_lifecycle.command";
constexpr std::string_view kLifecycleAcknowledgementSchema =
    "logos.managed_node_lifecycle.ack";
constexpr std::string_view kLifecycleEventSchema =
    "logos.managed_node_lifecycle.event";
constexpr std::string_view kDownloadProtocol = "logos.storage.download";
constexpr std::uint32_t kLifecycleVersion = 1U;
constexpr std::uint32_t kDownloadVersion = 2U;
constexpr std::size_t kMaximumNodeActionBytes = 65536U;
constexpr std::size_t kMaximumSnapshotBytes = 65536U;
constexpr std::size_t kMaximumAcknowledgementBytes = 16384U;
constexpr std::size_t kMaximumNodeChangedBytes = 131072U;
constexpr std::size_t kMaximumTransferEventBytes = 16384U;
constexpr std::size_t kMaximumInitializationConfigBytes = 49152U;
constexpr std::size_t kMaximumIdentifierBytes = 128U;
constexpr std::size_t kMaximumErrorBytes = 4096U;
constexpr std::size_t kMaximumJsonDepth = 8U;
constexpr std::size_t kMaximumJsonNodes = 128U;

bool isAsciiDigit(const char value)
{
    return value >= '0' && value <= '9';
}

bool isPrintableIdentifier(const std::string& value)
{
    return !value.empty() && value.size() <= kMaximumIdentifierBytes
        && std::all_of(
            value.begin(),
            value.end(),
            [](const unsigned char character) {
                return character >= 0x21U && character <= 0x7eU;
            });
}

bool isErrorCode(const std::string& value)
{
    return !value.empty() && value.size() <= kMaximumIdentifierBytes
        && std::all_of(
            value.begin(),
            value.end(),
            [](const char character) {
                return (character >= 'a' && character <= 'z')
                    || (character >= 'A' && character <= 'Z')
                    || (character >= '0' && character <= '9')
                    || character == '_' || character == '-'
                    || character == '.';
            });
}

bool decodeUtf8CodePoint(
    const std::string& value,
    std::size_t& cursor,
    std::uint32_t& codePoint)
{
    if (cursor >= value.size())
        return false;
    const auto first = static_cast<std::uint8_t>(value[cursor]);
    if (first <= 0x7fU) {
        codePoint = first;
        ++cursor;
        return true;
    }

    std::size_t continuationCount = 0U;
    std::uint32_t minimum = 0U;
    if (first >= 0xc2U && first <= 0xdfU) {
        continuationCount = 1U;
        codePoint = first & 0x1fU;
        minimum = 0x80U;
    } else if (first >= 0xe0U && first <= 0xefU) {
        continuationCount = 2U;
        codePoint = first & 0x0fU;
        minimum = 0x800U;
    } else if (first >= 0xf0U && first <= 0xf4U) {
        continuationCount = 3U;
        codePoint = first & 0x07U;
        minimum = 0x10000U;
    } else {
        return false;
    }
    if (continuationCount > value.size() - cursor - 1U)
        return false;
    for (std::size_t index = 0U; index < continuationCount; ++index) {
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

bool isValidUtf8(const std::string& value)
{
    std::size_t cursor = 0U;
    while (cursor < value.size()) {
        std::uint32_t codePoint = 0U;
        if (!decodeUtf8CodePoint(value, cursor, codePoint))
            return false;
    }
    return true;
}

bool appendUtf8(std::string& output, const std::uint32_t codePoint)
{
    if (codePoint <= 0x7fU) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (codePoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else if (codePoint <= 0xffffU) {
        if (codePoint >= 0xd800U && codePoint <= 0xdfffU)
            return false;
        output.push_back(static_cast<char>(0xe0U | (codePoint >> 12U)));
        output.push_back(
            static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else if (codePoint <= 0x10ffffU) {
        output.push_back(static_cast<char>(0xf0U | (codePoint >> 18U)));
        output.push_back(
            static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3fU)));
        output.push_back(
            static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else {
        return false;
    }
    return true;
}

enum class JsonType : std::uint8_t {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object,
};

struct JsonValue {
    JsonType type = JsonType::Null;
    bool boolean = false;
    std::string text;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;
};

class StrictJsonParser {
public:
    explicit StrictJsonParser(const std::string& input)
        : input_(input)
    {
    }

    bool parse(JsonValue& output, std::string& reason)
    {
        if (!isValidUtf8(input_)) {
            reason = "invalid-json-utf8";
            return false;
        }
        skipWhitespace();
        if (!parseValue(0U, output)) {
            reason = reason_.empty() ? "invalid-json" : reason_;
            return false;
        }
        skipWhitespace();
        if (cursor_ != input_.size()) {
            reason = "trailing-json-data";
            return false;
        }
        return true;
    }

private:
    bool fail(const char* reason)
    {
        if (reason_.empty())
            reason_ = reason;
        return false;
    }

    void skipWhitespace()
    {
        while (cursor_ < input_.size()) {
            const char value = input_[cursor_];
            if (value != ' ' && value != '\t'
                && value != '\r' && value != '\n') {
                break;
            }
            ++cursor_;
        }
    }

    bool parseValue(const std::size_t depth, JsonValue& output)
    {
        if (depth > kMaximumJsonDepth)
            return fail("json-too-deep");
        if (++nodes_ > kMaximumJsonNodes)
            return fail("json-too-many-nodes");
        skipWhitespace();
        if (cursor_ >= input_.size())
            return fail("truncated-json");
        switch (input_[cursor_]) {
        case 'n':
            output.type = JsonType::Null;
            return parseLiteral("null");
        case 't':
            output.type = JsonType::Boolean;
            output.boolean = true;
            return parseLiteral("true");
        case 'f':
            output.type = JsonType::Boolean;
            output.boolean = false;
            return parseLiteral("false");
        case '"':
            output.type = JsonType::String;
            return parseString(output.text);
        case '[':
            output.type = JsonType::Array;
            return parseArray(depth, output.array);
        case '{':
            output.type = JsonType::Object;
            return parseObject(depth, output.object);
        default:
            output.type = JsonType::Number;
            return parseNumber(output.text);
        }
    }

    bool parseLiteral(const std::string_view literal)
    {
        if (literal.size() > input_.size() - cursor_
            || input_.compare(cursor_, literal.size(), literal) != 0) {
            return fail("invalid-json-literal");
        }
        cursor_ += literal.size();
        return true;
    }

    bool parseHex4(std::uint32_t& output)
    {
        if (4U > input_.size() - cursor_)
            return false;
        output = 0U;
        for (std::size_t index = 0U; index < 4U; ++index) {
            const char character = input_[cursor_++];
            output <<= 4U;
            if (character >= '0' && character <= '9')
                output |= static_cast<std::uint32_t>(character - '0');
            else if (character >= 'a' && character <= 'f')
                output |= static_cast<std::uint32_t>(character - 'a' + 10);
            else if (character >= 'A' && character <= 'F')
                output |= static_cast<std::uint32_t>(character - 'A' + 10);
            else
                return false;
        }
        return true;
    }

    bool parseString(std::string& output)
    {
        if (input_[cursor_] != '"')
            return fail("expected-json-string");
        ++cursor_;
        output.clear();
        while (cursor_ < input_.size()) {
            const auto character =
                static_cast<std::uint8_t>(input_[cursor_++]);
            if (character == '"')
                return isValidUtf8(output)
                    || fail("invalid-json-string-utf8");
            if (character < 0x20U)
                return fail("unescaped-json-control");
            if (character != '\\') {
                output.push_back(static_cast<char>(character));
                continue;
            }
            if (cursor_ >= input_.size())
                return fail("truncated-json-escape");
            const char escaped = input_[cursor_++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                output.push_back(escaped);
                break;
            case 'b':
                output.push_back('\b');
                break;
            case 'f':
                output.push_back('\f');
                break;
            case 'n':
                output.push_back('\n');
                break;
            case 'r':
                output.push_back('\r');
                break;
            case 't':
                output.push_back('\t');
                break;
            case 'u': {
                std::uint32_t first = 0U;
                if (!parseHex4(first))
                    return fail("invalid-json-unicode-escape");
                std::uint32_t codePoint = first;
                if (first >= 0xd800U && first <= 0xdbffU) {
                    if (6U > input_.size() - cursor_
                        || input_[cursor_] != '\\'
                        || input_[cursor_ + 1U] != 'u') {
                        return fail("invalid-json-surrogate-pair");
                    }
                    cursor_ += 2U;
                    std::uint32_t second = 0U;
                    if (!parseHex4(second)
                        || second < 0xdc00U || second > 0xdfffU) {
                        return fail("invalid-json-surrogate-pair");
                    }
                    codePoint = 0x10000U
                        + ((first - 0xd800U) << 10U)
                        + (second - 0xdc00U);
                } else if (first >= 0xdc00U && first <= 0xdfffU) {
                    return fail("invalid-json-surrogate-pair");
                }
                if (!appendUtf8(output, codePoint))
                    return fail("invalid-json-code-point");
                break;
            }
            default:
                return fail("invalid-json-escape");
            }
        }
        return fail("unterminated-json-string");
    }

    bool parseNumber(std::string& output)
    {
        const std::size_t start = cursor_;
        if (input_[cursor_] == '-')
            ++cursor_;
        if (cursor_ >= input_.size())
            return fail("invalid-json-number");
        if (input_[cursor_] == '0') {
            ++cursor_;
            if (cursor_ < input_.size() && isAsciiDigit(input_[cursor_]))
                return fail("noncanonical-json-number");
        } else if (input_[cursor_] >= '1' && input_[cursor_] <= '9') {
            while (cursor_ < input_.size() && isAsciiDigit(input_[cursor_]))
                ++cursor_;
        } else {
            return fail("invalid-json-number");
        }
        if (cursor_ < input_.size() && input_[cursor_] == '.') {
            ++cursor_;
            if (cursor_ >= input_.size() || !isAsciiDigit(input_[cursor_]))
                return fail("invalid-json-number");
            while (cursor_ < input_.size() && isAsciiDigit(input_[cursor_]))
                ++cursor_;
        }
        if (cursor_ < input_.size()
            && (input_[cursor_] == 'e' || input_[cursor_] == 'E')) {
            ++cursor_;
            if (cursor_ < input_.size()
                && (input_[cursor_] == '+' || input_[cursor_] == '-')) {
                ++cursor_;
            }
            if (cursor_ >= input_.size() || !isAsciiDigit(input_[cursor_]))
                return fail("invalid-json-number");
            while (cursor_ < input_.size() && isAsciiDigit(input_[cursor_]))
                ++cursor_;
        }
        output.assign(input_, start, cursor_ - start);
        if (output.size() > 20U)
            return fail("json-number-too-large");
        return true;
    }

    bool parseArray(
        const std::size_t depth,
        std::vector<JsonValue>& output)
    {
        ++cursor_;
        skipWhitespace();
        if (cursor_ < input_.size() && input_[cursor_] == ']') {
            ++cursor_;
            return true;
        }
        while (cursor_ < input_.size()) {
            JsonValue value;
            if (!parseValue(depth + 1U, value))
                return false;
            output.push_back(std::move(value));
            skipWhitespace();
            if (cursor_ >= input_.size())
                return fail("unterminated-json-array");
            if (input_[cursor_] == ']') {
                ++cursor_;
                return true;
            }
            if (input_[cursor_] != ',')
                return fail("invalid-json-array-separator");
            ++cursor_;
            skipWhitespace();
        }
        return fail("unterminated-json-array");
    }

    bool parseObject(
        const std::size_t depth,
        std::vector<std::pair<std::string, JsonValue>>& output)
    {
        ++cursor_;
        skipWhitespace();
        if (cursor_ < input_.size() && input_[cursor_] == '}') {
            ++cursor_;
            return true;
        }
        std::set<std::string> keys;
        while (cursor_ < input_.size()) {
            if (input_[cursor_] != '"')
                return fail("invalid-json-object-key");
            std::string key;
            if (!parseString(key))
                return false;
            if (!keys.insert(key).second)
                return fail("duplicate-json-key");
            skipWhitespace();
            if (cursor_ >= input_.size() || input_[cursor_] != ':')
                return fail("invalid-json-object-separator");
            ++cursor_;
            JsonValue value;
            if (!parseValue(depth + 1U, value))
                return false;
            output.emplace_back(std::move(key), std::move(value));
            skipWhitespace();
            if (cursor_ >= input_.size())
                return fail("unterminated-json-object");
            if (input_[cursor_] == '}') {
                ++cursor_;
                return true;
            }
            if (input_[cursor_] != ',')
                return fail("invalid-json-object-separator");
            ++cursor_;
            skipWhitespace();
        }
        return fail("unterminated-json-object");
    }

    const std::string& input_;
    std::size_t cursor_ = 0U;
    std::size_t nodes_ = 0U;
    std::string reason_;
};

const JsonValue* member(const JsonValue& object, const std::string_view key)
{
    if (object.type != JsonType::Object)
        return nullptr;
    const auto found = std::find_if(
        object.object.begin(),
        object.object.end(),
        [&](const auto& entry) { return entry.first == key; });
    return found == object.object.end() ? nullptr : &found->second;
}

bool exactKeys(
    const JsonValue& object,
    const std::initializer_list<std::string_view> expected)
{
    if (object.type != JsonType::Object
        || object.object.size() != expected.size()) {
        return false;
    }
    return std::all_of(expected.begin(), expected.end(), [&](const auto key) {
        return member(object, key) != nullptr;
    });
}

bool jsonUnsigned64(const JsonValue& value, std::uint64_t& output)
{
    if (value.type != JsonType::Number || value.text.empty()
        || value.text.front() == '-'
        || value.text.find_first_of(".eE") != std::string::npos) {
        return false;
    }
    const auto parsed = std::from_chars(
        value.text.data(),
        value.text.data() + value.text.size(),
        output);
    return parsed.ec == std::errc{}
        && parsed.ptr == value.text.data() + value.text.size();
}

bool stringValue(
    const JsonValue& value,
    std::string& output,
    const std::size_t maximumBytes,
    const bool requireNonempty)
{
    if (value.type != JsonType::String
        || value.text.size() > maximumBytes
        || (requireNonempty && value.text.empty())
        || value.text.find('\0') != std::string::npos) {
        return false;
    }
    output = value.text;
    return true;
}

bool booleanValue(const JsonValue& value, bool& output)
{
    if (value.type != JsonType::Boolean)
        return false;
    output = value.boolean;
    return true;
}

bool parseDocument(
    const std::string& payload,
    const std::size_t maximumBytes,
    JsonValue& output,
    std::string& reason)
{
    if (payload.empty() || payload.size() > maximumBytes) {
        reason = payload.empty() ? "empty-payload" : "payload-too-large";
        return false;
    }
    StrictJsonParser parser(payload);
    return parser.parse(output, reason);
}

bool parseAction(
    const JsonValue& value,
    StorageLifecycleAction& output)
{
    if (value.type != JsonType::String)
        return false;
    if (value.text == "initialize") {
        output = StorageLifecycleAction::Initialize;
    } else if (value.text == "start") {
        output = StorageLifecycleAction::Start;
    } else if (value.text == "stop") {
        output = StorageLifecycleAction::Stop;
    } else if (value.text == "destroy") {
        output = StorageLifecycleAction::Destroy;
    } else {
        return false;
    }
    return true;
}

std::string_view actionName(const StorageLifecycleAction action)
{
    switch (action) {
    case StorageLifecycleAction::Initialize:
        return "initialize";
    case StorageLifecycleAction::Start:
        return "start";
    case StorageLifecycleAction::Stop:
        return "stop";
    case StorageLifecycleAction::Destroy:
        return "destroy";
    }
    return {};
}

bool parseState(const JsonValue& value, StorageNodeState& output)
{
    if (value.type != JsonType::String)
        return false;
    if (value.text == "uninitialized") {
        output = StorageNodeState::Uninitialized;
    } else if (value.text == "initializing") {
        output = StorageNodeState::Initializing;
    } else if (value.text == "stopped") {
        output = StorageNodeState::Stopped;
    } else if (value.text == "starting") {
        output = StorageNodeState::Starting;
    } else if (value.text == "running") {
        output = StorageNodeState::Running;
    } else if (value.text == "stopping") {
        output = StorageNodeState::Stopping;
    } else if (value.text == "destroying") {
        output = StorageNodeState::Destroying;
    } else {
        return false;
    }
    return true;
}

std::vector<StorageLifecycleAction> expectedActions(
    const StorageNodeState state)
{
    switch (state) {
    case StorageNodeState::Uninitialized:
        return {StorageLifecycleAction::Initialize};
    case StorageNodeState::Stopped:
        return {
            StorageLifecycleAction::Start,
            StorageLifecycleAction::Destroy,
        };
    case StorageNodeState::Starting:
    case StorageNodeState::Running:
        return {StorageLifecycleAction::Stop};
    case StorageNodeState::Initializing:
    case StorageNodeState::Stopping:
    case StorageNodeState::Destroying:
        return {};
    }
    return {};
}

bool pendingActionMatchesState(
    const StorageLifecycleAction action,
    const StorageNodeState state)
{
    return (action == StorageLifecycleAction::Initialize
               && state == StorageNodeState::Initializing)
        || (action == StorageLifecycleAction::Start
            && state == StorageNodeState::Starting)
        || (action == StorageLifecycleAction::Stop
            && state == StorageNodeState::Stopping)
        || (action == StorageLifecycleAction::Destroy
            && state == StorageNodeState::Destroying);
}

bool validCompletedOutcome(const std::string& outcome)
{
    return outcome == "succeeded" || outcome == "failed"
        || outcome == "no_op" || outcome == "rejected";
}

struct ParsedError {
    bool present = false;
    std::string code;
};

bool parseError(const JsonValue& value, ParsedError& output)
{
    output = {};
    if (value.type == JsonType::Null)
        return true;
    if (!exactKeys(value, {"code", "message", "occurred_at_ms"}))
        return false;
    const JsonValue* code = member(value, "code");
    const JsonValue* message = member(value, "message");
    const JsonValue* occurredAt = member(value, "occurred_at_ms");
    std::string parsedMessage;
    std::uint64_t parsedTime = 0U;
    if (code->type != JsonType::String || !isErrorCode(code->text)
        || !stringValue(
            *message,
            parsedMessage,
            kMaximumErrorBytes,
            true)
        || !jsonUnsigned64(*occurredAt, parsedTime)
        || parsedTime
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
        return false;
    }
    output.present = true;
    output.code = code->text;
    return true;
}

bool parseSnapshotValue(
    const JsonValue& value,
    StorageNodeSnapshotV1& output)
{
    if (!exactKeys(
            value,
            {
                "schema",
                "version",
                "instance_id",
                "epoch",
                "sequence",
                "scope",
                "state",
                "health",
                "supported_actions",
                "pending_operation",
                "last_completed_operation",
                "last_error",
                "updated_at_ms",
            })) {
        return false;
    }

    const JsonValue* schema = member(value, "schema");
    const JsonValue* version = member(value, "version");
    const JsonValue* instanceId = member(value, "instance_id");
    const JsonValue* epoch = member(value, "epoch");
    const JsonValue* sequence = member(value, "sequence");
    const JsonValue* scope = member(value, "scope");
    const JsonValue* state = member(value, "state");
    const JsonValue* health = member(value, "health");
    const JsonValue* supportedActions = member(value, "supported_actions");
    const JsonValue* pendingOperation = member(value, "pending_operation");
    const JsonValue* lastCompletedOperation =
        member(value, "last_completed_operation");
    const JsonValue* lastError = member(value, "last_error");
    const JsonValue* updatedAt = member(value, "updated_at_ms");

    StorageNodeSnapshotV1 parsed;
    std::uint64_t parsedVersion = 0U;
    std::uint64_t parsedUpdatedAt = 0U;
    if (schema->type != JsonType::String
        || schema->text != kLifecycleSnapshotSchema
        || !jsonUnsigned64(*version, parsedVersion)
        || parsedVersion != kLifecycleVersion
        || instanceId->type != JsonType::String
        || !isPrintableIdentifier(instanceId->text)
        || !jsonUnsigned64(*epoch, parsed.epoch)
        || !jsonUnsigned64(*sequence, parsed.sequence)
        || !exactKeys(*scope, {"kind"})
        || member(*scope, "kind")->type != JsonType::String
        || member(*scope, "kind")->text != "storage"
        || !parseState(*state, parsed.state)
        || health->type != JsonType::String
        || (health->text != "unknown" && health->text != "degraded")
        || supportedActions->type != JsonType::Array
        || supportedActions->array.size() > 2U
        || !jsonUnsigned64(*updatedAt, parsedUpdatedAt)
        || parsedUpdatedAt
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
        return false;
    }

    for (const JsonValue& actionValue : supportedActions->array) {
        StorageLifecycleAction action = StorageLifecycleAction::Initialize;
        if (!parseAction(actionValue, action))
            return false;
        parsed.supportedActions.push_back(action);
    }
    if (parsed.supportedActions != expectedActions(parsed.state))
        return false;

    if (pendingOperation->type != JsonType::Null) {
        if (!exactKeys(*pendingOperation, {"operation_id", "action"}))
            return false;
        const JsonValue* operationId =
            member(*pendingOperation, "operation_id");
        const JsonValue* action = member(*pendingOperation, "action");
        StorageNodePendingOperationV1 pending;
        if (operationId->type != JsonType::String
            || !isPrintableIdentifier(operationId->text)
            || !parseAction(*action, pending.action)
            || !pendingActionMatchesState(pending.action, parsed.state)) {
            return false;
        }
        pending.operationId = operationId->text;
        parsed.pendingOperation = std::move(pending);
    }

    if (lastCompletedOperation->type != JsonType::Null) {
        if (!exactKeys(
                *lastCompletedOperation,
                {"operation_id", "action", "outcome"})) {
            return false;
        }
        const JsonValue* operationId =
            member(*lastCompletedOperation, "operation_id");
        const JsonValue* action =
            member(*lastCompletedOperation, "action");
        const JsonValue* outcome =
            member(*lastCompletedOperation, "outcome");
        StorageNodeCompletedOperationV1 completed;
        if (operationId->type != JsonType::String
            || !isPrintableIdentifier(operationId->text)
            || !parseAction(*action, completed.action)
            || outcome->type != JsonType::String
            || !validCompletedOutcome(outcome->text)) {
            return false;
        }
        completed.operationId = operationId->text;
        completed.outcome = outcome->text;
        parsed.lastCompletedOperation = std::move(completed);
    }

    ParsedError parsedError;
    if (!parseError(*lastError, parsedError)
        || (health->text == "unknown" && parsedError.present)
        || (health->text == "degraded" && !parsedError.present)) {
        return false;
    }

    parsed.schema = schema->text;
    parsed.version = static_cast<std::uint32_t>(parsedVersion);
    parsed.instanceId = instanceId->text;
    parsed.scopeKind = member(*scope, "kind")->text;
    output = std::move(parsed);
    return true;
}

void appendQuoted(std::string& output, const std::string& value)
{
    static constexpr char hex[] = "0123456789abcdef";
    output.push_back('"');
    for (const char encodedCharacter : value) {
        const auto character =
            static_cast<unsigned char>(encodedCharacter);
        switch (character) {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (character < 0x20U) {
                output += "\\u00";
                output.push_back(hex[character >> 4U]);
                output.push_back(hex[character & 0x0fU]);
            } else {
                output.push_back(static_cast<char>(character));
            }
        }
    }
    output.push_back('"');
}

StorageModuleCodecResult rejected(std::string reason)
{
    return {false, std::move(reason)};
}

StorageModuleCodecResult accepted()
{
    return {true, {}};
}

} // namespace

StorageModuleEncodedCommand encodeStorageModuleNodeAction(
    const StorageModuleCommand& command)
{
    if (command.kind != StorageModuleCommandKind::NodeAction)
        return {false, "not-node-action-command", {}};
    const std::string_view action = actionName(command.lifecycleAction);
    if (action.empty()
        || !isPrintableIdentifier(command.lifecycleOperationId)
        || !isPrintableIdentifier(command.expectedInstanceId)
        || command.initializationConfig.size()
            > kMaximumInitializationConfigBytes
        || command.initializationConfig.find('\0') != std::string::npos
        || !isValidUtf8(command.initializationConfig)
        || (command.lifecycleAction == StorageLifecycleAction::Initialize
                ? command.initializationConfig.empty()
                : !command.initializationConfig.empty())) {
        return {false, "invalid-node-action-command", {}};
    }

    std::string payload;
    payload.reserve(command.initializationConfig.size() + 320U);
    payload += "{\"schema\":\"";
    payload += kLifecycleCommandSchema;
    payload += "\",\"version\":1,\"operation_id\":";
    appendQuoted(payload, command.lifecycleOperationId);
    payload += ",\"action\":\"";
    payload += action;
    payload += "\",\"expected\":{\"instance_id\":";
    appendQuoted(payload, command.expectedInstanceId);
    payload += ",\"epoch\":";
    payload += std::to_string(command.expectedEpoch);
    payload += ",\"sequence\":";
    payload += std::to_string(command.expectedSequence);
    payload += "},\"parameters\":{";
    if (command.lifecycleAction == StorageLifecycleAction::Initialize) {
        payload += "\"config\":";
        appendQuoted(payload, command.initializationConfig);
    }
    payload += "}}";
    if (payload.size() > kMaximumNodeActionBytes)
        return {false, "node-action-payload-too-large", {}};
    return {true, {}, std::move(payload)};
}

StorageModuleCodecResult parseStorageModuleNodeStatus(
    const std::string& payload,
    StorageNodeSnapshotV1& snapshot)
{
    JsonValue document;
    std::string reason;
    if (!parseDocument(
            payload,
            kMaximumSnapshotBytes,
            document,
            reason)) {
        return rejected(std::move(reason));
    }
    StorageNodeSnapshotV1 parsed;
    if (!parseSnapshotValue(document, parsed))
        return rejected("invalid-node-status");
    snapshot = std::move(parsed);
    return accepted();
}

StorageModuleCodecResult parseStorageModuleNodeActionAcknowledgement(
    const std::string& payload,
    StorageNodeActionAcknowledgementV1& acknowledgement)
{
    JsonValue document;
    std::string reason;
    if (!parseDocument(
            payload,
            kMaximumAcknowledgementBytes,
            document,
            reason)) {
        return rejected(std::move(reason));
    }
    if (!exactKeys(
            document,
            {
                "schema",
                "version",
                "operation_id",
                "accepted",
                "duplicate",
                "instance_id",
                "epoch",
                "sequence",
                "state",
                "error",
            })) {
        return rejected("invalid-node-action-acknowledgement");
    }

    const JsonValue* schema = member(document, "schema");
    const JsonValue* version = member(document, "version");
    const JsonValue* operationId = member(document, "operation_id");
    const JsonValue* acceptedValue = member(document, "accepted");
    const JsonValue* duplicate = member(document, "duplicate");
    const JsonValue* instanceId = member(document, "instance_id");
    const JsonValue* epoch = member(document, "epoch");
    const JsonValue* sequence = member(document, "sequence");
    const JsonValue* state = member(document, "state");
    const JsonValue* error = member(document, "error");

    StorageNodeActionAcknowledgementV1 parsed;
    std::uint64_t parsedVersion = 0U;
    ParsedError parsedError;
    if (schema->type != JsonType::String
        || schema->text != kLifecycleAcknowledgementSchema
        || !jsonUnsigned64(*version, parsedVersion)
        || parsedVersion != kLifecycleVersion
        || operationId->type != JsonType::String
        || !isPrintableIdentifier(operationId->text)
        || !booleanValue(*acceptedValue, parsed.accepted)
        || !booleanValue(*duplicate, parsed.duplicate)
        || instanceId->type != JsonType::String
        || !isPrintableIdentifier(instanceId->text)
        || !jsonUnsigned64(*epoch, parsed.epoch)
        || !jsonUnsigned64(*sequence, parsed.sequence)
        || !parseState(*state, parsed.state)
        || !parseError(*error, parsedError)
        || (parsed.accepted == parsedError.present)) {
        return rejected("invalid-node-action-acknowledgement");
    }
    parsed.schema = schema->text;
    parsed.version = static_cast<std::uint32_t>(parsedVersion);
    parsed.operationId = operationId->text;
    parsed.instanceId = instanceId->text;
    parsed.errorCode = parsedError.code;
    acknowledgement = std::move(parsed);
    return accepted();
}

StorageModuleCodecResult parseStorageModuleNodeChanged(
    const std::string& payload,
    StorageNodeChangedV1& event)
{
    JsonValue document;
    std::string reason;
    if (!parseDocument(
            payload,
            kMaximumNodeChangedBytes,
            document,
            reason)) {
        return rejected(std::move(reason));
    }
    if (!exactKeys(
            document,
            {
                "schema",
                "version",
                "instance_id",
                "epoch",
                "sequence",
                "scope",
                "operation_id",
                "action",
                "phase",
                "outcome",
                "previous_state",
                "status",
                "error",
                "emitted_at_ms",
            })) {
        return rejected("invalid-node-changed-event");
    }

    const JsonValue* schema = member(document, "schema");
    const JsonValue* version = member(document, "version");
    const JsonValue* instanceId = member(document, "instance_id");
    const JsonValue* epoch = member(document, "epoch");
    const JsonValue* sequence = member(document, "sequence");
    const JsonValue* scope = member(document, "scope");
    const JsonValue* operationId = member(document, "operation_id");
    const JsonValue* action = member(document, "action");
    const JsonValue* phase = member(document, "phase");
    const JsonValue* outcome = member(document, "outcome");
    const JsonValue* previousState = member(document, "previous_state");
    const JsonValue* status = member(document, "status");
    const JsonValue* error = member(document, "error");
    const JsonValue* emittedAt = member(document, "emitted_at_ms");

    StorageNodeChangedV1 parsed;
    std::uint64_t parsedVersion = 0U;
    std::uint64_t parsedEmittedAt = 0U;
    ParsedError parsedError;
    if (schema->type != JsonType::String
        || schema->text != kLifecycleEventSchema
        || !jsonUnsigned64(*version, parsedVersion)
        || parsedVersion != kLifecycleVersion
        || instanceId->type != JsonType::String
        || !isPrintableIdentifier(instanceId->text)
        || !jsonUnsigned64(*epoch, parsed.epoch)
        || !jsonUnsigned64(*sequence, parsed.sequence)
        || !exactKeys(*scope, {"kind"})
        || member(*scope, "kind")->type != JsonType::String
        || member(*scope, "kind")->text != "storage"
        || !parseAction(*action, parsed.action)
        || phase->type != JsonType::String
        || outcome->type != JsonType::String
        || !parseState(*previousState, parsed.previousState)
        || !parseSnapshotValue(*status, parsed.status)
        || !parseError(*error, parsedError)
        || !jsonUnsigned64(*emittedAt, parsedEmittedAt)
        || parsedEmittedAt
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
        return rejected("invalid-node-changed-event");
    }
    if (operationId->type == JsonType::String) {
        if (!isPrintableIdentifier(operationId->text))
            return rejected("invalid-node-changed-event");
        parsed.operationId = operationId->text;
    } else if (operationId->type != JsonType::Null) {
        return rejected("invalid-node-changed-event");
    }

    parsed.schema = schema->text;
    parsed.version = static_cast<std::uint32_t>(parsedVersion);
    parsed.instanceId = instanceId->text;
    parsed.scopeKind = member(*scope, "kind")->text;
    parsed.phase = phase->text;
    parsed.outcome = outcome->text;
    if (parsed.instanceId != parsed.status.instanceId
        || parsed.epoch != parsed.status.epoch
        || parsed.sequence != parsed.status.sequence) {
        return rejected("node-changed-status-mismatch");
    }
    if (parsed.phase == "accepted") {
        if (parsed.outcome != "accepted" || parsedError.present)
            return rejected("invalid-node-changed-event");
        if (!parsed.operationId.empty()
            && (!parsed.status.pendingOperation.has_value()
                || parsed.status.pendingOperation->operationId
                    != parsed.operationId
                || parsed.status.pendingOperation->action
                    != parsed.action)) {
            return rejected("node-changed-operation-mismatch");
        }
    } else if (parsed.phase == "settled") {
        if (!validCompletedOutcome(parsed.outcome)
            || ((parsed.outcome == "succeeded"
                    || parsed.outcome == "no_op")
                ? parsedError.present
                : !parsedError.present)) {
            return rejected("invalid-node-changed-event");
        }
        if (!parsed.operationId.empty()
            && (!parsed.status.lastCompletedOperation.has_value()
                || parsed.status.lastCompletedOperation->operationId
                    != parsed.operationId
                || parsed.status.lastCompletedOperation->action
                    != parsed.action
                || parsed.status.lastCompletedOperation->outcome
                    != parsed.outcome)) {
            return rejected("node-changed-operation-mismatch");
        }
    } else {
        return rejected("invalid-node-changed-event");
    }

    parsed.errorCode = parsedError.code;
    event = std::move(parsed);
    return accepted();
}

StorageModuleCodecResult parseStorageModuleUploadDone(
    const std::string& payload,
    StorageUploadDoneV1& event)
{
    JsonValue document;
    std::string reason;
    if (!parseDocument(
            payload,
            kMaximumTransferEventBytes,
            document,
            reason)) {
        return rejected(std::move(reason));
    }
    const JsonValue* success = member(document, "success");
    const JsonValue* sessionId = member(document, "sessionId");
    bool succeeded = false;
    if (success == nullptr || sessionId == nullptr
        || !booleanValue(*success, succeeded)
        || sessionId->type != JsonType::String
        || !isPrintableIdentifier(sessionId->text)) {
        return rejected("invalid-upload-done-event");
    }

    StorageUploadDoneV1 parsed;
    parsed.succeeded = succeeded;
    parsed.moduleSessionId = sessionId->text;
    if (parsed.succeeded) {
        if (!exactKeys(document, {"success", "sessionId", "cid"}))
            return rejected("invalid-upload-done-event");
        const JsonValue* cid = member(document, "cid");
        if (cid->type != JsonType::String
            || !isCanonicalStorageCid(cid->text))
            return rejected("invalid-upload-done-event");
        parsed.cid = cid->text;
    } else {
        if (!exactKeys(document, {"success", "sessionId", "error"}))
            return rejected("invalid-upload-done-event");
        const JsonValue* error = member(document, "error");
        if (!stringValue(
                *error,
                parsed.error,
                kMaximumErrorBytes,
                true)) {
            return rejected("invalid-upload-done-event");
        }
    }
    event = std::move(parsed);
    return accepted();
}

StorageModuleCodecResult parseStorageModuleDownloadAcknowledgementV2(
    const std::string& payload,
    StorageDownloadAcknowledgementV2& acknowledgement)
{
    JsonValue document;
    std::string reason;
    if (!parseDocument(
            payload,
            kMaximumAcknowledgementBytes,
            document,
            reason)) {
        return rejected(std::move(reason));
    }
    if (!exactKeys(
            document,
            {
                "protocol",
                "version",
                "accepted",
                "moduleOperationId",
                "cid",
            })) {
        return rejected("invalid-download-acknowledgement");
    }
    const JsonValue* protocol = member(document, "protocol");
    const JsonValue* version = member(document, "version");
    const JsonValue* acceptedValue = member(document, "accepted");
    const JsonValue* operationId =
        member(document, "moduleOperationId");
    const JsonValue* cid = member(document, "cid");
    StorageDownloadAcknowledgementV2 parsed;
    std::uint64_t parsedVersion = 0U;
    if (protocol->type != JsonType::String
        || protocol->text != kDownloadProtocol
        || !jsonUnsigned64(*version, parsedVersion)
        || parsedVersion != kDownloadVersion
        || !booleanValue(*acceptedValue, parsed.accepted)
        || !parsed.accepted
        || operationId->type != JsonType::String
        || !isPrintableIdentifier(operationId->text)
        || cid->type != JsonType::String
        || !isCanonicalStorageCid(cid->text)) {
        return rejected("invalid-download-acknowledgement");
    }
    parsed.protocol = protocol->text;
    parsed.version = static_cast<std::uint32_t>(parsedVersion);
    parsed.moduleOperationId = operationId->text;
    parsed.cid = cid->text;
    acknowledgement = std::move(parsed);
    return accepted();
}

StorageModuleCodecResult parseStorageModuleDownloadDoneV2(
    const std::string& payload,
    StorageDownloadDoneV2& event)
{
    JsonValue document;
    std::string reason;
    if (!parseDocument(
            payload,
            kMaximumTransferEventBytes,
            document,
            reason)) {
        return rejected(std::move(reason));
    }
    const JsonValue* outcome = member(document, "outcome");
    if (outcome == nullptr || outcome->type != JsonType::String)
        return rejected("invalid-download-done-event");
    const bool failed = outcome->text == "failed";
    if (!exactKeys(
            document,
            failed
                ? std::initializer_list<std::string_view>{
                    "protocol",
                    "version",
                    "moduleOperationId",
                    "cid",
                    "outcome",
                    "error",
                }
                : std::initializer_list<std::string_view>{
                    "protocol",
                    "version",
                    "moduleOperationId",
                    "cid",
                    "outcome",
                })) {
        return rejected("invalid-download-done-event");
    }
    const JsonValue* protocol = member(document, "protocol");
    const JsonValue* version = member(document, "version");
    const JsonValue* operationId =
        member(document, "moduleOperationId");
    const JsonValue* cid = member(document, "cid");
    StorageDownloadDoneV2 parsed;
    std::uint64_t parsedVersion = 0U;
    if (protocol->type != JsonType::String
        || protocol->text != kDownloadProtocol
        || !jsonUnsigned64(*version, parsedVersion)
        || parsedVersion != kDownloadVersion
        || operationId->type != JsonType::String
        || !isPrintableIdentifier(operationId->text)
        || cid->type != JsonType::String
        || !isCanonicalStorageCid(cid->text)) {
        return rejected("invalid-download-done-event");
    }
    if (outcome->text == "succeeded") {
        parsed.outcome = StorageTransferOutcome::Succeeded;
    } else if (outcome->text == "failed") {
        parsed.outcome = StorageTransferOutcome::Failed;
    } else if (outcome->text == "canceled") {
        parsed.outcome = StorageTransferOutcome::Canceled;
    } else {
        return rejected("invalid-download-done-event");
    }
    if (failed) {
        if (!stringValue(
                *member(document, "error"),
                parsed.error,
                kMaximumErrorBytes,
                true)) {
            return rejected("invalid-download-done-event");
        }
    }
    parsed.protocol = protocol->text;
    parsed.version = static_cast<std::uint32_t>(parsedVersion);
    parsed.moduleOperationId = operationId->text;
    parsed.cid = cid->text;
    event = std::move(parsed);
    return accepted();
}

} // namespace palace
