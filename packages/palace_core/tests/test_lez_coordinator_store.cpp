#include <logos_test.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#endif

#include "palace_lez.h"
#include "palace_action_journal.h"
#include "palace_core_impl.h"
#include "palace_lez_coordinator_store.h"
#include "palace_lez_submission_intent_store.h"
#include "palace_sha256.h"

namespace {

namespace fs = std::filesystem;

constexpr char kRecordFileName[] = "lez-coordinator-store-v1";
constexpr char kInterruptedFileName[] =
    "lez-coordinator-store-v1.next.interrupted";
constexpr char kProgramId[] =
    "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";
constexpr char kSigner[] =
    "6666666666666666666666666666666666666666666666666666666666666666";
constexpr char kBase64PublicKey[] =
    "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE=";

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(const std::string& label)
        : path_(
            fs::temp_directory_path()
            / (label + "-"
               + std::to_string(
                   std::chrono::steady_clock::now()
                       .time_since_epoch()
                       .count())))
    {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& path() const
    {
        return path_;
    }

private:
    fs::path path_;
};

std::string statusName(
    const palace::PalaceLezCoordinatorStoreStatus status)
{
    return palace::palaceLezCoordinatorStoreStatusName(status);
}

palace::PalaceLezBytes32 bytes(const std::uint8_t value)
{
    palace::PalaceLezBytes32 output{};
    output.fill(value);
    return output;
}

std::string hash(const std::uint64_t ordinal)
{
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(64) << ordinal;
    return output.str();
}

std::string hex(const std::vector<std::uint8_t>& value)
{
    constexpr char alphabet[] = "0123456789abcdef";
    std::string output(value.size() * 2U, '0');
    for (std::size_t index = 0U; index < value.size(); ++index) {
        output[index * 2U] = alphabet[value[index] >> 4U];
        output[index * 2U + 1U] =
            alphabet[value[index] & 0x0fU];
    }
    return output;
}

struct RootFixture {
    std::string dataHex;
    std::string digest;
};

RootFixture rootFixture()
{
    palace::PalaceLezRootRecordV3 root;
    root.palaceId = bytes(0x10U);
    root.title = "Palace";
    root.owner = bytes(0x20U);
    root.entryRoomId = bytes(0x30U);
    root.roomIds = {bytes(0x30U), bytes(0x40U)};
    root.activeManifestCid = "bafymanifest";
    root.userCount = 3U;
    root.grantCount = 2U;
    root.banCount = 1U;
    root.sharedStateCount = 1U;
    root.revision = 7U;
    root.lastOrderedActionId = 7U;
    const std::vector<std::uint8_t> encoded =
        palace::PalaceLezCodec::encodeRootRecord(root);
    LOGOS_ASSERT_FALSE(encoded.empty());
    return {
        hex(encoded),
        palace::PalaceLezCodec::sha256Hex(encoded),
    };
}

palace::PalaceLezNetworkFingerprint fingerprint()
{
    return {
        "lez-testnet",
        "0.4.0-alpha.2",
        "9e140c6d9529ee313d7bf4627677b4e7f552ab34",
        "6e0013f5b771fc1b96a65361b531f289111b8aaa",
        "0.2.0",
        "a58fbce2ff48c58b7bb5001b1a27e64b9596ee3a",
        kProgramId,
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    };
}

std::string rootAccountJson(const std::string& dataHex)
{
    return "{\"program_owner\":\"" + std::string(kProgramId)
        + "\",\"balance\":\"00000000000000000000000000000000\","
          "\"nonce\":\"00000000000000000000000000000000\",\"data\":\""
        + dataHex + "\"}";
}

std::string indexerJson(
    const palace::PalaceLezTransactionPlanV3& plan,
    const std::string& transactionHash)
{
    palace::PalaceLezBytes32 program{};
    LOGOS_ASSERT_TRUE(palace::PalaceLezCodec::parseBytes32Hex(
        plan.programIdHex, program));

    std::ostringstream output;
    output << "[{\"Public\":{\"hash\":\"" << transactionHash
           << "\",\"message\":{\"program_id\":\""
           << palace::PalaceLezCodec::accountIdBase58(program)
           << "\",\"account_ids\":[";
    for (std::size_t index = 0U; index < plan.accountIdsHex.size(); ++index) {
        palace::PalaceLezBytes32 account{};
        LOGOS_ASSERT_TRUE(palace::PalaceLezCodec::parseBytes32Hex(
            plan.accountIdsHex[index], account));
        if (index != 0U)
            output << ',';
        output << '"'
               << palace::PalaceLezCodec::accountIdBase58(account)
               << '"';
    }
    output << "],\"nonces\":[";
    for (std::size_t index = 0U; index < plan.accountIdsHex.size(); ++index) {
        if (index != 0U)
            output << ',';
        output << '0';
    }
    output << "],\"instruction_data\":[";
    for (std::size_t index = 0U; index < plan.instructionWords.size(); ++index) {
        if (index != 0U)
            output << ',';
        output << plan.instructionWords[index];
    }
    output << "]},\"witness_set\":{\"signatures_and_public_keys\":[";
    bool firstSignature = true;
    for (const bool signingRequired : plan.signingRequirements) {
        if (!signingRequired)
            continue;
        if (!firstSignature)
            output << ',';
        firstSignature = false;
        output << "[\"" << std::string(128U, '1') << "\",\""
               << kBase64PublicKey << "\"]";
    }
    output << "],\"proof\":null}}}]";
    return output.str();
}

void populate(
    palace::PalaceLezTransactionCoordinator& coordinator,
    const palace::PalaceLezTransactionStage stage,
    const std::uint64_t transactionOrdinal)
{
    const palace::PalaceLezNetworkFingerprint network = fingerprint();
    LOGOS_ASSERT_TRUE(
        coordinator.configureNetworkFingerprint(network, network).accepted);
    LOGOS_ASSERT_TRUE(coordinator.activate());

    const palace::PalaceLezTransactionPlanV3 plan =
        palace::PalaceLezCodec::buildTransaction(
            kProgramId,
            kSigner,
            palace::PalaceLezInstructionV3{
                palace::PalaceLezRevokeCapabilityV3{
                    7U, bytes(0x12U)}});
    LOGOS_ASSERT_TRUE(plan.accepted);
    const std::string transactionHash = hash(transactionOrdinal);
    const RootFixture root = rootFixture();
    LOGOS_ASSERT_TRUE(coordinator.registerSubmission(
        plan,
        "{\"success\":true,\"tx_hash\":\"" + transactionHash
            + "\",\"secrets\":[],\"error\":\"\"}",
        root.digest).accepted);

    if (stage == palace::PalaceLezTransactionStage::Observed
        || stage == palace::PalaceLezTransactionStage::Finalized) {
        const palace::PalaceLezCoordinatorUpdate observed =
            coordinator.observeStableRoot(
            transactionHash,
            42,
            rootAccountJson(root.dataHex),
            42);
        LOGOS_ASSERT_EQ(observed.reason, std::string("observed"));
        LOGOS_ASSERT_TRUE(observed.accepted);
    }
    if (stage == palace::PalaceLezTransactionStage::Finalized) {
        LOGOS_ASSERT_TRUE(coordinator.reconcileFinality(
            transactionHash,
            indexerJson(plan, transactionHash)).accepted);
    }
    coordinator.interrupt();
}

std::vector<std::uint8_t> readBytes(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

void ownerOnly(const fs::path& path)
{
#if defined(__unix__) || defined(__APPLE__)
    LOGOS_ASSERT_EQ(::chmod(path.c_str(), 0600), 0);
#else
    static_cast<void>(path);
#endif
}

void writeBytes(
    const fs::path& path,
    const std::vector<std::uint8_t>& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    output.close();
    LOGOS_ASSERT_TRUE(output.good());
    ownerOnly(path);
}

void writeText(const fs::path& path, const std::string& value)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << value;
    output.close();
    LOGOS_ASSERT_TRUE(output.good());
    ownerOnly(path);
}

std::vector<std::uint8_t> snapshotBytes(
    const palace::PalaceLezTransactionCoordinator& coordinator)
{
    const palace::PalaceLezCoordinatorSnapshot snapshot =
        coordinator.snapshot();
    LOGOS_ASSERT_TRUE(snapshot.accepted);
    return snapshot.bytes;
}

void replaceChecksum(std::vector<std::uint8_t>& record)
{
    constexpr std::size_t checksumBytes = 64U;
    LOGOS_ASSERT_GT(record.size(), checksumBytes);
    const std::size_t checksumOffset = record.size() - checksumBytes;
    const std::string body(
        reinterpret_cast<const char*>(record.data()), checksumOffset);
    const std::string checksum = palace::crypto::sha256Hex(body);
    LOGOS_ASSERT_EQ(checksum.size(), checksumBytes);
    std::copy(
        checksum.begin(),
        checksum.end(),
        record.begin() + static_cast<std::ptrdiff_t>(checksumOffset));
}

} // namespace

