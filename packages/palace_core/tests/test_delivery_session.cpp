#include <logos_test.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "palace_authority.h"
#include "palace_delivery.h"
#include "palace_delivery_session.h"

namespace {

palace::AuthoritySnapshotV1 authoritySnapshot()
{
    palace::AuthoritySnapshotV1 snapshot;
    snapshot.palaceId = "palace-1";
    snapshot.ownerUserId = "alice";
    snapshot.entryRoomId = "atrium";
    snapshot.users = {
        {"alice", "alice-key", 1},
        {"bob", "bob-key", 2},
        {"carol", "carol-key", 3},
    };
    snapshot.rooms = {
        {"atrium", false, "state-root-atrium", 9},
        {"lounge", false, "state-root-lounge", 4},
    };
    return snapshot;
}

palace::DeliverySessionConfigV1 sessionConfig()
{
    palace::DeliverySessionConfigV1 config;
    config.networkId = "logos.test";
    config.palaceId = "palace-1";
    config.roomId = "atrium";
    config.roomEpoch = 9;
    config.senderUserId = "carol";
    config.senderKeyEpoch = 3;
    return config;
}

class CanonicalVerifier final : public palace::DeliverySignatureVerifier {
public:
    bool verify(const std::string& publicKey,
                const std::string& canonicalEnvelope,
                const std::string& signature) const override
    {
        return signature == publicKey + ":" + canonicalEnvelope;
    }
};

class CanonicalSigner final : public palace::DeliverySignatureSigner {
public:
    explicit CanonicalSigner(std::string key)
        : m_key(std::move(key))
    {
    }

