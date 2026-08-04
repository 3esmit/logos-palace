#include <logos_test.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "palace_lez.h"
#include "palace_lez_authority_bundle_store.h"
#include "palace_sha256.h"

namespace {

namespace fs = std::filesystem;

constexpr char kRecordFileName[] = "lez-authority-bundle-v1";
constexpr char kProgramId[] =
    "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";
constexpr char kOtherProgramId[] =
    "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f";

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(const std::string& label)
        : path_(
            fs::temp_directory_path()
            / (label + "-"
               + std::to_string(
                   std::chrono::steady_clock::now()
                       .time_since_epoch()
                       .count())))
    {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& path() const
    {
        return path_;
    }

private:
    fs::path path_;
};

palace::PalaceLezBytes32 bytes(const std::uint8_t value)
{
    palace::PalaceLezBytes32 output{};
    output.fill(value);
    return output;
}

std::string hex(const std::vector<std::uint8_t>& value)
{
    constexpr char alphabet[] = "0123456789abcdef";
    std::string output(value.size() * 2U, '0');
    for (std::size_t index = 0U; index < value.size(); ++index) {
        output[index * 2U] = alphabet[value[index] >> 4U];
        output[index * 2U + 1U] =
            alphabet[value[index] & 0x0fU];
    }
    return output;
}

std::string rootJson(
    const std::string& programId,
    const std::uint64_t lastOrderedActionId)
{
    palace::PalaceLezRootRecordV3 root;
    root.palaceId = bytes(0x10U);
    root.title = "Logos Palace";
    root.owner = bytes(0x20U);
    root.entryRoomId = bytes(0x30U);
    root.roomIds = {bytes(0x30U), bytes(0x40U)};
    root.activeManifestCid = "bafymanifest";
    root.userCount = 3U;
    root.grantCount = 2U;
    root.banCount = 1U;
    root.sharedStateCount = 1U;
    root.revision = lastOrderedActionId + 1U;
    root.lastOrderedActionId = lastOrderedActionId;
    const std::vector<std::uint8_t> encoded =
        palace::PalaceLezCodec::encodeRootRecord(root);
    LOGOS_ASSERT_FALSE(encoded.empty());
    return "{\"program_owner\":\"" + programId
        + "\",\"balance\":\"00000000000000000000000000000000\","
          "\"nonce\":\"00000000000000000000000000000000\",\"data\":\""
        + hex(encoded) + "\"}";
}

palace::PalaceLezFinalizedAuthorityBundleV1 bundle()
{
    palace::PalaceLezFinalizedAuthorityBundleV1 result;
    result.scope.networkId = "lez-testnet";
    result.scope.programIdHex = kProgramId;
    result.scope.rootAccountIdHex =
        palace::PalaceLezCodec::deriveRootPda(kProgramId);
    LOGOS_ASSERT_EQ(result.scope.rootAccountIdHex.size(), 64U);
    result.checkpoint.finalizedBlockId = 41029U;
    result.checkpoint.finalizedBlockHeight = 41029U;
    result.checkpoint.finalizedBlockHashHex =
        "0ea1852f8c91d9ba8003844d1c98c68ba1b6ddc08a7c9dbf23dec95eb0460b92";
    result.checkpoint.lastOrderedActionId = 7U;
    result.accounts.push_back({
        result.scope.rootAccountIdHex,
        rootJson(kProgramId, result.checkpoint.lastOrderedActionId),
    });
    return result;
}

palace::PalaceLezAuthorityBundleExpectationV1 expectation(
    const palace::PalaceLezFinalizedAuthorityBundleV1& value,
    const bool pinCheckpoint = true)
{
    palace::PalaceLezAuthorityBundleExpectationV1 result;
    result.scope = value.scope;
    if (pinCheckpoint)
        result.checkpoint = value.checkpoint;
    return result;
}

std::string statusName(
    const palace::PalaceLezAuthorityBundleStoreStatus status)
{
    return palace::palaceLezAuthorityBundleStoreStatusName(status);
}

bool sameBundle(
    const palace::PalaceLezFinalizedAuthorityBundleV1& left,
    const palace::PalaceLezFinalizedAuthorityBundleV1& right)
{
    if (left.scope.networkId != right.scope.networkId
        || left.scope.programIdHex != right.scope.programIdHex
        || left.scope.rootAccountIdHex != right.scope.rootAccountIdHex
        || left.checkpoint.finalizedBlockId
            != right.checkpoint.finalizedBlockId
        || left.checkpoint.finalizedBlockHeight
            != right.checkpoint.finalizedBlockHeight
        || left.checkpoint.finalizedBlockHashHex
            != right.checkpoint.finalizedBlockHashHex
        || left.checkpoint.lastOrderedActionId
            != right.checkpoint.lastOrderedActionId
        || left.accounts.size() != right.accounts.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.accounts.size(); ++index) {
        if (left.accounts[index].accountIdHex
                != right.accounts[index].accountIdHex
            || left.accounts[index].responseJson
                != right.accounts[index].responseJson) {
            return false;
        }
    }
    return true;
}

std::vector<std::uint8_t> readBytes(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

void ownerOnly(const fs::path& path)
{
#if defined(__unix__) || defined(__APPLE__)
    LOGOS_ASSERT_EQ(::chmod(path.c_str(), 0600), 0);
#else
    static_cast<void>(path);
#endif
}

void writeBytes(
    const fs::path& path,
    const std::vector<std::uint8_t>& value)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(value.data()),
        static_cast<std::streamsize>(value.size()));
    output.close();
    LOGOS_ASSERT_TRUE(output.good());
    ownerOnly(path);
}

