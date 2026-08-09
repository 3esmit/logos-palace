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
        {"atrium", false, "", 9},
        {"lounge", false, "", 4},
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

palace::PalaceDeliveryEnvelopeV1 roomEnvelope(
    const std::string& roomId,
    std::int64_t roomEpoch,
    const std::string& userId,
    const std::string& key,
    std::int64_t keyEpoch,
    std::uint64_t sequence,
    palace::DeliveryKind kind,
    const std::string& payload)
{
    palace::PalaceDeliveryEnvelopeV1 message = envelope(
        userId, key, keyEpoch, sequence, kind, payload);
    message.roomId = roomId;
    message.roomEpoch = roomEpoch;
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

std::string loungeTopic()
{
    return palace::deriveRoomTopic("logos.test", "palace-1", "lounge", 4);
}

void bringOnline(palace::PalaceDeliverySession& session,
                 const std::string& expectedTopic = roomTopic())
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
    LOGOS_ASSERT_EQ(connected.commands.front().contentTopic, expectedTopic);
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
    LOGOS_ASSERT_TRUE(session.messagePropagated("request-1"));
    LOGOS_ASSERT_FALSE(session.outboxStatus("request-1").found);
    LOGOS_ASSERT_FALSE(session.messageError("request-1", 1051));
    LOGOS_ASSERT_TRUE(session.eligibleOutboxCommands(1051).empty());
    LOGOS_ASSERT_FALSE(session.messageSent("request-1"));
    LOGOS_ASSERT_FALSE(session.messagePropagated("request-1"));

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

LOGOS_TEST(delivery_session_applies_local_publication_once_through_signed_ingress) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::PalaceDeliverySession session(authority);
    LOGOS_ASSERT_TRUE(session.configure(sessionConfig()));
    bringOnline(session);

    CanonicalVerifier verifier;
    CanonicalSigner signer("carol-key");
    const palace::DeliverySessionTransition published = session.publish(
        "presence-local", palace::DeliveryKind::PresenceHello, "Carol",
        1050, 30, signer, verifier);
    LOGOS_ASSERT_TRUE(published.accepted);
    LOGOS_ASSERT_EQ(published.commands.size(), static_cast<std::size_t>(1));
    const palace::DeliverySessionReceive local = session.receive(
        published.commands.front().contentTopic,
        published.commands.front().payload,
        1050,
        verifier);
    LOGOS_ASSERT_TRUE(local.accepted);
    LOGOS_ASSERT_TRUE(local.projectionChanged);
    LOGOS_ASSERT_EQ(session.participantSnapshot().size(),
                    static_cast<std::size_t>(1));

    const palace::DeliverySessionReceive networkEcho = session.receive(
        published.commands.front().contentTopic,
        published.commands.front().payload,
        1050,
        verifier);
    LOGOS_ASSERT_FALSE(networkEcho.accepted);
    LOGOS_ASSERT_EQ(networkEcho.reason,
                    std::string("duplicate-or-replayed-sequence"));
}

LOGOS_TEST(delivery_session_switch_room_requires_empty_outbox) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::PalaceDeliverySession session(authority);
    LOGOS_ASSERT_TRUE(session.configure(sessionConfig()));
    bringOnline(session);

    CanonicalVerifier verifier;
    CanonicalSigner signer("carol-key");
    LOGOS_ASSERT_TRUE(session.publish(
        "pending", palace::DeliveryKind::Speech, "pending", 1050, 30,
        signer, verifier).accepted);

    const palace::DeliverySessionTransition rejected =
        session.switchRoom("lounge", 4);
    LOGOS_ASSERT_FALSE(rejected.accepted);
    LOGOS_ASSERT_EQ(
        rejected.reason, std::string("room-switch-outbox-not-empty"));
    LOGOS_ASSERT_EQ(session.configuration().roomId, std::string("atrium"));
    LOGOS_ASSERT_EQ(
        palace::deliverySessionStateName(session.state()),
        std::string("online"));
}

