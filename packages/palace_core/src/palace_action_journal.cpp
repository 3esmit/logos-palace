#include "palace_action_journal.h"

namespace palace {

bool ActionJournal::createDraft(const std::string& actionId)
{
    if (actionId.empty() || m_actions.find(actionId) != m_actions.end())
        return false;
    m_actions.emplace(actionId, ActionStatus{});
    return true;
}

bool ActionJournal::queue(const std::string& actionId)
{
    const auto found = m_actions.find(actionId);
    if (found == m_actions.end() || found->second.durableStage != DurableActionStage::LocalDraft)
        return false;
    found->second.durableStage = DurableActionStage::Queued;
    return true;
}

bool ActionJournal::markSubmittedToLez(const std::string& actionId)
{
    const auto found = m_actions.find(actionId);
    if (found == m_actions.end() || found->second.durableStage != DurableActionStage::Queued)
        return false;
    found->second.durableStage = DurableActionStage::SubmittedToLez;
    return true;
}

bool ActionJournal::markObserved(const std::string& actionId)
{
    const auto found = m_actions.find(actionId);
    if (found == m_actions.end()
        || found->second.durableStage != DurableActionStage::SubmittedToLez) {
        return false;
    }
    found->second.durableStage = DurableActionStage::Observed;
    return true;
}

bool ActionJournal::markFinalized(const std::string& actionId)
{
    const auto found = m_actions.find(actionId);
    if (found == m_actions.end()
        || found->second.durableStage != DurableActionStage::Observed) {
        return false;
    }
    found->second.durableStage = DurableActionStage::Finalized;
    return true;
}

bool ActionJournal::markRejected(const std::string& actionId)
{
    const auto found = m_actions.find(actionId);
    if (found == m_actions.end()
        || (found->second.durableStage != DurableActionStage::Queued
            && found->second.durableStage != DurableActionStage::SubmittedToLez)) {
        return false;
    }
    found->second.durableStage = DurableActionStage::Rejected;
    return true;
}

bool ActionJournal::markExpired(const std::string& actionId)
{
    const auto found = m_actions.find(actionId);
    if (found == m_actions.end()
        || (found->second.durableStage != DurableActionStage::Queued
            && found->second.durableStage != DurableActionStage::SubmittedToLez
            && found->second.durableStage != DurableActionStage::Observed)) {
        return false;
    }
    found->second.durableStage = DurableActionStage::Expired;
    return true;
}

bool ActionJournal::markOrphaned(const std::string& actionId)
{
    const auto found = m_actions.find(actionId);
    if (found == m_actions.end()
        || (found->second.durableStage != DurableActionStage::SubmittedToLez
            && found->second.durableStage != DurableActionStage::Observed)) {
        return false;
    }
    found->second.durableStage = DurableActionStage::Orphaned;
    return true;
}

bool ActionJournal::markDeliveryPublished(const std::string& actionId)
{
    const auto found = m_actions.find(actionId);
    if (found == m_actions.end() || found->second.durableStage == DurableActionStage::LocalDraft)
        return false;
    found->second.deliveryPublished = true;
    return true;
}

ActionStatus ActionJournal::status(const std::string& actionId) const
{
    const auto found = m_actions.find(actionId);
    return found == m_actions.end() ? ActionStatus{} : found->second;
}

std::string actionStatusName(DurableActionStage stage)
{
    switch (stage) {
    case DurableActionStage::LocalDraft: return "local_draft";
    case DurableActionStage::Queued: return "queued";
    case DurableActionStage::SubmittedToLez: return "submitted_to_lez";
    case DurableActionStage::Observed: return "observed";
    case DurableActionStage::Finalized: return "finalized";
    case DurableActionStage::Rejected: return "rejected";
    case DurableActionStage::Expired: return "expired";
    case DurableActionStage::Orphaned: return "orphaned";
    }
    return "unknown";
}

std::string canonicalActionStatus(const ActionStatus& status)
{
    return "durable=" + actionStatusName(status.durableStage)
        + ";delivery_published=" + (status.deliveryPublished ? "1" : "0");
}

} // namespace palace
