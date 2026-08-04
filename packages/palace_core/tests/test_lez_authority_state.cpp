#include <logos_test.h>

#include "palace_lez_authority_state.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr char kProgramId[] =
    "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";
constexpr char kSigner[] =
    "6666666666666666666666666666666666666666666666666666666666666666";
constexpr char kOrigin[] =
    "https://explorer.testnet.lez.logos.co";
constexpr char kSuffix[] = "3022937127152978530";
constexpr char kTransactionHash[] =
    "1212121212121212121212121212121212121212121212121212121212121212";
constexpr std::uint32_t kAllCapabilities = (1U << 5U) - 1U;

palace::PalaceLezBytes32 bytes(const std::uint8_t value)
{
    palace::PalaceLezBytes32 output{};
    output.fill(value);
    return output;
}

std::string repeated(const char value, const std::size_t size)
{
    return std::string(size, value);
}

std::string hex(const std::vector<std::uint8_t>& value)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string output(value.size() * 2U, '0');
    for (std::size_t index = 0U; index < value.size(); ++index) {
        output[index * 2U] = digits[value[index] >> 4U];
        output[index * 2U + 1U] =
            digits[value[index] & 0x0fU];
    }
    return output;
}

std::string base58(const std::string& accountIdHex)
{
    palace::PalaceLezBytes32 value{};
    LOGOS_ASSERT_TRUE(
        palace::PalaceLezCodec::parseBytes32Hex(
            accountIdHex, value));
    return palace::PalaceLezCodec::accountIdBase58(value);
}

class BorshWriter {
public:
    void u8(const std::uint8_t value)
    {
        data.push_back(value);
    }

    void u16(const std::uint16_t value)
    {
        u8(static_cast<std::uint8_t>(value));
        u8(static_cast<std::uint8_t>(value >> 8U));
    }

    void u32(const std::uint32_t value)
    {
        for (unsigned int shift = 0U; shift < 32U; shift += 8U)
            u8(static_cast<std::uint8_t>(value >> shift));
    }

    void u64(const std::uint64_t value)
    {
        for (unsigned int shift = 0U; shift < 64U; shift += 8U)
            u8(static_cast<std::uint8_t>(value >> shift));
    }

    void fixed(const palace::PalaceLezBytes32& value)
    {
        data.insert(data.end(), value.begin(), value.end());
    }

    void string(const std::string& value)
    {
        u32(static_cast<std::uint32_t>(value.size()));
        data.insert(data.end(), value.begin(), value.end());
    }

    void optionalString(
        const std::optional<std::string>& value)
    {
        u8(value.has_value() ? 1U : 0U);
        if (value.has_value())
            string(*value);
    }

    std::vector<std::uint8_t> data;
};

std::vector<std::uint8_t> encodeProfile(
    const palace::PalaceLezInitializeV3& initialize,
    const palace::PalaceLezBytes32& owner)
{
    BorshWriter writer;
    writer.u8(1U);
    writer.u16(3U);
    writer.fixed(initialize.palaceId);
    writer.fixed(owner);
    writer.string(initialize.ownerProfile.displayName);
    writer.fixed(initialize.ownerProfile.deliveryKey);
    writer.u64(initialize.ownerProfile.keyEpoch);
    writer.optionalString(
        initialize.ownerProfile.avatarManifestCid);
    writer.u64(0U);
    return std::move(writer.data);
}

std::vector<std::uint8_t> encodeRoom(
    const palace::PalaceLezInitializeV3& initialize,
    const palace::PalaceLezBytes32& roomId,
    const palace::PalaceLezRoomConfigInputV3& config)
{
    BorshWriter writer;
    writer.u8(2U);
    writer.u16(3U);
    writer.fixed(initialize.palaceId);
    writer.fixed(roomId);
    writer.string(config.title);
    writer.string(config.manifestCid);
    writer.string(config.scriptBundleCid);
    writer.u8(static_cast<std::uint8_t>(config.vmProfile));
    writer.u8(0U);
    writer.u64(0U);
    return std::move(writer.data);
}

std::vector<std::uint8_t> encodeOwnerGrant(
    const palace::PalaceLezInitializeV3& initialize,
    const palace::PalaceLezBytes32& owner)
{
    BorshWriter writer;
    writer.u8(3U);
    writer.u16(3U);
    writer.fixed(initialize.palaceId);
    writer.fixed(initialize.ownerGrantId);
    writer.fixed(owner);
    writer.fixed(owner);
    writer.u8(0U);
    writer.u32(kAllCapabilities);
    writer.u8(1U);
    writer.u64(std::numeric_limits<std::uint64_t>::max());
    writer.u8(0U);
    writer.u64(0U);
    return std::move(writer.data);
}

