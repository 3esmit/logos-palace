#include "palace_lez_explorer_history.h"

#include "palace_lez_explorer_protocol.h"

#include <algorithm>
#include <limits>
#include <set>
#include <type_traits>
#include <utility>

namespace palace {
namespace {

constexpr std::size_t kMaximumHistoryActions = 256U;

bool nonzeroHash(const std::string& value)
{
    return value.size() == 64U
        && std::any_of(value.begin(), value.end(), [](const char value) {
            return value != '0';
        });
}

std::uint64_t actionId(const PalaceLezInstructionV3& instruction)
{
    return std::visit(
        [](const auto& value) -> std::uint64_t {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, PalaceLezInitializeV3>)
                return 0U;
            else
                return value.orderedActionId;
        },
        instruction.payload);
}

bool isInitialize(const PalaceLezInstructionV3& instruction)
{
    return std::holds_alternative<PalaceLezInitializeV3>(
        instruction.payload);
}

bool base58ToHex(
    const std::string& value,
    std::string& output)
{
    PalaceLezBytes32 decoded{};
    if (!PalaceLezCodec::parseAccountIdBase58(value, decoded))
        return false;
    output = PalaceLezCodec::bytes32Hex(decoded);
    return true;
}

bool verifyPalaceTransaction(
    const lez_explorer_protocol::TransactionV1& transaction,
    const PalaceLezExplorerHistoryExpectationV1& expectation,
    PalaceLezFinalizedActionV3& output,
    std::string& reason)
{
    if (!nonzeroHash(transaction.hashHex)
        || transaction.accountIdsBase58.size() < 2U
        || transaction.nonceCount != transaction.accountIdsBase58.size()
        || transaction.signatureCount != 1U) {
        reason = "finalized-transaction-mismatch";
        return false;
    }

    std::vector<std::string> accountIdsHex;
    accountIdsHex.reserve(transaction.accountIdsBase58.size());
    for (const std::string& accountIdBase58 :
         transaction.accountIdsBase58) {
        std::string accountIdHex;
        if (!base58ToHex(accountIdBase58, accountIdHex)) {
            reason = "finalized-transaction-mismatch";
            return false;
        }
        accountIdsHex.push_back(std::move(accountIdHex));
    }

    const PalaceLezWireInstruction decoded =
        PalaceLezCodec::decodeInstruction(transaction.instructionWords);
    if (!decoded.accepted) {
        reason = decoded.reason;
        return false;
    }
    const PalaceLezTransactionPlanV3 plan =
        PalaceLezCodec::buildTransaction(
            expectation.programIdHex,
            accountIdsHex[1],
            decoded.instruction);
    const std::size_t expectedSignatures = static_cast<std::size_t>(
        std::count(
            plan.signingRequirements.begin(),
            plan.signingRequirements.end(),
            true));
    if (!plan.accepted
        || plan.rootAccountIdHex != expectation.rootAccountIdHex
        || plan.accountIdsHex != accountIdsHex
        || plan.instructionWords != transaction.instructionWords
        || expectedSignatures != transaction.signatureCount) {
        reason = "finalized-transaction-mismatch";
        return false;
    }

    output.transactionHash = transaction.hashHex;
    output.orderedActionId = actionId(decoded.instruction);
    output.instruction = decoded.instruction;
    output.accountIdsHex = std::move(accountIdsHex);
    return true;
}

} // namespace