void writeText(const fs::path& path, const std::string& value)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << value;
    output.close();
    LOGOS_ASSERT_TRUE(output.good());
    ownerOnly(path);
}

void replaceChecksum(std::vector<std::uint8_t>& record)
{
    constexpr std::size_t checksumBytes = 64U;
    LOGOS_ASSERT_GT(record.size(), checksumBytes);
    const std::size_t checksumOffset = record.size() - checksumBytes;
    const std::string body(
        reinterpret_cast<const char*>(record.data()), checksumOffset);
    const std::string checksum = palace::crypto::sha256Hex(body);
    LOGOS_ASSERT_EQ(checksum.size(), checksumBytes);
    std::copy(
        checksum.begin(),
        checksum.end(),
        record.begin() + static_cast<std::ptrdiff_t>(checksumOffset));
}

} // namespace

LOGOS_TEST(lez_authority_bundle_store_roundtrips_exact_finalized_accounts) {
    const palace::PalaceLezFinalizedAuthorityBundleV1 source = bundle();
    TemporaryDirectory root("palace-lez-authority-roundtrip");
    palace::PalaceLezAuthorityBundleStore store(
        root.path().string(), expectation(source));

    LOGOS_ASSERT_EQ(
        statusName(store.save(source)), std::string("saved"));
    const fs::path recordPath = root.path() / kRecordFileName;
    LOGOS_ASSERT_TRUE(fs::is_regular_file(recordPath));

#if defined(__unix__) || defined(__APPLE__)
    struct stat status {};
    LOGOS_ASSERT_EQ(::stat(recordPath.c_str(), &status), 0);
    LOGOS_ASSERT_EQ(
        static_cast<unsigned int>(status.st_mode & 0777),
        static_cast<unsigned int>(0600));
#endif

    palace::PalaceLezFinalizedAuthorityBundleV1 restored;
    LOGOS_ASSERT_EQ(
        statusName(store.load(restored)), std::string("loaded"));
    LOGOS_ASSERT_TRUE(sameBundle(restored, source));
    LOGOS_ASSERT_EQ(
        restored.accounts.front().responseJson,
        source.accounts.front().responseJson);

    const palace::PalaceLezPublicAccountV3 decoded =
        palace::PalaceLezCodec::decodePublicAccount(
            restored.accounts.front().responseJson,
            restored.scope.programIdHex);
    LOGOS_ASSERT_TRUE(decoded.accepted);
    LOGOS_ASSERT_EQ(
        static_cast<std::uint8_t>(decoded.recordType),
        static_cast<std::uint8_t>(
            palace::PalaceLezRecordTypeV3::PalaceRoot));

    palace::PalaceLezAuthorityBundleStore unpinnedStore(
        root.path().string(), expectation(source, false));
    palace::PalaceLezFinalizedAuthorityBundleV1 unpinned;
    LOGOS_ASSERT_EQ(
        statusName(unpinnedStore.load(unpinned)),
        std::string("loaded"));
    LOGOS_ASSERT_TRUE(sameBundle(unpinned, source));
}