std::string moduleAccountJson(
    const std::string& ownerHex,
    const std::vector<std::uint8_t>& data)
{
    return "{\"program_owner\":\"" + ownerHex
        + "\",\"balance\":\"00000000000000000000000000000000\","
          "\"nonce\":\"00000000000000000000000000000000\","
          "\"data\":\""
        + hex(data) + "\"}";
}

std::string base64(const std::vector<std::uint8_t>& value)
{
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    for (std::size_t index = 0U; index < value.size();
         index += 3U) {
        const std::uint32_t first = value[index];
        const std::uint32_t second =
            index + 1U < value.size()
            ? value[index + 1U] : 0U;
        const std::uint32_t third =
            index + 2U < value.size()
            ? value[index + 2U] : 0U;
        const std::uint32_t merged =
            (first << 16U) | (second << 8U) | third;
        output.push_back(
            alphabet[(merged >> 18U) & 0x3fU]);
        output.push_back(
            alphabet[(merged >> 12U) & 0x3fU]);
        output.push_back(
            index + 1U < value.size()
            ? alphabet[(merged >> 6U) & 0x3fU] : '=');
        output.push_back(
            index + 2U < value.size()
            ? alphabet[merged & 0x3fU] : '=');
    }
    return output;
}

std::string explorerAccountJson(
    const std::string& ownerBase58,
    const std::vector<std::uint8_t>& data)
{
    return "{\"program_owner\":\"" + ownerBase58
        + "\",\"balance\":0,\"data\":\"" + base64(data)
        + "\",\"nonce\":0}";
}

palace::PalaceLezInitializeV3 initialize()
{
    palace::PalaceLezInitializeV3 value;
    value.palaceId = bytes(0x10U);
    value.title = "Logos Palace";
    value.activeManifestCid = "bafypalacemanifest";
    value.ownerProfile = {
        "Alice", bytes(0x51U), 1U, std::nullopt};
    value.ownerGrantId = bytes(0x11U);
    value.entryRoomId = bytes(0x31U);
    value.entryRoom = {
        "Atrium",
        "bafyatrium",
        "bafyatriumscript",
        palace::PalaceLezVmProfileV3::IptScraeMvpV1,
    };
    value.secondaryRoomId = bytes(0x32U);
    value.secondaryRoom = {
        "Lounge",
        "bafylounge",
        "bafyloungescript",
        palace::PalaceLezVmProfileV3::IptScraeMvpV1,
    };
    return value;
}

struct Fixture {
    palace::PalaceLezInitializeV3 initialization;
    palace::PalaceLezTransactionPlanV3 initializePlan;
    palace::PalaceLezRootRecordV3 root;
    std::vector<std::uint8_t> rootData;
    std::vector<std::string> initializeResponses;
    palace::PalaceLezFinalizedAuthorityBundleV1 bundle;
    palace::PalaceLezAuthorityBundleExpectationV1 expectation;
};

Fixture fixture()
{
    Fixture value;
    value.initialization = initialize();
    value.initializePlan = palace::PalaceLezCodec::buildTransaction(
        kProgramId,
        kSigner,
        palace::PalaceLezInstructionV3{
            value.initialization});
    LOGOS_ASSERT_TRUE(value.initializePlan.accepted);
    const auto expectedRoot =
        palace::PalaceLezCodec::expectedInitialRoot(
            kSigner,
            palace::PalaceLezInstructionV3{
                value.initialization});
    LOGOS_ASSERT_TRUE(expectedRoot.accepted);
    value.root = expectedRoot.record;
    value.rootData = expectedRoot.encodedRecord;

    palace::PalaceLezBytes32 owner{};
    LOGOS_ASSERT_TRUE(
        palace::PalaceLezCodec::parseBytes32Hex(kSigner, owner));
    const std::vector<std::uint8_t> profile =
        encodeProfile(value.initialization, owner);
    const std::vector<std::uint8_t> entry =
        encodeRoom(
            value.initialization,
            value.initialization.entryRoomId,
            value.initialization.entryRoom);
    const std::vector<std::uint8_t> secondary =
        encodeRoom(
            value.initialization,
            value.initialization.secondaryRoomId,
            value.initialization.secondaryRoom);
    const std::vector<std::uint8_t> grant =
        encodeOwnerGrant(value.initialization, owner);
    const std::vector<std::uint8_t> signerData{0x42U};
    value.initializeResponses = {
        moduleAccountJson(kProgramId, value.rootData),
        moduleAccountJson(repeated('0', 64U), signerData),
        moduleAccountJson(kProgramId, profile),
        moduleAccountJson(kProgramId, entry),
        moduleAccountJson(kProgramId, secondary),
        moduleAccountJson(kProgramId, grant),
    };

    value.bundle.scope.networkId = "lez-testnet";
    value.bundle.scope.programIdHex = kProgramId;
    value.bundle.scope.rootAccountIdHex =
        value.initializePlan.rootAccountIdHex;
    value.bundle.checkpoint = {
        100U, 100U, repeated('9', 64U), 0U};
    for (const std::size_t index :
         std::vector<std::size_t>{0U, 2U, 3U, 4U, 5U}) {
        value.bundle.accounts.push_back({
            value.initializePlan.accountIdsHex[index],
            value.initializeResponses[index],
        });
    }
    value.expectation.scope = value.bundle.scope;
    value.expectation.checkpoint = value.bundle.checkpoint;
    return value;
}

