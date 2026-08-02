#include <logos_test.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <QBuffer>
#include <QImage>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "palace_sha256.h"
#include "palace_storage_mvp.h"
#include "palace_storage_mvp_catalog_store.h"

namespace {

namespace fs = std::filesystem;

constexpr char kRecordFileName[] = "storage-mvp-catalog-v1";
constexpr char kRootManifestCid[] =
    "zb2rhWeJ5rsKhbfi31kikfdS3hE2pvWzhW27Es4Jmc863vFdx";

class TemporaryDirectory {
public:
  explicit TemporaryDirectory(const std::string &label)
      : path_(fs::temp_directory_path() /
              (label + "-" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()))) {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  ~TemporaryDirectory() {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  const fs::path &path() const { return path_; }

private:
  fs::path path_;
};

palace::PalaceStorageMvpCatalogBindingV1 binding() {
  palace::PalaceStorageMvpCatalogBindingV1 result;
  result.networkId = "lez-testnet";
  result.programIdHex = "0102030405060708090a0b0c0d0e0f10"
                        "1112131415161718191a1b1c1d1e1f20";
  result.rootAccountIdHex = "202122232425262728292a2b2c2d2e2f"
                            "303132333435363738393a3b3c3d3e3f";
  // A finalized block ID, not an ordered-action ID.
  result.finalizedCheckpoint = 41029U;
  result.finalizedHash = "0ea1852f8c91d9ba8003844d1c98c68b"
                         "a1b6ddc08a7c9dbf23dec95eb0460b92";
  result.rootManifestCid = kRootManifestCid;
  return result;
}

std::string canonicalCatalog() {
  const std::string body = "logos-palace-catalog-v1\n"
                           "entry=opaque-record\n";
  return body + "checksum=" + palace::crypto::sha256Hex(body) + '\n';
}

palace::PalaceStorageMvpCatalogRecordV1 record() {
  palace::PalaceStorageMvpCatalogRecordV1 result;
  result.binding = binding();
  result.canonicalCatalog = canonicalCatalog();
  result.catalogChecksumHex =
      palace::crypto::sha256Hex(result.canonicalCatalog);
  return result;
}

std::string encodedPng(const int width, const int height, const QRgb color) {
  QImage image(width, height, QImage::Format_RGBA8888);
  image.fill(color);
  QBuffer buffer;
  if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
    return {};
  return {buffer.data().constData(),
          static_cast<std::size_t>(buffer.data().size())};
}

std::uint8_t hexNibble(const char value) {
  if (value >= '0' && value <= '9')
    return static_cast<std::uint8_t>(value - '0');
  return static_cast<std::uint8_t>(value - 'a' + 10);
}

std::string storageCid(const std::string &digest) {
  std::vector<std::uint8_t> bytes = {0x01U, 0x55U, 0x12U, 0x20U};
  for (std::size_t index = 0U; index < digest.size(); index += 2U) {
    bytes.push_back(static_cast<std::uint8_t>(
        (hexNibble(digest[index]) << 4U) | hexNibble(digest[index + 1U])));
  }

  static constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz234567";
  std::string encoded = "b";
  std::uint32_t accumulator = 0U;
  unsigned int bitCount = 0U;
  for (const std::uint8_t byte : bytes) {
    accumulator = (accumulator << 8U) | byte;
    bitCount += 8U;
    while (bitCount >= 5U) {
      bitCount -= 5U;
      encoded.push_back(alphabet[(accumulator >> bitCount) & 0x1fU]);
      accumulator &= bitCount == 0U ? 0U : ((1U << bitCount) - 1U);
    }
  }
  if (bitCount != 0U)
    encoded.push_back(alphabet[(accumulator << (5U - bitCount)) & 0x1fU]);
  return encoded;
}

bool publishAll(palace::PalaceStorageMvpBundle &bundle) {
  while (!bundle.complete()) {
    const std::vector<std::string> stageable = bundle.stageableObjectIds();
    if (stageable.empty())
      return false;
    for (const std::string &objectId : stageable) {
      const palace::PalaceStorageMvpArtifactV1 *artifact =
          bundle.artifact(objectId);
      if (artifact == nullptr)
        return false;
      const std::string nativeManifestDigest = palace::crypto::sha256Hex(
          "native-storage-manifest-v1\n" + objectId + "\n" +
          artifact->specification.contentSha256);
      if (!bundle.assignPublicationCid(
              objectId, storageCid(nativeManifestDigest))) {
        return false;
      }
    }
  }
  return true;
}

std::string
statusName(const palace::PalaceStorageMvpCatalogStoreStatus status) {
  return palace::palaceStorageMvpCatalogStoreStatusName(status);
}

bool sameBinding(const palace::PalaceStorageMvpCatalogBindingV1 &left,
                 const palace::PalaceStorageMvpCatalogBindingV1 &right) {
  return left.networkId == right.networkId &&
         left.programIdHex == right.programIdHex &&
         left.rootAccountIdHex == right.rootAccountIdHex &&
         left.finalizedCheckpoint == right.finalizedCheckpoint &&
         left.finalizedHash == right.finalizedHash &&
         left.rootManifestCid == right.rootManifestCid;
}

bool sameRecord(const palace::PalaceStorageMvpCatalogRecordV1 &left,
                const palace::PalaceStorageMvpCatalogRecordV1 &right) {
  return sameBinding(left.binding, right.binding) &&
         left.canonicalCatalog == right.canonicalCatalog &&
         left.catalogChecksumHex == right.catalogChecksumHex;
}

std::vector<std::uint8_t> readBytes(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void ownerOnly(const fs::path &path) {
#if defined(__unix__) || defined(__APPLE__)
  LOGOS_ASSERT_EQ(::chmod(path.c_str(), 0600), 0);
#else
  static_cast<void>(path);
#endif
}

void writeBytes(const fs::path &path, const std::vector<std::uint8_t> &value) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char *>(value.data()),
               static_cast<std::streamsize>(value.size()));
  output.close();
  LOGOS_ASSERT_TRUE(output.good());
  ownerOnly(path);
}

void writeText(const fs::path &path, const std::string &value) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << value;
  output.close();
  LOGOS_ASSERT_TRUE(output.good());
  ownerOnly(path);
}

