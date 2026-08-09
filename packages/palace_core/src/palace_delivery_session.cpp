#include "palace_delivery_session.h"

#include "palace_sha256.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace palace {
namespace {

namespace fs = std::filesystem;

constexpr const char* kSessionFileName = "delivery-session-v1";
constexpr const char* kTemporaryFileName = "delivery-session-v1.next";
constexpr char kHexDigits[] = "0123456789abcdef";
constexpr std::size_t kMaximumTrackedSenders = 1024U;
constexpr std::size_t kMaximumOutboxEntries = 256U;
constexpr std::size_t kMaximumParticipants = 512U;
constexpr std::size_t kMaximumEncodedEnvelopeBytes = 4096U;
constexpr std::size_t kMaximumStateBytes = 3U * 1024U * 1024U;
constexpr std::uint64_t kMaximumPendingIngressGap = 32U;
constexpr std::size_t kMaximumPendingIngressCount = 256U;
constexpr std::size_t kMaximumPendingIngressBytes = 128U * 1024U;

DeliverySessionTransition transition(bool accepted, std::string reason)
{
    return {accepted, std::move(reason), {}};
}

DeliverySessionTransition commandTransition(bool accepted,
                                            std::string reason,
                                            DeliverySessionCommand command)
{
    DeliverySessionTransition result{accepted, std::move(reason), {}};
    result.commands.push_back(std::move(command));
    return result;
}

bool isIdentifier(const std::string& value)
{
    if (value.empty() || value.size() > 64U)
        return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '_' || character == '-';
    });
}

std::string ingressSenderKey(const PalaceDeliveryEnvelopeV1& envelope)
{
    return envelope.senderUserId + "@"
        + std::to_string(envelope.senderKeyEpoch);
}

bool requiresParticipantProjection(DeliveryKind kind)
{
    return kind == DeliveryKind::PresenceHello
        || kind == DeliveryKind::Motion
        || kind == DeliveryKind::Speech
        || kind == DeliveryKind::WearProp
        || kind == DeliveryKind::RemoveProp;
}

bool isNetworkId(const std::string& value)
{
    if (value.empty() || value.size() > 128U)
        return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '_' || character == '-'
            || character == '.';
    });
}

bool validConfig(const DeliverySessionConfigV1& config,
                 const AuthorityProjection& authority,
                 const bool allowPendingAuthority = false)
{
    const bool authorityPending = authority.palaceId().empty();
    return isNetworkId(config.networkId)
        && isIdentifier(config.palaceId)
        && isIdentifier(config.roomId)
        && config.roomEpoch >= 0
        && isIdentifier(config.senderUserId)
        && config.senderKeyEpoch >= 0
        && config.maxTrackedSenders > 0U
        && config.maxTrackedSenders <= kMaximumTrackedSenders
        && config.maxOutboxEntries > 0U
        && config.maxOutboxEntries <= kMaximumOutboxEntries
        && config.maxParticipants > 0U
        && config.maxParticipants <= kMaximumParticipants
        && ((allowPendingAuthority && authorityPending)
            || (authority.palaceId() == config.palaceId
                && !authority.deliveryKeyFor(
                    config.senderUserId,
                    config.senderKeyEpoch).empty()));
}

bool parseMotion(const std::string& payload, std::int64_t& x, std::int64_t& y)
{
    const std::size_t separator = payload.find(';');
    if (separator == std::string::npos || payload.find(';', separator + 1U) != std::string::npos)
        return false;
    const auto parseCoordinate = [](const std::string& field,
                                    const std::string& prefix,
                                    std::int64_t& coordinate) {
        if (field.rfind(prefix, 0) != 0)
            return false;
        const std::string value = field.substr(prefix.size());
        const auto parsed = std::from_chars(
            value.data(), value.data() + value.size(), coordinate);
        return !value.empty() && parsed.ec == std::errc()
            && parsed.ptr == value.data() + value.size()
            && coordinate >= 0 && coordinate <= 10000;
    };
    return parseCoordinate(payload.substr(0, separator), "x=", x)
        && parseCoordinate(payload.substr(separator + 1U), "y=", y);
}

std::string hexEncode(const std::string& value)
{
    std::string encoded;
    encoded.reserve(value.size() * 2U);
    for (const unsigned char byte : value) {
        encoded.push_back(kHexDigits[byte >> 4U]);
        encoded.push_back(kHexDigits[byte & 0x0fU]);
    }
    return encoded;
}

std::string hexEncode(const std::vector<std::uint8_t>& value)
{
    return hexEncode(std::string(value.begin(), value.end()));
}

bool hexNibble(char character, unsigned char& value)
{
    if (character >= '0' && character <= '9') {
        value = static_cast<unsigned char>(character - '0');
        return true;
    }
    if (character >= 'a' && character <= 'f') {
        value = static_cast<unsigned char>(character - 'a' + 10);
        return true;
    }
    return false;
}

bool hexDecode(const std::string& encoded, std::string& value)
{
    if (encoded.empty() || encoded.size() % 2U != 0U)
        return false;
    value.clear();
    value.reserve(encoded.size() / 2U);
    for (std::size_t index = 0; index < encoded.size(); index += 2U) {
        unsigned char high = 0;
        unsigned char low = 0;
        if (!hexNibble(encoded[index], high) || !hexNibble(encoded[index + 1U], low))
            return false;
        value.push_back(static_cast<char>((high << 4U) | low));
    }
    return true;
}

bool hexDecode(const std::string& encoded, std::vector<std::uint8_t>& value)
{
    std::string decoded;
    if (!hexDecode(encoded, decoded))
        return false;
    value.assign(decoded.begin(), decoded.end());
    return true;
}

