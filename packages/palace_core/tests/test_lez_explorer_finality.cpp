#include <logos_test.h>

#include "palace_lez.h"
#include "palace_lez_explorer_finality.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char* kProgramId =
    "AB5o45vJVuFkyDhiK13QRfsxMuxsiz44GMmpr2Xfjm4N";
constexpr const char* kAccountA =
    "4BdcjoXkq786TMWcBGGHqcxeLYMZmn17rL4eM9ZyRWNU";
constexpr const char* kAccountB =
    "4BdcjoXkq786TMWcBGGHqcxeLYMZmn17rL4eM9ZyRWSs";
constexpr const char* kTargetHash =
    "b538bf42bfdbcbc7fdc1f6927c0d605f74d22be9564a8dc9047052f1eb0aca19";
constexpr const char* kOtherHash =
    "fd21ed632670697d7f413e69e1f9eb021615ca87cd0dec48e169c4613b1d5f0d";
constexpr const char* kSuffix = "3022937127152978530";
constexpr const char* kOrigin = "https://explorer.testnet.lez.logos.co";

std::string repeated(const char value, const std::size_t count)
{
    return std::string(count, value);
}

std::string base64(const std::vector<std::uint8_t>& bytes)
{
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    for (std::size_t index = 0U; index < bytes.size(); index += 3U) {
        const std::uint32_t first = bytes[index];
        const std::uint32_t second =
            index + 1U < bytes.size() ? bytes[index + 1U] : 0U;
        const std::uint32_t third =
            index + 2U < bytes.size() ? bytes[index + 2U] : 0U;
        const std::uint32_t value =
            (first << 16U) | (second << 8U) | third;
        output.push_back(alphabet[(value >> 18U) & 0x3fU]);
        output.push_back(alphabet[(value >> 12U) & 0x3fU]);
        output.push_back(
            index + 1U < bytes.size()
                ? alphabet[(value >> 6U) & 0x3fU]
                : '=');
        output.push_back(
            index + 2U < bytes.size()
                ? alphabet[value & 0x3fU]
                : '=');
    }
    return output;
}

palace::PalaceLezExplorerNetworkFingerprintV1 fingerprint()
{
    palace::PalaceLezExplorerNetworkFingerprintV1 value;
    value.networkId = "logos-lez-testnet-v0.2.0";
    for (std::size_t index = 0U; index < 32U; ++index)
        value.channelIdHex += "01";
    value.explorerOrigin = kOrigin;
    value.serverFunctionSuffix = kSuffix;
    return value;
}

palace::PalaceLezExplorerFinalityLimitsV1 limits()
{
    palace::PalaceLezExplorerFinalityLimitsV1 value;
    value.pageSize = 1U;
    value.maxPages = 4U;
    value.maxBlocks = 4U;
    value.maxTransactionsPerBlock = 8U;
    value.maxAccounts = 4U;
    value.maxInstructionWords = 16U;
    value.maxJsonBytes = 64U * 1024U;
    value.maxAccountDataBytes = 1024U;
    value.maxJsonDepth = 16U;
    value.maxJsonNodes = 4096U;
    return value;
}

palace::PalaceLezExplorerTransactionExpectationV1 expectation()
{
    palace::PalaceLezExplorerTransactionExpectationV1 value;
    value.transactionHashHex = kTargetHash;
    value.programIdBase58 = kProgramId;
    value.accountIdsBase58 = {kAccountA, kAccountB};
    value.instructionWords = {2873403345U, 415U};
    value.instructionWordsSha256Hex =
        palace::PalaceLezExplorerFinalitySession::
            instructionWordsSha256Hex(value.instructionWords);
    value.baselineBlockId = 99U;
    const std::vector<std::vector<std::uint8_t>> data = {
        {0x18U, 0x9eU, 0x00U, 0x01U},
        {0x03U, 0x00U, 0xffU},
    };
    for (std::size_t index = 0U; index < value.accountIdsBase58.size(); ++index) {
        palace::PalaceLezExplorerAccountExpectationV1 account;
        account.accountIdBase58 = value.accountIdsBase58[index];
        account.programOwnerBase58 = kProgramId;
        account.expectedData = data[index];
        account.expectedDataSha256Hex =
            palace::PalaceLezExplorerFinalitySession::bytesSha256Hex(
                account.expectedData);
        value.accountExpectations.push_back(std::move(account));
    }
    return value;
}

