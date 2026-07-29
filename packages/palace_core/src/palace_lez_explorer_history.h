#pragma once

#include "palace_lez.h"
#include "palace_lez_explorer_finality.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace palace {

enum class PalaceLezExplorerHistoryOutcome : std::uint8_t {
    Pending = 0U,
    Rebuilt = 1U,
    Degraded = 2U,
    Rejected = 3U,
};

struct PalaceLezExplorerHistoryExpectationV1 {
    std::string programIdHex;
    std::string rootAccountIdHex;
};

struct PalaceLezExplorerHistoryUpdateV1 {
    bool accepted = false;
    bool changed = false;
    PalaceLezExplorerHistoryOutcome outcome =
        PalaceLezExplorerHistoryOutcome::Pending;
    std::string reason;
};

struct PalaceLezExplorerHistoryResultV1 {
    std::uint32_t resultVersion = 1U;
    std::vector<PalaceLezFinalizedActionV3> actions;
    // Stable first-seen order across chronological actions.
    std::vector<std::string> uniqueAccountIdsHex;
    std::uint64_t latestFinalizedBlockId = 0U;
    std::string latestFinalizedBlockHashHex;
};

// Transport-neutral restart seam. Scans strict finalized explorer block pages
// backward to Initialize(0), then emits only a complete verified history.
class PalaceLezExplorerHistorySession {
public:
    PalaceLezExplorerHistoryUpdateV1 start(
        const PalaceLezExplorerNetworkFingerprintV1& fingerprint,
        const PalaceLezExplorerFinalityLimitsV1& limits,
        const PalaceLezExplorerHistoryExpectationV1& expectation);

    std::optional<PalaceLezExplorerCommandV1> takeNextCommand();
    PalaceLezExplorerHistoryUpdateV1 acceptResponse(
        const PalaceLezExplorerHttpResponseV1& response);

    PalaceLezExplorerHistoryOutcome outcome() const;
    const std::string& reason() const;
    bool waitingForResponse() const;
    bool scanExhausted() const;
    std::size_t pagesScanned() const;
    std::size_t blocksScanned() const;
    std::optional<PalaceLezExplorerHistoryResultV1> rebuildResult() const;

private:
    PalaceLezExplorerHistoryUpdateV1 setTerminal(
        PalaceLezExplorerHistoryOutcome outcome,
        std::string reason);
    PalaceLezExplorerHistoryUpdateV1 setPending(
        std::string reason,
        bool scanExhausted);
    void queueBlocksCommand(std::optional<std::uint64_t> before);
    PalaceLezExplorerHistoryUpdateV1 acceptBlocksBody(
        const std::string& body);
    PalaceLezExplorerHistoryUpdateV1 completeRebuild();

    PalaceLezExplorerNetworkFingerprintV1 fingerprint_;
    PalaceLezExplorerFinalityLimitsV1 limits_;
    PalaceLezExplorerHistoryExpectationV1 expectation_;
    std::string programIdBase58_;
    std::string rootAccountIdBase58_;
    PalaceLezExplorerHistoryOutcome outcome_ =
        PalaceLezExplorerHistoryOutcome::Pending;
    std::string reason_ = "not-started";
    bool started_ = false;
    bool finished_ = false;
    bool scanExhausted_ = false;
    std::uint64_t nextCommandSequence_ = 1U;
    std::optional<PalaceLezExplorerCommandV1> readyCommand_;
    std::optional<PalaceLezExplorerCommandV1> outstandingCommand_;
    std::size_t pagesScanned_ = 0U;
    std::size_t blocksScanned_ = 0U;
    std::uint64_t oldestBlockId_ = 0U;
    std::uint64_t oldestBlockTimestamp_ = 0U;
    std::string oldestBlockPreviousHashHex_;
    std::set<std::uint64_t> seenBlockIds_;
    std::set<std::string> seenBlockHashes_;
    std::set<std::string> seenTransactionHashes_;
    std::vector<PalaceLezFinalizedActionV3> reverseActions_;
    std::uint64_t latestFinalizedBlockId_ = 0U;
    std::string latestFinalizedBlockHashHex_;
    std::optional<PalaceLezExplorerHistoryResultV1> result_;
};

} // namespace palace