LOGOS_TEST(lez_coordinator_store_distinguishes_invalid_missing_and_empty) {
    palace::PalaceLezTransactionCoordinator coordinator;
    palace::PalaceLezCoordinatorStore invalid("");
    LOGOS_ASSERT_EQ(
        statusName(invalid.load(coordinator)),
        std::string("invalid_argument"));
    LOGOS_ASSERT_EQ(
        statusName(invalid.save(coordinator)),
        std::string("invalid_argument"));

    TemporaryDirectory missing("palace-lez-store-missing");
    palace::PalaceLezCoordinatorStore missingStore(missing.path().string());
    LOGOS_ASSERT_EQ(
        statusName(missingStore.load(coordinator)),
        std::string("not_found"));
    LOGOS_ASSERT_FALSE(fs::exists(missing.path()));

    fs::create_directories(missing.path());
    writeText(missing.path() / kRecordFileName, "");
    LOGOS_ASSERT_EQ(
        statusName(missingStore.load(coordinator)),
        std::string("invalid_record"));
    LOGOS_ASSERT_TRUE(coordinator.transactions().empty());

    TemporaryDirectory emptyState("palace-lez-store-empty-state");
    palace::PalaceLezCoordinatorStore emptyStore(
        emptyState.path().string());
    LOGOS_ASSERT_EQ(
        statusName(emptyStore.save(coordinator)),
        std::string("saved"));
    palace::PalaceLezTransactionCoordinator restored;
    LOGOS_ASSERT_EQ(
        statusName(emptyStore.load(restored)),
        std::string("loaded"));
    LOGOS_ASSERT_TRUE(restored.transactions().empty());
    LOGOS_ASSERT_FALSE(restored.running());
    LOGOS_ASSERT_TRUE(snapshotBytes(restored) == snapshotBytes(coordinator));
}

