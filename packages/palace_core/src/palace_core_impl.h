#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "logos_module_context.h"

#include "palace_action_journal.h"
#include "palace_projection.h"
#include "palace_storage.h"
#include "palace_verified_asset_store.h"

// Palace Core is the sole future owner of LEZ, Delivery, Storage, VM, local
// projection, and recovery composition. This initial API exposes the durable
// action seam; UI code never calls upstream modules directly.
class PalaceCoreImpl : public LogosModuleContext {
public:
    std::string enterRoom(const std::string& roomId);
    std::string useSpot(const std::string& spotId);
    std::string startDelivery(const std::string& nodeConfig);
    std::string subscribeRoom(const std::string& networkId,
                              const std::string& palaceId,
                              const std::string& roomId,
                              std::int64_t roomEpoch);
    std::string startStorage(const std::string& nodeConfig);
    std::string fetchPngDerivative(const std::string& sourceCid,
                                   const std::string& derivativeCid,
                                   std::uint64_t byteLength,
                                   const std::string& contentSha256,
                                   std::uint32_t width,
                                   std::uint32_t height);
    std::string assetStatus(const std::string& derivativeCid) const;
    std::string publishVerifiedPng(const std::string& handle);
    std::string publicationStatus(const std::string& handle) const;
    std::string roomTitle() const;
    std::string syncHealth() const;
    std::string localProjection() const;
    std::string submitIntent(const std::string& actionId);
    // Submits a queued transition through LEZ. `transitionJson.kind` is one of
    // bind_delivery_key, delegate_moderator, revoke_moderator, ban_user,
    // ban_asset, set_room_locked, publish_manifest, or
    // set_shared_spot_revision. Numeric key_epoch and revision fields are
    // decimal strings, preventing JSON number precision loss.
    std::string submitPalaceTransition(const std::string& actionId,
                                       const std::string& stateAccountIdHex,
                                       const std::string& callerAccountIdHex,
                                       const std::string& programIdHex,
                                       const std::string& transitionJson);
    std::string actionStatus(const std::string& actionId) const;

private:
    void persistProjection();
    void persistActionJournal();
    void storageStartFinished(const std::string& payload);
    void storageUploadFinished(const std::string& payload);
    void storageDownloadFinished(const std::string& payload);
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
    std::unique_ptr<palace::ProjectionStore> m_projectionStore;
    std::unique_ptr<palace::VerifiedAssetStore> m_verifiedAssetStore;
    palace::StorageAssetQueue m_storageAssets;
    std::map<std::string, std::string> m_assetStatus;
    std::map<std::string, std::string> m_storagePublicationBySession;
    std::map<std::string, std::string> m_publicationStatus;
    bool m_deliveryNodeCreated = false;
    bool m_storageNodeCreated = false;
    bool m_storageStartRequested = false;
    bool m_storageRunning = false;
};