bool sameBundle(
    const palace::PalaceLezFinalizedAuthorityBundleV1& left,
    const palace::PalaceLezFinalizedAuthorityBundleV1& right)
{
    if (left.scope.networkId != right.scope.networkId
        || left.scope.programIdHex != right.scope.programIdHex
        || left.scope.rootAccountIdHex
            != right.scope.rootAccountIdHex
        || left.checkpoint.finalizedBlockId
            != right.checkpoint.finalizedBlockId
        || left.checkpoint.finalizedBlockHeight
            != right.checkpoint.finalizedBlockHeight
        || left.checkpoint.finalizedBlockHashHex
            != right.checkpoint.finalizedBlockHashHex
        || left.checkpoint.lastOrderedActionId
            != right.checkpoint.lastOrderedActionId
        || left.accounts.size() != right.accounts.size()) {
        return false;
    }
    for (std::size_t index = 0U;
         index < left.accounts.size();
         ++index) {
        if (left.accounts[index].accountIdHex
                != right.accounts[index].accountIdHex
            || left.accounts[index].responseJson
                != right.accounts[index].responseJson) {
            return false;
        }
    }
    return true;
}

palace::PalaceLezExplorerNetworkFingerprintV1 fingerprint()
{
    palace::PalaceLezExplorerNetworkFingerprintV1 value;
    value.networkId = "lez-testnet";
    value.channelIdHex = repeated('1', 64U);
    value.explorerOrigin = kOrigin;
    value.serverFunctionSuffix = kSuffix;
    return value;
}

palace::PalaceLezExplorerFinalityLimitsV1 limits()
{
    palace::PalaceLezExplorerFinalityLimitsV1 value;
    value.pageSize = 2U;
    value.maxPages = 2U;
    value.maxBlocks = 4U;
    value.maxTransactionsPerBlock = 8U;
    value.maxAccounts = 8U;
    value.maxInstructionWords = 512U;
    value.maxJsonBytes = 64U * 1024U;
    value.maxAccountDataBytes = 64U * 1024U;
    value.maxJsonDepth = 16U;
    value.maxJsonNodes = 4096U;
    return value;
}

std::string publicTransaction(
    const palace::PalaceLezExplorerTransactionExpectationV1& expected)
{
    std::string accounts;
    for (const std::string& account : expected.accountIdsBase58) {
        if (!accounts.empty())
            accounts += ',';
        accounts += '"' + account + '"';
    }
    std::string words;
    for (const std::uint32_t word :
         expected.instructionWords) {
        if (!words.empty())
            words += ',';
        words += std::to_string(word);
    }
    return "{\"Public\":{\"hash\":\""
        + expected.transactionHashHex
        + "\",\"message\":{\"program_id\":\""
        + expected.programIdBase58
        + "\",\"account_ids\":[" + accounts
        + "],\"nonces\":[],\"instruction_data\":[" + words
        + "]},\"witness_set\":{\"signatures_and_public_keys\":[],"
          "\"proof\":null}}}";
}

std::string finalizedBlock(
    const palace::PalaceLezExplorerTransactionExpectationV1& expected)
{
    return "[{\"header\":{\"block_id\":101,"
        "\"prev_block_hash\":\"" + repeated('9', 64U)
        + "\",\"hash\":\"" + repeated('a', 64U)
        + "\",\"timestamp\":101000,\"signature\":\""
        + repeated('c', 128U)
        + "\"},\"body\":{\"transactions\":["
        + publicTransaction(expected)
        + "]},\"bedrock_status\":\"Finalized\"}]";
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
    palace::PalaceLezExplorerFinalitySession& session)
{
    const auto value = session.takeNextCommand();
    LOGOS_ASSERT_TRUE(value.has_value());
    return *value;
}

