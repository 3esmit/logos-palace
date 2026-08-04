#include "palace_asset.h"

#include "palace_sha256.h"

#include <cstddef>
#include <cstdint>

namespace palace {
namespace {

constexpr std::size_t kMaxEncodedBytes = 10U * 1024U * 1024U;
constexpr std::uint32_t kMaxDimension = 4096U;
constexpr std::uint64_t kMaxPixels = 16U * 1024U * 1024U;

bool isLowerHexDigest(const std::string& value)
{
    if (value.size() != 64U)
        return false;
    for (const unsigned char character : value) {
        if (!((character >= '0' && character <= '9')
              || (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

std::uint32_t readBigEndian(const std::string& bytes, std::size_t offset)
{
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset])) << 24U)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 1U])) << 16U)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 2U])) << 8U)
        | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 3U]));
}

bool hasPngHeader(const std::string& encoded, std::uint32_t& width, std::uint32_t& height)
{
    static constexpr unsigned char kSignature[] = {
        0x89U, 0x50U, 0x4eU, 0x47U, 0x0dU, 0x0aU, 0x1aU, 0x0aU,
    };
    if (encoded.size() < 24U)
        return false;
    for (std::size_t index = 0; index < 8U; ++index) {
        if (static_cast<unsigned char>(encoded[index]) != kSignature[index])
            return false;
    }
    if (encoded.substr(12U, 4U) != "IHDR")
        return false;
    width = readBigEndian(encoded, 16U);
    height = readBigEndian(encoded, 20U);
    return width > 0U && height > 0U;
}

VerifiedAsset reject(const std::string& reason)
{
    return {false, {}, reason, 0U, 0U};
}

} // namespace

VerifiedAsset AssetCovenant::verifyPngDerivative(const AssetRefV1& reference,
                                                 const std::string& encoded,
                                                 const BoundedRasterDecoder& decoder) const
{
    if (reference.sourceCid.empty() || reference.derivativeCid.empty()
        || reference.mediaType != "image/png"
        || reference.technicalProfile != "palace-png-v1"
        || !isLowerHexDigest(reference.contentSha256)) {
        return reject("invalid-asset-reference");
    }
    if (encoded.size() > kMaxEncodedBytes
        || reference.byteLength != encoded.size()
        || crypto::sha256Hex(encoded) != reference.contentSha256) {
        return reject("byte-length-or-digest-mismatch");
    }

    std::uint32_t headerWidth = 0;
    std::uint32_t headerHeight = 0;
    if (!hasPngHeader(encoded, headerWidth, headerHeight)
        || headerWidth > kMaxDimension || headerHeight > kMaxDimension
        || static_cast<std::uint64_t>(headerWidth) * headerHeight > kMaxPixels
        || headerWidth != reference.width || headerHeight != reference.height) {
        return reject("invalid-or-oversized-png-header");
    }

    std::uint32_t decodedWidth = 0;
    std::uint32_t decodedHeight = 0;
    if (!decoder.decodePng(encoded, decodedWidth, decodedHeight)
        || decodedWidth != headerWidth || decodedHeight != headerHeight) {
        return reject("raster-decoder-rejected");
    }
    return {true, reference.contentSha256, "verified", headerWidth, headerHeight};
}

} // namespace palace
