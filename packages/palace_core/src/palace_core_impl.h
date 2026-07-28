#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "logos_module_context.h"

#include "palace_action_journal.h"
#include "palace_projection.h"

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

protected:
    void onContextReady() override;

private:
    palace::ActionJournal m_actionJournal;
    palace::PalaceProjection m_projection;
    std::unique_ptr<palace::ProjectionStore> m_projectionStore;
    bool m_deliveryNodeCreated = false;
};
