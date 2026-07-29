#include <logos_test.h>

#include "palace_authority.h"
#include "palace_delivery.h"

namespace {

palace::AuthoritySnapshotV1 baseSnapshot()
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
    snapshot.grants = {
        {"grant-user", "palace-1", "", "bob", palace::CapabilityKind::ModerateUser,
         false, 0, false},
        {"grant-asset", "palace-1", "", "bob", palace::CapabilityKind::ModerateAsset,
         false, 0, false},
    };
    return snapshot;
}

class CanonicalTestVerifier final : public palace::DeliverySignatureVerifier {
public:
    bool verify(const std::string& publicKey,
                const std::string& canonicalEnvelope,
                const std::string& signature) const override
    {
        return signature == publicKey + ":" + canonicalEnvelope;
    }
};

class CanonicalTestSigner final : public palace::DeliverySignatureSigner {
public:
    std::string publicKey() const override { return "carol-key"; }
    std::string sign(const std::string& canonicalEnvelope) const override
    {
        return publicKey() + ":" + canonicalEnvelope;
    }
};

palace::PalaceDeliveryEnvelopeV1 validSpeech(std::uint64_t sequence)
{
    palace::PalaceDeliveryEnvelopeV1 envelope;
    envelope.networkId = "logos.test";
    envelope.palaceId = "palace-1";
    envelope.roomId = "atrium";
    envelope.roomEpoch = 9;
    envelope.kind = palace::DeliveryKind::Speech;
    envelope.senderUserId = "carol";
    envelope.senderKeyEpoch = 3;
    envelope.senderSequence = sequence;
    envelope.createdAt = 1000;
    envelope.expiresAt = 1100;
    envelope.payload = "hello";
    envelope.signature = "carol-key:" + palace::canonicalDeliveryEnvelope(envelope);
    return envelope;
}

palace::DeliveryPolicy policyFor(const palace::AuthorityProjection& authority)
{
    palace::DeliveryPolicy policy;
    policy.networkId = "logos.test";
    policy.palaceId = "palace-1";
    policy.roomId = "atrium";
    policy.roomEpoch = 9;
    policy.now = 1050;
    policy.minMotionIntervalSeconds = 1;
    policy.allowedProps = {{"test-prop", "cid-test-prop"}};
    policy.authority = &authority;
    return policy;
}

void sign(palace::PalaceDeliveryEnvelopeV1& envelope, const std::string& key)
{
    envelope.signature = key + ":" + palace::canonicalDeliveryEnvelope(envelope);
}

} // namespace

LOGOS_TEST(finalized_authority_rejects_unauthorized_bans_and_applies_delegated_bans) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(baseSnapshot(), 900));
    LOGOS_ASSERT_EQ(authority.entryRoomId(), std::string("atrium"));
    LOGOS_ASSERT_EQ(authority.roomEpoch("atrium"), 9);
    LOGOS_ASSERT_EQ(authority.roomEpoch("lounge"), 4);
    LOGOS_ASSERT_EQ(authority.roomEpoch("missing"), -1);

    palace::AuthoritySnapshotV1 unauthorized = baseSnapshot();
    unauthorized.bans.push_back({"ban-unauthorized", "palace-1", "", "carol", "", "carol", true});
    LOGOS_ASSERT_FALSE(authority.replaceFinalized(unauthorized, 901));
    LOGOS_ASSERT_FALSE(authority.isUserBanned("carol", "atrium"));

    palace::AuthoritySnapshotV1 delegated = baseSnapshot();
    delegated.bans.push_back({"ban-carol", "palace-1", "", "carol", "", "bob", true});
    delegated.bans.push_back({"ban-test-prop", "palace-1", "atrium", "", "cid-test-prop", "bob", true});
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(delegated, 902));
    LOGOS_ASSERT_TRUE(authority.isUserBanned("carol", "lounge"));
    LOGOS_ASSERT_TRUE(authority.isAssetBanned("cid-test-prop", "atrium"));
    LOGOS_ASSERT_FALSE(authority.isAssetBanned("cid-test-prop", "lounge"));
}

LOGOS_TEST(delivery_accepts_ordered_messages_then_rejects_replay_and_invalid_raw_input) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(baseSnapshot(), 900));
    const palace::DeliveryPolicy policy = policyFor(authority);
    const std::string topic = palace::deriveRoomTopic("logos.test", "palace-1", "atrium", 9);
    CanonicalTestVerifier verifier;
    palace::DeliveryIngress ingress;

    for (std::uint64_t sequence = 1; sequence <= 100; ++sequence) {
        const palace::DeliveryValidation accepted = ingress.receive(topic, validSpeech(sequence), policy, verifier);
        LOGOS_ASSERT_TRUE(accepted.accepted);
    }
    LOGOS_ASSERT_EQ(ingress.receive(topic, validSpeech(100), policy, verifier).reason,
                    std::string("duplicate-or-replayed-sequence"));

    palace::PalaceDeliveryEnvelopeV1 badSignature = validSpeech(101);
    badSignature.signature = "forged";
    LOGOS_ASSERT_EQ(ingress.receive(topic, badSignature, policy, verifier).reason,
                    std::string("bad-signature-or-key-binding"));
    LOGOS_ASSERT_TRUE(ingress.receive(topic, validSpeech(101), policy, verifier).accepted);

    palace::PalaceDeliveryEnvelopeV1 expired = validSpeech(102);
    expired.expiresAt = 1049;
    sign(expired, "carol-key");
    LOGOS_ASSERT_EQ(ingress.receive(topic, expired, policy, verifier).reason,
                    std::string("expired-or-invalid-time"));

    palace::PalaceDeliveryEnvelopeV1 wrongEpoch = validSpeech(102);
    wrongEpoch.roomEpoch = 8;
    sign(wrongEpoch, "carol-key");
    LOGOS_ASSERT_EQ(ingress.receive(topic, wrongEpoch, policy, verifier).reason,
                    std::string("wrong-palace-room-or-epoch"));
    LOGOS_ASSERT_EQ(ingress.receive("/logos-palace/1/room-public/proto", validSpeech(102), policy, verifier).reason,
                    std::string("wrong-topic"));
}

