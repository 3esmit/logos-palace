#include <logos_test.h>

#include "palace_asset.h"
#include "palace_sha256.h"

namespace {

std::string pngHeader(std::uint32_t width, std::uint32_t height)
{
    std::string bytes;
    const unsigned char signature[] = {0x89U, 0x50U, 0x4eU, 0x47U, 0x0dU, 0x0aU, 0x1aU, 0x0aU};
    bytes.append(reinterpret_cast<const char*>(signature), sizeof(signature));
    bytes.append("\x00\x00\x00\x0dIHDR", 8U);
    const auto append32 = [&](std::uint32_t value) {
        bytes.push_back(static_cast<char>(value >> 24U));
        bytes.push_back(static_cast<char>(value >> 16U));
        bytes.push_back(static_cast<char>(value >> 8U));
        bytes.push_back(static_cast<char>(value));
    };
    append32(width);
    append32(height);
    return bytes;
}

palace::AssetRefV1 assetRef(const std::string& bytes, std::uint32_t width, std::uint32_t height)
{
    palace::AssetRefV1 reference;
    reference.sourceCid = "cid-source";
    reference.derivativeCid = "cid-derivative";
    reference.byteLength = bytes.size();
    reference.mediaType = "image/png";
    reference.width = width;
    reference.height = height;
    reference.technicalProfile = "palace-png-v1";
    reference.contentSha256 = palace::crypto::sha256Hex(bytes);
    return reference;
}

class HeaderDecoder final : public palace::BoundedRasterDecoder {
public:
    HeaderDecoder(std::uint32_t width, std::uint32_t height, bool accepts)
        : m_width(width), m_height(height), m_accepts(accepts)
    {
    }

    bool decodePng(const std::string&, std::uint32_t& width, std::uint32_t& height) const override
    {
        width = m_width;
        height = m_height;
        return m_accepts;
    }

private:
    std::uint32_t m_width;
    std::uint32_t m_height;
    bool m_accepts;
};

} // namespace

LOGOS_TEST(asset_covenant_emits_only_a_digest_handle_after_full_decoder_confirmation) {
    const std::string bytes = pngHeader(640, 480);
    const palace::AssetRefV1 reference = assetRef(bytes, 640, 480);
    const palace::VerifiedAsset asset = palace::AssetCovenant().verifyPngDerivative(
        reference, bytes, HeaderDecoder(640, 480, true));

    LOGOS_ASSERT_TRUE(asset.accepted);
    LOGOS_ASSERT_EQ(asset.handle, reference.contentSha256);
    LOGOS_ASSERT_EQ(asset.width, static_cast<std::uint32_t>(640));
}

LOGOS_TEST(asset_covenant_rejects_mime_hash_dimension_and_decoder_confusion) {
    const std::string bytes = pngHeader(640, 480);
    palace::AssetRefV1 reference = assetRef(bytes, 640, 480);
    const palace::AssetCovenant covenant;

    reference.mediaType = "image/jpeg";
    LOGOS_ASSERT_EQ(covenant.verifyPngDerivative(reference, bytes, HeaderDecoder(640, 480, true)).reason,
                    std::string("invalid-asset-reference"));

    reference = assetRef(bytes, 640, 480);
    reference.contentSha256.assign(64U, '0');
    LOGOS_ASSERT_EQ(covenant.verifyPngDerivative(reference, bytes, HeaderDecoder(640, 480, true)).reason,
                    std::string("byte-length-or-digest-mismatch"));

    reference = assetRef(bytes, 640, 480);
    LOGOS_ASSERT_EQ(covenant.verifyPngDerivative(reference, bytes, HeaderDecoder(640, 480, false)).reason,
                    std::string("raster-decoder-rejected"));

    const std::string tooWide = pngHeader(4097, 1);
    LOGOS_ASSERT_EQ(covenant.verifyPngDerivative(assetRef(tooWide, 4097, 1), tooWide,
                                                  HeaderDecoder(4097, 1, true)).reason,
                    std::string("invalid-or-oversized-png-header"));
}
