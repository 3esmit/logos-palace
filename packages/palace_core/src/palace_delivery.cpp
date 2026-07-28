#include "palace_delivery.h"

#include "palace_sha256.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <sstream>

namespace palace {
namespace {

std::string lengthEncoded(const std::string& value)
{
    return std::to_string(value.size()) + ":" + value;
}

bool isIdentifier(const std::string& value)
{
    if (value.empty() || value.size() > 64U)
        return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '_' || character == '-';
    });
}

bool isText(const std::string& value, std::size_t limit)
{
    if (value.empty() || value.size() > limit)
        return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return character >= 0x20U && character != 0x7fU;
    });
}

bool parseMotion(const std::string& payload)
{
    std::istringstream stream(payload);
    std::string first;
    std::string second;
    std::string trailing;
    if (!std::getline(stream, first, ';') || !std::getline(stream, second, ';')
        || std::getline(stream, trailing, ';')) {
        return false;
    }
    const auto parseCoordinate = [](const std::string& field, const std::string& name) {
        if (field.rfind(name + "=", 0) != 0)
            return false;
        const std::string value = field.substr(name.size() + 1U);
        std::int64_t coordinate = 0;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), coordinate);
        return !value.empty() && result.ec == std::errc()
            && result.ptr == value.data() + value.size() && coordinate >= 0 && coordinate <= 10000;
    };
    return parseCoordinate(first, "x") && parseCoordinate(second, "y");
}

std::string senderKey(const PalaceDeliveryEnvelopeV1& envelope)
{
    return envelope.senderUserId + "@" + std::to_string(envelope.senderKeyEpoch);
}

DeliveryValidation reject(const std::string& reason)
{
    return {false, reason};
}

bool payloadAllowed(const PalaceDeliveryEnvelopeV1& envelope, const DeliveryPolicy& policy)
{
    switch (envelope.kind) {
    case DeliveryKind::PresenceHello:
        return isText(envelope.payload, 256U);
    case DeliveryKind::PresenceBye:
    case DeliveryKind::AuthorityRefreshNotice:
        return envelope.payload.empty();
    case DeliveryKind::Motion:
        return parseMotion(envelope.payload);
    case DeliveryKind::Speech:
        return isText(envelope.payload, 280U);
    case DeliveryKind::WearProp: {
        const auto found = policy.allowedProps.find(envelope.payload);
        return found != policy.allowedProps.end()
            && !policy.authority->isAssetBanned(found->second, policy.roomId);
    }
    case DeliveryKind::RemoveProp:
    case DeliveryKind::SpotIntentNotice:
        return isIdentifier(envelope.payload);
    }
    return false;
}

} // namespace

std::string canonicalDeliveryEnvelope(const PalaceDeliveryEnvelopeV1& envelope)
{
    return "version=" + std::to_string(envelope.protocolVersion)
        + ";network=" + lengthEncoded(envelope.networkId)
        + ";palace=" + lengthEncoded(envelope.palaceId)
        + ";room=" + lengthEncoded(envelope.roomId)
        + ";epoch=" + std::to_string(envelope.roomEpoch)
        + ";kind=" + std::to_string(static_cast<int>(envelope.kind))
        + ";sender=" + lengthEncoded(envelope.senderUserId)
        + ";key_epoch=" + std::to_string(envelope.senderKeyEpoch)
        + ";sequence=" + std::to_string(envelope.senderSequence)
        + ";created=" + std::to_string(envelope.createdAt)
        + ";expires=" + std::to_string(envelope.expiresAt)
        + ";payload=" + lengthEncoded(envelope.payload);
}

std::string deriveRoomTopic(const std::string& networkId,
                            const std::string& palaceId,
                            const std::string& roomId,
                            std::int64_t roomEpoch)
{
    const std::string preimage = "logos-palace-room-v1|" + lengthEncoded(networkId)
        + lengthEncoded(palaceId) + lengthEncoded(roomId) + std::to_string(roomEpoch);
    return "/logos-palace/1/room-" + crypto::sha256Hex(preimage).substr(0, 32U) + "/proto";
}

DeliveryValidation DeliveryIngress::receive(const std::string& contentTopic,
                                             const PalaceDeliveryEnvelopeV1& envelope,
                                             const DeliveryPolicy& policy,
                                             const DeliverySignatureVerifier& verifier)
{
    if (policy.authority == nullptr || policy.now <= 0 || policy.networkId.empty()
        || policy.palaceId.empty() || policy.roomId.empty()) {
        return reject("invalid-policy");
    }
    if (envelope.protocolVersion != 1 || envelope.networkId != policy.networkId
        || envelope.palaceId != policy.palaceId || envelope.roomId != policy.roomId
        || envelope.roomEpoch != policy.roomEpoch) {
        return reject("wrong-palace-room-or-epoch");
    }
    if (contentTopic != deriveRoomTopic(policy.networkId, policy.palaceId, policy.roomId,
                                        policy.roomEpoch)) {
        return reject("wrong-topic");
    }
    if (envelope.senderUserId.empty() || envelope.senderKeyEpoch < 0
        || envelope.senderSequence == 0 || envelope.payload.size() > policy.maxPayloadBytes
        || envelope.signature.empty()) {
        return reject("invalid-envelope-shape");
    }
    if (envelope.createdAt > policy.now + policy.maxFutureSkewSeconds
        || envelope.expiresAt < policy.now || envelope.expiresAt < envelope.createdAt
        || envelope.expiresAt - envelope.createdAt > policy.maxLifetimeSeconds) {
        return reject("expired-or-invalid-time");
    }
    if (policy.authority->isUserBanned(envelope.senderUserId, policy.roomId))
        return reject("sender-banned");

    const std::string publicKey = policy.authority->deliveryKeyFor(envelope.senderUserId,
                                                                     envelope.senderKeyEpoch);
    if (publicKey.empty() || !verifier.verify(publicKey, canonicalDeliveryEnvelope(envelope),
                                               envelope.signature)) {
        return reject("bad-signature-or-key-binding");
    }
    if (!payloadAllowed(envelope, policy))
        return reject("invalid-or-banned-payload");

    const std::string key = senderKey(envelope);
    const auto previous = m_lastSequence.find(key);
    if (previous != m_lastSequence.end() && envelope.senderSequence <= previous->second)
        return reject("duplicate-or-replayed-sequence");
    if (envelope.kind == DeliveryKind::Motion && policy.minMotionIntervalSeconds > 0) {
        const auto lastMotion = m_lastMotionAt.find(key);
        if (lastMotion != m_lastMotionAt.end()
            && envelope.createdAt - lastMotion->second < policy.minMotionIntervalSeconds) {
            return reject("motion-rate-exceeded");
        }
    }

    m_lastSequence[key] = envelope.senderSequence;
    if (envelope.kind == DeliveryKind::Motion)
        m_lastMotionAt[key] = envelope.createdAt;
    return {true, "accepted"};
}

} // namespace palace