LOGOS_TEST(lez_coordinator_store_restart_preserves_every_stage_exactly) {
    const std::array<palace::PalaceLezTransactionStage, 3> stages{{
        palace::PalaceLezTransactionStage::Submitted,
        palace::PalaceLezTransactionStage::Observed,
        palace::PalaceLezTransactionStage::Finalized,
    }};
    std::uint64_t ordinal = 100U;
    for (const palace::PalaceLezTransactionStage stage : stages) {
        TemporaryDirectory root(
            "palace-lez-store-stage-" + std::to_string(ordinal));
        palace::PalaceLezTransactionCoordinator source;
        populate(source, stage, ordinal);
        palace::PalaceLezCoordinatorStore store(root.path().string());
        LOGOS_ASSERT_EQ(
            statusName(store.save(source)), std::string("saved"));
        const std::vector<std::uint8_t> record =
            readBytes(root.path() / kRecordFileName);
        const std::vector<std::uint8_t> expectedSnapshot =
            snapshotBytes(source);
        LOGOS_ASSERT_GT(record.size(), static_cast<std::size_t>(80U));
        const std::vector<std::uint8_t> framedSnapshot(
            record.begin() + 16,
            record.end() - 64);
        LOGOS_ASSERT_TRUE(framedSnapshot == expectedSnapshot);

#if defined(__unix__) || defined(__APPLE__)
        struct stat status {};
        LOGOS_ASSERT_EQ(
            ::stat((root.path() / kRecordFileName).c_str(), &status), 0);
        LOGOS_ASSERT_EQ(
            static_cast<unsigned int>(status.st_mode & 0777),
            static_cast<unsigned int>(0600));
#endif

        palace::PalaceLezTransactionCoordinator restored;
        LOGOS_ASSERT_EQ(
            statusName(store.load(restored)), std::string("loaded"));
        LOGOS_ASSERT_FALSE(restored.running());
        LOGOS_ASSERT_EQ(restored.transactions().size(), 1U);
        LOGOS_ASSERT_EQ(
            static_cast<std::uint8_t>(
                restored.transactions().front().stage),
            static_cast<std::uint8_t>(stage));
        LOGOS_ASSERT_TRUE(
            snapshotBytes(restored) == expectedSnapshot);
        ++ordinal;
    }
}