    std::string publicKey() const override { return m_key; }
    std::string sign(const std::string& canonicalEnvelope) const override
    {
        return m_key + ":" + canonicalEnvelope;
    }

private:
    std::string m_key;
};

palace::PalaceDeliveryEnvelopeV1 envelope(
    const std::string& userId,
    const std::string& key,
    std::int64_t keyEpoch,
    std::uint64_t sequence,
    palace::DeliveryKind kind,
    const std::string& payload,
    std::int64_t createdAt = 1040,
    std::int64_t expiresAt = 1100)
{
    palace::PalaceDeliveryEnvelopeV1 message;
    message.networkId = "logos.test";
    message.palaceId = "palace-1";
    message.roomId = "atrium";
    message.roomEpoch = 9;
    message.kind = kind;
    message.senderUserId = userId;
    message.senderKeyEpoch = keyEpoch;
    message.senderSequence = sequence;
    message.createdAt = createdAt;
    message.expiresAt = expiresAt;
    message.payload = payload;
    message.signature = key + ":" + palace::canonicalDeliveryEnvelope(message);
    return message;
}

std::vector<std::uint8_t> wire(const palace::PalaceDeliveryEnvelopeV1& message)
{
    const std::string encoded = palace::encodeDeliveryEnvelope(message);
    return std::vector<std::uint8_t>(encoded.begin(), encoded.end());
}

std::string roomTopic()
{
    return palace::deriveRoomTopic("logos.test", "palace-1", "atrium", 9);
}

void bringOnline(palace::PalaceDeliverySession& session)
{
    const palace::DeliverySessionTransition begin = session.start();
    LOGOS_ASSERT_TRUE(begin.accepted);
    LOGOS_ASSERT_EQ(begin.commands.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(static_cast<int>(begin.commands.front().kind),
                    static_cast<int>(palace::DeliverySessionCommandKind::RegisterCallbacks));

    const palace::DeliverySessionTransition callbacks =
        session.callbacksRegistered(true);
    LOGOS_ASSERT_TRUE(callbacks.accepted);
    LOGOS_ASSERT_EQ(static_cast<int>(callbacks.commands.front().kind),
                    static_cast<int>(palace::DeliverySessionCommandKind::StartNode));

    LOGOS_ASSERT_TRUE(session.nodeStarted(true).accepted);
    const palace::DeliverySessionTransition connected =
        session.connectionStateChanged(
            palace::DeliveryConnectionState::Connected);
    LOGOS_ASSERT_TRUE(connected.accepted);
    LOGOS_ASSERT_EQ(connected.commands.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(static_cast<int>(connected.commands.front().kind),
                    static_cast<int>(palace::DeliverySessionCommandKind::Subscribe));
    LOGOS_ASSERT_EQ(connected.commands.front().contentTopic, roomTopic());
    LOGOS_ASSERT_TRUE(session.subscriptionResult(true).accepted);
    LOGOS_ASSERT_EQ(palace::deliverySessionStateName(session.state()),
                    std::string("online"));
}

} // namespace

LOGOS_TEST(delivery_session_orders_callbacks_start_connection_and_subscription) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::PalaceDeliverySession session(authority);
    palace::DeliverySessionConfigV1 unbound = sessionConfig();
    unbound.senderKeyEpoch = 4;
    LOGOS_ASSERT_FALSE(session.configure(unbound));
    LOGOS_ASSERT_TRUE(session.configure(sessionConfig()));

    const palace::DeliverySessionTransition begin = session.start();
    LOGOS_ASSERT_EQ(begin.commands.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(static_cast<int>(begin.commands.front().kind),
                    static_cast<int>(palace::DeliverySessionCommandKind::RegisterCallbacks));

    const palace::DeliverySessionTransition earlyConnection =
        session.connectionStateChanged(
            palace::DeliveryConnectionState::Connected);
    LOGOS_ASSERT_TRUE(earlyConnection.accepted);
    LOGOS_ASSERT_TRUE(earlyConnection.commands.empty());
    LOGOS_ASSERT_EQ(palace::deliverySessionStateName(session.state()),
                    std::string("awaiting_callbacks"));

    const palace::DeliverySessionTransition callbacks =
        session.callbacksRegistered(true);
    LOGOS_ASSERT_EQ(callbacks.commands.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(static_cast<int>(callbacks.commands.front().kind),
                    static_cast<int>(palace::DeliverySessionCommandKind::StartNode));

    const palace::DeliverySessionTransition started = session.nodeStarted(true);
    LOGOS_ASSERT_EQ(started.commands.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(static_cast<int>(started.commands.front().kind),
                    static_cast<int>(palace::DeliverySessionCommandKind::Subscribe));
    LOGOS_ASSERT_TRUE(session.subscriptionResult(true).accepted);

    session.interrupt(true);
    LOGOS_ASSERT_EQ(palace::deliverySessionStateName(session.state()),
                    std::string("recovering"));
    session.interrupt(false);
    LOGOS_ASSERT_EQ(palace::deliverySessionStateName(session.state()),
                    std::string("offline"));
}

LOGOS_TEST(delivery_session_maps_signed_sends_to_bounded_correlated_outbox) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::DeliverySessionConfigV1 config = sessionConfig();
    config.maxOutboxEntries = 1;
    palace::PalaceDeliverySession session(authority);
    LOGOS_ASSERT_TRUE(session.configure(config));
    bringOnline(session);

    CanonicalVerifier verifier;
    CanonicalSigner signer("carol-key");
    const palace::DeliverySessionTransition published = session.publish(
        "request-1", palace::DeliveryKind::Speech, "hello", 1050, 30,
        signer, verifier);
    LOGOS_ASSERT_TRUE(published.accepted);
    LOGOS_ASSERT_EQ(published.commands.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(published.commands.front().requestId,
                    std::string("request-1"));
    const palace::DeliveryEnvelopeDecode decoded =
        palace::decodeDeliveryEnvelope(std::string(
            published.commands.front().payload.begin(),
            published.commands.front().payload.end()));
    LOGOS_ASSERT_TRUE(decoded.accepted);
    LOGOS_ASSERT_EQ(decoded.envelope.senderSequence,
                    static_cast<std::uint64_t>(1));
    LOGOS_ASSERT_EQ(decoded.envelope.signature,
                    std::string("carol-key:")
                        + palace::canonicalDeliveryEnvelope(decoded.envelope));

    LOGOS_ASSERT_FALSE(session.messageSent("unknown"));
    LOGOS_ASSERT_FALSE(session.messagePropagated("unknown"));
    LOGOS_ASSERT_FALSE(session.messageError("unknown", 1051));
    LOGOS_ASSERT_TRUE(session.messageSent("request-1"));
    LOGOS_ASSERT_EQ(static_cast<int>(session.outboxStatus("request-1").stage),
                    static_cast<int>(palace::DeliveryOutboxStage::Sent));
    LOGOS_ASSERT_TRUE(session.messageError("request-1", 1051));
    LOGOS_ASSERT_EQ(static_cast<int>(session.outboxStatus("request-1").stage),
                    static_cast<int>(palace::DeliveryOutboxStage::PendingSend));
    LOGOS_ASSERT_EQ(session.eligibleOutboxCommands(1051).size(),
                    static_cast<std::size_t>(1));

    LOGOS_ASSERT_FALSE(session.publish(
        "request-full", palace::DeliveryKind::Speech, "blocked", 1051, 30,
        signer, verifier).accepted);
    LOGOS_ASSERT_TRUE(session.messagePropagated("request-1"));
    LOGOS_ASSERT_FALSE(session.outboxStatus("request-1").found);

    const palace::DeliverySessionTransition second = session.publish(
        "request-2", palace::DeliveryKind::Speech, "again", 1052, 30,
        signer, verifier);
    LOGOS_ASSERT_TRUE(second.accepted);
    const palace::DeliveryEnvelopeDecode secondDecoded =
        palace::decodeDeliveryEnvelope(std::string(
            second.commands.front().payload.begin(),
            second.commands.front().payload.end()));
    LOGOS_ASSERT_EQ(secondDecoded.envelope.senderSequence,
                    static_cast<std::uint64_t>(2));
}

LOGOS_TEST(delivery_session_validates_before_live_projection_mutation) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::PalaceDeliverySession session(authority);
    LOGOS_ASSERT_TRUE(session.configure(sessionConfig()));
    session.replaceAllowedProps({{"hat", "cid-hat"}});
    bringOnline(session);
    CanonicalVerifier verifier;

    LOGOS_ASSERT_TRUE(session.receive(
        roomTopic(), wire(envelope(
            "carol", "carol-key", 3, 1,
            palace::DeliveryKind::PresenceHello, "Carol")),
        1050, verifier).projectionChanged);
    LOGOS_ASSERT_TRUE(session.receive(
        roomTopic(), wire(envelope(
            "carol", "carol-key", 3, 2,
            palace::DeliveryKind::Motion, "x=25;y=50")),
        1050, verifier).projectionChanged);
    LOGOS_ASSERT_TRUE(session.receive(
        roomTopic(), wire(envelope(
            "carol", "carol-key", 3, 3,
            palace::DeliveryKind::Speech, "hello")),
        1050, verifier).projectionChanged);
    LOGOS_ASSERT_TRUE(session.receive(
        roomTopic(), wire(envelope(
            "carol", "carol-key", 3, 4,
            palace::DeliveryKind::WearProp, "hat")),
        1050, verifier).projectionChanged);
    LOGOS_ASSERT_TRUE(session.receive(
        roomTopic(), wire(envelope(
            "carol", "carol-key", 3, 5,
            palace::DeliveryKind::RemoveProp, "hat")),
        1050, verifier).projectionChanged);

    std::vector<palace::DeliveryParticipantProjectionV1> snapshot =
        session.participantSnapshot();
    LOGOS_ASSERT_EQ(snapshot.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_TRUE(snapshot.front().present);
    LOGOS_ASSERT_TRUE(snapshot.front().hasMotion);
    LOGOS_ASSERT_EQ(snapshot.front().motionX, static_cast<std::int64_t>(25));
    LOGOS_ASSERT_EQ(snapshot.front().motionY, static_cast<std::int64_t>(50));
    LOGOS_ASSERT_EQ(snapshot.front().speech, std::string("hello"));
    LOGOS_ASSERT_TRUE(snapshot.front().propIds.empty());

    LOGOS_ASSERT_EQ(session.receive(
        roomTopic(), wire(envelope(
            "carol", "carol-key", 3, 5,
            palace::DeliveryKind::Speech, "duplicate")),
        1050, verifier).reason, std::string("duplicate-or-replayed-sequence"));

    palace::PalaceDeliveryEnvelopeV1 expired = envelope(
        "carol", "carol-key", 3, 6, palace::DeliveryKind::Speech,
        "expired", 1000, 1049);
    LOGOS_ASSERT_EQ(session.receive(
        roomTopic(), wire(expired), 1050, verifier).reason,
        std::string("expired-or-invalid-time"));

    palace::PalaceDeliveryEnvelopeV1 badSignature = envelope(
        "carol", "carol-key", 3, 6, palace::DeliveryKind::Speech,
        "forged");
    badSignature.signature = "forged";
    LOGOS_ASSERT_EQ(session.receive(
        roomTopic(), wire(badSignature), 1050, verifier).reason,
        std::string("bad-signature-or-key-binding"));

    palace::PalaceDeliveryEnvelopeV1 wrongEpoch = envelope(
        "carol", "carol-key", 3, 6, palace::DeliveryKind::Speech,
        "wrong epoch");
    wrongEpoch.roomEpoch = 8;
    wrongEpoch.signature = "carol-key:"
        + palace::canonicalDeliveryEnvelope(wrongEpoch);
    LOGOS_ASSERT_EQ(session.receive(
        roomTopic(), wire(wrongEpoch), 1050, verifier).reason,
        std::string("wrong-palace-room-or-epoch"));

    palace::PalaceDeliveryEnvelopeV1 outOfBounds = envelope(
        "carol", "carol-key", 3, 6, palace::DeliveryKind::Motion,
        "x=10001;y=50");
    LOGOS_ASSERT_EQ(session.receive(
        roomTopic(), wire(outOfBounds), 1050, verifier).reason,
        std::string("invalid-or-banned-payload"));

    snapshot = session.participantSnapshot();
    LOGOS_ASSERT_EQ(snapshot.front().motionX, static_cast<std::int64_t>(25));
    LOGOS_ASSERT_EQ(snapshot.front().speech, std::string("hello"));
    LOGOS_ASSERT_TRUE(session.receive(
        roomTopic(), wire(envelope(
            "carol", "carol-key", 3, 6,
            palace::DeliveryKind::Speech, "accepted after rejects")),
        1050, verifier).accepted);

    session.expireTransient(1101);
    LOGOS_ASSERT_TRUE(session.participantSnapshot().empty());
}

LOGOS_TEST(delivery_session_bounds_replay_senders_without_advancing_on_rejection) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::DeliverySessionConfigV1 config = sessionConfig();
    config.maxTrackedSenders = 1;
    palace::PalaceDeliverySession session(authority);
    LOGOS_ASSERT_TRUE(session.configure(config));
    bringOnline(session);
    CanonicalVerifier verifier;

    LOGOS_ASSERT_TRUE(session.receive(
        roomTopic(), wire(envelope(
            "carol", "carol-key", 3, 1,
            palace::DeliveryKind::PresenceHello, "Carol")),
        1050, verifier).accepted);
    LOGOS_ASSERT_EQ(session.receive(
        roomTopic(), wire(envelope(
            "bob", "bob-key", 2, 1,
            palace::DeliveryKind::PresenceHello, "Bob")),
        1050, verifier).reason, std::string("replay-state-capacity-exceeded"));
    LOGOS_ASSERT_EQ(session.participantSnapshot().size(),
                    static_cast<std::size_t>(1));
}

LOGOS_TEST(delivery_session_restart_restores_sequences_outbox_not_live_presence) {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path()
        / "logos-palace-delivery-session-contract";
    std::filesystem::remove_all(directory);

    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::PalaceDeliverySession original(authority);
    LOGOS_ASSERT_TRUE(original.configure(sessionConfig()));
    bringOnline(original);
    CanonicalVerifier verifier;
    CanonicalSigner signer("carol-key");

    LOGOS_ASSERT_TRUE(original.receive(
        roomTopic(), wire(envelope(
            "bob", "bob-key", 2, 1,
            palace::DeliveryKind::PresenceHello, "Bob")),
        1050, verifier).accepted);
    LOGOS_ASSERT_TRUE(original.publish(
        "request-1", palace::DeliveryKind::Speech, "one", 1050, 100,
        signer, verifier).accepted);
    LOGOS_ASSERT_TRUE(original.messageSent("request-1"));
    LOGOS_ASSERT_TRUE(original.publish(
        "request-2", palace::DeliveryKind::Speech, "two", 1051, 100,
        signer, verifier).accepted);

    palace::DeliverySessionStore store(directory.string());
    LOGOS_ASSERT_TRUE(store.save(original));
    LOGOS_ASSERT_TRUE(store.exists());

    palace::PalaceDeliverySession restored(authority);
    LOGOS_ASSERT_TRUE(store.load(restored));
    LOGOS_ASSERT_EQ(palace::deliverySessionStateName(restored.state()),
                    std::string("recovering"));
    LOGOS_ASSERT_EQ(restored.configuration().roomId, std::string("atrium"));
    LOGOS_ASSERT_EQ(restored.configuration().roomEpoch,
                    static_cast<std::int64_t>(9));
    LOGOS_ASSERT_TRUE(restored.participantSnapshot().empty());
    LOGOS_ASSERT_EQ(restored.outboxSize(), static_cast<std::size_t>(2));
    LOGOS_ASSERT_EQ(static_cast<int>(restored.outboxStatus("request-1").stage),
                    static_cast<int>(palace::DeliveryOutboxStage::Sent));

    bringOnline(restored);
    LOGOS_ASSERT_EQ(restored.eligibleOutboxCommands(1060).size(),
                    static_cast<std::size_t>(2));
    LOGOS_ASSERT_EQ(restored.receive(
        roomTopic(), wire(envelope(
            "bob", "bob-key", 2, 1,
            palace::DeliveryKind::PresenceHello, "Bob")),
        1060, verifier).reason, std::string("duplicate-or-replayed-sequence"));

    const palace::DeliverySessionTransition third = restored.publish(
        "request-3", palace::DeliveryKind::Speech, "three", 1060, 100,
        signer, verifier);
    LOGOS_ASSERT_TRUE(third.accepted);
    const palace::DeliveryEnvelopeDecode decoded =
        palace::decodeDeliveryEnvelope(std::string(
            third.commands.front().payload.begin(),
            third.commands.front().payload.end()));
    LOGOS_ASSERT_EQ(decoded.envelope.senderSequence,
                    static_cast<std::uint64_t>(3));
    std::filesystem::remove_all(directory);
}

LOGOS_TEST(delivery_session_corrupt_restore_fails_without_partial_mutation) {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path()
        / "logos-palace-delivery-session-corrupt-contract";
    std::filesystem::remove_all(directory);

    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::PalaceDeliverySession session(authority);
    LOGOS_ASSERT_TRUE(session.configure(sessionConfig()));
    const std::string safeState = session.canonicalState();
    LOGOS_ASSERT_FALSE(session.restoreCanonicalState(
        safeState + "ingress;zz;1\n"));
    LOGOS_ASSERT_EQ(session.canonicalState(), safeState);
    LOGOS_ASSERT_EQ(palace::deliverySessionStateName(session.state()),
                    std::string("offline"));

    palace::DeliverySessionStore store(directory.string());
    LOGOS_ASSERT_TRUE(store.save(session));
    {
        std::ofstream output(
            directory / "delivery-session-v1",
            std::ios::binary | std::ios::trunc);
        output << std::string(64U, '0') << '\n' << safeState;
    }
    LOGOS_ASSERT_FALSE(store.load(session));
    LOGOS_ASSERT_EQ(session.canonicalState(), safeState);
    LOGOS_ASSERT_EQ(palace::deliverySessionStateName(session.state()),
                    std::string("offline"));
    std::filesystem::remove_all(directory);
}
