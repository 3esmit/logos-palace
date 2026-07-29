#include <logos_test.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "palace_lez.h"
#include "palace_lez_submission_intent_store.h"

namespace {

namespace fs = std::filesystem;

constexpr const char* kProgramIdHex =
    "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";
constexpr const char* kSignerHex =
    "6666666666666666666666666666666666666666666666666666666666666666";

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(const std::string& name)
        : path_(
            fs::temp_directory_path()
            / (name + "-"
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

palace::PalaceLezSubmissionIntentV1 intent()
{
    palace::PalaceLezRegisterUserV3 registration;
    registration.orderedActionId = 1U;
    registration.profile.displayName = "Bob";
    registration.profile.deliveryKey.fill(0x22U);
    registration.profile.keyEpoch = 1U;

    palace::PalaceLezSubmissionIntentV1 value;
    value.actionId = "1";
    value.minimumFinalizedBlockExclusive = 42U;
    value.plan = palace::PalaceLezCodec::buildTransaction(
        kProgramIdHex,
        kSignerHex,
        palace::PalaceLezInstructionV3{registration});
    LOGOS_ASSERT_TRUE(value.plan.accepted);
    value.expectedRootDataSha256Hex = std::string(64U, 'a');
    return value;
}

} // namespace

LOGOS_TEST(lez_submission_intent_store_persists_exact_write_ahead_plan)
{
    TemporaryDirectory root("palace-lez-submission-intent");
    palace::PalaceLezSubmissionIntentStore store(
        root.path().string());

    palace::PalaceLezSubmissionIntentV1 prepared = intent();
    LOGOS_ASSERT_TRUE(
        store.save(prepared)
        == palace::PalaceLezSubmissionIntentStoreStatus::Saved);

    palace::PalaceLezSubmissionIntentV1 restored;
    LOGOS_ASSERT_EQ(
        std::string(palace::palaceLezSubmissionIntentStoreStatusName(
            store.load(restored))),
        std::string("loaded"));
    LOGOS_ASSERT_TRUE(
        palace::samePalaceLezSubmissionIntent(prepared, restored));

    restored.phase =
        palace::PalaceLezSubmissionIntentPhase::
            MayHaveBeenSubmitted;
    LOGOS_ASSERT_TRUE(
        store.save(restored)
        == palace::PalaceLezSubmissionIntentStoreStatus::Saved);

    restored.phase =
        palace::PalaceLezSubmissionIntentPhase::Committed;
    restored.transactionHash = std::string(64U, 'b');
    LOGOS_ASSERT_TRUE(
        store.save(restored)
        == palace::PalaceLezSubmissionIntentStoreStatus::Saved);
}

LOGOS_TEST(lez_submission_intent_store_rejects_plan_drift_after_send_boundary)
{
    TemporaryDirectory root("palace-lez-submission-intent-drift");
    palace::PalaceLezSubmissionIntentStore store(
        root.path().string());
    palace::PalaceLezSubmissionIntentV1 pending = intent();
    LOGOS_ASSERT_TRUE(
        store.save(pending)
        == palace::PalaceLezSubmissionIntentStoreStatus::Saved);
    pending.phase =
        palace::PalaceLezSubmissionIntentPhase::
            MayHaveBeenSubmitted;
    LOGOS_ASSERT_TRUE(
        store.save(pending)
        == palace::PalaceLezSubmissionIntentStoreStatus::Saved);

    palace::PalaceLezSubmissionIntentV1 drifted = pending;
    ++drifted.minimumFinalizedBlockExclusive;
    drifted.phase =
        palace::PalaceLezSubmissionIntentPhase::Committed;
    drifted.transactionHash = std::string(64U, 'c');
    LOGOS_ASSERT_TRUE(
        store.save(drifted)
        == palace::PalaceLezSubmissionIntentStoreStatus::
            InvalidArgument);

    palace::PalaceLezSubmissionIntentV1 restored;
    LOGOS_ASSERT_TRUE(
        store.load(restored)
        == palace::PalaceLezSubmissionIntentStoreStatus::Loaded);
    LOGOS_ASSERT_TRUE(
        restored.phase
        == palace::PalaceLezSubmissionIntentPhase::
            MayHaveBeenSubmitted);
    LOGOS_ASSERT_TRUE(restored.transactionHash.empty());
}

#if defined(__unix__) || defined(__APPLE__)
LOGOS_TEST(lez_submission_intent_store_rejects_final_symlink_and_corruption)
{
    TemporaryDirectory root("palace-lez-submission-intent-symlink");
    fs::create_directories(root.path());
    LOGOS_ASSERT_EQ(::chmod(root.path().c_str(), 0700), 0);
    const fs::path target = root.path() / "outside";
    {
        std::ofstream output(target, std::ios::binary);
        output << "do-not-overwrite";
    }
    const fs::path record =
        root.path() / "lez-submission-intent-v1";
    fs::create_symlink(target, record);

    palace::PalaceLezSubmissionIntentStore store(
        root.path().string());
    LOGOS_ASSERT_TRUE(
        store.save(intent())
        == palace::PalaceLezSubmissionIntentStoreStatus::
            InvalidRecord);
    std::ifstream input(target, std::ios::binary);
    const std::string untouched{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    LOGOS_ASSERT_EQ(untouched, std::string("do-not-overwrite"));

    fs::remove(record);
    LOGOS_ASSERT_TRUE(
        store.save(intent())
        == palace::PalaceLezSubmissionIntentStoreStatus::Saved);
    {
        std::fstream corrupt(
            record,
            std::ios::binary | std::ios::in | std::ios::out);
        corrupt.seekp(0, std::ios::beg);
        corrupt.put('X');
    }
    palace::PalaceLezSubmissionIntentV1 loaded;
    LOGOS_ASSERT_TRUE(
        store.load(loaded)
        == palace::PalaceLezSubmissionIntentStoreStatus::
            InvalidRecord);
    LOGOS_ASSERT_TRUE(
        store.save(intent())
        == palace::PalaceLezSubmissionIntentStoreStatus::
            InvalidRecord);
}

LOGOS_TEST(lez_submission_intent_store_never_follows_colliding_temp_symlinks)
{
    TemporaryDirectory root("palace-lez-submission-intent-temp");
    fs::create_directories(root.path());
    LOGOS_ASSERT_EQ(::chmod(root.path().c_str(), 0700), 0);
    const fs::path target = root.path() / "outside";
    {
        std::ofstream output(target, std::ios::binary);
        output << "do-not-overwrite";
    }
    for (std::size_t index = 0U; index < 512U; ++index) {
        fs::create_symlink(
            target,
            root.path()
                / (".lez-submission-intent-v1.next."
                   + std::to_string(
                       static_cast<unsigned long>(::getpid()))
                   + "." + std::to_string(index)));
    }

    palace::PalaceLezSubmissionIntentStore store(
        root.path().string());
    LOGOS_ASSERT_TRUE(
        store.save(intent())
        == palace::PalaceLezSubmissionIntentStoreStatus::IoError);
    std::ifstream input(target, std::ios::binary);
    const std::string untouched{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    LOGOS_ASSERT_EQ(untouched, std::string("do-not-overwrite"));
    LOGOS_ASSERT_FALSE(
        fs::exists(root.path() / "lez-submission-intent-v1"));
}
#endif
