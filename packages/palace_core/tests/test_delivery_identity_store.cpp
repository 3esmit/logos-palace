#include <logos_test.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#endif

#include "palace_delivery_identity_store.h"
#include "palace_identity.h"

namespace {

namespace fs = std::filesystem;

constexpr const char* kRecordFileName = "delivery-identity-v1";
constexpr const char* kRegistrationRecordFileName =
    "delivery-identity-registration-v1";

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

palace::DeliveryIdentityMetadataV1 metadata()
{
    return {"GfnNCLQrVEXK5p9STR3AHPpu9sxcdqp9QnyvrENmkmuJ", "Ária", 7};
}

std::string statusName(const palace::DeliveryIdentityOpenStatus status)
{
    return palace::deliveryIdentityOpenStatusName(status);
}

std::string registrationStatusName(
    const palace::DeliveryIdentityRegistrationStoreStatus status)
{
    return palace::deliveryIdentityRegistrationStoreStatusName(
        status);
}

std::string hex64(const char character)
{
    return std::string(64U, character);
}

void flipLastByte(const fs::path& path)
{
    std::fstream record(
        path, std::ios::binary | std::ios::in | std::ios::out);
    record.seekg(-1, std::ios::end);
    char value = 0;
    record.read(&value, 1);
    value = static_cast<char>(value ^ 0x01);
    record.seekp(-1, std::ios::end);
    record.write(&value, 1);
}

void replaceVersion(const fs::path& path)
{
    std::fstream record(
        path, std::ios::binary | std::ios::in | std::ios::out);
    const char version[] = {2, 0, 0, 0};
    record.seekp(8, std::ios::beg);
    record.write(version, sizeof(version));
}

} // namespace

LOGOS_TEST(delivery_identity_create_persists_owner_only_generated_signer) {
    TemporaryDirectory root("palace-delivery-identity-create");
    palace::PalaceDeliveryIdentity identity;
    LOGOS_ASSERT_EQ(statusName(
        palace::PalaceDeliveryIdentity::create(
            root.path().string(), metadata(), identity)),
        std::string("created"));
    LOGOS_ASSERT_TRUE(identity.valid());
    LOGOS_ASSERT_EQ(identity.accountId(), metadata().accountId);
    LOGOS_ASSERT_EQ(identity.displayName(), metadata().displayName);
    LOGOS_ASSERT_EQ(
        identity.deliveryKeyEpoch(), metadata().deliveryKeyEpoch);
    LOGOS_ASSERT_EQ(identity.publicKey().size(), static_cast<std::size_t>(64));

    const std::string message = "canonical-delivery-envelope-v1";
    const std::string signature = identity.sign(message);
    palace::Ed25519EnvelopeVerifier verifier;
    LOGOS_ASSERT_EQ(signature.size(), static_cast<std::size_t>(128));
    LOGOS_ASSERT_TRUE(
        verifier.verify(identity.publicKey(), message, signature));
    LOGOS_ASSERT_FALSE(
        verifier.verify(identity.publicKey(), message + "-changed", signature));

#if defined(__unix__) || defined(__APPLE__)
    struct stat status {};
    LOGOS_ASSERT_EQ(
        ::stat((root.path() / kRecordFileName).c_str(), &status), 0);
    LOGOS_ASSERT_EQ(
        static_cast<unsigned int>(status.st_mode & 0777),
        static_cast<unsigned int>(0600));
#endif
}

LOGOS_TEST(delivery_identity_restart_loads_same_key_and_persisted_metadata) {
    TemporaryDirectory root("palace-delivery-identity-restart");
    palace::PalaceDeliveryIdentity created;
    LOGOS_ASSERT_EQ(statusName(
        palace::PalaceDeliveryIdentity::openOrCreate(
            root.path().string(), metadata(), created)),
        std::string("created"));
    const std::string originalPublicKey = created.publicKey();

    palace::PalaceDeliveryIdentity restored;
    const palace::DeliveryIdentityMetadataV1 ignored{
        "different-account", "Different", 99};
    LOGOS_ASSERT_EQ(statusName(
        palace::PalaceDeliveryIdentity::openOrCreate(
            root.path().string(), ignored, restored)),
        std::string("loaded"));
    LOGOS_ASSERT_EQ(restored.publicKey(), originalPublicKey);
    LOGOS_ASSERT_EQ(restored.accountId(), metadata().accountId);
    LOGOS_ASSERT_EQ(restored.displayName(), metadata().displayName);
    LOGOS_ASSERT_EQ(
        restored.deliveryKeyEpoch(), metadata().deliveryKeyEpoch);

    palace::Ed25519EnvelopeVerifier verifier;
    const std::string signature = restored.sign("restart-message");
    LOGOS_ASSERT_TRUE(
        verifier.verify(originalPublicKey, "restart-message", signature));
}

