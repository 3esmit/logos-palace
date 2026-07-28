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
    std::string roomTitle() const;
    std::string syncHealth() const;
    std::string localProjection() const;
    std::string submitIntent(const std::string& actionId);
    std::string markSubmittedToLez(const std::string& actionId);
    std::string markObserved(const std::string& actionId);
    std::string markFinalized(const std::string& actionId);
    std::string markDeliveryPublished(const std::string& actionId);
    std::string actionStatus(const std::string& actionId) const;

private:
    void persistProjection();
    void storageStartFinished(const std::string& payload);
    void storageDownloadFinished(const std::string& payload);

protected:
    void onContextReady() override;

private:
    palace::ActionJournal m_actionJournal;
    palace::PalaceProjection m_projection;
    std::unique_ptr<palace::ProjectionStore> m_projectionStore;
    std::unique_ptr<palace::VerifiedAssetStore> m_verifiedAssetStore;
    palace::StorageAssetQueue m_storageAssets;
    std::map<std::string, std::string> m_assetStatus;
    bool m_deliveryNodeCreated = false;
    bool m_storageNodeCreated = false;
    bool m_storageStartRequested = false;
    bool m_storageRunning = false;
};
