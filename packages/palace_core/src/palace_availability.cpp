#include "palace_availability.h"

namespace palace {

std::string objectAvailabilityName(ObjectAvailability availability)
{
    switch (availability) {
    case ObjectAvailability::Missing: return "missing";
    case ObjectAvailability::Degraded: return "degraded";
    case ObjectAvailability::RedundantlyRetained: return "redundantly_retained";
    }
    return "missing";
}

bool ObjectRetentionIndex::declareActive(const std::string& cid)
{
    if (cid.empty() || m_retainers.find(cid) != m_retainers.end())
        return false;
    m_retainers.emplace(cid, std::set<std::string>{});
    return true;
}

bool ObjectRetentionIndex::retain(const std::string& cid, const std::string& participantId)
{
    const auto object = m_retainers.find(cid);
    if (object == m_retainers.end() || participantId.empty())
        return false;
    return object->second.insert(participantId).second;
}

bool ObjectRetentionIndex::removeRetainer(const std::string& cid, const std::string& participantId)
{
    const auto object = m_retainers.find(cid);
    if (object == m_retainers.end() || participantId.empty())
        return false;
    return object->second.erase(participantId) == 1U;
}

ObjectAvailability ObjectRetentionIndex::availability(const std::string& cid) const
{
    const auto object = m_retainers.find(cid);
    if (object == m_retainers.end() || object->second.empty())
        return ObjectAvailability::Missing;
    if (object->second.size() == 1U)
        return ObjectAvailability::Degraded;
    return ObjectAvailability::RedundantlyRetained;
}

bool ObjectRetentionIndex::canResolve(const std::string& cid) const
{
    return availability(cid) != ObjectAvailability::Missing;
}

bool ObjectRetentionIndex::meetsActivationPolicy(const std::string& cid) const
{
    return availability(cid) == ObjectAvailability::RedundantlyRetained;
}

} // namespace palace