LOGOS_TEST(delivery_identity_generation_is_unique_per_instance) {
    TemporaryDirectory firstRoot("palace-delivery-identity-unique-first");
    TemporaryDirectory secondRoot("palace-delivery-identity-unique-second");
    palace::PalaceDeliveryIdentity first;
    palace::PalaceDeliveryIdentity second;
    LOGOS_ASSERT_EQ(statusName(
        palace::PalaceDeliveryIdentity::create(
            firstRoot.path().string(), metadata(), first)),
        std::string("created"));
    LOGOS_ASSERT_EQ(statusName(
        palace::PalaceDeliveryIdentity::create(
            secondRoot.path().string(), metadata(), second)),
        std::string("created"));
    LOGOS_ASSERT_NE(first.publicKey(), second.publicKey());
    LOGOS_ASSERT_NE(first.sign("same-message"), second.sign("same-message"));
}

LOGOS_TEST(delivery_identity_rejects_corrupt_record_without_mutating_output) {
    TemporaryDirectory validRoot("palace-delivery-identity-valid-output");
    palace::PalaceDeliveryIdentity output;
    LOGOS_ASSERT_EQ(statusName(
        palace::PalaceDeliveryIdentity::create(
            validRoot.path().string(), metadata(), output)),
        std::string("created"));
    const std::string retainedPublicKey = output.publicKey();

    TemporaryDirectory corruptRoot("palace-delivery-identity-corrupt");
    palace::PalaceDeliveryIdentity corrupt;
    LOGOS_ASSERT_EQ(statusName(
        palace::PalaceDeliveryIdentity::create(
            corruptRoot.path().string(), metadata(), corrupt)),
        std::string("created"));
    flipLastByte(corruptRoot.path() / kRecordFileName);

    LOGOS_ASSERT_EQ(statusName(
        palace::PalaceDeliveryIdentity::load(
            corruptRoot.path().string(), output)),
        std::string("invalid_record"));
    LOGOS_ASSERT_EQ(output.publicKey(), retainedPublicKey);
    LOGOS_ASSERT_TRUE(output.valid());
}

LOGOS_TEST(delivery_identity_rejects_truncated_and_version_mismatched_records) {
    TemporaryDirectory truncatedRoot(
        "palace-delivery-identity-truncated");
    palace::PalaceDeliveryIdentity truncated;
    LOGOS_ASSERT_EQ(statusName(
        palace::PalaceDeliveryIdentity::create(
            truncatedRoot.path().string(), metadata(), truncated)),
        std::string("created"));
    fs::resize_file(truncatedRoot.path() / kRecordFileName, 31U);

    palace::PalaceDeliveryIdentity output;
    LOGOS_ASSERT_EQ(statusName(
        palace::PalaceDeliveryIdentity::load(
            truncatedRoot.path().string(), output)),
        std::string("invalid_record"));
    LOGOS_ASSERT_FALSE(output.valid());
    LOGOS_ASSERT_TRUE(output.publicKey().empty());
    LOGOS_ASSERT_TRUE(output.sign("message").empty());

    TemporaryDirectory versionRoot("palace-delivery-identity-version");
    palace::PalaceDeliveryIdentity versioned;
    LOGOS_ASSERT_EQ(statusName(
        palace::PalaceDeliveryIdentity::create(
            versionRoot.path().string(), metadata(), versioned)),
        std::string("created"));
    replaceVersion(versionRoot.path() / kRecordFileName);
    LOGOS_ASSERT_EQ(statusName(
        palace::PalaceDeliveryIdentity::load(
            versionRoot.path().string(), output)),
        std::string("invalid_record"));
    LOGOS_ASSERT_FALSE(output.valid());
}

