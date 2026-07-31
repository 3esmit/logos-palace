#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "logos_module_context.h"

#include "palace_action_journal.h"
#include "palace_callback_lifetime.h"
#if defined(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
#include "palace_delivery_acceptance_fixture.h"
#endif
#include "palace_delivery_bridge.h"
#include "palace_delivery_identity_store.h"
#include "palace_delivery_session.h"
#include "palace_lez.h"
#include "palace_lez_authority_bundle_store.h"
#include "palace_lez_authority_state.h"
#include "palace_lez_coordinator_store.h"
#include "palace_lez_explorer_finality.h"
#include "palace_lez_explorer_history.h"
#include "palace_lez_explorer_qt_transport.h"
#include "palace_lez_explorer_submission_recovery.h"
#include "palace_human_moderation.h"
#include "palace_identity.h"
#include "palace_lez_finality_expectation.h"
#include "palace_lez_submission_intent_store.h"
#include "palace_projection.h"
#include "palace_room_transition.h"
#include "palace_asset_authoring.h"
#include "palace_sha256.h"
#include "palace_storage.h"
#include "palace_storage_catalog_session.h"
#include "palace_storage_mvp.h"
#include "palace_storage_mvp_catalog_store.h"
#include "palace_storage_module_codec.h"
#include "palace_storage_module_session.h"
#include "palace_verified_asset_store.h"