void replaceOuterChecksum(std::vector<std::uint8_t> &framed) {
  constexpr std::size_t checksumBytes = 64U;
  LOGOS_ASSERT_GT(framed.size(), checksumBytes);
  const std::size_t checksumOffset = framed.size() - checksumBytes;
  const std::string body(reinterpret_cast<const char *>(framed.data()),
                         checksumOffset);
  const std::string checksum = palace::crypto::sha256Hex(body);
  LOGOS_ASSERT_EQ(checksum.size(), checksumBytes);
  std::copy(checksum.begin(), checksum.end(),
            framed.begin() + static_cast<std::ptrdiff_t>(checksumOffset));
}

} // namespace

LOGOS_TEST(storage_mvp_catalog_store_roundtrips_exact_sealed_record) {
  const palace::PalaceStorageMvpCatalogRecordV1 source = record();
  TemporaryDirectory root("palace-storage-mvp-catalog-roundtrip");
  palace::PalaceStorageMvpCatalogStore store(root.path().string());

  LOGOS_ASSERT_EQ(statusName(store.save(source)), std::string("saved"));
  const fs::path recordPath = root.path() / kRecordFileName;
  LOGOS_ASSERT_TRUE(fs::is_regular_file(recordPath));

#if defined(__unix__) || defined(__APPLE__)
  struct stat status{};
  LOGOS_ASSERT_EQ(::stat(recordPath.c_str(), &status), 0);
  LOGOS_ASSERT_EQ(static_cast<unsigned int>(status.st_mode & 0777),
                  static_cast<unsigned int>(0600));
#endif

  palace::PalaceStorageMvpCatalogRecordV1 restored;
  LOGOS_ASSERT_EQ(statusName(store.load(source.binding, restored)),
                  std::string("loaded"));
  LOGOS_ASSERT_TRUE(sameRecord(restored, source));
  LOGOS_ASSERT_EQ(restored.catalogChecksumHex,
                  palace::crypto::sha256Hex(source.canonicalCatalog));
}