LOGOS_TEST(delivery_session_switches_online_room_and_persists_canonical_room) {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path()
        / "logos-palace-delivery-room-switch-contract";
    std::filesystem::remove_all(directory);

    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::PalaceDeliverySession session(authority);
    LOGOS_ASSERT_TRUE(session.configure(sessionConfig()));
    bringOnline(session);
    CanonicalVerifier verifier;
    CanonicalSigner signer("carol-key");

    LOGOS_ASSERT_TRUE(session.receive(
        roomTopic(), wire(envelope(
            "bob", "bob-key", 2, 1,
            palace::DeliveryKind::PresenceHello, "Bob")),
        1050, verifier).accepted);
    LOGOS_ASSERT_EQ(
        session.participantSnapshot().size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_FALSE(session.switchRoom("lounge", 5).accepted);
    LOGOS_ASSERT_FALSE(session.switchRoom("outside", 1).accepted);

    const palace::DeliverySessionTransition switched =
        session.switchRoom("lounge", 4);
    LOGOS_ASSERT_TRUE(switched.accepted);
    LOGOS_ASSERT_EQ(switched.commands.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(
        static_cast<int>(switched.commands.front().kind),
        static_cast<int>(palace::DeliverySessionCommandKind::Subscribe));
    LOGOS_ASSERT_EQ(switched.commands.front().contentTopic, loungeTopic());
    LOGOS_ASSERT_EQ(session.configuration().roomId, std::string("lounge"));
    LOGOS_ASSERT_EQ(
        session.configuration().roomEpoch, static_cast<std::int64_t>(4));
    LOGOS_ASSERT_TRUE(session.participantSnapshot().empty());
    LOGOS_ASSERT_TRUE(session.subscriptionResult(true).accepted);

    const palace::DeliverySessionReceive oldRoom = session.receive(
        roomTopic(), wire(envelope(
            "bob", "bob-key", 2, 2,
            palace::DeliveryKind::Speech, "old room")),
        1050, verifier);
    LOGOS_ASSERT_FALSE(oldRoom.accepted);
    LOGOS_ASSERT_EQ(
        oldRoom.reason, std::string("wrong-palace-room-or-epoch"));
    LOGOS_ASSERT_TRUE(session.receive(
        loungeTopic(), wire(roomEnvelope(
            "lounge", 4, "bob", "bob-key", 2, 1,
            palace::DeliveryKind::PresenceHello, "Bob")),
        1050, verifier).accepted);

    const palace::DeliverySessionTransition published = session.publish(
        "lounge-message", palace::DeliveryKind::Speech, "hello", 1050, 30,
        signer, verifier);
    LOGOS_ASSERT_TRUE(published.accepted);
    const palace::DeliveryEnvelopeDecode decoded =
        palace::decodeDeliveryEnvelope(std::string(
            published.commands.front().payload.begin(),
            published.commands.front().payload.end()));
    LOGOS_ASSERT_TRUE(decoded.accepted);
    LOGOS_ASSERT_EQ(decoded.envelope.roomId, std::string("lounge"));
    LOGOS_ASSERT_EQ(decoded.envelope.roomEpoch, static_cast<std::int64_t>(4));
    LOGOS_ASSERT_EQ(
        decoded.envelope.senderSequence, static_cast<std::uint64_t>(1));
    LOGOS_ASSERT_TRUE(session.messagePropagated("lounge-message"));

    palace::DeliverySessionStore store(directory.string());
    LOGOS_ASSERT_TRUE(store.save(session));
    palace::PalaceDeliverySession restored(authority);
    LOGOS_ASSERT_TRUE(store.load(restored));
    LOGOS_ASSERT_EQ(restored.configuration().roomId, std::string("lounge"));
    LOGOS_ASSERT_EQ(
        restored.configuration().roomEpoch, static_cast<std::int64_t>(4));
    bringOnline(restored, loungeTopic());
    std::filesystem::remove_all(directory);
}

LOGOS_TEST(delivery_session_validates_before_live_projection_mutation) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::PalaceDeliverySession session(authority);
    LOGOS_ASSERT_TRUE(session.configure(sessionConfig()));
    session.replaceAllowedProps({{"test-prop", "cid-test-prop"}});
    bringOnline(session);
    CanonicalVerifier verifier;
    CanonicalSigner signer("carol-key");

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
            palace::DeliveryKind::WearProp, "test-prop")),
        1050, verifier).projectionChanged);
    LOGOS_ASSERT_TRUE(session.receive(
        roomTopic(), wire(envelope(
            "carol", "carol-key", 3, 5,
            palace::DeliveryKind::RemoveProp, "test-prop")),
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

LOGOS_TEST(delivery_session_reconciles_finalized_user_ban_from_live_projection) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::PalaceDeliverySession session(authority);
    LOGOS_ASSERT_TRUE(session.configure(sessionConfig()));
    bringOnline(session);
    CanonicalVerifier verifier;
    CanonicalSigner signer("carol-key");

    LOGOS_ASSERT_TRUE(session.receive(
        roomTopic(), wire(envelope(
            "carol", "carol-key", 3, 1,
            palace::DeliveryKind::PresenceHello, "Carol")),
        1050, verifier).projectionChanged);
    LOGOS_ASSERT_EQ(
        session.participantSnapshot().size(), static_cast<std::size_t>(1));
    const palace::DeliverySessionReceive buffered = session.receive(
        roomTopic(), wire(envelope(
            "carol", "carol-key", 3, 3,
            palace::DeliveryKind::Speech, "stale")),
        1050, verifier);
    LOGOS_ASSERT_TRUE(buffered.accepted);
    LOGOS_ASSERT_FALSE(buffered.projectionChanged);
    LOGOS_ASSERT_TRUE(session.publish(
        "queued-before-authority-rebuild", palace::DeliveryKind::Speech,
        "queued", 1050, 30, signer, verifier).accepted);
    LOGOS_ASSERT_EQ(session.outboxSize(), static_cast<std::size_t>(1));

    palace::AuthoritySnapshotV1 banned = authoritySnapshot();
    banned.bans.push_back({
        "ban-carol", "palace-1", "atrium", "carol", "", "alice", true});
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(banned, 1051));

    LOGOS_ASSERT_TRUE(session.reconcileAuthority());
    LOGOS_ASSERT_TRUE(session.participantSnapshot().empty());
    LOGOS_ASSERT_EQ(session.outboxSize(), static_cast<std::size_t>(0));
    const palace::DeliverySessionTransition bannedPublish = session.publish(
        "rejected-after-authority-rebuild", palace::DeliveryKind::Speech,
        "blocked", 1051, 30, signer, verifier);
    LOGOS_ASSERT_FALSE(bannedPublish.accepted);
    LOGOS_ASSERT_EQ(bannedPublish.reason, std::string("sender-banned"));
    LOGOS_ASSERT_FALSE(session.reconcileAuthority());

    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1052));
    LOGOS_ASSERT_TRUE(session.receive(
        roomTopic(), wire(envelope(
            "carol", "carol-key", 3, 2,
            palace::DeliveryKind::PresenceHello, "Carol")),
        1052, verifier).projectionChanged);
    const auto reentered = session.participantSnapshot();
    LOGOS_ASSERT_EQ(reentered.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_TRUE(reentered.front().speech.empty());
}

