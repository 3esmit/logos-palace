#include <logos_test.h>

#include "palace_identity.h"

LOGOS_TEST(ed25519_envelope_verifier_accepts_only_bound_canonical_data) {
    palace::Ed25519KeyPair keyPair;
    LOGOS_ASSERT_TRUE(palace::Ed25519KeyPair::generate(keyPair));
    const std::string publicKey = keyPair.publicKeyHex();
    const std::string message = "palace-envelope-v1";
    const std::string signature = keyPair.signHex(message);
    palace::Ed25519EnvelopeVerifier verifier;

    LOGOS_ASSERT_EQ(publicKey.size(), static_cast<std::size_t>(64));
    LOGOS_ASSERT_EQ(signature.size(), static_cast<std::size_t>(128));
    LOGOS_ASSERT_TRUE(verifier.verify(publicKey, message, signature));
    LOGOS_ASSERT_FALSE(verifier.verify(publicKey, message + "-mutated", signature));
    LOGOS_ASSERT_FALSE(verifier.verify(publicKey, message, signature.substr(2)));
}

LOGOS_TEST(ed25519_private_seed_restore_is_atomic_and_matches_public_key) {
    palace::Ed25519KeyPair keyPair;
    LOGOS_ASSERT_TRUE(palace::Ed25519KeyPair::fromPrivateKeyHex(
        "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
        keyPair));
    LOGOS_ASSERT_EQ(
        keyPair.publicKeyHex(),
        std::string(
            "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"));
    const std::string originalPublicKey = keyPair.publicKeyHex();
    LOGOS_ASSERT_FALSE(palace::Ed25519KeyPair::fromPrivateKeyHex(
        "not-a-private-key", keyPair));
    LOGOS_ASSERT_EQ(keyPair.publicKeyHex(), originalPublicKey);
}