LOGOS_TEST(storage_mvp_catalog_store_accepts_genesis_checkpoint_binding) {
  palace::PalaceStorageMvpCatalogRecordV1 source = record();
  source.binding.finalizedCheckpoint = 0U;
  TemporaryDirectory root("palace-storage-mvp-catalog-genesis");
  palace::PalaceStorageMvpCatalogStore store(root.path().string());

  LOGOS_ASSERT_EQ(statusName(store.save(source)), std::string("saved"));

  palace::PalaceStorageMvpCatalogRecordV1 restored;
  LOGOS_ASSERT_EQ(statusName(store.load(source.binding, restored)),
                  std::string("loaded"));
  LOGOS_ASSERT_TRUE(sameRecord(restored, source));
}

LOGOS_TEST(
    storage_mvp_catalog_store_local_committed_allows_verified_empty_block_advance) {
  const palace::PalaceStorageMvpCatalogRecordV1 source = record();
  TemporaryDirectory root("palace-storage-mvp-catalog-local-advance");
  palace::PalaceStorageMvpCatalogStore store(root.path().string());
  LOGOS_ASSERT_EQ(statusName(store.save(source)), std::string("saved"));

  palace::PalaceStorageMvpCatalogBindingV1 advanced = source.binding;
  ++advanced.finalizedCheckpoint;
  advanced.finalizedHash = std::string(64U, '1');

  palace::PalaceStorageMvpCatalogRecordV1 restored;
  // Public-finality loading remains an exact tip/hash comparison.
  LOGOS_ASSERT_EQ(statusName(store.load(advanced, restored)),
                  std::string("binding_mismatch"));
  LOGOS_ASSERT_EQ(statusName(store.loadLocalCommitted(advanced, restored)),
                  std::string("loaded"));
  LOGOS_ASSERT_TRUE(sameRecord(restored, source));

  const auto rejected = [&](const palace::PalaceStorageMvpCatalogBindingV1
                                &expected) {
    palace::PalaceStorageMvpCatalogRecordV1 retained = source;
    retained.binding.networkId = "retained-sentinel";
    const palace::PalaceStorageMvpCatalogRecordV1 before = retained;
    LOGOS_ASSERT_EQ(statusName(store.loadLocalCommitted(expected, retained)),
                    std::string("binding_mismatch"));
    LOGOS_ASSERT_TRUE(sameRecord(retained, before));
  };

  palace::PalaceStorageMvpCatalogBindingV1 sameHeightDifferentHash =
      source.binding;
  sameHeightDifferentHash.finalizedHash = std::string(64U, '2');
  rejected(sameHeightDifferentHash);

  palace::PalaceStorageMvpCatalogBindingV1 behind = source.binding;
  --behind.finalizedCheckpoint;
  behind.finalizedHash = std::string(64U, '3');
  rejected(behind);

  std::vector<palace::PalaceStorageMvpCatalogBindingV1> differentAuthority;
  palace::PalaceStorageMvpCatalogBindingV1 changed = advanced;
  changed.networkId = "other-network";
  differentAuthority.push_back(changed);
  changed = advanced;
  changed.programIdHex[0] = 'f';
  differentAuthority.push_back(changed);
  changed = advanced;
  changed.rootAccountIdHex[0] = 'f';
  differentAuthority.push_back(changed);
  changed = advanced;
  changed.rootManifestCid =
      "QmNLfbof5rLekrACjeuLk9JmGZD2HDBHCU4z16iYKmx5SE";
  differentAuthority.push_back(changed);
  for (const auto &different : differentAuthority)
    rejected(different);
}