LOGOS_TEST(delivery_session_reconciles_removed_prop_mapping_from_live_projection) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::PalaceDeliverySession session(authority);
    LOGOS_ASSERT_TRUE(session.configure(sessionConfig()));
    session.replaceAllowedProps({{"test-prop", "cid-test-prop"}});
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
            palace::DeliveryKind::WearProp, "test-prop")),
        1050, verifier).projectionChanged);

    session.replaceAllowedProps({});
    LOGOS_ASSERT_TRUE(session.reconcileAuthority());
    const auto snapshot = session.participantSnapshot();
    LOGOS_ASSERT_EQ(snapshot.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_TRUE(snapshot.front().propIds.empty());
    LOGOS_ASSERT_FALSE(session.reconcileAuthority());
}

LOGOS_TEST(delivery_session_reconciles_finalized_asset_ban_from_worn_props) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::PalaceDeliverySession session(authority);
    LOGOS_ASSERT_TRUE(session.configure(sessionConfig()));
    session.replaceAllowedProps({{"test-prop", "cid-test-prop"}});
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
            palace::DeliveryKind::WearProp, "test-prop")),
        1050, verifier).projectionChanged);
    LOGOS_ASSERT_TRUE(
        session.participantSnapshot().front().propIds.find("test-prop")
        != session.participantSnapshot().front().propIds.end());

    palace::AuthoritySnapshotV1 banned = authoritySnapshot();
    banned.bans.push_back({
        "ban-test-prop", "palace-1", "atrium", "", "cid-test-prop", "alice", true});
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(banned, 1051));

    LOGOS_ASSERT_TRUE(session.reconcileAuthority());
    const auto snapshot = session.participantSnapshot();
    LOGOS_ASSERT_EQ(snapshot.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_TRUE(snapshot.front().propIds.empty());
    LOGOS_ASSERT_FALSE(session.reconcileAuthority());
}

