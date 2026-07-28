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

    // Versioned deterministic state used only behind the core-owned durable
    // journal boundary. Failed restores leave the current journal untouched.
    std::string canonicalState() const;
    bool restoreCanonicalState(const std::string& serialized);

private:
    std::map<std::string, ActionStatus> m_actions;
};

// File boundary for private durable action state. Records are checksummed,
// flushed, atomically renamed, and parsed before replacing live journal state.
class ActionJournalStore {
public:
    explicit ActionJournalStore(std::string directory);

    bool save(const ActionJournal& journal) const;
    bool load(ActionJournal& journal) const;
    bool exists() const;

private:
    std::string m_directory;
};

std::string actionStatusName(DurableActionStage stage);
std::string canonicalActionStatus(const ActionStatus& status);

} // namespace palace
