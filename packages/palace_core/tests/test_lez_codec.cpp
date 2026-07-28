#include <logos_test.h>

#include "palace_lez.h"

namespace {

constexpr const char* kStateAccount =
    "1111111111111111111111111111111111111111111111111111111111111111";
constexpr const char* kCallerAccount =
    "2222222222222222222222222222222222222222222222222222222222222222";
constexpr const char* kProgramId =
    "3333333333333333333333333333333333333333333333333333333333333333";

palace::PalaceLezSubmitRequestV1 request(palace::PalaceLezInstructionKind kind)
{
    palace::PalaceLezSubmitRequestV1 value;
    value.orderedActionId = 1U;
    value.stateAccountIdHex = kStateAccount;
    value.callerAccountIdHex = kCallerAccount;
    value.programIdHex = kProgramId;
    value.instruction.kind = kind;
    return value;
}

} // namespace

LOGOS_TEST(palace_lez_codec_encodes_apply_using_risc0_word_order) {
    auto value = request(palace::PalaceLezInstructionKind::PublishManifest);
    value.orderedActionId = 0x0000000200000001ULL;
    value.instruction.cid = "bafy";

    const palace::PalaceLezWireInstruction encoded = palace::PalaceLezCodec::encodeApply(value);
    LOGOS_ASSERT_TRUE(encoded.accepted);
    LOGOS_ASSERT_EQ(encoded.words.size(), 6U);
    LOGOS_ASSERT_EQ(encoded.words.at(0), 1U);
    LOGOS_ASSERT_EQ(encoded.words.at(1), 1U);
    LOGOS_ASSERT_EQ(encoded.words.at(2), 2U);
    LOGOS_ASSERT_EQ(encoded.words.at(3), 6U);
    LOGOS_ASSERT_EQ(encoded.words.at(4), 4U);
    LOGOS_ASSERT_EQ(encoded.words.at(5), 0x79666162U);
}

LOGOS_TEST(palace_lez_codec_preserves_fixed_array_and_u64_encoding) {
    auto value = request(palace::PalaceLezInstructionKind::BindDeliveryKey);
    value.instruction.subjectAccountIdHex = kCallerAccount;
    value.instruction.deliveryKeyHex = kProgramId;
    value.instruction.keyEpoch = 9U;

    const palace::PalaceLezWireInstruction encoded = palace::PalaceLezCodec::encodeApply(value);
    LOGOS_ASSERT_TRUE(encoded.accepted);
    LOGOS_ASSERT_EQ(encoded.words.size(), 70U);
    LOGOS_ASSERT_EQ(encoded.words.at(0), 1U);
    LOGOS_ASSERT_EQ(encoded.words.at(1), 1U);
    LOGOS_ASSERT_EQ(encoded.words.at(2), 0U);
    LOGOS_ASSERT_EQ(encoded.words.at(3), 0U);
    LOGOS_ASSERT_EQ(encoded.words.at(4), 0x22U);
    LOGOS_ASSERT_EQ(encoded.words.at(35), 0x22U);
    LOGOS_ASSERT_EQ(encoded.words.at(36), 0x33U);
    LOGOS_ASSERT_EQ(encoded.words.at(67), 0x33U);
    LOGOS_ASSERT_EQ(encoded.words.at(68), 9U);
    LOGOS_ASSERT_EQ(encoded.words.at(69), 0U);
}

LOGOS_TEST(palace_lez_codec_rejects_untrusted_ids_and_invalid_payloads) {
    auto value = request(palace::PalaceLezInstructionKind::PublishManifest);
    value.instruction.cid = "bafy";
    value.callerAccountIdHex = "not-a-valid-account";
    LOGOS_ASSERT_FALSE(palace::PalaceLezCodec::encodeApply(value).accepted);

    value.callerAccountIdHex = kCallerAccount;
    value.instruction.cid = "bad-cid!";
    LOGOS_ASSERT_FALSE(palace::PalaceLezCodec::encodeApply(value).accepted);

    value.instruction.cid = "bafy";
    value.orderedActionId = 0;
    const palace::PalaceLezWireInstruction invalidAction =
        palace::PalaceLezCodec::encodeApply(value);
    LOGOS_ASSERT_FALSE(invalidAction.accepted);
    LOGOS_ASSERT_EQ(invalidAction.reason, "invalid-ordered-action-id");
}

LOGOS_TEST(palace_lez_codec_parses_only_canonical_positive_ordered_action_ids) {
    std::uint64_t orderedActionId = 0;
    LOGOS_ASSERT_TRUE(palace::PalaceLezCodec::parseOrderedActionId("1", orderedActionId));
    LOGOS_ASSERT_EQ(orderedActionId, 1U);
    LOGOS_ASSERT_TRUE(palace::PalaceLezCodec::parseOrderedActionId(
        "18446744073709551615", orderedActionId));
    LOGOS_ASSERT_EQ(orderedActionId, UINT64_MAX);

    LOGOS_ASSERT_FALSE(palace::PalaceLezCodec::parseOrderedActionId("", orderedActionId));
    LOGOS_ASSERT_FALSE(palace::PalaceLezCodec::parseOrderedActionId("0", orderedActionId));
    LOGOS_ASSERT_FALSE(palace::PalaceLezCodec::parseOrderedActionId("00", orderedActionId));
    LOGOS_ASSERT_FALSE(palace::PalaceLezCodec::parseOrderedActionId("01", orderedActionId));
    LOGOS_ASSERT_FALSE(palace::PalaceLezCodec::parseOrderedActionId("+1", orderedActionId));
    LOGOS_ASSERT_FALSE(palace::PalaceLezCodec::parseOrderedActionId("-1", orderedActionId));
    LOGOS_ASSERT_FALSE(palace::PalaceLezCodec::parseOrderedActionId(" 1", orderedActionId));
    LOGOS_ASSERT_FALSE(palace::PalaceLezCodec::parseOrderedActionId("1 ", orderedActionId));
    LOGOS_ASSERT_FALSE(palace::PalaceLezCodec::parseOrderedActionId(
        "18446744073709551616", orderedActionId));
}

LOGOS_TEST(palace_lez_codec_requires_success_and_a_canonical_transaction_hash) {
    const std::string hash(64U, 'a');
    const palace::PalaceLezSubmissionResult accepted = palace::PalaceLezCodec::parseSubmissionResult(
        "{\"success\":true,\"tx_hash\":\"" + hash + "\",\"secrets\":[],\"error\":\"\"}");
    LOGOS_ASSERT_TRUE(accepted.accepted);
    LOGOS_ASSERT_EQ(accepted.transactionHash, hash);

    LOGOS_ASSERT_FALSE(palace::PalaceLezCodec::parseSubmissionResult(
        "{\"success\":true,\"tx_hash\":\"short\",\"error\":\"\"}").accepted);
    LOGOS_ASSERT_FALSE(palace::PalaceLezCodec::parseSubmissionResult(
        "{\"success\":false,\"tx_hash\":\"\",\"error\":\"rejected\"}").accepted);
}