LOGOS_TEST(
    core_lez_repairs_durable_coordinator_after_accept_to_journal_crash) {
    TemporaryDirectory root(
        "palace-lez-store-accept-to-journal-crash");
    palace::PalaceLezTransactionCoordinator source;
    populate(
        source,
        palace::PalaceLezTransactionStage::Submitted,
        150U);
    palace::PalaceLezCoordinatorStore coordinatorStore(
        root.path().string());
    LOGOS_ASSERT_EQ(
        statusName(coordinatorStore.save(source)),
        std::string("saved"));

    palace::ActionJournal queued;
    LOGOS_ASSERT_TRUE(queued.createDraft("7"));
    LOGOS_ASSERT_TRUE(queued.queue("7"));
    palace::ActionJournalStore journalStore(
        root.path().string());
    LOGOS_ASSERT_TRUE(journalStore.save(queued));

    const palace::PalaceLezTrackedTransaction tracked =
        source.transactions().front();
    palace::PalaceLezSubmissionIntentV1 intent;
    intent.actionId = "7";
    intent.minimumFinalizedBlockExclusive = 41U;
    intent.plan = tracked.plan;
    intent.expectedRootDataSha256Hex =
        tracked.expectedRootDataSha256Hex;
    palace::PalaceLezSubmissionIntentStore intentStore(
        root.path().string());
    LOGOS_ASSERT_TRUE(
        intentStore.save(intent)
        == palace::PalaceLezSubmissionIntentStoreStatus::Saved);
    intent.phase =
        palace::PalaceLezSubmissionIntentPhase::
            MayHaveBeenSubmitted;
    LOGOS_ASSERT_TRUE(
        intentStore.save(intent)
        == palace::PalaceLezSubmissionIntentStoreStatus::Saved);

    palace::PalaceLezTransactionCoordinator restartedCoordinator;
    LOGOS_ASSERT_TRUE(
        coordinatorStore.load(restartedCoordinator)
        == palace::PalaceLezCoordinatorStoreStatus::Loaded);
    palace::ActionJournal restartedJournal;
    LOGOS_ASSERT_TRUE(journalStore.load(restartedJournal));
    palace::PalaceLezSubmissionIntentV1 restartedIntent;
    LOGOS_ASSERT_TRUE(
        intentStore.load(restartedIntent)
        == palace::PalaceLezSubmissionIntentStoreStatus::Loaded);

    palace::PalaceLezTransactionPlanV3 drifted =
        restartedIntent.plan;
    drifted.instructionWords.back() ^= 1U;
    LOGOS_ASSERT_FALSE(
        palace::core_detail::recoverTrackedPalaceSubmissionV1(
            "7",
            drifted,
            restartedIntent,
            restartedCoordinator.transactions(),
            restartedJournal).accepted);
    std::vector<palace::PalaceLezTrackedTransaction>
        wrongExpectedRoot = restartedCoordinator.transactions();
    wrongExpectedRoot.front().expectedRootDataSha256Hex =
        std::string(64U, 'f');
    LOGOS_ASSERT_FALSE(
        palace::core_detail::recoverTrackedPalaceSubmissionV1(
            "7",
            restartedIntent.plan,
            restartedIntent,
            wrongExpectedRoot,
            restartedJournal).accepted);
    std::vector<palace::PalaceLezTrackedTransaction> duplicated =
        restartedCoordinator.transactions();
    duplicated.push_back(duplicated.front());
    LOGOS_ASSERT_FALSE(
        palace::core_detail::recoverTrackedPalaceSubmissionV1(
            "7",
            restartedIntent.plan,
            restartedIntent,
            duplicated,
            restartedJournal).accepted);

    auto repaired =
        palace::core_detail::recoverTrackedPalaceSubmissionV1(
            "7",
            restartedIntent.plan,
            restartedIntent,
            restartedCoordinator.transactions(),
            restartedJournal);
    LOGOS_ASSERT_TRUE(repaired.accepted);
    LOGOS_ASSERT_EQ(
        repaired.reason,
        std::string("tracked-submission-recovered"));
    LOGOS_ASSERT_EQ(
        repaired.transactionHash,
        tracked.transactionHash);
    LOGOS_ASSERT_EQ(
        static_cast<std::uint8_t>(
            repaired.repairedJournal.status("7").durableStage),
        static_cast<std::uint8_t>(
            palace::DurableActionStage::SubmittedToLez));
    LOGOS_ASSERT_TRUE(
        repaired.committedIntent.phase
        == palace::PalaceLezSubmissionIntentPhase::Committed);
    LOGOS_ASSERT_EQ(
        repaired.committedIntent.transactionHash,
        tracked.transactionHash);

    LOGOS_ASSERT_TRUE(
        journalStore.save(repaired.repairedJournal));
    LOGOS_ASSERT_TRUE(
        intentStore.save(repaired.committedIntent)
        == palace::PalaceLezSubmissionIntentStoreStatus::Saved);

    palace::ActionJournal verifiedJournal;
    palace::PalaceLezSubmissionIntentV1 verifiedIntent;
    LOGOS_ASSERT_TRUE(journalStore.load(verifiedJournal));
    LOGOS_ASSERT_TRUE(
        intentStore.load(verifiedIntent)
        == palace::PalaceLezSubmissionIntentStoreStatus::Loaded);
    LOGOS_ASSERT_EQ(
        verifiedJournal.status("7").transactionHash,
        tracked.transactionHash);
    LOGOS_ASSERT_TRUE(
        verifiedIntent.phase
        == palace::PalaceLezSubmissionIntentPhase::Committed);
    LOGOS_ASSERT_EQ(
        verifiedIntent.transactionHash,
        tracked.transactionHash);
}