struct FinalizedActionFixture {
    palace::PalaceLezTrackedTransaction tracked;
    palace::PalaceLezStableAccountBatchV1 stableAccounts;
    palace::PalaceLezExplorerFinalityCertificateV1 certificate;
};

FinalizedActionFixture finalizedInitialize(const Fixture& initial)
{
    palace::PalaceLezExplorerTransactionExpectationV1 expected;
    expected.transactionHashHex = kTransactionHash;
    expected.programIdBase58 = base58(kProgramId);
    for (const std::string& account :
         initial.initializePlan.accountIdsHex) {
        expected.accountIdsBase58.push_back(base58(account));
    }
    expected.instructionWords =
        initial.initializePlan.instructionWords;
    expected.instructionWordsSha256Hex =
        palace::PalaceLezExplorerFinalitySession::
            instructionWordsSha256Hex(
                expected.instructionWords);
    for (std::size_t index = 0U;
         index < initial.initializeResponses.size();
         ++index) {
        const auto raw =
            palace::PalaceLezCodec::parsePublicAccountSnapshot(
                initial.initializeResponses[index]);
        LOGOS_ASSERT_TRUE(raw.accepted);
        expected.accountExpectations.push_back({
            expected.accountIdsBase58[index],
            base58(raw.programOwnerHex),
            raw.data,
            raw.dataSha256Hex,
        });
    }

    palace::PalaceLezExplorerFinalitySession session;
    auto update = session.start(
        fingerprint(), limits(), expected);
    LOGOS_ASSERT_TRUE(update.accepted);
    auto next = command(session);
    update = session.acceptResponse(
        response(next, finalizedBlock(expected)));
    LOGOS_ASSERT_TRUE(update.accepted);
    for (const auto& account : expected.accountExpectations) {
        next = command(session);
        update = session.acceptResponse(response(
            next,
            explorerAccountJson(
                account.programOwnerBase58,
                account.expectedData)));
        LOGOS_ASSERT_TRUE(update.accepted);
    }
    const auto certificate = session.finalityCertificate();
    LOGOS_ASSERT_TRUE(certificate.has_value());

    palace::PalaceLezTrackedTransaction tracked;
    tracked.stage = palace::PalaceLezTransactionStage::Finalized;
    tracked.transactionHash = kTransactionHash;
    tracked.orderedActionId = 0U;
    tracked.observedBlockHeight = 102U;
    tracked.expectedRootDataSha256Hex =
        palace::PalaceLezCodec::sha256Hex(initial.rootData);
    tracked.plan = initial.initializePlan;
    palace::PalaceLezStableAccountBatchV1 stable;
    stable.heightBefore = 102;
    stable.heightAfter = 102;
    stable.accountResponseJson =
        initial.initializeResponses;
    return {
        std::move(tracked),
        std::move(stable),
        *certificate,
    };
}

