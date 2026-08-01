#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "palace_authority.h"
#include "palace_identity.h"

namespace palace {

struct DeliveryAcceptanceIdentity {
    std::string userId;
    std::string displayName;
    std::int64_t keyEpoch = 0;
    Ed25519KeyPair signer;
};

// Gate-2 test bootstrap. Seeds are public RFC 8032 vectors, not user keys.
bool bootstrapDeliveryAcceptanceAuthority(AuthorityProjection& authority);
bool deliveryAcceptanceIdentity(const std::string& userId,
                                DeliveryAcceptanceIdentity& identity);
std::int64_t deliveryAcceptanceRoomEpoch(const std::string& roomId);
std::map<std::string, std::string> deliveryAcceptanceAllowedProps();

} // namespace palace