std::string publicTransaction(
    const std::string& hash,
    const std::string& programId = kProgramId,
    const std::vector<std::string>& accounts = {kAccountA, kAccountB},
    const std::vector<std::uint32_t>& words = {2873403345U, 415U})
{
    std::string accountJson;
    for (const std::string& account : accounts) {
        if (!accountJson.empty())
            accountJson += ',';
        accountJson += '"' + account + '"';
    }
    std::string wordJson;
    for (const std::uint32_t word : words) {
        if (!wordJson.empty())
            wordJson += ',';
        wordJson += std::to_string(word);
    }
    return "{\"Public\":{\"hash\":\"" + hash
        + "\",\"message\":{\"program_id\":\"" + programId
        + "\",\"account_ids\":[" + accountJson
        + "],\"nonces\":[],\"instruction_data\":[" + wordJson
        + "]},\"witness_set\":{\"signatures_and_public_keys\":[],"
          "\"proof\":null}}}";
}

std::string deploymentTransaction(const std::string& hash)
{
    return "{\"ProgramDeployment\":{\"hash\":\"" + hash
        + "\",\"message\":{\"bytecode\":\"AQID\"}}}";
}

std::string confusedTransaction()
{
    std::string value = publicTransaction(kOtherHash);
    value.pop_back();
    return value
        + ",\"ProgramDeployment\":{\"hash\":\"" + std::string(kOtherHash)
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
    std::string body = "[";
    for (const std::string& value : blocks) {
        if (body.size() != 1U)
            body += ',';
        body += value;
    }
    return body + ']';
}

std::string accountResponse(
    const std::vector<std::uint8_t>& data,
    const std::string& owner = kProgramId)
{
    return "{\"program_owner\":\"" + owner
        + "\",\"balance\":0,\"data\":\"" + base64(data)
        + "\",\"nonce\":0}";
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

palace::PalaceLezExplorerCommandV1 requireCommand(
    palace::PalaceLezExplorerFinalitySession& session)
{
    const auto command = session.takeNextCommand();
    LOGOS_ASSERT_TRUE(command.has_value());
    return *command;
}

void start(
    palace::PalaceLezExplorerFinalitySession& session,
    const palace::PalaceLezExplorerTransactionExpectationV1& expected =
        expectation(),
    const palace::PalaceLezExplorerFinalityLimitsV1& configuredLimits =
        limits())
{
    const auto update =
        session.start(fingerprint(), configuredLimits, expected);
    LOGOS_ASSERT_TRUE(update.accepted);
    LOGOS_ASSERT_TRUE(
        update.outcome
        == palace::PalaceLezExplorerFinalityOutcome::Pending);
}

void advanceToAccountChecks(
    palace::PalaceLezExplorerFinalitySession& session,
    const std::string& status = "Finalized",
    const std::string& transaction = publicTransaction(kTargetHash))
{
    const auto command = requireCommand(session);
    const auto update = session.acceptResponse(response(
        command,
        page({block(101U, 'a', '9', status, {transaction})})));
    LOGOS_ASSERT_TRUE(update.accepted);
    LOGOS_ASSERT_TRUE(
        update.outcome
        == palace::PalaceLezExplorerFinalityOutcome::Pending);
}

palace::PalaceLezBytes32 filledBytes(const std::uint8_t value)
{
    palace::PalaceLezBytes32 output{};
    output.fill(value);
    return output;
}

std::string bytesHex(const std::vector<std::uint8_t>& bytes)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string output(bytes.size() * 2U, '0');
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        output[index * 2U] = digits[bytes[index] >> 4U];
        output[index * 2U + 1U] = digits[bytes[index] & 0x0fU];
    }
    return output;
}

std::string accountBase58(const std::string& accountIdHex)
{
    palace::PalaceLezBytes32 accountId{};
    LOGOS_ASSERT_TRUE(
        palace::PalaceLezCodec::parseBytes32Hex(accountIdHex, accountId));
    return palace::PalaceLezCodec::accountIdBase58(accountId);
}

palace::PalaceLezTransactionPlanV3 coordinatorPlan()
{
    constexpr const char* programIdHex =
        "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";
    constexpr const char* signerHex =
        "6666666666666666666666666666666666666666666666666666666666666666";
    return palace::PalaceLezCodec::buildTransaction(
        programIdHex,
        signerHex,
        palace::PalaceLezInstructionV3{
            palace::PalaceLezRevokeCapabilityV3{
                7U,
                filledBytes(0x12U),
            }});
}

std::vector<std::uint8_t> coordinatorRootData()
{
    palace::PalaceLezRootRecordV3 root;
    root.palaceId = filledBytes(0x10U);
    root.title = "Palace";
    root.owner = filledBytes(0x20U);
    root.entryRoomId = filledBytes(0x31U);
    root.roomIds = {filledBytes(0x31U), filledBytes(0x32U)};
    root.activeManifestCid = "bafypalacemanifest";
    root.userCount = 1U;
    root.grantCount = 1U;
    root.revision = 7U;
    root.lastOrderedActionId = 7U;
    return palace::PalaceLezCodec::encodeRootRecord(root);
}

