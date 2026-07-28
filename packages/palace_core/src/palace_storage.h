#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "palace_asset.h"

namespace palace {

bool isSafePalaceCid(const std::string& value);

struct StorageUploadTerminal {
  bool accepted = false;
  bool succeeded = false;
  std::string sessionId;
  std::string cid;
};

// Parses the Storage upload terminal event before it can update Palace state.
StorageUploadTerminal parseStorageUploadDone(const std::string& payload);

struct PendingStorageAsset {
  std::string operationId;
  AssetRefV1 reference;
  std::string destinationPath;
};

// Owns the caller-generated IDs for Storage V2 downloads. Only an expected,
// bounded Palace asset can enter this queue; source CIDs never form a file
// path. The core binds the returned destination to its own persistence root.
class StorageAssetQueue {
public:
  std::optional<PendingStorageAsset>
  begin(const AssetRefV1 &reference, const std::string &destinationDirectory);
  std::optional<PendingStorageAsset> take(const std::string &operationId);
  bool cancel(const std::string &operationId);

private:
  std::uint64_t m_nextOperation = 1U;
  std::map<std::string, PendingStorageAsset> m_pending;
  std::map<std::string, std::string> m_pendingByDerivativeCid;
};

} // namespace palace