PalaceLezExplorerHistoryUpdateV1
PalaceLezExplorerHistorySession::start(
    const PalaceLezExplorerNetworkFingerprintV1& fingerprint,
    const PalaceLezExplorerFinalityLimitsV1& limits,
    const PalaceLezExplorerHistoryExpectationV1& expectation)
{
    fingerprint_ = {};
    limits_ = {};
    expectation_ = {};
    programIdBase58_.clear();
    rootAccountIdBase58_.clear();
    outcome_ = PalaceLezExplorerHistoryOutcome::Pending;
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
    reverseActions_.clear();
    latestFinalizedBlockId_ = 0U;
    latestFinalizedBlockHashHex_.clear();
    result_.reset();

    if (!lez_explorer_protocol::networkFingerprintAccepted(fingerprint))
        return setTerminal(
            PalaceLezExplorerHistoryOutcome::Rejected,
            "invalid-explorer-network-fingerprint");
    if (!lez_explorer_protocol::limitsAccepted(limits))
        return setTerminal(
            PalaceLezExplorerHistoryOutcome::Rejected,
            "invalid-explorer-limits");

    PalaceLezBytes32 programId{};
    PalaceLezBytes32 rootAccountId{};
    if (!PalaceLezCodec::parseBytes32Hex(
            expectation.programIdHex,
            programId)
        || !PalaceLezCodec::parseBytes32Hex(
            expectation.rootAccountIdHex,
            rootAccountId)
        || PalaceLezCodec::deriveRootPda(expectation.programIdHex)
            != expectation.rootAccountIdHex) {
        return setTerminal(
            PalaceLezExplorerHistoryOutcome::Rejected,
            "invalid-program-or-root");
    }

    fingerprint_ = fingerprint;
    limits_ = limits;
    expectation_ = expectation;
    programIdBase58_ = PalaceLezCodec::accountIdBase58(programId);
    rootAccountIdBase58_ =
        PalaceLezCodec::accountIdBase58(rootAccountId);
    started_ = true;
    reason_ = "awaiting-finalized-history-page";
    queueBlocksCommand(std::nullopt);
    return {
        true,
        true,
        PalaceLezExplorerHistoryOutcome::Pending,
        reason_,
    };
}

std::optional<PalaceLezExplorerCommandV1>
PalaceLezExplorerHistorySession::takeNextCommand()
{
    if (!started_ || finished_ || outstandingCommand_.has_value()
        || !readyCommand_.has_value()) {
        return std::nullopt;
    }
    outstandingCommand_ = std::move(readyCommand_);
    readyCommand_.reset();
    return outstandingCommand_;
}

PalaceLezExplorerHistoryUpdateV1
PalaceLezExplorerHistorySession::acceptResponse(
    const PalaceLezExplorerHttpResponseV1& response)
{
    if (!started_)
        return {false, false, outcome_, "history-session-not-started"};
    if (finished_)
        return {false, false, outcome_, "history-session-finished"};
    if (!outstandingCommand_.has_value()
        || response.commandSequence != outstandingCommand_->sequence) {
        return setTerminal(
            PalaceLezExplorerHistoryOutcome::Rejected,
            "explorer-command-correlation-mismatch");
    }

    const PalaceLezExplorerCommandV1 command = *outstandingCommand_;
    outstandingCommand_.reset();
    std::string metadataReason;
    if (!lez_explorer_protocol::responseMetadataAccepted(
            response,
            command,
            fingerprint_,
            metadataReason)) {
        return setTerminal(
            PalaceLezExplorerHistoryOutcome::Degraded,
            std::move(metadataReason));
    }
    if (command.kind != PalaceLezExplorerCommandKind::Blocks)
        return setTerminal(
            PalaceLezExplorerHistoryOutcome::Rejected,
            "explorer-command-phase-mismatch");
    return acceptBlocksBody(response.body);
}

PalaceLezExplorerHistoryOutcome
PalaceLezExplorerHistorySession::outcome() const
{
    return outcome_;
}

const std::string& PalaceLezExplorerHistorySession::reason() const
{
    return reason_;
}

bool PalaceLezExplorerHistorySession::waitingForResponse() const
{
    return outstandingCommand_.has_value();
}

bool PalaceLezExplorerHistorySession::scanExhausted() const
{
    return scanExhausted_;
}

std::size_t PalaceLezExplorerHistorySession::pagesScanned() const
{
    return pagesScanned_;
}

std::size_t PalaceLezExplorerHistorySession::blocksScanned() const
{
    return blocksScanned_;
}

std::optional<PalaceLezExplorerHistoryResultV1>
PalaceLezExplorerHistorySession::rebuildResult() const
{
    if (outcome_ != PalaceLezExplorerHistoryOutcome::Rebuilt)
        return std::nullopt;
    return result_;
}