FinalizedActionFixture finalizedPublish(const Fixture& initial)
{
    palace::PalaceLezPublishManifestV3 publish;
    publish.orderedActionId = 1U;
    publish.cid = "bafyupdatedmanifest";
    const palace::PalaceLezInstructionV3 instruction{publish};
    const palace::PalaceLezTransactionPlanV3 plan =
        palace::PalaceLezCodec::buildTransaction(
            kProgramId, kSigner, instruction);
    LOGOS_ASSERT_TRUE(plan.accepted);
    const auto advanced =
        palace::PalaceLezCodec::expectedAdvancedRoot(
            initial.root, instruction);
    LOGOS_ASSERT_TRUE(advanced.accepted);
    const std::vector<std::uint8_t> signerData{0x42U};

    palace::PalaceLezExplorerTransactionExpectationV1 expected;
    expected.transactionHashHex = kTransactionHash;
    expected.programIdBase58 = base58(kProgramId);
    for (const std::string& account : plan.accountIdsHex)
        expected.accountIdsBase58.push_back(base58(account));
    expected.instructionWords = plan.instructionWords;
    expected.instructionWordsSha256Hex =
        palace::PalaceLezExplorerFinalitySession::
            instructionWordsSha256Hex(plan.instructionWords);
    expected.accountExpectations = {
        {
            expected.accountIdsBase58[0],
            expected.programIdBase58,
            advanced.encodedRecord,
            advanced.dataSha256Hex,
        },
        {
            expected.accountIdsBase58[1],
            base58(repeated('0', 64U)),
            signerData,
            palace::PalaceLezExplorerFinalitySession::
                bytesSha256Hex(signerData),
        },
    };

    palace::PalaceLezExplorerFinalitySession session;
    auto update = session.start(
        fingerprint(), limits(), expected);
    LOGOS_ASSERT_TRUE(update.accepted);
    auto next = command(session);
    update = session.acceptResponse(
        response(next, finalizedBlock(expected)));
    LOGOS_ASSERT_TRUE(update.accepted);
    for (const auto& account : expected.accountExpectations) {
        next = command(session);
        update = session.acceptResponse(response(
            next,
            explorerAccountJson(
                account.programOwnerBase58,
                account.expectedData)));
        LOGOS_ASSERT_TRUE(update.accepted);
    }
    const auto certificate = session.finalityCertificate();
    LOGOS_ASSERT_TRUE(certificate.has_value());

    palace::PalaceLezTrackedTransaction tracked;
    tracked.stage = palace::PalaceLezTransactionStage::Finalized;
    tracked.transactionHash = kTransactionHash;
    tracked.orderedActionId = 1U;
    tracked.observedBlockHeight = 102U;
    tracked.expectedRootDataSha256Hex =
        advanced.dataSha256Hex;
    tracked.plan = plan;

    palace::PalaceLezStableAccountBatchV1 stable;
    stable.heightBefore = 102;
    stable.heightAfter = 102;
    stable.accountResponseJson = {
        moduleAccountJson(kProgramId, advanced.encodedRecord),
        moduleAccountJson(repeated('0', 64U), signerData),
    };
    return {
        std::move(tracked),
        std::move(stable),
        *certificate,
    };
}

palace::PalaceLezExplorerHistoryResultV1 history(
    const Fixture& value)
{
    palace::PalaceLezExplorerHistoryResultV1 result;
    palace::PalaceLezFinalizedActionV3 action;
    action.transactionHash = repeated('1', 64U);
    action.orderedActionId = 0U;
    action.instruction =
        palace::PalaceLezInstructionV3{
            value.initialization};
    action.accountIdsHex =
        value.initializePlan.accountIdsHex;
    result.actions.push_back(std::move(action));
    result.uniqueAccountIdsHex =
        value.initializePlan.accountIdsHex;
    result.latestFinalizedBlockId = 100U;
    result.latestFinalizedBlockHashHex =
        repeated('9', 64U);
    return result;
}

void assertProjectionUnchanged(
    const palace::AuthorityProjection& authority,
    const std::string& palaceId,
    const std::int64_t finalizedAt)
{
    LOGOS_ASSERT_EQ(authority.palaceId(), palaceId);
    LOGOS_ASSERT_EQ(authority.finalizedAt(), finalizedAt);
}

} // namespace

LOGOS_TEST(lez_authority_state_restores_only_complete_exact_bundle)
{
    Fixture value = fixture();
    palace::AuthorityProjection authority;
    const auto restored =
        palace::restoreFinalizedLezAuthorityStateV1(
            authority, value.bundle, value.expectation);
    LOGOS_ASSERT_TRUE(restored.accepted);
    LOGOS_ASSERT_EQ(restored.reason, std::string("accepted"));
    LOGOS_ASSERT_EQ(authority.finalizedAt(), 100);
    LOGOS_ASSERT_EQ(
        authority.deliveryKeyFor(
            kSigner, 1),
        palace::PalaceLezCodec::bytes32Hex(bytes(0x51U)));

    const std::string palaceId = authority.palaceId();
    palace::PalaceLezFinalizedAuthorityBundleV1 wrongPda =
        value.bundle;
    wrongPda.accounts[1].accountIdHex =
        repeated('f', 64U);
    const auto rejected =
        palace::restoreFinalizedLezAuthorityStateV1(
            authority, wrongPda, value.expectation);
    LOGOS_ASSERT_FALSE(rejected.accepted);
    assertProjectionUnchanged(authority, palaceId, 100);

    palace::PalaceLezFinalizedAuthorityBundleV1 duplicate =
        value.bundle;
    duplicate.accounts.push_back(duplicate.accounts.back());
    LOGOS_ASSERT_FALSE(
        palace::restoreFinalizedLezAuthorityStateV1(
            authority, duplicate, value.expectation).accepted);
    assertProjectionUnchanged(authority, palaceId, 100);
}