namespace palace::core_detail {

struct PalaceVmPromotionRecoveryInputV1 {
    std::string actionId;
    std::string script;
    std::string scriptBundleCid;
    std::string roomEpoch;
    std::string trigger;
    std::string priorState;
    std::string allowedRooms;
    std::string provisionalReceipt;
    std::string expectedFinalizedReceipt;
    std::uint64_t instructionBudget = 0U;
    bool roomLocked = false;
    bool canMutateSharedState = false;
};

struct PalaceVmPromotionRecoveryResultV1 {
    bool accepted = false;
    std::string reason;
    std::string finalizedReceipt;
};

struct PalaceLezTrackedSubmissionRecoveryResultV1 {
    bool accepted = false;
    std::string reason;
    std::string transactionHash;
    ActionJournal repairedJournal;
    PalaceLezSubmissionIntentV1 committedIntent;
};

inline bool samePalaceLezRecoveryPlanV1(
    const PalaceLezTransactionPlanV3& first,
    const PalaceLezTransactionPlanV3& second)
{
    return first.accepted && second.accepted
        && first.programIdHex == second.programIdHex
        && first.rootAccountIdHex == second.rootAccountIdHex
        && first.accountIdsHex == second.accountIdsHex
        && first.signingRequirements == second.signingRequirements
        && first.instructionWords == second.instructionWords;
}

inline bool canonicalPalaceLezTransactionHashV1(
    const std::string& value)
{
    return value.size() == 64U
        && std::all_of(
            value.begin(),
            value.end(),
            [](const unsigned char character) {
                return (character >= '0' && character <= '9')
                    || (character >= 'a' && character <= 'f');
            });
}

// A tracked coordinator entry is recovery evidence only after the complete
// coordinator snapshot is durable. Callers must persist it before advancing
// the action journal or committing the submission intent.
inline PalaceLezCoordinatorStoreStatus
persistTrackedPalaceSubmissionV1(
    const PalaceLezTransactionCoordinator& coordinator,
    const PalaceLezCoordinatorStore* store)
{
    return store == nullptr
        ? PalaceLezCoordinatorStoreStatus::InvalidArgument
        : store->save(coordinator);
}

// Repairs the crash boundary where the accepted transaction coordinator was
// durable but the queued action journal was not advanced yet. Only one exact
// coordinator entry can supply the missing transaction hash.
inline PalaceLezTrackedSubmissionRecoveryResultV1
recoverTrackedPalaceSubmissionV1(
    const std::string& actionId,
    const PalaceLezTransactionPlanV3& requestedPlan,
    const PalaceLezSubmissionIntentV1& intent,
    const std::vector<PalaceLezTrackedTransaction>& transactions,
    const ActionJournal& journal)
{
    if (intent.phase
            != PalaceLezSubmissionIntentPhase::MayHaveBeenSubmitted
        || intent.actionId != actionId
        || !samePalaceLezRecoveryPlanV1(
            requestedPlan, intent.plan)) {
        return {
            false,
            "tracked-submission-request-conflict",
            {},
            {},
            {},
        };
    }

    std::uint64_t orderedActionId = 0U;
    if (!PalaceLezCodec::parseOrderedActionId(
            actionId, orderedActionId)) {
        return {
            false,
            "tracked-submission-action-invalid",
            {},
            {},
            {},
        };
    }

    const PalaceLezTrackedTransaction* exact = nullptr;
    for (const PalaceLezTrackedTransaction& tracked : transactions) {
        if (tracked.orderedActionId != orderedActionId
            || tracked.plan.rootAccountIdHex
                != intent.plan.rootAccountIdHex) {
            continue;
        }
        if (exact != nullptr
            || !canonicalPalaceLezTransactionHashV1(
                tracked.transactionHash)
            || tracked.expectedRootDataSha256Hex
                != intent.expectedRootDataSha256Hex
            || !samePalaceLezRecoveryPlanV1(
                tracked.plan, intent.plan)) {
            return {
                false,
                "tracked-submission-coordinator-conflict",
                {},
                {},
                {},
            };
        }
        exact = &tracked;
    }
    if (exact == nullptr) {
        return {
            false,
            "tracked-submission-not-found",
            {},
            {},
            {},
        };
    }

    const ActionStatus status = journal.status(actionId);
    if (status.durableStage != DurableActionStage::Queued
        || !status.transactionHash.empty()) {
        return {
            false,
            "tracked-submission-journal-conflict",
            {},
            {},
            {},
        };
    }
    ActionJournal repaired = journal;
    if (!repaired.markSubmittedToLez(
            actionId, exact->transactionHash)) {
        return {
            false,
            "tracked-submission-journal-conflict",
            {},
            {},
            {},
        };
    }

    PalaceLezSubmissionIntentV1 committed = intent;
    committed.phase = PalaceLezSubmissionIntentPhase::Committed;
    committed.transactionHash = exact->transactionHash;
    return {
        true,
        "tracked-submission-recovered",
        exact->transactionHash,
        std::move(repaired),
        std::move(committed),
    };
}

inline std::string vmLengthEncodedV1(
    const std::string& value)
{
    return std::to_string(value.size()) + ":" + value;
}

inline std::string palaceVmTurnInputDigestV1(
    const PalaceVmPromotionRecoveryInputV1& input)
{
    return crypto::sha256Hex(
        "palace-vm-turn-input-v1;"
        "action=" + vmLengthEncodedV1(input.actionId) + ";"
        "script=" + vmLengthEncodedV1(input.script) + ";"
        "bundle=" + vmLengthEncodedV1(
            input.scriptBundleCid) + ";"
        "epoch=" + vmLengthEncodedV1(input.roomEpoch) + ";"
        "trigger=" + vmLengthEncodedV1(input.trigger) + ";"
        "prior_state=" + vmLengthEncodedV1(
            input.priorState) + ";"
        "allowed_rooms=" + vmLengthEncodedV1(
            input.allowedRooms) + ";"
        "room_locked="
        + (input.roomLocked ? std::string("1;")
                            : std::string("0;"))
        + "can_mutate_shared_state="
        + (input.canMutateSharedState ? std::string("1;")
                                      : std::string("0;"))
        + "instruction_budget="
        + std::to_string(input.instructionBudget) + ";");
}

inline PalaceVmPromotionRecoveryResultV1
recoverPromotedPalaceVmReceiptV1(
    const std::string& moduleStatus,
    const PalaceVmPromotionRecoveryInputV1& input)
{
    const std::string expectedStatus =
        "status=promoted;action=" + input.actionId
        + ";turn_input_digest="
        + palaceVmTurnInputDigestV1(input)
        + ";provisional_receipt_sha256="
        + crypto::sha256Hex(input.provisionalReceipt)
        + ";finalized_receipt_sha256="
        + crypto::sha256Hex(
            input.expectedFinalizedReceipt);
    if (moduleStatus != expectedStatus) {
        return {
            false,
            "vm-promotion-evidence-mismatch",
            {},
        };
    }
    return {
        true,
        "promotion-recovered",
        input.expectedFinalizedReceipt,
    };
}

} // namespace palace::core_detail

