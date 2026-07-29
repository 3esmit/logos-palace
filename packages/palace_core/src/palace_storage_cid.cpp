#include "palace_storage_cid.h"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace palace {
namespace {

constexpr std::string_view kBase58BitcoinAlphabet =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
constexpr std::string_view kLowerBase32Alphabet =
    "abcdefghijklmnopqrstuvwxyz234567";
constexpr char kHexDigits[] = "0123456789abcdef";
constexpr std::size_t kMaximumCidBytes = 128U;

int base58BitcoinValue(char character)
{
    const std::size_t position =
        kBase58BitcoinAlphabet.find(character);
    return position == std::string_view::npos
        ? -1 : static_cast<int>(position);
}

bool decodeBase58Bitcoin(
    const std::string& encoded,
    std::vector<std::uint8_t>& decoded)
{
    decoded.clear();
    if (encoded.empty())
        return false;

    std::vector<std::uint8_t> number(1U, 0U);
    std::size_t leadingZeroes = 0U;
    while (leadingZeroes < encoded.size()
           && encoded[leadingZeroes] == '1') {
        ++leadingZeroes;
    }
    for (char character : encoded) {
        const int digit = base58BitcoinValue(character);
        if (digit < 0)
            return false;
        unsigned int carry = static_cast<unsigned int>(digit);
        for (auto byte = number.rbegin(); byte != number.rend(); ++byte) {
            carry += static_cast<unsigned int>(*byte) * 58U;
            *byte = static_cast<std::uint8_t>(carry & 0xffU);
            carry >>= 8U;
        }
        while (carry != 0U) {
            number.insert(
                number.begin(),
                static_cast<std::uint8_t>(carry & 0xffU));
            carry >>= 8U;
        }
        if (number.size() + leadingZeroes > kMaximumCidBytes)
            return false;
    }

    decoded.assign(leadingZeroes, 0U);
    if (!(number.size() == 1U && number.front() == 0U))
        decoded.insert(decoded.end(), number.begin(), number.end());
    return !decoded.empty()
        && decoded.size() <= kMaximumCidBytes;
}

std::string encodeBase58Bitcoin(
    const std::vector<std::uint8_t>& decoded)
{
    if (decoded.empty())
        return {};
    std::size_t leadingZeroes = 0U;
    while (leadingZeroes < decoded.size()
           && decoded[leadingZeroes] == 0U) {
        ++leadingZeroes;
    }

    std::vector<unsigned int> digits(1U, 0U);
    for (std::size_t index = leadingZeroes;
         index < decoded.size();
         ++index) {
        unsigned int carry = decoded[index];
        for (unsigned int& digit : digits) {
            const unsigned int value = digit * 256U + carry;
            digit = value % 58U;
            carry = value / 58U;
        }
        while (carry != 0U) {
            digits.push_back(carry % 58U);
            carry /= 58U;
        }
    }

    std::string encoded(leadingZeroes, '1');
    if (leadingZeroes != decoded.size()) {
        for (auto digit = digits.rbegin(); digit != digits.rend(); ++digit)
            encoded.push_back(kBase58BitcoinAlphabet[*digit]);
    }
    return encoded;
}

bool decodeLowerBase32(
    const std::string& encoded,
    std::vector<std::uint8_t>& decoded)
{
    decoded.clear();
    if (encoded.empty())
        return false;
    std::uint32_t accumulator = 0U;
    unsigned int bitCount = 0U;
    for (char character : encoded) {
        const std::size_t position =
            kLowerBase32Alphabet.find(character);
        if (position == std::string_view::npos)
            return false;
        accumulator =
            (accumulator << 5U)
            | static_cast<std::uint32_t>(position);
        bitCount += 5U;
        while (bitCount >= 8U) {
            bitCount -= 8U;
            decoded.push_back(static_cast<std::uint8_t>(
                accumulator >> bitCount));
            accumulator &= bitCount == 0U
                ? 0U : ((1U << bitCount) - 1U);
        }
        if (decoded.size() > kMaximumCidBytes)
            return false;
    }
    return (bitCount == 0U || accumulator == 0U)
        && !decoded.empty();
}

std::string encodeLowerBase32(
    const std::vector<std::uint8_t>& decoded)
{
    std::string encoded;
    std::uint32_t accumulator = 0U;
    unsigned int bitCount = 0U;
    for (std::uint8_t byte : decoded) {
        accumulator = (accumulator << 8U) | byte;
        bitCount += 8U;
        while (bitCount >= 5U) {
            bitCount -= 5U;
            encoded.push_back(kLowerBase32Alphabet[
                (accumulator >> bitCount) & 0x1fU]);
            accumulator &= bitCount == 0U
                ? 0U : ((1U << bitCount) - 1U);
        }
    }
    if (bitCount != 0U) {
        encoded.push_back(kLowerBase32Alphabet[
            (accumulator << (5U - bitCount)) & 0x1fU]);
    }
    return encoded;
}

bool readCanonicalVarint(
    const std::vector<std::uint8_t>& bytes,
    std::size_t& cursor,
    std::uint64_t& value)
{
    value = 0U;
    unsigned int shift = 0U;
    std::size_t encodedBytes = 0U;
    while (cursor < bytes.size() && encodedBytes < 10U) {
        const std::uint8_t byte = bytes[cursor++];
        const std::uint8_t payload =
            static_cast<std::uint8_t>(byte & 0x7fU);
        if (shift == 63U && payload > 1U)
            return false;
        value |= static_cast<std::uint64_t>(payload) << shift;
        ++encodedBytes;
        if ((byte & 0x80U) == 0U)
            return encodedBytes == 1U || payload != 0U;
        shift += 7U;
    }
    return false;
}

std::string lowerHex(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset)
{
    std::string encoded;
    encoded.reserve((bytes.size() - offset) * 2U);
    for (; offset < bytes.size(); ++offset) {
        encoded.push_back(kHexDigits[bytes[offset] >> 4U]);
        encoded.push_back(kHexDigits[bytes[offset] & 0x0fU]);
    }
    return encoded;
}

bool parseCidV1(
    const std::vector<std::uint8_t>& decoded,
    std::string& digest)
{
    std::size_t cursor = 0U;
    std::uint64_t version = 0U;
    std::uint64_t codec = 0U;
    std::uint64_t multihashCode = 0U;
    std::uint64_t digestLength = 0U;
    if (!readCanonicalVarint(decoded, cursor, version)
        || version != 1U
        || !readCanonicalVarint(decoded, cursor, codec)
        || codec == 0U
        || !readCanonicalVarint(
            decoded, cursor, multihashCode)
        || multihashCode != 0x12U
        || !readCanonicalVarint(
            decoded, cursor, digestLength)
        || digestLength != 32U
        || cursor + digestLength != decoded.size()) {
        return false;
    }
    digest = lowerHex(decoded, cursor);
    return true;
}

} // namespace