LOGOS_TEST(lez_authority_state_applies_certificate_bound_action_transactionally)
{
    Fixture value = fixture();
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(
        palace::restoreFinalizedLezAuthorityStateV1(
            authority, value.bundle, value.expectation).accepted);
    FinalizedActionFixture action =
        finalizedPublish(value);
    action.stableAccounts.heightBefore = 120;
    action.stableAccounts.heightAfter = 120;
    const auto applied =
        palace::applyFinalizedLezAuthorityActionV1(
            authority,
            value.bundle,
            value.expectation,
            action.tracked,
            action.certificate,
            action.stableAccounts);
    LOGOS_ASSERT_TRUE(applied.accepted);
    LOGOS_ASSERT_EQ(value.bundle.checkpoint.finalizedBlockId, 101U);
    LOGOS_ASSERT_EQ(
        value.bundle.checkpoint.finalizedBlockHashHex,
        repeated('a', 64U));
    LOGOS_ASSERT_EQ(
        value.bundle.checkpoint.lastOrderedActionId, 1U);
    LOGOS_ASSERT_EQ(value.bundle.accounts.size(), 5U);
    LOGOS_ASSERT_TRUE(std::none_of(
        value.bundle.accounts.begin(),
        value.bundle.accounts.end(),
        [](const auto& account) {
            return account.accountIdHex == kSigner;
        }));
    LOGOS_ASSERT_EQ(authority.finalizedAt(), 101);

    const auto root =
        palace::PalaceLezCodec::decodePublicAccount(
            value.bundle.accounts.front().responseJson,
            kProgramId);
    LOGOS_ASSERT_TRUE(root.accepted);
    LOGOS_ASSERT_EQ(
        std::get<palace::PalaceLezRootRecordV3>(
            root.record).lastOrderedActionId,
        1U);
}

LOGOS_TEST(lez_authority_state_seeds_only_complete_certificate_bound_genesis)
{
    const Fixture value = fixture();
    const FinalizedActionFixture genesis =
        finalizedInitialize(value);
    palace::PalaceLezFinalizedAuthorityBundleV1 bundle;
    bundle.scope = value.bundle.scope;
    palace::PalaceLezAuthorityBundleExpectationV1 expectation;
    expectation.scope = value.bundle.scope;
    palace::AuthorityProjection authority;

    const auto seeded =
        palace::applyFinalizedLezAuthorityActionV1(
            authority,
            bundle,
            expectation,
            genesis.tracked,
            genesis.certificate,
            genesis.stableAccounts);
    LOGOS_ASSERT_TRUE(seeded.accepted);
    LOGOS_ASSERT_EQ(bundle.accounts.size(), 5U);
    for (std::size_t index = 0U;
         index < bundle.accounts.size();
         ++index) {
        LOGOS_ASSERT_EQ(
            bundle.accounts[index].accountIdHex,
            value.bundle.accounts[index].accountIdHex);
        LOGOS_ASSERT_EQ(
            bundle.accounts[index].responseJson,
            value.bundle.accounts[index].responseJson);
    }
    LOGOS_ASSERT_EQ(
        bundle.checkpoint.lastOrderedActionId, 0U);
    LOGOS_ASSERT_EQ(authority.finalizedAt(), 101);

    for (std::size_t scenario = 0U; scenario < 3U; ++scenario) {
        palace::PalaceLezFinalizedAuthorityBundleV1 empty;
        empty.scope = value.bundle.scope;
        palace::AuthorityProjection retained;
        LOGOS_ASSERT_TRUE(
            palace::restoreFinalizedLezAuthorityStateV1(
                retained,
                value.bundle,
                value.expectation).accepted);
        const std::string palaceId = retained.palaceId();
        auto stable = genesis.stableAccounts;
        if (scenario == 0U) {
            stable.accountResponseJson.pop_back();
        } else if (scenario == 1U) {
            empty.accounts.push_back(
                value.bundle.accounts.front());
        } else {
            empty.checkpoint.finalizedBlockId = 99U;
        }
        const auto rejected =
            palace::applyFinalizedLezAuthorityActionV1(
                retained,
                empty,
                expectation,
                genesis.tracked,
                genesis.certificate,
                stable);
        LOGOS_ASSERT_FALSE(rejected.accepted);
        assertProjectionUnchanged(retained, palaceId, 100);
    }
}

