#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace palace {

// Authority fields that bind an on-disk catalog to the LEZ snapshot that
// authorized its root manifest. Public-finality recovery compares every field
// exactly. Local-committed recovery has a separate, explicitly named path for
// harmless empty-block advancement.
struct PalaceStorageMvpCatalogBindingV1 {
  std::string networkId;
  std::string programIdHex;
  std::string rootAccountIdHex;
  std::uint64_t finalizedCheckpoint = 0U;
  std::string finalizedHash;
  std::string rootManifestCid;
};

struct PalaceStorageMvpCatalogRecordV1 {
  PalaceStorageMvpCatalogBindingV1 binding;
  // Canonical catalog transport bytes only. Media bytes never belong here.
  std::string canonicalCatalog;
  std::string catalogChecksumHex;
};

enum class PalaceStorageMvpCatalogStoreStatus {
  Saved,
  Loaded,
  NotFound,
  InvalidArgument,
  CatalogRejected,
  InvalidRecord,
  BindingMismatch,
  InsecurePermissions,
  UnsafePath,
  IoError,
};

// Load outcomes have deliberately different restart behavior. A valid record
// bound to another finalized authority is stale transport state, not a
// corruption signal; it stays inactive and permits a current graph to replace
// it. Every malformed or unsafe record fails closed.
enum class PalaceStorageMvpCatalogRecoveryAction {
  Restore,
  Idle,
  Degrade,
};

// Sealed, owner-only persistence for a canonical Storage catalog. The catalog
// remains untrusted transport data: callers must bind load to restored LEZ
// authority and re-verify every referenced CID before treating it as active.
class PalaceStorageMvpCatalogStore {
public:
  static constexpr std::size_t MaximumCatalogBytes = 16U * 1024U;

  explicit PalaceStorageMvpCatalogStore(std::string instancePersistenceRoot);

  // Persists record.binding and the SHA-256 of its canonical catalog in one
  // crash-safe record. Catalog input is bounded printable protocol text;
  // it cannot contain media bytes.
  PalaceStorageMvpCatalogStoreStatus
  save(const PalaceStorageMvpCatalogRecordV1 &record) const;

  // Does not modify record unless every framing, checksum, permission, and
  // exact comparison to expectedBinding succeeds.
  PalaceStorageMvpCatalogStoreStatus
  load(const PalaceStorageMvpCatalogBindingV1 &expectedBinding,
       PalaceStorageMvpCatalogRecordV1 &record) const;

  // Local-development-only recovery for a current local-committed authority.
  // The sealed record must have the exact network, program, root, and active
  // root-manifest binding, and cannot be ahead of the current local tip. An
  // equal-height record must also have the exact tip hash. Callers must obtain
  // expectedBinding from a complete local history and re-verify every fetched
  // CID before making the catalog active. This never relaxes load().
  PalaceStorageMvpCatalogStoreStatus
  loadLocalCommitted(const PalaceStorageMvpCatalogBindingV1 &expectedBinding,
                     PalaceStorageMvpCatalogRecordV1 &record) const;

private:
  std::string instancePersistenceRoot_;
};

const char *palaceStorageMvpCatalogStoreStatusName(
    PalaceStorageMvpCatalogStoreStatus status);
PalaceStorageMvpCatalogRecoveryAction
palaceStorageMvpCatalogRecoveryAction(
    PalaceStorageMvpCatalogStoreStatus status);

} // namespace palace
