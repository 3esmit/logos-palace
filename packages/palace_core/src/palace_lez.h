#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace palace {

class PalaceLezExplorerFinalityCertificateV1;

using PalaceLezBytes32 = std::array<std::uint8_t, 32>;

// Transitional schema-v2 facade retained only until Palace Core moves its
// command parser to PalaceLezInstructionV3. New submission/replay code must
// use the v3 types below.
enum class PalaceLezInstructionKind : std::uint32_t {
    BindDeliveryKey = 0,
    DelegateModerator = 1,
    RevokeModerator = 2,
    BanUser = 3,
    BanAsset = 4,
    SetRoomLocked = 5,
    PublishManifest = 6,
    SetSharedSpotRevision = 7,
};

struct PalaceLezInstructionV1 {
    PalaceLezInstructionKind kind = PalaceLezInstructionKind::PublishManifest;
    std::string subjectAccountIdHex;
    std::string deliveryKeyHex;
    std::uint64_t keyEpoch = 0;
    std::string roomId;
    std::string cid;
    bool locked = false;
    std::string spotId;
    std::uint64_t revision = 0;
};

struct PalaceLezSubmitRequestV1 {
    std::uint64_t orderedActionId = 0;
    std::string stateAccountIdHex;
    std::string callerAccountIdHex;
    std::string programIdHex;
    PalaceLezInstructionV1 instruction;
};

enum class PalaceLezInstructionKindV3 : std::uint32_t {
    Initialize = 0,
    RegisterUser = 1,
    UpdateUserProfile = 2,
    RotateDeliveryKey = 3,
    PublishManifest = 4,
    UpdateRoom = 5,
    GrantCapability = 6,
    RevokeCapability = 7,
    SetRoomLocked = 8,
    CreateUserBan = 9,
    CreateAssetBan = 10,
    SetBanActive = 11,
    CreateSharedState = 12,
    UpdateSharedState = 13,
};

enum class PalaceLezVmProfileV3 : std::uint32_t {
    NoScript = 0,
    IptScraeMvpV1 = 1,
};

enum class PalaceLezScopeKindV3 : std::uint32_t {
    Palace = 0,
    Room = 1,
};

struct PalaceLezScopeV3 {
    PalaceLezScopeKindV3 kind = PalaceLezScopeKindV3::Palace;
    PalaceLezBytes32 roomId{};
};

struct PalaceLezUserProfileInputV3 {
    std::string displayName;
    PalaceLezBytes32 deliveryKey{};
    std::uint64_t keyEpoch = 0;
    std::optional<std::string> avatarManifestCid;
};

struct PalaceLezRoomConfigInputV3 {
    std::string title;
    std::string manifestCid;
    std::string scriptBundleCid;
    PalaceLezVmProfileV3 vmProfile = PalaceLezVmProfileV3::IptScraeMvpV1;
};

struct PalaceLezInitializeV3 {
    PalaceLezBytes32 palaceId{};
    std::string title;
    std::string activeManifestCid;
    PalaceLezUserProfileInputV3 ownerProfile;
    PalaceLezBytes32 ownerGrantId{};
    PalaceLezBytes32 entryRoomId{};
    PalaceLezRoomConfigInputV3 entryRoom;
    PalaceLezBytes32 secondaryRoomId{};
    PalaceLezRoomConfigInputV3 secondaryRoom;
};

struct PalaceLezRegisterUserV3 {
    std::uint64_t orderedActionId = 0;
    PalaceLezUserProfileInputV3 profile;
};

struct PalaceLezUpdateUserProfileV3 {
    std::uint64_t orderedActionId = 0;
    std::string displayName;
    std::optional<std::string> avatarManifestCid;
};

struct PalaceLezRotateDeliveryKeyV3 {
    std::uint64_t orderedActionId = 0;
    PalaceLezBytes32 deliveryKey{};
    std::uint64_t keyEpoch = 0;
};