// Palace Core is the sole future owner of LEZ, Delivery, Storage, VM, local
// projection, and recovery composition. This initial API exposes the durable
// action seam; UI code never calls upstream modules directly.
class PalaceCoreImpl : public LogosModuleContext {
public:
    ~PalaceCoreImpl() override;

    std::string applicationRoundTrip(
        const std::string& payload) const;
    std::string enterRoom(const std::string& roomId);
    std::string previewSpot(const std::string& spotId);
    std::string useSpot(const std::string& spotId);
    std::string reconcileSpot();
    std::string spotStatus();
    std::string vmTurnMetrics(const std::string& actionId,
                              const std::string& phase);
    std::string startLez(const std::string& password);
    std::string lezStatus() const;
    std::string createIdentity(const std::string& displayName);
    std::string identityStatus() const;
    std::string openPalace(const std::string& palaceUri);
    std::string palaceStatus() const;
    std::string startDelivery(const std::string& nodeConfig);
    std::string subscribeRoom(const std::string& networkId,
                              const std::string& palaceId,
                              const std::string& roomId,
                              std::int64_t roomEpoch);
    std::string deliverySessionStatus();
    std::string participantProjection();
    std::string deliveryNodeEvidence();
    std::string say(const std::string& text);
    std::string move(std::int64_t x, std::int64_t y);
    std::string wearProp(const std::string& propId);
    std::string removeProp(const std::string& propId);
    std::string refreshPresence();
    std::string startStorage(const std::string& nodeConfig);
    std::string storageSessionStatus();
    // Multi-node mesh: peerId + SPR after Storage is running. Gate 3 uses SPR
    // as bootstrap-node for peer B/C and peerId for explicit loopback dials.
    std::string storagePeerEndpoint();
    // Dial an explicit peer (JSON array of multiaddrs). Completes async via
    // storageConnect; success here only means the connect command was sent.
    std::string connectStoragePeer(
        const std::string& peerId,
        const std::string& addressesJson);
    std::string fetchPngDerivative(const std::string& sourceCid,
                                   const std::string& derivativeCid,
                                   std::uint64_t byteLength,
                                   const std::string& contentSha256,
                                   std::uint32_t width,
                                   std::uint32_t height);
    std::string assetStatus(const std::string& derivativeCid) const;
    std::string publishVerifiedPng(const std::string& handle);
    std::string publicationStatus(const std::string& handle) const;
    std::string beginAssetStage(const std::string& label);
    std::string appendAssetStageChunk(
        const std::string& sessionId,
        std::uint64_t sequence,
        const std::string& canonicalBase64);
    std::string commitAssetStage(
        const std::string& sessionId);
    std::string cancelAssetStage(
        const std::string& sessionId);
    std::string assetAuthoringCatalog() const;
    std::string reviewAsset(
        const std::string& handle,
        const std::string& decision);
    std::string publishAsset(
        const std::string& handle);
    std::string assignRoomBackground(
        const std::string& roomId,
        const std::string& handle);
    std::string assignPropAsset(
        const std::string& propId,
        const std::string& handle,
        std::uint32_t anchorX,
        std::uint32_t anchorY,
        const std::string& layer);
    std::string publishMvpStorageBundle();
    std::string mvpStorageBundleStatus();
    std::string fetchMvpStorageBundle(const std::string& catalogBase64);
    std::string verifyMvpStorageRetention();
    std::string storageObjectStatus(const std::string& objectId);
    std::string roomTitle() const;
    std::string roomBackgroundHandle() const;
    std::string activePropAsset() const;
    std::string syncHealth() const;
    std::string localProjection() const;
    // Human moderation commands derive authority context and exact schema-v3
    // JSON inside Core. UI code supplies only the selected user or prop.
    std::string banUser(const std::string& subjectUserIdHex);
    std::string banProp(const std::string& propId);
    std::string moderationStatus() const;
    // `actionId` is a canonical decimal u64 shared with the guest. Zero is
    // reserved for Palace initialization; later actions are positive.
    std::string submitIntent(const std::string& actionId);
    // Submits one of the 14 exact schema-v3 transitions through LEZ. Numeric
    // u64 fields are decimal strings, preventing JSON number precision loss.
    std::string submitPalaceTransition(const std::string& actionId,
                                       const std::string& stateAccountIdHex,
                                       const std::string& callerAccountIdHex,
                                       const std::string& programIdHex,
                                       const std::string& transitionJson);
    std::string observePalaceTransition(const std::string& actionId);
    // Pumps an existing explorer scan. An exhausted bounded scan is restarted
    // from the immutable observed account snapshot.
    std::string reconcilePalaceTransition(const std::string& actionId);
    std::string actionStatus(const std::string& actionId) const;

private:
    enum class QueuedDeliveryEventKind {
        NodeChanged,
        NodeStarted,
        NodeStopped,
        ConnectionChanged,
        MessageReceived,
        MessageSent,
        MessagePropagated,
        MessageError,
    };

