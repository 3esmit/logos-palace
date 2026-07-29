#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace palace {

enum class StorageCatalogObjectKind : std::uint8_t {
    Blob = 0U,
    PropManifest = 1U,
    RoomManifest = 2U,
    PalaceManifest = 3U,
};

enum class StorageCatalogLocalPhase : std::uint8_t {
    Missing = 0U,
    Fetching = 1U,
    Verified = 2U,
};

enum class StorageCatalogPublicationStage : std::uint8_t {
    Staged = 0U,
    Uploading = 1U,
    VerifyingLocal = 2U,
    Published = 3U,
    Failed = 4U,
};

enum class StorageCatalogRetention : std::uint8_t {
    Unknown = 0U,
    Degraded = 1U,
    Redundant = 2U,
};

enum class StorageCatalogOperationKind : std::uint8_t {
    Upload = 0U,
    LocalFetch = 1U,
    PublicationVerification = 2U,
};

enum class StorageCatalogOperationPhase : std::uint8_t {
    AwaitingAcknowledgement = 0U,
    Active = 1U,
};

enum class StorageCatalogDownloadOutcome : std::uint8_t {
    Succeeded = 0U,
    Failed = 1U,
    Canceled = 2U,
};

struct StorageCatalogSessionConfigV3 {
    // Canonical 32-byte LEZ account ID: exactly 64 lower-case hex digits.
    std::string localHolderAccountId;
    std::size_t maxObjects = 256U;
    std::size_t maxOperations = 128U;
    std::size_t maxChildrenPerObject = 64U;
    std::size_t maxAttestationsPerObject = 16U;
    std::size_t maxPendingAttestationChallenges = 128U;
    std::size_t maxCompletedOperations = 256U;
    std::size_t maxCompletedChallenges = 256U;
    std::size_t maxSignatureBytes = 512U;
    std::uint64_t maxChallengeLifetimeSeconds = 300U;
    std::uint64_t maxObjectBytes = 16U * 1024U * 1024U;
    std::uint64_t initialSessionEpoch = 1U;
};

struct StorageCatalogManifestChildV1 {
    std::string objectId;
    std::string cid;
    std::uint64_t byteLength = 0U;
    std::string contentSha256;
};

struct StorageCatalogObjectSpecV2 {
    std::string objectId;
    StorageCatalogObjectKind kind = StorageCatalogObjectKind::Blob;
    std::uint64_t byteLength = 0U;
    std::string contentSha256;
    std::vector<StorageCatalogManifestChildV1> children;
};

struct StorageCatalogOperation {
    StorageCatalogOperationKind kind = StorageCatalogOperationKind::Upload;
    StorageCatalogOperationPhase phase =
        StorageCatalogOperationPhase::AwaitingAcknowledgement;
    std::string operationId;
    std::string objectId;
    std::string cid;
    bool localOnly = false;
    std::uint64_t maxBytes = 0U;
};

struct StorageCatalogTransition {
    bool accepted = false;
    std::string reason;
    std::optional<StorageCatalogOperation> operation;
};

struct StorageCatalogDownloadTerminalV2 {
    std::string protocol;
    std::uint32_t version = 0U;
    std::string operationId;
    std::string cid;
    StorageCatalogDownloadOutcome outcome =
        StorageCatalogDownloadOutcome::Failed;
};

struct StorageCatalogDownloadAcknowledgementV2 {
    std::string protocol;
    std::uint32_t version = 0U;
    bool accepted = false;
    std::string operationId;
    std::string cid;
};

struct StorageCatalogHolderChallengeV1 {
    std::uint32_t version = 1U;
    std::string challengeId;
    std::string holderAccountId;
    std::uint64_t holderKeyEpoch = 0U;
    std::string objectId;
    std::string cid;
    std::uint64_t byteLength = 0U;
    std::string contentSha256;
    std::uint64_t issuedAtUnixSeconds = 0U;
    std::uint64_t expiresAtUnixSeconds = 0U;
};

struct StorageCatalogHolderAttestationReceiptV1 {
    std::uint32_t version = 1U;
    std::string challengeId;
    std::string holderAccountId;
    std::uint64_t holderKeyEpoch = 0U;
    std::string objectId;
    std::string cid;
    std::uint64_t byteLength = 0U;
    std::string contentSha256;
    std::uint64_t issuedAtUnixSeconds = 0U;
    std::uint64_t expiresAtUnixSeconds = 0U;
};

struct StorageCatalogChallengeTransition {
    bool accepted = false;
    std::string reason;
    std::optional<StorageCatalogHolderChallengeV1> challenge;
};

struct StorageCatalogReconciliation {
    bool accepted = false;
    std::string reason;
    std::vector<StorageCatalogOperation> requeuedOperations;
};