std::vector<std::string> split(const std::string& value, char delimiter)
{
    std::vector<std::string> fields;
    std::size_t cursor = 0U;
    while (true) {
        const std::size_t next = value.find(delimiter, cursor);
        fields.push_back(value.substr(cursor, next - cursor));
        if (next == std::string::npos)
            return fields;
        cursor = next + 1U;
    }
}

template <typename Integer>
bool parseInteger(const std::string& value, Integer& parsedValue)
{
    if (value.empty())
        return false;
    const auto parsed = std::from_chars(
        value.data(), value.data() + value.size(), parsedValue);
    return parsed.ec == std::errc() && parsed.ptr == value.data() + value.size();
}

bool flushFile(const fs::path& path)
{
#if defined(__unix__) || defined(__APPLE__)
    const int descriptor = ::open(path.c_str(), O_RDONLY);
    if (descriptor < 0)
        return false;
    const bool flushed = ::fsync(descriptor) == 0;
    ::close(descriptor);
    return flushed;
#else
    (void)path;
    return true;
#endif
}

bool flushDirectory(const fs::path& path)
{
#if defined(__unix__) || defined(__APPLE__)
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (descriptor < 0)
        return false;
    const bool flushed = ::fsync(descriptor) == 0;
    ::close(descriptor);
    return flushed;
#else
    (void)path;
    return true;
#endif
}

bool parseRecord(const std::string& record, std::string& state)
{
    if (record.size() > kMaximumStateBytes)
        return false;
    const std::size_t newline = record.find('\n');
    if (newline == std::string::npos)
        return false;
    const std::string checksum = record.substr(0, newline);
    state = record.substr(newline + 1U);
    return checksum.size() == 64U && checksum == crypto::sha256Hex(state);
}

} // namespace

std::string deliverySessionStateName(DeliverySessionState state)
{
    switch (state) {
    case DeliverySessionState::Unconfigured: return "unconfigured";
    case DeliverySessionState::Offline: return "offline";
    case DeliverySessionState::AwaitingCallbacks: return "awaiting_callbacks";
    case DeliverySessionState::StartingNode: return "starting_node";
    case DeliverySessionState::WaitingForConnection: return "waiting_for_connection";
    case DeliverySessionState::Subscribing: return "subscribing";
    case DeliverySessionState::Online: return "online";
    case DeliverySessionState::Recovering: return "recovering";
    }
    return "offline";
}

PalaceDeliverySession::PalaceDeliverySession(const AuthorityProjection& authority)
    : m_authority(authority)
{
}

bool PalaceDeliverySession::configure(const DeliverySessionConfigV1& config)
{
    if ((m_state != DeliverySessionState::Unconfigured
         && m_state != DeliverySessionState::Offline)
        || !validConfig(config, m_authority)) {
        return false;
    }

    DeliveryIngress emptyIngress;
    DeliveryEgress emptyEgress;
    m_config = config;
    m_configured = true;
    m_state = DeliverySessionState::Offline;
    m_connectionState = DeliveryConnectionState::Disconnected;
    m_nodeStarted = false;
    m_subscriptionPending = false;
    m_subscribed = false;
    m_ingress = std::move(emptyIngress);
    m_egress = std::move(emptyEgress);
    m_allowedProps.clear();
    m_outbox.clear();
    m_participants.clear();
    clearPendingIngress();
    return true;
}

bool PalaceDeliverySession::hasConfiguration() const
{
    return m_configured;
}

bool PalaceDeliverySession::configurationMatchesAuthority() const
{
    return m_configured && validConfig(m_config, m_authority);
}

const DeliverySessionConfigV1& PalaceDeliverySession::configuration() const
{
    return m_config;
}

DeliverySessionTransition PalaceDeliverySession::switchRoom(
    const std::string& roomId,
    std::int64_t roomEpoch)
{
    if (!m_configured)
        return transition(false, "session-not-configured");
    if (!isIdentifier(roomId) || roomEpoch < 0)
        return transition(false, "invalid-room");
    if (m_authority.palaceId() != m_config.palaceId
        || m_authority.roomEpoch(m_config.roomId) != m_config.roomEpoch
        || m_authority.roomEpoch(roomId) != roomEpoch) {
        return transition(false, "room-not-in-finalized-authority");
    }
    if (m_authority.isRoomLocked(roomId))
        return transition(false, "finalized-room-locked");
    if (roomId == m_config.roomId && roomEpoch == m_config.roomEpoch)
        return transition(true, "room-unchanged");
    if (!m_outbox.empty())
        return transition(false, "room-switch-outbox-not-empty");
    if (m_subscriptionPending)
        return transition(false, "room-switch-subscription-pending");

    DeliverySessionConfigV1 nextConfig = m_config;
    nextConfig.roomId = roomId;
    nextConfig.roomEpoch = roomEpoch;
    if (!validConfig(nextConfig, m_authority))
        return transition(false, "invalid-room-configuration");

    DeliveryIngress emptyIngress;
    DeliveryEgress emptyEgress;
    m_config = std::move(nextConfig);
    m_subscribed = false;
    m_ingress = std::move(emptyIngress);
    m_egress = std::move(emptyEgress);
    m_participants.clear();
    clearPendingIngress();
    return subscribeIfReady();
}

