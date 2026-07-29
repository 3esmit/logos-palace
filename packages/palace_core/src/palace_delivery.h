#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

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

class DeliverySignatureSigner {
public:
    virtual ~DeliverySignatureSigner() = default;
    virtual std::string publicKey() const = 0;
    virtual std::string sign(const std::string& canonicalEnvelope) const = 0;
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
    std::size_t maxTrackedSenders = 512;
    std::map<std::string, std::string> allowedProps;
    const AuthorityProjection* authority = nullptr;
};

struct DeliveryValidation {
    bool accepted = false;
    std::string reason;
};

struct DeliveryEnvelopeDecode {
    bool accepted = false;
    std::string reason;
    PalaceDeliveryEnvelopeV1 envelope;
};

struct DeliveryPublication {
    bool accepted = false;
    std::string reason;
    std::string contentTopic;
    std::vector<std::uint8_t> payload;
    std::uint64_t sequence = 0;
};

struct DeliverySequenceStateV1 {
    std::map<std::string, std::uint64_t> lastSequence;
};

std::string canonicalDeliveryEnvelope(const PalaceDeliveryEnvelopeV1& envelope);
std::string encodeDeliveryEnvelope(const PalaceDeliveryEnvelopeV1& envelope);
DeliveryEnvelopeDecode decodeDeliveryEnvelope(const std::string& encoded);
std::string deriveRoomTopic(const std::string& networkId,
                            const std::string& palaceId,
                            const std::string& roomId,
                            std::int64_t roomEpoch);

// Stateful ingress boundary. Advances replay/rate state only after every
// schema, authority, signature, bounds, and topic check succeeds.
class DeliveryIngress {
public:
    DeliveryValidation validate(const std::string& contentTopic,
                                const PalaceDeliveryEnvelopeV1& envelope,
                                const DeliveryPolicy& policy,
                                const DeliverySignatureVerifier& verifier) const;
    DeliveryValidation commitValidated(
        const PalaceDeliveryEnvelopeV1& envelope,
        const DeliveryPolicy& policy);
    DeliveryValidation receive(const std::string& contentTopic,
                               const PalaceDeliveryEnvelopeV1& envelope,
                               const DeliveryPolicy& policy,
                               const DeliverySignatureVerifier& verifier);

    std::uint64_t lastSequenceFor(const std::string& senderUserId,
                                  std::int64_t senderKeyEpoch) const;
    DeliverySequenceStateV1 sequenceState() const;
    bool restoreSequenceState(const DeliverySequenceStateV1& state,
                              std::size_t maxTrackedSenders);

private:
    std::map<std::string, std::uint64_t> m_lastSequence;
    std::map<std::string, std::int64_t> m_lastMotionAt;
};

// Egress has the same policy and signature checks as ingress before any bytes
// reach Delivery. It increments a sequence only after this preflight accepts.
class DeliveryEgress {
public:
    DeliveryPublication prepare(const DeliveryPolicy& policy,
                                const std::string& senderUserId,
                                std::int64_t senderKeyEpoch,
                                DeliveryKind kind,
                                const std::string& payload,
                                std::int64_t createdAt,
                                std::int64_t lifetimeSeconds,
                                const DeliverySignatureSigner& signer,
                                const DeliverySignatureVerifier& verifier);

    DeliverySequenceStateV1 sequenceState() const;
    bool restoreSequenceState(const DeliverySequenceStateV1& state,
                              std::size_t maxTrackedSenders);

private:
    DeliveryIngress m_preflight;
    std::map<std::string, std::uint64_t> m_lastSequence;
};

} // namespace palace