LOGOS_TEST(delivery_session_drains_cross_kind_reordering_in_sender_sequence) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::PalaceDeliverySession session(authority);
    LOGOS_ASSERT_TRUE(session.configure(sessionConfig()));
    session.replaceAllowedProps({{"test-prop", "cid-test-prop"}});
    bringOnline(session);
    CanonicalVerifier verifier;

    LOGOS_ASSERT_TRUE(session.receive(
        roomTopic(), wire(envelope(
            "carol", "carol-key", 3, 41,
            palace::DeliveryKind::PresenceHello, "Carol")),
        1050, verifier).accepted);
    const palace::DeliverySessionReceive bufferedProp = session.receive(
        roomTopic(), wire(envelope(
            "carol", "carol-key", 3, 43,
            palace::DeliveryKind::WearProp, "test-prop")),
        1050, verifier);
    LOGOS_ASSERT_TRUE(bufferedProp.accepted);
    LOGOS_ASSERT_FALSE(bufferedProp.projectionChanged);
    LOGOS_ASSERT_TRUE(session.participantSnapshot().front().propIds.empty());

    const palace::DeliverySessionReceive drained = session.receive(
        roomTopic(), wire(envelope(
            "carol", "carol-key", 3, 42,
            palace::DeliveryKind::Motion, "x=25;y=50")),
        1050, verifier);
    LOGOS_ASSERT_TRUE(drained.accepted);
    LOGOS_ASSERT_TRUE(drained.projectionChanged);
    const auto snapshot = session.participantSnapshot();
    LOGOS_ASSERT_EQ(snapshot.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_TRUE(snapshot.front().hasMotion);
    LOGOS_ASSERT_EQ(snapshot.front().motionX, static_cast<std::int64_t>(25));
    LOGOS_ASSERT_EQ(snapshot.front().motionY, static_cast<std::int64_t>(50));
    LOGOS_ASSERT_TRUE(
        snapshot.front().propIds.find("test-prop")
        != snapshot.front().propIds.end());
}

