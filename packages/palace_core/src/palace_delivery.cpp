#include "palace_delivery.h"

#include "palace_sha256.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <sstream>
#include <utility>

namespace palace {
namespace {

constexpr std::size_t kMaxEncodedEnvelopeBytes = 4096U;
constexpr std::size_t kMaxLengthEncodedFieldBytes = 2048U;

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

bool isSenderStateKey(const std::string& value)
{
    if (value.empty() || value.size() > 96U)
        return false;
    const std::size_t separator = value.rfind('@');
    if (separator == std::string::npos || separator == 0U || separator + 1U >= value.size())
        return false;
    if (!isIdentifier(value.substr(0, separator)))
        return false;
    std::int64_t keyEpoch = -1;
    const char* first = value.data() + separator + 1U;
    const char* last = value.data() + value.size();
    const auto parsed = std::from_chars(first, last, keyEpoch);
    return parsed.ec == std::errc() && parsed.ptr == last && keyEpoch >= 0;
}

bool validSequenceState(const DeliverySequenceStateV1& state, std::size_t maxTrackedSenders)
{
    if (maxTrackedSenders == 0U || state.lastSequence.size() > maxTrackedSenders)
        return false;
    return std::all_of(state.lastSequence.begin(), state.lastSequence.end(),
        [](const auto& entry) {
            return isSenderStateKey(entry.first) && entry.second > 0U;
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

DeliveryEnvelopeDecode decodeReject(const std::string& reason)
{
    return {false, reason, {}};
}

bool readLiteral(const std::string& encoded, std::size_t& cursor, const std::string& literal)
{
    if (encoded.compare(cursor, literal.size(), literal) != 0)
        return false;
    cursor += literal.size();
    return true;
}

template <typename Integer>
bool readInteger(const std::string& encoded, std::size_t& cursor, Integer& value)
{
    const std::size_t delimiter = encoded.find(';', cursor);
    if (delimiter == std::string::npos || delimiter == cursor)
        return false;
    const char* first = encoded.data() + cursor;
    const char* last = encoded.data() + delimiter;
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ec != std::errc() || parsed.ptr != last)
        return false;
    cursor = delimiter + 1U;
    return true;
}

bool readLengthEncoded(const std::string& encoded, std::size_t& cursor, std::string& value)
{
    const std::size_t colon = encoded.find(':', cursor);
    if (colon == std::string::npos || colon == cursor)
        return false;
    std::size_t length = 0U;
    const char* first = encoded.data() + cursor;
    const char* last = encoded.data() + colon;
    const auto parsed = std::from_chars(first, last, length);
    if (parsed.ec != std::errc() || parsed.ptr != last || length > kMaxLengthEncodedFieldBytes)
        return false;
    cursor = colon + 1U;
    if (length > encoded.size() - cursor)
        return false;
    value.assign(encoded, cursor, length);
    cursor += length;
    return true;
}

bool readFieldSeparator(const std::string& encoded, std::size_t& cursor)
{
    if (cursor >= encoded.size() || encoded[cursor] != ';')
        return false;
    ++cursor;
    return true;
}

bool readLengthField(const std::string& encoded,
                     std::size_t& cursor,
                     const std::string& name,
                     std::string& value)
{
    return readLiteral(encoded, cursor, name + "=")
        && readLengthEncoded(encoded, cursor, value)
        && readFieldSeparator(encoded, cursor);
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

std::string encodeDeliveryEnvelope(const PalaceDeliveryEnvelopeV1& envelope)
{
    return canonicalDeliveryEnvelope(envelope) + ";signature=" + lengthEncoded(envelope.signature);
}

DeliveryEnvelopeDecode decodeDeliveryEnvelope(const std::string& encoded)
{
    if (encoded.empty() || encoded.size() > kMaxEncodedEnvelopeBytes)
        return decodeReject("invalid-envelope-size");

    PalaceDeliveryEnvelopeV1 envelope;
    std::size_t cursor = 0U;
    int kind = -1;
    if (!readLiteral(encoded, cursor, "version=")
        || !readInteger(encoded, cursor, envelope.protocolVersion)
        || !readLengthField(encoded, cursor, "network", envelope.networkId)
        || !readLengthField(encoded, cursor, "palace", envelope.palaceId)
        || !readLengthField(encoded, cursor, "room", envelope.roomId)
        || !readLiteral(encoded, cursor, "epoch=")
        || !readInteger(encoded, cursor, envelope.roomEpoch)
        || !readLiteral(encoded, cursor, "kind=")
        || !readInteger(encoded, cursor, kind)
        || !readLengthField(encoded, cursor, "sender", envelope.senderUserId)
        || !readLiteral(encoded, cursor, "key_epoch=")
        || !readInteger(encoded, cursor, envelope.senderKeyEpoch)
        || !readLiteral(encoded, cursor, "sequence=")
        || !readInteger(encoded, cursor, envelope.senderSequence)
        || !readLiteral(encoded, cursor, "created=")
        || !readInteger(encoded, cursor, envelope.createdAt)
        || !readLiteral(encoded, cursor, "expires=")
        || !readInteger(encoded, cursor, envelope.expiresAt)
        || !readLengthField(encoded, cursor, "payload", envelope.payload)
        || !readLiteral(encoded, cursor, "signature=")
        || !readLengthEncoded(encoded, cursor, envelope.signature)
        || cursor != encoded.size()) {
        return decodeReject("invalid-envelope-encoding");
    }
    if (kind < static_cast<int>(DeliveryKind::PresenceHello)
        || kind > static_cast<int>(DeliveryKind::AuthorityRefreshNotice)) {
        return decodeReject("invalid-envelope-kind");
    }
    envelope.kind = static_cast<DeliveryKind>(kind);
    return {true, "accepted", std::move(envelope)};
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

DeliveryValidation DeliveryIngress::validate(
    const std::string& contentTopic,
    const PalaceDeliveryEnvelopeV1& envelope,
    const DeliveryPolicy& policy,
    const DeliverySignatureVerifier& verifier) const
{
    if (policy.authority == nullptr || policy.now <= 0 || policy.networkId.empty()
        || policy.palaceId.empty() || policy.roomId.empty()
        || policy.maxTrackedSenders == 0U) {
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
    if (previous == m_lastSequence.end()
        && m_lastSequence.size() >= policy.maxTrackedSenders) {
        return reject("replay-state-capacity-exceeded");
    }
    if (envelope.kind == DeliveryKind::Motion && policy.minMotionIntervalSeconds > 0) {
        const auto lastMotion = m_lastMotionAt.find(key);
        if (lastMotion != m_lastMotionAt.end()
            && envelope.createdAt - lastMotion->second < policy.minMotionIntervalSeconds) {
            return reject("motion-rate-exceeded");
        }
    }

    return {true, "accepted"};
}

DeliveryValidation DeliveryIngress::commitValidated(
    const PalaceDeliveryEnvelopeV1& envelope,
    const DeliveryPolicy& policy)
{
    if (policy.maxTrackedSenders == 0U)
        return reject("invalid-policy");

    const std::string key = senderKey(envelope);
    const auto previous = m_lastSequence.find(key);
    if (previous != m_lastSequence.end()
        && envelope.senderSequence <= previous->second) {
        return reject("duplicate-or-replayed-sequence");
    }
    if (previous == m_lastSequence.end()
        && m_lastSequence.size() >= policy.maxTrackedSenders) {
        return reject("replay-state-capacity-exceeded");
    }
    if (envelope.kind == DeliveryKind::Motion
        && policy.minMotionIntervalSeconds > 0) {
        const auto lastMotion = m_lastMotionAt.find(key);
        if (lastMotion != m_lastMotionAt.end()
            && envelope.createdAt - lastMotion->second
                < policy.minMotionIntervalSeconds) {
            return reject("motion-rate-exceeded");
        }
    }

    m_lastSequence[key] = envelope.senderSequence;
    if (envelope.kind == DeliveryKind::Motion)
        m_lastMotionAt[key] = envelope.createdAt;
    return {true, "accepted"};
}

DeliveryValidation DeliveryIngress::receive(
    const std::string& contentTopic,
    const PalaceDeliveryEnvelopeV1& envelope,
    const DeliveryPolicy& policy,
    const DeliverySignatureVerifier& verifier)
{
    const DeliveryValidation checked = validate(
        contentTopic, envelope, policy, verifier);
    if (!checked.accepted)
        return checked;
    return commitValidated(envelope, policy);
}

std::uint64_t DeliveryIngress::lastSequenceFor(
    const std::string& senderUserId,
    std::int64_t senderKeyEpoch) const
{
    const auto found = m_lastSequence.find(
        senderUserId + "@" + std::to_string(senderKeyEpoch));
    return found == m_lastSequence.end() ? 0U : found->second;
}

DeliverySequenceStateV1 DeliveryIngress::sequenceState() const
{
    return {m_lastSequence};
}

bool DeliveryIngress::restoreSequenceState(const DeliverySequenceStateV1& state,
                                           std::size_t maxTrackedSenders)
{
    if (!validSequenceState(state, maxTrackedSenders))
        return false;
    m_lastSequence = state.lastSequence;
    m_lastMotionAt.clear();
    return true;
}

DeliveryPublication DeliveryEgress::prepare(const DeliveryPolicy& policy,
                                             const std::string& senderUserId,
                                             std::int64_t senderKeyEpoch,
                                             DeliveryKind kind,
                                             const std::string& payload,
                                             std::int64_t createdAt,
                                             std::int64_t lifetimeSeconds,
                                             const DeliverySignatureSigner& signer,
                                             const DeliverySignatureVerifier& verifier)
{
    if (policy.authority == nullptr || senderUserId.empty() || senderKeyEpoch < 0
        || createdAt <= 0 || lifetimeSeconds <= 0
        || createdAt > std::numeric_limits<std::int64_t>::max() - lifetimeSeconds) {
        return {false, "invalid-egress-request", {}, {}, 0U};
    }
    const std::string publicKey = signer.publicKey();
    if (publicKey.empty() || policy.authority->deliveryKeyFor(senderUserId, senderKeyEpoch) != publicKey) {
        return {false, "unbound-delivery-key", {}, {}, 0U};
    }
    const std::string senderKey = senderUserId + "@" + std::to_string(senderKeyEpoch);
    const auto previous = m_lastSequence.find(senderKey);
    if (previous == m_lastSequence.end()
        && m_lastSequence.size() >= policy.maxTrackedSenders) {
        return {false, "egress-state-capacity-exceeded", {}, {}, 0U};
    }
    const std::uint64_t sequence = previous == m_lastSequence.end() ? 1U : previous->second + 1U;
    if (sequence == 0U)
        return {false, "sequence-exhausted", {}, {}, 0U};

    PalaceDeliveryEnvelopeV1 envelope;
    envelope.networkId = policy.networkId;
    envelope.palaceId = policy.palaceId;
    envelope.roomId = policy.roomId;
    envelope.roomEpoch = policy.roomEpoch;
    envelope.kind = kind;
    envelope.senderUserId = senderUserId;
    envelope.senderKeyEpoch = senderKeyEpoch;
    envelope.senderSequence = sequence;
    envelope.createdAt = createdAt;
    envelope.expiresAt = createdAt + lifetimeSeconds;
    envelope.payload = payload;
    envelope.signature = signer.sign(canonicalDeliveryEnvelope(envelope));
    if (envelope.signature.empty())
        return {false, "signing-failed", {}, {}, 0U};

    const std::string topic = deriveRoomTopic(policy.networkId, policy.palaceId, policy.roomId,
                                               policy.roomEpoch);
    const DeliveryValidation checked = m_preflight.receive(topic, envelope, policy, verifier);
    if (!checked.accepted)
        return {false, "preflight=" + checked.reason, {}, {}, 0U};

    m_lastSequence[senderKey] = sequence;
    const std::string encoded = encodeDeliveryEnvelope(envelope);
    return {true, "accepted", topic, std::vector<std::uint8_t>(encoded.begin(), encoded.end()), sequence};
}

DeliverySequenceStateV1 DeliveryEgress::sequenceState() const
{
    return {m_lastSequence};
}

bool DeliveryEgress::restoreSequenceState(const DeliverySequenceStateV1& state,
                                          std::size_t maxTrackedSenders)
{
    if (!validSequenceState(state, maxTrackedSenders))
        return false;

    DeliveryIngress restoredPreflight;
    if (!restoredPreflight.restoreSequenceState(state, maxTrackedSenders))
        return false;
    m_lastSequence = state.lastSequence;
    m_preflight = std::move(restoredPreflight);
    return true;
}

} // namespace palace