DeliverySessionTransition PalaceDeliverySession::rebindRoom(
    const std::string& roomId,
    std::int64_t roomEpoch)
{
    if (!m_configured)
        return transition(false, "session-not-configured");
    if (!isIdentifier(roomId) || roomEpoch < 0)
        return transition(false, "invalid-room");
    if (m_authority.palaceId() != m_config.palaceId
        || m_authority.roomEpoch(roomId) != roomEpoch
        || m_authority.isRoomLocked(roomId)) {
        return transition(false, "room-not-in-finalized-authority");
    }
    if (roomId == m_config.roomId && roomEpoch == m_config.roomEpoch)
        return transition(true, "room-unchanged");
    if (!m_outbox.empty())
        return transition(false, "room-switch-outbox-not-empty");
    if (m_subscriptionPending)
        return transition(false, "room-switch-subscription-pending");

    DeliverySessionConfigV1 nextConfig = m_config;
    nextConfig.roomId = roomId;
    nextConfig.roomEpoch = roomEpoch;
    if (!validConfig(nextConfig, m_authority))
        return transition(false, "invalid-room-configuration");

    DeliveryIngress emptyIngress;
    DeliveryEgress emptyEgress;
    m_config = std::move(nextConfig);
    m_subscribed = false;
    m_ingress = std::move(emptyIngress);
    m_egress = std::move(emptyEgress);
    m_participants.clear();
    clearPendingIngress();
    return subscribeIfReady();
}

DeliverySessionTransition PalaceDeliverySession::start()
{
    if (!m_configured
        || (m_state != DeliverySessionState::Offline
            && m_state != DeliverySessionState::Recovering)) {
        return transition(false, "invalid-session-state");
    }
    m_nodeStarted = false;
    m_subscriptionPending = false;
    m_subscribed = false;
    m_connectionState = DeliveryConnectionState::Disconnected;
    m_participants.clear();
    clearPendingIngress();
    m_state = DeliverySessionState::AwaitingCallbacks;
    return commandTransition(true, "register-callbacks",
                             {DeliverySessionCommandKind::RegisterCallbacks, {}, {}, {}});
}

DeliverySessionTransition PalaceDeliverySession::callbacksRegistered(bool succeeded)
{
    if (m_state != DeliverySessionState::AwaitingCallbacks)
        return transition(false, "unexpected-callback-registration-result");
    if (!succeeded) {
        clearPendingIngress();
        m_state = DeliverySessionState::Offline;
        return transition(false, "callback-registration-failed");
    }
    m_state = DeliverySessionState::StartingNode;
    return commandTransition(true, "start-node",
                             {DeliverySessionCommandKind::StartNode, {}, {}, {}});
}

DeliverySessionTransition PalaceDeliverySession::nodeStarted(bool succeeded)
{
    if (m_state != DeliverySessionState::StartingNode)
        return transition(false, "unexpected-node-start-result");
    if (!succeeded) {
        m_nodeStarted = false;
        clearPendingIngress();
        m_state = DeliverySessionState::Offline;
        return transition(false, "node-start-failed");
    }
    m_nodeStarted = true;
    m_state = DeliverySessionState::WaitingForConnection;
    return subscribeIfReady();
}

DeliverySessionTransition PalaceDeliverySession::connectionStateChanged(
    DeliveryConnectionState state)
{
    if (!m_configured)
        return transition(false, "session-not-configured");

    m_connectionState = state;
    if (state == DeliveryConnectionState::Connected)
        return subscribeIfReady();

    clearPendingIngress();
    if (m_subscribed || m_subscriptionPending
        || m_state == DeliverySessionState::Online
        || m_state == DeliverySessionState::Subscribing) {
        m_subscribed = false;
        m_subscriptionPending = false;
        m_participants.clear();
        m_state = DeliverySessionState::Recovering;
    } else if (m_nodeStarted && m_state != DeliverySessionState::StartingNode) {
        m_state = DeliverySessionState::WaitingForConnection;
    }
    return transition(true, state == DeliveryConnectionState::Connecting
                                ? "connection-pending"
                                : "connection-unavailable");
}

DeliverySessionTransition PalaceDeliverySession::subscriptionResult(bool succeeded)
{
    if (m_state != DeliverySessionState::Subscribing || !m_subscriptionPending)
        return transition(false, "unexpected-subscription-result");
    m_subscriptionPending = false;
    if (!succeeded) {
        m_subscribed = false;
        clearPendingIngress();
        m_state = DeliverySessionState::Recovering;
        return transition(false, "subscription-failed");
    }
    m_subscribed = true;
    m_state = DeliverySessionState::Online;
    return transition(true, "online");
}

void PalaceDeliverySession::interrupt(bool recoverable)
{
    if (!m_configured)
        return;
    m_connectionState = DeliveryConnectionState::Disconnected;
    m_nodeStarted = false;
    m_subscriptionPending = false;
    m_subscribed = false;
    m_participants.clear();
    clearPendingIngress();
    m_state = recoverable ? DeliverySessionState::Recovering
                          : DeliverySessionState::Offline;
}

DeliverySessionState PalaceDeliverySession::state() const
{
    return m_state;
}

DeliveryConnectionState PalaceDeliverySession::connectionState() const
{
    return m_connectionState;
}

void PalaceDeliverySession::replaceAllowedProps(
    std::map<std::string, std::string> allowedProps)
{
    m_allowedProps = std::move(allowedProps);
}

bool PalaceDeliverySession::reconcileAuthority()
{
    if (!m_configured)
        return false;

    bool projectionChanged = false;
    if (m_authority.isUserBanned(
            m_config.senderUserId, m_config.roomId)
        && !m_outbox.empty()) {
        // A restart may restore queued sends before the latest authority
        // checkpoint is rebuilt. Never flush work authored by a now-banned
        // sender.
        m_outbox.clear();
        projectionChanged = true;
    }
    for (auto participant = m_participants.begin();
         participant != m_participants.end();) {
        if (m_authority.isUserBanned(participant->first, m_config.roomId)) {
            participant = m_participants.erase(participant);
            projectionChanged = true;
            continue;
        }

        auto prop = participant->second.propIds.begin();
        while (prop != participant->second.propIds.end()) {
            const auto allowed = m_allowedProps.find(*prop);
            if (allowed == m_allowedProps.end()
                || m_authority.isAssetBanned(allowed->second, m_config.roomId)) {
                prop = participant->second.propIds.erase(prop);
                projectionChanged = true;
            } else {
                ++prop;
            }
        }
        ++participant;
    }

    for (auto sender = m_pendingIngress.begin();
         sender != m_pendingIngress.end();) {
        auto pending = sender->second.begin();
        while (pending != sender->second.end()) {
            if (m_authority.isUserBanned(
                    pending->second.envelope.senderUserId, m_config.roomId)) {
                --m_pendingIngressCount;
                m_pendingIngressBytes -= pending->second.encodedBytes;
                pending = sender->second.erase(pending);
            } else {
                ++pending;
            }
        }
        if (sender->second.empty())
            sender = m_pendingIngress.erase(sender);
        else
            ++sender;
    }
    return projectionChanged;
}

