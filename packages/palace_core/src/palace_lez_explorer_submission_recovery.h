#pragma once

#include "palace_lez_explorer_finality.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace palace {

enum class PalaceLezSubmissionRecoveryOutcome : std::uint8_t {
    Pending = 0U,
    Found = 1U,
    NotFound = 2U,
    Degraded = 3U,
    Rejected = 4U,
};

struct PalaceLezSubmissionRecoveryExpectationV1 {
    std::string programIdHex;
    std::vector<std::string> accountIdsHex;
    std::vector<std::uint32_t> instructionWords;
    std::size_t signatureCount = 0U;
    // Finalized height sampled and durably stored before submission. Only
    // transactions in later blocks can satisfy this recovery.
    std::uint64_t minimumFinalizedBlockExclusive = 0U;
};

struct PalaceLezSubmissionRecoveryResultV1 {
    std::string transactionHash;
    std::uint64_t finalizedBlockId = 0U;
    std::string finalizedBlockHashHex;
};

struct PalaceLezSubmissionRecoveryUpdateV1 {
    bool accepted = false;
    bool changed = false;
    PalaceLezSubmissionRecoveryOutcome outcome =
        PalaceLezSubmissionRecoveryOutcome::Pending;
    std::string reason;
};

// Transport-neutral write-ahead recovery scanner. It verifies every finalized
// block from the current explorer tip down through the pre-submit checkpoint,
// and returns a transaction only when the exact tuple has one unique match.
class PalaceLezExplorerSubmissionRecoverySession {
public:
    PalaceLezSubmissionRecoveryUpdateV1 start(
        const PalaceLezExplorerNetworkFingerprintV1& fingerprint,
        const PalaceLezExplorerFinalityLimitsV1& limits,
        const PalaceLezSubmissionRecoveryExpectationV1& expectation);

    std::optional<PalaceLezExplorerCommandV1> takeNextCommand();
    PalaceLezSubmissionRecoveryUpdateV1 acceptResponse(
        const PalaceLezExplorerHttpResponseV1& response);

    PalaceLezSubmissionRecoveryOutcome outcome() const;
    const std::string& reason() const;
    bool waitingForResponse() const;
    bool scanExhausted() const;
    std::optional<PalaceLezSubmissionRecoveryResultV1> result() const;

private:
    PalaceLezSubmissionRecoveryUpdateV1 setTerminal(
        PalaceLezSubmissionRecoveryOutcome outcome,
        std::string reason);
    PalaceLezSubmissionRecoveryUpdateV1 setPending(
        std::string reason,
        bool scanExhausted);
    PalaceLezSubmissionRecoveryUpdateV1 completeScan();
    PalaceLezSubmissionRecoveryUpdateV1 acceptBlocksBody(
        const std::string& body);
    void queueBlocksCommand(std::optional<std::uint64_t> before);

    PalaceLezExplorerNetworkFingerprintV1 fingerprint_;
    PalaceLezExplorerFinalityLimitsV1 limits_;
    PalaceLezSubmissionRecoveryExpectationV1 expectation_;
    std::string programIdBase58_;
    std::vector<std::string> accountIdsBase58_;
    PalaceLezSubmissionRecoveryOutcome outcome_ =
        PalaceLezSubmissionRecoveryOutcome::Pending;
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
    std::optional<PalaceLezSubmissionRecoveryResultV1> match_;
};

} // namespace palace