LOGOS_TEST(delivery_identity_fails_closed_on_insecure_permissions) {
#if defined(__unix__) || defined(__APPLE__)
    TemporaryDirectory root("palace-delivery-identity-permissions");
    palace::PalaceDeliveryIdentity created;
    LOGOS_ASSERT_EQ(statusName(
        palace::PalaceDeliveryIdentity::create(
            root.path().string(), metadata(), created)),
        std::string("created"));
    LOGOS_ASSERT_EQ(
        ::chmod((root.path() / kRecordFileName).c_str(), 0644), 0);

    palace::PalaceDeliveryIdentity loaded;
    LOGOS_ASSERT_EQ(statusName(
        palace::PalaceDeliveryIdentity::load(
            root.path().string(), loaded)),
        std::string("insecure_permissions"));
    LOGOS_ASSERT_FALSE(loaded.valid());
#endif
}

LOGOS_TEST(delivery_identity_refuses_invalid_metadata_and_replacement) {
    TemporaryDirectory root("palace-delivery-identity-invalid");
    palace::PalaceDeliveryIdentity identity;
    palace::DeliveryIdentityMetadataV1 invalid = metadata();
    invalid.deliveryKeyEpoch = 0;
    LOGOS_ASSERT_EQ(statusName(
        palace::PalaceDeliveryIdentity::create(
            root.path().string(), invalid, identity)),
        std::string("invalid_argument"));
    LOGOS_ASSERT_FALSE(
        fs::exists(root.path() / kRecordFileName));

    LOGOS_ASSERT_EQ(statusName(
        palace::PalaceDeliveryIdentity::create(
            root.path().string(), metadata(), identity)),
        std::string("created"));
    const std::string publicKey = identity.publicKey();
    palace::PalaceDeliveryIdentity replacement;
    LOGOS_ASSERT_EQ(statusName(
        palace::PalaceDeliveryIdentity::create(
            root.path().string(), metadata(), replacement)),
        std::string("already_exists"));
    LOGOS_ASSERT_FALSE(replacement.valid());

    palace::PalaceDeliveryIdentity loaded;
    LOGOS_ASSERT_EQ(statusName(
        palace::PalaceDeliveryIdentity::load(
            root.path().string(), loaded)),
        std::string("loaded"));
    LOGOS_ASSERT_EQ(loaded.publicKey(), publicKey);
}