palace::PalaceLezExplorerTransactionExpectationV1 coordinatorExpectation(
    const palace::PalaceLezTransactionPlanV3& plan,
    const std::string& transactionHash)
{
    palace::PalaceLezExplorerTransactionExpectationV1 expected;
    expected.transactionHashHex = transactionHash;
    expected.programIdBase58 = accountBase58(plan.programIdHex);
    for (const std::string& accountIdHex : plan.accountIdsHex)
        expected.accountIdsBase58.push_back(accountBase58(accountIdHex));
    expected.instructionWords = plan.instructionWords;
    expected.instructionWordsSha256Hex =
        palace::PalaceLezExplorerFinalitySession::
            instructionWordsSha256Hex(expected.instructionWords);
    expected.baselineBlockId = 99U;
    for (std::size_t index = 0U;
         index < expected.accountIdsBase58.size();
         ++index) {
        palace::PalaceLezExplorerAccountExpectationV1 account;
        account.accountIdBase58 = expected.accountIdsBase58[index];
        account.programOwnerBase58 = expected.programIdBase58;
        account.expectedData = index == 0U
            ? coordinatorRootData()
            : std::vector<std::uint8_t>{
                static_cast<std::uint8_t>(index),
                0xa5U,
            };
        account.expectedDataSha256Hex =
            palace::PalaceLezExplorerFinalitySession::bytesSha256Hex(
                account.expectedData);
        expected.accountExpectations.push_back(std::move(account));
    }
    return expected;
}

std::optional<palace::PalaceLezExplorerFinalityCertificateV1>
verifiedCertificate(
    const palace::PalaceLezExplorerTransactionExpectationV1& expected,
    const std::uint64_t blockId = 101U,
    const char blockHashDigit = 'a')
{
    palace::PalaceLezExplorerFinalitySession session;
    auto configuredLimits = limits();
    configuredLimits.maxAccounts = 8U;
    configuredLimits.maxInstructionWords = 512U;
    start(session, expected, configuredLimits);
    auto command = requireCommand(session);
    const auto found = session.acceptResponse(response(
        command,
        page({block(
            blockId,
            blockHashDigit,
            '9',
            "Finalized",
            {publicTransaction(
                expected.transactionHashHex,
                expected.programIdBase58,
                expected.accountIdsBase58,
                expected.instructionWords)})})));
    LOGOS_ASSERT_TRUE(found.accepted);
    for (const auto& account : expected.accountExpectations) {
        command = requireCommand(session);
        const auto update = session.acceptResponse(response(
            command,
            accountResponse(
                account.expectedData,
                account.programOwnerBase58)));
        LOGOS_ASSERT_TRUE(update.accepted);
    }
    return session.finalityCertificate();
}

void prepareCoordinator(
    palace::PalaceLezTransactionCoordinator& coordinator,
    const palace::PalaceLezTransactionPlanV3& plan,
    const std::string& transactionHash,
    const std::uint64_t observedHeight,
    const bool observe)
{
    palace::PalaceLezNetworkFingerprint network{
        "lez-testnet",
        "0.4.0-alpha.2",
        repeated('1', 40U),
        repeated('2', 40U),
        "0.2.0",
        repeated('3', 40U),
        plan.programIdHex,
        repeated('a', 64U),
    };
    LOGOS_ASSERT_TRUE(
        coordinator.configureNetworkFingerprint(network, network).accepted);
    LOGOS_ASSERT_TRUE(coordinator.activate());
    const std::string rootDigest =
        palace::PalaceLezCodec::sha256Hex(coordinatorRootData());
    LOGOS_ASSERT_TRUE(coordinator.registerSubmission(
        plan,
        "{\"success\":true,\"tx_hash\":\"" + transactionHash
            + "\",\"secrets\":[],\"error\":\"\"}",
        rootDigest).accepted);
    if (!observe)
        return;
    const std::string rootJson =
        "{\"program_owner\":\"" + plan.programIdHex
        + "\",\"balance\":\"00000000000000000000000000000000\","
          "\"nonce\":\"00000000000000000000000000000000\","
          "\"data\":\""
        + bytesHex(coordinatorRootData()) + "\"}";
    const auto observed = coordinator.observeStableRoot(
        transactionHash,
        static_cast<std::int64_t>(observedHeight),
        rootJson,
        static_cast<std::int64_t>(observedHeight));
    LOGOS_ASSERT_TRUE(observed.accepted);
    LOGOS_ASSERT_TRUE(observed.changed);
}

} // namespace