struct PalaceLezPublishManifestV3 {
    std::uint64_t orderedActionId = 0;
    std::string cid;
};

struct PalaceLezUpdateRoomV3 {
    std::uint64_t orderedActionId = 0;
    PalaceLezBytes32 grantId{};
    PalaceLezBytes32 roomId{};
    PalaceLezRoomConfigInputV3 config;
};

struct PalaceLezGrantCapabilityV3 {
    std::uint64_t orderedActionId = 0;
    PalaceLezBytes32 grantId{};
    PalaceLezBytes32 subjectUserId{};
    PalaceLezScopeV3 scope;
    std::uint32_t capabilities = 0;
    bool delegable = false;
    std::uint64_t validThroughActionId = 0;
};

struct PalaceLezRevokeCapabilityV3 {
    std::uint64_t orderedActionId = 0;
    PalaceLezBytes32 grantId{};
};

struct PalaceLezSetRoomLockedV3 {
    std::uint64_t orderedActionId = 0;
    PalaceLezBytes32 grantId{};
    PalaceLezBytes32 roomId{};
    bool locked = false;
};

struct PalaceLezCreateUserBanV3 {
    std::uint64_t orderedActionId = 0;
    PalaceLezBytes32 grantId{};
    PalaceLezBytes32 banId{};
    PalaceLezBytes32 subjectUserId{};
    PalaceLezScopeV3 scope;
};

struct PalaceLezCreateAssetBanV3 {
    std::uint64_t orderedActionId = 0;
    PalaceLezBytes32 grantId{};
    PalaceLezBytes32 banId{};
    std::string cid;
    PalaceLezScopeV3 scope;
};

struct PalaceLezSetBanActiveV3 {
    std::uint64_t orderedActionId = 0;
    PalaceLezBytes32 grantId{};
    PalaceLezBytes32 banId{};
    bool active = false;
};

struct PalaceLezCreateSharedStateV3 {
    std::uint64_t orderedActionId = 0;
    PalaceLezBytes32 grantId{};
    PalaceLezBytes32 sharedStateId{};
    PalaceLezBytes32 roomId{};
    std::string key;
    std::vector<std::uint8_t> value;
    PalaceLezBytes32 stateRoot{};
};

struct PalaceLezUpdateSharedStateV3 {
    std::uint64_t orderedActionId = 0;
    PalaceLezBytes32 grantId{};
    PalaceLezBytes32 sharedStateId{};
    PalaceLezBytes32 roomId{};
    std::uint64_t stateRevision = 0;
    std::vector<std::uint8_t> value;
    PalaceLezBytes32 stateRoot{};
};

using PalaceLezInstructionPayloadV3 = std::variant<
    PalaceLezInitializeV3,
    PalaceLezRegisterUserV3,
    PalaceLezUpdateUserProfileV3,
    PalaceLezRotateDeliveryKeyV3,
    PalaceLezPublishManifestV3,
    PalaceLezUpdateRoomV3,
    PalaceLezGrantCapabilityV3,
    PalaceLezRevokeCapabilityV3,
    PalaceLezSetRoomLockedV3,
    PalaceLezCreateUserBanV3,
    PalaceLezCreateAssetBanV3,
    PalaceLezSetBanActiveV3,
    PalaceLezCreateSharedStateV3,
    PalaceLezUpdateSharedStateV3>;

struct PalaceLezInstructionV3 {
    PalaceLezInstructionPayloadV3 payload = PalaceLezInitializeV3{};
};

struct PalaceLezWireInstruction {
    bool accepted = false;
    std::string reason;
    std::vector<std::uint32_t> words;
    PalaceLezInstructionV3 instruction;
};

struct PalaceLezTransactionPlanV3 {
    bool accepted = false;
    std::string reason;
    std::string programIdHex;
    std::string rootAccountIdHex;
    std::vector<std::string> accountIdsHex;
    std::vector<bool> signingRequirements;
    std::vector<std::uint32_t> instructionWords;
    PalaceLezInstructionV3 instruction;
};