PalaceLezExplorerHistoryUpdateV1
PalaceLezExplorerHistorySession::setTerminal(
    const PalaceLezExplorerHistoryOutcome outcome,
    std::string reason)
{
    const bool changed = outcome_ != outcome || reason_ != reason;
    outcome_ = outcome;
    reason_ = std::move(reason);
    finished_ = true;
    readyCommand_.reset();
    outstandingCommand_.reset();
    if (outcome != PalaceLezExplorerHistoryOutcome::Rebuilt)
        result_.reset();
    return {false, changed, outcome_, reason_};
}

PalaceLezExplorerHistoryUpdateV1
PalaceLezExplorerHistorySession::setPending(
    std::string reason,
    const bool scanExhausted)
{
    const bool changed =
        outcome_ != PalaceLezExplorerHistoryOutcome::Pending
        || reason_ != reason || scanExhausted_ != scanExhausted;
    outcome_ = PalaceLezExplorerHistoryOutcome::Pending;
    reason_ = std::move(reason);
    scanExhausted_ = scanExhausted;
    if (scanExhausted) {
        finished_ = true;
        readyCommand_.reset();
        outstandingCommand_.reset();
    }
    return {true, changed, outcome_, reason_};
}

void PalaceLezExplorerHistorySession::queueBlocksCommand(
    const std::optional<std::uint64_t> before)
{
    readyCommand_ = lez_explorer_protocol::makeBlocksCommand(
        fingerprint_,
        limits_,
        nextCommandSequence_++,
        blocksScanned_,
        before);
}

PalaceLezExplorerHistoryUpdateV1
PalaceLezExplorerHistorySession::acceptBlocksBody(
    const std::string& body)
{
    lez_explorer_protocol::BlockPageParseResultV1 parsed =
        lez_explorer_protocol::parseBlockPage(body, limits_);
    if (!parsed.accepted) {
        return setTerminal(
            PalaceLezExplorerHistoryOutcome::Degraded,
            std::move(parsed.reason));
    }
    ++pagesScanned_;
    if (parsed.blocks.empty())
        return setPending("palace-initialize-not-found", true);
    if (parsed.blocks.size() > limits_.maxBlocks - blocksScanned_) {
        return setTerminal(
            PalaceLezExplorerHistoryOutcome::Degraded,
            "block-scan-limit-exceeded");
    }

    for (std::size_t index = 0U; index < parsed.blocks.size(); ++index) {
        const lez_explorer_protocol::BlockV1& block =
            parsed.blocks[index];
        if (block.bedrockStatus != "Finalized") {
            return setTerminal(
                PalaceLezExplorerHistoryOutcome::Rejected,
                "history-block-not-finalized");
        }
        if (index != 0U) {
            const lez_explorer_protocol::BlockV1& newer =
                parsed.blocks[index - 1U];
            if (newer.id != block.id + 1U
                || newer.previousHashHex != block.hashHex
                || newer.timestamp < block.timestamp) {
                return setTerminal(
                    PalaceLezExplorerHistoryOutcome::Rejected,
                    "block-page-chain-order-mismatch");
            }
        } else if (oldestBlockId_ != 0U) {
            if (oldestBlockId_ != block.id + 1U
                || oldestBlockPreviousHashHex_ != block.hashHex
                || oldestBlockTimestamp_ < block.timestamp) {
                return setTerminal(
                    PalaceLezExplorerHistoryOutcome::Rejected,
                    "block-page-cursor-replay-or-overlap");
            }
        }
        if (!seenBlockIds_.insert(block.id).second
            || !seenBlockHashes_.insert(block.hashHex).second) {
            return setTerminal(
                PalaceLezExplorerHistoryOutcome::Rejected,
                "duplicate-or-replayed-block");
        }
        for (const lez_explorer_protocol::TransactionV1& transaction :
             block.transactions) {
            if (!seenTransactionHashes_.insert(
                    transaction.hashHex).second) {
                return setTerminal(
                    PalaceLezExplorerHistoryOutcome::Rejected,
                    "duplicate-transaction-hash");
            }
        }
    }

    if (latestFinalizedBlockId_ == 0U) {
        latestFinalizedBlockId_ = parsed.blocks.front().id;
        latestFinalizedBlockHashHex_ =
            parsed.blocks.front().hashHex;
    }

    bool initializeFound = false;
    for (const lez_explorer_protocol::BlockV1& block :
         parsed.blocks) {
        for (auto iterator = block.transactions.rbegin();
             iterator != block.transactions.rend();
             ++iterator) {
            const lez_explorer_protocol::TransactionV1& transaction =
                *iterator;
            if (transaction.kind
                    != lez_explorer_protocol::TransactionKind::Public
                || transaction.programIdBase58 != programIdBase58_
                || transaction.accountIdsBase58.empty()
                || transaction.accountIdsBase58.front()
                    != rootAccountIdBase58_) {
                continue;
            }
            if (reverseActions_.size() >= kMaximumHistoryActions)
                return setPending(
                    "history-action-window-exhausted",
                    true);

            PalaceLezFinalizedActionV3 action;
            std::string verificationReason;
            if (!verifyPalaceTransaction(
                    transaction,
                    expectation_,
                    action,
                    verificationReason)) {
                return setTerminal(
                    PalaceLezExplorerHistoryOutcome::Rejected,
                    std::move(verificationReason));
            }
            initializeFound = isInitialize(action.instruction);
            reverseActions_.push_back(std::move(action));
            if (initializeFound)
                break;
        }
        if (initializeFound)
            break;
    }

    blocksScanned_ += parsed.blocks.size();
    const lez_explorer_protocol::BlockV1& oldest =
        parsed.blocks.back();
    oldestBlockId_ = oldest.id;
    oldestBlockTimestamp_ = oldest.timestamp;
    oldestBlockPreviousHashHex_ = oldest.previousHashHex;

    if (initializeFound)
        return completeRebuild();
    if (blocksScanned_ >= limits_.maxBlocks
        || pagesScanned_ >= limits_.maxPages) {
        return setPending("history-scan-window-exhausted", true);
    }
    queueBlocksCommand(oldestBlockId_);
    return setPending("awaiting-older-finalized-history-page", false);
}

