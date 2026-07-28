#pragma once

#include <array>
#include <string>

#include "palace_delivery.h"

namespace palace {

// Private material stays in this process-only type. It has no QML or VM API.
class Ed25519KeyPair {
public:
    static bool generate(Ed25519KeyPair& keyPair);

    std::string publicKeyHex() const;
    std::string signHex(const std::string& message) const;

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