LOGOS_TEST(delivery_session_buffers_initial_delta_until_presence_checkpoint) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::PalaceDeliverySession session(authority);
    LOGOS_ASSERT_TRUE(session.configure(sessionConfig()));
    bringOnline(session);
    CanonicalVerifier verifier;

    const palace::DeliverySessionReceive buffered = session.receive(
        roomTopic(), wire(envelope(
            "bob", "bob-key", 2, 42,
            palace::DeliveryKind::Motion, "x=125;y=250")),
        1050, verifier);
    LOGOS_ASSERT_TRUE(buffered.accepted);
    LOGOS_ASSERT_FALSE(buffered.projectionChanged);
    LOGOS_ASSERT_TRUE(session.participantSnapshot().empty());
    LOGOS_ASSERT_TRUE(
        session.canonicalState().find("ingress;") == std::string::npos);

    const palace::DeliverySessionReceive checkpoint = session.receive(
        roomTopic(), wire(envelope(
            "bob", "bob-key", 2, 41,
            palace::DeliveryKind::PresenceHello, "Bob")),
        1050, verifier);
    LOGOS_ASSERT_TRUE(checkpoint.accepted);
    LOGOS_ASSERT_TRUE(checkpoint.projectionChanged);
    const auto snapshot = session.participantSnapshot();
    LOGOS_ASSERT_EQ(snapshot.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_TRUE(snapshot.front().present);
    LOGOS_ASSERT_TRUE(snapshot.front().hasMotion);
    LOGOS_ASSERT_EQ(snapshot.front().motionX, static_cast<std::int64_t>(125));
    LOGOS_ASSERT_EQ(snapshot.front().motionY, static_cast<std::int64_t>(250));
}

LOGOS_TEST(delivery_session_rejects_duplicate_buffer_and_oversized_gap) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::PalaceDeliverySession session(authority);
    LOGOS_ASSERT_TRUE(session.configure(sessionConfig()));
    session.replaceAllowedProps({{"test-prop", "cid-test-prop"}});
    bringOnline(session);
    CanonicalVerifier verifier;

    LOGOS_ASSERT_TRUE(session.receive(
        roomTopic(), wire(envelope(
            "carol", "carol-key", 3, 41,
            palace::DeliveryKind::PresenceHello, "Carol")),
        1050, verifier).accepted);
    const std::vector<std::uint8_t> pending = wire(envelope(
        "carol", "carol-key", 3, 43,
        palace::DeliveryKind::WearProp, "test-prop"));
    LOGOS_ASSERT_TRUE(
        session.receive(roomTopic(), pending, 1050, verifier).accepted);
    LOGOS_ASSERT_EQ(
        session.receive(roomTopic(), pending, 1050, verifier).reason,
        std::string("duplicate-or-replayed-sequence"));
    LOGOS_ASSERT_EQ(
        session.receive(
            roomTopic(), wire(envelope(
                "carol", "carol-key", 3, 74,
                palace::DeliveryKind::Speech, "too far")),
            1050, verifier).reason,
        std::string("reorder-gap-exceeded"));
}

LOGOS_TEST(delivery_session_bounds_pending_reorder_count_and_bytes) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    CanonicalVerifier verifier;

    palace::PalaceDeliverySession countBounded(authority);
    LOGOS_ASSERT_TRUE(countBounded.configure(sessionConfig()));
    bringOnline(countBounded);
    for (std::uint64_t sequence = 1; sequence <= 256; ++sequence) {
        LOGOS_ASSERT_TRUE(countBounded.receive(
            roomTopic(), wire(envelope(
                "bob", "bob-key", 2, sequence,
                palace::DeliveryKind::Speech, "x")),
            1050, verifier).accepted);
    }
    LOGOS_ASSERT_EQ(
        countBounded.receive(
            roomTopic(), wire(envelope(
                "bob", "bob-key", 2, 257,
                palace::DeliveryKind::Speech, "x")),
            1050, verifier).reason,
        std::string("reorder-buffer-count-exceeded"));

    palace::PalaceDeliverySession byteBounded(authority);
    LOGOS_ASSERT_TRUE(byteBounded.configure(sessionConfig()));
    bringOnline(byteBounded);
    const std::string largeSpeech(280, 'x');
    std::string rejection;
    std::uint64_t accepted = 0;
    for (std::uint64_t sequence = 1; sequence <= 256; ++sequence) {
        const palace::DeliverySessionReceive received = byteBounded.receive(
            roomTopic(), wire(envelope(
                "bob", "bob-key", 2, sequence,
                palace::DeliveryKind::Speech, largeSpeech)),
            1050, verifier);
        if (!received.accepted) {
            rejection = received.reason;
            break;
        }
        ++accepted;
    }
    LOGOS_ASSERT_TRUE(accepted > 0U);
    LOGOS_ASSERT_TRUE(accepted < 256U);
    LOGOS_ASSERT_EQ(
        rejection, std::string("reorder-buffer-bytes-exceeded"));
}