LOGOS_TEST(lez_explorer_finality_scans_exclusive_pages_then_checks_every_account)
{
    palace::PalaceLezExplorerFinalitySession session;
    start(session);
    LOGOS_ASSERT_FALSE(session.finalityCertificate().has_value());

    const auto newest = requireCommand(session);
    LOGOS_ASSERT_TRUE(
        newest.kind == palace::PalaceLezExplorerCommandKind::Blocks);
    LOGOS_ASSERT_EQ(
        newest.path,
        "/api/get_blocks3022937127152978530");
    LOGOS_ASSERT_EQ(newest.formBody, "limit=1");
    LOGOS_ASSERT_FALSE(session.takeNextCommand().has_value());

    const auto first = session.acceptResponse(response(
        newest,
        page({block(
            102U,
            'b',
            'a',
            "Finalized",
            {deploymentTransaction(kOtherHash)})})));
    LOGOS_ASSERT_TRUE(first.accepted);
    LOGOS_ASSERT_TRUE(
        first.outcome
        == palace::PalaceLezExplorerFinalityOutcome::Pending);

    const auto older = requireCommand(session);
    LOGOS_ASSERT_EQ(older.formBody, "limit=1&before=102");
    const auto found = session.acceptResponse(response(
        older,
        page({block(
            101U,
            'a',
            '9',
            "Finalized",
            {publicTransaction(kTargetHash)})})));
    LOGOS_ASSERT_TRUE(found.accepted);
    LOGOS_ASSERT_EQ(session.pagesScanned(), 2U);
    LOGOS_ASSERT_EQ(session.blocksScanned(), 2U);
    LOGOS_ASSERT_EQ(session.finalizedBlockId(), 101U);
    LOGOS_ASSERT_EQ(session.finalizedBlockHashHex(), repeated('a', 64U));
    LOGOS_ASSERT_FALSE(session.finalityCertificate().has_value());

    const auto firstAccount = requireCommand(session);
    LOGOS_ASSERT_TRUE(
        firstAccount.kind == palace::PalaceLezExplorerCommandKind::Account);
    LOGOS_ASSERT_EQ(
        firstAccount.path,
        "/api/get_account3022937127152978530");
    LOGOS_ASSERT_EQ(
        firstAccount.formBody,
        std::string("account_id=") + kAccountA);
    const auto accountA = session.acceptResponse(response(
        firstAccount,
        accountResponse(expectation().accountExpectations[0].expectedData)));
    LOGOS_ASSERT_TRUE(accountA.accepted);
    LOGOS_ASSERT_EQ(session.verifiedAccountCount(), 1U);
    LOGOS_ASSERT_FALSE(session.finalityCertificate().has_value());

    const auto secondAccount = requireCommand(session);
    LOGOS_ASSERT_EQ(
        secondAccount.formBody,
        std::string("account_id=") + kAccountB);
    const auto finalized = session.acceptResponse(response(
        secondAccount,
        accountResponse(expectation().accountExpectations[1].expectedData)));
    LOGOS_ASSERT_TRUE(finalized.accepted);
    LOGOS_ASSERT_TRUE(
        finalized.outcome
        == palace::PalaceLezExplorerFinalityOutcome::Finalized);
    LOGOS_ASSERT_EQ(session.verifiedAccountCount(), 2U);
    LOGOS_ASSERT_FALSE(session.takeNextCommand().has_value());
    const auto certificate = session.finalityCertificate();
    LOGOS_ASSERT_TRUE(certificate.has_value());
    LOGOS_ASSERT_EQ(certificate->certificateVersion(), 1U);
    LOGOS_ASSERT_EQ(
        certificate->transactionHashHex(),
        std::string(kTargetHash));
    LOGOS_ASSERT_EQ(
        certificate->programIdBase58(),
        std::string(kProgramId));
    LOGOS_ASSERT_TRUE(
        certificate->accountIdsBase58()
        == expectation().accountIdsBase58);
    LOGOS_ASSERT_TRUE(
        certificate->instructionWords()
        == expectation().instructionWords);
    LOGOS_ASSERT_EQ(
        certificate->instructionWordsSha256Hex(),
        expectation().instructionWordsSha256Hex);
    LOGOS_ASSERT_EQ(certificate->finalizedBlockId(), 101U);
    LOGOS_ASSERT_EQ(certificate->finalizedBlockHeight(), 101U);
    LOGOS_ASSERT_EQ(
        certificate->finalizedBlockHashHex(),
        repeated('a', 64U));
    LOGOS_ASSERT_EQ(certificate->accountEvidence().size(), 2U);
    for (std::size_t index = 0U; index < 2U; ++index) {
        LOGOS_ASSERT_EQ(
            certificate->accountEvidence()[index].accountIdBase58,
            expectation().accountExpectations[index].accountIdBase58);
        LOGOS_ASSERT_EQ(
            certificate->accountEvidence()[index].programOwnerBase58,
            expectation().accountExpectations[index].programOwnerBase58);
        LOGOS_ASSERT_EQ(
            certificate->accountEvidence()[index].dataSha256Hex,
            expectation().accountExpectations[index]
                .expectedDataSha256Hex);
    }
}

