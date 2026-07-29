#include <logos_test.h>

#include "palace_application_round_trip.h"

#include <string>
#include <vector>

LOGOS_TEST(application_round_trip_echoes_only_exact_bounded_utf8_sizes) {
    const std::vector<std::string> accepted = {
        {},
        std::string(256U, 'a'),
        std::string(4096U, 'b'),
        std::string(252U, 'c') + "\xf0\x9f\x8f\xb0",
    };
    for (const std::string& payload : accepted) {
        const auto result =
            palace::applicationRoundTripV1(payload);
        LOGOS_ASSERT_TRUE(result.accepted);
        LOGOS_ASSERT_EQ(result.reason, std::string("echoed"));
        LOGOS_ASSERT_EQ(result.utf8Bytes, payload.size());
        LOGOS_ASSERT_EQ(result.response, payload);
    }

    for (const std::size_t size : {1U, 255U, 257U, 4095U, 4097U}) {
        const auto result =
            palace::applicationRoundTripV1(
                std::string(size, 'x'));
        LOGOS_ASSERT_FALSE(result.accepted);
        LOGOS_ASSERT_EQ(result.reason, std::string("size"));
        LOGOS_ASSERT_TRUE(result.response.empty());
    }
}

LOGOS_TEST(application_round_trip_rejects_invalid_utf8_without_echoing) {
    std::string invalid(256U, 'x');
    invalid[127] = static_cast<char>(0xc0);
    invalid[128] = static_cast<char>(0x80);
    const auto result =
        palace::applicationRoundTripV1(invalid);

    LOGOS_ASSERT_FALSE(result.accepted);
    LOGOS_ASSERT_EQ(result.reason, std::string("utf8"));
    LOGOS_ASSERT_EQ(result.utf8Bytes, invalid.size());
    LOGOS_ASSERT_TRUE(result.response.empty());
}