LOGOS_TEST(
    core_lez_retry_persists_volatile_coordinator_before_repairing_journal) {
    TemporaryDirectory root(
        "palace-lez-store-volatile-retry");
    palace::PalaceLezTransactionCoordinator coordinator;
    const palace::PalaceLezNetworkFingerprint network =
        fingerprint();
    LOGOS_ASSERT_TRUE(
        coordinator.configureNetworkFingerprint(
            network, network).accepted);
    LOGOS_ASSERT_TRUE(coordinator.activate());

    palace::PalaceLezCoordinatorStore coordinatorStore(
        root.path().string());
    LOGOS_ASSERT_TRUE(
        palace::core_detail::persistTrackedPalaceSubmissionV1(
            coordinator, &coordinatorStore)
        == palace::PalaceLezCoordinatorStoreStatus::Saved);

    const palace::PalaceLezTransactionPlanV3 plan =
        palace::PalaceLezCodec::buildTransaction(
            kProgramId,
            kSigner,
            palace::PalaceLezInstructionV3{
                palace::PalaceLezRevokeCapabilityV3{
                    7U, bytes(0x12U)}});
    LOGOS_ASSERT_TRUE(plan.accepted);
    const RootFixture rootRecord = rootFixture();
    const std::string transactionHash = hash(151U);
    LOGOS_ASSERT_TRUE(
        coordinator.registerSubmission(
            plan,
            "{\"success\":true,\"tx_hash\":\""
                + transactionHash
                + "\",\"secrets\":[],\"error\":\"\"}",
            rootRecord.digest).accepted);

    palace::ActionJournal queued;
    LOGOS_ASSERT_TRUE(queued.createDraft("7"));
    LOGOS_ASSERT_TRUE(queued.queue("7"));
    palace::PalaceLezSubmissionIntentV1 intent;
    intent.actionId = "7";
    intent.minimumFinalizedBlockExclusive = 41U;
    intent.plan = plan;
    intent.expectedRootDataSha256Hex =
        rootRecord.digest;
    palace::PalaceLezSubmissionIntentStore intentStore(
        root.path().string());
    LOGOS_ASSERT_TRUE(
        intentStore.save(intent)
        == palace::PalaceLezSubmissionIntentStoreStatus::Saved);
    intent.phase =
        palace::PalaceLezSubmissionIntentPhase::
            MayHaveBeenSubmitted;
    LOGOS_ASSERT_TRUE(
        intentStore.save(intent)
        == palace::PalaceLezSubmissionIntentStoreStatus::Saved);

    palace::PalaceLezCoordinatorStore failingStore("");
    LOGOS_ASSERT_TRUE(
        palace::core_detail::persistTrackedPalaceSubmissionV1(
            coordinator, &failingStore)
        == palace::PalaceLezCoordinatorStoreStatus::
            InvalidArgument);
    LOGOS_ASSERT_TRUE(
        queued.status("7").durableStage
        == palace::DurableActionStage::Queued);
    LOGOS_ASSERT_TRUE(
        intent.phase
        == palace::PalaceLezSubmissionIntentPhase::
            MayHaveBeenSubmitted);

    palace::PalaceLezTransactionCoordinator beforeRetryRestart;
    LOGOS_ASSERT_TRUE(
        coordinatorStore.load(beforeRetryRestart)
        == palace::PalaceLezCoordinatorStoreStatus::Loaded);
    LOGOS_ASSERT_TRUE(
        beforeRetryRestart.transactions().empty());

    LOGOS_ASSERT_TRUE(
        palace::core_detail::persistTrackedPalaceSubmissionV1(
            coordinator, &coordinatorStore)
        == palace::PalaceLezCoordinatorStoreStatus::Saved);
    const auto repaired =
        palace::core_detail::recoverTrackedPalaceSubmissionV1(
            "7",
            plan,
            intent,
            coordinator.transactions(),
            queued);
    LOGOS_ASSERT_TRUE(repaired.accepted);

    palace::ActionJournalStore journalStore(
        root.path().string());
    LOGOS_ASSERT_TRUE(
        journalStore.save(repaired.repairedJournal));
    LOGOS_ASSERT_TRUE(
        intentStore.save(repaired.committedIntent)
        == palace::PalaceLezSubmissionIntentStoreStatus::Saved);

    palace::PalaceLezTransactionCoordinator restartedCoordinator;
    palace::ActionJournal restartedJournal;
    palace::PalaceLezSubmissionIntentV1 restartedIntent;
    LOGOS_ASSERT_TRUE(
        coordinatorStore.load(restartedCoordinator)
        == palace::PalaceLezCoordinatorStoreStatus::Loaded);
    LOGOS_ASSERT_TRUE(journalStore.load(restartedJournal));
    LOGOS_ASSERT_TRUE(
        intentStore.load(restartedIntent)
        == palace::PalaceLezSubmissionIntentStoreStatus::Loaded);
    LOGOS_ASSERT_EQ(
        restartedCoordinator.transactions().size(),
        static_cast<std::size_t>(1U));
    LOGOS_ASSERT_EQ(
        restartedCoordinator.transactions().front()
            .transactionHash,
        transactionHash);
    LOGOS_ASSERT_TRUE(
        restartedJournal.status("7").durableStage
        == palace::DurableActionStage::SubmittedToLez);
    LOGOS_ASSERT_EQ(
        restartedJournal.status("7").transactionHash,
        transactionHash);
    LOGOS_ASSERT_TRUE(
        restartedIntent.phase
        == palace::PalaceLezSubmissionIntentPhase::Committed);
    LOGOS_ASSERT_EQ(
        restartedIntent.transactionHash,
        transactionHash);
}