struct PalaceLezSubmissionResult {
    bool accepted = false;
    std::string reason;
    std::string transactionHash;
};

enum class PalaceLezRecordTypeV3 : std::uint8_t {
    PalaceRoot = 0,
    UserProfile = 1,
    Room = 2,
    CapabilityGrant = 3,
    Ban = 4,
    RoomSharedState = 5,
};

struct PalaceLezRootRecordV3 {
    PalaceLezBytes32 palaceId{};
    std::string title;
    PalaceLezBytes32 owner{};
    PalaceLezBytes32 entryRoomId{};
    std::array<PalaceLezBytes32, 2> roomIds{};
    std::string activeManifestCid;
    std::uint32_t userCount = 0;
    std::uint32_t grantCount = 0;
    std::uint32_t banCount = 0;
    std::uint32_t sharedStateCount = 0;
    std::uint64_t revision = 0;
    std::uint64_t lastOrderedActionId = 0;
};

struct PalaceLezExpectedRootV3 {
    bool accepted = false;
    std::string reason;
    PalaceLezRootRecordV3 record;
    std::vector<std::uint8_t> encodedRecord;
    std::string dataSha256Hex;
};

struct PalaceLezUserProfileRecordV3 {
    PalaceLezBytes32 palaceId{};
    PalaceLezBytes32 userId{};
    std::string displayName;
    PalaceLezBytes32 deliveryKey{};
    std::uint64_t keyEpoch = 0;
    std::optional<std::string> avatarManifestCid;
    std::uint64_t profileRevision = 0;
};

struct PalaceLezRoomRecordV3 {
    PalaceLezBytes32 palaceId{};
    PalaceLezBytes32 roomId{};
    std::string title;
    std::string manifestCid;
    std::string scriptBundleCid;
    PalaceLezVmProfileV3 vmProfile = PalaceLezVmProfileV3::IptScraeMvpV1;
    bool locked = false;
    std::uint64_t revision = 0;
};

struct PalaceLezCapabilityGrantRecordV3 {
    PalaceLezBytes32 palaceId{};
    PalaceLezBytes32 grantId{};
    PalaceLezBytes32 subjectUserId{};
    PalaceLezBytes32 issuedBy{};
    PalaceLezScopeV3 scope;
    std::uint32_t capabilities = 0;
    bool delegable = false;
    std::uint64_t validThroughActionId = 0;
    bool revoked = false;
    std::uint64_t revision = 0;
};

enum class PalaceLezBanTargetKindV3 : std::uint8_t {
    User = 0,
    AssetCid = 1,
};

struct PalaceLezBanRecordV3 {
    PalaceLezBytes32 palaceId{};
    PalaceLezBytes32 banId{};
    PalaceLezBanTargetKindV3 targetKind = PalaceLezBanTargetKindV3::User;
    PalaceLezBytes32 targetUserId{};
    std::string targetAssetCid;
    PalaceLezBytes32 issuer{};
    PalaceLezScopeV3 scope;
    bool active = false;
    std::uint64_t revision = 0;
};

struct PalaceLezRoomSharedStateRecordV3 {
    PalaceLezBytes32 palaceId{};
    PalaceLezBytes32 sharedStateId{};
    PalaceLezBytes32 roomId{};
    std::string key;
    std::vector<std::uint8_t> value;
    PalaceLezBytes32 stateRoot{};
    std::uint64_t revision = 0;
    std::uint64_t lastOrderedActionId = 0;
};

using PalaceLezRecordV3 = std::variant<
    PalaceLezRootRecordV3,
    PalaceLezUserProfileRecordV3,
    PalaceLezRoomRecordV3,
    PalaceLezCapabilityGrantRecordV3,
    PalaceLezBanRecordV3,
    PalaceLezRoomSharedStateRecordV3>;

