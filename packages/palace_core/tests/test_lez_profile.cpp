#include <logos_test.h>

#include <QJsonDocument>
#include <QJsonObject>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#endif

#include "palace_lez_profile.h"

namespace {

namespace fs = std::filesystem;

constexpr char kBindingFileName[] = "lez-profile-binding-v1";
constexpr char kLegacyMigrationFileName[] =
    "lez-profile-legacy-direct-root-migration-v1";
constexpr char kLegacyMigrationHeader[] =
    "logos-palace-lez-profile-legacy-direct-root-migration-v1";

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(const std::string& label)
        : path_(fs::temp_directory_path()
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

void writeOwnerOnly(const fs::path& path, const std::string& value)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    output.close();
    LOGOS_ASSERT_TRUE(output.good());
#if defined(__unix__) || defined(__APPLE__)
    LOGOS_ASSERT_EQ(::chmod(path.c_str(), 0600), 0);
#endif
}

std::string readText(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
}

std::string statusName(const palace::PalaceLezProfileBindingStatusV1 status)
{
    return palace::palaceLezProfileBindingStatusNameV1(status);
}

void assertWalletConfigSchema(
    const palace::PalaceLezProfileV1& profile)
{
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray::fromStdString(profile.walletConfigJson));
    LOGOS_ASSERT_TRUE(document.isObject());
    const QJsonObject config = document.object();
    LOGOS_ASSERT_EQ(
        config.value(QStringLiteral("sequencer_addr")).toString().toStdString(),
        profile.expectedSequencerOrigin);
    LOGOS_ASSERT_TRUE(
        config.value(QStringLiteral("seq_poll_timeout")).isString());
    LOGOS_ASSERT_TRUE(
        config.value(QStringLiteral("seq_tx_poll_max_blocks")).isDouble());
    LOGOS_ASSERT_TRUE(
        config.value(QStringLiteral("seq_poll_max_retries")).isDouble());
    LOGOS_ASSERT_TRUE(
        config.value(QStringLiteral("seq_block_poll_max_amount")).isDouble());
    LOGOS_ASSERT_FALSE(config.contains(QStringLiteral("sequencers")));
    LOGOS_ASSERT_FALSE(config.contains(QStringLiteral("calibration_limit")));
}

} // namespace

LOGOS_TEST(lez_profile_defaults_to_release_and_isolates_local_development)
{
    TemporaryDirectory directory("palace-lez-profile-isolation");
    const palace::PalaceLezProfileBindingResultV1 release =
        palace::bindPalaceLezProfileV1(directory.path().string(), "");
    LOGOS_ASSERT_TRUE(release.accepted());
    LOGOS_ASSERT_TRUE(release.profile.has_value());
    LOGOS_ASSERT_EQ(release.profile->id, std::string("release"));
    LOGOS_ASSERT_TRUE(release.profile->publicFinalityAvailable);
    assertWalletConfigSchema(*release.profile);
    LOGOS_ASSERT_TRUE(release.profile->acceptsLiveModule(
        "lez_core",
        release.profile->network.moduleApiVersion,
        release.profile->expectedSequencerOrigin + "/"));

    const palace::PalaceLezProfileBindingResultV1 local =
        palace::bindPalaceLezProfileV1(
            directory.path().string(), "local-development");
    LOGOS_ASSERT_TRUE(local.accepted());
    LOGOS_ASSERT_TRUE(local.profile.has_value());
    LOGOS_ASSERT_EQ(local.profile->id, std::string("local-development"));
    LOGOS_ASSERT_FALSE(local.profile->publicFinalityAvailable);
    assertWalletConfigSchema(*local.profile);
    LOGOS_ASSERT_EQ(
        local.profile->expectedSequencerOrigin,
        std::string("http://127.0.0.1:3040"));
    LOGOS_ASSERT_TRUE(local.profile->walletConfigJson.find(
        "\"sequencer_addr\":\"http://127.0.0.1:3040\"")
        != std::string::npos);
    LOGOS_ASSERT_EQ(
        local.profile->walletConfigJson.find("\"sequencers\""),
        std::string::npos);
    LOGOS_ASSERT_EQ(
        local.profile->walletConfigJson.find("\"calibration_limit\""),
        std::string::npos);
    LOGOS_ASSERT_NE(release.persistenceRoot, local.persistenceRoot);
    LOGOS_ASSERT_TRUE(fs::is_regular_file(
        fs::path(release.persistenceRoot) / kBindingFileName));
    LOGOS_ASSERT_TRUE(fs::is_regular_file(
        fs::path(local.persistenceRoot) / kBindingFileName));

    const fs::path localAssetDirectory =
        fs::path(local.persistenceRoot) / "verified_assets" / "logos_palace_ui";
    fs::create_directories(localAssetDirectory);
    const palace::PalaceLezProfileBindingResultV1 localAgain =
        palace::bindPalaceLezProfileV1(
            directory.path().string(), "local-development");
    LOGOS_ASSERT_TRUE(localAgain.accepted());
    LOGOS_ASSERT_EQ(localAgain.persistenceRoot, local.persistenceRoot);

    const fs::path releaseAssetDirectory =
        fs::path(release.persistenceRoot)
        / "verified_assets" / "logos_palace_ui";
    fs::create_directories(releaseAssetDirectory);
    writeOwnerOnly(releaseAssetDirectory / "already-profiled.png", "release");
    const palace::PalaceLezProfileBindingResultV1 releaseAgain =
        palace::bindPalaceLezProfileV1(directory.path().string(), "release");
    LOGOS_ASSERT_TRUE(releaseAgain.accepted());
    LOGOS_ASSERT_EQ(releaseAgain.persistenceRoot, release.persistenceRoot);
}