LOGOS_TEST(
    delivery_identity_registration_recovers_acceptance_without_resubmit) {
    TemporaryDirectory root(
        "palace-delivery-identity-registration-retry");
    fs::create_directories(root.path());
#if defined(__unix__) || defined(__APPLE__)
    LOGOS_ASSERT_EQ(::chmod(root.path().c_str(), 0700), 0);
#endif

    const std::string accountId = hex64('a');
    const std::string transactionHash = hex64('b');
    std::size_t calls = 0U;
    palace::PalaceDeliveryIdentityRegistration first(
        root.path().string());
    const palace::DeliveryIdentityRegistrationTransitionV1
        failed = first.ensureSubmitted(
            accountId,
            10U,
            []() {
                return palace::
                    DeliveryIdentityRegistrationRecoveryV1{};
            },
            [&calls]() {
                ++calls;
                return palace::
                    DeliveryIdentityRegistrationSubmissionV1{
                        false,
                        {},
                        "module-call-failed",
                    };
            },
            []() { return true; });
    LOGOS_ASSERT_FALSE(failed.ready);
    LOGOS_ASSERT_TRUE(failed.attempted);
    LOGOS_ASSERT_FALSE(failed.walletSaveAttempted);
    LOGOS_ASSERT_EQ(failed.reason, std::string("module-call-failed"));
    LOGOS_ASSERT_EQ(first.phaseName(), std::string("pending"));
    LOGOS_ASSERT_TRUE(first.transactionHash().empty());
    LOGOS_ASSERT_EQ(calls, static_cast<std::size_t>(1));

#if defined(__unix__) || defined(__APPLE__)
    struct stat status {};
    LOGOS_ASSERT_EQ(
        ::stat(
            (root.path() / kRegistrationRecordFileName).c_str(),
            &status),
        0);
    LOGOS_ASSERT_EQ(
        static_cast<unsigned int>(status.st_mode & 0777),
        static_cast<unsigned int>(0600));
#endif

    palace::PalaceDeliveryIdentityRegistration restarted(
        root.path().string());
    LOGOS_ASSERT_EQ(
        registrationStatusName(restarted.restore(accountId)),
        std::string("loaded"));
    LOGOS_ASSERT_FALSE(restarted.ready());
    LOGOS_ASSERT_EQ(
        restarted.phaseName(), std::string("pending"));

    std::size_t walletSaveCalls = 0U;
    const palace::DeliveryIdentityRegistrationTransitionV1
        retried = restarted.ensureSubmitted(
            accountId,
            10U,
            [&transactionHash]() {
                return palace::
                    DeliveryIdentityRegistrationRecoveryV1{
                        palace::
                            DeliveryIdentityRegistrationRecoveryOutcome::
                                Found,
                        transactionHash,
                        "unique-finalized-submission-found",
                    };
            },
            [&calls]() {
                ++calls;
                return palace::
                    DeliveryIdentityRegistrationSubmissionV1{
                        false,
                        {},
                        "must-not-resubmit",
                    };
            },
            [&walletSaveCalls]() {
                ++walletSaveCalls;
                return false;
            });
    LOGOS_ASSERT_FALSE(retried.ready);
    LOGOS_ASSERT_FALSE(retried.attempted);
    LOGOS_ASSERT_TRUE(retried.recoveryAttempted);
    LOGOS_ASSERT_TRUE(retried.walletSaveAttempted);
    LOGOS_ASSERT_FALSE(restarted.ready());
    LOGOS_ASSERT_EQ(
        restarted.phaseName(),
        std::string("submitted_pending_wallet_save"));
    LOGOS_ASSERT_EQ(
        restarted.transactionHash(), transactionHash);
    LOGOS_ASSERT_EQ(calls, static_cast<std::size_t>(1));
    LOGOS_ASSERT_EQ(
        walletSaveCalls, static_cast<std::size_t>(1));

    palace::PalaceDeliveryIdentityRegistration walletRestarted(
        root.path().string());
    LOGOS_ASSERT_EQ(
        registrationStatusName(walletRestarted.restore(accountId)),
        std::string("loaded"));
    LOGOS_ASSERT_EQ(
        walletRestarted.phaseName(),
        std::string("submitted_pending_wallet_save"));
    std::size_t unexpectedRegistrationCalls = 0U;
    const palace::DeliveryIdentityRegistrationTransitionV1
        finalized = walletRestarted.ensureSubmitted(
            accountId,
            10U,
            [&unexpectedRegistrationCalls]() {
                ++unexpectedRegistrationCalls;
                return palace::
                    DeliveryIdentityRegistrationRecoveryV1{};
            },
            [&unexpectedRegistrationCalls]() {
                ++unexpectedRegistrationCalls;
                return palace::
                    DeliveryIdentityRegistrationSubmissionV1{
                        false,
                        {},
                        "must-not-run",
                    };
            },
            [&walletSaveCalls]() {
                ++walletSaveCalls;
                return true;
            });
    LOGOS_ASSERT_TRUE(finalized.ready);
    LOGOS_ASSERT_FALSE(finalized.attempted);
    LOGOS_ASSERT_TRUE(finalized.walletSaveAttempted);
    LOGOS_ASSERT_TRUE(walletRestarted.ready());
    LOGOS_ASSERT_EQ(
        unexpectedRegistrationCalls,
        static_cast<std::size_t>(0));
    LOGOS_ASSERT_EQ(
        walletSaveCalls, static_cast<std::size_t>(2));
}