DeliverySessionTransition PalaceDeliverySession::publish(
    const std::string& requestId,
    DeliveryKind kind,
    const std::string& payload,
    std::int64_t now,
    std::int64_t lifetimeSeconds,
    const DeliverySignatureSigner& signer,
    const DeliverySignatureVerifier& verifier)
{
    if (!m_configured)
        return transition(false, "session-not-configured");
    if (m_authority.isUserBanned(
            m_config.senderUserId, m_config.roomId))
        return transition(false, "sender-banned");
    if (!isIdentifier(requestId))
        return transition(false, "invalid-request-id");
    if (m_outbox.find(requestId) != m_outbox.end())
        return transition(false, "duplicate-request-id");
    if (m_outbox.size() >= m_config.maxOutboxEntries)
        return transition(false, "outbox-capacity-exceeded");
    if (now <= 0 || lifetimeSeconds <= 0 || lifetimeSeconds > 300
        || now > std::numeric_limits<std::int64_t>::max() - lifetimeSeconds) {
        return transition(false, "invalid-message-lifetime");
    }

    const DeliveryPublication publication = m_egress.prepare(
        policy(now), m_config.senderUserId, m_config.senderKeyEpoch, kind,
        payload, now, lifetimeSeconds, signer, verifier);
    if (!publication.accepted)
        return transition(false, publication.reason);

    const std::string encoded(publication.payload.begin(), publication.payload.end());
    const DeliveryEnvelopeDecode decoded = decodeDeliveryEnvelope(encoded);
    if (!decoded.accepted || decoded.envelope.expiresAt < now)
        return transition(false, "invalid-prepared-envelope");

    OutboxEntry entry;
    entry.contentTopic = publication.contentTopic;
    entry.payload = publication.payload;
    entry.senderSequence = publication.sequence;
    entry.expiresAt = decoded.envelope.expiresAt;
    const auto inserted = m_outbox.emplace(requestId, std::move(entry));
    if (!inserted.second)
        return transition(false, "duplicate-request-id");

    if (m_state != DeliverySessionState::Online)
        return transition(true, "queued");
    return commandTransition(true, "send-message",
                             sendCommand(inserted.first->first, inserted.first->second));
}

bool PalaceDeliverySession::messageSent(const std::string& requestId)
{
    return m_outbox.erase(requestId) == 1U;
}

bool PalaceDeliverySession::messagePropagated(const std::string& requestId)
{
    // Palace live-room messages require peer propagation, not archive/store
    // validation. Delivery may never emit message_sent when store is disabled,
    // so propagation is the terminal success boundary for this outbox.
    return m_outbox.erase(requestId) == 1U;
}

bool PalaceDeliverySession::messageError(const std::string& requestId, std::int64_t now)
{
    const auto found = m_outbox.find(requestId);
    if (found == m_outbox.end() || now <= 0)
        return false;
    if (found->second.expiresAt < now) {
        m_outbox.erase(found);
        return true;
    }
    found->second.stage = DeliveryOutboxStage::PendingSend;
    return true;
}

std::vector<DeliverySessionCommand> PalaceDeliverySession::eligibleOutboxCommands(
    std::int64_t now)
{
    std::vector<DeliverySessionCommand> commands;
    if (now <= 0)
        return commands;

    for (auto entry = m_outbox.begin(); entry != m_outbox.end();) {
        if (entry->second.expiresAt < now) {
            entry = m_outbox.erase(entry);
            continue;
        }
        if (m_state == DeliverySessionState::Online)
            commands.push_back(sendCommand(entry->first, entry->second));
        ++entry;
    }
    return commands;
}

DeliveryOutboxStatus PalaceDeliverySession::outboxStatus(
    const std::string& requestId) const
{
    const auto found = m_outbox.find(requestId);
    if (found == m_outbox.end())
        return {};
    return {true, found->second.stage, found->second.senderSequence,
            found->second.expiresAt};
}

std::size_t PalaceDeliverySession::outboxSize() const
{
    return m_outbox.size();
}