LOGOS_TEST(lez_profile_rejects_unknown_selector)
{
    TemporaryDirectory unknownDirectory("palace-lez-profile-unknown");
    const palace::PalaceLezProfileBindingResultV1 unknown =
        palace::bindPalaceLezProfileV1(
            unknownDirectory.path().string(), "remote-development");
    LOGOS_ASSERT_FALSE(unknown.accepted());
    LOGOS_ASSERT_EQ(statusName(unknown.status), std::string("unknown-profile"));
    LOGOS_ASSERT_FALSE(fs::exists(unknownDirectory.path()));
}

LOGOS_TEST(lez_profile_migrates_release_direct_root_state_and_cache_directories)
{
    TemporaryDirectory directory("palace-lez-profile-legacy-release");
    fs::create_directories(directory.path() / "asset_downloads" / "nested");
    fs::create_directories(
        directory.path() / "storage_catalog_downloads" / "catalog");
    writeOwnerOnly(directory.path() / "projection-v1", "legacy-projection");
    writeOwnerOnly(
        directory.path() / "asset_downloads" / "nested" / "asset.png",
        "cached-asset");
    writeOwnerOnly(
        directory.path() / "storage_catalog_downloads" / "catalog" / "room.bin",
        "cached-catalog");

    const palace::PalaceLezProfileBindingResultV1 release =
        palace::bindPalaceLezProfileV1(directory.path().string(), "release");
    LOGOS_ASSERT_TRUE(release.accepted());
    const fs::path releaseRoot(release.persistenceRoot);
    LOGOS_ASSERT_EQ(
        readText(releaseRoot / "projection-v1"),
        std::string("legacy-projection"));
    LOGOS_ASSERT_EQ(
        readText(releaseRoot / "asset_downloads" / "nested" / "asset.png"),
        std::string("cached-asset"));
    LOGOS_ASSERT_EQ(
        readText(
            releaseRoot / "storage_catalog_downloads" / "catalog" / "room.bin"),
        std::string("cached-catalog"));
    LOGOS_ASSERT_FALSE(fs::exists(directory.path() / "projection-v1"));
    LOGOS_ASSERT_FALSE(fs::exists(directory.path() / "asset_downloads"));
    LOGOS_ASSERT_FALSE(
        fs::exists(directory.path() / "storage_catalog_downloads"));
    LOGOS_ASSERT_FALSE(fs::exists(releaseRoot / kLegacyMigrationFileName));

    const palace::PalaceLezProfileBindingResultV1 rebound =
        palace::bindPalaceLezProfileV1(directory.path().string(), "release");
    LOGOS_ASSERT_TRUE(rebound.accepted());
    LOGOS_ASSERT_EQ(rebound.persistenceRoot, release.persistenceRoot);
}