LOGOS_TEST(delivery_session_prunes_expired_pending_reorder_entries) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::PalaceDeliverySession session(authority);
    LOGOS_ASSERT_TRUE(session.configure(sessionConfig()));
    bringOnline(session);
    CanonicalVerifier verifier;

    LOGOS_ASSERT_TRUE(session.receive(
        roomTopic(), wire(envelope(
            "bob", "bob-key", 2, 41,
            palace::DeliveryKind::PresenceHello, "Bob")),
        1050, verifier).accepted);
    LOGOS_ASSERT_TRUE(session.receive(
        roomTopic(), wire(envelope(
            "bob", "bob-key", 2, 43,
            palace::DeliveryKind::Speech, "expires", 1040, 1051)),
        1050, verifier).accepted);
    session.expireTransient(1052);
    LOGOS_ASSERT_TRUE(session.receive(
        roomTopic(), wire(envelope(
            "bob", "bob-key", 2, 42,
            palace::DeliveryKind::Motion, "x=25;y=50", 1051, 1100)),
        1052, verifier).accepted);
    LOGOS_ASSERT_TRUE(session.participantSnapshot().front().speech.empty());
    LOGOS_ASSERT_TRUE(session.receive(
        roomTopic(), wire(envelope(
            "bob", "bob-key", 2, 43,
            palace::DeliveryKind::Speech, "replacement", 1052, 1100)),
        1052, verifier).accepted);
    LOGOS_ASSERT_EQ(
        session.participantSnapshot().front().speech,
        std::string("replacement"));
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
    LOGOS_ASSERT_TRUE(original.messagePropagated("request-1"));
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
    LOGOS_ASSERT_EQ(restored.outboxSize(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_FALSE(restored.outboxStatus("request-1").found);

    bringOnline(restored);
    LOGOS_ASSERT_EQ(restored.eligibleOutboxCommands(1060).size(),
                    static_cast<std::size_t>(1));
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

LOGOS_TEST(delivery_session_rebinds_when_finalized_room_epoch_advances) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::PalaceDeliverySession session(authority);
    LOGOS_ASSERT_TRUE(session.configure(sessionConfig()));
    bringOnline(session);

    palace::AuthoritySnapshotV1 advanced = authoritySnapshot();
    advanced.rooms[0].roomEpoch = 11;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(advanced, 1001));

    const palace::DeliverySessionTransition rebound =
        session.rebindRoom("atrium", 11);
    LOGOS_ASSERT_TRUE(rebound.accepted);
    LOGOS_ASSERT_EQ(rebound.commands.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(
        static_cast<int>(rebound.commands.front().kind),
        static_cast<int>(palace::DeliverySessionCommandKind::Subscribe));
    LOGOS_ASSERT_EQ(
        rebound.commands.front().contentTopic,
        palace::deriveRoomTopic("logos.test", "palace-1", "atrium", 11));
    LOGOS_ASSERT_EQ(session.configuration().roomEpoch,
                    static_cast<std::int64_t>(11));
    LOGOS_ASSERT_TRUE(session.subscriptionResult(true).accepted);
    LOGOS_ASSERT_EQ(palace::deliverySessionStateName(session.state()),
                    std::string("online"));
}

LOGOS_TEST(delivery_session_restart_can_wait_for_authority_before_rebinding) {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path()
        / "logos-palace-delivery-session-authority-rebind";
    std::filesystem::remove_all(directory);

    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    palace::PalaceDeliverySession original(authority);
    LOGOS_ASSERT_TRUE(original.configure(sessionConfig()));
    palace::DeliverySessionStore store(directory.string());
    LOGOS_ASSERT_TRUE(store.save(original));

    palace::AuthorityProjection pendingAuthority;
    palace::PalaceDeliverySession restored(pendingAuthority);
    LOGOS_ASSERT_TRUE(store.load(restored));
    LOGOS_ASSERT_TRUE(restored.hasConfiguration());
    LOGOS_ASSERT_FALSE(restored.configurationMatchesAuthority());

    LOGOS_ASSERT_TRUE(
        pendingAuthority.replaceFinalized(authoritySnapshot(), 1001));
    LOGOS_ASSERT_TRUE(restored.configurationMatchesAuthority());
    std::filesystem::remove_all(directory);
}

LOGOS_TEST(delivery_session_restart_rebuilds_placeholder_and_presence_checkpoint) {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path()
        / "logos-palace-delivery-reorder-restart-contract";
    std::filesystem::remove_all(directory);

    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(authoritySnapshot(), 1000));
    CanonicalVerifier verifier;

    palace::PalaceDeliverySession original(authority);
    LOGOS_ASSERT_TRUE(original.configure(sessionConfig()));
    bringOnline(original);
    LOGOS_ASSERT_TRUE(original.receive(
        roomTopic(), wire(envelope(
            "bob", "bob-key", 2, 38,
            palace::DeliveryKind::PresenceHello, "Bob")),
        1050, verifier).accepted);
    palace::DeliverySessionStore store(directory.string());
    LOGOS_ASSERT_TRUE(store.save(original));

    palace::PalaceDeliverySession placeholderRestore(authority);
    LOGOS_ASSERT_TRUE(store.load(placeholderRestore));
    bringOnline(placeholderRestore);
    LOGOS_ASSERT_TRUE(placeholderRestore.receive(
        roomTopic(), wire(envelope(
            "bob", "bob-key", 2, 39,
            palace::DeliveryKind::Motion, "x=125;y=250")),
        1060, verifier).accepted);
    auto placeholder = placeholderRestore.participantSnapshot();
    LOGOS_ASSERT_EQ(placeholder.size(), static_cast<std::size_t>(1));
    LOGOS_ASSERT_FALSE(placeholder.front().present);
    LOGOS_ASSERT_TRUE(placeholder.front().hasMotion);
    LOGOS_ASSERT_TRUE(placeholderRestore.receive(
        roomTopic(), wire(envelope(
            "bob", "bob-key", 2, 40,
            palace::DeliveryKind::PresenceHello, "Bob")),
        1060, verifier).accepted);
    placeholder = placeholderRestore.participantSnapshot();
    LOGOS_ASSERT_TRUE(placeholder.front().present);
    LOGOS_ASSERT_TRUE(placeholder.front().hasMotion);
    LOGOS_ASSERT_EQ(
        placeholder.front().motionX, static_cast<std::int64_t>(125));

    palace::PalaceDeliverySession checkpointRestore(authority);
    LOGOS_ASSERT_TRUE(store.load(checkpointRestore));
    bringOnline(checkpointRestore);
    LOGOS_ASSERT_TRUE(checkpointRestore.receive(
        roomTopic(), wire(envelope(
            "bob", "bob-key", 2, 40,
            palace::DeliveryKind::PresenceHello, "Bob")),
        1060, verifier).accepted);
    LOGOS_ASSERT_EQ(
        checkpointRestore.receive(
            roomTopic(), wire(envelope(
                "bob", "bob-key", 2, 39,
                palace::DeliveryKind::Motion, "x=125;y=250")),
            1060, verifier).reason,
        std::string("duplicate-or-replayed-sequence"));

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