// Transport-neutral trust boundary. Core supplies an implementation backed by
// the authoritative holder key for holderAccountId and holderKeyEpoch.
class StorageCatalogAttestationVerifier {
public:
    virtual ~StorageCatalogAttestationVerifier() = default;
    virtual bool verify(
        const std::string& holderAccountId,
        std::uint64_t holderKeyEpoch,
        const std::string& canonicalReceipt,
        const std::string& signature) const = 0;
};

struct StorageCatalogObjectStatus {
    bool found = false;
    bool reconciliationRequired = false;
    // False only for a child commitment learned from a CID-verified
    // bootstrap manifest whose own bytes have not yet been admitted.
    bool specificationAdmitted = false;
    StorageCatalogObjectSpecV2 specification;
    StorageCatalogLocalPhase localPhase =
        StorageCatalogLocalPhase::Missing;
    StorageCatalogPublicationStage publicationStage =
        StorageCatalogPublicationStage::Failed;
    // Degraded means exactly one current evidence location. Redundant means
    // local Verified plus at least one unexpired distinct attestation, or
    // local not Verified plus at least two unexpired distinct attestations.
    StorageCatalogRetention retention = StorageCatalogRetention::Unknown;
    std::string cid;

    // Attestations prove signed possession claims, not Storage provider
    // identity or service availability.
    std::set<std::string> attestedHolderIds;
};

std::string storageCatalogLocalPhaseName(StorageCatalogLocalPhase phase);
std::string storageCatalogPublicationStageName(
    StorageCatalogPublicationStage stage);
std::string storageCatalogRetentionName(StorageCatalogRetention retention);

// Strict canonical manifest bytes. Returns empty for invalid kinds, IDs, CIDs,
// child records, or non-canonical child order.
std::string canonicalStorageCatalogManifestV1(
    StorageCatalogObjectKind kind,
    const std::string& objectId,
    const std::vector<StorageCatalogManifestChildV1>& children);

// Exact bytes signed by a holder. Returns empty for a non-canonical receipt.
std::string canonicalHolderAttestationReceiptV1(
    const StorageCatalogHolderAttestationReceiptV1& receipt);

// Transport-neutral owner of immutable catalog publication/retrieval and
// application-level holder attestations. Storage terminals prove only that
// exact bytes arrived for a caller-correlated operation.
class PalaceStorageCatalogSession {
public:
    bool configure(const StorageCatalogSessionConfigV3& config);
    bool hasConfiguration() const;
    const StorageCatalogSessionConfigV3& configuration() const;
    bool reconciliationRequired() const;
    std::uint64_t sessionEpoch() const;
    std::uint64_t lastOperationSequence() const;
    std::uint64_t lastChallengeSequence() const;

    StorageCatalogTransition stagePublicationObject(
        const StorageCatalogObjectSpecV2& specification,
        const std::string& fullBytes);
    StorageCatalogTransition trackPublishedObject(
        const StorageCatalogObjectSpecV2& specification,
        const std::string& cid);

    // Cold-start trust seam. The expected CID must be a canonical SHA-256 CID
    // whose digest matches fullBytes. Only an exact bounded canonical Palace
    // manifest is admitted; its Room child commitments become fetchable but
    // remain unadmitted until their own bytes pass the same trust boundary.
    StorageCatalogTransition admitFinalizedPalaceManifest(
        const std::string& expectedCid,
        const std::string& fullBytes);

    StorageCatalogTransition beginUpload(const std::string& objectId);
    StorageCatalogTransition beginPublicationVerification(
        const std::string& objectId);
    StorageCatalogTransition beginLocalFetch(const std::string& objectId);

    // Upload dispatch has no typed V2 response. Download dispatch must retain
    // exact protocol/version/operation/CID correlation inside this boundary.
    StorageCatalogTransition operationAcknowledged(
        const std::string& operationId,
        bool acceptedByModule);
    StorageCatalogTransition downloadAcknowledged(
        const StorageCatalogDownloadAcknowledgementV2& acknowledgement);
    StorageCatalogTransition uploadFinished(
        const std::string& operationId,
        bool succeeded,
        const std::string& cid);
    StorageCatalogTransition downloadFinished(
        const StorageCatalogDownloadTerminalV2& terminal,
        const std::string& fullBytes);

    StorageCatalogTransition retryOperation(
        const std::string& operationId);
    StorageCatalogTransition abandonOperation(
        const std::string& operationId);
    StorageCatalogTransition retryPublication(
        const std::string& objectId,
        const std::string& fullBytes);
    StorageCatalogTransition markLocalMissing(const std::string& objectId);