    struct QueuedDeliveryEvent {
        QueuedDeliveryEventKind kind = QueuedDeliveryEventKind::NodeChanged;
        std::uint64_t roomTransitionGeneration = 0U;
        bool succeeded = false;
        std::string requestId;
        std::string contentTopic;
        std::string connectionStatus;
        std::vector<std::uint8_t> payload;
    };

    enum class PendingDeliveryModuleEvent {
        Sent,
        Propagated,
        Error,
    };

    enum class StorageMvpTransferPurpose {
        PublicationUpload = 0,
        PublicationVerification = 1,
        NetworkFetch = 2,
        RetentionVerification = 3,
    };

    struct StorageMvpTransfer {
        StorageMvpTransferPurpose purpose =
            StorageMvpTransferPurpose::NetworkFetch;
        std::string objectId;
        std::string path;
        std::uint64_t retentionRound = 0U;
        // Network peer fetches can race DHT provider records and connect()
        // completion; retry a few times before degrading the MVP bundle.
        std::uint32_t attempt = 0U;
    };

    // Storage returns a canonical dataset/manifest CID for uploaded bytes. That
    // CID's multihash is the manifest digest, not the PNG content digest. Core
    // therefore re-reads the already-verified local PNG for the authoring
    // handle and only then binds the returned Storage CID.
    struct AssetPublicationVerification {
        std::string handle;
        std::string cid;
    };

    struct PalaceLezPendingFinality {
        std::string actionId;
        std::string transactionHash;
        palace::PalaceLezTrackedTransaction transaction;
        palace::PalaceLezStableAccountBatchV1 stableAccounts;
        palace::PalaceLezExplorerTransactionExpectationV1 expectation;
        palace::PalaceLezExplorerFinalitySession session;
    };

    struct PalaceLezFinalizedEvidence {
        std::string actionId;
        palace::PalaceLezTrackedTransaction transaction;
        palace::PalaceLezStableAccountBatchV1 stableAccounts;
        palace::PalaceLezExplorerFinalityCertificateV1 certificate;
    };

    struct PalaceLezOpenHistory {
        std::string palaceUri;
        std::string palaceIdHex;
        palace::PalaceLezExplorerHistoryExpectationV1 expectation;
        palace::PalaceLezExplorerHistorySession session;
        bool authorityApplied = false;
        std::string reason = "not-started";
    };

    struct PalaceLezPendingSubmissionRecovery {
        std::string key;
        palace::PalaceLezSubmissionRecoveryExpectationV1
            expectation;
        palace::PalaceLezExplorerSubmissionRecoverySession
            session;
        std::string reason = "not-started";
    };

