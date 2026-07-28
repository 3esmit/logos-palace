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
    value.stateAccountIdHex = kStateAccount;
    value.callerAccountIdHex = kCallerAccount;
    value.programIdHex = kProgramId;
    value.instruction.kind = kind;
    return value;
}

} // namespace

LOGOS_TEST(palace_lez_codec_encodes_apply_using_risc0_word_order) {
    auto value = request(palace::PalaceLezInstructionKind::PublishManifest);
    value.instruction.cid = "bafy";

    const palace::PalaceLezWireInstruction encoded = palace::PalaceLezCodec::encodeApply(value);
    LOGOS_ASSERT_TRUE(encoded.accepted);
    LOGOS_ASSERT_EQ(encoded.words.size(), 4U);
    LOGOS_ASSERT_EQ(encoded.words.at(0), 1U);
    LOGOS_ASSERT_EQ(encoded.words.at(1), 6U);
    LOGOS_ASSERT_EQ(encoded.words.at(2), 4U);
    LOGOS_ASSERT_EQ(encoded.words.at(3), 0x79666162U);
}

LOGOS_TEST(palace_lez_codec_preserves_fixed_array_and_u64_encoding) {
    auto value = request(palace::PalaceLezInstructionKind::BindDeliveryKey);
    value.instruction.subjectAccountIdHex = kCallerAccount;
    value.instruction.deliveryKeyHex = kProgramId;
    value.instruction.keyEpoch = 9U;

    const palace::PalaceLezWireInstruction encoded = palace::PalaceLezCodec::encodeApply(value);
    LOGOS_ASSERT_TRUE(encoded.accepted);
    LOGOS_ASSERT_EQ(encoded.words.size(), 68U);
    LOGOS_ASSERT_EQ(encoded.words.at(0), 1U);
    LOGOS_ASSERT_EQ(encoded.words.at(1), 0U);
    LOGOS_ASSERT_EQ(encoded.words.at(2), 0x22U);
    LOGOS_ASSERT_EQ(encoded.words.at(33), 0x22U);
    LOGOS_ASSERT_EQ(encoded.words.at(34), 0x33U);
    LOGOS_ASSERT_EQ(encoded.words.at(65), 0x33U);
    LOGOS_ASSERT_EQ(encoded.words.at(66), 9U);
    LOGOS_ASSERT_EQ(encoded.words.at(67), 0U);
}

LOGOS_TEST(palace_lez_codec_rejects_untrusted_ids_and_invalid_payloads) {
    auto value = request(palace::PalaceLezInstructionKind::PublishManifest);
    value.instruction.cid = "bafy";
    value.callerAccountIdHex = "not-a-valid-account";
    LOGOS_ASSERT_FALSE(palace::PalaceLezCodec::encodeApply(value).accepted);

    value.callerAccountIdHex = kCallerAccount;
    value.instruction.cid = "bad-cid!";
    LOGOS_ASSERT_FALSE(palace::PalaceLezCodec::encodeApply(value).accepted);
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
