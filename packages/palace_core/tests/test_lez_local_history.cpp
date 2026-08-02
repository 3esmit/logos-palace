#include <logos_test.h>

#include <QBuffer>
#include <QImage>
#include <QTemporaryDir>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "palace_lez.h"
#include "palace_lez_local_history.h"
#include "palace_sha256.h"
#include "palace_storage_mvp.h"
#include "palace_storage_mvp_catalog_store.h"

namespace {

constexpr const char *kProgramIdHex =
    "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";
constexpr const char *kRootAccountIdHex =
    "99e683e8adac1e4f591b42879ae3e6480414f644c3eda3b592da3d62d72853b3";
constexpr const char *kSignerHex =
    "6666666666666666666666666666666666666666666666666666666666666666";

std::string repeated(const char value, const std::size_t count) {
  return std::string(count, value);
}

palace::PalaceLezBytes32 bytes(const std::uint8_t value) {
  palace::PalaceLezBytes32 output{};
  output.fill(value);
  return output;
}

std::string base58(const std::string &accountIdHex) {
  palace::PalaceLezBytes32 accountId{};
  LOGOS_ASSERT_TRUE(
      palace::PalaceLezCodec::parseBytes32Hex(accountIdHex, accountId));
  return palace::PalaceLezCodec::accountIdBase58(accountId);
}

std::string programIdJson(const std::string &programIdHex) {
  palace::PalaceLezBytes32 programId{};
  LOGOS_ASSERT_TRUE(
      palace::PalaceLezCodec::parseBytes32Hex(programIdHex, programId));
  std::string json = "[";
  for (std::size_t word = 0U; word < 8U; ++word) {
    if (word != 0U)
      json += ',';
    const std::size_t offset = word * 4U;
    const std::uint32_t value =
        static_cast<std::uint32_t>(programId[offset + 0U]) |
        (static_cast<std::uint32_t>(programId[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(programId[offset + 2U]) << 16U) |
        (static_cast<std::uint32_t>(programId[offset + 3U]) << 24U);
    json += std::to_string(value);
  }
  return json + ']';
}

palace::PalaceLezInstructionV3 initializeInstruction() {
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

palace::PalaceLezInstructionV3 initializeInstruction(const std::uint8_t palace) {
  palace::PalaceLezInitializeV3 initialize =
      std::get<palace::PalaceLezInitializeV3>(initializeInstruction().payload);
  initialize.palaceId = bytes(palace);
  initialize.ownerGrantId = bytes(static_cast<std::uint8_t>(palace + 1U));
  initialize.entryRoomId = bytes(static_cast<std::uint8_t>(palace + 2U));
  initialize.secondaryRoomId = bytes(static_cast<std::uint8_t>(palace + 3U));
  return palace::PalaceLezInstructionV3{initialize};
}

palace::PalaceLezInstructionV3 initializeInstruction(
    const std::string &palaceManifestCid, const std::string &atriumManifestCid,
    const std::string &loungeManifestCid, const std::string &scriptCid) {
  palace::PalaceLezInitializeV3 initialize =
      std::get<palace::PalaceLezInitializeV3>(initializeInstruction().payload);
  initialize.activeManifestCid = palaceManifestCid;
  initialize.entryRoom.manifestCid = atriumManifestCid;
  initialize.secondaryRoom.manifestCid = loungeManifestCid;
  initialize.entryRoom.scriptBundleCid = scriptCid;
  initialize.secondaryRoom.scriptBundleCid = scriptCid;
  return palace::PalaceLezInstructionV3{initialize};
}

palace::PalaceLezInstructionV3 actionOneInstruction() {
  return palace::PalaceLezInstructionV3{palace::PalaceLezRegisterUserV3{
      1U,
      {"Bob", bytes(0x52U), 1U, std::nullopt},
  }};
}

palace::PalaceLezInstructionV3 doorOpenInstruction() {
  return palace::PalaceLezInstructionV3{
      palace::PalaceLezUpdateSharedStateV3{
          1U,
          bytes(0x11U),
          bytes(0x61U),
          bytes(0x31U),
          1U,
          {0x01U},
          bytes(0x71U),
      }};
}

palace::PalaceLezInstructionV3 actionTwoInstruction() {
  return palace::PalaceLezInstructionV3{palace::PalaceLezPublishManifestV3{
      2U,
      "bafysecondmanifest",
  }};
}

palace::PalaceLezTransactionPlanV3
plan(const palace::PalaceLezInstructionV3 &instruction) {
  const auto result = palace::PalaceLezCodec::buildTransaction(
      kProgramIdHex, kSignerHex, instruction);
  LOGOS_ASSERT_TRUE(result.accepted);
  return result;
}

std::string
publicTransaction(const palace::PalaceLezTransactionPlanV3 &transactionPlan,
                  const std::string &hash) {
  std::string accountsJson = "[";
  for (std::size_t index = 0U; index < transactionPlan.accountIdsHex.size();
       ++index) {
    if (index != 0U)
      accountsJson += ',';
    accountsJson += '"' + base58(transactionPlan.accountIdsHex[index]) + '"';
  }
  accountsJson += ']';

  std::string wordsJson = "[";
  for (std::size_t index = 0U; index < transactionPlan.instructionWords.size();
       ++index) {
    if (index != 0U)
      wordsJson += ',';
    wordsJson += std::to_string(transactionPlan.instructionWords[index]);
  }
  wordsJson += ']';

  return "{\"transaction_hash\":\"" + hash +
         "\",\"program_id\":" + programIdJson(transactionPlan.programIdHex) +
         ",\"account_ids\":" + accountsJson +
         ",\"instruction_data\":" + wordsJson + "}";
}

std::string blockHeader(const std::uint64_t blockId, const char hashDigit,
                        const char previousHashDigit) {
  return "{\"block_id\":" + std::to_string(blockId) + ",\"block_hash\":\"" +
         repeated(hashDigit, 64U) + "\",\"previous_block_hash\":\"" +
         repeated(previousHashDigit, 64U) + "\"}";
}

std::string block(const std::uint64_t blockId, const char hashDigit,
                  const char previousHashDigit,
                  const std::vector<std::string> &transactions) {
  std::string transactionJson = "[";
  for (std::size_t index = 0U; index < transactions.size(); ++index) {
    if (index != 0U)
      transactionJson += ',';
    transactionJson += transactions[index];
  }
  transactionJson += ']';
  return "{\"header\":" + blockHeader(blockId, hashDigit, previousHashDigit) +
         ",\"public_transactions\":" + transactionJson + "}";
}

std::string page(const std::string &snapshotTip,
                 const std::vector<std::string> &blocks,
                 const std::optional<std::uint64_t> nextBlockId) {
  std::string blocksJson = "[";
  for (std::size_t index = 0U; index < blocks.size(); ++index) {
    if (index != 0U)
      blocksJson += ',';
    blocksJson += blocks[index];
  }
  blocksJson += ']';
  return "{\"snapshot_tip\":" + snapshotTip + ",\"blocks\":" + blocksJson +
         ",\"next_block_id\":" +
         (nextBlockId.has_value() ? std::to_string(*nextBlockId)
                                  : std::string("null")) +
         "}";
}

std::string rpcEnvelope(const std::string &result) {
  return "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":" + result + "}";
}

palace::PalaceLezLocalCommittedHistoryExpectationV1
expectation(const std::uint8_t palace = 0x10U) {
  return {kProgramIdHex, kRootAccountIdHex,
          palace::PalaceLezCodec::bytes32Hex(bytes(palace))};
}

std::string storageCid(const std::uint8_t seed) {
  std::vector<std::uint8_t> bytes = {0x01U, 0x55U, 0x12U, 0x20U};
  for (std::uint8_t index = 0U; index < 32U; ++index) {
    bytes.push_back(static_cast<std::uint8_t>(seed + index));
  }
  static constexpr char kAlphabet[] = "abcdefghijklmnopqrstuvwxyz234567";
  std::string encoded = "b";
  std::uint32_t accumulator = 0U;
  unsigned int bitCount = 0U;
  for (const std::uint8_t byte : bytes) {
    accumulator = (accumulator << 8U) | byte;
    bitCount += 8U;
    while (bitCount >= 5U) {
      bitCount -= 5U;
      encoded.push_back(kAlphabet[(accumulator >> bitCount) & 0x1fU]);
      accumulator &= bitCount == 0U ? 0U : ((1U << bitCount) - 1U);
    }
  }
  if (bitCount != 0U) {
    encoded.push_back(kAlphabet[(accumulator << (5U - bitCount)) & 0x1fU]);
  }
  return encoded;
}

std::string png(const int width, const int height, const QRgb color) {
  QImage image(width, height, QImage::Format_RGBA8888);
  image.fill(color);
  QBuffer buffer;
  if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
    return {};
  return {buffer.data().constData(),
          static_cast<std::size_t>(buffer.data().size())};
}

bool publishGraph(palace::PalaceStorageMvpBundle &bundle) {
  std::uint8_t seed = 1U;
  while (!bundle.complete()) {
    const std::vector<std::string> stageable = bundle.stageableObjectIds();
    if (stageable.empty())
      return false;
    for (const std::string &objectId : stageable) {
      if (!bundle.assignPublicationCid(objectId, storageCid(seed++)))
        return false;
    }
  }
  return bundle.fetchedContentValid();
}

palace::PalaceLezLocalCommittedHistoryResultV1 rebuildHistory(
    const palace::PalaceLezInstructionV3 &initialize,
    const std::optional<palace::PalaceLezInstructionV3> &laterAction,
    const std::uint64_t tipBlockId, const char tipHash,
    const char previousTipHash) {
  palace::PalaceLezLocalCommittedHistorySession session;
  LOGOS_ASSERT_TRUE(session.start(expectation()).accepted);
  LOGOS_ASSERT_TRUE(session.takeNextRequest().has_value());

  const palace::PalaceLezTransactionPlanV3 initializePlan = plan(initialize);
  std::vector<std::string> blocks;
  blocks.push_back(block(0U, 'a', '0', {}));
  blocks.push_back(block(
      1U, 'b', 'a',
      {publicTransaction(initializePlan, repeated('1', 64U))}));
  if (laterAction.has_value()) {
    const palace::PalaceLezTransactionPlanV3 actionPlan = plan(*laterAction);
    blocks.push_back(block(
        2U, 'c', 'b',
        {publicTransaction(actionPlan, repeated('2', 64U))}));
  }
  LOGOS_ASSERT_EQ(tipBlockId, laterAction.has_value() ? 2U : 1U);
  const auto update = session.acceptPage(page(
      blockHeader(tipBlockId, tipHash, previousTipHash), blocks,
      std::nullopt));
  LOGOS_ASSERT_TRUE(update.accepted);
  LOGOS_ASSERT_TRUE(
      update.outcome == palace::PalaceLezLocalCommittedHistoryOutcome::Rebuilt);
  const auto rebuilt = session.rebuildResult();
  LOGOS_ASSERT_TRUE(rebuilt.has_value());
  return *rebuilt;
}

} // namespace

LOGOS_TEST(lez_local_committed_history_rebuilds_pinned_forward_pages) {
  const auto initializePlan = plan(initializeInstruction());
  const auto actionOnePlan = plan(actionOneInstruction());
  const auto actionTwoPlan = plan(actionTwoInstruction());
  const std::string snapshotTip = blockHeader(2U, 'c', 'b');

  palace::PalaceLezLocalCommittedHistorySession session;
  auto update = session.start(expectation());
  LOGOS_ASSERT_TRUE(update.accepted);
  LOGOS_ASSERT_EQ(update.reason,
                  std::string("awaiting-local-committed-history-page"));

  auto request = session.takeNextRequest();
  LOGOS_ASSERT_TRUE(request.has_value());
  LOGOS_ASSERT_EQ(request->startBlockId, 0U);
  LOGOS_ASSERT_TRUE(request->expectedTipJson.empty());
  update = session.acceptPage(
      page(snapshotTip,
           {
               block(0U, 'a', '0',
                     {publicTransaction(initializePlan, repeated('1', 64U))}),
               block(1U, 'b', 'a',
                     {publicTransaction(actionOnePlan, repeated('2', 64U))}),
           },
           2U));
  LOGOS_ASSERT_TRUE(update.accepted);
  LOGOS_ASSERT_TRUE(update.outcome ==
                    palace::PalaceLezLocalCommittedHistoryOutcome::Pending);

  request = session.takeNextRequest();
  LOGOS_ASSERT_TRUE(request.has_value());
  LOGOS_ASSERT_EQ(request->startBlockId, 2U);
  LOGOS_ASSERT_EQ(request->expectedTipJson,
                  std::string("{\"block_id\":2,\"block_hash\":\"") +
                      repeated('c', 64U) + "\",\"previous_block_hash\":\"" +
                      repeated('b', 64U) + "\"}");
  update = session.acceptPage(
      page(snapshotTip,
           {
               block(2U, 'c', 'b',
                     {publicTransaction(actionTwoPlan, repeated('3', 64U))}),
           },
           std::nullopt));
  LOGOS_ASSERT_TRUE(update.accepted);
  LOGOS_ASSERT_TRUE(update.outcome ==
                    palace::PalaceLezLocalCommittedHistoryOutcome::Rebuilt);

  const auto rebuilt = session.rebuildResult();
  LOGOS_ASSERT_TRUE(rebuilt.has_value());
  LOGOS_ASSERT_EQ(rebuilt->source,
                  std::string(palace::kPalaceLezLocalCommittedHistorySourceV1));
  LOGOS_ASSERT_EQ(rebuilt->actions.size(), 3U);
  LOGOS_ASSERT_EQ(rebuilt->actions[0].orderedActionId, 0U);
  LOGOS_ASSERT_EQ(rebuilt->actions[1].orderedActionId, 1U);
  LOGOS_ASSERT_EQ(rebuilt->actions[2].orderedActionId, 2U);
  LOGOS_ASSERT_EQ(rebuilt->latestLocalCommittedBlockId, 2U);
  LOGOS_ASSERT_EQ(rebuilt->latestLocalCommittedBlockHashHex,
                  repeated('c', 64U));
  LOGOS_ASSERT_EQ(session.pagesScanned(), 2U);
  LOGOS_ASSERT_EQ(session.blocksScanned(), 3U);
  LOGOS_ASSERT_TRUE(rebuilt->uniqueAccountIdsHex.size() >= 2U);
}

LOGOS_TEST(
    lez_local_committed_history_rebuilds_jsonrpc_wrapped_history_pages) {
  const auto initializePlan = plan(initializeInstruction());
  const std::string snapshotTip = blockHeader(0U, 'a', '0');

  palace::PalaceLezLocalCommittedHistorySession session;
  auto update = session.start(expectation());
  LOGOS_ASSERT_TRUE(update.accepted);
  LOGOS_ASSERT_TRUE(session.takeNextRequest().has_value());
  update = session.acceptPage(rpcEnvelope(page(
      snapshotTip,
      {
          block(0U, 'a', '0',
                {publicTransaction(initializePlan, repeated('1', 64U))}),
      },
      std::nullopt)));
  LOGOS_ASSERT_TRUE(update.accepted);
  LOGOS_ASSERT_TRUE(update.outcome ==
                    palace::PalaceLezLocalCommittedHistoryOutcome::Rebuilt);

  const auto rebuilt = session.rebuildResult();
  LOGOS_ASSERT_TRUE(rebuilt.has_value());
  LOGOS_ASSERT_EQ(rebuilt->actions.size(), 1U);
  LOGOS_ASSERT_EQ(rebuilt->latestLocalCommittedBlockId, 0U);
}

LOGOS_TEST(
    lez_local_committed_history_rebuilds_requested_palace_on_reused_chain) {
  const auto priorInitializePlan = plan(initializeInstruction(0x10U));
  const auto priorActionPlan = plan(actionOneInstruction());
  const auto targetInitializePlan = plan(initializeInstruction(0x20U));
  const auto targetActionPlan = plan(actionOneInstruction());
  const std::string snapshotTip = blockHeader(3U, 'd', 'c');

  palace::PalaceLezLocalCommittedHistorySession session;
  auto update = session.start(expectation(0x20U));
  LOGOS_ASSERT_TRUE(update.accepted);
  LOGOS_ASSERT_TRUE(session.takeNextRequest().has_value());
  update = session.acceptPage(
      page(snapshotTip,
           {
               block(0U, 'a', '0',
                     {publicTransaction(priorInitializePlan, repeated('1', 64U))}),
               block(1U, 'b', 'a',
                     {publicTransaction(priorActionPlan, repeated('2', 64U))}),
               block(2U, 'c', 'b',
                     {publicTransaction(targetInitializePlan, repeated('3', 64U))}),
               block(3U, 'd', 'c',
                     {publicTransaction(targetActionPlan, repeated('4', 64U))}),
           },
           std::nullopt));
  LOGOS_ASSERT_TRUE(update.accepted);
  LOGOS_ASSERT_TRUE(update.outcome ==
                    palace::PalaceLezLocalCommittedHistoryOutcome::Rebuilt);

  const auto rebuilt = session.rebuildResult();
  LOGOS_ASSERT_TRUE(rebuilt.has_value());
  LOGOS_ASSERT_EQ(rebuilt->actions.size(), 2U);
  const auto *initialize = std::get_if<palace::PalaceLezInitializeV3>(
      &rebuilt->actions.front().instruction.payload);
  LOGOS_ASSERT_TRUE(initialize != nullptr);
  LOGOS_ASSERT_EQ(palace::PalaceLezCodec::bytes32Hex(initialize->palaceId),
                  palace::PalaceLezCodec::bytes32Hex(bytes(0x20U)));
  LOGOS_ASSERT_EQ(rebuilt->actions.front().orderedActionId, 0U);
  LOGOS_ASSERT_EQ(rebuilt->actions.back().orderedActionId, 1U);
  LOGOS_ASSERT_EQ(rebuilt->latestLocalCommittedBlockId, 3U);
}

LOGOS_TEST(
    lez_local_committed_restart_restores_authored_room_graph_at_latest_checkpoint) {
  const std::string atriumBackground = png(11, 7, qRgb(0x11, 0x33, 0x55));
  const std::string loungeBackground = png(13, 9, qRgb(0x66, 0x44, 0x22));
  LOGOS_ASSERT_FALSE(atriumBackground.empty());
  LOGOS_ASSERT_FALSE(loungeBackground.empty());

  palace::PalaceStorageMvpBundle authored;
  LOGOS_ASSERT_TRUE(authored.initialize(atriumBackground, loungeBackground));
  LOGOS_ASSERT_TRUE(publishGraph(authored));
  const auto *palaceManifest = authored.artifact("palace-1");
  const auto *atriumManifest = authored.artifact("room-atrium");
  const auto *loungeManifest = authored.artifact("room-lounge");
  const auto *doorScript = authored.artifact("script-door");
  LOGOS_ASSERT_TRUE(palaceManifest != nullptr);
  LOGOS_ASSERT_TRUE(atriumManifest != nullptr);
  LOGOS_ASSERT_TRUE(loungeManifest != nullptr);
  LOGOS_ASSERT_TRUE(doorScript != nullptr);

  const palace::PalaceLezInstructionV3 initialize = initializeInstruction(
      palaceManifest->cid, atriumManifest->cid, loungeManifest->cid,
      doorScript->cid);
  const palace::PalaceLezLocalCommittedHistoryResultV1 created =
      rebuildHistory(initialize, std::nullopt, 1U, 'b', 'a');
  LOGOS_ASSERT_EQ(created.actions.size(), 1U);

  QTemporaryDir temporary;
  LOGOS_ASSERT_TRUE(temporary.isValid());
  palace::PalaceStorageMvpCatalogStore catalog(
      temporary.path().toStdString());
  palace::PalaceStorageMvpCatalogBindingV1 createdBinding;
  createdBinding.networkId = "logos-lez-local-development-v1";
  createdBinding.programIdHex = kProgramIdHex;
  createdBinding.rootAccountIdHex = kRootAccountIdHex;
  createdBinding.finalizedCheckpoint = created.latestLocalCommittedBlockId;
  createdBinding.finalizedHash = created.latestLocalCommittedBlockHashHex;
  createdBinding.rootManifestCid = palaceManifest->cid;
  palace::PalaceStorageMvpCatalogRecordV1 record;
  record.binding = createdBinding;
  record.canonicalCatalog = authored.canonicalCatalog();
  record.catalogChecksumHex =
      palace::crypto::sha256Hex(record.canonicalCatalog);
  LOGOS_ASSERT_TRUE(
      catalog.save(record) == palace::PalaceStorageMvpCatalogStoreStatus::Saved);

  // A door action changes the local committed checkpoint but not the immutable
  // authored graph. The current verified graph must be re-sealed at that
  // checkpoint before a later process rebuilds authority from history.
  const palace::PalaceLezLocalCommittedHistoryResultV1 reopened =
      rebuildHistory(initialize, doorOpenInstruction(), 2U, 'c', 'b');
  LOGOS_ASSERT_EQ(reopened.actions.size(), 2U);
  record.binding.finalizedCheckpoint = reopened.latestLocalCommittedBlockId;
  record.binding.finalizedHash = reopened.latestLocalCommittedBlockHashHex;
  LOGOS_ASSERT_TRUE(
      catalog.save(record) == palace::PalaceStorageMvpCatalogStoreStatus::Saved);

  palace::PalaceStorageMvpCatalogRecordV1 stale;
  LOGOS_ASSERT_TRUE(
      catalog.load(createdBinding, stale)
      == palace::PalaceStorageMvpCatalogStoreStatus::BindingMismatch);
  palace::PalaceStorageMvpCatalogRecordV1 restoredRecord;
  LOGOS_ASSERT_TRUE(
      catalog.load(record.binding, restoredRecord)
      == palace::PalaceStorageMvpCatalogStoreStatus::Loaded);

  palace::PalaceStorageMvpBundle restarted;
  LOGOS_ASSERT_TRUE(
      restarted.restoreCanonicalCatalog(restoredRecord.canonicalCatalog));
  LOGOS_ASSERT_TRUE(restarted.complete());
  LOGOS_ASSERT_FALSE(restarted.fetchedContentValid());
  for (const palace::PalaceStorageMvpArtifactV1 &artifact :
       authored.artifacts()) {
    LOGOS_ASSERT_TRUE(
        restarted.acceptFetchedBytes(artifact.objectId, artifact.bytes));
  }
  LOGOS_ASSERT_TRUE(restarted.fetchedContentValid());

  const auto *restartedAtrium = restarted.artifact("background-atrium");
  const auto *restartedLounge = restarted.artifact("background-lounge");
  const auto *restartedAtriumManifest = restarted.artifact("room-atrium");
  const auto *restartedLoungeManifest = restarted.artifact("room-lounge");
  const auto *restartedScript = restarted.artifact("script-door");
  LOGOS_ASSERT_TRUE(restartedAtrium != nullptr);
  LOGOS_ASSERT_TRUE(restartedLounge != nullptr);
  LOGOS_ASSERT_TRUE(restartedAtriumManifest != nullptr);
  LOGOS_ASSERT_TRUE(restartedLoungeManifest != nullptr);
  LOGOS_ASSERT_TRUE(restartedScript != nullptr);
  LOGOS_ASSERT_EQ(restartedAtrium->bytes, atriumBackground);
  LOGOS_ASSERT_EQ(restartedLounge->bytes, loungeBackground);
  const auto *initialized = std::get_if<palace::PalaceLezInitializeV3>(
      &initialize.payload);
  LOGOS_ASSERT_TRUE(initialized != nullptr);
  LOGOS_ASSERT_EQ(restartedAtriumManifest->cid,
                  initialized->entryRoom.manifestCid);
  LOGOS_ASSERT_EQ(restartedLoungeManifest->cid,
                  initialized->secondaryRoom.manifestCid);
  LOGOS_ASSERT_EQ(restartedScript->cid, doorScript->cid);
}

LOGOS_TEST(lez_local_committed_history_rejects_snapshot_and_chain_drift) {
  const auto initializePlan = plan(initializeInstruction());
  const auto actionOnePlan = plan(actionOneInstruction());

  palace::PalaceLezLocalCommittedHistorySession changedTip;
  auto update = changedTip.start(expectation());
  LOGOS_ASSERT_TRUE(update.accepted);
  LOGOS_ASSERT_TRUE(changedTip.takeNextRequest().has_value());
  update = changedTip.acceptPage(
      page(blockHeader(1U, 'b', 'a'),
           {
               block(0U, 'a', '0',
                     {publicTransaction(initializePlan, repeated('1', 64U))}),
           },
           1U));
  LOGOS_ASSERT_TRUE(update.accepted);
  const auto request = changedTip.takeNextRequest();
  LOGOS_ASSERT_TRUE(request.has_value());
  LOGOS_ASSERT_EQ(request->startBlockId, 1U);
  update = changedTip.acceptPage(
      page(blockHeader(2U, 'c', 'b'),
           {
               block(1U, 'b', 'a',
                     {publicTransaction(actionOnePlan, repeated('2', 64U))}),
           },
           std::nullopt));
  LOGOS_ASSERT_TRUE(update.outcome ==
                    palace::PalaceLezLocalCommittedHistoryOutcome::Rejected);
  LOGOS_ASSERT_EQ(update.reason,
                  std::string("local-committed-snapshot-mismatch"));

  palace::PalaceLezLocalCommittedHistorySession brokenChain;
  update = brokenChain.start(expectation());
  LOGOS_ASSERT_TRUE(update.accepted);
  LOGOS_ASSERT_TRUE(brokenChain.takeNextRequest().has_value());
  update = brokenChain.acceptPage(
      page(blockHeader(1U, 'b', 'a'),
           {
               block(0U, 'a', '0',
                     {publicTransaction(initializePlan, repeated('1', 64U))}),
           },
           1U));
  LOGOS_ASSERT_TRUE(update.accepted);
  LOGOS_ASSERT_TRUE(brokenChain.takeNextRequest().has_value());
  update = brokenChain.acceptPage(
      page(blockHeader(1U, 'b', 'a'),
           {
               block(1U, 'b', 'f',
                     {publicTransaction(actionOnePlan, repeated('2', 64U))}),
           },
           std::nullopt));
  LOGOS_ASSERT_TRUE(update.outcome ==
                    palace::PalaceLezLocalCommittedHistoryOutcome::Rejected);
  LOGOS_ASSERT_EQ(update.reason,
                  std::string("local-committed-page-chain-mismatch"));
}

LOGOS_TEST(lez_local_committed_history_rejects_incomplete_palace_actions) {
  palace::PalaceLezLocalCommittedHistorySession session;
  auto update = session.start(expectation());
  LOGOS_ASSERT_TRUE(update.accepted);
  LOGOS_ASSERT_TRUE(session.takeNextRequest().has_value());
  update = session.acceptPage(
      page(blockHeader(0U, 'a', '0'),
           {
               block(0U, 'a', '0',
                     {publicTransaction(plan(actionOneInstruction()),
                                        repeated('1', 64U))}),
           },
           std::nullopt));
  LOGOS_ASSERT_TRUE(update.outcome ==
                    palace::PalaceLezLocalCommittedHistoryOutcome::Rejected);
  LOGOS_ASSERT_EQ(update.reason,
                  std::string("local-committed-initialize-not-found"));
  LOGOS_ASSERT_FALSE(session.rebuildResult().has_value());
}

LOGOS_TEST(lez_local_committed_history_rejects_mismatched_palace_transaction) {
  const auto initializePlan = plan(initializeInstruction());
  std::string transaction =
      publicTransaction(initializePlan, repeated('1', 64U));
  const std::string instructionPrefix = "\"instruction_data\":";
  const std::size_t instructionPrefixPosition =
      transaction.find(instructionPrefix);
  LOGOS_ASSERT_TRUE(instructionPrefixPosition != std::string::npos);
  const std::size_t instructionStart =
      instructionPrefixPosition + instructionPrefix.size();
  const std::size_t instructionEnd = transaction.find(']', instructionStart);
  LOGOS_ASSERT_TRUE(instructionEnd != std::string::npos);
  transaction.replace(instructionStart, instructionEnd - instructionStart + 1U,
                      "[99]");

  palace::PalaceLezLocalCommittedHistorySession session;
  auto update = session.start(expectation());
  LOGOS_ASSERT_TRUE(update.accepted);
  LOGOS_ASSERT_TRUE(session.takeNextRequest().has_value());
  update = session.acceptPage(page(blockHeader(0U, 'a', '0'),
                                   {
                                       block(0U, 'a', '0', {transaction}),
                                   },
                                   std::nullopt));
  LOGOS_ASSERT_TRUE(update.outcome ==
                    palace::PalaceLezLocalCommittedHistoryOutcome::Rejected);
  LOGOS_ASSERT_EQ(update.reason,
                  std::string("local-committed-transaction-mismatch"));
}
