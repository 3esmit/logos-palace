#pragma once

#include <cstdint>
#include <string>

namespace palace {

struct AssetRefV1 {
    std::string sourceCid;
    std::string derivativeCid;
    std::uint64_t byteLength = 0;
    std::string mediaType;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string technicalProfile;
    std::string contentSha256;
};

class BoundedRasterDecoder {
public:
    virtual ~BoundedRasterDecoder() = default;
    virtual bool decodePng(const std::string& encoded,
                           std::uint32_t& decodedWidth,
                           std::uint32_t& decodedHeight) const = 0;
};

struct VerifiedAsset {
    bool accepted = false;
    std::string handle;
    std::string reason;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Validates Storage bytes before a core projection may publish an opaque handle
// to the UI. A concrete decoder must safely decode the complete raster; header
// inspection alone never accepts an asset.
class AssetCovenant {
public:
    VerifiedAsset verifyPngDerivative(const AssetRefV1& reference,
                                      const std::string& encoded,
                                      const BoundedRasterDecoder& decoder) const;
};

} // namespace palace
