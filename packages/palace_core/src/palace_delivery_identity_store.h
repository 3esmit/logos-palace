#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "palace_delivery.h"

namespace palace {

struct DeliveryIdentityMetadataV1 {
    std::string accountId;
    std::string displayName;
    std::int64_t deliveryKeyEpoch = 0;
};

enum class DeliveryIdentityOpenStatus {
    Created,
    Loaded,
    NotFound,
    AlreadyExists,
    InvalidArgument,
    InvalidRecord,
    InsecurePermissions,
    IoError,
    CryptoError,
};

// Process-owned signing identity. Private key material is write-only outside
// this type: callers can sign canonical Delivery envelopes but cannot retrieve
// the private seed.
class PalaceDeliveryIdentity final : public DeliverySignatureSigner {
public:
    PalaceDeliveryIdentity() = default;
    ~PalaceDeliveryIdentity();
    PalaceDeliveryIdentity(const PalaceDeliveryIdentity&) = delete;
    PalaceDeliveryIdentity& operator=(const PalaceDeliveryIdentity&) = delete;
    PalaceDeliveryIdentity(PalaceDeliveryIdentity&& other) noexcept;
    PalaceDeliveryIdentity& operator=(PalaceDeliveryIdentity&& other) noexcept;

    // Creates an immutable identity record below instanceRoot. Existing
    // records are never replaced.
    static DeliveryIdentityOpenStatus create(
        const std::string& instanceRoot,
        const DeliveryIdentityMetadataV1& metadata,
        PalaceDeliveryIdentity& identity);

    // Loads only a complete, current, owner-only record. Failed loads leave
    // identity unchanged.
    static DeliveryIdentityOpenStatus load(
        const std::string& instanceRoot,
        PalaceDeliveryIdentity& identity);

    // Loads an existing record or creates one from initialMetadata when none
    // exists. Existing persisted metadata always wins.
    static DeliveryIdentityOpenStatus openOrCreate(
        const std::string& instanceRoot,
        const DeliveryIdentityMetadataV1& initialMetadata,
        PalaceDeliveryIdentity& identity);

    bool valid() const;
    const DeliveryIdentityMetadataV1& metadata() const;
    const std::string& accountId() const;
    const std::string& displayName() const;
    std::int64_t deliveryKeyEpoch() const;

    std::string publicKey() const override;
    std::string sign(const std::string& canonicalEnvelope) const override;

private:
    void clear() noexcept;

    DeliveryIdentityMetadataV1 m_metadata;
    std::array<unsigned char, 32> m_publicKey{};
    std::array<unsigned char, 32> m_privateSeed{};
    bool m_valid = false;
};

const char* deliveryIdentityOpenStatusName(DeliveryIdentityOpenStatus status);

enum class DeliveryIdentityRegistrationPhase : std::uint8_t {
    Prepared = 0,
    // Submission may have been accepted. Recovery must reconcile finalized
    // explorer history; this phase must never call the submitter again.
    Pending = 1,
    SubmittedPendingWalletSave = 2,
    Submitted = 3,
};

struct DeliveryIdentityRegistrationRecordV1 {
    std::string accountId;
    DeliveryIdentityRegistrationPhase phase =
        DeliveryIdentityRegistrationPhase::Prepared;
    std::uint64_t minimumFinalizedBlockExclusive = 0U;
    std::string transactionHash;
};

enum class DeliveryIdentityRegistrationStoreStatus {
    Saved,
    Loaded,
    NotFound,
    InvalidArgument,
    InvalidRecord,
    InsecurePermissions,
    IoError,
};

// Atomic, owner-only lifecycle record for the LEZ public-account transaction.
// The record is bound to the persisted Delivery identity account and only
// permits the monotonic missing -> prepared -> may-have-submitted -> accepted
// -> submitted transition.
class PalaceDeliveryIdentityRegistrationStore final {
public:
    explicit PalaceDeliveryIdentityRegistrationStore(
        std::string instanceRoot);

    DeliveryIdentityRegistrationStoreStatus load(
        const std::string& expectedAccountId,
        DeliveryIdentityRegistrationRecordV1& record) const;
    DeliveryIdentityRegistrationStoreStatus save(
        const DeliveryIdentityRegistrationRecordV1& record) const;

private:
    std::string m_instanceRoot;
};

struct DeliveryIdentityRegistrationSubmissionV1 {
    bool accepted = false;
    std::string transactionHash;
    std::string reason;
};

enum class DeliveryIdentityRegistrationRecoveryOutcome : std::uint8_t {
    Pending = 0U,
    Found = 1U,
    Rejected = 2U,
};

struct DeliveryIdentityRegistrationRecoveryV1 {
    DeliveryIdentityRegistrationRecoveryOutcome outcome =
        DeliveryIdentityRegistrationRecoveryOutcome::Pending;
    std::string transactionHash;
    std::string reason;
};

struct DeliveryIdentityRegistrationTransitionV1 {
    bool ready = false;
    bool attempted = false;
    bool recoveryAttempted = false;
    bool walletSaveAttempted = false;
    std::string reason;
    DeliveryIdentityRegistrationStoreStatus storeStatus =
        DeliveryIdentityRegistrationStoreStatus::NotFound;
};

// Restart-safe registration seam. Prepared and may-have-submitted records are
// durable before the module call. Restart recovery never invokes submitter.
class PalaceDeliveryIdentityRegistration final {
public:
    using Submitter = std::function<
        DeliveryIdentityRegistrationSubmissionV1()>;
    using RecoveryLookup = std::function<
        DeliveryIdentityRegistrationRecoveryV1()>;
    using WalletSaver = std::function<bool()>;

    explicit PalaceDeliveryIdentityRegistration(std::string instanceRoot);

    DeliveryIdentityRegistrationStoreStatus restore(
        const std::string& accountId);
    DeliveryIdentityRegistrationTransitionV1 ensureSubmitted(
        const std::string& accountId,
        std::uint64_t minimumFinalizedBlockExclusive,
        const RecoveryLookup& recoveryLookup,
        const Submitter& submitter,
        const WalletSaver& walletSaver);

    bool ready() const;
    bool storeHealthy() const;
    std::string phaseName() const;
    std::uint64_t minimumFinalizedBlockExclusive() const;
    const std::string& transactionHash() const;

private:
    PalaceDeliveryIdentityRegistrationStore m_store;
    std::optional<DeliveryIdentityRegistrationRecordV1> m_record;
    std::string m_accountId;
    bool m_restored = false;
    bool m_storeHealthy = true;
};

const char* deliveryIdentityRegistrationStoreStatusName(
    DeliveryIdentityRegistrationStoreStatus status);

} // namespace palace