LOGOS_TEST(delivery_rejects_out_of_bounds_motion_and_finalized_user_or_asset_bans) {
    CanonicalTestVerifier verifier;
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(baseSnapshot(), 900));
    palace::DeliveryPolicy policy = policyFor(authority);
    const std::string topic = palace::deriveRoomTopic("logos.test", "palace-1", "atrium", 9);
    palace::DeliveryIngress ingress;

    palace::PalaceDeliveryEnvelopeV1 badMotion = validSpeech(1);
    badMotion.kind = palace::DeliveryKind::Motion;
    badMotion.payload = "x=10001;y=8";
    sign(badMotion, "carol-key");
    LOGOS_ASSERT_EQ(ingress.receive(topic, badMotion, policy, verifier).reason,
                    std::string("invalid-or-banned-payload"));

    palace::PalaceDeliveryEnvelopeV1 wear = validSpeech(1);
    wear.kind = palace::DeliveryKind::WearProp;
    wear.payload = "test-prop";
    sign(wear, "carol-key");
    LOGOS_ASSERT_TRUE(ingress.receive(topic, wear, policy, verifier).accepted);

    palace::AuthoritySnapshotV1 bannedAsset = baseSnapshot();
    bannedAsset.bans.push_back({"ban-test-prop", "palace-1", "atrium", "", "cid-test-prop", "bob", true});
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(bannedAsset, 901));
    palace::DeliveryIngress assetIngress;
    LOGOS_ASSERT_EQ(assetIngress.receive(topic, wear, policy, verifier).reason,
                    std::string("invalid-or-banned-payload"));

    palace::AuthoritySnapshotV1 bannedUser = baseSnapshot();
    bannedUser.bans.push_back({"ban-carol", "palace-1", "", "carol", "", "bob", true});
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(bannedUser, 902));
    palace::DeliveryIngress userIngress;
    LOGOS_ASSERT_EQ(userIngress.receive(topic, validSpeech(1), policy, verifier).reason,
                    std::string("sender-banned"));
}

LOGOS_TEST(delivery_egress_preflights_bound_signed_messages_and_increments_sequences) {
    palace::AuthorityProjection authority;
    LOGOS_ASSERT_TRUE(authority.replaceFinalized(baseSnapshot(), 900));
    palace::DeliveryPolicy policy = policyFor(authority);
    CanonicalTestVerifier verifier;
    CanonicalTestSigner signer;
    palace::DeliveryEgress egress;

    const palace::DeliveryPublication first = egress.prepare(
        policy, "carol", 3, palace::DeliveryKind::Speech, "hello; lounge", 1050, 30,
        signer, verifier);
    LOGOS_ASSERT_TRUE(first.accepted);
    LOGOS_ASSERT_EQ(first.sequence, static_cast<std::uint64_t>(1));
    LOGOS_ASSERT_EQ(first.contentTopic, palace::deriveRoomTopic("logos.test", "palace-1", "atrium", 9));

    const std::string encoded(first.payload.begin(), first.payload.end());
    const palace::DeliveryEnvelopeDecode decoded = palace::decodeDeliveryEnvelope(encoded);
    LOGOS_ASSERT_TRUE(decoded.accepted);
    LOGOS_ASSERT_EQ(decoded.envelope.payload, std::string("hello; lounge"));
    LOGOS_ASSERT_EQ(decoded.envelope.senderSequence, static_cast<std::uint64_t>(1));

    const palace::DeliveryPublication second = egress.prepare(
        policy, "carol", 3, palace::DeliveryKind::Speech, "again", 1051, 30,
        signer, verifier);
    LOGOS_ASSERT_TRUE(second.accepted);
    LOGOS_ASSERT_EQ(second.sequence, static_cast<std::uint64_t>(2));
}

LOGOS_TEST(delivery_codec_rejects_trailing_or_malformed_length_fields) {
    palace::PalaceDeliveryEnvelopeV1 envelope = validSpeech(1);
    const std::string encoded = palace::encodeDeliveryEnvelope(envelope);
    LOGOS_ASSERT_FALSE(palace::decodeDeliveryEnvelope(encoded + "x").accepted);
    LOGOS_ASSERT_FALSE(palace::decodeDeliveryEnvelope("version=1;network=999999:bad").accepted);
}
