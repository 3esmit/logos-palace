#include "logos_test.h"

#include "palace_sha256.h"
#include "palace_storage.h"

LOGOS_TEST(storage_cid_boundary_accepts_only_bounded_alphanumeric_ids) {
    LOGOS_ASSERT_TRUE(palace::isSafePalaceCid("bafy1234"));
    LOGOS_ASSERT_FALSE(palace::isSafePalaceCid("../bafy"));
    LOGOS_ASSERT_FALSE(palace::isSafePalaceCid(std::string(129U, 'a')));
}

LOGOS_TEST(storage_upload_terminal_requires_correlated_session_and_safe_cid) {
    const palace::StorageUploadTerminal success = palace::parseStorageUploadDone(
        R"({"success":true,"sessionId":"session-1","cid":"bafy1234"})");
    LOGOS_ASSERT_TRUE(success.accepted);
    LOGOS_ASSERT_TRUE(success.succeeded);
    LOGOS_ASSERT_EQ(success.sessionId, std::string("session-1"));
    LOGOS_ASSERT_EQ(success.cid, std::string("bafy1234"));

    const palace::StorageUploadTerminal failed = palace::parseStorageUploadDone(
        R"({"success":false,"sessionId":"session-2","error":"offline"})");
    LOGOS_ASSERT_TRUE(failed.accepted);
    LOGOS_ASSERT_FALSE(failed.succeeded);
    LOGOS_ASSERT_FALSE(palace::parseStorageUploadDone(
        R"({"success":true,"sessionId":"session-3","cid":"../escape"})").accepted);
    LOGOS_ASSERT_FALSE(palace::parseStorageUploadDone(
        R"({"success":"yes","sessionId":"session-4","cid":"bafy1234"})").accepted);
}

namespace {

palace::AssetRefV1 validAsset() {
  palace::AssetRefV1 reference;
  reference.sourceCid =
      "bafybeiaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  reference.derivativeCid =
      "bafybeibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
  reference.byteLength = 64U;
  reference.mediaType = "image/png";
  reference.width = 8U;
  reference.height = 8U;
  reference.technicalProfile = "palace-png-v1";
  reference.contentSha256 =
      palace::crypto::sha256Hex("expected-download-digest");
  return reference;
}

} // namespace

LOGOS_TEST(
    storage_asset_queue_uses_core_generated_paths_and_unique_operation_ids) {
  palace::StorageAssetQueue queue;
  const palace::AssetRefV1 reference = validAsset();

  const auto first = queue.begin(reference, "/core/asset_downloads");
  LOGOS_ASSERT_TRUE(first.has_value());
  LOGOS_ASSERT_EQ(first->operationId, std::string("palace-asset-1"));
  LOGOS_ASSERT_EQ(first->destinationPath,
                  std::string("/core/asset_downloads/palace-asset-1.png"));

  LOGOS_ASSERT_FALSE(
      queue.begin(reference, "/core/asset_downloads").has_value());
  const auto completed = queue.take(first->operationId);
  LOGOS_ASSERT_TRUE(completed.has_value());
  LOGOS_ASSERT_EQ(completed->reference.derivativeCid, reference.derivativeCid);

  const auto retried = queue.begin(reference, "/core/asset_downloads");
  LOGOS_ASSERT_TRUE(retried.has_value());
  LOGOS_ASSERT_EQ(retried->operationId, std::string("palace-asset-2"));
}

LOGOS_TEST(
    storage_asset_queue_rejects_unbounded_or_path_like_asset_references) {
  palace::StorageAssetQueue queue;
  palace::AssetRefV1 invalid = validAsset();
  invalid.derivativeCid = "../../outside";
  LOGOS_ASSERT_FALSE(queue.begin(invalid, "/core/asset_downloads").has_value());

  invalid = validAsset();
  invalid.byteLength = 10U * 1024U * 1024U + 1U;
  LOGOS_ASSERT_FALSE(queue.begin(invalid, "/core/asset_downloads").has_value());

  invalid = validAsset();
  invalid.contentSha256 = "not-a-digest";
  LOGOS_ASSERT_FALSE(queue.begin(invalid, "/core/asset_downloads").has_value());
}
