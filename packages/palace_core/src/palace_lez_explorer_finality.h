#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace palace {

enum class PalaceLezExplorerFinalityOutcome : std::uint8_t {
    Pending = 0U,
    Finalized = 1U,
    Degraded = 2U,
    Rejected = 3U,
};

enum class PalaceLezExplorerCommandKind : std::uint8_t {
    Blocks = 0U,
    Account = 1U,
};

// Caller-pinned network and deployed explorer schema. The Leptos suffix is
// build-generated, so changing it is an explicit fingerprint change.
struct PalaceLezExplorerNetworkFingerprintV1 {
    std::uint32_t fingerprintVersion = 1U;
    std::string networkId;
    std::string channelIdHex;
    std::string explorerOrigin;
    std::string serverFunctionSuffix;
    std::uint32_t explorerSchemaVersion = 1U;
};

struct PalaceLezExplorerFinalityLimitsV1 {
    std::size_t pageSize = 32U;
    std::size_t maxPages = 16U;
    std::size_t maxBlocks = 512U;
    std::size_t maxTransactionsPerBlock = 128U;
    std::size_t maxAccounts = 8U;
    std::size_t maxInstructionWords = 1024U;
    std::size_t maxJsonBytes = 2U * 1024U * 1024U;
    std::size_t maxAccountDataBytes = 64U * 1024U;
    std::size_t maxJsonDepth = 24U;
    std::size_t maxJsonNodes = 200000U;
};

struct PalaceLezExplorerAccountExpectationV1 {
    std::string accountIdBase58;
    std::string programOwnerBase58;
    std::vector<std::uint8_t> expectedData;
    std::string expectedDataSha256Hex;
};

struct PalaceLezExplorerTransactionExpectationV1 {
    std::string transactionHashHex;
    std::string programIdBase58;
    std::vector<std::string> accountIdsBase58;
    std::vector<std::uint32_t> instructionWords;
    // SHA-256 of instructionWords concatenated as little-endian u32 values.
    std::string instructionWordsSha256Hex;

    // Target must occur strictly after this block. Scanning stops when this
    // inclusive lower boundary is reached.
    std::uint64_t baselineBlockId = 0U;

    // Exact post-state checks, in the same order as accountIdsBase58. Every
    // transaction account is checked; no current-state response is trusted by
    // position alone.
    std::vector<PalaceLezExplorerAccountExpectationV1> accountExpectations;
};

struct PalaceLezExplorerCommandV1 {
    std::uint64_t sequence = 0U;
    PalaceLezExplorerCommandKind kind =
        PalaceLezExplorerCommandKind::Blocks;
    std::string method;
    std::string origin;
    std::string path;
    std::string requestContentType;
    std::string formBody;
    std::size_t maxResponseBytes = 0U;
};

// Filled by the transport adapter. effectiveOrigin is the origin after request
// completion; redirected must reflect any redirect attempt, even when the
// transport policy refused it.
struct PalaceLezExplorerHttpResponseV1 {
    std::uint64_t commandSequence = 0U;
    int statusCode = 0;
    std::string contentType;
    std::string effectiveOrigin;
    bool redirected = false;
    std::string transportError;
    std::string body;
};

struct PalaceLezExplorerFinalityUpdate {
    bool accepted = false;
    bool changed = false;
    PalaceLezExplorerFinalityOutcome outcome =
        PalaceLezExplorerFinalityOutcome::Pending;
    std::string reason;
};

struct PalaceLezExplorerFinalityAccountEvidenceV1 {
    std::string accountIdBase58;
    std::string programOwnerBase58;
    std::string dataSha256Hex;
};

// Immutable proof boundary emitted only after the session has verified the
// exact finalized transaction and every ordered post-state account snapshot.
class PalaceLezExplorerFinalityCertificateV1 {
public:
    PalaceLezExplorerFinalityCertificateV1(
        const PalaceLezExplorerFinalityCertificateV1&) = default;
    PalaceLezExplorerFinalityCertificateV1(
        PalaceLezExplorerFinalityCertificateV1&&) noexcept = default;
    PalaceLezExplorerFinalityCertificateV1& operator=(
        const PalaceLezExplorerFinalityCertificateV1&) = default;
    PalaceLezExplorerFinalityCertificateV1& operator=(
        PalaceLezExplorerFinalityCertificateV1&&) noexcept = default;

