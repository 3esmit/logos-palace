#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "palace_authority.h"

namespace palace {

enum class DeliveryKind {
    PresenceHello,
    PresenceBye,
    Motion,
    Speech,
    WearProp,
    RemoveProp,
    SpotIntentNotice,
    AuthorityRefreshNotice,
};

struct PalaceDeliveryEnvelopeV1 {
    std::int64_t protocolVersion = 1;
    std::string networkId;
    std::string palaceId;
    std::string roomId;
    std::int64_t roomEpoch = 0;
    DeliveryKind kind = DeliveryKind::PresenceHello;
    std::string senderUserId;
    std::int64_t senderKeyEpoch = 0;
    std::uint64_t senderSequence = 0;
    std::int64_t createdAt = 0;
    std::int64_t expiresAt = 0;
    std::string payload;
    std::string signature;
};

class DeliverySignatureVerifier {
public:
    virtual ~DeliverySignatureVerifier() = default;
    virtual bool verify(const std::string& publicKey,
                        const std::string& canonicalEnvelope,
                        const std::string& signature) const = 0;
};

struct DeliveryPolicy {
    std::string networkId;
    std::string palaceId;
    std::string roomId;
    std::int64_t roomEpoch = 0;
    std::int64_t now = 0;
    std::int64_t maxFutureSkewSeconds = 30;
    std::int64_t maxLifetimeSeconds = 300;
    std::int64_t minMotionIntervalSeconds = 0;
    std::size_t maxPayloadBytes = 1024;
    std::map<std::string, std::string> allowedProps;
    const AuthorityProjection* authority = nullptr;
};

struct DeliveryValidation {
    bool accepted = false;
    std::string reason;
};

std::string canonicalDeliveryEnvelope(const PalaceDeliveryEnvelopeV1& envelope);
std::string deriveRoomTopic(const std::string& networkId,
                            const std::string& palaceId,
                            const std::string& roomId,
                            std::int64_t roomEpoch);

// Stateful ingress boundary. Advances replay/rate state only after every
// schema, authority, signature, bounds, and topic check succeeds.
class DeliveryIngress {
public:
    DeliveryValidation receive(const std::string& contentTopic,
                               const PalaceDeliveryEnvelopeV1& envelope,
                               const DeliveryPolicy& policy,
                               const DeliverySignatureVerifier& verifier);

private:
    std::map<std::string, std::uint64_t> m_lastSequence;
    std::map<std::string, std::int64_t> m_lastMotionAt;
};

} // namespace palace