LOGOS_TEST(lez_explorer_finality_rejects_http_metadata_and_command_confusion)
{
    for (std::size_t scenario = 0U; scenario < 6U; ++scenario) {
        palace::PalaceLezExplorerFinalitySession session;
        start(session);
        const auto command = requireCommand(session);
        auto http = response(command, "[]");
        switch (scenario) {
        case 0U:
            http.statusCode = 503;
            break;
        case 1U:
            http.contentType = "application/json; charset=utf-8";
            break;
        case 2U:
            http.effectiveOrigin = "https://attacker.invalid";
            break;
        case 3U:
            http.redirected = true;
            break;
        case 4U:
            http.transportError = "timeout";
            break;
        default:
            http.body.assign(command.maxResponseBytes + 1U, ' ');
            break;
        }
        const auto update = session.acceptResponse(http);
        LOGOS_ASSERT_FALSE(update.accepted);
        LOGOS_ASSERT_TRUE(
            update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Degraded);
    }

    palace::PalaceLezExplorerFinalitySession confused;
    start(confused);
    const auto command = requireCommand(confused);
    auto http = response(command, "[]");
    ++http.commandSequence;
    const auto rejected = confused.acceptResponse(http);
    LOGOS_ASSERT_TRUE(
        rejected.outcome
        == palace::PalaceLezExplorerFinalityOutcome::Rejected);
}

LOGOS_TEST(lez_explorer_finality_degrades_on_strict_json_and_schema_failures)
{
    std::string extraKeyTransaction = publicTransaction(kOtherHash);
    extraKeyTransaction.insert(
        std::string("{\"Public\":{").size(),
        "\"extra\":0,");
    std::string badSignature = page({block(
        101U,
        'a',
        '9',
        "Finalized",
        {publicTransaction(kOtherHash)})});
    badSignature.replace(
        badSignature.find(repeated('c', 128U)),
        128U,
        repeated('C', 128U));
    const std::vector<std::string> malformed = {
        "[{\"header\":{},\"header\":{}}]",
        "[[[[[[[[[[[[[[[[[0]]]]]]]]]]]]]]]]]",
        "[{\"header\":{\"block_id\":01}}]",
        page({block(
            101U,
            'a',
            '9',
            "Finalized",
            {publicTransaction(
                kTargetHash,
                "0B5o45vJVuFkyDhiK13QRfsxMuxsiz44GMmpr2Xfjm4N")})}),
        page({block(
            101U,
            'a',
            '9',
            "Finalized",
            {confusedTransaction()})}),
        page({block(
            101U,
            'a',
            '9',
            "Finalized",
            {extraKeyTransaction})}),
        page({block(
            101U,
            'a',
            '9',
            "Finalized",
            {publicTransaction(repeated('g', 64U))})}),
        badSignature,
    };
    for (const std::string& body : malformed) {
        palace::PalaceLezExplorerFinalitySession session;
        start(session);
        const auto command = requireCommand(session);
        const auto update =
            session.acceptResponse(response(command, body));
        LOGOS_ASSERT_TRUE(
            update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Degraded);
    }

    palace::PalaceLezExplorerFinalitySession invalidUtf8;
    start(invalidUtf8);
    const auto command = requireCommand(invalidUtf8);
    std::string body = "[\"";
    body.push_back(static_cast<char>(0xc0U));
    body += "\"]";
    const auto update =
        invalidUtf8.acceptResponse(response(command, body));
    LOGOS_ASSERT_TRUE(
        update.outcome
        == palace::PalaceLezExplorerFinalityOutcome::Degraded);
}

