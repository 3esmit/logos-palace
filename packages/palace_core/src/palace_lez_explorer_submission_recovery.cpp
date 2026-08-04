#include "palace_lez_explorer_submission_recovery.h"

#include "palace_lez.h"
#include "palace_lez_explorer_protocol.h"

#include <algorithm>
#include <utility>

namespace palace {
namespace {

bool toBase58(const std::string& value, std::string& output)
{
    PalaceLezBytes32 decoded{};
    if (!PalaceLezCodec::parseBytes32Hex(value, decoded))
        return false;
    output = PalaceLezCodec::accountIdBase58(decoded);
    return !output.empty();
}

bool exactTransaction(
    const lez_explorer_protocol::TransactionV1& transaction,
    const PalaceLezSubmissionRecoveryExpectationV1& expectation,
    const std::string& programIdBase58,
    const std::vector<std::string>& accountIdsBase58)
{
    return transaction.kind
            == lez_explorer_protocol::TransactionKind::Public
        && transaction.programIdBase58 == programIdBase58
        && transaction.accountIdsBase58 == accountIdsBase58
        && transaction.instructionWords == expectation.instructionWords
        && transaction.nonceCount == accountIdsBase58.size()
        && transaction.signatureCount == expectation.signatureCount;
}

} // namespace

PalaceLezSubmissionRecoveryUpdateV1
PalaceLezExplorerSubmissionRecoverySession::start(
    const PalaceLezExplorerNetworkFingerprintV1& fingerprint,
    const PalaceLezExplorerFinalityLimitsV1& limits,
    const PalaceLezSubmissionRecoveryExpectationV1& expectation)
{
    fingerprint_ = {};
    limits_ = {};
    expectation_ = {};
    programIdBase58_.clear();
    accountIdsBase58_.clear();
    outcome_ = PalaceLezSubmissionRecoveryOutcome::Pending;
    reason_ = "not-started";
    started_ = false;
    finished_ = false;
    scanExhausted_ = false;
    nextCommandSequence_ = 1U;
    readyCommand_.reset();
    outstandingCommand_.reset();
    pagesScanned_ = 0U;
    blocksScanned_ = 0U;
    oldestBlockId_ = 0U;
    oldestBlockTimestamp_ = 0U;
    oldestBlockPreviousHashHex_.clear();
    seenBlockIds_.clear();
    seenBlockHashes_.clear();
    seenTransactionHashes_.clear();
    match_.reset();

    if (!lez_explorer_protocol::networkFingerprintAccepted(fingerprint))
        return setTerminal(
            PalaceLezSubmissionRecoveryOutcome::Rejected,
            "invalid-explorer-network-fingerprint");
    if (!lez_explorer_protocol::limitsAccepted(limits))
        return setTerminal(
            PalaceLezSubmissionRecoveryOutcome::Rejected,
            "invalid-explorer-limits");
    if (expectation.accountIdsHex.empty()
        || expectation.accountIdsHex.size() > limits.maxAccounts
        || expectation.instructionWords.empty()
        || expectation.instructionWords.size()
            > limits.maxInstructionWords
        || expectation.signatureCount
            > expectation.accountIdsHex.size()
        || !toBase58(
            expectation.programIdHex,
            programIdBase58_)) {
        return setTerminal(
            PalaceLezSubmissionRecoveryOutcome::Rejected,
            "invalid-submission-recovery-expectation");
    }
    accountIdsBase58_.reserve(expectation.accountIdsHex.size());
    for (const std::string& accountId : expectation.accountIdsHex) {
        std::string encoded;
        if (!toBase58(accountId, encoded)) {
            return setTerminal(
                PalaceLezSubmissionRecoveryOutcome::Rejected,
                "invalid-submission-recovery-expectation");
        }
        accountIdsBase58_.push_back(std::move(encoded));
    }

    fingerprint_ = fingerprint;
    limits_ = limits;
    expectation_ = expectation;
    started_ = true;
    reason_ = "awaiting-finalized-submission-history";
    queueBlocksCommand(std::nullopt);
    return {true, true, outcome_, reason_};
}

std::optional<PalaceLezExplorerCommandV1>
PalaceLezExplorerSubmissionRecoverySession::takeNextCommand()
{
    if (!started_ || finished_ || outstandingCommand_.has_value()
        || !readyCommand_.has_value()) {
        return std::nullopt;
    }
    outstandingCommand_ = std::move(readyCommand_);
    readyCommand_.reset();
    return outstandingCommand_;
}

PalaceLezSubmissionRecoveryUpdateV1
PalaceLezExplorerSubmissionRecoverySession::acceptResponse(
    const PalaceLezExplorerHttpResponseV1& response)
{
    if (!started_)
        return {false, false, outcome_, "recovery-session-not-started"};
    if (finished_)
        return {false, false, outcome_, "recovery-session-finished"};
    if (!outstandingCommand_.has_value()
        || response.commandSequence != outstandingCommand_->sequence) {
        return setTerminal(
            PalaceLezSubmissionRecoveryOutcome::Rejected,
            "explorer-command-correlation-mismatch");
    }

    const PalaceLezExplorerCommandV1 command = *outstandingCommand_;
    outstandingCommand_.reset();
    std::string metadataReason;
    if (!lez_explorer_protocol::responseMetadataAccepted(
            response, command, fingerprint_, metadataReason)) {
        return setTerminal(
            PalaceLezSubmissionRecoveryOutcome::Degraded,
            std::move(metadataReason));
    }
    if (command.kind != PalaceLezExplorerCommandKind::Blocks)
        return setTerminal(
            PalaceLezSubmissionRecoveryOutcome::Rejected,
            "explorer-command-phase-mismatch");
    return acceptBlocksBody(response.body);
}

PalaceLezSubmissionRecoveryOutcome
PalaceLezExplorerSubmissionRecoverySession::outcome() const
{
    return outcome_;
}

const std::string&
PalaceLezExplorerSubmissionRecoverySession::reason() const
{
    return reason_;
}

bool PalaceLezExplorerSubmissionRecoverySession::waitingForResponse() const
{
    return outstandingCommand_.has_value();
}

bool PalaceLezExplorerSubmissionRecoverySession::scanExhausted() const
{
    return scanExhausted_;
}

std::optional<PalaceLezSubmissionRecoveryResultV1>
PalaceLezExplorerSubmissionRecoverySession::result() const
{
    return outcome_ == PalaceLezSubmissionRecoveryOutcome::Found
        ? match_ : std::nullopt;
}

PalaceLezSubmissionRecoveryUpdateV1
PalaceLezExplorerSubmissionRecoverySession::setTerminal(
    const PalaceLezSubmissionRecoveryOutcome outcome,
    std::string reason)
{
    const bool changed = outcome_ != outcome || reason_ != reason;
    outcome_ = outcome;
    reason_ = std::move(reason);
    finished_ = true;
    readyCommand_.reset();
    outstandingCommand_.reset();
    if (outcome != PalaceLezSubmissionRecoveryOutcome::Found)
        match_.reset();
    return {
        outcome == PalaceLezSubmissionRecoveryOutcome::Found
            || outcome == PalaceLezSubmissionRecoveryOutcome::NotFound,
        changed,
        outcome_,
        reason_,
    };
}

PalaceLezSubmissionRecoveryUpdateV1
PalaceLezExplorerSubmissionRecoverySession::setPending(
    std::string reason,
    const bool scanExhausted)
{
    const bool changed =
        outcome_ != PalaceLezSubmissionRecoveryOutcome::Pending
        || reason_ != reason || scanExhausted_ != scanExhausted;
    outcome_ = PalaceLezSubmissionRecoveryOutcome::Pending;
    reason_ = std::move(reason);
    scanExhausted_ = scanExhausted;
    if (scanExhausted) {
        finished_ = true;
        readyCommand_.reset();
        outstandingCommand_.reset();
    }
    return {true, changed, outcome_, reason_};
}

PalaceLezSubmissionRecoveryUpdateV1
PalaceLezExplorerSubmissionRecoverySession::completeScan()
{
    return match_.has_value()
        ? setTerminal(
              PalaceLezSubmissionRecoveryOutcome::Found,
              "unique-finalized-submission-found")
        : setTerminal(
              PalaceLezSubmissionRecoveryOutcome::NotFound,
              "finalized-submission-not-found");
}

void PalaceLezExplorerSubmissionRecoverySession::queueBlocksCommand(
    const std::optional<std::uint64_t> before)
{
    readyCommand_ = lez_explorer_protocol::makeBlocksCommand(
        fingerprint_,
        limits_,
        nextCommandSequence_++,
        blocksScanned_,
        before);
}

PalaceLezSubmissionRecoveryUpdateV1
PalaceLezExplorerSubmissionRecoverySession::acceptBlocksBody(
    const std::string& body)
{
    lez_explorer_protocol::BlockPageParseResultV1 parsed =
        lez_explorer_protocol::parseBlockPage(body, limits_);
    if (!parsed.accepted) {
        return setTerminal(
            PalaceLezSubmissionRecoveryOutcome::Degraded,
            std::move(parsed.reason));
    }
    ++pagesScanned_;
    if (parsed.blocks.empty())
        return completeScan();
    if (parsed.blocks.size() > limits_.maxBlocks - blocksScanned_) {
        return setTerminal(
            PalaceLezSubmissionRecoveryOutcome::Degraded,
            "block-scan-limit-exceeded");
    }

    bool reachedCheckpoint = false;
    for (std::size_t index = 0U; index < parsed.blocks.size(); ++index) {
        const lez_explorer_protocol::BlockV1& block =
            parsed.blocks[index];
        if (block.bedrockStatus != "Finalized") {
            return setTerminal(
                PalaceLezSubmissionRecoveryOutcome::Rejected,
                "recovery-block-not-finalized");
        }
        if (index != 0U) {
            const lez_explorer_protocol::BlockV1& newer =
                parsed.blocks[index - 1U];
            if (newer.id != block.id + 1U
                || newer.previousHashHex != block.hashHex
                || newer.timestamp < block.timestamp) {
                return setTerminal(
                    PalaceLezSubmissionRecoveryOutcome::Rejected,
                    "block-page-chain-order-mismatch");
            }
        } else if (oldestBlockId_ != 0U) {
            if (oldestBlockId_ != block.id + 1U
                || oldestBlockPreviousHashHex_ != block.hashHex
                || oldestBlockTimestamp_ < block.timestamp) {
                return setTerminal(
                    PalaceLezSubmissionRecoveryOutcome::Rejected,
                    "block-page-cursor-replay-or-overlap");
            }
        }
        if (!seenBlockIds_.insert(block.id).second
            || !seenBlockHashes_.insert(block.hashHex).second) {
            return setTerminal(
                PalaceLezSubmissionRecoveryOutcome::Rejected,
                "duplicate-or-replayed-block");
        }
        for (const lez_explorer_protocol::TransactionV1& transaction :
             block.transactions) {
            if (!seenTransactionHashes_.insert(
                    transaction.hashHex).second) {
                return setTerminal(
                    PalaceLezSubmissionRecoveryOutcome::Rejected,
                    "duplicate-transaction-hash");
            }
            if (block.id
                    <= expectation_.minimumFinalizedBlockExclusive
                || !exactTransaction(
                    transaction,
                    expectation_,
                    programIdBase58_,
                    accountIdsBase58_)) {
                continue;
            }
            if (match_.has_value()) {
                return setTerminal(
                    PalaceLezSubmissionRecoveryOutcome::Rejected,
                    "multiple-finalized-submission-matches");
            }
            match_ = PalaceLezSubmissionRecoveryResultV1{
                transaction.hashHex,
                block.id,
                block.hashHex,
            };
        }
        if (block.id
            <= expectation_.minimumFinalizedBlockExclusive) {
            reachedCheckpoint = true;
        }
    }

    blocksScanned_ += parsed.blocks.size();
    const lez_explorer_protocol::BlockV1& oldest =
        parsed.blocks.back();
    oldestBlockId_ = oldest.id;
    oldestBlockTimestamp_ = oldest.timestamp;
    oldestBlockPreviousHashHex_ = oldest.previousHashHex;

    if (reachedCheckpoint)
        return completeScan();
    if (blocksScanned_ >= limits_.maxBlocks
        || pagesScanned_ >= limits_.maxPages) {
        return setPending("submission-recovery-window-exhausted", true);
    }
    queueBlocksCommand(oldestBlockId_);
    return setPending(
        "awaiting-older-finalized-submission-history",
        false);
}

} // namespace palace