struct PalaceLezRawAccountV1 {
    bool accepted = false;
    std::string reason;
    std::string programOwnerHex;
    std::string balanceLeHex;
    std::string nonceLeHex;
    std::vector<std::uint8_t> data;
    std::string dataSha256Hex;
};

struct PalaceLezPublicAccountV3 {
    bool accepted = false;
    std::string reason;
    std::string programOwnerHex;
    std::string balanceLeHex;
    std::string nonceLeHex;
    std::string dataSha256Hex;
    PalaceLezRecordTypeV3 recordType = PalaceLezRecordTypeV3::PalaceRoot;
    PalaceLezRecordV3 record = PalaceLezRootRecordV3{};
};

struct PalaceLezIndexerAccountRef {
    std::string accountIdHex;
    std::string nonce;
};

struct PalaceLezIndexerTransaction {
    std::string hash;
    std::string programIdHex;
    std::vector<PalaceLezIndexerAccountRef> accounts;
    std::vector<std::uint32_t> instructionWords;
    std::uint32_t signatureCount = 0;
};

struct PalaceLezIndexerParseResult {
    bool accepted = false;
    std::string reason;
    std::vector<PalaceLezIndexerTransaction> transactions;
};

struct PalaceLezFinalizedActionV3 {
    std::string transactionHash;
    std::uint64_t orderedActionId = 0;
    PalaceLezInstructionV3 instruction;
    std::vector<std::string> accountIdsHex;
};

struct PalaceLezRebuildResultV3 {
    bool accepted = false;
    std::string reason;
    std::vector<PalaceLezFinalizedActionV3> actions;
};

class PalaceLezCodec {
public:
    static bool parseOrderedActionId(const std::string& value, std::uint64_t& output);
    static PalaceLezWireInstruction encodeApply(const PalaceLezSubmitRequestV1& request);
    static bool parseBytes32Hex(const std::string& value, PalaceLezBytes32& output);
    static std::string bytes32Hex(const PalaceLezBytes32& value);
    static std::string accountIdBase58(const PalaceLezBytes32& value);
    static bool parseAccountIdBase58(const std::string& value, PalaceLezBytes32& output);

    static std::string deriveRootPda(const std::string& programIdHex);
    static std::string deriveRecordPda(
        const std::string& programIdHex,
        const std::string& recordTag,
        const std::string& rootAccountIdHex,
        const PalaceLezBytes32& stableId);

    static PalaceLezWireInstruction encodeInstruction(
        const PalaceLezInstructionV3& instruction);
    static PalaceLezWireInstruction decodeInstruction(
        const std::vector<std::uint32_t>& words);
    static PalaceLezTransactionPlanV3 buildTransaction(
        const std::string& programIdHex,
        const std::string& signerAccountIdHex,
        const PalaceLezInstructionV3& instruction);
    // Exact Borsh bytes owned by the Rust schema-v3 contract. These helpers
    // predict only PalaceRoot fields; child-account authorization and mutation
    // remain enforced by the deployed guest.
    static PalaceLezExpectedRootV3 expectedInitialRoot(
        const std::string& ownerAccountIdHex,
        const PalaceLezInstructionV3& instruction);
    static PalaceLezExpectedRootV3 expectedAdvancedRoot(
        const PalaceLezRootRecordV3& current,
        const PalaceLezInstructionV3& instruction);
    static std::vector<std::uint8_t> encodeRootRecord(
        const PalaceLezRootRecordV3& record);
    static PalaceLezSubmissionResult parseSubmissionResult(const std::string& responseJson);
    static PalaceLezRawAccountV1 parsePublicAccountSnapshot(
        const std::string& responseJson);
    static PalaceLezPublicAccountV3 decodePublicAccount(
        const std::string& responseJson,
        const std::string& expectedProgramIdHex);
    static PalaceLezIndexerParseResult parseFinalizedTransactions(
        const std::string& responseJson);
    static PalaceLezRebuildResultV3 rebuildFinalizedHistory(
        const std::string& programIdHex,
        const std::string& rootAccountIdHex,
        const std::string& responseJson);
    static std::string sha256Hex(const std::vector<std::uint8_t>& bytes);
};