    StorageCatalogChallengeTransition beginHolderAttestation(
        const std::string& objectId,
        const std::string& holderAccountId,
        std::uint64_t holderKeyEpoch,
        std::uint64_t issuedAtUnixSeconds,
        std::uint64_t expiresAtUnixSeconds);
    StorageCatalogTransition completeHolderAttestation(
        const StorageCatalogHolderAttestationReceiptV1& receipt,
        const std::string& signature,
        std::uint64_t nowUnixSeconds,
        const StorageCatalogAttestationVerifier& verifier);
    StorageCatalogTransition abandonHolderAttestation(
        const std::string& challengeId);
    bool revokeHolderAttestation(
        const std::string& objectId,
        const std::string& holderAccountId);

    StorageCatalogObjectStatus status(
        const std::string& objectId,
        std::uint64_t nowUnixSeconds) const;
    std::vector<StorageCatalogOperation> pendingOperations() const;
    std::vector<StorageCatalogHolderChallengeV1>
        pendingAttestationChallenges() const;
    std::vector<std::string> completedOperationIds() const;
    std::vector<std::string> completedChallengeIds() const;

    // Restore always requires this call with a strictly newer epoch. It clears
    // volatile bytes and attestations, rejects late callbacks/challenges, and
    // requeues only Storage downloads safe without caller-owned bytes.
    StorageCatalogReconciliation reconcileAfterRestart(
        std::uint64_t newSessionEpoch);

    std::string canonicalState() const;
    bool restoreCanonicalState(const std::string& serialized);

private:
    struct AttestationRecord {
        std::uint64_t holderKeyEpoch = 0U;
        std::uint64_t issuedAtUnixSeconds = 0U;
        std::uint64_t expiresAtUnixSeconds = 0U;
    };

    struct ObjectRecord {
        StorageCatalogObjectSpecV2 specification;
        bool bootstrapAdmissionPending = false;
        StorageCatalogLocalPhase localPhase =
            StorageCatalogLocalPhase::Missing;
        StorageCatalogPublicationStage publicationStage =
            StorageCatalogPublicationStage::Failed;
        std::string cid;
        std::map<std::string, AttestationRecord> attestations;
    };

    bool canAct() const;
    bool validSpecification(
        const StorageCatalogObjectSpecV2& specification) const;
    bool verifyFullBytes(
        const StorageCatalogObjectSpecV2& specification,
        const std::string& fullBytes) const;
    StorageCatalogTransition admitBootstrapRetrievedObject(
        const std::string& objectId,
        const std::string& fullBytes);
    bool validBootstrapPendingObject(
        const ObjectRecord& object) const;
    bool objectHasPath(
        const std::string& fromObjectId,
        const std::string& toObjectId) const;
    bool childrenPublished(const ObjectRecord& object) const;
    bool hasObjectOperation(
        const std::string& objectId,
        std::optional<StorageCatalogOperationKind> kind = std::nullopt) const;
    std::size_t challengeReservations(const std::string& objectId) const;
    bool validState() const;

    StorageCatalogTransition beginOperation(
        const std::string& objectId,
        StorageCatalogOperationKind kind);
    StorageCatalogTransition acknowledgeReservedOperation(
        const std::string& operationId,
        bool acceptedByModule);
    std::optional<StorageCatalogOperation> enqueueReplacement(
        const StorageCatalogOperation& previous);
    std::optional<std::string> generateOperationId(
        const std::string& cid);
    std::optional<std::string> generateChallengeId();
    void rememberCompletedOperation(const std::string& operationId);
    void rememberCompletedChallenge(const std::string& challengeId);
    bool wasOperationCompleted(const std::string& operationId) const;
    bool wasChallengeCompleted(const std::string& challengeId) const;
    void rollbackAcknowledgement(
        const StorageCatalogOperation& operation);
    void settleAbandonedOperation(
        const StorageCatalogOperation& operation);
    void settleChallenge(const std::string& challengeId);
    void pruneExpiredAttestations(std::uint64_t nowUnixSeconds);
    void pruneExpiredChallenges(std::uint64_t nowUnixSeconds);

    StorageCatalogSessionConfigV3 m_config;
    bool m_configured = false;
    bool m_reconciliationRequired = false;
    std::uint64_t m_sessionEpoch = 0U;
    std::uint64_t m_lastOperationSequence = 0U;
    std::uint64_t m_lastChallengeSequence = 0U;
    std::map<std::string, ObjectRecord> m_objects;
    std::map<std::string, StorageCatalogOperation> m_operations;
    std::map<std::string, StorageCatalogHolderChallengeV1> m_challenges;
    std::deque<std::string> m_completedOperationIds;
    std::set<std::string> m_completedOperationIndex;
    std::deque<std::string> m_completedChallengeIds;
    std::set<std::string> m_completedChallengeIndex;
};

} // namespace palace