    std::uint32_t certificateVersion() const;
    const std::string& transactionHashHex() const;
    const std::string& programIdBase58() const;
    const std::vector<std::string>& accountIdsBase58() const;
    const std::vector<std::uint32_t>& instructionWords() const;
    const std::string& instructionWordsSha256Hex() const;
    std::uint64_t finalizedBlockId() const;
    std::uint64_t finalizedBlockHeight() const;
    const std::string& finalizedBlockHashHex() const;
    const std::vector<PalaceLezExplorerFinalityAccountEvidenceV1>&
    accountEvidence() const;

private:
    friend class PalaceLezExplorerFinalitySession;

    PalaceLezExplorerFinalityCertificateV1() = default;

    std::uint32_t certificateVersion_ = 1U;
    std::string transactionHashHex_;
    std::string programIdBase58_;
    std::vector<std::string> accountIdsBase58_;
    std::vector<std::uint32_t> instructionWords_;
    std::string instructionWordsSha256Hex_;
    std::uint64_t finalizedBlockId_ = 0U;
    // LEZ explorer block_id is the canonical public chain height.
    std::uint64_t finalizedBlockHeight_ = 0U;
    std::string finalizedBlockHashHex_;
    std::vector<PalaceLezExplorerFinalityAccountEvidenceV1>
        accountEvidence_;
};

// Transport-neutral, single-flight finality verifier. It emits bounded POST
// commands; Core's Qt adapter executes them asynchronously with redirects
// disabled and passes immutable response metadata back here.
class PalaceLezExplorerFinalitySession {
public:
    PalaceLezExplorerFinalityUpdate start(
        const PalaceLezExplorerNetworkFingerprintV1& fingerprint,
        const PalaceLezExplorerFinalityLimitsV1& limits,
        const PalaceLezExplorerTransactionExpectationV1& expectation);

    std::optional<PalaceLezExplorerCommandV1> takeNextCommand();
    PalaceLezExplorerFinalityUpdate acceptResponse(
        const PalaceLezExplorerHttpResponseV1& response);

    PalaceLezExplorerFinalityOutcome outcome() const;
    const std::string& reason() const;
    bool waitingForResponse() const;
    bool scanExhausted() const;
    std::size_t pagesScanned() const;
    std::size_t blocksScanned() const;
    std::size_t verifiedAccountCount() const;
    std::uint64_t finalizedBlockId() const;
    const std::string& finalizedBlockHashHex() const;
    std::optional<PalaceLezExplorerFinalityCertificateV1>
    finalityCertificate() const;

    static std::string instructionWordsSha256Hex(
        const std::vector<std::uint32_t>& words);
    static std::string bytesSha256Hex(
        const std::vector<std::uint8_t>& bytes);

private:
    enum class Phase : std::uint8_t {
        Idle = 0U,
        ScanningBlocks = 1U,
        CheckingAccounts = 2U,
        Finished = 3U,
    };

    PalaceLezExplorerFinalityUpdate setTerminal(
        PalaceLezExplorerFinalityOutcome outcome,
        std::string reason);
    PalaceLezExplorerFinalityUpdate setPending(
        std::string reason,
        bool scanExhausted);
    void queueBlocksCommand(std::optional<std::uint64_t> before);
    void queueAccountCommand(std::size_t accountIndex);
    PalaceLezExplorerFinalityUpdate acceptBlocksBody(
        const std::string& body);
    PalaceLezExplorerFinalityUpdate acceptAccountBody(
        const std::string& body);

    PalaceLezExplorerNetworkFingerprintV1 fingerprint_;
    PalaceLezExplorerFinalityLimitsV1 limits_;
    PalaceLezExplorerTransactionExpectationV1 expectation_;
    PalaceLezExplorerFinalityOutcome outcome_ =
        PalaceLezExplorerFinalityOutcome::Pending;
    Phase phase_ = Phase::Idle;
    std::string reason_ = "not-started";
    bool scanExhausted_ = false;
    std::uint64_t nextCommandSequence_ = 1U;
    std::optional<PalaceLezExplorerCommandV1> readyCommand_;
    std::optional<PalaceLezExplorerCommandV1> outstandingCommand_;
    std::size_t pagesScanned_ = 0U;
    std::size_t blocksScanned_ = 0U;
    std::size_t verifiedAccounts_ = 0U;
    std::uint64_t oldestBlockId_ = 0U;
    std::uint64_t oldestBlockTimestamp_ = 0U;
    std::string oldestBlockPreviousHashHex_;
    std::set<std::uint64_t> seenBlockIds_;
    std::set<std::string> seenBlockHashes_;
    std::set<std::string> seenTransactionHashes_;
    std::uint64_t finalizedBlockId_ = 0U;
    std::string finalizedBlockHashHex_;
    std::optional<PalaceLezExplorerFinalityCertificateV1> certificate_;
};

} // namespace palace
