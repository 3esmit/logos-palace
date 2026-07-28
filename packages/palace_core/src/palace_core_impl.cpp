#include "palace_core_impl.h"

#include "logos_sdk.h"

#include "palace_delivery.h"

namespace {

std::string result(bool changed, const palace::ActionStatus& status)
{
    return std::string("changed=") + (changed ? "1" : "0")
        + ";" + palace::canonicalActionStatus(status);
}

} // namespace

void PalaceCoreImpl::onContextReady()
{
    if (instancePersistencePath().empty())
        return;
    m_projectionStore = std::make_unique<palace::ProjectionStore>(instancePersistencePath());
    m_verifiedAssetStore = std::make_unique<palace::VerifiedAssetStore>(instancePersistencePath());
    if (!m_projectionStore->load(m_projection)) {
        m_projection.setSyncHealth(palace::SyncHealth::Degraded);
        persistProjection();
    }
}

void PalaceCoreImpl::persistProjection()
{
    if (m_projectionStore)
        m_projectionStore->save(m_projection);
}

std::string PalaceCoreImpl::enterRoom(const std::string& roomId)
{
    if (!m_projection.enterRoom(roomId))
        return "rejected=unknown-room";
    persistProjection();
    return "ok;room=" + m_projection.currentRoomId()
        + ";title=" + m_projection.currentRoomTitle()
        + ";sync=" + palace::syncHealthName(m_projection.syncHealth());
}

std::string PalaceCoreImpl::useSpot(const std::string& spotId)
{
    if (spotId != "door")
        return "rejected=unknown-spot";
    if (!isContextReady())
        return "rejected=vm-not-ready";

    const std::string receipt = modules().palace_vm.executeTurn(
        "ON SELECT door\nSAY Welcome to the Lounge\nGOTOROOM lounge\n",
        "fixture-script-bundle-v1",
        "1",
        "SELECT:door",
        "door_open=0",
        "atrium,lounge",
        false,
        false,
        8);
    if (receipt.rfind("accepted=1;", 0) != 0
        || receipt.find("15:navigate:lounge") == std::string::npos) {
        return "rejected=vm-turn;" + receipt;
    }
    return enterRoom("lounge") + ";vm_receipt=" + receipt;
}

std::string PalaceCoreImpl::startDelivery(const std::string& nodeConfig)
{
    if (!isContextReady() || nodeConfig.empty())
        return "rejected=delivery-not-ready-or-empty-config";
    if (!m_deliveryNodeCreated) {
        const StdLogosResult created = modules().delivery_module.createNode(nodeConfig);
        if (!created.success)
            return "rejected=delivery-create-node;" + created.error;
        m_deliveryNodeCreated = true;
    }
    const StdLogosResult started = modules().delivery_module.start();
    if (!started.success)
        return "rejected=delivery-start;" + started.error;
    return "ok;delivery=start-requested";
}

std::string PalaceCoreImpl::subscribeRoom(const std::string& networkId,
                                          const std::string& palaceId,
                                          const std::string& roomId,
                                          std::int64_t roomEpoch)
{
    if (!isContextReady() || !m_deliveryNodeCreated || roomEpoch < 0)
        return "rejected=delivery-node-not-created";
    const std::string topic = palace::deriveRoomTopic(networkId, palaceId, roomId, roomEpoch);
    const StdLogosResult subscribed = modules().delivery_module.subscribe(topic);
    if (!subscribed.success)
        return "rejected=delivery-subscribe;" + subscribed.error;
    return "ok;topic=" + topic;
}

std::string PalaceCoreImpl::roomTitle() const
{
    return m_projection.currentRoomTitle();
}

std::string PalaceCoreImpl::syncHealth() const
{
    return palace::syncHealthName(m_projection.syncHealth());
}

std::string PalaceCoreImpl::localProjection() const
{
    return m_projection.canonicalLocalState();
}

std::string PalaceCoreImpl::submitIntent(const std::string& actionId)
{
    const bool changed = m_actionJournal.createDraft(actionId) && m_actionJournal.queue(actionId);
    return result(changed, m_actionJournal.status(actionId));
}

std::string PalaceCoreImpl::markSubmittedToLez(const std::string& actionId)
{
    const bool changed = m_actionJournal.markSubmittedToLez(actionId);
    return result(changed, m_actionJournal.status(actionId));
}

std::string PalaceCoreImpl::markObserved(const std::string& actionId)
{
    const bool changed = m_actionJournal.markObserved(actionId);
    return result(changed, m_actionJournal.status(actionId));
}

std::string PalaceCoreImpl::markFinalized(const std::string& actionId)
{
    const bool changed = m_actionJournal.markFinalized(actionId);
    return result(changed, m_actionJournal.status(actionId));
}

std::string PalaceCoreImpl::markDeliveryPublished(const std::string& actionId)
{
    const bool changed = m_actionJournal.markDeliveryPublished(actionId);
    return result(changed, m_actionJournal.status(actionId));
}

std::string PalaceCoreImpl::actionStatus(const std::string& actionId) const
{
    return palace::canonicalActionStatus(m_actionJournal.status(actionId));
}