bool canonicalStorageCidSha256(
    const std::string& value,
    std::string& digest)
{
    digest.clear();
    if (value.size() < 10U || value.size() > kMaximumCidBytes)
        return false;

    std::vector<std::uint8_t> decoded;
    if (value.size() == 46U
        && value.rfind("Qm", 0U) == 0U) {
        if (!decodeBase58Bitcoin(value, decoded)
            || encodeBase58Bitcoin(decoded) != value
            || decoded.size() != 34U
            || decoded[0] != 0x12U
            || decoded[1] != 0x20U) {
            return false;
        }
        digest = lowerHex(decoded, 2U);
        return true;
    }

    return canonicalStorageCidV1Sha256(value, digest);
}

bool canonicalStorageCidV1Sha256(
    const std::string& value,
    std::string& digest)
{
    digest.clear();
    if (value.size() < 10U || value.size() > kMaximumCidBytes)
        return false;

    std::vector<std::uint8_t> decoded;
    if (value.front() == 'b') {
        const std::string body = value.substr(1U);
        if (!decodeLowerBase32(body, decoded)
            || "b" + encodeLowerBase32(decoded) != value) {
            return false;
        }
    } else if (value.front() == 'z') {
        const std::string body = value.substr(1U);
        if (!decodeBase58Bitcoin(body, decoded)
            || "z" + encodeBase58Bitcoin(decoded) != value) {
            return false;
        }
    } else {
        return false;
    }
    return parseCidV1(decoded, digest);
}

bool isCanonicalStorageCid(const std::string& value)
{
    std::string digest;
    return canonicalStorageCidSha256(value, digest);
}

} // namespace palace