LOGOS_TEST(
    storage_mvp_catalog_store_restores_transport_only_until_exact_bytes) {
  palace::PalaceStorageMvpBundle published;
  LOGOS_ASSERT_TRUE(published.initialize(
      encodedPng(11, 7, qRgb(0x10, 0x30, 0x50)),
      encodedPng(13, 9, qRgb(0x60, 0x40, 0x20))));
  LOGOS_ASSERT_TRUE(publishAll(published));
  const palace::PalaceStorageMvpArtifactV1 *root =
      published.artifact("palace-1");
  LOGOS_ASSERT_TRUE(root != nullptr);

  palace::PalaceStorageMvpCatalogRecordV1 source;
  source.binding = binding();
  source.binding.rootManifestCid = root->cid;
  source.canonicalCatalog = published.canonicalCatalog();
  source.catalogChecksumHex =
      palace::crypto::sha256Hex(source.canonicalCatalog);

  TemporaryDirectory directory("palace-storage-mvp-catalog-rehydrate");
  palace::PalaceStorageMvpCatalogStore store(directory.path().string());
  LOGOS_ASSERT_EQ(statusName(store.save(source)), std::string("saved"));

  palace::PalaceStorageMvpCatalogRecordV1 restoredRecord;
  LOGOS_ASSERT_EQ(statusName(store.load(source.binding, restoredRecord)),
                  std::string("loaded"));
  palace::PalaceStorageMvpBundle restored;
  LOGOS_ASSERT_TRUE(
      restored.restoreCanonicalCatalog(restoredRecord.canonicalCatalog));
  LOGOS_ASSERT_TRUE(restored.complete());
  LOGOS_ASSERT_FALSE(restored.fetchedContentValid());
  LOGOS_ASSERT_TRUE(restored.artifact("background-atrium")->bytes.empty());
  LOGOS_ASSERT_TRUE(restored.artifact("background-lounge")->bytes.empty());

  for (const palace::PalaceStorageMvpArtifactV1 &artifact :
       published.artifacts()) {
    LOGOS_ASSERT_TRUE(
        restored.acceptFetchedBytes(artifact.objectId, artifact.bytes));
  }
  LOGOS_ASSERT_TRUE(restored.fetchedContentValid());
}

LOGOS_TEST(storage_mvp_catalog_recovery_keeps_stale_idle_and_fails_closed) {
  using Action = palace::PalaceStorageMvpCatalogRecoveryAction;
  using Status = palace::PalaceStorageMvpCatalogStoreStatus;

  LOGOS_ASSERT_EQ(
      static_cast<int>(palace::palaceStorageMvpCatalogRecoveryAction(
          Status::Loaded)),
      static_cast<int>(Action::Restore));
  LOGOS_ASSERT_EQ(
      static_cast<int>(palace::palaceStorageMvpCatalogRecoveryAction(
          Status::NotFound)),
      static_cast<int>(Action::Idle));
  LOGOS_ASSERT_EQ(
      static_cast<int>(palace::palaceStorageMvpCatalogRecoveryAction(
          Status::BindingMismatch)),
      static_cast<int>(Action::Idle));
  LOGOS_ASSERT_EQ(
      static_cast<int>(palace::palaceStorageMvpCatalogRecoveryAction(
          Status::InvalidRecord)),
      static_cast<int>(Action::Degrade));
  LOGOS_ASSERT_EQ(
      static_cast<int>(palace::palaceStorageMvpCatalogRecoveryAction(
          Status::InsecurePermissions)),
      static_cast<int>(Action::Degrade));
  LOGOS_ASSERT_EQ(
      static_cast<int>(palace::palaceStorageMvpCatalogRecoveryAction(
          Status::UnsafePath)),
      static_cast<int>(Action::Degrade));
}