LOGOS_TEST(lez_profile_resumes_interrupted_release_direct_root_migration)
{
    TemporaryDirectory directory("palace-lez-profile-legacy-resume");
    const fs::path releaseRoot =
        directory.path() / "palace-profiles" / "release";
    fs::create_directories(releaseRoot);
    fs::create_directories(directory.path() / "asset_downloads");
    writeOwnerOnly(releaseRoot / "projection-v1", "moved-before-restart");
    writeOwnerOnly(
        directory.path() / "asset_downloads" / "pending.png",
        "move-after-restart");
    writeOwnerOnly(
        releaseRoot / kLegacyMigrationFileName,
        std::string(kLegacyMigrationHeader) + "\n"
            + "profile_id=release\n"
            + "entry=projection-v1\n"
            + "entry=asset_downloads\n");

    const palace::PalaceLezProfileBindingResultV1 resumed =
        palace::bindPalaceLezProfileV1(directory.path().string(), "release");
    LOGOS_ASSERT_TRUE(resumed.accepted());
    LOGOS_ASSERT_EQ(
        readText(fs::path(resumed.persistenceRoot) / "projection-v1"),
        std::string("moved-before-restart"));
    LOGOS_ASSERT_EQ(
        readText(
            fs::path(resumed.persistenceRoot) / "asset_downloads" / "pending.png"),
        std::string("move-after-restart"));
    LOGOS_ASSERT_FALSE(fs::exists(directory.path() / "asset_downloads"));
    LOGOS_ASSERT_FALSE(fs::exists(
        fs::path(resumed.persistenceRoot) / kLegacyMigrationFileName));
    LOGOS_ASSERT_TRUE(fs::is_regular_file(
        fs::path(resumed.persistenceRoot) / kBindingFileName));
}

LOGOS_TEST(lez_profile_blocks_local_profile_during_release_migration)
{
    TemporaryDirectory directory("palace-lez-profile-local-migration-block");
    const fs::path releaseRoot =
        directory.path() / "palace-profiles" / "release";
    fs::create_directories(releaseRoot);
    writeOwnerOnly(releaseRoot / "projection-v1", "moved-before-binding");
    writeOwnerOnly(
        releaseRoot / kLegacyMigrationFileName,
        std::string(kLegacyMigrationHeader) + "\n"
            + "profile_id=release\n"
            + "entry=projection-v1\n");

    const palace::PalaceLezProfileBindingResultV1 local =
        palace::bindPalaceLezProfileV1(
            directory.path().string(), "local-development");
    LOGOS_ASSERT_FALSE(local.accepted());
    LOGOS_ASSERT_EQ(
        local.reason,
        std::string("legacy-direct-root-migration-in-progress"));
    LOGOS_ASSERT_FALSE(fs::exists(
        directory.path() / "palace-profiles" / "local-development"));
}

LOGOS_TEST(lez_profile_rejects_local_direct_root_state_and_conflicting_upgrade)
{
    TemporaryDirectory legacyDirectory("palace-lez-profile-local-legacy");
    fs::create_directories(legacyDirectory.path() / "asset_downloads");
    writeOwnerOnly(
        legacyDirectory.path() / "asset_downloads" / "legacy.png", "legacy");
    const palace::PalaceLezProfileBindingResultV1 legacy =
        palace::bindPalaceLezProfileV1(
            legacyDirectory.path().string(), "local-development");
    LOGOS_ASSERT_FALSE(legacy.accepted());
    LOGOS_ASSERT_EQ(
        statusName(legacy.status), std::string("legacy-direct-root-state"));
    LOGOS_ASSERT_TRUE(fs::exists(
        legacyDirectory.path() / "asset_downloads" / "legacy.png"));

    TemporaryDirectory conflictDirectory("palace-lez-profile-legacy-conflict");
    const fs::path conflictReleaseRoot =
        conflictDirectory.path() / "palace-profiles" / "release";
    fs::create_directories(conflictReleaseRoot);
    writeOwnerOnly(conflictDirectory.path() / "projection-v1", "source");
    writeOwnerOnly(
        conflictReleaseRoot / kLegacyMigrationFileName,
        std::string(kLegacyMigrationHeader) + "\n"
            + "profile_id=release\n"
            + "entry=projection-v1\n");
    writeOwnerOnly(conflictReleaseRoot / "projection-v1", "destination");
    const palace::PalaceLezProfileBindingResultV1 conflict =
        palace::bindPalaceLezProfileV1(
            conflictDirectory.path().string(), "release");
    LOGOS_ASSERT_FALSE(conflict.accepted());
    LOGOS_ASSERT_EQ(
        statusName(conflict.status), std::string("legacy-direct-root-state"));
    LOGOS_ASSERT_EQ(conflict.reason, std::string("legacy-direct-root-conflict"));
    LOGOS_ASSERT_EQ(
        readText(conflictDirectory.path() / "projection-v1"),
        std::string("source"));
    LOGOS_ASSERT_EQ(
        readText(conflictReleaseRoot / "projection-v1"),
        std::string("destination"));
    LOGOS_ASSERT_TRUE(fs::exists(
        conflictReleaseRoot / kLegacyMigrationFileName));
}