LOGOS_TEST(lez_authority_state_rejects_digest_owner_order_and_checkpoint_confusion)
{
    const Fixture value = fixture();
    const FinalizedActionFixture action =
        finalizedPublish(value);

    for (std::size_t scenario = 0U; scenario < 4U; ++scenario) {
        palace::PalaceLezFinalizedAuthorityBundleV1 bundle =
            value.bundle;
        palace::AuthorityProjection authority;
        LOGOS_ASSERT_TRUE(
            palace::restoreFinalizedLezAuthorityStateV1(
                authority, bundle, value.expectation).accepted);
        const std::string palaceId = authority.palaceId();
        const auto before = bundle;
        auto priorExpectation = value.expectation;
        auto stable = action.stableAccounts;
        if (scenario == 0U) {
            stable.accountResponseJson[1] =
                moduleAccountJson(
                    repeated('0', 64U), {0x43U});
        } else if (scenario == 1U) {
            stable.accountResponseJson[0] =
                moduleAccountJson(
                    repeated('0', 64U),
                    palace::PalaceLezCodec::encodeRootRecord(
                        value.root));
        } else if (scenario == 2U) {
            std::swap(
                stable.accountResponseJson[0],
                stable.accountResponseJson[1]);
        } else {
            priorExpectation.checkpoint->
                finalizedBlockHashHex = repeated('8', 64U);
        }
        const auto rejected =
            palace::applyFinalizedLezAuthorityActionV1(
                authority,
                bundle,
                priorExpectation,
                action.tracked,
                action.certificate,
                stable);
        LOGOS_ASSERT_FALSE(rejected.accepted);
        LOGOS_ASSERT_TRUE(sameBundle(bundle, before));
        assertProjectionUnchanged(authority, palaceId, 100);
    }
}

LOGOS_TEST(lez_authority_state_rebuilds_history_and_filters_only_strict_system_signer)
{
    const Fixture value = fixture();
    palace::AuthorityProjection authority;
    palace::PalaceLezFinalizedAuthorityBundleV1 bundle;
    palace::PalaceLezStableAccountBatchV1 stable;
    stable.heightBefore = 105;
    stable.heightAfter = 105;
    stable.accountResponseJson = value.initializeResponses;

    const auto rebuilt =
        palace::rebuildFinalizedLezAuthorityStateV1(
            authority,
            bundle,
            value.bundle.scope,
            history(value),
            stable,
            value.bundle.checkpoint);
    LOGOS_ASSERT_TRUE(rebuilt.accepted);
    LOGOS_ASSERT_TRUE(sameBundle(bundle, value.bundle));
    LOGOS_ASSERT_EQ(authority.finalizedAt(), 100);
    LOGOS_ASSERT_EQ(authority.palaceId(),
        palace::PalaceLezCodec::bytes32Hex(
            value.initialization.palaceId));
}

LOGOS_TEST(lez_authority_state_rejects_pre_finality_and_moving_snapshots)
{
    const Fixture value = fixture();
    const FinalizedActionFixture action =
        finalizedPublish(value);
    for (std::size_t scenario = 0U; scenario < 2U; ++scenario) {
        palace::AuthorityProjection authority;
        palace::PalaceLezFinalizedAuthorityBundleV1 bundle =
            value.bundle;
        LOGOS_ASSERT_TRUE(
            palace::restoreFinalizedLezAuthorityStateV1(
                authority, bundle, value.expectation).accepted);
        const auto before = bundle;
        auto stable = action.stableAccounts;
        if (scenario == 0U) {
            stable.heightBefore = 100;
            stable.heightAfter = 100;
        } else {
            stable.heightAfter =
                stable.heightBefore + 1;
        }
        const auto rejected =
            palace::applyFinalizedLezAuthorityActionV1(
                authority,
                bundle,
                value.expectation,
                action.tracked,
                action.certificate,
                stable);
        LOGOS_ASSERT_FALSE(rejected.accepted);
        LOGOS_ASSERT_TRUE(sameBundle(bundle, before));
        assertProjectionUnchanged(
            authority,
            palace::PalaceLezCodec::bytes32Hex(
                value.initialization.palaceId),
            100);
    }
}

