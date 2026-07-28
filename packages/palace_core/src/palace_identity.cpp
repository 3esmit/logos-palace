#include "palace_identity.h"

#include <openssl/evp.h>

#include <array>
#include <cctype>
#include <memory>

namespace palace {
namespace {

struct PkeyDeleter {
    void operator()(EVP_PKEY* value) const { EVP_PKEY_free(value); }
};

struct PkeyContextDeleter {
    void operator()(EVP_PKEY_CTX* value) const { EVP_PKEY_CTX_free(value); }
};

struct MdContextDeleter {
    void operator()(EVP_MD_CTX* value) const { EVP_MD_CTX_free(value); }
};

std::string encodeHex(const unsigned char* bytes, std::size_t length)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string result;
    result.reserve(length * 2U);
    for (std::size_t index = 0; index < length; ++index) {
        result.push_back(kDigits[bytes[index] >> 4U]);
        result.push_back(kDigits[bytes[index] & 0x0fU]);
    }
    return result;
}

bool decodeHex(const std::string& value, unsigned char* output, std::size_t outputLength)
{
    if (value.size() != outputLength * 2U)
        return false;
    const auto nibble = [](unsigned char character, unsigned char& valueOut) {
        if (character >= '0' && character <= '9') {
            valueOut = character - '0';
        } else if (character >= 'a' && character <= 'f') {
            valueOut = character - 'a' + 10U;
        } else if (character >= 'A' && character <= 'F') {
            valueOut = character - 'A' + 10U;
        } else {
            return false;
        }
        return true;
    };
    for (std::size_t index = 0; index < outputLength; ++index) {
        unsigned char high = 0;
        unsigned char low = 0;
        if (!nibble(static_cast<unsigned char>(value[index * 2U]), high)
            || !nibble(static_cast<unsigned char>(value[index * 2U + 1U]), low)) {
            return false;
        }
        output[index] = static_cast<unsigned char>((high << 4U) | low);
    }
    return true;
}

std::unique_ptr<EVP_PKEY, PkeyDeleter> publicPkey(const std::array<unsigned char, 32>& key)
{
    return std::unique_ptr<EVP_PKEY, PkeyDeleter>(EVP_PKEY_new_raw_public_key(
        EVP_PKEY_ED25519, nullptr, key.data(), key.size()));
}

} // namespace

bool Ed25519KeyPair::generate(Ed25519KeyPair& keyPair)
{
    std::unique_ptr<EVP_PKEY_CTX, PkeyContextDeleter> context(EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr));
    if (!context || EVP_PKEY_keygen_init(context.get()) != 1)
        return false;

    EVP_PKEY* rawKey = nullptr;
    if (EVP_PKEY_keygen(context.get(), &rawKey) != 1)
        return false;
    std::unique_ptr<EVP_PKEY, PkeyDeleter> generated(rawKey);

    std::size_t publicLength = keyPair.m_publicKey.size();
    std::size_t privateLength = keyPair.m_privateKey.size();
    if (EVP_PKEY_get_raw_public_key(generated.get(), keyPair.m_publicKey.data(), &publicLength) != 1
        || EVP_PKEY_get_raw_private_key(generated.get(), keyPair.m_privateKey.data(), &privateLength) != 1
        || publicLength != keyPair.m_publicKey.size() || privateLength != keyPair.m_privateKey.size()) {
        return false;
    }
    keyPair.m_valid = true;
    return true;
}

std::string Ed25519KeyPair::publicKeyHex() const
{
    return m_valid ? encodeHex(m_publicKey.data(), m_publicKey.size()) : std::string{};
}

std::string Ed25519KeyPair::signHex(const std::string& message) const
{
    if (!m_valid)
        return {};
    const std::unique_ptr<EVP_PKEY, PkeyDeleter> key(EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519, nullptr, m_privateKey.data(), m_privateKey.size()));
    std::unique_ptr<EVP_MD_CTX, MdContextDeleter> context(EVP_MD_CTX_new());
    if (!key || !context || EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key.get()) != 1)
        return {};

    std::array<unsigned char, 64> signature{};
    std::size_t signatureLength = signature.size();
    if (EVP_DigestSign(context.get(), signature.data(), &signatureLength,
                       reinterpret_cast<const unsigned char*>(message.data()), message.size()) != 1
        || signatureLength != signature.size()) {
        return {};
    }
    return encodeHex(signature.data(), signatureLength);
}

std::string Ed25519KeyPair::publicKey() const
{
    return publicKeyHex();
}

std::string Ed25519KeyPair::sign(const std::string& canonicalEnvelope) const
{
    return signHex(canonicalEnvelope);
}

bool Ed25519EnvelopeVerifier::verify(const std::string& publicKey,
                                     const std::string& canonicalEnvelope,
                                     const std::string& signature) const
{
    std::array<unsigned char, 32> rawPublicKey{};
    std::array<unsigned char, 64> rawSignature{};
    if (!decodeHex(publicKey, rawPublicKey.data(), rawPublicKey.size())
        || !decodeHex(signature, rawSignature.data(), rawSignature.size())) {
        return false;
    }
    const std::unique_ptr<EVP_PKEY, PkeyDeleter> key = publicPkey(rawPublicKey);
    std::unique_ptr<EVP_MD_CTX, MdContextDeleter> context(EVP_MD_CTX_new());
    return key && context
        && EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()) == 1
        && EVP_DigestVerify(context.get(), rawSignature.data(), rawSignature.size(),
                            reinterpret_cast<const unsigned char*>(canonicalEnvelope.data()),
                            canonicalEnvelope.size()) == 1;
}

} // namespace palace
