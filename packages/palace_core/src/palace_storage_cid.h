#pragma once

#include <string>

namespace palace {

// Accepts canonical CIDv0 base58btc or CIDv1 lower-base32/base58btc carrying
// exactly one SHA2-256 multihash. On success, digest receives 64 lower hex
// characters. Failure clears digest.
bool canonicalStorageCidSha256(
    const std::string& value,
    std::string& digest);

bool isCanonicalStorageCid(const std::string& value);

} // namespace palace