LOGOS_TEST(lez_profile_rejects_unsafe_legacy_direct_root_state)
{
#if defined(__unix__) || defined(__APPLE__)
    TemporaryDirectory directory("palace-lez-profile-legacy-symlink");
    const fs::path outside = directory.path() / "outside";
    fs::create_directories(outside);
    std::error_code error;
    fs::create_directory_symlink(
        outside, directory.path() / "verified_assets", error);
    LOGOS_ASSERT_FALSE(error);

    const palace::PalaceLezProfileBindingResultV1 unsafe =
        palace::bindPalaceLezProfileV1(directory.path().string(), "release");
    LOGOS_ASSERT_FALSE(unsafe.accepted());
    LOGOS_ASSERT_EQ(
        statusName(unsafe.status), std::string("legacy-direct-root-state"));
    LOGOS_ASSERT_EQ(unsafe.reason, std::string("legacy-direct-root-unsafe"));
    LOGOS_ASSERT_TRUE(fs::is_symlink(
        directory.path() / "verified_assets"));
#endif
}

LOGOS_TEST(lez_profile_rejects_unbound_nonempty_release_profile_root)
{
    TemporaryDirectory directory("palace-lez-profile-unbound-release");
    const fs::path releaseRoot =
        directory.path() / "palace-profiles" / "release";
    fs::create_directories(releaseRoot);
    writeOwnerOnly(releaseRoot / "projection-v1", "orphaned-state");

    const palace::PalaceLezProfileBindingResultV1 result =
        palace::bindPalaceLezProfileV1(directory.path().string(), "release");
    LOGOS_ASSERT_FALSE(result.accepted());
    LOGOS_ASSERT_EQ(
        statusName(result.status), std::string("legacy-direct-root-state"));
    LOGOS_ASSERT_EQ(result.reason, std::string("legacy-direct-root-conflict"));
    LOGOS_ASSERT_EQ(
        readText(releaseRoot / "projection-v1"),
        std::string("orphaned-state"));
    LOGOS_ASSERT_FALSE(fs::exists(releaseRoot / kBindingFileName));
}

LOGOS_TEST(lez_profile_rejects_modified_or_symlinked_binding)
{
    TemporaryDirectory directory("palace-lez-profile-binding");
    const palace::PalaceLezProfileBindingResultV1 bound =
        palace::bindPalaceLezProfileV1(directory.path().string(), "release");
    LOGOS_ASSERT_TRUE(bound.accepted());
    const fs::path bindingPath =
        fs::path(bound.persistenceRoot) / kBindingFileName;
    std::string modified = readText(bindingPath);
    LOGOS_ASSERT_FALSE(modified.empty());
    modified.replace(0U, std::string("logos").size(), "bogus");
    writeOwnerOnly(bindingPath, modified);

    const palace::PalaceLezProfileBindingResultV1 mismatch =
        palace::bindPalaceLezProfileV1(directory.path().string(), "release");
    LOGOS_ASSERT_FALSE(mismatch.accepted());
    LOGOS_ASSERT_EQ(statusName(mismatch.status), std::string("binding-mismatch"));

#if defined(__unix__) || defined(__APPLE__)
    TemporaryDirectory symlinkDirectory("palace-lez-profile-symlink");
    const palace::PalaceLezProfileBindingResultV1 symlinkBound =
        palace::bindPalaceLezProfileV1(symlinkDirectory.path().string(), "release");
    LOGOS_ASSERT_TRUE(symlinkBound.accepted());
    const fs::path symlinkBinding =
        fs::path(symlinkBound.persistenceRoot) / kBindingFileName;
    const fs::path outside = symlinkDirectory.path() / "outside-record";
    writeOwnerOnly(outside, readText(symlinkBinding));
    std::error_code error;
    fs::remove(symlinkBinding, error);
    LOGOS_ASSERT_FALSE(error);
    fs::create_symlink(outside, symlinkBinding, error);
    LOGOS_ASSERT_FALSE(error);

    const palace::PalaceLezProfileBindingResultV1 symlink =
        palace::bindPalaceLezProfileV1(
            symlinkDirectory.path().string(), "release");
    LOGOS_ASSERT_FALSE(symlink.accepted());
    LOGOS_ASSERT_EQ(statusName(symlink.status), std::string("binding-invalid"));
#endif
}