LOGOS_TEST(lez_authority_bundle_store_rejects_invalid_input_and_bindings) {
    const palace::PalaceLezFinalizedAuthorityBundleV1 source = bundle();
    TemporaryDirectory root("palace-lez-authority-validation");
    palace::PalaceLezAuthorityBundleStore store(
        root.path().string(), expectation(source));

    palace::PalaceLezAuthorityBundleStore invalidRoot(
        "", expectation(source));
    LOGOS_ASSERT_EQ(
        statusName(invalidRoot.save(source)),
        std::string("invalid_argument"));

    palace::PalaceLezAuthorityBundleExpectationV1 invalidExpected =
        expectation(source);
    invalidExpected.scope.networkId.clear();
    palace::PalaceLezAuthorityBundleStore invalidExpectation(
        root.path().string(), invalidExpected);
    LOGOS_ASSERT_EQ(
        statusName(invalidExpectation.save(source)),
        std::string("invalid_argument"));

    palace::PalaceLezFinalizedAuthorityBundleV1 invalid = source;
    invalid.accounts.push_back(invalid.accounts.front());
    LOGOS_ASSERT_EQ(
        statusName(store.save(invalid)),
        std::string("bundle_rejected"));
    LOGOS_ASSERT_FALSE(fs::exists(root.path()));

    invalid = source;
    invalid.checkpoint.finalizedBlockHeight += 1U;
    LOGOS_ASSERT_EQ(
        statusName(store.save(invalid)),
        std::string("bundle_rejected"));

    invalid = source;
    invalid.checkpoint.lastOrderedActionId += 1U;
    palace::PalaceLezAuthorityBundleStore unpinned(
        root.path().string(), expectation(source, false));
    LOGOS_ASSERT_EQ(
        statusName(unpinned.save(invalid)),
        std::string("bundle_rejected"));

    invalid = source;
    invalid.accounts.front().responseJson =
        rootJson(kOtherProgramId, source.checkpoint.lastOrderedActionId);
    LOGOS_ASSERT_EQ(
        statusName(store.save(invalid)),
        std::string("bundle_rejected"));

    invalid = source;
    invalid.accounts.front().accountIdHex =
        std::string(64U, '7');
    LOGOS_ASSERT_EQ(
        statusName(store.save(invalid)),
        std::string("bundle_rejected"));

    invalid = source;
    invalid.scope.networkId = "another-network";
    LOGOS_ASSERT_EQ(
        statusName(store.save(invalid)),
        std::string("binding_mismatch"));

    LOGOS_ASSERT_EQ(
        statusName(store.save(source)), std::string("saved"));
    palace::PalaceLezFinalizedAuthorityBundleV1 retained = source;
    retained.scope.networkId = "retained-sentinel";
    const palace::PalaceLezFinalizedAuthorityBundleV1 before = retained;

    palace::PalaceLezAuthorityBundleExpectationV1 wrong =
        expectation(source, false);
    wrong.scope.networkId = "wrong-network";
    palace::PalaceLezAuthorityBundleStore wrongNetwork(
        root.path().string(), wrong);
    LOGOS_ASSERT_EQ(
        statusName(wrongNetwork.load(retained)),
        std::string("binding_mismatch"));
    LOGOS_ASSERT_TRUE(sameBundle(retained, before));

    wrong = expectation(source, false);
    wrong.scope.programIdHex = kOtherProgramId;
    palace::PalaceLezAuthorityBundleStore wrongProgram(
        root.path().string(), wrong);
    LOGOS_ASSERT_EQ(
        statusName(wrongProgram.load(retained)),
        std::string("binding_mismatch"));
    LOGOS_ASSERT_TRUE(sameBundle(retained, before));

    wrong = expectation(source, false);
    wrong.scope.rootAccountIdHex = std::string(64U, '6');
    palace::PalaceLezAuthorityBundleStore wrongRoot(
        root.path().string(), wrong);
    LOGOS_ASSERT_EQ(
        statusName(wrongRoot.load(retained)),
        std::string("binding_mismatch"));
    LOGOS_ASSERT_TRUE(sameBundle(retained, before));

    wrong = expectation(source);
    wrong.checkpoint->finalizedBlockHashHex = std::string(64U, 'a');
    palace::PalaceLezAuthorityBundleStore wrongCheckpoint(
        root.path().string(), wrong);
    LOGOS_ASSERT_EQ(
        statusName(wrongCheckpoint.load(retained)),
        std::string("checkpoint_mismatch"));
    LOGOS_ASSERT_TRUE(sameBundle(retained, before));
}