DeliverySessionReceive PalaceDeliverySession::receive(
    const std::string& contentTopic,
    const std::vector<std::uint8_t>& payload,
    std::int64_t now,
    const DeliverySignatureVerifier& verifier)
{
    if (m_state != DeliverySessionState::Online)
        return {false, false, "session-not-online"};
    if (payload.empty() || payload.size() > kMaximumEncodedEnvelopeBytes)
        return {false, false, "invalid-envelope-size"};

    const std::string encoded(payload.begin(), payload.end());
    const DeliveryEnvelopeDecode decoded = decodeDeliveryEnvelope(encoded);
    if (!decoded.accepted)
        return {false, false, decoded.reason};

    const DeliveryPolicy currentPolicy = policy(now);
    const DeliveryValidation validated = m_ingress.validate(
        contentTopic, decoded.envelope, currentPolicy, verifier);
    if (!validated.accepted)
        return {false, false, validated.reason};

    const PalaceDeliveryEnvelopeV1& envelope = decoded.envelope;
    const std::string senderKey = ingressSenderKey(envelope);
    const std::uint64_t committed = m_ingress.lastSequenceFor(
        envelope.senderUserId, envelope.senderKeyEpoch);
    const auto pendingSender = m_pendingIngress.find(senderKey);
    if (pendingSender != m_pendingIngress.end()
        && pendingSender->second.find(envelope.senderSequence)
            != pendingSender->second.end()) {
        return {false, false, "duplicate-or-replayed-sequence"};
    }

    if (committed == 0U && pendingSender == m_pendingIngress.end()) {
        const DeliverySequenceStateV1 committedState =
            m_ingress.sequenceState();
        std::size_t trackedSenders = committedState.lastSequence.size();
        for (const auto& pending : m_pendingIngress) {
            if (committedState.lastSequence.find(pending.first)
                == committedState.lastSequence.end()) {
                ++trackedSenders;
            }
        }
        if (trackedSenders >= m_config.maxTrackedSenders)
            return {false, false, "replay-state-capacity-exceeded"};
    }

    if (envelope.kind == DeliveryKind::PresenceHello) {
        if (wouldExceedParticipantLimit(envelope))
            return {false, false, "participant-capacity-exceeded"};
        const DeliveryValidation committedPresence =
            m_ingress.commitValidated(envelope, currentPolicy);
        if (!committedPresence.accepted)
            return {false, false, committedPresence.reason};
        discardPendingIngressThrough(senderKey, envelope.senderSequence);
        const auto future = m_pendingIngress.find(senderKey);
        if (future != m_pendingIngress.end()) {
            for (auto pending = future->second.begin();
                 pending != future->second.end();) {
                if (pending->first - envelope.senderSequence
                    > kMaximumPendingIngressGap) {
                    --m_pendingIngressCount;
                    m_pendingIngressBytes -= pending->second.encodedBytes;
                    pending = future->second.erase(pending);
                } else {
                    ++pending;
                }
            }
            if (future->second.empty())
                m_pendingIngress.erase(future);
        }
        bool projectionChanged = apply(envelope);
        projectionChanged =
            drainPendingIngress(senderKey, currentPolicy)
            || projectionChanged;
        return {true, projectionChanged, "accepted"};
    }

    if (committed == 0U) {
        const DeliveryValidation buffered = bufferPendingIngress(
            senderKey, envelope, encoded.size());
        return {buffered.accepted, false, buffered.reason};
    }

    if (envelope.senderSequence > committed + 1U) {
        if (envelope.senderSequence - committed
            > kMaximumPendingIngressGap) {
            return {false, false, "reorder-gap-exceeded"};
        }
        const DeliveryValidation buffered = bufferPendingIngress(
            senderKey, envelope, encoded.size());
        return {buffered.accepted, false, buffered.reason};
    }

    if (wouldExceedParticipantLimit(envelope))
        return {false, false, "participant-capacity-exceeded"};
    const DeliveryValidation committedEnvelope =
        m_ingress.commitValidated(envelope, currentPolicy);
    if (!committedEnvelope.accepted)
        return {false, false, committedEnvelope.reason};
    bool projectionChanged = apply(envelope);
    projectionChanged =
        drainPendingIngress(senderKey, currentPolicy)
        || projectionChanged;
    return {true, projectionChanged, "accepted"};
}

void PalaceDeliverySession::expireTransient(std::int64_t now)
{
    if (now <= 0)
        return;
    for (auto sender = m_pendingIngress.begin();
         sender != m_pendingIngress.end();) {
        for (auto pending = sender->second.begin();
             pending != sender->second.end();) {
            if (pending->second.envelope.expiresAt < now) {
                --m_pendingIngressCount;
                m_pendingIngressBytes -= pending->second.encodedBytes;
                pending = sender->second.erase(pending);
            } else {
                ++pending;
            }
        }
        if (sender->second.empty())
            sender = m_pendingIngress.erase(sender);
        else
            ++sender;
    }
    for (auto participant = m_participants.begin();
         participant != m_participants.end();) {
        DeliveryParticipantProjectionV1& projection = participant->second;
        if (projection.present && projection.presenceExpiresAt < now) {
            participant = m_participants.erase(participant);
            continue;
        }
        if (projection.hasMotion && projection.motionExpiresAt < now) {
            projection.hasMotion = false;
            projection.motionX = 0;
            projection.motionY = 0;
            projection.motionExpiresAt = 0;
        }
        if (!projection.speech.empty() && projection.speechExpiresAt < now) {
            projection.speech.clear();
            projection.speechExpiresAt = 0;
        }
        ++participant;
    }
}

std::vector<DeliveryParticipantProjectionV1>
PalaceDeliverySession::participantSnapshot() const
{
    std::vector<DeliveryParticipantProjectionV1> snapshot;
    snapshot.reserve(m_participants.size());
    for (const auto& participant : m_participants)
        snapshot.push_back(participant.second);
    return snapshot;
}

std::string PalaceDeliverySession::canonicalState() const
{
    if (!m_configured)
        return {};

    std::ostringstream state;
    state << "version=1\n"
          << "config;" << hexEncode(m_config.networkId)
          << ';' << hexEncode(m_config.palaceId)
          << ';' << hexEncode(m_config.roomId)
          << ';' << m_config.roomEpoch
          << ';' << hexEncode(m_config.senderUserId)
          << ';' << m_config.senderKeyEpoch
          << ';' << m_config.maxTrackedSenders
          << ';' << m_config.maxOutboxEntries
          << ';' << m_config.maxParticipants << '\n';

    for (const auto& sequence : m_ingress.sequenceState().lastSequence) {
        state << "ingress;" << hexEncode(sequence.first)
              << ';' << sequence.second << '\n';
    }
    for (const auto& sequence : m_egress.sequenceState().lastSequence) {
        state << "egress;" << hexEncode(sequence.first)
              << ';' << sequence.second << '\n';
    }
    for (const auto& outbox : m_outbox) {
        state << "outbox;" << hexEncode(outbox.first)
              << ';' << static_cast<unsigned int>(outbox.second.stage)
              << ';' << outbox.second.senderSequence
              << ';' << outbox.second.expiresAt
              << ';' << hexEncode(outbox.second.contentTopic)
              << ';' << hexEncode(outbox.second.payload) << '\n';
    }
    return state.str();
}

