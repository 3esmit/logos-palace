#pragma once

#include <array>
#include <string>

#include "palace_delivery.h"

namespace palace {

// Private material stays in this process-only type. It has no QML or VM API.
class Ed25519KeyPair final : public DeliverySignatureSigner {
public:
    Ed25519KeyPair() = default;
    ~Ed25519KeyPair();
    Ed25519KeyPair(const Ed25519KeyPair&) = delete;
    Ed25519KeyPair& operator=(const Ed25519KeyPair&) = delete;
    Ed25519KeyPair(Ed25519KeyPair&& other) noexcept;
    Ed25519KeyPair& operator=(Ed25519KeyPair&& other) noexcept;

    static bool generate(Ed25519KeyPair& keyPair);
    // Keystore adapters may restore the 32-byte Ed25519 private seed. The
    // decoded seed remains process-only and is never part of a Core API.
    static bool fromPrivateKeyHex(const std::string& privateKeyHex,
                                  Ed25519KeyPair& keyPair);

    std::string publicKeyHex() const;
    std::string signHex(const std::string& message) const;
    std::string publicKey() const override;
    std::string sign(const std::string& canonicalEnvelope) const override;

private:
    std::array<unsigned char, 32> m_publicKey{};
    std::array<unsigned char, 32> m_privateKey{};
    bool m_valid = false;

    friend class Ed25519EnvelopeVerifier;
};

class Ed25519EnvelopeVerifier final : public DeliverySignatureVerifier {
public:
    bool verify(const std::string& publicKey,
                const std::string& canonicalEnvelope,
                const std::string& signature) const override;
};

} // namespace palace