LOGOS_TEST(lez_authority_bundle_store_rejects_corruption_without_mutation) {
    const palace::PalaceLezFinalizedAuthorityBundleV1 source = bundle();
    TemporaryDirectory root("palace-lez-authority-corruption");
    palace::PalaceLezAuthorityBundleStore store(
        root.path().string(), expectation(source));
    LOGOS_ASSERT_EQ(
        statusName(store.save(source)), std::string("saved"));
    const fs::path recordPath = root.path() / kRecordFileName;
    const std::vector<std::uint8_t> valid = readBytes(recordPath);
    LOGOS_ASSERT_GT(valid.size(), static_cast<std::size_t>(100U));

    palace::PalaceLezFinalizedAuthorityBundleV1 retained = source;
    retained.scope.networkId = "retained-sentinel";
    const palace::PalaceLezFinalizedAuthorityBundleV1 before = retained;
    const auto rejected = [&](const std::vector<std::uint8_t>& record) {
        writeBytes(recordPath, record);
        LOGOS_ASSERT_EQ(
            statusName(store.load(retained)),
            std::string("invalid_record"));
        LOGOS_ASSERT_TRUE(sameBundle(retained, before));
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

    std::vector<std::uint8_t> wrongChecksum = valid;
    wrongChecksum.back() ^= 0x01U;
    rejected(wrongChecksum);

    std::vector<std::uint8_t> invalidPayload = valid;
    // First payload field is a little-endian length at offset 16.
    invalidPayload[16U] = 0xffU;
    invalidPayload[17U] = 0xffU;
    replaceChecksum(invalidPayload);
    rejected(invalidPayload);

    std::vector<std::uint8_t> oversized(
        16U * 1024U * 1024U + 128U, 0U);
    rejected(oversized);
}

LOGOS_TEST(lez_authority_bundle_store_rejects_symlinks_and_insecure_files) {
#if defined(__unix__) || defined(__APPLE__)
    const palace::PalaceLezFinalizedAuthorityBundleV1 source = bundle();
    TemporaryDirectory root("palace-lez-authority-symlink");
    fs::create_directories(root.path());
    const fs::path outside = root.path().parent_path()
        / (root.path().filename().string() + "-outside");
    {
        std::error_code error;
        fs::remove(outside, error);
    }
    writeText(outside, "outside-must-not-change");
    fs::create_symlink(outside, root.path() / kRecordFileName);

    palace::PalaceLezAuthorityBundleStore store(
        root.path().string(), expectation(source));
    palace::PalaceLezFinalizedAuthorityBundleV1 restored;
    LOGOS_ASSERT_EQ(
        statusName(store.load(restored)), std::string("unsafe_path"));
    LOGOS_ASSERT_EQ(
        statusName(store.save(source)), std::string("unsafe_path"));
    const std::vector<std::uint8_t> outsideBytes = readBytes(outside);
    LOGOS_ASSERT_EQ(
        std::string(outsideBytes.begin(), outsideBytes.end()),
        std::string("outside-must-not-change"));

    fs::remove(root.path() / kRecordFileName);
    LOGOS_ASSERT_EQ(
        statusName(store.save(source)), std::string("saved"));
    LOGOS_ASSERT_EQ(
        ::chmod((root.path() / kRecordFileName).c_str(), 0644), 0);
    LOGOS_ASSERT_EQ(
        statusName(store.load(restored)),
        std::string("insecure_permissions"));
    LOGOS_ASSERT_EQ(
        statusName(store.save(source)),
        std::string("insecure_permissions"));

    TemporaryDirectory linkedRoot(
        "palace-lez-authority-linked-root");
    TemporaryDirectory linkedTarget(
        "palace-lez-authority-linked-target");
    fs::create_directories(linkedTarget.path());
    fs::create_directory_symlink(
        linkedTarget.path(), linkedRoot.path());
    palace::PalaceLezAuthorityBundleStore linkedStore(
        linkedRoot.path().string(), expectation(source));
    LOGOS_ASSERT_EQ(
        statusName(linkedStore.load(restored)),
        std::string("unsafe_path"));
    LOGOS_ASSERT_EQ(
        statusName(linkedStore.save(source)),
        std::string("unsafe_path"));
    LOGOS_ASSERT_FALSE(
        fs::exists(linkedTarget.path() / kRecordFileName));

    std::error_code error;
    fs::remove(outside, error);
#endif
}