LOGOS_TEST(lez_explorer_finality_rejects_nonfinal_and_mismatched_target)
{
    {
        palace::PalaceLezExplorerFinalitySession session;
        start(session);
        const auto command = requireCommand(session);
        const auto update = session.acceptResponse(response(
            command,
            page({block(
                101U,
                '0',
                '9',
                "Finalized",
                {publicTransaction(kTargetHash)})})));
        LOGOS_ASSERT_TRUE(
            update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Degraded);
        LOGOS_ASSERT_FALSE(session.finalityCertificate().has_value());
    }
    {
        palace::PalaceLezExplorerFinalitySession session;
        start(session);
        const auto command = requireCommand(session);
        const auto update = session.acceptResponse(response(
            command,
            page({block(
                101U,
                'a',
                '9',
                "Safe",
                {publicTransaction(kTargetHash)})})));
        LOGOS_ASSERT_TRUE(
            update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Rejected);
        LOGOS_ASSERT_EQ(update.reason, "target-block-not-finalized");
    }
    {
        palace::PalaceLezExplorerFinalitySession session;
        start(session);
        const auto command = requireCommand(session);
        const auto update = session.acceptResponse(response(
            command,
            page({block(
                101U,
                'a',
                '9',
                "Finalized",
                {publicTransaction(
                    kTargetHash,
                    kProgramId,
                    {kAccountB, kAccountA})})})));
        LOGOS_ASSERT_TRUE(
            update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Rejected);
        LOGOS_ASSERT_EQ(update.reason, "target-transaction-content-mismatch");
    }
    {
        palace::PalaceLezExplorerFinalitySession session;
        start(session);
        const auto command = requireCommand(session);
        const auto update = session.acceptResponse(response(
            command,
            page({block(
                101U,
                'a',
                '9',
                "Finalized",
                {publicTransaction(
                    kTargetHash,
                    kProgramId,
                    {kAccountA, kAccountB},
                    {2873403345U, 416U})})})));
        LOGOS_ASSERT_TRUE(
            update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Rejected);
    }
}

LOGOS_TEST(lez_explorer_finality_rejects_duplicate_overlap_and_broken_chain)
{
    {
        auto configuredLimits = limits();
        configuredLimits.pageSize = 2U;
        palace::PalaceLezExplorerFinalitySession session;
        start(session, expectation(), configuredLimits);
        const auto command = requireCommand(session);
        const auto update = session.acceptResponse(response(
            command,
            page({
                block(
                    103U,
                    'c',
                    'b',
                    "Finalized",
                    {publicTransaction(kOtherHash)}),
                block(
                    101U,
                    'a',
                    '9',
                    "Finalized",
                    {publicTransaction(
                        "f0b728539f6cad6a9073be2d055ec08663ee75bd905fe8aed1a80074bcc9a18f")}),
            })));
        LOGOS_ASSERT_TRUE(
            update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Rejected);
        LOGOS_ASSERT_EQ(update.reason, "block-page-chain-order-mismatch");
    }
    {
        palace::PalaceLezExplorerFinalitySession session;
        start(session);
        const auto command = requireCommand(session);
        const auto update = session.acceptResponse(response(
            command,
            page({block(
                101U,
                'a',
                '9',
                "Finalized",
                {publicTransaction(kOtherHash),
                 publicTransaction(kOtherHash)})})));
        LOGOS_ASSERT_TRUE(
            update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Rejected);
        LOGOS_ASSERT_EQ(update.reason, "duplicate-transaction-hash");
    }
    {
        palace::PalaceLezExplorerFinalitySession session;
        start(session);
        auto command = requireCommand(session);
        LOGOS_ASSERT_TRUE(session.acceptResponse(response(
            command,
            page({block(
                102U,
                'b',
                'a',
                "Finalized",
                {publicTransaction(kOtherHash)})}))).accepted);
        command = requireCommand(session);
        const auto update = session.acceptResponse(response(
            command,
            page({block(
                102U,
                'b',
                'a',
                "Finalized",
                {publicTransaction(
                    "f0b728539f6cad6a9073be2d055ec08663ee75bd905fe8aed1a80074bcc9a18f")})})));
        LOGOS_ASSERT_TRUE(
            update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Rejected);
        LOGOS_ASSERT_EQ(
            update.reason,
            "block-page-cursor-replay-or-overlap");
    }
    {
        palace::PalaceLezExplorerFinalitySession session;
        start(session);
        auto command = requireCommand(session);
        LOGOS_ASSERT_TRUE(session.acceptResponse(response(
            command,
            page({block(
                102U,
                'b',
                'a',
                "Finalized",
                {publicTransaction(kOtherHash)})}))).accepted);
        command = requireCommand(session);
        const auto update = session.acceptResponse(response(
            command,
            page({block(
                101U,
                'd',
                '9',
                "Finalized",
                {publicTransaction(
                    "f0b728539f6cad6a9073be2d055ec08663ee75bd905fe8aed1a80074bcc9a18f")})})));
        LOGOS_ASSERT_TRUE(
            update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Rejected);
    }
}

