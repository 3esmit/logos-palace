#include "palace_lez_explorer_finality.h"

#include "palace_lez_explorer_protocol.h"
#include "palace_sha256.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

namespace palace {
namespace {

constexpr std::size_t kAbsoluteMaxPageSize = 100U;
constexpr std::size_t kAbsoluteMaxPages = 64U;
constexpr std::size_t kAbsoluteMaxBlocks = 6400U;
constexpr std::size_t kAbsoluteMaxTransactionsPerBlock = 256U;
constexpr std::size_t kAbsoluteMaxAccounts = 16U;
constexpr std::size_t kAbsoluteMaxInstructionWords = 4096U;
constexpr std::size_t kAbsoluteMaxJsonBytes = 4U * 1024U * 1024U;
constexpr std::size_t kAbsoluteMaxAccountDataBytes = 1024U * 1024U;
constexpr std::size_t kAbsoluteMaxJsonDepth = 32U;
constexpr std::size_t kAbsoluteMaxJsonNodes = 400000U;
constexpr std::size_t kMaximumNetworkIdBytes = 64U;
constexpr std::size_t kMaximumOriginBytes = 256U;
constexpr std::size_t kMaximumSuffixBytes = 32U;
constexpr std::string_view kJsonContentType = "application/json";
constexpr std::string_view kFormContentType =
    "application/x-www-form-urlencoded";
constexpr std::string_view kMaximumU128 =
    "340282366920938463463374607431768211455";
constexpr char kBase58Alphabet[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

bool isAsciiDigit(const char value)
{
    return value >= '0' && value <= '9';
}

bool isAsciiLower(const char value)
{
    return value >= 'a' && value <= 'z';
}

bool isAsciiUpper(const char value)
{
    return value >= 'A' && value <= 'Z';
}

bool isAsciiAlphaNumeric(const char value)
{
    return isAsciiDigit(value) || isAsciiLower(value) || isAsciiUpper(value);
}

bool isLowerHex(const std::string& value, const std::size_t exactSize)
{
    return value.size() == exactSize
        && std::all_of(value.begin(), value.end(), [](const char character) {
            return isAsciiDigit(character)
                || (character >= 'a' && character <= 'f');
        });
}

bool isNonzeroLowerHex(
    const std::string& value,
    const std::size_t exactSize)
{
    return isLowerHex(value, exactSize)
        && std::any_of(value.begin(), value.end(), [](const char character) {
            return character != '0';
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
    StrictJsonParser(
        const std::string& input,
        const std::size_t maximumDepth,
        const std::size_t maximumNodes)
        : input_(input)
        , maximumDepth_(maximumDepth)
        , maximumNodes_(maximumNodes)
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
            if (value != ' ' && value != '\t' && value != '\r'
                && value != '\n') {
                break;
            }
            ++cursor_;
        }
    }

    bool parseValue(const std::size_t depth, JsonValue& output)
    {
        if (depth > maximumDepth_)
            return fail("json-too-deep");
        if (++nodes_ > maximumNodes_)
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
                return isValidUtf8(output) || fail("invalid-json-string-utf8");
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
            if (output.size() > input_.size())
                return fail("json-string-too-large");
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
        if (output.size() > 64U)
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
    std::size_t maximumDepth_ = 0U;
    std::size_t maximumNodes_ = 0U;
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
        || value.text.front() == '-' || value.text.find_first_of(".eE")
            != std::string::npos) {
        return false;
    }
    const auto parsed = std::from_chars(
        value.text.data(),
        value.text.data() + value.text.size(),
        output);
    return parsed.ec == std::errc{}
        && parsed.ptr == value.text.data() + value.text.size();
}

bool jsonUnsigned128(const JsonValue& value)
{
    if (value.type != JsonType::Number || value.text.empty()
        || value.text.front() == '-' || value.text.find_first_of(".eE")
            != std::string::npos) {
        return false;
    }
    if (value.text.size() > 1U && value.text.front() == '0')
        return false;
    return value.text.size() < kMaximumU128.size()
        || (value.text.size() == kMaximumU128.size()
            && value.text <= kMaximumU128);
}

std::string encodeBase58(const std::array<std::uint8_t, 32>& bytes)
{
    std::size_t leadingZeroes = 0U;
    while (leadingZeroes < bytes.size() && bytes[leadingZeroes] == 0U)
        ++leadingZeroes;
    std::vector<std::uint8_t> digits;
    for (const std::uint8_t byte : bytes) {
        unsigned int carry = byte;
        for (std::uint8_t& digit : digits) {
            carry += static_cast<unsigned int>(digit) * 256U;
            digit = static_cast<std::uint8_t>(carry % 58U);
            carry /= 58U;
        }
        while (carry != 0U) {
            digits.push_back(static_cast<std::uint8_t>(carry % 58U));
            carry /= 58U;
        }
    }
    std::string output(leadingZeroes, '1');
    for (auto digit = digits.rbegin(); digit != digits.rend(); ++digit)
        output.push_back(kBase58Alphabet[*digit]);
    return output;
}

bool decodeCanonicalBase58Id(
    const std::string& encoded,
    std::array<std::uint8_t, 32>& decoded)
{
    if (encoded.empty() || encoded.size() > 44U)
        return false;
    std::size_t leadingZeroes = 0U;
    while (leadingZeroes < encoded.size()
           && encoded[leadingZeroes] == '1') {
        ++leadingZeroes;
    }
    std::vector<std::uint8_t> bytes;
    for (const char character : encoded) {
        const char* found = std::char_traits<char>::find(
            kBase58Alphabet,
            std::char_traits<char>::length(kBase58Alphabet),
            character);
        if (found == nullptr)
            return false;
        unsigned int carry =
            static_cast<unsigned int>(found - kBase58Alphabet);
        for (std::uint8_t& byte : bytes) {
            carry += static_cast<unsigned int>(byte) * 58U;
            byte = static_cast<std::uint8_t>(carry & 0xffU);
            carry >>= 8U;
        }
        while (carry != 0U) {
            bytes.push_back(static_cast<std::uint8_t>(carry & 0xffU));
            carry >>= 8U;
        }
    }
    if (leadingZeroes + bytes.size() != decoded.size())
        return false;
    decoded.fill(0U);
    for (std::size_t index = 0U; index < bytes.size(); ++index)
        decoded[decoded.size() - 1U - index] = bytes[index];
    return encodeBase58(decoded) == encoded;
}

bool isCanonicalBase58Id(const std::string& value)
{
    std::array<std::uint8_t, 32> decoded{};
    return decodeCanonicalBase58Id(value, decoded);
}

int base64Digit(const char character)
{
    const char* found = std::char_traits<char>::find(
        kBase64Alphabet,
        std::char_traits<char>::length(kBase64Alphabet),
        character);
    return found == nullptr ? -1 : static_cast<int>(found - kBase64Alphabet);
}

std::string encodeBase64(const std::vector<std::uint8_t>& bytes)
{
    std::string output;
    output.reserve(((bytes.size() + 2U) / 3U) * 4U);
    for (std::size_t index = 0U; index < bytes.size(); index += 3U) {
        const std::uint32_t first = bytes[index];
        const std::uint32_t second =
            index + 1U < bytes.size() ? bytes[index + 1U] : 0U;
        const std::uint32_t third =
            index + 2U < bytes.size() ? bytes[index + 2U] : 0U;
        const std::uint32_t value =
            (first << 16U) | (second << 8U) | third;
        output.push_back(kBase64Alphabet[(value >> 18U) & 0x3fU]);
        output.push_back(kBase64Alphabet[(value >> 12U) & 0x3fU]);
        output.push_back(
            index + 1U < bytes.size()
                ? kBase64Alphabet[(value >> 6U) & 0x3fU]
                : '=');
        output.push_back(
            index + 2U < bytes.size()
                ? kBase64Alphabet[value & 0x3fU]
                : '=');
    }
    return output;
}

bool decodeCanonicalBase64(
    const std::string& encoded,
    const std::size_t maximumBytes,
    std::vector<std::uint8_t>& decoded)
{
    decoded.clear();
    if (encoded.empty())
        return true;
    if (encoded.size() % 4U != 0U
        || encoded.size() / 4U > (maximumBytes + 2U) / 3U + 1U) {
        return false;
    }
    decoded.reserve(encoded.size() / 4U * 3U);
    for (std::size_t index = 0U; index < encoded.size(); index += 4U) {
        const bool last = index + 4U == encoded.size();
        const int a = base64Digit(encoded[index]);
        const int b = base64Digit(encoded[index + 1U]);
        const int c = encoded[index + 2U] == '='
            ? -2 : base64Digit(encoded[index + 2U]);
        const int d = encoded[index + 3U] == '='
            ? -2 : base64Digit(encoded[index + 3U]);
        if (a < 0 || b < 0 || c == -1 || d == -1
            || (!last && (c < 0 || d < 0))
            || (c == -2 && d != -2)) {
            return false;
        }
        const std::uint32_t value =
            (static_cast<std::uint32_t>(a) << 18U)
            | (static_cast<std::uint32_t>(b) << 12U)
            | (c >= 0 ? static_cast<std::uint32_t>(c) << 6U : 0U)
            | (d >= 0 ? static_cast<std::uint32_t>(d) : 0U);
        decoded.push_back(static_cast<std::uint8_t>(value >> 16U));
        if (c >= 0)
            decoded.push_back(static_cast<std::uint8_t>(value >> 8U));
        if (d >= 0)
            decoded.push_back(static_cast<std::uint8_t>(value));
        if (decoded.size() > maximumBytes)
            return false;
    }
    return encodeBase64(decoded) == encoded;
}

bool isCanonicalBase64Bytes(
    const JsonValue& value,
    const std::size_t maximumBytes,
    const std::optional<std::size_t> exactBytes = std::nullopt)
{
    if (value.type != JsonType::String)
        return false;
    std::vector<std::uint8_t> decoded;
    return decodeCanonicalBase64(value.text, maximumBytes, decoded)
        && (!exactBytes.has_value() || decoded.size() == *exactBytes);
}

bool validateOrigin(const std::string& value)
{
    constexpr std::string_view prefix = "https://";
    if (value.size() <= prefix.size() || value.size() > kMaximumOriginBytes
        || value.compare(0U, prefix.size(), prefix) != 0
        || value.back() == '/' || value.find_first_of("/?#", prefix.size())
            != std::string::npos) {
        return false;
    }
    const std::string host = value.substr(prefix.size());
    if (host.front() == '.' || host.back() == '.')
        return false;
    bool previousDot = false;
    for (const char character : host) {
        if (character == '.') {
            if (previousDot)
                return false;
            previousDot = true;
            continue;
        }
        previousDot = false;
        if (!isAsciiLower(character) && !isAsciiDigit(character)
            && character != '-') {
            return false;
        }
    }
    return true;
}

bool validateFingerprint(
    const PalaceLezExplorerNetworkFingerprintV1& fingerprint)
{
    return fingerprint.fingerprintVersion == 1U
        && fingerprint.explorerSchemaVersion == 1U
        && !fingerprint.networkId.empty()
        && fingerprint.networkId.size() <= kMaximumNetworkIdBytes
        && std::all_of(
            fingerprint.networkId.begin(),
            fingerprint.networkId.end(),
            [](const char character) {
                return isAsciiAlphaNumeric(character)
                    || character == '-' || character == '_' || character == '.';
            })
        && isLowerHex(fingerprint.channelIdHex, 64U)
        && validateOrigin(fingerprint.explorerOrigin)
        && !fingerprint.serverFunctionSuffix.empty()
        && fingerprint.serverFunctionSuffix.size() <= kMaximumSuffixBytes
        && fingerprint.serverFunctionSuffix.front() != '0'
        && std::all_of(
            fingerprint.serverFunctionSuffix.begin(),
            fingerprint.serverFunctionSuffix.end(),
            isAsciiDigit);
}

bool validateLimits(const PalaceLezExplorerFinalityLimitsV1& limits)
{
    return limits.pageSize != 0U
        && limits.pageSize <= kAbsoluteMaxPageSize
        && limits.maxPages != 0U
        && limits.maxPages <= kAbsoluteMaxPages
        && limits.maxBlocks != 0U
        && limits.maxBlocks <= kAbsoluteMaxBlocks
        && limits.maxTransactionsPerBlock != 0U
        && limits.maxTransactionsPerBlock
            <= kAbsoluteMaxTransactionsPerBlock
        && limits.maxAccounts != 0U
        && limits.maxAccounts <= kAbsoluteMaxAccounts
        && limits.maxInstructionWords != 0U
        && limits.maxInstructionWords <= kAbsoluteMaxInstructionWords
        && limits.maxJsonBytes >= 1024U
        && limits.maxJsonBytes <= kAbsoluteMaxJsonBytes
        && limits.maxAccountDataBytes != 0U
        && limits.maxAccountDataBytes <= kAbsoluteMaxAccountDataBytes
        && limits.maxJsonDepth >= 4U
        && limits.maxJsonDepth <= kAbsoluteMaxJsonDepth
        && limits.maxJsonNodes >= 64U
        && limits.maxJsonNodes <= kAbsoluteMaxJsonNodes;
}

bool validateAccountObject(
    const JsonValue& object,
    const std::size_t maximumDataBytes,
    std::string* owner,
    std::vector<std::uint8_t>* data)
{
    if (!exactKeys(object, {"program_owner", "balance", "data", "nonce"}))
        return false;
    const JsonValue* programOwner = member(object, "program_owner");
    const JsonValue* balance = member(object, "balance");
    const JsonValue* accountData = member(object, "data");
    const JsonValue* nonce = member(object, "nonce");
    if (programOwner->type != JsonType::String
        || !isCanonicalBase58Id(programOwner->text)
        || !jsonUnsigned128(*balance) || !jsonUnsigned128(*nonce)
        || accountData->type != JsonType::String) {
        return false;
    }
    std::vector<std::uint8_t> decoded;
    if (!decodeCanonicalBase64(
            accountData->text,
            maximumDataBytes,
            decoded)) {
        return false;
    }
    if (owner != nullptr)
        *owner = programOwner->text;
    if (data != nullptr)
        *data = std::move(decoded);
    return true;
}

bool validateNonceArray(
    const JsonValue& value,
    const std::size_t maximumAccounts,
    std::size_t& count)
{
    if (value.type != JsonType::Array
        || value.array.size() > maximumAccounts
        || !std::all_of(
            value.array.begin(),
            value.array.end(),
            jsonUnsigned128)) {
        return false;
    }
    count = value.array.size();
    return true;
}

bool validateWitness(
    const JsonValue& value,
    const std::optional<std::size_t> expectedSignatures,
    const bool proofRequired,
    const PalaceLezExplorerFinalityLimitsV1& limits,
    std::size_t& signatureCount)
{
    if (!exactKeys(value, {"signatures_and_public_keys", "proof"}))
        return false;
    const JsonValue* signatures = member(value, "signatures_and_public_keys");
    const JsonValue* proof = member(value, "proof");
    if (signatures->type != JsonType::Array
        || (expectedSignatures.has_value()
            && signatures->array.size() != *expectedSignatures)
        || signatures->array.size() > limits.maxAccounts) {
        return false;
    }
    for (const JsonValue& pair : signatures->array) {
        if (pair.type != JsonType::Array || pair.array.size() != 2U
            || pair.array[0].type != JsonType::String
            || !isLowerHex(pair.array[0].text, 128U)
            || !isCanonicalBase64Bytes(
                pair.array[1],
                32U,
                32U)) {
            return false;
        }
    }
    signatureCount = signatures->array.size();
    if (proofRequired) {
        return proof->type == JsonType::String
            && !proof->text.empty()
            && isCanonicalBase64Bytes(
                *proof,
                limits.maxJsonBytes);
    }
    return proof->type == JsonType::Null;
}

bool validateValidityWindow(const JsonValue& value)
{
    if (value.type != JsonType::Array || value.array.size() != 2U)
        return false;
    for (const JsonValue& boundary : value.array) {
        std::uint64_t ignored = 0U;
        if (boundary.type != JsonType::Null
            && !jsonUnsigned64(boundary, ignored)) {
            return false;
        }
    }
    return true;
}

bool validatePrivacyMessage(
    const JsonValue& value,
    const PalaceLezExplorerFinalityLimitsV1& limits,
    std::size_t& nonceCount)
{
    if (!exactKeys(
            value,
            {"public_account_ids", "nonces", "public_post_states",
             "encrypted_private_post_states", "new_commitments",
             "new_nullifiers", "block_validity_window",
             "timestamp_validity_window"})) {
        return false;
    }
    const JsonValue* accountIds = member(value, "public_account_ids");
    const JsonValue* nonces = member(value, "nonces");
    const JsonValue* publicPostStates = member(value, "public_post_states");
    const JsonValue* encryptedPostStates =
        member(value, "encrypted_private_post_states");
    const JsonValue* commitments = member(value, "new_commitments");
    const JsonValue* nullifiers = member(value, "new_nullifiers");
    if (accountIds->type != JsonType::Array
        || accountIds->array.size() > limits.maxAccounts
        || !std::all_of(
            accountIds->array.begin(),
            accountIds->array.end(),
            [](const JsonValue& entry) {
                return entry.type == JsonType::String
                    && isCanonicalBase58Id(entry.text);
            })
        || !validateNonceArray(*nonces, limits.maxAccounts, nonceCount)
        || publicPostStates->type != JsonType::Array
        || publicPostStates->array.size() > limits.maxAccounts
        || !std::all_of(
            publicPostStates->array.begin(),
            publicPostStates->array.end(),
            [&](const JsonValue& account) {
                return validateAccountObject(
                    account,
                    limits.maxAccountDataBytes,
                    nullptr,
                    nullptr);
            })
        || encryptedPostStates->type != JsonType::Array
        || encryptedPostStates->array.size() > limits.maxAccounts) {
        return false;
    }
    for (const JsonValue& encrypted : encryptedPostStates->array) {
        if (!exactKeys(encrypted, {"ciphertext", "epk", "view_tag"}))
            return false;
        const JsonValue* ciphertext = member(encrypted, "ciphertext");
        const JsonValue* ephemeralKey = member(encrypted, "epk");
        const JsonValue* viewTag = member(encrypted, "view_tag");
        std::uint64_t viewTagValue = 0U;
        if (!isCanonicalBase64Bytes(
                *ciphertext,
                limits.maxAccountDataBytes)
            || !isCanonicalBase64Bytes(
                *ephemeralKey,
                limits.maxAccountDataBytes)
            || !jsonUnsigned64(*viewTag, viewTagValue)
            || viewTagValue > std::numeric_limits<std::uint8_t>::max()) {
            return false;
        }
    }
    if (commitments->type != JsonType::Array
        || commitments->array.size() > limits.maxAccounts
        || !std::all_of(
            commitments->array.begin(),
            commitments->array.end(),
            [](const JsonValue& entry) {
                return isCanonicalBase64Bytes(entry, 32U, 32U);
            })
        || nullifiers->type != JsonType::Array
        || nullifiers->array.size() > limits.maxAccounts) {
        return false;
    }
    for (const JsonValue& pair : nullifiers->array) {
        if (pair.type != JsonType::Array || pair.array.size() != 2U
            || !isCanonicalBase64Bytes(pair.array[0], 32U, 32U)
            || !isCanonicalBase64Bytes(pair.array[1], 32U, 32U)) {
            return false;
        }
    }
    return validateValidityWindow(*member(value, "block_validity_window"))
        && validateValidityWindow(
            *member(value, "timestamp_validity_window"));
}

enum class ParsedTransactionKind : std::uint8_t {
    Public,
    PrivacyPreserving,
    ProgramDeployment,
};

struct ParsedTransaction {
    ParsedTransactionKind kind = ParsedTransactionKind::Public;
    std::string hashHex;
    std::string programIdBase58;
    std::vector<std::string> accountIdsBase58;
    std::vector<std::uint32_t> instructionWords;
    std::size_t nonceCount = 0U;
    std::size_t signatureCount = 0U;
};

bool parsePublicTransaction(
    const JsonValue& value,
    const PalaceLezExplorerFinalityLimitsV1& limits,
    ParsedTransaction& output)
{
    if (!exactKeys(value, {"hash", "message", "witness_set"}))
        return false;
    const JsonValue* hash = member(value, "hash");
    const JsonValue* message = member(value, "message");
    const JsonValue* witness = member(value, "witness_set");
    if (hash->type != JsonType::String
        || !isLowerHex(hash->text, 64U)
        || !exactKeys(
            *message,
            {"program_id", "account_ids", "nonces", "instruction_data"})) {
        return false;
    }
    const JsonValue* programId = member(*message, "program_id");
    const JsonValue* accountIds = member(*message, "account_ids");
    const JsonValue* nonces = member(*message, "nonces");
    const JsonValue* instruction = member(*message, "instruction_data");
    if (programId->type != JsonType::String
        || !isCanonicalBase58Id(programId->text)
        || accountIds->type != JsonType::Array
        || accountIds->array.empty()
        || accountIds->array.size() > limits.maxAccounts
        || instruction->type != JsonType::Array
        || instruction->array.size() > limits.maxInstructionWords) {
        return false;
    }
    std::set<std::string> distinctAccounts;
    output.accountIdsBase58.reserve(accountIds->array.size());
    for (const JsonValue& account : accountIds->array) {
        if (account.type != JsonType::String
            || !isCanonicalBase58Id(account.text)
            || !distinctAccounts.insert(account.text).second) {
            return false;
        }
        output.accountIdsBase58.push_back(account.text);
    }
    output.instructionWords.reserve(instruction->array.size());
    for (const JsonValue& word : instruction->array) {
        std::uint64_t parsed = 0U;
        if (!jsonUnsigned64(word, parsed)
            || parsed > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        output.instructionWords.push_back(static_cast<std::uint32_t>(parsed));
    }
    if (!validateNonceArray(
            *nonces,
            limits.maxAccounts,
            output.nonceCount)
        || !validateWitness(
            *witness,
            std::nullopt,
            false,
            limits,
            output.signatureCount)) {
        return false;
    }
    output.kind = ParsedTransactionKind::Public;
    output.hashHex = hash->text;
    output.programIdBase58 = programId->text;
    return true;
}

bool parsePrivacyTransaction(
    const JsonValue& value,
    const PalaceLezExplorerFinalityLimitsV1& limits,
    ParsedTransaction& output)
{
    if (!exactKeys(value, {"hash", "message", "witness_set"}))
        return false;
    const JsonValue* hash = member(value, "hash");
    const JsonValue* message = member(value, "message");
    const JsonValue* witness = member(value, "witness_set");
    std::size_t nonceCount = 0U;
    std::size_t signatureCount = 0U;
    if (hash->type != JsonType::String
        || !isLowerHex(hash->text, 64U)
        || !validatePrivacyMessage(*message, limits, nonceCount)
        || !validateWitness(
            *witness,
            nonceCount,
            true,
            limits,
            signatureCount)) {
        return false;
    }
    output.kind = ParsedTransactionKind::PrivacyPreserving;
    output.hashHex = hash->text;
    output.nonceCount = nonceCount;
    output.signatureCount = signatureCount;
    return true;
}

bool parseDeploymentTransaction(
    const JsonValue& value,
    const PalaceLezExplorerFinalityLimitsV1& limits,
    ParsedTransaction& output)
{
    if (!exactKeys(value, {"hash", "message"}))
        return false;
    const JsonValue* hash = member(value, "hash");
    const JsonValue* message = member(value, "message");
    if (hash->type != JsonType::String
        || !isLowerHex(hash->text, 64U)
        || !exactKeys(*message, {"bytecode"})
        || !isCanonicalBase64Bytes(
            *member(*message, "bytecode"),
            limits.maxJsonBytes)) {
        return false;
    }
    output.kind = ParsedTransactionKind::ProgramDeployment;
    output.hashHex = hash->text;
    return true;
}

bool parseTransaction(
    const JsonValue& value,
    const PalaceLezExplorerFinalityLimitsV1& limits,
    ParsedTransaction& output)
{
    if (value.type != JsonType::Object || value.object.size() != 1U)
        return false;
    const std::string& variant = value.object.front().first;
    const JsonValue& body = value.object.front().second;
    if (variant == "Public")
        return parsePublicTransaction(body, limits, output);
    if (variant == "PrivacyPreserving")
        return parsePrivacyTransaction(body, limits, output);
    if (variant == "ProgramDeployment")
        return parseDeploymentTransaction(body, limits, output);
    return false;
}

struct ParsedBlock {
    std::uint64_t id = 0U;
    std::uint64_t timestamp = 0U;
    std::string previousHashHex;
    std::string hashHex;
    std::string bedrockStatus;
    std::vector<ParsedTransaction> transactions;
};

bool parseBlock(
    const JsonValue& value,
    const PalaceLezExplorerFinalityLimitsV1& limits,
    ParsedBlock& output)
{
    if (!exactKeys(value, {"header", "body", "bedrock_status"}))
        return false;
    const JsonValue* header = member(value, "header");
    const JsonValue* body = member(value, "body");
    const JsonValue* status = member(value, "bedrock_status");
    if (!exactKeys(
            *header,
            {"block_id", "prev_block_hash", "hash", "timestamp", "signature"})
        || !exactKeys(*body, {"transactions"})
        || status->type != JsonType::String
        || (status->text != "Pending" && status->text != "Safe"
            && status->text != "Finalized")) {
        return false;
    }
    const JsonValue* blockId = member(*header, "block_id");
    const JsonValue* previousHash = member(*header, "prev_block_hash");
    const JsonValue* hash = member(*header, "hash");
    const JsonValue* timestamp = member(*header, "timestamp");
    const JsonValue* signature = member(*header, "signature");
    const JsonValue* transactions = member(*body, "transactions");
    if (!jsonUnsigned64(*blockId, output.id) || output.id == 0U
        || previousHash->type != JsonType::String
        || !isLowerHex(previousHash->text, 64U)
        || hash->type != JsonType::String
        || !isNonzeroLowerHex(hash->text, 64U)
        || !jsonUnsigned64(*timestamp, output.timestamp)
        || signature->type != JsonType::String
        || !isLowerHex(signature->text, 128U)
        || transactions->type != JsonType::Array
        || transactions->array.size() > limits.maxTransactionsPerBlock) {
        return false;
    }
    output.previousHashHex = previousHash->text;
    output.hashHex = hash->text;
    output.bedrockStatus = status->text;
    output.transactions.reserve(transactions->array.size());
    for (const JsonValue& transaction : transactions->array) {
        ParsedTransaction parsed;
        if (!parseTransaction(transaction, limits, parsed))
            return false;
        output.transactions.push_back(std::move(parsed));
    }
    return true;
}

bool parsePage(
    const std::string& body,
    const PalaceLezExplorerFinalityLimitsV1& limits,
    std::vector<ParsedBlock>& output,
    std::string& reason)
{
    JsonValue document;
    StrictJsonParser parser(
        body,
        limits.maxJsonDepth,
        limits.maxJsonNodes);
    if (!parser.parse(document, reason))
        return false;
    if (document.type != JsonType::Array
        || document.array.size() > limits.pageSize) {
        reason = "invalid-block-page-schema";
        return false;
    }
    output.reserve(document.array.size());
    for (const JsonValue& block : document.array) {
        ParsedBlock parsed;
        if (!parseBlock(block, limits, parsed)) {
            reason = "invalid-block-page-schema";
            return false;
        }
        output.push_back(std::move(parsed));
    }
    return true;
}

bool validateExpectation(
    const PalaceLezExplorerTransactionExpectationV1& expectation,
    const PalaceLezExplorerFinalityLimitsV1& limits)
{
    if (!isLowerHex(expectation.transactionHashHex, 64U)
        || !isCanonicalBase58Id(expectation.programIdBase58)
        || expectation.accountIdsBase58.empty()
        || expectation.accountIdsBase58.size() > limits.maxAccounts
        || expectation.instructionWords.empty()
        || expectation.instructionWords.size() > limits.maxInstructionWords
        || expectation.accountExpectations.size()
            != expectation.accountIdsBase58.size()
        || !isLowerHex(expectation.instructionWordsSha256Hex, 64U)
        || PalaceLezExplorerFinalitySession::instructionWordsSha256Hex(
               expectation.instructionWords)
            != expectation.instructionWordsSha256Hex) {
        return false;
    }

    std::set<std::string> accountIds;
    for (std::size_t index = 0U;
         index < expectation.accountIdsBase58.size();
         ++index) {
        const std::string& accountId =
            expectation.accountIdsBase58[index];
        const PalaceLezExplorerAccountExpectationV1& account =
            expectation.accountExpectations[index];
        if (!isCanonicalBase58Id(accountId)
            || !accountIds.insert(accountId).second
            || account.accountIdBase58 != accountId
            || !isCanonicalBase58Id(account.programOwnerBase58)
            || account.expectedData.size() > limits.maxAccountDataBytes
            || !isLowerHex(account.expectedDataSha256Hex, 64U)
            || PalaceLezExplorerFinalitySession::bytesSha256Hex(
                   account.expectedData)
                != account.expectedDataSha256Hex) {
            return false;
        }
    }
    return true;
}

bool validateResponseMetadata(
    const PalaceLezExplorerHttpResponseV1& response,
    const PalaceLezExplorerCommandV1& command,
    const PalaceLezExplorerNetworkFingerprintV1& fingerprint,
    std::string& reason)
{
    if (!response.transportError.empty()) {
        reason = "explorer-transport-error";
        return false;
    }
    if (response.redirected) {
        reason = "explorer-redirect-rejected";
        return false;
    }
    if (response.effectiveOrigin != fingerprint.explorerOrigin
        || response.effectiveOrigin != command.origin) {
        reason = "explorer-origin-mismatch";
        return false;
    }
    if (response.statusCode != 200) {
        reason = "explorer-http-status";
        return false;
    }
    if (response.contentType != kJsonContentType) {
        reason = "explorer-content-type";
        return false;
    }
    if (response.body.empty()
        || response.body.size() > command.maxResponseBytes) {
        reason = "explorer-response-size";
        return false;
    }
    return true;
}

} // namespace

namespace lez_explorer_protocol {

BlockPageParseResultV1 parseBlockPage(
    const std::string& body,
    const PalaceLezExplorerFinalityLimitsV1& limits)
{
    BlockPageParseResultV1 result;
    std::vector<ParsedBlock> parsedBlocks;
    if (!parsePage(body, limits, parsedBlocks, result.reason))
        return result;

    result.blocks.reserve(parsedBlocks.size());
    for (ParsedBlock& parsedBlock : parsedBlocks) {
        BlockV1 block;
        block.id = parsedBlock.id;
        block.timestamp = parsedBlock.timestamp;
        block.previousHashHex = std::move(parsedBlock.previousHashHex);
        block.hashHex = std::move(parsedBlock.hashHex);
        block.bedrockStatus = std::move(parsedBlock.bedrockStatus);
        block.transactions.reserve(parsedBlock.transactions.size());
        for (ParsedTransaction& parsedTransaction :
             parsedBlock.transactions) {
            TransactionV1 transaction;
            switch (parsedTransaction.kind) {
            case ParsedTransactionKind::Public:
                transaction.kind = TransactionKind::Public;
                break;
            case ParsedTransactionKind::PrivacyPreserving:
                transaction.kind = TransactionKind::PrivacyPreserving;
                break;
            case ParsedTransactionKind::ProgramDeployment:
                transaction.kind = TransactionKind::ProgramDeployment;
                break;
            }
            transaction.hashHex = std::move(parsedTransaction.hashHex);
            transaction.programIdBase58 =
                std::move(parsedTransaction.programIdBase58);
            transaction.accountIdsBase58 =
                std::move(parsedTransaction.accountIdsBase58);
            transaction.instructionWords =
                std::move(parsedTransaction.instructionWords);
            transaction.nonceCount = parsedTransaction.nonceCount;
            transaction.signatureCount =
                parsedTransaction.signatureCount;
            block.transactions.push_back(std::move(transaction));
        }
        result.blocks.push_back(std::move(block));
    }
    result.accepted = true;
    result.reason = "accepted";
    return result;
}

bool networkFingerprintAccepted(
    const PalaceLezExplorerNetworkFingerprintV1& fingerprint)
{
    return validateFingerprint(fingerprint);
}

bool limitsAccepted(
    const PalaceLezExplorerFinalityLimitsV1& limits)
{
    return validateLimits(limits);
}

bool responseMetadataAccepted(
    const PalaceLezExplorerHttpResponseV1& response,
    const PalaceLezExplorerCommandV1& command,
    const PalaceLezExplorerNetworkFingerprintV1& fingerprint,
    std::string& reason)
{
    return validateResponseMetadata(
        response,
        command,
        fingerprint,
        reason);
}

PalaceLezExplorerCommandV1 makeBlocksCommand(
    const PalaceLezExplorerNetworkFingerprintV1& fingerprint,
    const PalaceLezExplorerFinalityLimitsV1& limits,
    const std::uint64_t sequence,
    const std::size_t blocksScanned,
    const std::optional<std::uint64_t> before)
{
    PalaceLezExplorerCommandV1 command;
    command.sequence = sequence;
    command.kind = PalaceLezExplorerCommandKind::Blocks;
    command.method = "POST";
    command.origin = fingerprint.explorerOrigin;
    command.path =
        "/api/get_blocks" + fingerprint.serverFunctionSuffix;
    command.requestContentType = std::string(kFormContentType);
    const std::size_t remaining =
        limits.maxBlocks - blocksScanned;
    const std::size_t limit = std::min(limits.pageSize, remaining);
    command.formBody = "limit=" + std::to_string(limit);
    if (before.has_value())
        command.formBody += "&before=" + std::to_string(*before);
    command.maxResponseBytes = limits.maxJsonBytes;
    return command;
}

} // namespace lez_explorer_protocol

std::uint32_t
PalaceLezExplorerFinalityCertificateV1::certificateVersion() const
{
    return certificateVersion_;
}

const std::string&
PalaceLezExplorerFinalityCertificateV1::transactionHashHex() const
{
    return transactionHashHex_;
}

const std::string&
PalaceLezExplorerFinalityCertificateV1::programIdBase58() const
{
    return programIdBase58_;
}

const std::vector<std::string>&
PalaceLezExplorerFinalityCertificateV1::accountIdsBase58() const
{
    return accountIdsBase58_;
}

const std::vector<std::uint32_t>&
PalaceLezExplorerFinalityCertificateV1::instructionWords() const
{
    return instructionWords_;
}

const std::string&
PalaceLezExplorerFinalityCertificateV1::instructionWordsSha256Hex() const
{
    return instructionWordsSha256Hex_;
}

std::uint64_t
PalaceLezExplorerFinalityCertificateV1::finalizedBlockId() const
{
    return finalizedBlockId_;
}

std::uint64_t
PalaceLezExplorerFinalityCertificateV1::finalizedBlockHeight() const
{
    return finalizedBlockHeight_;
}

const std::string&
PalaceLezExplorerFinalityCertificateV1::finalizedBlockHashHex() const
{
    return finalizedBlockHashHex_;
}

const std::vector<PalaceLezExplorerFinalityAccountEvidenceV1>&
PalaceLezExplorerFinalityCertificateV1::accountEvidence() const
{
    return accountEvidence_;
}

std::string PalaceLezExplorerFinalitySession::instructionWordsSha256Hex(
    const std::vector<std::uint32_t>& words)
{
    std::string bytes;
    bytes.reserve(words.size() * sizeof(std::uint32_t));
    for (const std::uint32_t word : words) {
        bytes.push_back(static_cast<char>(word & 0xffU));
        bytes.push_back(static_cast<char>((word >> 8U) & 0xffU));
        bytes.push_back(static_cast<char>((word >> 16U) & 0xffU));
        bytes.push_back(static_cast<char>((word >> 24U) & 0xffU));
    }
    return crypto::sha256Hex(bytes);
}

std::string PalaceLezExplorerFinalitySession::bytesSha256Hex(
    const std::vector<std::uint8_t>& bytes)
{
    return crypto::sha256Hex(std::string(bytes.begin(), bytes.end()));
}

PalaceLezExplorerFinalityUpdate
PalaceLezExplorerFinalitySession::start(
    const PalaceLezExplorerNetworkFingerprintV1& fingerprint,
    const PalaceLezExplorerFinalityLimitsV1& limits,
    const PalaceLezExplorerTransactionExpectationV1& expectation)
{
    fingerprint_ = {};
    limits_ = {};
    expectation_ = {};
    outcome_ = PalaceLezExplorerFinalityOutcome::Pending;
    phase_ = Phase::Idle;
    reason_ = "not-started";
    scanExhausted_ = false;
    nextCommandSequence_ = 1U;
    readyCommand_.reset();
    outstandingCommand_.reset();
    pagesScanned_ = 0U;
    blocksScanned_ = 0U;
    verifiedAccounts_ = 0U;
    oldestBlockId_ = 0U;
    oldestBlockTimestamp_ = 0U;
    oldestBlockPreviousHashHex_.clear();
    seenBlockIds_.clear();
    seenBlockHashes_.clear();
    seenTransactionHashes_.clear();
    finalizedBlockId_ = 0U;
    finalizedBlockHashHex_.clear();
    certificate_.reset();

    if (!validateFingerprint(fingerprint))
        return setTerminal(
            PalaceLezExplorerFinalityOutcome::Rejected,
            "invalid-explorer-network-fingerprint");
    if (!validateLimits(limits))
        return setTerminal(
            PalaceLezExplorerFinalityOutcome::Rejected,
            "invalid-explorer-limits");
    if (!validateExpectation(expectation, limits))
        return setTerminal(
            PalaceLezExplorerFinalityOutcome::Rejected,
            "invalid-finality-expectation");

    fingerprint_ = fingerprint;
    limits_ = limits;
    expectation_ = expectation;
    phase_ = Phase::ScanningBlocks;
    reason_ = "awaiting-finalized-block-page";
    queueBlocksCommand(std::nullopt);
    return {
        true,
        true,
        PalaceLezExplorerFinalityOutcome::Pending,
        reason_,
    };
}

std::optional<PalaceLezExplorerCommandV1>
PalaceLezExplorerFinalitySession::takeNextCommand()
{
    if (phase_ == Phase::Finished || outstandingCommand_.has_value()
        || !readyCommand_.has_value()) {
        return std::nullopt;
    }
    outstandingCommand_ = std::move(readyCommand_);
    readyCommand_.reset();
    return outstandingCommand_;
}

PalaceLezExplorerFinalityUpdate
PalaceLezExplorerFinalitySession::acceptResponse(
    const PalaceLezExplorerHttpResponseV1& response)
{
    if (phase_ == Phase::Finished) {
        return {false, false, outcome_, "finality-session-finished"};
    }
    if (!outstandingCommand_.has_value()
        || response.commandSequence != outstandingCommand_->sequence) {
        return setTerminal(
            PalaceLezExplorerFinalityOutcome::Rejected,
            "explorer-command-correlation-mismatch");
    }

    const PalaceLezExplorerCommandV1 command = *outstandingCommand_;
    outstandingCommand_.reset();
    std::string metadataReason;
    if (!validateResponseMetadata(
            response,
            command,
            fingerprint_,
            metadataReason)) {
        return setTerminal(
            PalaceLezExplorerFinalityOutcome::Degraded,
            std::move(metadataReason));
    }
    if (command.kind == PalaceLezExplorerCommandKind::Blocks
        && phase_ == Phase::ScanningBlocks) {
        return acceptBlocksBody(response.body);
    }
    if (command.kind == PalaceLezExplorerCommandKind::Account
        && phase_ == Phase::CheckingAccounts) {
        return acceptAccountBody(response.body);
    }
    return setTerminal(
        PalaceLezExplorerFinalityOutcome::Rejected,
        "explorer-command-phase-mismatch");
}

PalaceLezExplorerFinalityOutcome
PalaceLezExplorerFinalitySession::outcome() const
{
    return outcome_;
}

const std::string& PalaceLezExplorerFinalitySession::reason() const
{
    return reason_;
}

bool PalaceLezExplorerFinalitySession::waitingForResponse() const
{
    return outstandingCommand_.has_value();
}

bool PalaceLezExplorerFinalitySession::scanExhausted() const
{
    return scanExhausted_;
}

std::size_t PalaceLezExplorerFinalitySession::pagesScanned() const
{
    return pagesScanned_;
}

std::size_t PalaceLezExplorerFinalitySession::blocksScanned() const
{
    return blocksScanned_;
}

std::size_t PalaceLezExplorerFinalitySession::verifiedAccountCount() const
{
    return verifiedAccounts_;
}

std::uint64_t PalaceLezExplorerFinalitySession::finalizedBlockId() const
{
    return finalizedBlockId_;
}

const std::string&
PalaceLezExplorerFinalitySession::finalizedBlockHashHex() const
{
    return finalizedBlockHashHex_;
}

std::optional<PalaceLezExplorerFinalityCertificateV1>
PalaceLezExplorerFinalitySession::finalityCertificate() const
{
    if (outcome_ != PalaceLezExplorerFinalityOutcome::Finalized)
        return std::nullopt;
    return certificate_;
}

PalaceLezExplorerFinalityUpdate
PalaceLezExplorerFinalitySession::setTerminal(
    const PalaceLezExplorerFinalityOutcome outcome,
    std::string reason)
{
    const bool changed = outcome_ != outcome || reason_ != reason;
    outcome_ = outcome;
    reason_ = std::move(reason);
    phase_ = Phase::Finished;
    readyCommand_.reset();
    outstandingCommand_.reset();
    if (outcome != PalaceLezExplorerFinalityOutcome::Finalized)
        certificate_.reset();
    return {false, changed, outcome_, reason_};
}

PalaceLezExplorerFinalityUpdate
PalaceLezExplorerFinalitySession::setPending(
    std::string reason,
    const bool scanExhausted)
{
    const bool changed =
        outcome_ != PalaceLezExplorerFinalityOutcome::Pending
        || reason_ != reason || scanExhausted_ != scanExhausted;
    outcome_ = PalaceLezExplorerFinalityOutcome::Pending;
    reason_ = std::move(reason);
    scanExhausted_ = scanExhausted;
    if (scanExhausted) {
        phase_ = Phase::Finished;
        readyCommand_.reset();
        outstandingCommand_.reset();
    }
    return {true, changed, outcome_, reason_};
}

void PalaceLezExplorerFinalitySession::queueBlocksCommand(
    const std::optional<std::uint64_t> before)
{
    readyCommand_ = lez_explorer_protocol::makeBlocksCommand(
        fingerprint_,
        limits_,
        nextCommandSequence_++,
        blocksScanned_,
        before);
}

void PalaceLezExplorerFinalitySession::queueAccountCommand(
    const std::size_t accountIndex)
{
    PalaceLezExplorerCommandV1 command;
    command.sequence = nextCommandSequence_++;
    command.kind = PalaceLezExplorerCommandKind::Account;
    command.method = "POST";
    command.origin = fingerprint_.explorerOrigin;
    command.path =
        "/api/get_account" + fingerprint_.serverFunctionSuffix;
    command.requestContentType = std::string(kFormContentType);
    command.formBody = "account_id="
        + expectation_.accountExpectations[accountIndex].accountIdBase58;
    const std::size_t encodedDataBytes =
        ((limits_.maxAccountDataBytes + 2U) / 3U) * 4U;
    command.maxResponseBytes =
        std::min(limits_.maxJsonBytes, encodedDataBytes + 1024U);
    readyCommand_ = std::move(command);
}

PalaceLezExplorerFinalityUpdate
PalaceLezExplorerFinalitySession::acceptBlocksBody(
    const std::string& body)
{
    std::vector<ParsedBlock> page;
    std::string parseReason;
    if (!parsePage(body, limits_, page, parseReason)) {
        return setTerminal(
            PalaceLezExplorerFinalityOutcome::Degraded,
            std::move(parseReason));
    }
    ++pagesScanned_;
    if (page.empty())
        return setPending("finalized-history-exhausted", true);
    if (page.size() > limits_.maxBlocks - blocksScanned_) {
        return setTerminal(
            PalaceLezExplorerFinalityOutcome::Degraded,
            "block-scan-limit-exceeded");
    }

    for (std::size_t index = 0U; index < page.size(); ++index) {
        const ParsedBlock& block = page[index];
        if (index != 0U) {
            const ParsedBlock& newer = page[index - 1U];
            if (newer.id != block.id + 1U
                || newer.previousHashHex != block.hashHex
                || newer.timestamp < block.timestamp) {
                return setTerminal(
                    PalaceLezExplorerFinalityOutcome::Rejected,
                    "block-page-chain-order-mismatch");
            }
        } else if (oldestBlockId_ != 0U) {
            if (oldestBlockId_ != block.id + 1U
                || oldestBlockPreviousHashHex_ != block.hashHex
                || oldestBlockTimestamp_ < block.timestamp) {
                return setTerminal(
                    PalaceLezExplorerFinalityOutcome::Rejected,
                    "block-page-cursor-replay-or-overlap");
            }
        }
        if (!seenBlockIds_.insert(block.id).second
            || !seenBlockHashes_.insert(block.hashHex).second) {
            return setTerminal(
                PalaceLezExplorerFinalityOutcome::Rejected,
                "duplicate-or-replayed-block");
        }
        for (const ParsedTransaction& transaction : block.transactions) {
            if (!seenTransactionHashes_.insert(transaction.hashHex).second) {
                return setTerminal(
                    PalaceLezExplorerFinalityOutcome::Rejected,
                    "duplicate-transaction-hash");
            }
        }
    }

    bool targetFound = false;
    for (const ParsedBlock& block : page) {
        for (const ParsedTransaction& transaction : block.transactions) {
            if (transaction.hashHex != expectation_.transactionHashHex)
                continue;
            if (targetFound) {
                return setTerminal(
                    PalaceLezExplorerFinalityOutcome::Rejected,
                    "duplicate-target-transaction");
            }
            targetFound = true;
            if (block.id <= expectation_.baselineBlockId) {
                return setTerminal(
                    PalaceLezExplorerFinalityOutcome::Rejected,
                    "target-transaction-before-baseline");
            }
            if (block.bedrockStatus != "Finalized") {
                return setTerminal(
                    PalaceLezExplorerFinalityOutcome::Rejected,
                    "target-block-not-finalized");
            }
            if (transaction.kind != ParsedTransactionKind::Public) {
                return setTerminal(
                    PalaceLezExplorerFinalityOutcome::Rejected,
                    "target-transaction-variant-mismatch");
            }
            if (transaction.programIdBase58
                    != expectation_.programIdBase58
                || transaction.accountIdsBase58
                    != expectation_.accountIdsBase58
                || transaction.instructionWords
                    != expectation_.instructionWords
                || instructionWordsSha256Hex(
                       transaction.instructionWords)
                    != expectation_.instructionWordsSha256Hex) {
                return setTerminal(
                    PalaceLezExplorerFinalityOutcome::Rejected,
                    "target-transaction-content-mismatch");
            }
            finalizedBlockId_ = block.id;
            finalizedBlockHashHex_ = block.hashHex;
        }
    }

    blocksScanned_ += page.size();
    const ParsedBlock& oldest = page.back();
    oldestBlockId_ = oldest.id;
    oldestBlockTimestamp_ = oldest.timestamp;
    oldestBlockPreviousHashHex_ = oldest.previousHashHex;

    if (targetFound) {
        phase_ = Phase::CheckingAccounts;
        reason_ = "awaiting-finalized-account-state";
        queueAccountCommand(0U);
        return {
            true,
            true,
            PalaceLezExplorerFinalityOutcome::Pending,
            reason_,
        };
    }
    if (oldestBlockId_ <= expectation_.baselineBlockId)
        return setPending("finality-baseline-exhausted", true);
    if (blocksScanned_ >= limits_.maxBlocks
        || pagesScanned_ >= limits_.maxPages) {
        return setPending("finality-scan-window-exhausted", true);
    }

    queueBlocksCommand(oldestBlockId_);
    return setPending("awaiting-older-finalized-block-page", false);
}

PalaceLezExplorerFinalityUpdate
PalaceLezExplorerFinalitySession::acceptAccountBody(
    const std::string& body)
{
    JsonValue document;
    std::string parseReason;
    StrictJsonParser parser(
        body,
        limits_.maxJsonDepth,
        limits_.maxJsonNodes);
    if (!parser.parse(document, parseReason)) {
        return setTerminal(
            PalaceLezExplorerFinalityOutcome::Degraded,
            std::move(parseReason));
    }
    if (document.type == JsonType::Null) {
        return setTerminal(
            PalaceLezExplorerFinalityOutcome::Rejected,
            "finalized-account-not-found");
    }
    std::string owner;
    std::vector<std::uint8_t> data;
    if (!validateAccountObject(
            document,
            limits_.maxAccountDataBytes,
            &owner,
            &data)) {
        return setTerminal(
            PalaceLezExplorerFinalityOutcome::Degraded,
            "invalid-account-response-schema");
    }
    if (verifiedAccounts_ >= expectation_.accountExpectations.size()) {
        return setTerminal(
            PalaceLezExplorerFinalityOutcome::Rejected,
            "unexpected-account-response");
    }
    const PalaceLezExplorerAccountExpectationV1& expected =
        expectation_.accountExpectations[verifiedAccounts_];
    if (owner != expected.programOwnerBase58) {
        return setTerminal(
            PalaceLezExplorerFinalityOutcome::Rejected,
            "finalized-account-owner-mismatch");
    }
    if (data != expected.expectedData
        || bytesSha256Hex(data) != expected.expectedDataSha256Hex) {
        return setTerminal(
            PalaceLezExplorerFinalityOutcome::Rejected,
            "finalized-account-data-mismatch");
    }

    ++verifiedAccounts_;
    if (verifiedAccounts_ == expectation_.accountExpectations.size()) {
        PalaceLezExplorerFinalityCertificateV1 certificate;
        certificate.transactionHashHex_ = expectation_.transactionHashHex;
        certificate.programIdBase58_ = expectation_.programIdBase58;
        certificate.accountIdsBase58_ = expectation_.accountIdsBase58;
        certificate.instructionWords_ = expectation_.instructionWords;
        certificate.instructionWordsSha256Hex_ =
            expectation_.instructionWordsSha256Hex;
        certificate.finalizedBlockId_ = finalizedBlockId_;
        certificate.finalizedBlockHeight_ = finalizedBlockId_;
        certificate.finalizedBlockHashHex_ = finalizedBlockHashHex_;
        certificate.accountEvidence_.reserve(
            expectation_.accountExpectations.size());
        for (const PalaceLezExplorerAccountExpectationV1& account :
             expectation_.accountExpectations) {
            certificate.accountEvidence_.push_back({
                account.accountIdBase58,
                account.programOwnerBase58,
                account.expectedDataSha256Hex,
            });
        }
        certificate_ = std::move(certificate);
        outcome_ = PalaceLezExplorerFinalityOutcome::Finalized;
        reason_ = "finalized-account-state-verified";
        phase_ = Phase::Finished;
        return {
            true,
            true,
            PalaceLezExplorerFinalityOutcome::Finalized,
            reason_,
        };
    }
    queueAccountCommand(verifiedAccounts_);
    reason_ = "awaiting-finalized-account-state";
    return {
        true,
        true,
        PalaceLezExplorerFinalityOutcome::Pending,
        reason_,
    };
}

} // namespace palace
