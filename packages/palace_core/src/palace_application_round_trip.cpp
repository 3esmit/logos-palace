#include "palace_application_round_trip.h"

#include <cstdint>

namespace palace {
namespace {

bool continuation(const std::uint8_t byte)
{
    return byte >= 0x80U && byte <= 0xbfU;
}

bool validUtf8(const std::string& value)
{
    std::size_t cursor = 0U;
    while (cursor < value.size()) {
        const auto first =
            static_cast<std::uint8_t>(value[cursor]);
        if (first <= 0x7fU) {
            ++cursor;
            continue;
        }
        if (first >= 0xc2U && first <= 0xdfU) {
            if (cursor + 1U >= value.size()
                || !continuation(
                    static_cast<std::uint8_t>(
                        value[cursor + 1U]))) {
                return false;
            }
            cursor += 2U;
            continue;
        }
        if (first >= 0xe0U && first <= 0xefU) {
            if (cursor + 2U >= value.size())
                return false;
            const auto second =
                static_cast<std::uint8_t>(value[cursor + 1U]);
            const auto third =
                static_cast<std::uint8_t>(value[cursor + 2U]);
            if (!continuation(third)
                || (first == 0xe0U
                    ? second < 0xa0U || second > 0xbfU
                    : first == 0xedU
                        ? second < 0x80U || second > 0x9fU
                        : !continuation(second))) {
                return false;
            }
            cursor += 3U;
            continue;
        }
        if (first >= 0xf0U && first <= 0xf4U) {
            if (cursor + 3U >= value.size())
                return false;
            const auto second =
                static_cast<std::uint8_t>(value[cursor + 1U]);
            if ((first == 0xf0U
                    ? second < 0x90U || second > 0xbfU
                    : first == 0xf4U
                        ? second < 0x80U || second > 0x8fU
                        : !continuation(second))
                || !continuation(
                    static_cast<std::uint8_t>(
                        value[cursor + 2U]))
                || !continuation(
                    static_cast<std::uint8_t>(
                        value[cursor + 3U]))) {
                return false;
            }
            cursor += 4U;
            continue;
        }
        return false;
    }
    return true;
}

} // namespace

PalaceApplicationRoundTripResultV1 applicationRoundTripV1(
    const std::string& payload)
{
    PalaceApplicationRoundTripResultV1 result;
    result.utf8Bytes = payload.size();
    if (payload.size() != 0U
        && payload.size() != 256U
        && payload.size() != 4096U) {
        result.reason = "size";
        return result;
    }
    if (!validUtf8(payload)) {
        result.reason = "utf8";
        return result;
    }
    result.accepted = true;
    result.response = payload;
    result.reason = "echoed";
    return result;
}

} // namespace palace