LOGOS_TEST(storage_mvp_catalog_store_rejects_invalid_catalog_and_binding) {
  const palace::PalaceStorageMvpCatalogRecordV1 source = record();
  TemporaryDirectory root("palace-storage-mvp-catalog-validation");
  palace::PalaceStorageMvpCatalogStore store(root.path().string());

  palace::PalaceStorageMvpCatalogStore invalidRoot("");
  LOGOS_ASSERT_EQ(statusName(invalidRoot.save(source)),
                  std::string("invalid_argument"));

  palace::PalaceStorageMvpCatalogRecordV1 invalid = source;
  invalid.binding.networkId.clear();
  LOGOS_ASSERT_EQ(statusName(store.save(invalid)),
                  std::string("catalog_rejected"));
  LOGOS_ASSERT_FALSE(fs::exists(root.path()));

  invalid = source;
  invalid.canonicalCatalog = "not-canonical";
  invalid.catalogChecksumHex =
      palace::crypto::sha256Hex(invalid.canonicalCatalog);
  LOGOS_ASSERT_EQ(statusName(store.save(invalid)),
                  std::string("catalog_rejected"));

  invalid = source;
  invalid.canonicalCatalog = "binary";
  invalid.canonicalCatalog.push_back('\0');
  invalid.canonicalCatalog.push_back('\n');
  invalid.catalogChecksumHex =
      palace::crypto::sha256Hex(invalid.canonicalCatalog);
  LOGOS_ASSERT_EQ(statusName(store.save(invalid)),
                  std::string("catalog_rejected"));

  invalid = source;
  invalid.canonicalCatalog.assign(
      palace::PalaceStorageMvpCatalogStore::MaximumCatalogBytes, 'a');
  invalid.canonicalCatalog.back() = '\n';
  invalid.catalogChecksumHex =
      palace::crypto::sha256Hex(invalid.canonicalCatalog);
  LOGOS_ASSERT_EQ(statusName(store.save(invalid)),
                  std::string("catalog_rejected"));

  LOGOS_ASSERT_EQ(statusName(store.save(source)), std::string("saved"));

  std::vector<palace::PalaceStorageMvpCatalogBindingV1> mismatches;
  palace::PalaceStorageMvpCatalogBindingV1 changed = source.binding;
  changed.networkId = "other-network";
  mismatches.push_back(changed);
  changed = source.binding;
  changed.programIdHex[0] = 'f';
  mismatches.push_back(changed);
  changed = source.binding;
  changed.rootAccountIdHex[0] = 'f';
  mismatches.push_back(changed);
  changed = source.binding;
  ++changed.finalizedCheckpoint;
  mismatches.push_back(changed);
  changed = source.binding;
  changed.finalizedHash[0] = 'f';
  mismatches.push_back(changed);
  changed = source.binding;
  changed.rootManifestCid = "QmNLfbof5rLekrACjeuLk9JmGZD2HDBHCU4z16iYKmx5SE";
  mismatches.push_back(changed);

  for (const auto &expected : mismatches) {
    palace::PalaceStorageMvpCatalogRecordV1 retained = source;
    retained.binding.networkId = "retained-sentinel";
    const palace::PalaceStorageMvpCatalogRecordV1 before = retained;
    LOGOS_ASSERT_EQ(statusName(store.load(expected, retained)),
                    std::string("binding_mismatch"));
    LOGOS_ASSERT_TRUE(sameRecord(retained, before));
  }

  palace::PalaceStorageMvpCatalogBindingV1 invalidExpected = source.binding;
  invalidExpected.rootManifestCid.clear();
  palace::PalaceStorageMvpCatalogRecordV1 retained = source;
  const palace::PalaceStorageMvpCatalogRecordV1 before = retained;
  LOGOS_ASSERT_EQ(statusName(store.load(invalidExpected, retained)),
                  std::string("invalid_argument"));
  LOGOS_ASSERT_TRUE(sameRecord(retained, before));
}

LOGOS_TEST(storage_mvp_catalog_store_rejects_partial_or_tampered_record) {
  const palace::PalaceStorageMvpCatalogRecordV1 source = record();
  TemporaryDirectory root("palace-storage-mvp-catalog-corruption");
  palace::PalaceStorageMvpCatalogStore store(root.path().string());
  LOGOS_ASSERT_EQ(statusName(store.save(source)), std::string("saved"));
  const fs::path recordPath = root.path() / kRecordFileName;
  const std::vector<std::uint8_t> valid = readBytes(recordPath);
  LOGOS_ASSERT_GT(valid.size(), static_cast<std::size_t>(100U));

  const auto rejected = [&](const std::vector<std::uint8_t> &altered) {
    writeBytes(recordPath, altered);
    palace::PalaceStorageMvpCatalogRecordV1 retained = source;
    retained.binding.networkId = "retained-sentinel";
    const palace::PalaceStorageMvpCatalogRecordV1 before = retained;
    LOGOS_ASSERT_EQ(statusName(store.load(source.binding, retained)),
                    std::string("invalid_record"));
    LOGOS_ASSERT_TRUE(sameRecord(retained, before));
  };

  std::vector<std::uint8_t> truncated = valid;
  truncated.pop_back();
  rejected(truncated);

  std::vector<std::uint8_t> trailing = valid;
  trailing.push_back(0U);
  rejected(trailing);

  std::vector<std::uint8_t> wrongVersion = valid;
  wrongVersion[8U] ^= 0x01U;
  rejected(wrongVersion);

  std::vector<std::uint8_t> wrongLength = valid;
  wrongLength[12U] ^= 0x01U;
  rejected(wrongLength);

  std::vector<std::uint8_t> wrongOuterChecksum = valid;
  wrongOuterChecksum.back() ^= 0x01U;
  rejected(wrongOuterChecksum);

  std::vector<std::uint8_t> wrongCatalogChecksum = valid;
  const auto checksum = std::search(
      wrongCatalogChecksum.begin(), wrongCatalogChecksum.end(),
      source.catalogChecksumHex.begin(), source.catalogChecksumHex.end());
  LOGOS_ASSERT_TRUE(checksum != wrongCatalogChecksum.end());
  *checksum = *checksum == 'a' ? 'b' : 'a';
  replaceOuterChecksum(wrongCatalogChecksum);
  rejected(wrongCatalogChecksum);

  std::vector<std::uint8_t> oversized(20U * 1024U, 0U);
  rejected(oversized);
}