LOGOS_TEST(lez_explorer_finality_never_promotes_missing_transaction)
{
    {
        palace::PalaceLezExplorerFinalitySession session;
        start(session);
        const auto command = requireCommand(session);
        const auto update = session.acceptResponse(response(
            command,
            page({block(
                99U,
                '9',
                '8',
                "Finalized",
                {publicTransaction(kOtherHash)})})));
        LOGOS_ASSERT_TRUE(update.accepted);
        LOGOS_ASSERT_TRUE(
            update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Pending);
        LOGOS_ASSERT_TRUE(session.scanExhausted());
        LOGOS_ASSERT_FALSE(session.takeNextCommand().has_value());
    }
    {
        auto configuredLimits = limits();
        configuredLimits.maxPages = 1U;
        palace::PalaceLezExplorerFinalitySession session;
        start(session, expectation(), configuredLimits);
        const auto command = requireCommand(session);
        const auto update = session.acceptResponse(response(
            command,
            page({block(
                102U,
                'b',
                'a',
                "Finalized",
                {publicTransaction(kOtherHash)})})));
        LOGOS_ASSERT_TRUE(
            update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Pending);
        LOGOS_ASSERT_EQ(update.reason, "finality-scan-window-exhausted");
        LOGOS_ASSERT_TRUE(session.scanExhausted());
    }
}

LOGOS_TEST(lez_explorer_finality_validates_canonical_account_owner_and_bytes)
{
    {
        palace::PalaceLezExplorerFinalitySession session;
        start(session);
        advanceToAccountChecks(session);
        const auto command = requireCommand(session);
        const auto update = session.acceptResponse(response(
            command,
            accountResponse(
                expectation().accountExpectations[0].expectedData,
                kAccountA)));
        LOGOS_ASSERT_TRUE(
            update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Rejected);
        LOGOS_ASSERT_EQ(update.reason, "finalized-account-owner-mismatch");
    }
    {
        palace::PalaceLezExplorerFinalitySession session;
        start(session);
        advanceToAccountChecks(session);
        const auto command = requireCommand(session);
        const auto update = session.acceptResponse(response(
            command,
            accountResponse({0x00U})));
        LOGOS_ASSERT_TRUE(
            update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Rejected);
        LOGOS_ASSERT_EQ(update.reason, "finalized-account-data-mismatch");
    }
    {
        palace::PalaceLezExplorerFinalitySession session;
        start(session);
        advanceToAccountChecks(session);
        const auto command = requireCommand(session);
        const auto update = session.acceptResponse(response(
            command,
            "{\"program_owner\":\""
                + std::string(kProgramId)
                + "\",\"balance\":0,\"data\":\"AB==\",\"nonce\":0}"));
        LOGOS_ASSERT_TRUE(
            update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Degraded);
    }
    {
        palace::PalaceLezExplorerFinalitySession session;
        start(session);
        advanceToAccountChecks(session);
        const auto command = requireCommand(session);
        const auto update =
            session.acceptResponse(response(command, "null"));
        LOGOS_ASSERT_TRUE(
            update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Rejected);
    }
}

LOGOS_TEST(lez_explorer_finality_rejects_unpinned_or_inconsistent_inputs)
{
    {
        auto configuredFingerprint = fingerprint();
        configuredFingerprint.serverFunctionSuffix = "0";
        palace::PalaceLezExplorerFinalitySession session;
        const auto update =
            session.start(configuredFingerprint, limits(), expectation());
        LOGOS_ASSERT_TRUE(
            update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Rejected);
    }
    {
        auto expected = expectation();
        expected.instructionWordsSha256Hex = repeated('0', 64U);
        palace::PalaceLezExplorerFinalitySession session;
        const auto update =
            session.start(fingerprint(), limits(), expected);
        LOGOS_ASSERT_TRUE(
            update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Rejected);
    }
    {
        auto expected = expectation();
        expected.accountExpectations.pop_back();
        palace::PalaceLezExplorerFinalitySession session;
        const auto update =
            session.start(fingerprint(), limits(), expected);
        LOGOS_ASSERT_TRUE(
            update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Rejected);
    }
}

LOGOS_TEST(lez_explorer_certificate_requires_observation_before_promotion)
{
    const palace::PalaceLezTransactionPlanV3 plan = coordinatorPlan();
    LOGOS_ASSERT_TRUE(plan.accepted);
    const auto certificate =
        verifiedCertificate(coordinatorExpectation(plan, kTargetHash));
    LOGOS_ASSERT_TRUE(certificate.has_value());

    palace::PalaceLezTransactionCoordinator coordinator;
    prepareCoordinator(
        coordinator,
        plan,
        kTargetHash,
        107U,
        false);
    const auto update =
        coordinator.reconcileExplorerFinality(*certificate);
    LOGOS_ASSERT_FALSE(update.accepted);
    LOGOS_ASSERT_EQ(update.reason, "observation-required");
    LOGOS_ASSERT_TRUE(
        coordinator.transactions().front().stage
        == palace::PalaceLezTransactionStage::Submitted);
}

