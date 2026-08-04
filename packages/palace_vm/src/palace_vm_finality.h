#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace palace {

struct PalaceVmTurnInput {
    std::string actionId;
    std::string script;
    std::string scriptBundleCid;
    std::string roomEpoch;
    std::string trigger;
    std::string priorState;
    std::string allowedRooms;
    bool roomLocked = false;
    bool canMutateSharedState = false;
    std::uint64_t instructionBudget = 0U;
};

enum class PalaceVmFinalityStatus {
    Pending,
    Promoted,
};

struct PalaceVmFinalityRecord {
    std::string actionId;
    std::string turnInputDigest;
    std::string provisionalReceipt;
    PalaceVmFinalityStatus status = PalaceVmFinalityStatus::Pending;
    std::string finalizedReceipt;
};

struct PalaceVmFinalityResult {
    bool accepted = false;
    bool changed = false;
    std::string reason;
};

std::string palaceVmTurnInputDigest(const PalaceVmTurnInput& input);
bool isPalaceVmActionId(const std::string& actionId);
const char* palaceVmFinalityStatusName(PalaceVmFinalityStatus status);

// Tracks the exact provisional turn that may later be promoted. It never
// decides finality: Palace Core owns that decision and invokes promotion only
// after its LEZ observer has accepted finality.
class PalaceVmFinalityJournal {
public:
    PalaceVmFinalityResult recordProvisional(
        const PalaceVmTurnInput& input,
        const std::string& provisionalReceipt);
    PalaceVmFinalityResult validatePromotion(
        const PalaceVmTurnInput& input,
        const std::string& provisionalReceipt) const;
    PalaceVmFinalityResult recordPromotion(
        const PalaceVmTurnInput& input,
        const std::string& provisionalReceipt,
        const std::string& finalizedReceipt);

    const PalaceVmFinalityRecord* find(const std::string& actionId) const;
    std::string statusEvidence(const std::string& actionId) const;

    std::string snapshot() const;
    bool restore(const std::string& snapshot);

private:
    std::map<std::string, PalaceVmFinalityRecord> m_records;
};

class PalaceVmFinalityStore {
public:
    explicit PalaceVmFinalityStore(std::string instancePersistencePath);

    bool exists() const;
    bool load(PalaceVmFinalityJournal& journal) const;
    bool save(const PalaceVmFinalityJournal& journal) const;

private:
    std::string m_instancePersistencePath;
};

} // namespace palace