PalaceLezExplorerHistoryUpdateV1
PalaceLezExplorerHistorySession::completeRebuild()
{
    if (reverseActions_.empty())
        return setPending("palace-initialize-not-found", true);

    PalaceLezExplorerHistoryResultV1 rebuilt;
    rebuilt.actions.assign(
        reverseActions_.rbegin(),
        reverseActions_.rend());
    if (!isInitialize(rebuilt.actions.front().instruction)
        || rebuilt.actions.front().orderedActionId != 0U) {
        return setTerminal(
            PalaceLezExplorerHistoryOutcome::Rejected,
            "non-chronological-finalized-history");
    }

    std::uint64_t expectedActionId = 0U;
    std::set<std::string> accountIds;
    for (std::size_t index = 0U;
         index < rebuilt.actions.size();
         ++index) {
        const PalaceLezFinalizedActionV3& action =
            rebuilt.actions[index];
        if ((index != 0U && isInitialize(action.instruction))
            || action.orderedActionId != expectedActionId) {
            return setTerminal(
                PalaceLezExplorerHistoryOutcome::Rejected,
                "non-chronological-finalized-history");
        }
        for (const std::string& accountId : action.accountIdsHex) {
            if (accountIds.insert(accountId).second)
                rebuilt.uniqueAccountIdsHex.push_back(accountId);
        }
        if (index + 1U < rebuilt.actions.size()) {
            if (expectedActionId
                == std::numeric_limits<std::uint64_t>::max()) {
                return setTerminal(
                    PalaceLezExplorerHistoryOutcome::Rejected,
                    "non-chronological-finalized-history");
            }
            ++expectedActionId;
        }
    }

    rebuilt.latestFinalizedBlockId = latestFinalizedBlockId_;
    rebuilt.latestFinalizedBlockHashHex =
        latestFinalizedBlockHashHex_;
    result_ = std::move(rebuilt);
    outcome_ = PalaceLezExplorerHistoryOutcome::Rebuilt;
    reason_ = "finalized-history-rebuilt";
    finished_ = true;
    readyCommand_.reset();
    outstandingCommand_.reset();
    return {
        true,
        true,
        PalaceLezExplorerHistoryOutcome::Rebuilt,
        reason_,
    };
}

} // namespace palace