LOGOS_TEST(
    delivery_identity_registration_restores_submitted_evidence) {
    TemporaryDirectory root(
        "palace-delivery-identity-registration-submitted");
    fs::create_directories(root.path());
#if defined(__unix__) || defined(__APPLE__)
    LOGOS_ASSERT_EQ(::chmod(root.path().c_str(), 0700), 0);
#endif

    const std::string accountId = hex64('c');
    const std::string transactionHash = hex64('d');
    palace::PalaceDeliveryIdentityRegistration initial(
        root.path().string());
    const palace::DeliveryIdentityRegistrationTransitionV1
        submitted = initial.ensureSubmitted(
            accountId,
            20U,
            []() {
                return palace::
                    DeliveryIdentityRegistrationRecoveryV1{};
            },
            [&transactionHash]() {
                return palace::
                    DeliveryIdentityRegistrationSubmissionV1{
                        true,
                        transactionHash,
                        "accepted",
                    };
            },
            []() { return true; });
    LOGOS_ASSERT_TRUE(submitted.ready);

    palace::PalaceDeliveryIdentityRegistration restarted(
        root.path().string());
    LOGOS_ASSERT_EQ(
        registrationStatusName(restarted.restore(accountId)),
        std::string("loaded"));
    LOGOS_ASSERT_TRUE(restarted.ready());
    LOGOS_ASSERT_EQ(
        restarted.phaseName(), std::string("submitted"));
    LOGOS_ASSERT_EQ(
        restarted.transactionHash(), transactionHash);

    std::size_t unexpectedCalls = 0U;
    const palace::DeliveryIdentityRegistrationTransitionV1
        reused = restarted.ensureSubmitted(
            accountId,
            20U,
            [&unexpectedCalls]() {
                ++unexpectedCalls;
                return palace::
                    DeliveryIdentityRegistrationRecoveryV1{};
            },
            [&unexpectedCalls]() {
                ++unexpectedCalls;
                return palace::
                    DeliveryIdentityRegistrationSubmissionV1{
                        false,
                        {},
                        "must-not-run",
                    };
            },
            [&unexpectedCalls]() {
                ++unexpectedCalls;
                return false;
            });
    LOGOS_ASSERT_TRUE(reused.ready);
    LOGOS_ASSERT_FALSE(reused.attempted);
    LOGOS_ASSERT_EQ(
        unexpectedCalls, static_cast<std::size_t>(0));
}

LOGOS_TEST(
    delivery_identity_registration_accept_to_store_crash_never_resubmits) {
    TemporaryDirectory root(
        "palace-delivery-identity-registration-accept-crash");
    fs::create_directories(root.path());
#if defined(__unix__) || defined(__APPLE__)
    LOGOS_ASSERT_EQ(::chmod(root.path().c_str(), 0700), 0);
#endif

    const std::string accountId = hex64('e');
    const std::string transactionHash = hex64('f');
    palace::PalaceDeliveryIdentityRegistrationStore store(
        root.path().string());
    palace::DeliveryIdentityRegistrationRecordV1 prepared;
    prepared.accountId = accountId;
    prepared.phase =
        palace::DeliveryIdentityRegistrationPhase::Prepared;
    prepared.minimumFinalizedBlockExclusive = 41U;
    LOGOS_ASSERT_TRUE(
        store.save(prepared)
        == palace::DeliveryIdentityRegistrationStoreStatus::Saved);
    palace::DeliveryIdentityRegistrationRecordV1 sendBoundary =
        prepared;
    sendBoundary.phase =
        palace::DeliveryIdentityRegistrationPhase::Pending;
    LOGOS_ASSERT_TRUE(
        store.save(sendBoundary)
        == palace::DeliveryIdentityRegistrationStoreStatus::Saved);

    palace::PalaceDeliveryIdentityRegistration restarted(
        root.path().string());
    LOGOS_ASSERT_TRUE(
        restarted.restore(accountId)
        == palace::DeliveryIdentityRegistrationStoreStatus::Loaded);
    std::size_t submitCalls = 0U;
    const auto recovered = restarted.ensureSubmitted(
        accountId,
        999U,
        [&transactionHash]() {
            return palace::DeliveryIdentityRegistrationRecoveryV1{
                palace::DeliveryIdentityRegistrationRecoveryOutcome::
                    Found,
                transactionHash,
                "unique-finalized-submission-found",
            };
        },
        [&submitCalls]() {
            ++submitCalls;
            return palace::DeliveryIdentityRegistrationSubmissionV1{
                false,
                {},
                "must-not-resubmit",
            };
        },
        []() { return true; });
    LOGOS_ASSERT_TRUE(recovered.ready);
    LOGOS_ASSERT_TRUE(recovered.recoveryAttempted);
    LOGOS_ASSERT_FALSE(recovered.attempted);
    LOGOS_ASSERT_EQ(submitCalls, static_cast<std::size_t>(0));
    LOGOS_ASSERT_EQ(
        restarted.minimumFinalizedBlockExclusive(),
        static_cast<std::uint64_t>(41U));
    LOGOS_ASSERT_EQ(
        restarted.transactionHash(), transactionHash);
}