LOGOS_TEST(lez_coordinator_store_rejects_every_frame_failure_without_mutation) {
    TemporaryDirectory root("palace-lez-store-corruption");
    palace::PalaceLezTransactionCoordinator source;
    populate(
        source,
        palace::PalaceLezTransactionStage::Observed,
        200U);
    palace::PalaceLezCoordinatorStore store(root.path().string());
    LOGOS_ASSERT_EQ(
        statusName(store.save(source)), std::string("saved"));
    const std::vector<std::uint8_t> valid =
        readBytes(root.path() / kRecordFileName);
    LOGOS_ASSERT_GT(valid.size(), static_cast<std::size_t>(80U));

    palace::PalaceLezTransactionCoordinator retained;
    populate(
        retained,
        palace::PalaceLezTransactionStage::Submitted,
        201U);
    const std::vector<std::uint8_t> retainedSnapshot =
        snapshotBytes(retained);

    const auto rejected = [&](const std::vector<std::uint8_t>& record) {
        writeBytes(root.path() / kRecordFileName, record);
        LOGOS_ASSERT_EQ(
            statusName(store.load(retained)),
            std::string("invalid_record"));
        LOGOS_ASSERT_TRUE(
            snapshotBytes(retained) == retainedSnapshot);
    };

    std::vector<std::uint8_t> truncated = valid;
    truncated.pop_back();
    rejected(truncated);

    std::vector<std::uint8_t> trailing = valid;
    trailing.push_back(0U);
    rejected(trailing);

    std::vector<std::uint8_t> wrongVersion = valid;
    wrongVersion[8U] ^= 0x01U;
    rejected(wrongVersion);

    std::vector<std::uint8_t> wrongLength = valid;
    wrongLength[12U] ^= 0x01U;
    rejected(wrongLength);

    std::vector<std::uint8_t> wrongChecksum = valid;
    wrongChecksum.back() ^= 0x01U;
    rejected(wrongChecksum);

    std::vector<std::uint8_t> invalidInner = valid;
    invalidInner[16U] ^= 0x01U;
    replaceChecksum(invalidInner);
    rejected(invalidInner);

    std::vector<std::uint8_t> oversized(600000U, 0U);
    rejected(oversized);
}

