#pragma once

#include <map>
#include <set>
#include <string>

namespace palace {

enum class ObjectAvailability {
    Missing,
    Degraded,
    RedundantlyRetained,
};

std::string objectAvailabilityName(ObjectAvailability availability);

// Tracks participant-held copies named by immutable CID. It never treats the
// creator as a special server: any independent holder may satisfy recovery.
class ObjectRetentionIndex {
public:
    bool declareActive(const std::string& cid);
    bool retain(const std::string& cid, const std::string& participantId);
    bool removeRetainer(const std::string& cid, const std::string& participantId);

    ObjectAvailability availability(const std::string& cid) const;
    bool canResolve(const std::string& cid) const;
    bool meetsActivationPolicy(const std::string& cid) const;

private:
    std::map<std::string, std::set<std::string>> m_retainers;
};

} // namespace palace