LOGOS_TEST(lez_authority_state_refreshes_history_checkpoint_transactionally)
{
    const Fixture value = fixture();
    palace::AuthorityProjection authority;
    palace::PalaceLezFinalizedAuthorityBundleV1 bundle;
    palace::PalaceLezStableAccountBatchV1 initialStable{
        105, 105, value.initializeResponses};
    LOGOS_ASSERT_TRUE(
        palace::rebuildFinalizedLezAuthorityStateV1(
            authority,
            bundle,
            value.bundle.scope,
            history(value),
            initialStable,
            value.bundle.checkpoint).accepted);

    const FinalizedActionFixture action =
        finalizedPublish(value);
    auto refreshedHistory = history(value);
    palace::PalaceLezFinalizedActionV3 refreshedAction;
    refreshedAction.transactionHash =
        repeated('2', 64U);
    refreshedAction.orderedActionId = 1U;
    refreshedAction.instruction =
        action.tracked.plan.instruction;
    refreshedAction.accountIdsHex =
        action.tracked.plan.accountIdsHex;
    refreshedHistory.actions.push_back(
        std::move(refreshedAction));
    refreshedHistory.latestFinalizedBlockId = 101U;
    refreshedHistory.latestFinalizedBlockHashHex =
        repeated('a', 64U);

    palace::PalaceLezAuthorityBundleCheckpointV1 checkpoint{
        101U, 101U, repeated('a', 64U), 1U};
    palace::PalaceLezStableAccountBatchV1 refreshedStable{
        120, 120, value.initializeResponses};
    const auto retainedBundle = bundle;
    const auto staleRootRejected =
        palace::rebuildFinalizedLezAuthorityStateV1(
            authority,
            bundle,
            value.bundle.scope,
            refreshedHistory,
            refreshedStable,
            checkpoint);
    LOGOS_ASSERT_FALSE(staleRootRejected.accepted);
    LOGOS_ASSERT_TRUE(
        sameBundle(bundle, retainedBundle));
    assertProjectionUnchanged(
        authority,
        palace::PalaceLezCodec::bytes32Hex(
            value.initialization.palaceId),
        100);

    refreshedStable.accountResponseJson[0] =
        action.stableAccounts.accountResponseJson[0];
    const auto refreshed =
        palace::rebuildFinalizedLezAuthorityStateV1(
            authority,
            bundle,
            value.bundle.scope,
            refreshedHistory,
            refreshedStable,
            checkpoint);
    LOGOS_ASSERT_TRUE(refreshed.accepted);
    LOGOS_ASSERT_EQ(
        bundle.checkpoint.lastOrderedActionId, 1U);
    LOGOS_ASSERT_EQ(
        bundle.checkpoint.finalizedBlockHeight, 101U);
    LOGOS_ASSERT_EQ(authority.finalizedAt(), 101);
}

LOGOS_TEST(lez_authority_state_rebuild_rejects_missing_extra_duplicate_pda_root_and_checkpoint)
{
    const Fixture value = fixture();
    for (std::size_t scenario = 0U; scenario < 9U; ++scenario) {
        palace::AuthorityProjection authority;
        palace::PalaceLezFinalizedAuthorityBundleV1 destination =
            value.bundle;
        LOGOS_ASSERT_TRUE(
            palace::restoreFinalizedLezAuthorityStateV1(
                authority,
                destination,
                value.expectation).accepted);
        const auto before = destination;
        const std::string palaceId = authority.palaceId();
        auto rebuiltHistory = history(value);
        auto stable = palace::PalaceLezStableAccountBatchV1{
            100, 100, value.initializeResponses};
        auto checkpoint = value.bundle.checkpoint;
        if (scenario == 0U) {
            stable.accountResponseJson.pop_back();
        } else if (scenario == 1U) {
            stable.accountResponseJson.push_back(
                value.initializeResponses.back());
        } else if (scenario == 2U) {
            rebuiltHistory.uniqueAccountIdsHex[2] =
                rebuiltHistory.uniqueAccountIdsHex[1];
        } else if (scenario == 3U) {
            rebuiltHistory.actions[0].accountIdsHex[2] =
                repeated('f', 64U);
            rebuiltHistory.uniqueAccountIdsHex[2] =
                repeated('f', 64U);
        } else if (scenario == 4U) {
            stable.accountResponseJson[2] =
                moduleAccountJson(
                    repeated('0', 64U), {0x01U});
        } else if (scenario == 5U) {
            palace::PalaceLezRootRecordV3 wrongRoot =
                value.root;
            wrongRoot.lastOrderedActionId = 1U;
            wrongRoot.revision = 1U;
            stable.accountResponseJson[0] =
                moduleAccountJson(
                    kProgramId,
                    palace::PalaceLezCodec::encodeRootRecord(
                        wrongRoot));
        } else if (scenario == 6U) {
            checkpoint.finalizedBlockHashHex =
                repeated('8', 64U);
        } else if (scenario == 7U) {
            stable.heightBefore = 99;
            stable.heightAfter = 99;
        } else {
            stable.heightAfter = 101;
        }
        const auto rejected =
            palace::rebuildFinalizedLezAuthorityStateV1(
                authority,
                destination,
                value.bundle.scope,
                rebuiltHistory,
                stable,
                checkpoint);
        LOGOS_ASSERT_FALSE(rejected.accepted);
        LOGOS_ASSERT_TRUE(sameBundle(destination, before));
        assertProjectionUnchanged(authority, palaceId, 100);
    }
}