LOGOS_TEST(lez_coordinator_store_ignores_interrupted_write_artifacts) {
    TemporaryDirectory root("palace-lez-store-interrupted");
    palace::PalaceLezTransactionCoordinator source;
    populate(
        source,
        palace::PalaceLezTransactionStage::Finalized,
        300U);
    palace::PalaceLezCoordinatorStore store(root.path().string());
    LOGOS_ASSERT_EQ(
        statusName(store.save(source)), std::string("saved"));
    writeText(
        root.path() / kInterruptedFileName,
        "partial-uncommitted-record");

    palace::PalaceLezTransactionCoordinator restored;
    LOGOS_ASSERT_EQ(
        statusName(store.load(restored)), std::string("loaded"));
    LOGOS_ASSERT_TRUE(snapshotBytes(restored) == snapshotBytes(source));

    TemporaryDirectory artifactOnly(
        "palace-lez-store-interrupted-only");
    fs::create_directories(artifactOnly.path());
    writeText(
        artifactOnly.path() / kInterruptedFileName,
        "partial-uncommitted-record");
    palace::PalaceLezCoordinatorStore artifactStore(
        artifactOnly.path().string());
    palace::PalaceLezTransactionCoordinator retained;
    populate(
        retained,
        palace::PalaceLezTransactionStage::Submitted,
        301U);
    const std::vector<std::uint8_t> before = snapshotBytes(retained);
    LOGOS_ASSERT_EQ(
        statusName(artifactStore.load(retained)),
        std::string("not_found"));
    LOGOS_ASSERT_TRUE(snapshotBytes(retained) == before);
}

LOGOS_TEST(lez_coordinator_store_atomic_replace_keeps_latest_exact_snapshot) {
    TemporaryDirectory root("palace-lez-store-replace");
    palace::PalaceLezCoordinatorStore store(root.path().string());
    palace::PalaceLezTransactionCoordinator submitted;
    populate(
        submitted,
        palace::PalaceLezTransactionStage::Submitted,
        400U);
    LOGOS_ASSERT_EQ(
        statusName(store.save(submitted)), std::string("saved"));

    palace::PalaceLezTransactionCoordinator finalized;
    populate(
        finalized,
        palace::PalaceLezTransactionStage::Finalized,
        401U);
    LOGOS_ASSERT_EQ(
        statusName(store.save(finalized)), std::string("saved"));

    palace::PalaceLezTransactionCoordinator restored;
    LOGOS_ASSERT_EQ(
        statusName(store.load(restored)), std::string("loaded"));
    LOGOS_ASSERT_TRUE(
        snapshotBytes(restored) == snapshotBytes(finalized));
    LOGOS_ASSERT_EQ(restored.transactions().size(), 1U);
    LOGOS_ASSERT_EQ(
        static_cast<std::uint8_t>(
            restored.transactions().front().stage),
        static_cast<std::uint8_t>(
            palace::PalaceLezTransactionStage::Finalized));
}

