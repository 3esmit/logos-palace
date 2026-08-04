#include <logos_test.h>

#include <string>
#include <vector>

#include "palace_lez.h"
#include "palace_lez_explorer_submission_recovery.h"

namespace {

constexpr const char* kProgramIdHex =
    "dcbbfebcd59399961ed9973b8307dc475fd4c5ca5779aacfe7588f7dbc3f4a71";
constexpr const char* kAccountIdHex =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr const char* kOrigin = "https://explorer.example.test";
constexpr const char* kPublicKeyBase64 =
    "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE=";

std::string repeated(const char value, const std::size_t count)
{
    return std::string(count, value);
}

std::string base58(const std::string& hex)
{
    palace::PalaceLezBytes32 bytes{};
    LOGOS_ASSERT_TRUE(
        palace::PalaceLezCodec::parseBytes32Hex(hex, bytes));
    return palace::PalaceLezCodec::accountIdBase58(bytes);
}

std::string transaction(const char hashDigit)
{
    return "{\"Public\":{\"hash\":\""
        + repeated(hashDigit, 64U)
        + "\",\"message\":{\"program_id\":\""
        + base58(kProgramIdHex)
        + "\",\"account_ids\":[\"" + base58(kAccountIdHex)
        + "\"],\"nonces\":[0],\"instruction_data\":[1]},"
          "\"witness_set\":{\"signatures_and_public_keys\":[[\""
        + repeated('1', 128U) + "\",\"" + kPublicKeyBase64
        + "\"]],\"proof\":null}}}";
}

std::string block(
    const std::uint64_t id,
    const char hashDigit,
    const char previousHashDigit,
    const std::vector<std::string>& transactions)
{
    std::string transactionJson;
    for (const std::string& value : transactions) {
        if (!transactionJson.empty())
            transactionJson += ',';
        transactionJson += value;
    }
    return "{\"header\":{\"block_id\":" + std::to_string(id)
        + ",\"prev_block_hash\":\""
        + repeated(previousHashDigit, 64U)
        + "\",\"hash\":\"" + repeated(hashDigit, 64U)
        + "\",\"timestamp\":" + std::to_string(id * 1000U)
        + ",\"signature\":\"" + repeated('c', 128U)
        + "\"},\"body\":{\"transactions\":[" + transactionJson
        + "]},\"bedrock_status\":\"Finalized\"}";
}

std::string page(const std::vector<std::string>& blocks)
{
    std::string output = "[";
    for (const std::string& value : blocks) {
        if (output.size() != 1U)
            output += ',';
        output += value;
    }
    return output + ']';
}

palace::PalaceLezExplorerNetworkFingerprintV1 fingerprint()
{
    return {
        1U,
        "logos-lez-testnet-v0.2.0",
        repeated('1', 64U),
        kOrigin,
        "3022937127152978530",
        1U,
    };
}

palace::PalaceLezExplorerFinalityLimitsV1 limits()
{
    palace::PalaceLezExplorerFinalityLimitsV1 value;
    value.pageSize = 2U;
    value.maxPages = 4U;
    value.maxBlocks = 8U;
    value.maxTransactionsPerBlock = 16U;
    value.maxAccounts = 8U;
    value.maxInstructionWords = 1024U;
    value.maxJsonBytes = 1024U * 1024U;
    value.maxAccountDataBytes = 4096U;
    value.maxJsonDepth = 24U;
    value.maxJsonNodes = 20000U;
    return value;
}

palace::PalaceLezSubmissionRecoveryExpectationV1 expectation()
{
    return {
        kProgramIdHex,
        {kAccountIdHex},
        {1U},
        1U,
        10U,
    };
}

palace::PalaceLezExplorerHttpResponseV1 response(
    const palace::PalaceLezExplorerCommandV1& command,
    std::string body)
{
    palace::PalaceLezExplorerHttpResponseV1 value;
    value.commandSequence = command.sequence;
    value.statusCode = 200;
    value.contentType = "application/json";
    value.effectiveOrigin = kOrigin;
    value.body = std::move(body);
    return value;
}

palace::PalaceLezExplorerCommandV1 command(
    palace::PalaceLezExplorerSubmissionRecoverySession& session)
{
    const auto value = session.takeNextCommand();
    LOGOS_ASSERT_TRUE(value.has_value());
    return *value;
}

} // namespace

LOGOS_TEST(
    lez_submission_recovery_returns_only_one_exact_post_checkpoint_match)
{
    palace::PalaceLezExplorerSubmissionRecoverySession session;
    const auto started =
        session.start(fingerprint(), limits(), expectation());
    LOGOS_ASSERT_TRUE(started.accepted);

    auto next = command(session);
    auto update = session.acceptResponse(response(
        next,
        page({
            block(12U, 'c', 'b', {transaction('a')}),
            block(11U, 'b', 'a', {}),
        })));
    LOGOS_ASSERT_TRUE(update.accepted);
    LOGOS_ASSERT_TRUE(
        update.outcome
        == palace::PalaceLezSubmissionRecoveryOutcome::Pending);
    LOGOS_ASSERT_FALSE(session.result().has_value());

    next = command(session);
    update = session.acceptResponse(response(
        next, page({block(10U, 'a', '9', {})})));
    LOGOS_ASSERT_TRUE(update.accepted);
    LOGOS_ASSERT_TRUE(
        update.outcome
        == palace::PalaceLezSubmissionRecoveryOutcome::Found);
    const auto recovered = session.result();
    LOGOS_ASSERT_TRUE(recovered.has_value());
    LOGOS_ASSERT_EQ(recovered->transactionHash, repeated('a', 64U));
    LOGOS_ASSERT_EQ(recovered->finalizedBlockId, 12U);
}

LOGOS_TEST(
    lez_submission_recovery_fails_closed_on_multiple_exact_matches)
{
    palace::PalaceLezExplorerSubmissionRecoverySession session;
    LOGOS_ASSERT_TRUE(
        session.start(fingerprint(), limits(), expectation()).accepted);
    const auto next = command(session);
    const auto update = session.acceptResponse(response(
        next,
        page({
            block(12U, 'c', 'b', {transaction('a')}),
            block(11U, 'b', 'a', {transaction('b')}),
        })));
    LOGOS_ASSERT_TRUE(
        update.outcome
        == palace::PalaceLezSubmissionRecoveryOutcome::Rejected);
    LOGOS_ASSERT_EQ(
        update.reason,
        std::string("multiple-finalized-submission-matches"));
    LOGOS_ASSERT_FALSE(session.result().has_value());
}

LOGOS_TEST(
    lez_submission_recovery_does_not_match_checkpoint_or_wrong_tuple)
{
    palace::PalaceLezExplorerSubmissionRecoverySession session;
    LOGOS_ASSERT_TRUE(
        session.start(fingerprint(), limits(), expectation()).accepted);
    const auto next = command(session);
    const auto update = session.acceptResponse(response(
        next,
        page({block(10U, 'a', '9', {transaction('a')})})));
    LOGOS_ASSERT_TRUE(update.accepted);
    LOGOS_ASSERT_TRUE(
        update.outcome
        == palace::PalaceLezSubmissionRecoveryOutcome::NotFound);
    LOGOS_ASSERT_FALSE(session.result().has_value());
}