bool PalaceDeliverySession::restoreCanonicalState(const std::string& serialized)
{
    static constexpr const char* kPrefix = "version=1\n";
    if (serialized.size() > kMaximumStateBytes
        || serialized.rfind(kPrefix, 0) != 0
        || serialized.back() != '\n') {
        return false;
    }

    DeliverySessionConfigV1 restoredConfig;
    DeliverySequenceStateV1 restoredIngressState;
    DeliverySequenceStateV1 restoredEgressState;
    std::map<std::string, OutboxEntry> restoredOutbox;
    bool sawConfig = false;
    bool sawNonConfig = false;
    std::set<std::uint64_t> outboxSequences;

    std::istringstream input(
        serialized.substr(std::char_traits<char>::length(kPrefix)));
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty())
            return false;
        const std::vector<std::string> fields = split(line, ';');
        if (fields[0] == "config") {
            if (sawConfig || sawNonConfig || fields.size() != 10U)
                return false;
            std::uint64_t tracked = 0U;
            std::uint64_t outbox = 0U;
            std::uint64_t participants = 0U;
            if (!hexDecode(fields[1], restoredConfig.networkId)
                || !hexDecode(fields[2], restoredConfig.palaceId)
                || !hexDecode(fields[3], restoredConfig.roomId)
                || !parseInteger(fields[4], restoredConfig.roomEpoch)
                || !hexDecode(fields[5], restoredConfig.senderUserId)
                || !parseInteger(fields[6], restoredConfig.senderKeyEpoch)
                || !parseInteger(fields[7], tracked)
                || !parseInteger(fields[8], outbox)
                || !parseInteger(fields[9], participants)
                || tracked > std::numeric_limits<std::size_t>::max()
                || outbox > std::numeric_limits<std::size_t>::max()
                || participants > std::numeric_limits<std::size_t>::max()) {
                return false;
            }
            restoredConfig.maxTrackedSenders = static_cast<std::size_t>(tracked);
            restoredConfig.maxOutboxEntries = static_cast<std::size_t>(outbox);
            restoredConfig.maxParticipants = static_cast<std::size_t>(participants);
            if (!validConfig(restoredConfig, m_authority, true))
                return false;
            sawConfig = true;
            continue;
        }

        sawNonConfig = true;
        if (!sawConfig)
            return false;
        if (fields[0] == "ingress" || fields[0] == "egress") {
            if (fields.size() != 3U)
                return false;
            std::string senderKey;
            std::uint64_t sequence = 0U;
            if (!hexDecode(fields[1], senderKey)
                || !parseInteger(fields[2], sequence)
                || sequence == 0U) {
                return false;
            }
            auto& sequences = fields[0] == "ingress"
                ? restoredIngressState.lastSequence
                : restoredEgressState.lastSequence;
            if (!sequences.emplace(std::move(senderKey), sequence).second)
                return false;
            continue;
        }
        if (fields[0] != "outbox" || fields.size() != 7U)
            return false;

        std::string requestId;
        unsigned int stage = 0U;
        OutboxEntry entry;
        if (!hexDecode(fields[1], requestId)
            || !isIdentifier(requestId)
            || !parseInteger(fields[2], stage)
            || stage > static_cast<unsigned int>(
                DeliveryOutboxStage::PendingSend)
            || !parseInteger(fields[3], entry.senderSequence)
            || entry.senderSequence == 0U
            || !parseInteger(fields[4], entry.expiresAt)
            || entry.expiresAt <= 0
            || !hexDecode(fields[5], entry.contentTopic)
            || !hexDecode(fields[6], entry.payload)
            || entry.payload.empty()
            || entry.payload.size() > kMaximumEncodedEnvelopeBytes) {
            return false;
        }
        entry.stage = static_cast<DeliveryOutboxStage>(stage);
        const DeliveryEnvelopeDecode decoded = decodeDeliveryEnvelope(
            std::string(entry.payload.begin(), entry.payload.end()));
        if (!decoded.accepted
            || entry.contentTopic != deriveRoomTopic(
                restoredConfig.networkId, restoredConfig.palaceId,
                restoredConfig.roomId, restoredConfig.roomEpoch)
            || decoded.envelope.networkId != restoredConfig.networkId
            || decoded.envelope.palaceId != restoredConfig.palaceId
            || decoded.envelope.roomId != restoredConfig.roomId
            || decoded.envelope.roomEpoch != restoredConfig.roomEpoch
            || decoded.envelope.senderUserId != restoredConfig.senderUserId
            || decoded.envelope.senderKeyEpoch != restoredConfig.senderKeyEpoch
            || decoded.envelope.senderSequence != entry.senderSequence
            || decoded.envelope.expiresAt != entry.expiresAt
            || decoded.envelope.createdAt <= 0
            || decoded.envelope.expiresAt < decoded.envelope.createdAt
            || decoded.envelope.expiresAt - decoded.envelope.createdAt > 300
            || decoded.envelope.signature.empty()
            || !outboxSequences.insert(entry.senderSequence).second
            || !restoredOutbox.emplace(std::move(requestId), std::move(entry)).second) {
            return false;
        }
    }

    if (!sawConfig
        || restoredOutbox.size() > restoredConfig.maxOutboxEntries) {
        return false;
    }
    const std::string localSenderKey = restoredConfig.senderUserId
        + "@" + std::to_string(restoredConfig.senderKeyEpoch);
    if (restoredEgressState.lastSequence.size() > 1U
        || (!restoredEgressState.lastSequence.empty()
            && restoredEgressState.lastSequence.begin()->first != localSenderKey)) {
        return false;
    }
    const auto localSequence = restoredEgressState.lastSequence.find(localSenderKey);
    if (!restoredOutbox.empty()
        && (localSequence == restoredEgressState.lastSequence.end()
            || localSequence->second < *outboxSequences.rbegin())) {
        return false;
    }

    DeliveryIngress restoredIngress;
    DeliveryEgress restoredEgress;
    if (!restoredIngress.restoreSequenceState(
            restoredIngressState, restoredConfig.maxTrackedSenders)
        || !restoredEgress.restoreSequenceState(
            restoredEgressState, restoredConfig.maxTrackedSenders)) {
        return false;
    }

    m_config = std::move(restoredConfig);
    m_configured = true;
    m_state = DeliverySessionState::Recovering;
    m_connectionState = DeliveryConnectionState::Disconnected;
    m_nodeStarted = false;
    m_subscriptionPending = false;
    m_subscribed = false;
    m_ingress = std::move(restoredIngress);
    m_egress = std::move(restoredEgress);
    m_allowedProps.clear();
    m_outbox = std::move(restoredOutbox);
    m_participants.clear();
    clearPendingIngress();
    return true;
}