LOGOS_TEST(lez_coordinator_store_failed_target_restore_preserves_live_state) {
    TemporaryDirectory root("palace-lez-store-live-target");
    palace::PalaceLezTransactionCoordinator persisted;
    populate(
        persisted,
        palace::PalaceLezTransactionStage::Observed,
        500U);
    palace::PalaceLezCoordinatorStore store(root.path().string());
    LOGOS_ASSERT_EQ(
        statusName(store.save(persisted)), std::string("saved"));

    palace::PalaceLezTransactionCoordinator running;
    const palace::PalaceLezNetworkFingerprint network = fingerprint();
    LOGOS_ASSERT_TRUE(
        running.configureNetworkFingerprint(network, network).accepted);
    LOGOS_ASSERT_TRUE(running.activate());
    const std::vector<std::uint8_t> before = snapshotBytes(running);
    LOGOS_ASSERT_EQ(
        statusName(store.load(running)),
        std::string("coordinator_rejected"));
    LOGOS_ASSERT_TRUE(running.running());
    LOGOS_ASSERT_TRUE(snapshotBytes(running) == before);
}

LOGOS_TEST(lez_coordinator_store_rejects_symlinks_and_insecure_files) {
#if defined(__unix__) || defined(__APPLE__)
    TemporaryDirectory root("palace-lez-store-symlink");
    fs::create_directories(root.path());
    const fs::path outside = root.path().parent_path()
        / (root.path().filename().string() + "-outside");
    {
        std::error_code error;
        fs::remove(outside, error);
    }
    writeText(outside, "outside-must-not-change");
    fs::create_symlink(outside, root.path() / kRecordFileName);

    palace::PalaceLezTransactionCoordinator source;
    populate(
        source,
        palace::PalaceLezTransactionStage::Submitted,
        600U);
    palace::PalaceLezCoordinatorStore store(root.path().string());
    palace::PalaceLezTransactionCoordinator target;
    LOGOS_ASSERT_EQ(
        statusName(store.load(target)), std::string("unsafe_path"));
    LOGOS_ASSERT_EQ(
        statusName(store.save(source)), std::string("unsafe_path"));
    const std::vector<std::uint8_t> outsideBytes = readBytes(outside);
    LOGOS_ASSERT_EQ(
        std::string(
            outsideBytes.begin(),
            outsideBytes.end()),
        std::string("outside-must-not-change"));

    fs::remove(root.path() / kRecordFileName);
    LOGOS_ASSERT_EQ(
        statusName(store.save(source)), std::string("saved"));
    LOGOS_ASSERT_EQ(
        ::chmod((root.path() / kRecordFileName).c_str(), 0644), 0);
    LOGOS_ASSERT_EQ(
        statusName(store.load(target)),
        std::string("insecure_permissions"));

    TemporaryDirectory linkedRoot("palace-lez-store-linked-root");
    TemporaryDirectory linkedTarget("palace-lez-store-linked-target");
    fs::create_directories(linkedTarget.path());
    fs::create_directory_symlink(
        linkedTarget.path(), linkedRoot.path());
    palace::PalaceLezCoordinatorStore linkedStore(
        linkedRoot.path().string());
    LOGOS_ASSERT_EQ(
        statusName(linkedStore.load(target)),
        std::string("unsafe_path"));
    LOGOS_ASSERT_EQ(
        statusName(linkedStore.save(source)),
        std::string("unsafe_path"));
    LOGOS_ASSERT_FALSE(
        fs::exists(linkedTarget.path() / kRecordFileName));

    std::error_code error;
    fs::remove(outside, error);
#endif
}
