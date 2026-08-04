#include <logos_test.h>

#include "palace_lez_finality_expectation.h"

#include <string>
#include <vector>

namespace {

constexpr const char* kProgramId =
    "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";
constexpr const char* kSigner =
    "6666666666666666666666666666666666666666666666666666666666666666";

palace::PalaceLezBytes32 bytes(const std::uint8_t value)
{
    palace::PalaceLezBytes32 output{};
    output.fill(value);
    return output;
}

std::string hexBytes(const std::vector<std::uint8_t>& bytes)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string output(bytes.size() * 2U, '0');
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        output[index * 2U] = digits[bytes[index] >> 4U];
        output[index * 2U + 1U] = digits[bytes[index] & 0x0fU];
    }
    return output;
}

std::string accountJson(
    const std::string& owner,
    const std::string& data)
{
    return "{\"program_owner\":\"" + owner
        + "\",\"balance\":\"00000000000000000000000000000000\","
          "\"nonce\":\"00000000000000000000000000000000\","
          "\"data\":\""
        + data + "\"}";
}

struct Fixture {
    palace::PalaceLezTrackedTransaction tracked;
    palace::PalaceLezStableAccountBatchV1 accounts;
};

Fixture fixture()
{
    using namespace palace;
    PalaceLezInitializeV3 initialize;
    initialize.palaceId = bytes(0x10U);
    initialize.title = "Palace";
    initialize.activeManifestCid = "bafypalacemanifest";
    initialize.ownerProfile = {
        "Alice", bytes(0x51U), 1U, std::nullopt};
    initialize.ownerGrantId = bytes(0x11U);
    initialize.entryRoomId = bytes(0x31U);
    initialize.entryRoom = {
        "Atrium",
        "bafyatrium",
        "bafyatriumscript",
        PalaceLezVmProfileV3::IptScraeMvpV1,
    };
    initialize.secondaryRoomId = bytes(0x32U);
    initialize.secondaryRoom = {
        "Lounge",
        "bafylounge",
        "bafyloungescript",
        PalaceLezVmProfileV3::IptScraeMvpV1,
    };

    Fixture result;
    result.tracked.stage = PalaceLezTransactionStage::Observed;
    result.tracked.transactionHash = std::string(64U, 'a');
    result.tracked.observedBlockHeight = 700U;
    result.tracked.orderedActionId = 0U;
    result.tracked.plan = PalaceLezCodec::buildTransaction(
        kProgramId,
        kSigner,
        PalaceLezInstructionV3{initialize});
    LOGOS_ASSERT_TRUE(result.tracked.plan.accepted);
    const PalaceLezExpectedRootV3 root =
        PalaceLezCodec::expectedInitialRoot(
            kSigner,
            PalaceLezInstructionV3{initialize});
    LOGOS_ASSERT_TRUE(root.accepted);
    result.tracked.expectedRootDataSha256Hex =
        root.dataSha256Hex;

    result.accounts.heightBefore = 700;
    result.accounts.heightAfter = 700;
    result.accounts.accountResponseJson.reserve(
        result.tracked.plan.accountIdsHex.size());
    for (std::size_t index = 0U;
         index < result.tracked.plan.accountIdsHex.size();
         ++index) {
        const bool rootAccount = index == 0U;
        const bool signer =
            result.tracked.plan.signingRequirements[index];
        result.accounts.accountResponseJson.push_back(
            accountJson(
                signer ? std::string(64U, '0')
                       : std::string(kProgramId),
                rootAccount ? hexBytes(root.encodedRecord)
                            : (signer ? std::string{}
                                      : std::string("00"))));
    }
    return result;
}

} // namespace

LOGOS_TEST(lez_finality_expectation_binds_every_height_stable_account) {
    using namespace palace;
    const Fixture input = fixture();
    const PalaceLezFinalityExpectationBuildResultV1 built =
        buildPalaceLezFinalityExpectationV1(
            input.tracked, input.accounts);
    LOGOS_ASSERT_TRUE(built.accepted);
    LOGOS_ASSERT_EQ(built.reason, std::string("accepted"));
    LOGOS_ASSERT_EQ(
        built.expectation.transactionHashHex,
        input.tracked.transactionHash);
    LOGOS_ASSERT_EQ(
        built.expectation.accountIdsBase58.size(),
        input.tracked.plan.accountIdsHex.size());
    LOGOS_ASSERT_EQ(
        built.expectation.accountExpectations.size(),
        input.tracked.plan.accountIdsHex.size());
    LOGOS_ASSERT_EQ(
        built.expectation.accountExpectations[0]
            .expectedDataSha256Hex,
        input.tracked.expectedRootDataSha256Hex);
    LOGOS_ASSERT_TRUE(
        built.expectation.accountExpectations[1]
            .expectedData.empty());
}

LOGOS_TEST(lez_finality_expectation_rejects_races_owner_and_root_confusion) {
    using namespace palace;

    Fixture raced = fixture();
    raced.accounts.heightAfter = 701;
    LOGOS_ASSERT_EQ(
        buildPalaceLezFinalityExpectationV1(
            raced.tracked, raced.accounts).reason,
        std::string("unstable-account-snapshot"));

    Fixture missing = fixture();
    missing.accounts.accountResponseJson.pop_back();
    LOGOS_ASSERT_EQ(
        buildPalaceLezFinalityExpectationV1(
            missing.tracked, missing.accounts).reason,
        std::string("invalid-observed-transaction"));

    Fixture wrongOwner = fixture();
    wrongOwner.accounts.accountResponseJson[2] =
        accountJson(std::string(64U, '0'), "00");
    LOGOS_ASSERT_EQ(
        buildPalaceLezFinalityExpectationV1(
            wrongOwner.tracked, wrongOwner.accounts).reason,
        std::string("account-program-owner-mismatch"));

    Fixture wrongRoot = fixture();
    wrongRoot.accounts.accountResponseJson[0] =
        accountJson(kProgramId, "00");
    LOGOS_ASSERT_EQ(
        buildPalaceLezFinalityExpectationV1(
            wrongRoot.tracked, wrongRoot.accounts).reason,
        std::string("root-post-state-mismatch"));

    Fixture duplicateJson = fixture();
    duplicateJson.accounts.accountResponseJson[1] =
        "{\"program_owner\":\""
        + std::string(64U, '0')
        + "\",\"progra\\u006d_owner\":\""
        + std::string(64U, '0')
        + "\",\"balance\":\"00000000000000000000000000000000\","
          "\"nonce\":\"00000000000000000000000000000000\","
          "\"data\":\"\"}";
    LOGOS_ASSERT_EQ(
        buildPalaceLezFinalityExpectationV1(
            duplicateJson.tracked,
            duplicateJson.accounts).reason,
        std::string(
            "invalid-account-snapshot:duplicate-json-key"));
}
