#pragma once

#include <map>
#include <string>

namespace palace {

enum class DurableActionStage {
    LocalDraft,
    Queued,
    SubmittedToLez,
    Observed,
    Finalized,
    Rejected,
    Expired,
    Orphaned,
};

struct ActionStatus {
    DurableActionStage durableStage = DurableActionStage::LocalDraft;
    bool deliveryPublished = false;
};

// Owns the user-visible durable-action lifecycle. Delivery publication is an
// observation only and deliberately cannot advance durable finality.
class ActionJournal {
public:
    bool createDraft(const std::string& actionId);
    bool queue(const std::string& actionId);
    bool markSubmittedToLez(const std::string& actionId);
    bool markObserved(const std::string& actionId);
    bool markFinalized(const std::string& actionId);
    bool markRejected(const std::string& actionId);
    bool markExpired(const std::string& actionId);
    bool markOrphaned(const std::string& actionId);
    bool markDeliveryPublished(const std::string& actionId);
    ActionStatus status(const std::string& actionId) const;

private:
    std::map<std::string, ActionStatus> m_actions;
};

std::string actionStatusName(DurableActionStage stage);
std::string canonicalActionStatus(const ActionStatus& status);

} // namespace palace
