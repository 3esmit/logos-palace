#pragma once

#include <optional>
#include <string>

#include "palace_asset.h"

namespace palace {

// Core-private bridge from a Storage-validated manifest entry to a Basecamp
// verified-asset handle. The directory is always below this core module's
// host-owned instance persistence path.
class VerifiedAssetStore {
public:
    explicit VerifiedAssetStore(std::string instancePersistencePath);

    VerifiedAsset stagePngDerivative(const AssetRefV1& reference,
                                     const std::string& encoded) const;
    // Resolves a digest handle only after rechecking containment, file type,
    // and bytes. Callers never receive a path derived from a CID or QML input.
    std::optional<std::string> verifiedPngPath(const std::string& handle) const;
    const std::string& directory() const;

private:
    std::string m_instancePersistencePath;
    std::string m_directory;
};

} // namespace palace