DeliveryPolicy PalaceDeliverySession::policy(std::int64_t now) const
{
    DeliveryPolicy configuredPolicy;
    configuredPolicy.networkId = m_config.networkId;
    configuredPolicy.palaceId = m_config.palaceId;
    configuredPolicy.roomId = m_config.roomId;
    configuredPolicy.roomEpoch = m_config.roomEpoch;
    configuredPolicy.now = now;
    configuredPolicy.minMotionIntervalSeconds = 1;
    configuredPolicy.maxTrackedSenders = m_config.maxTrackedSenders;
    configuredPolicy.allowedProps = m_allowedProps;
    configuredPolicy.authority = &m_authority;
    return configuredPolicy;
}

DeliverySessionTransition PalaceDeliverySession::subscribeIfReady()
{
    if (!m_nodeStarted || m_connectionState != DeliveryConnectionState::Connected) {
        if (m_nodeStarted)
            m_state = DeliverySessionState::WaitingForConnection;
        return transition(true, "waiting-for-node-and-connection");
    }
    if (m_subscribed || m_subscriptionPending)
        return transition(true, "subscription-already-active");

    m_subscriptionPending = true;
    m_state = DeliverySessionState::Subscribing;
    return commandTransition(true, "subscribe", subscribeCommand());
}

DeliverySessionCommand PalaceDeliverySession::subscribeCommand() const
{
    DeliverySessionCommand command;
    command.kind = DeliverySessionCommandKind::Subscribe;
    command.contentTopic = deriveRoomTopic(
        m_config.networkId, m_config.palaceId, m_config.roomId,
        m_config.roomEpoch);
    return command;
}

DeliverySessionCommand PalaceDeliverySession::sendCommand(
    const std::string& requestId,
    const OutboxEntry& entry) const
{
    DeliverySessionCommand command;
    command.kind = DeliverySessionCommandKind::SendMessage;
    command.requestId = requestId;
    command.contentTopic = entry.contentTopic;
    command.payload = entry.payload;
    return command;
}

bool PalaceDeliverySession::wouldExceedParticipantLimit(
    const PalaceDeliveryEnvelopeV1& envelope) const
{
    return requiresParticipantProjection(envelope.kind)
        && m_participants.find(envelope.senderUserId) == m_participants.end()
        && m_participants.size() >= m_config.maxParticipants;
}

DeliveryValidation PalaceDeliverySession::bufferPendingIngress(
    const std::string& senderKey,
    const PalaceDeliveryEnvelopeV1& envelope,
    std::size_t encodedBytes)
{
    if (m_pendingIngressCount >= kMaximumPendingIngressCount)
        return {false, "reorder-buffer-count-exceeded"};
    if (encodedBytes > kMaximumPendingIngressBytes
        || m_pendingIngressBytes
            > kMaximumPendingIngressBytes - encodedBytes) {
        return {false, "reorder-buffer-bytes-exceeded"};
    }

    auto& pending = m_pendingIngress[senderKey];
    const auto inserted = pending.emplace(
        envelope.senderSequence,
        PendingIngressEntry{envelope, encodedBytes});
    if (!inserted.second)
        return {false, "duplicate-or-replayed-sequence"};
    ++m_pendingIngressCount;
    m_pendingIngressBytes += encodedBytes;
    return {true, "buffered-out-of-order"};
}

void PalaceDeliverySession::discardPendingIngressThrough(
    const std::string& senderKey,
    std::uint64_t sequence)
{
    const auto sender = m_pendingIngress.find(senderKey);
    if (sender == m_pendingIngress.end())
        return;

    auto pending = sender->second.begin();
    while (pending != sender->second.end()
           && pending->first <= sequence) {
        --m_pendingIngressCount;
        m_pendingIngressBytes -= pending->second.encodedBytes;
        pending = sender->second.erase(pending);
    }
    if (sender->second.empty())
        m_pendingIngress.erase(sender);
}

