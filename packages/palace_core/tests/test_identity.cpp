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
