#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "palace_authority.h"
#include "palace_delivery.h"

namespace palace {

enum class DeliverySessionState {
    Unconfigured,
    Offline,
    AwaitingCallbacks,
    StartingNode,
    WaitingForConnection,
    Subscribing,
    Online,
    Recovering,
};

enum class DeliveryConnectionState {
    Disconnected,
    Connecting,
    Connected,
};

enum class DeliverySessionCommandKind {
    RegisterCallbacks,
    StartNode,
    Subscribe,
    SendMessage,
};

enum class DeliveryOutboxStage {
    PendingSend,
    Sent,
};

struct DeliverySessionConfigV1 {
    std::string networkId;
    std::string palaceId;
    std::string roomId;
    std::int64_t roomEpoch = 0;
    std::string senderUserId;
    std::int64_t senderKeyEpoch = 0;
    std::size_t maxTrackedSenders = 512;
    std::size_t maxOutboxEntries = 128;
    std::size_t maxParticipants = 256;
};

struct DeliverySessionCommand {
    DeliverySessionCommandKind kind = DeliverySessionCommandKind::RegisterCallbacks;
    std::string requestId;
    std::string contentTopic;
    std::vector<std::uint8_t> payload;
};

struct DeliverySessionTransition {
    bool accepted = false;
    std::string reason;
    std::vector<DeliverySessionCommand> commands;
};

struct DeliveryParticipantProjectionV1 {
    std::string userId;
    bool present = false;
    std::string displayName;
    std::int64_t presenceExpiresAt = 0;
    bool hasMotion = false;
    std::int64_t motionX = 0;
    std::int64_t motionY = 0;
    std::int64_t motionExpiresAt = 0;
    std::string speech;
    std::int64_t speechExpiresAt = 0;
    std::set<std::string> propIds;
};

struct DeliverySessionReceive {
    bool accepted = false;
    bool projectionChanged = false;
    std::string reason;
};

struct DeliveryOutboxStatus {
    bool found = false;
    DeliveryOutboxStage stage = DeliveryOutboxStage::PendingSend;
    std::uint64_t senderSequence = 0;
    std::int64_t expiresAt = 0;
};

std::string deliverySessionStateName(DeliverySessionState state);

// Transport-agnostic owner of Delivery lifecycle and live room projection.
// Commands are intentionally small: an adapter performs them, then reports
// only typed completion events back to this boundary.
class PalaceDeliverySession {
public:
    explicit PalaceDeliverySession(const AuthorityProjection& authority);

    bool configure(const DeliverySessionConfigV1& config);
    bool hasConfiguration() const;
    const DeliverySessionConfigV1& configuration() const;

    DeliverySessionTransition start();
    DeliverySessionTransition callbacksRegistered(bool succeeded);
    DeliverySessionTransition nodeStarted(bool succeeded);
    DeliverySessionTransition connectionStateChanged(DeliveryConnectionState state);
    DeliverySessionTransition subscriptionResult(bool succeeded);
    void interrupt(bool recoverable);

    DeliverySessionState state() const;
    DeliveryConnectionState connectionState() const;

    void replaceAllowedProps(std::map<std::string, std::string> allowedProps);

    DeliverySessionTransition publish(const std::string& requestId,
                                      DeliveryKind kind,
                                      const std::string& payload,
                                      std::int64_t now,
                                      std::int64_t lifetimeSeconds,
                                      const DeliverySignatureSigner& signer,
                                      const DeliverySignatureVerifier& verifier);
    bool messageSent(const std::string& requestId);
    bool messagePropagated(const std::string& requestId);
    bool messageError(const std::string& requestId, std::int64_t now);
    std::vector<DeliverySessionCommand> eligibleOutboxCommands(std::int64_t now);
    DeliveryOutboxStatus outboxStatus(const std::string& requestId) const;
    std::size_t outboxSize() const;

    DeliverySessionReceive receive(const std::string& contentTopic,
                                   const std::vector<std::uint8_t>& payload,
                                   std::int64_t now,
                                   const DeliverySignatureVerifier& verifier);
    void expireTransient(std::int64_t now);
    std::vector<DeliveryParticipantProjectionV1> participantSnapshot() const;

    // Versioned, deterministic restart state. Live participant presence,
    // motion, and speech are deliberately excluded.
    std::string canonicalState() const;
    bool restoreCanonicalState(const std::string& serialized);

private:
    struct OutboxEntry {
        DeliveryOutboxStage stage = DeliveryOutboxStage::PendingSend;
        std::string contentTopic;
        std::vector<std::uint8_t> payload;
        std::uint64_t senderSequence = 0;
        std::int64_t expiresAt = 0;
    };

    DeliveryPolicy policy(std::int64_t now) const;
    DeliverySessionTransition subscribeIfReady();
    DeliverySessionCommand subscribeCommand() const;
    DeliverySessionCommand sendCommand(const std::string& requestId,
                                       const OutboxEntry& entry) const;
    bool wouldExceedParticipantLimit(const PalaceDeliveryEnvelopeV1& envelope) const;
    bool apply(const PalaceDeliveryEnvelopeV1& envelope);

    const AuthorityProjection& m_authority;
    DeliverySessionConfigV1 m_config;
    bool m_configured = false;
    DeliverySessionState m_state = DeliverySessionState::Unconfigured;
    DeliveryConnectionState m_connectionState = DeliveryConnectionState::Disconnected;
    bool m_nodeStarted = false;
    bool m_subscriptionPending = false;
    bool m_subscribed = false;
    DeliveryIngress m_ingress;
    DeliveryEgress m_egress;
    std::map<std::string, std::string> m_allowedProps;
    std::map<std::string, OutboxEntry> m_outbox;
    std::map<std::string, DeliveryParticipantProjectionV1> m_participants;
};

// Crash-safe file boundary for Delivery session restart state.
class DeliverySessionStore {
public:
    explicit DeliverySessionStore(std::string directory);

    bool save(const PalaceDeliverySession& session) const;
    bool load(PalaceDeliverySession& session) const;
    bool exists() const;

private:
    std::string m_directory;
};

} // namespace palace