bool PalaceDeliverySession::drainPendingIngress(
    const std::string& senderKey,
    const DeliveryPolicy& currentPolicy)
{
    bool projectionChanged = false;
    while (true) {
        const auto sender = m_pendingIngress.find(senderKey);
        if (sender == m_pendingIngress.end())
            return projectionChanged;

        const PalaceDeliveryEnvelopeV1& firstEnvelope =
            sender->second.begin()->second.envelope;
        const std::uint64_t committed = m_ingress.lastSequenceFor(
            firstEnvelope.senderUserId, firstEnvelope.senderKeyEpoch);
        if (committed == std::numeric_limits<std::uint64_t>::max())
            return projectionChanged;

        const auto pending = sender->second.find(committed + 1U);
        if (pending == sender->second.end())
            return projectionChanged;
        if (pending->second.envelope.expiresAt < currentPolicy.now) {
            --m_pendingIngressCount;
            m_pendingIngressBytes -= pending->second.encodedBytes;
            sender->second.erase(pending);
            if (sender->second.empty())
                m_pendingIngress.erase(sender);
            return projectionChanged;
        }
        if (wouldExceedParticipantLimit(pending->second.envelope))
            return projectionChanged;

        const DeliveryValidation committedEnvelope =
            m_ingress.commitValidated(
                pending->second.envelope, currentPolicy);
        if (!committedEnvelope.accepted) {
            --m_pendingIngressCount;
            m_pendingIngressBytes -= pending->second.encodedBytes;
            sender->second.erase(pending);
            if (sender->second.empty())
                m_pendingIngress.erase(sender);
            return projectionChanged;
        }
        projectionChanged =
            apply(pending->second.envelope) || projectionChanged;
        --m_pendingIngressCount;
        m_pendingIngressBytes -= pending->second.encodedBytes;
        sender->second.erase(pending);
        if (sender->second.empty())
            m_pendingIngress.erase(sender);
    }
}

void PalaceDeliverySession::clearPendingIngress()
{
    m_pendingIngress.clear();
    m_pendingIngressCount = 0U;
    m_pendingIngressBytes = 0U;
}

bool PalaceDeliverySession::apply(const PalaceDeliveryEnvelopeV1& envelope)
{
    if (envelope.kind == DeliveryKind::PresenceBye)
        return m_participants.erase(envelope.senderUserId) == 1U;

    auto participant = m_participants.find(envelope.senderUserId);
    if (envelope.kind == DeliveryKind::PresenceHello) {
        const bool inserted = participant == m_participants.end();
        if (inserted) {
            DeliveryParticipantProjectionV1 projection;
            projection.userId = envelope.senderUserId;
            participant = m_participants.emplace(
                envelope.senderUserId, std::move(projection)).first;
        }
        DeliveryParticipantProjectionV1& projection = participant->second;
        const bool changed = inserted || !projection.present
            || projection.displayName != envelope.payload
            || projection.presenceExpiresAt != envelope.expiresAt;
        projection.present = true;
        projection.displayName = envelope.payload;
        projection.presenceExpiresAt = envelope.expiresAt;
        return changed;
    }

    if (participant == m_participants.end()) {
        if (!requiresParticipantProjection(envelope.kind))
            return false;
        DeliveryParticipantProjectionV1 projection;
        projection.userId = envelope.senderUserId;
        participant = m_participants.emplace(
            envelope.senderUserId, std::move(projection)).first;
    }
    DeliveryParticipantProjectionV1& projection = participant->second;
    switch (envelope.kind) {
    case DeliveryKind::Motion: {
        std::int64_t x = 0;
        std::int64_t y = 0;
        if (!parseMotion(envelope.payload, x, y))
            return false;
        const bool changed = !projection.hasMotion
            || projection.motionX != x || projection.motionY != y
            || projection.motionExpiresAt != envelope.expiresAt;
        projection.hasMotion = true;
        projection.motionX = x;
        projection.motionY = y;
        projection.motionExpiresAt = envelope.expiresAt;
        return changed;
    }
    case DeliveryKind::Speech: {
        const bool changed = projection.speech != envelope.payload
            || projection.speechExpiresAt != envelope.expiresAt;
        projection.speech = envelope.payload;
        projection.speechExpiresAt = envelope.expiresAt;
        return changed;
    }
    case DeliveryKind::WearProp:
        return projection.propIds.insert(envelope.payload).second;
    case DeliveryKind::RemoveProp:
        return projection.propIds.erase(envelope.payload) == 1U;
    case DeliveryKind::PresenceHello:
    case DeliveryKind::PresenceBye:
    case DeliveryKind::SpotIntentNotice:
    case DeliveryKind::AuthorityRefreshNotice:
        return false;
    }
    return false;
}

DeliverySessionStore::DeliverySessionStore(std::string directory)
    : m_directory(std::move(directory))
{
}

bool DeliverySessionStore::save(const PalaceDeliverySession& session) const
{
    const std::string state = session.canonicalState();
    if (m_directory.empty() || state.empty() || state.size() > kMaximumStateBytes)
        return false;

    std::error_code error;
    const fs::path directory(m_directory);
    fs::create_directories(directory, error);
    if (error)
        return false;

    const fs::path temporary = directory / kTemporaryFileName;
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << crypto::sha256Hex(state) << '\n' << state;
        output.flush();
        if (!output.good())
            return false;
    }
    if (!flushFile(temporary))
        return false;

    const fs::path destination = directory / kSessionFileName;
    fs::rename(temporary, destination, error);
    if (error)
        return false;
    return flushDirectory(directory);
}

bool DeliverySessionStore::load(PalaceDeliverySession& session) const
{
    if (m_directory.empty())
        return false;
    std::ifstream input(
        fs::path(m_directory) / kSessionFileName, std::ios::binary);
    if (!input)
        return false;
    const std::string record(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    std::string state;
    return parseRecord(record, state) && session.restoreCanonicalState(state);
}

bool DeliverySessionStore::exists() const
{
    if (m_directory.empty())
        return false;
    std::error_code error;
    const bool present = fs::exists(
        fs::path(m_directory) / kSessionFileName, error);
    return !error && present;
}

} // namespace palace