LOGOS_TEST(storage_mvp_catalog_store_rejects_symlinks_and_insecure_paths) {
#if defined(__unix__) || defined(__APPLE__)
  const palace::PalaceStorageMvpCatalogRecordV1 source = record();
  TemporaryDirectory root("palace-storage-mvp-catalog-symlink");
  fs::create_directories(root.path());
  const fs::path outside = root.path().parent_path() /
                           (root.path().filename().string() + "-outside");
  {
    std::error_code error;
    fs::remove(outside, error);
  }
  writeText(outside, "outside-must-not-change");
  fs::create_symlink(outside, root.path() / kRecordFileName);

  palace::PalaceStorageMvpCatalogStore store(root.path().string());
  palace::PalaceStorageMvpCatalogRecordV1 restored;
  LOGOS_ASSERT_EQ(statusName(store.load(source.binding, restored)),
                  std::string("unsafe_path"));
  LOGOS_ASSERT_EQ(statusName(store.save(source)), std::string("unsafe_path"));
  const std::vector<std::uint8_t> outsideBytes = readBytes(outside);
  LOGOS_ASSERT_EQ(std::string(outsideBytes.begin(), outsideBytes.end()),
                  std::string("outside-must-not-change"));

  fs::remove(root.path() / kRecordFileName);
  LOGOS_ASSERT_EQ(statusName(store.save(source)), std::string("saved"));
  const fs::path recordPath = root.path() / kRecordFileName;
  LOGOS_ASSERT_EQ(::chmod(recordPath.c_str(), 0644), 0);
  LOGOS_ASSERT_EQ(statusName(store.load(source.binding, restored)),
                  std::string("insecure_permissions"));
  LOGOS_ASSERT_EQ(statusName(store.save(source)),
                  std::string("insecure_permissions"));

  fs::remove(recordPath);
  LOGOS_ASSERT_EQ(::chmod(root.path().c_str(), 0770), 0);
  LOGOS_ASSERT_EQ(statusName(store.save(source)),
                  std::string("insecure_permissions"));
  LOGOS_ASSERT_EQ(::chmod(root.path().c_str(), 0700), 0);

  TemporaryDirectory linkedRoot("palace-storage-mvp-catalog-linked-root");
  TemporaryDirectory linkedTarget("palace-storage-mvp-catalog-linked-target");
  fs::create_directories(linkedTarget.path());
  fs::create_directory_symlink(linkedTarget.path(), linkedRoot.path());
  palace::PalaceStorageMvpCatalogStore linkedStore(linkedRoot.path().string());
  LOGOS_ASSERT_EQ(statusName(linkedStore.load(source.binding, restored)),
                  std::string("unsafe_path"));
  LOGOS_ASSERT_EQ(statusName(linkedStore.save(source)),
                  std::string("unsafe_path"));
  LOGOS_ASSERT_FALSE(fs::exists(linkedTarget.path() / kRecordFileName));

  std::error_code error;
  fs::remove(outside, error);
#endif
}
