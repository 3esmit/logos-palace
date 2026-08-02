#pragma once

#include "palace_lez.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace palace {

inline constexpr char kPalaceLezLocalCommittedHistorySourceV1[] =
    "local-committed";

enum class PalaceLezLocalCommittedHistoryOutcome : std::uint8_t {
  Pending = 0U,
  Rebuilt = 1U,
  Degraded = 2U,
  Rejected = 3U,
};

struct PalaceLezLocalCommittedHistoryExpectationV1 {
  std::string programIdHex;
  std::string rootAccountIdHex;
  std::string palaceIdHex;
};

// Transport-neutral cursor for the configured LEZ wallet module. The first
// request uses startBlockId == 0 and no expected snapshot. Each later request
// repeats the initial snapshot so a replay cannot combine chain views.
struct PalaceLezLocalCommittedHistoryRequestV1 {
  std::uint64_t startBlockId = 0U;
  std::string expectedTipJson;
};

struct PalaceLezLocalCommittedHistoryUpdateV1 {
  bool accepted = false;
  bool changed = false;
  PalaceLezLocalCommittedHistoryOutcome outcome =
      PalaceLezLocalCommittedHistoryOutcome::Pending;
  std::string reason;
};

struct PalaceLezLocalCommittedActionV1 {
  std::string transactionHash;
  std::uint64_t orderedActionId = 0U;
  PalaceLezInstructionV3 instruction;
  std::vector<std::string> accountIdsHex;
};

struct PalaceLezLocalCommittedHistoryResultV1 {
  std::uint32_t resultVersion = 1U;
  std::string source = kPalaceLezLocalCommittedHistorySourceV1;
  std::vector<PalaceLezLocalCommittedActionV1> actions;
  // Stable first-seen order across chronological actions.
  std::vector<std::string> uniqueAccountIdsHex;
  std::uint64_t latestLocalCommittedBlockId = 0U;
  std::string latestLocalCommittedBlockHashHex;
};

// Replays only bounded pages returned by the configured LEZ wallet. It has no
// endpoint, transport, or lifecycle controls. A complete result contains the
// chronological Palace action stream from Initialize(0) through one pinned
// local chain snapshot.
class PalaceLezLocalCommittedHistorySession {
public:
  PalaceLezLocalCommittedHistoryUpdateV1
  start(const PalaceLezLocalCommittedHistoryExpectationV1 &expectation);

  std::optional<PalaceLezLocalCommittedHistoryRequestV1> takeNextRequest();
  PalaceLezLocalCommittedHistoryUpdateV1
  acceptPage(const std::string &pageJson);

  PalaceLezLocalCommittedHistoryOutcome outcome() const;
  const std::string &reason() const;
  bool waitingForPage() const;
  std::size_t pagesScanned() const;
  std::size_t blocksScanned() const;
  std::optional<PalaceLezLocalCommittedHistoryResultV1> rebuildResult() const;

private:
  PalaceLezLocalCommittedHistoryUpdateV1
  setTerminal(PalaceLezLocalCommittedHistoryOutcome outcome,
              std::string reason);
  PalaceLezLocalCommittedHistoryUpdateV1 setPending(std::string reason);
  PalaceLezLocalCommittedHistoryUpdateV1 completeRebuild();

  PalaceLezLocalCommittedHistoryExpectationV1 expectation_;
  PalaceLezLocalCommittedHistoryOutcome outcome_ =
      PalaceLezLocalCommittedHistoryOutcome::Pending;
  std::string reason_ = "not-started";
  bool started_ = false;
  bool finished_ = false;
  std::optional<PalaceLezLocalCommittedHistoryRequestV1> readyRequest_;
  std::optional<PalaceLezLocalCommittedHistoryRequestV1> outstandingRequest_;
  std::size_t pagesScanned_ = 0U;
  std::size_t blocksScanned_ = 0U;
  std::uint64_t lastBlockId_ = 0U;
  std::string lastBlockHashHex_;
  std::set<std::uint64_t> seenBlockIds_;
  std::set<std::string> seenBlockHashes_;
  std::set<std::string> seenTransactionHashes_;
  std::vector<PalaceLezLocalCommittedActionV1> actions_;
  bool targetInitializeFound_ = false;
  bool collectingTargetStream_ = false;
  std::string snapshotTipJson_;
  std::uint64_t snapshotTipBlockId_ = 0U;
  std::string snapshotTipBlockHashHex_;
  std::string snapshotTipPreviousBlockHashHex_;
  std::optional<PalaceLezLocalCommittedHistoryResultV1> result_;
};

} // namespace palace