LOGOS_TEST(lez_explorer_certificate_promotes_only_exact_registered_evidence)
{
    const palace::PalaceLezTransactionPlanV3 plan = coordinatorPlan();
    LOGOS_ASSERT_TRUE(plan.accepted);
    palace::PalaceLezTransactionCoordinator coordinator;
    prepareCoordinator(
        coordinator,
        plan,
        kTargetHash,
        107U,
        true);

    auto reordered = coordinatorExpectation(plan, kTargetHash);
    std::swap(
        reordered.accountIdsBase58[0],
        reordered.accountIdsBase58[1]);
    std::swap(
        reordered.accountExpectations[0],
        reordered.accountExpectations[1]);
    auto certificate = verifiedCertificate(reordered);
    LOGOS_ASSERT_TRUE(certificate.has_value());
    auto update = coordinator.reconcileExplorerFinality(*certificate);
    LOGOS_ASSERT_FALSE(update.accepted);
    LOGOS_ASSERT_EQ(update.reason, "finalized-transaction-mismatch");

    auto wrongWords = coordinatorExpectation(plan, kTargetHash);
    ++wrongWords.instructionWords.back();
    wrongWords.instructionWordsSha256Hex =
        palace::PalaceLezExplorerFinalitySession::
            instructionWordsSha256Hex(wrongWords.instructionWords);
    certificate = verifiedCertificate(wrongWords);
    LOGOS_ASSERT_TRUE(certificate.has_value());
    update = coordinator.reconcileExplorerFinality(*certificate);
    LOGOS_ASSERT_FALSE(update.accepted);
    LOGOS_ASSERT_EQ(update.reason, "finalized-transaction-mismatch");

    certificate =
        verifiedCertificate(coordinatorExpectation(plan, kOtherHash));
    LOGOS_ASSERT_TRUE(certificate.has_value());
    update = coordinator.reconcileExplorerFinality(*certificate);
    LOGOS_ASSERT_FALSE(update.accepted);
    LOGOS_ASSERT_EQ(update.reason, "transaction-hash-mismatch");

    certificate = verifiedCertificate(
        coordinatorExpectation(plan, kTargetHash),
        108U,
        'b');
    LOGOS_ASSERT_TRUE(certificate.has_value());
    update = coordinator.reconcileExplorerFinality(*certificate);
    LOGOS_ASSERT_FALSE(update.accepted);
    LOGOS_ASSERT_EQ(update.reason, "finality-block-height-mismatch");

    auto wrongRootOwner = coordinatorExpectation(plan, kTargetHash);
    wrongRootOwner.accountExpectations.front().programOwnerBase58 =
        wrongRootOwner.accountIdsBase58[1];
    certificate = verifiedCertificate(wrongRootOwner);
    LOGOS_ASSERT_TRUE(certificate.has_value());
    update = coordinator.reconcileExplorerFinality(*certificate);
    LOGOS_ASSERT_FALSE(update.accepted);
    LOGOS_ASSERT_EQ(update.reason, "finalized-account-state-mismatch");

    auto wrongRootData = coordinatorExpectation(plan, kTargetHash);
    wrongRootData.accountExpectations.front().expectedData.push_back(0U);
    wrongRootData.accountExpectations.front().expectedDataSha256Hex =
        palace::PalaceLezExplorerFinalitySession::bytesSha256Hex(
            wrongRootData.accountExpectations.front().expectedData);
    certificate = verifiedCertificate(wrongRootData);
    LOGOS_ASSERT_TRUE(certificate.has_value());
    update = coordinator.reconcileExplorerFinality(*certificate);
    LOGOS_ASSERT_FALSE(update.accepted);
    LOGOS_ASSERT_EQ(update.reason, "finalized-account-state-mismatch");

    certificate =
        verifiedCertificate(coordinatorExpectation(plan, kTargetHash));
    LOGOS_ASSERT_TRUE(certificate.has_value());
    update = coordinator.reconcileExplorerFinality(*certificate);
    LOGOS_ASSERT_TRUE(update.accepted);
    LOGOS_ASSERT_TRUE(update.changed);
    LOGOS_ASSERT_TRUE(
        update.stage == palace::PalaceLezTransactionStage::Finalized);

    const auto duplicate =
        coordinator.reconcileExplorerFinality(*certificate);
    LOGOS_ASSERT_FALSE(duplicate.accepted);
    LOGOS_ASSERT_FALSE(duplicate.changed);
    LOGOS_ASSERT_EQ(
        duplicate.reason,
        "duplicate-finality-certificate");
}