    struct PalaceVmTurn {
        std::string phase = "prepared";
        std::string reason = "prepared";
        std::string actionId;
        std::string palaceIdHex;
        std::string rootAccountIdHex;
        std::string callerAccountIdHex;
        std::string programIdHex;
        std::string grantIdHex;
        std::string sharedStateIdHex;
        std::string roomIdHex;
        std::string script;
        std::string scriptBundleCid;
        std::string roomEpoch;
        std::string trigger;
        std::string priorState;
        std::string allowedRooms;
        std::string resultingState;
        std::string expectedStateRootHex;
        std::string provisionalReceipt;
        std::string finalizedReceipt;
        std::string navigateRoom;
        std::uint64_t stateRevision = 0U;
        std::uint64_t instructionBudget = 0U;
        bool roomLocked = false;
        bool canMutateSharedState = false;
        bool navigationApplied = false;
    };

    bool persistProjection();
    void persistActionJournal();
    void persistDeliverySessionLocked();
    void registerDeliveryCallbacks();
    void enqueueDeliveryEvent(QueuedDeliveryEvent event);
    void drainDeliveryEvents();
    void executeDeliveryCommands(
        const std::vector<palace::DeliverySessionCommand>& commands,
        std::uint64_t generation);
    void executeDeliveryCommand(
        const palace::DeliverySessionCommand& command);
    void executeDeliveryRecoveryActions(
        const std::vector<palace::DeliveryRecoveryAction>& actions,
        std::uint64_t generation);
    void executeDeliveryRecoveryAction(
        const palace::DeliveryRecoveryAction& action);
    void deliveryNodeStarted(bool succeeded);
    void deliveryNodeStopped(bool succeeded);
    void deliveryConnectionChanged(const std::string& status);
    void deliveryMessageReceived(const std::string& contentTopic,
                                 const std::vector<std::uint8_t>& payload);
    void deliveryModuleEvent(const std::string& moduleRequestId,
                             PendingDeliveryModuleEvent event);
    void applyDeliveryModuleEventLocked(
        const std::string& logicalRequestId,
        const std::string& moduleRequestId,
        PendingDeliveryModuleEvent event);
    std::string announceDeliveryPresence();
    void refreshDeliveryPresenceIfDue();
    void flushDeliveryOutbox();
    std::string publishDelivery(palace::DeliveryKind kind,
                                const std::string& payload,
                                std::int64_t lifetimeSeconds);
    std::string nextDeliveryRequestIdLocked(const std::string& prefix);
    void registerStorageCallbacks();
    void storageNodeChanged(const std::string& payload);
    void storageUploadFinished(const std::string& payload);
    void storageDownloadFinished(const std::string& payload);
    void drainStorageCallbacks();
    void executeStorageCommands(
        const std::vector<palace::StorageModuleCommand>& commands);
    palace::StorageModuleSessionTransition executeStorageCommand(
        const palace::StorageModuleCommand& command);
    void applyStorageTransition(
        palace::StorageModuleSessionTransition transition,
        std::deque<palace::StorageModuleCommand>& commands);
    void applyStorageTerminal(
        const palace::StorageTransferTerminal& terminal);
    void finishDownloadedAsset(
        const palace::PendingStorageAsset& pending);
    bool syncLezWalletToCurrent(std::string& reason);
    std::optional<palace::PalaceLezTrackedTransaction>
    trackedLezTransaction(const std::string& transactionHash) const;
    bool readStableLezAccounts(
        const palace::PalaceLezTrackedTransaction& transaction,
        palace::PalaceLezStableAccountBatchV1& stableAccounts,
        std::string& reason);
    bool startPalaceFinality(
        const std::string& actionId,
        const palace::PalaceLezTrackedTransaction& transaction,
        palace::PalaceLezStableAccountBatchV1 stableAccounts,
        std::string& reason);
    void pumpPalaceFinality();
    void acceptPalaceFinalityResponse(
        const std::string& actionId,
        const std::string& transactionHash,
        const palace::PalaceLezExplorerHttpResponseV1& response);
    bool persistPalaceFinalityCertificate(std::string& reason);
    bool persistFinalizedAuthorityState(
        const palace::PalaceLezTrackedTransaction& transaction,
        const palace::PalaceLezExplorerFinalityCertificateV1&
            certificate,
        const palace::PalaceLezStableAccountBatchV1& stableAccounts,
        std::string& reason);
    bool finalizedAuthorityIncludes(
        const palace::PalaceLezTrackedTransaction& transaction) const;
    void pumpPalaceHistory();
    void acceptPalaceHistoryResponse(
        const std::string& palaceIdHex,
        const palace::PalaceLezExplorerHttpResponseV1& response);
    palace::DeliveryIdentityRegistrationRecoveryV1
    lookupFinalizedLezSubmission(
        const std::string& key,
        const palace::PalaceLezSubmissionRecoveryExpectationV1&
            expectation);
    void pumpLezSubmissionRecovery();
    void acceptLezSubmissionRecoveryResponse(
        const std::string& key,
        const palace::PalaceLezExplorerHttpResponseV1& response);
    bool recoverPalaceSubmissionIntent(
        const palace::PalaceLezSubmissionIntentV1& intent,
        std::string& reason);
    std::string submitHumanModeration(
        palace::PalaceHumanModerationTargetV1 targetKind,
        const std::string& selectedTarget);
    bool repairTrackedPalaceSubmissionIntent(
        const palace::PalaceLezTransactionPlanV3& requestedPlan,
        std::string& transactionHash,
        std::string& reason);
    bool commitTrackedPalaceSubmissionIntent(
        const std::string& actionId,
        const std::string& transactionHash,
        std::string& reason);
    bool completePalaceHistoryRebuild(std::string& reason);
    bool readStableLezAccountIds(
        const std::vector<std::string>& accountIdsHex,
        std::int64_t minimumFinalizedHeight,
        palace::PalaceLezStableAccountBatchV1& stableAccounts,
        std::string& reason);
    bool loadPalaceVmTurn();
    bool validPalaceVmTurn(
        const PalaceVmTurn& turn) const;
    bool savePalaceVmTurn(const PalaceVmTurn& turn) const;
    bool preparePalaceVmTurn(
        PalaceVmTurn& turn,
        std::string& reason) const;
    bool ensurePalaceVmActionQueued(
        const PalaceVmTurn& turn,
        std::string& reason);
    bool executePreparedPalaceVmTurn(std::string& reason);
    bool submitPalaceVmTurn(std::string& reason);
    bool recoverFinalizedPalaceVmTurn(
        std::string& reason);
    bool promoteFinalizedPalaceVmTurn(
        const std::string& actionId,
        std::string& reason);
    std::map<std::string, std::string>
    productionDeliveryAllowedProps() const;
    void refreshDeliveryAllowedProps();
    std::string palaceFinalityReason(
        const std::string& actionId) const;
    bool initializeStorageMvpBundle();
    bool promoteStorageMvpBackgrounds();
    std::optional<palace::PalaceStorageMvpCatalogBindingV1>
    storageMvpCatalogBinding() const;
    void clearStorageMvpRuntimeState();
    bool restoreStorageMvpCatalog();
    bool beginStorageMvpFetch(std::string& reason);
    void startRestoredStorageMvpFetchIfReady();
    void persistStorageMvpCatalogIfFinalized();
    bool writeStorageMvpArtifact(
        const palace::PalaceStorageMvpArtifactV1& artifact,
        std::string& path) const;
    bool readStorageDownload(
        const std::string& path,
        std::uint64_t maximumBytes,
        std::string& bytes) const;
    std::string storageDownloadPath(
        const std::string& operationId) const;
    bool verifyAssetPublication(
        const AssetPublicationVerification& verification);
    void scheduleStorageMvpPublications();
    bool startStorageMvpPublication(
        const palace::PalaceStorageMvpArtifactV1& artifact);
    bool startStorageMvpCatalogDownload(
        const palace::StorageCatalogOperation& operation,
        bool localOnly,
        StorageMvpTransferPurpose purpose,
        std::uint32_t attempt = 0U);
    // Starts at most one outstanding MVP network/cache fetch so
    // fetchMvpStorageBundle can return ok;state=fetching without waiting for
    // every downloadToUrlV2 provider lookup to finish.
    bool scheduleNextStorageMvpFetch();
    // Completes PublicationVerification from bytes already held in the MVP
    // artifact after a successful local upload. Avoids blocking the publish
    // dispatch on storage_module.downloadToUrlV2 (localOnly), which can hang
    // on provider discovery even immediately after uploadUrl succeeds.
    bool completeStorageMvpPublicationFromKnownBytes(
        const std::string& objectId,
        const std::string& cid);
    void applyStorageMvpTerminal(
        const palace::StorageTransferTerminal& terminal);
    // Observer and Delivery callbacks use these internally. They remain hidden
    // from UI modules so a local caller cannot forge a durable lifecycle stage.
    std::string markSubmittedToLez(const std::string& actionId,
                                   const std::string& transactionHash);
    std::string markObserved(const std::string& actionId);
    std::string markFinalized(const std::string& actionId);
    std::string markDeliveryPublished(const std::string& actionId);

protected:
    void onContextReady() override;

private:
    palace::ActionJournal m_actionJournal;
    std::unique_ptr<palace::ActionJournalStore> m_actionJournalStore;
    palace::PalaceProjection m_projection;
    palace::AuthorityProjection m_deliveryAuthority;
    std::unique_ptr<palace::PalaceDeliverySession> m_deliverySession;
    std::unique_ptr<palace::DeliverySessionStore> m_deliverySessionStore;
#if defined(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
    palace::DeliveryAcceptanceIdentity m_deliveryAcceptanceIdentity;
#endif
    palace::PalaceDeliveryIdentity m_deliveryIdentity;
    std::unique_ptr<palace::PalaceDeliveryIdentityRegistration>
        m_deliveryIdentityRegistration;
    const palace::DeliverySignatureSigner* m_deliverySigner = nullptr;
    palace::Ed25519EnvelopeVerifier m_deliveryVerifier;
    palace::DeliveryRequestCorrelation m_deliveryRequestCorrelation;
    palace::DeliveryRecoveryCoordinator m_deliveryRecovery;
    palace::AssetAuthoringCatalog m_assetAuthoring;
    std::unique_ptr<palace::ProjectionStore> m_projectionStore;
    std::unique_ptr<palace::VerifiedAssetStore> m_verifiedAssetStore;
    palace::PalaceLezTransactionCoordinator m_lezCoordinator;
    std::unique_ptr<palace::PalaceLezCoordinatorStore>
        m_lezCoordinatorStore;
    std::unique_ptr<palace::PalaceLezSubmissionIntentStore>
        m_lezSubmissionIntentStore;
    std::optional<palace::PalaceLezSubmissionIntentV1>
        m_lezSubmissionIntent;
    std::unique_ptr<palace::PalaceLezAuthorityBundleStore>
        m_lezAuthorityBundleStore;
    palace::PalaceLezFinalizedAuthorityBundleV1
        m_lezAuthorityBundle;
    std::shared_ptr<palace::CallbackLifetime<PalaceCoreImpl>>
        m_callbackLifetime;
    std::unique_ptr<palace::PalaceLezExplorerQtTransport>
        m_lezFinalityTransport;
    std::optional<PalaceLezPendingFinality> m_lezPendingFinality;
    std::optional<PalaceLezFinalizedEvidence>
        m_lezLatestFinalizedEvidence;
    std::optional<PalaceLezOpenHistory> m_lezOpenHistory;
    std::optional<PalaceLezPendingSubmissionRecovery>
        m_lezPendingSubmissionRecovery;
    std::optional<PalaceVmTurn> m_palaceVmTurn;
    std::map<std::string, std::string> m_lezFinalityReasons;
    palace::StorageAssetQueue m_storageAssets;
    palace::PalaceStorageModuleSession m_storageSession;
    palace::PalaceStorageCatalogSession m_storageCatalog;
    palace::PalaceStorageMvpBundle m_storageMvpBundle;
    std::unique_ptr<palace::PalaceStorageMvpCatalogStore>
        m_storageMvpCatalogStore;
    std::map<std::string, StorageMvpTransfer> m_storageMvpTransfers;
    std::set<std::string> m_storageMvpScheduledPublications;
    std::set<std::string> m_storageMvpFetchedObjects;
    std::set<std::string> m_storageMvpRetainedObjects;
    std::map<std::string, std::string>
        m_storageMvpPendingRoomBackgrounds;
    std::map<std::string, std::string>
        m_storageMvpResolvedRoomBackgrounds;
    std::map<std::string, std::string> m_storageMvpFailures;
    std::map<std::string, std::string> m_assetStatus;
    std::map<std::string, std::string> m_storagePublicationByOperation;
    std::map<std::string, std::string> m_publicationStatus;
    std::map<std::string, std::vector<PendingDeliveryModuleEvent>>
        m_earlyDeliveryModuleEvents;
    std::deque<QueuedDeliveryEvent> m_deliveryEvents;
    mutable std::mutex m_deliveryMutex;
    std::string m_deliveryNodeConfig;
    std::string m_deliveryProfile;
    std::string m_deliveryDisplayName;
    std::int64_t m_deliveryKeyEpoch = 0;
    std::string m_lezWalletState = "closed";
    std::string m_lezSyncState = "not-started";
    std::string m_lezAuthorityState = "missing";
    std::string m_palaceVmStoreState = "missing";
    std::int64_t m_lezCurrentHeight = -1;
    std::int64_t m_lezSyncedHeight = -1;
    std::uint64_t m_nextDeliveryRequestId = 0;
    std::int64_t m_deliveryPresenceRefreshAt = 0;
    std::size_t m_deliverySendCallsInFlight = 0;
    palace::PalaceRoomTransitionNativeCallGate
        m_deliveryNativeCallGate;
    std::uint64_t m_deliveryAcceptedMessageCount = 0;
    std::uint64_t m_deliveryRejectedMessageCount = 0;
    std::array<
        std::uint64_t,
        static_cast<std::size_t>(palace::DeliveryRejectionClass::Count)>
        m_deliveryRejectionCounts{};
    bool m_deliveryCallbacksRegistered = false;
    bool m_deliveryCallbackRegistrationAttempted = false;
    bool m_deliveryDrainingEvents = false;
    bool m_deliveryEventOverflow = false;
    bool m_deliveryNodeCreatePending = false;
    bool m_deliveryNodeCreated = false;
    bool m_deliveryNodeRunning = false;
    std::atomic_bool m_roomTransitionHealthy{false};
    palace::PalaceRoomTransitionParticipantWriteGate
        m_roomTransitionParticipantWriteGate;
    bool m_lezReady = false;
    bool m_lezWalletOpened = false;
    bool m_lezAuthorityReady = false;
    bool m_lezCoordinatorStoreHealthy = true;
    std::string m_storageInitializationConfig;
    std::string m_storageHolderAccountId;
    std::string m_storageMvpMode = "idle";
    std::optional<palace::PalaceStorageMvpFetchSource>
        m_storageMvpFetchSource;
    std::size_t m_storageMvpNativeAvailableCount = 0U;
    std::size_t m_storageMvpNativeTotalCount = 0U;
    std::uint64_t m_nextStoragePublicationId = 0;
    std::uint64_t m_storageRetentionRound = 0;
    bool m_storageRetentionInProgress = false;
    // A valid catalog from a different finalized authority snapshot must not
    // revive local authoring previews while recovery waits for a new graph.
    bool m_storageMvpCatalogStale = false;
    bool m_storageCallbacksRegistered = false;
    bool m_storageCallbackRegistrationAttempted = false;
};