enum class PalaceLezTransactionStage : std::uint8_t {
    Submitted = 0,
    Observed = 1,
    Finalized = 2,
};

struct PalaceLezTrackedTransaction {
    PalaceLezTransactionStage stage = PalaceLezTransactionStage::Submitted;
    std::string transactionHash;
    std::uint64_t orderedActionId = 0;
    std::uint64_t observedBlockHeight = 0;
    std::string expectedRootDataSha256Hex;
    PalaceLezTransactionPlanV3 plan;
};

struct PalaceLezCoordinatorUpdate {
    bool accepted = false;
    bool changed = false;
    std::string reason;
    PalaceLezTransactionStage stage = PalaceLezTransactionStage::Submitted;
};

struct PalaceLezCoordinatorSnapshot {
    bool accepted = false;
    std::string reason;
    std::vector<std::uint8_t> bytes;
};

struct PalaceLezNetworkFingerprint {
    std::string networkId;
    std::string moduleApiVersion;
    std::string moduleRevision;
    std::string runtimeRevision;
    std::string publicContractVersion;
    std::string publicContractRevision;
    std::string programIdHex;
    std::string programBytecodeSha256Hex;
};

enum class PalaceLezCompatibilityState : std::uint8_t {
    Unknown = 0,
    Compatible = 1,
    Incompatible = 2,
};

struct PalaceLezCompatibilityResult {
    bool accepted = false;
    std::string reason;
    PalaceLezCompatibilityState state = PalaceLezCompatibilityState::Unknown;
};

// Pure reconciliation state machine. Module calls stay in Palace Core: callers
// bracket get_account_public with heights, then pass the immutable evidence
// here. Interrupt retains durable state but rejects progression until activate.
class PalaceLezTransactionCoordinator {
public:
    PalaceLezCompatibilityResult configureNetworkFingerprint(
        const PalaceLezNetworkFingerprint& required,
        const PalaceLezNetworkFingerprint& observed);
    PalaceLezCompatibilityState compatibilityState() const;
    bool activate();
    void interrupt();
    bool running() const;
    bool reset();

    PalaceLezCoordinatorUpdate registerSubmission(
        const PalaceLezTransactionPlanV3& plan,
        const std::string& submissionResponseJson,
        const std::string& expectedRootDataSha256Hex);
    // Registers explorer-proven acceptance after a write-ahead recovery.
    // An exact existing transaction is idempotent; any drift fails closed.
    PalaceLezCoordinatorUpdate registerRecoveredSubmission(
        const PalaceLezTransactionPlanV3& plan,
        const std::string& transactionHash,
        const std::string& expectedRootDataSha256Hex);
    PalaceLezCoordinatorUpdate observeStableRoot(
        const std::string& transactionHash,
        std::int64_t heightBefore,
        const std::string& rootAccountJson,
        std::int64_t heightAfter);
    PalaceLezCoordinatorUpdate reconcileFinality(
        const std::string& transactionHash,
        const std::string& indexerTransactionsJson);
    PalaceLezCoordinatorUpdate reconcileExplorerFinality(
        const PalaceLezExplorerFinalityCertificateV1& certificate);

    PalaceLezCoordinatorSnapshot snapshot() const;
    PalaceLezCoordinatorUpdate restore(const std::vector<std::uint8_t>& bytes);
    std::vector<PalaceLezTrackedTransaction> transactions() const;

private:
    mutable std::mutex mutex_;
    bool running_ = false;
    PalaceLezCompatibilityState compatibilityState_ =
        PalaceLezCompatibilityState::Unknown;
    std::vector<PalaceLezTrackedTransaction> transactions_;
};

} // namespace palace
