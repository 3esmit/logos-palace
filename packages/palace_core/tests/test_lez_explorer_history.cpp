#include <logos_test.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "palace_lez.h"
#include "palace_lez_explorer_history.h"

namespace {

constexpr const char* kProgramIdHex =
    "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";
constexpr const char* kRootAccountIdHex =
    "99e683e8adac1e4f591b42879ae3e6480414f644c3eda3b592da3d62d72853b3";
constexpr const char* kSignerHex =
    "6666666666666666666666666666666666666666666666666666666666666666";
constexpr const char* kOrigin = "https://explorer.example.test";
constexpr const char* kPublicKeyBase64 =
    "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE=";

std::string repeated(const char value, const std::size_t count)
{
    return std::string(count, value);
}

palace::PalaceLezBytes32 bytes(const std::uint8_t value)
{
    palace::PalaceLezBytes32 output{};
    output.fill(value);
    return output;
}

std::string base58(const std::string& accountIdHex)
{
    palace::PalaceLezBytes32 accountId{};
    LOGOS_ASSERT_TRUE(
        palace::PalaceLezCodec::parseBytes32Hex(
            accountIdHex,
            accountId));
    return palace::PalaceLezCodec::accountIdBase58(accountId);
}

palace::PalaceLezInstructionV3 initializeInstruction()
{
    palace::PalaceLezInitializeV3 initialize;
    initialize.palaceId = bytes(0x10U);
    initialize.title = "Palace";
    initialize.activeManifestCid = "bafypalacemanifest";
    initialize.ownerProfile = {
        "Alice",
        bytes(0x51U),
        1U,
        std::string("bafyavatar"),
    };
    initialize.ownerGrantId = bytes(0x11U);
    initialize.entryRoomId = bytes(0x31U);
    initialize.entryRoom = {
        "Atrium",
        "bafyatrium",
        "bafyatriumscript",
        palace::PalaceLezVmProfileV3::IptScraeMvpV1,
    };
    initialize.secondaryRoomId = bytes(0x32U);
    initialize.secondaryRoom = {
        "Lounge",
        "bafylounge",
        "bafyloungescript",
        palace::PalaceLezVmProfileV3::IptScraeMvpV1,
    };
    return palace::PalaceLezInstructionV3{initialize};
}

palace::PalaceLezInstructionV3 actionOneInstruction()
{
    return palace::PalaceLezInstructionV3{
        palace::PalaceLezRegisterUserV3{
            1U,
            {"Bob", bytes(0x52U), 1U, std::nullopt},
        }};
}

palace::PalaceLezInstructionV3 actionTwoInstruction()
{
    return palace::PalaceLezInstructionV3{
        palace::PalaceLezPublishManifestV3{
            2U,
            "bafysecondmanifest",
        }};
}

palace::PalaceLezTransactionPlanV3 plan(
    const palace::PalaceLezInstructionV3& instruction)
{
    const auto result = palace::PalaceLezCodec::buildTransaction(
        kProgramIdHex,
        kSignerHex,
        instruction);
    LOGOS_ASSERT_TRUE(result.accepted);
    return result;
}

std::string publicTransaction(
    const palace::PalaceLezTransactionPlanV3& transactionPlan,
    const std::string& hash,
    const std::size_t signatureCount = 1U,
    const std::optional<std::string>& programIdBase58 = std::nullopt,
    const std::optional<std::vector<std::string>>& accountIdsBase58 =
        std::nullopt)
{
    std::vector<std::string> accounts;
    if (accountIdsBase58.has_value()) {
        accounts = *accountIdsBase58;
    } else {
        for (const std::string& accountId :
             transactionPlan.accountIdsHex) {
            accounts.push_back(base58(accountId));
        }
    }

    std::string accountJson;
    std::string nonceJson;
    for (std::size_t index = 0U; index < accounts.size(); ++index) {
        if (index != 0U) {
            accountJson += ',';
            nonceJson += ',';
        }
        accountJson += '"' + accounts[index] + '"';
        nonceJson += std::to_string(index);
    }
    std::string wordsJson;
    for (const std::uint32_t word : transactionPlan.instructionWords) {
        if (!wordsJson.empty())
            wordsJson += ',';
        wordsJson += std::to_string(word);
    }
    std::string signaturesJson;
    for (std::size_t index = 0U; index < signatureCount; ++index) {
        if (!signaturesJson.empty())
            signaturesJson += ',';
        signaturesJson += "[\"" + repeated(
            static_cast<char>('1' + index),
            128U)
            + "\",\"" + kPublicKeyBase64 + "\"]";
    }
    const std::string program = programIdBase58.has_value()
        ? *programIdBase58
        : base58(transactionPlan.programIdHex);
    return "{\"Public\":{\"hash\":\"" + hash
        + "\",\"message\":{\"program_id\":\"" + program
        + "\",\"account_ids\":[" + accountJson
        + "],\"nonces\":[" + nonceJson
        + "],\"instruction_data\":[" + wordsJson
        + "]},\"witness_set\":{\"signatures_and_public_keys\":["
        + signaturesJson + "],\"proof\":null}}}";
}

std::string deploymentTransaction(const std::string& hash)
{
    return "{\"ProgramDeployment\":{\"hash\":\"" + hash
        + "\",\"message\":{\"bytecode\":\"AQID\"}}}";
}

std::string block(
    const std::uint64_t id,
    const char hashDigit,
    const char previousHashDigit,
    const std::string& status,
    const std::vector<std::string>& transactions)
{
    std::string transactionJson;
    for (const std::string& transaction : transactions) {
        if (!transactionJson.empty())
            transactionJson += ',';
        transactionJson += transaction;
    }
    return "{\"header\":{\"block_id\":" + std::to_string(id)
        + ",\"prev_block_hash\":\"" + repeated(previousHashDigit, 64U)
        + "\",\"hash\":\"" + repeated(hashDigit, 64U)
        + "\",\"timestamp\":" + std::to_string(id * 1000U)
        + ",\"signature\":\"" + repeated('c', 128U)
        + "\"},\"body\":{\"transactions\":[" + transactionJson
        + "]},\"bedrock_status\":\"" + status + "\"}";
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

palace::PalaceLezExplorerHistoryExpectationV1 expectation()
{
    return {kProgramIdHex, kRootAccountIdHex};
}

palace::PalaceLezExplorerHttpResponseV1 response(
    const palace::PalaceLezExplorerCommandV1& command,
    std::string body)
{
    palace::PalaceLezExplorerHttpResponseV1 result;
    result.commandSequence = command.sequence;
    result.statusCode = 200;
    result.contentType = "application/json";
    result.effectiveOrigin = kOrigin;
    result.body = std::move(body);
    return result;
}

palace::PalaceLezExplorerCommandV1 requireCommand(
    palace::PalaceLezExplorerHistorySession& session)
{
    const auto command = session.takeNextCommand();
    LOGOS_ASSERT_TRUE(command.has_value());
    return *command;
}

void start(
    palace::PalaceLezExplorerHistorySession& session,
    const palace::PalaceLezExplorerFinalityLimitsV1& configuredLimits =
        limits())
{
    const auto update =
        session.start(fingerprint(), configuredLimits, expectation());
    LOGOS_ASSERT_TRUE(update.accepted);
    LOGOS_ASSERT_TRUE(
        update.outcome
        == palace::PalaceLezExplorerHistoryOutcome::Pending);
}

std::vector<std::string> expectedUniqueAccounts(
    const std::vector<palace::PalaceLezTransactionPlanV3>& plans)
{
    std::set<std::string> seen;
    std::vector<std::string> result;
    for (const auto& transactionPlan : plans) {
        for (const std::string& accountId :
             transactionPlan.accountIdsHex) {
            if (seen.insert(accountId).second)
                result.push_back(accountId);
        }
    }
    return result;
}

palace::PalaceLezExplorerHistoryResultV1 rebuildHappyHistory()
{
    const auto initializePlan = plan(initializeInstruction());
    const auto actionOnePlan = plan(actionOneInstruction());
    const auto actionTwoPlan = plan(actionTwoInstruction());

    palace::PalaceLezBytes32 otherProgram{};
    otherProgram.fill(0x77U);
    const std::string unrelatedProgram =
        palace::PalaceLezCodec::accountIdBase58(otherProgram);
    const std::vector<std::string> rootReference = {
        base58(kRootAccountIdHex),
        base58(kSignerHex),
    };

    palace::PalaceLezExplorerHistorySession session;
    start(session);
    auto command = requireCommand(session);
    LOGOS_ASSERT_EQ(command.formBody, "limit=2");
    auto update = session.acceptResponse(response(
        command,
        page({
            block(
                12U,
                'c',
                'b',
                "Finalized",
                {
                    publicTransaction(
                        actionTwoPlan,
                        repeated('8', 64U),
                        1U,
                        unrelatedProgram,
                        rootReference),
                    publicTransaction(
                        actionTwoPlan,
                        repeated('3', 64U)),
                }),
            block(
                11U,
                'b',
                'a',
                "Finalized",
                {
                    publicTransaction(
                        actionOnePlan,
                        repeated('2', 64U)),
                }),
        })));
    LOGOS_ASSERT_TRUE(update.accepted);
    LOGOS_ASSERT_TRUE(
        update.outcome
        == palace::PalaceLezExplorerHistoryOutcome::Pending);
    LOGOS_ASSERT_FALSE(session.rebuildResult().has_value());

    command = requireCommand(session);
    LOGOS_ASSERT_EQ(command.formBody, "limit=2&before=11");
    update = session.acceptResponse(response(
        command,
        page({
            block(
                10U,
                'a',
                '9',
                "Finalized",
                {
                    deploymentTransaction(repeated('9', 64U)),
                    publicTransaction(
                        initializePlan,
                        repeated('1', 64U)),
                }),
        })));
    LOGOS_ASSERT_TRUE(update.accepted);
    LOGOS_ASSERT_TRUE(
        update.outcome
        == palace::PalaceLezExplorerHistoryOutcome::Rebuilt);
    const auto rebuilt = session.rebuildResult();
    LOGOS_ASSERT_TRUE(rebuilt.has_value());
    LOGOS_ASSERT_FALSE(session.takeNextCommand().has_value());
    return *rebuilt;
}

palace::PalaceLezExplorerHistoryOutcome runSinglePage(
    const std::vector<std::string>& transactions,
    const std::string& status = "Finalized")
{
    palace::PalaceLezExplorerHistorySession session;
    start(session);
    const auto command = requireCommand(session);
    return session.acceptResponse(response(
        command,
        page({block(10U, 'a', '9', status, transactions)}))).outcome;
}

} // namespace

LOGOS_TEST(lez_explorer_history_rebuilds_paginated_finalized_history_stably)
{
    const auto first = rebuildHappyHistory();
    const auto second = rebuildHappyHistory();
    const std::vector<palace::PalaceLezTransactionPlanV3> plans = {
        plan(initializeInstruction()),
        plan(actionOneInstruction()),
        plan(actionTwoInstruction()),
    };

    LOGOS_ASSERT_EQ(first.resultVersion, 1U);
    LOGOS_ASSERT_EQ(first.actions.size(), 3U);
    LOGOS_ASSERT_EQ(first.actions[0].orderedActionId, 0U);
    LOGOS_ASSERT_EQ(first.actions[1].orderedActionId, 1U);
    LOGOS_ASSERT_EQ(first.actions[2].orderedActionId, 2U);
    LOGOS_ASSERT_EQ(first.actions[0].transactionHash, repeated('1', 64U));
    LOGOS_ASSERT_EQ(first.actions[1].transactionHash, repeated('2', 64U));
    LOGOS_ASSERT_EQ(first.actions[2].transactionHash, repeated('3', 64U));
    LOGOS_ASSERT_TRUE(
        first.uniqueAccountIdsHex
        == expectedUniqueAccounts(plans));
    LOGOS_ASSERT_EQ(first.latestFinalizedBlockId, 12U);
    LOGOS_ASSERT_EQ(
        first.latestFinalizedBlockHashHex,
        repeated('c', 64U));

    LOGOS_ASSERT_EQ(second.actions.size(), first.actions.size());
    for (std::size_t index = 0U; index < first.actions.size(); ++index) {
        LOGOS_ASSERT_EQ(
            second.actions[index].transactionHash,
            first.actions[index].transactionHash);
        LOGOS_ASSERT_EQ(
            second.actions[index].orderedActionId,
            first.actions[index].orderedActionId);
        LOGOS_ASSERT_TRUE(
            second.actions[index].accountIdsHex
            == first.actions[index].accountIdsHex);
    }
    LOGOS_ASSERT_TRUE(
        second.uniqueAccountIdsHex
        == first.uniqueAccountIdsHex);
    LOGOS_ASSERT_EQ(
        second.latestFinalizedBlockHashHex,
        first.latestFinalizedBlockHashHex);
}

LOGOS_TEST(lez_explorer_history_rejects_gaps_duplicates_and_reordered_accounts)
{
    const auto initializePlan = plan(initializeInstruction());
    const auto actionOnePlan = plan(actionOneInstruction());
    const auto actionTwoPlan = plan(actionTwoInstruction());
    LOGOS_ASSERT_TRUE(
        runSinglePage({
            publicTransaction(initializePlan, repeated('1', 64U)),
            publicTransaction(actionTwoPlan, repeated('3', 64U)),
        }) == palace::PalaceLezExplorerHistoryOutcome::Rejected);
    LOGOS_ASSERT_TRUE(
        runSinglePage({
            publicTransaction(initializePlan, repeated('1', 64U)),
            publicTransaction(actionOnePlan, repeated('2', 64U)),
            publicTransaction(actionOnePlan, repeated('3', 64U)),
        }) == palace::PalaceLezExplorerHistoryOutcome::Rejected);

    std::vector<std::string> reordered;
    for (const std::string& accountId : actionOnePlan.accountIdsHex)
        reordered.push_back(base58(accountId));
    std::swap(reordered[1], reordered[2]);
    LOGOS_ASSERT_TRUE(
        runSinglePage({
            publicTransaction(initializePlan, repeated('1', 64U)),
            publicTransaction(
                actionOnePlan,
                repeated('2', 64U),
                1U,
                std::nullopt,
                reordered),
        }) == palace::PalaceLezExplorerHistoryOutcome::Rejected);
}

LOGOS_TEST(lez_explorer_history_rejects_signer_hash_finality_and_chain_failures)
{
    const auto initializePlan = plan(initializeInstruction());
    const auto actionOnePlan = plan(actionOneInstruction());
    LOGOS_ASSERT_TRUE(
        runSinglePage({
            publicTransaction(
                initializePlan,
                repeated('1', 64U),
                0U),
        }) == palace::PalaceLezExplorerHistoryOutcome::Rejected);
    LOGOS_ASSERT_TRUE(
        runSinglePage({
            publicTransaction(
                initializePlan,
                repeated('1', 64U),
                2U),
        }) == palace::PalaceLezExplorerHistoryOutcome::Rejected);
    LOGOS_ASSERT_TRUE(
        runSinglePage(
            {publicTransaction(initializePlan, repeated('1', 64U))},
            "Safe")
        == palace::PalaceLezExplorerHistoryOutcome::Rejected);
    LOGOS_ASSERT_TRUE(
        runSinglePage({
            publicTransaction(initializePlan, repeated('1', 64U)),
            publicTransaction(actionOnePlan, repeated('1', 64U)),
        }) == palace::PalaceLezExplorerHistoryOutcome::Rejected);

    palace::PalaceLezExplorerHistorySession brokenChain;
    start(brokenChain);
    const auto command = requireCommand(brokenChain);
    const auto update = brokenChain.acceptResponse(response(
        command,
        page({
            block(12U, 'c', 'd', "Finalized", {}),
            block(
                11U,
                'b',
                'a',
                "Finalized",
                {publicTransaction(
                    initializePlan,
                    repeated('1', 64U))}),
        })));
    LOGOS_ASSERT_TRUE(
        update.outcome
        == palace::PalaceLezExplorerHistoryOutcome::Rejected);
    LOGOS_ASSERT_EQ(update.reason, "block-page-chain-order-mismatch");
}

LOGOS_TEST(lez_explorer_history_rejects_duplicate_json_and_transport_confusion)
{
    const auto initializePlan = plan(initializeInstruction());
    std::string valid = page({
        block(
            10U,
            'a',
            '9',
            "Finalized",
            {publicTransaction(
                initializePlan,
                repeated('1', 64U))}),
    });
    valid.insert(2U, "\"\\u0068eader\":{},");

    palace::PalaceLezExplorerHistorySession duplicateJson;
    start(duplicateJson);
    auto command = requireCommand(duplicateJson);
    auto update =
        duplicateJson.acceptResponse(response(command, valid));
    LOGOS_ASSERT_TRUE(
        update.outcome
        == palace::PalaceLezExplorerHistoryOutcome::Degraded);
    LOGOS_ASSERT_EQ(update.reason, "duplicate-json-key");
    LOGOS_ASSERT_FALSE(duplicateJson.rebuildResult().has_value());

    palace::PalaceLezExplorerHistorySession correlation;
    start(correlation);
    command = requireCommand(correlation);
    auto confusedResponse = response(command, "[]");
    ++confusedResponse.commandSequence;
    update = correlation.acceptResponse(confusedResponse);
    LOGOS_ASSERT_TRUE(
        update.outcome
        == palace::PalaceLezExplorerHistoryOutcome::Rejected);

    palace::PalaceLezExplorerHistorySession metadata;
    start(metadata);
    command = requireCommand(metadata);
    auto wrongOrigin = response(command, "[]");
    wrongOrigin.effectiveOrigin = "https://attacker.invalid";
    update = metadata.acceptResponse(wrongOrigin);
    LOGOS_ASSERT_TRUE(
        update.outcome
        == palace::PalaceLezExplorerHistoryOutcome::Degraded);
}

LOGOS_TEST(lez_explorer_history_never_returns_partial_history_without_initialize)
{
    auto configuredLimits = limits();
    configuredLimits.maxPages = 1U;
    palace::PalaceLezExplorerHistorySession session;
    start(session, configuredLimits);
    const auto command = requireCommand(session);
    const auto update = session.acceptResponse(response(
        command,
        page({
            block(
                11U,
                'b',
                'a',
                "Finalized",
                {publicTransaction(
                    plan(actionOneInstruction()),
                    repeated('2', 64U))}),
        })));
    LOGOS_ASSERT_TRUE(update.accepted);
    LOGOS_ASSERT_TRUE(
        update.outcome
        == palace::PalaceLezExplorerHistoryOutcome::Pending);
    LOGOS_ASSERT_EQ(update.reason, "history-scan-window-exhausted");
    LOGOS_ASSERT_TRUE(session.scanExhausted());
    LOGOS_ASSERT_FALSE(session.rebuildResult().has_value());
    LOGOS_ASSERT_FALSE(session.takeNextCommand().has_value());
}
