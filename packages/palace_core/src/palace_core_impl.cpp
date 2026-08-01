#include "palace_core_impl.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include <openssl/crypto.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "logos_sdk.h"
#include "logos_json.h"

#include "palace_application_round_trip.h"
#include "palace_delivery.h"
#include "palace_human_moderation.h"
#include "palace_lez.h"
#include "palace_lez_intent.h"
#include "palace_lez_release_lock.h"
#include "palace_sha256.h"
#include "palace_storage_cid.h"
#include "palace_vm_finalized_replay.h"

namespace {

std::string result(bool changed, const palace::ActionStatus& status)
{
    return std::string("changed=") + (changed ? "1" : "0")
        + ";" + palace::canonicalActionStatus(status);
}

bool isUnder(const QString& candidate, const QString& root)
{
    return !candidate.isEmpty() && !root.isEmpty()
        && (candidate == root || candidate.startsWith(root + QLatin1Char('/')));
}

bool isLowerHexAccountId(const std::string& value)
{
    return value.size() == 64U
        && std::all_of(
            value.begin(), value.end(), [](unsigned char character) {
                return (character >= '0' && character <= '9')
                    || (character >= 'a' && character <= 'f');
            });
}

bool isNonzeroLowerHexAccountId(const std::string& value)
{
    return isLowerHexAccountId(value)
        && value != std::string(64U, '0');
}

bool parseCanonicalU64(
    const std::string& value,
    std::uint64_t& parsed)
{
    if (value.empty() || value.size() > 20U
        || (value.size() > 1U && value.front() == '0')) {
        return false;
    }
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    return result.ec == std::errc()
        && result.ptr == value.data() + value.size()
        && value == std::to_string(parsed);
}

bool exactJsonKeys(
    const QJsonObject& object,
    const std::initializer_list<const char*> expected)
{
    std::set<QString> keys;
    for (const char* key : expected)
        keys.emplace(QString::fromLatin1(key));
    const QStringList actual = object.keys();
    return actual.size()
            == static_cast<qsizetype>(keys.size())
        && std::all_of(
            actual.begin(), actual.end(),
            [&keys](const QString& key) {
                return keys.find(key) != keys.end();
            });
}

std::string hexBytes(const std::string& bytes)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string encoded(bytes.size() * 2U, '0');
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        const auto byte = static_cast<unsigned char>(bytes[index]);
        encoded[index * 2U] = kDigits[byte >> 4U];
        encoded[index * 2U + 1U] =
            kDigits[byte & 0x0fU];
    }
    return encoded;
}

std::string lengthEncoded(const std::string& value)
{
    return std::to_string(value.size()) + ":" + value;
}

std::string expectedDoorVmReceipt(
    const std::string& scriptBundleCid,
    const std::string& roomEpoch,
    const std::string& trigger,
    const std::string& resultingState,
    const std::string& stateRoot,
    const bool finalized)
{
    const std::string shared = "set:door_open=1";
    const std::string navigation = "navigate:lounge";
    const std::string effects =
        std::string("accepted=1;local=")
        + (finalized ? lengthEncoded(navigation)
                     : std::string{})
        + ";shared=" + lengthEncoded(shared)
        + ";deferred="
        + (finalized
               ? std::string{}
               : lengthEncoded(
                     "await-finality:" + navigation))
        + ";rejected=;state=" + resultingState;
    const std::string receipt = palace::crypto::sha256Hex(
        "profile=" + lengthEncoded("classic-mvp-v1")
        + ";bundle=" + lengthEncoded(scriptBundleCid)
        + ";epoch=" + lengthEncoded(roomEpoch)
        + ";trigger=" + lengthEncoded(trigger)
        + ";phase="
        + (finalized ? "finalized" : "provisional")
        + ";state_root=" + stateRoot
        + ";effects=" + effects);
    return effects + ";state_root=" + stateRoot
        + ";receipt=" + receipt;
}

bool validPalaceVmPhase(const std::string& phase)
{
    return phase == "prepared"
        || phase == "provisional"
        || phase == "submitted"
        || phase == "finalized"
        || phase == "promoted"
        || phase == "degraded";
}

bool safeStatusReason(const std::string& reason)
{
    return !reason.empty() && reason.size() <= 128U
        && std::all_of(
            reason.begin(), reason.end(),
            [](const unsigned char character) {
                return (character >= 'a' && character <= 'z')
                    || (character >= '0' && character <= '9')
                    || character == '-';
            });
}

bool isColdVmReplayReason(const std::string& reason)
{
    return reason.rfind("cold-replay-", 0U) == 0U;
}

struct NamedLezRecord {
    std::string accountIdHex;
    palace::PalaceLezPublicAccountV3 account;
};

struct ActiveGate3Content {
    bool accepted = false;
    std::string reason;
    palace::PalaceLezRootRecordV3 root;
    palace::PalaceLezRoomRecordV3 atrium;
    palace::PalaceLezRoomRecordV3 lounge;
    std::vector<NamedLezRecord> records;
    std::string rootDataSha256Hex;
    std::string script;
    std::string scriptCid;
    std::string propCid;
};

ActiveGate3Content gate3AuthorityLinkedContent(
    const palace::PalaceLezFinalizedAuthorityBundleV1&
        authority,
    const palace::PalaceStorageMvpBundle& storage)
{
    ActiveGate3Content result;
    if (!storage.complete()) {
        result.reason = "gate3-bundle-incomplete";
        return result;
    }

    const auto* palaceManifest = storage.artifact("palace-1");
    const auto* atriumManifest =
        storage.artifact("room-atrium");
    const auto* loungeManifest =
        storage.artifact("room-lounge");
    const auto* script = storage.artifact("script-door");
    const auto* prop = storage.artifact(
        storage.propManifestObjectId());
    if (palaceManifest == nullptr
        || atriumManifest == nullptr
        || loungeManifest == nullptr
        || script == nullptr
        || !palace::isSafePalaceCid(palaceManifest->cid)
        || !palace::isSafePalaceCid(atriumManifest->cid)
        || !palace::isSafePalaceCid(loungeManifest->cid)
        || !palace::isSafePalaceCid(script->cid)
        || (prop != nullptr
            && !palace::isSafePalaceCid(prop->cid))) {
        result.reason = "gate3-content-mismatch";
        return result;
    }

    result.records.reserve(authority.accounts.size());
    for (const auto& stored : authority.accounts) {
        NamedLezRecord named;
        named.accountIdHex = stored.accountIdHex;
        named.account =
            palace::PalaceLezCodec::decodePublicAccount(
                stored.responseJson,
                authority.scope.programIdHex);
        if (!named.account.accepted) {
            result.reason = "gate3-authority-account-invalid";
            return result;
        }
        result.records.push_back(std::move(named));
    }

    const auto rootRecord = std::find_if(
        result.records.begin(), result.records.end(),
        [&authority](const NamedLezRecord& named) {
            return named.accountIdHex
                == authority.scope.rootAccountIdHex;
        });
    const auto* root =
        rootRecord == result.records.end()
        ? nullptr
        : std::get_if<palace::PalaceLezRootRecordV3>(
              &rootRecord->account.record);
    if (root == nullptr
        || root->roomIds.size() != 2U
        || root->roomIds[0] == root->roomIds[1]
        || root->lastOrderedActionId
            != authority.checkpoint.lastOrderedActionId
        || root->entryRoomId != root->roomIds[0]
        || root->activeManifestCid != palaceManifest->cid) {
        result.reason = "gate3-root-link-mismatch";
        return result;
    }

    const auto roomRecord =
        [&result](const palace::PalaceLezBytes32& roomId)
            -> const palace::PalaceLezRoomRecordV3* {
        const auto found = std::find_if(
            result.records.begin(), result.records.end(),
            [&roomId](const NamedLezRecord& named) {
                const auto* room =
                    std::get_if<palace::PalaceLezRoomRecordV3>(
                        &named.account.record);
                return room != nullptr
                    && room->roomId == roomId;
            });
        return found == result.records.end()
            ? nullptr
            : std::get_if<palace::PalaceLezRoomRecordV3>(
                  &found->account.record);
    };
    const auto* atrium = roomRecord(root->roomIds[0]);
    const auto* lounge = roomRecord(root->roomIds[1]);
    if (atrium == nullptr || lounge == nullptr
        || atrium->manifestCid != atriumManifest->cid
        || lounge->manifestCid != loungeManifest->cid
        || atrium->scriptBundleCid != script->cid
        || lounge->scriptBundleCid != script->cid
        || atrium->vmProfile
            != palace::PalaceLezVmProfileV3::IptScraeMvpV1
        || lounge->vmProfile
            != palace::PalaceLezVmProfileV3::IptScraeMvpV1) {
        result.reason = "gate3-room-link-mismatch";
        return result;
    }

    result.accepted = true;
    result.reason = "accepted";
    result.root = *root;
    result.rootDataSha256Hex =
        rootRecord->account.dataSha256Hex;
    result.atrium = *atrium;
    result.lounge = *lounge;
    result.script = script->bytes;
    result.scriptCid = script->cid;
    result.propCid = prop == nullptr
        ? std::string{} : prop->cid;
    return result;
}

ActiveGate3Content activeGate3Content(
    const palace::PalaceLezFinalizedAuthorityBundleV1&
        authority,
    const palace::PalaceStorageMvpBundle& storage,
    const std::string& storageMode,
    const std::size_t verifiedObjects)
{
    ActiveGate3Content result;
    if ((storageMode != "verified"
            && storageMode != "retained")
        || verifiedObjects != storage.artifactCount()) {
        result.reason = "gate3-bundle-not-verified";
        return result;
    }

    result = gate3AuthorityLinkedContent(authority, storage);
    if (!result.accepted)
        return result;
    if (result.script
        != "ON SELECT door\n"
           "SET door_open 1\n"
           "GOTOROOM lounge\n") {
        result.accepted = false;
        result.reason = "gate3-content-mismatch";
    }
    return result;
}

std::optional<palace::PalaceLezRootRecordV3>
finalizedAuthorityRootRecord(
    const palace::PalaceLezFinalizedAuthorityBundleV1& authority)
{
    const palace::PalaceLezRootRecordV3* root = nullptr;
    for (const palace::PalaceLezFinalizedAuthorityAccountV1& stored
         : authority.accounts) {
        if (stored.accountIdHex != authority.scope.rootAccountIdHex)
            continue;
        const palace::PalaceLezPublicAccountV3 decoded =
            palace::PalaceLezCodec::decodePublicAccount(
                stored.responseJson, authority.scope.programIdHex);
        const auto* candidate = decoded.accepted
            ? std::get_if<palace::PalaceLezRootRecordV3>(
                  &decoded.record)
            : nullptr;
        if (candidate == nullptr || root != nullptr)
            return std::nullopt;
        root = candidate;
    }
    if (root == nullptr
        || root->lastOrderedActionId
            != authority.checkpoint.lastOrderedActionId
        || !palace::isSafePalaceCid(root->activeManifestCid)) {
        return std::nullopt;
    }
    return *root;
}

std::optional<std::string> palaceIdFromUri(
    const std::string& palaceUri)
{
    static const std::string prefix = "palace://";
    if (palaceUri.size() != prefix.size() + 64U
        || palaceUri.compare(0U, prefix.size(), prefix) != 0) {
        return std::nullopt;
    }
    const std::string palaceId = palaceUri.substr(prefix.size());
    if (!isLowerHexAccountId(palaceId)
        || palaceId == std::string(64U, '0')) {
        return std::nullopt;
    }
    return palaceId;
}

#if defined(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
std::optional<std::string> storageAcceptanceHolderId(
    const std::string& profile)
{
    if (profile != "alice"
        && profile != "bob"
        && profile != "carol") {
        return std::nullopt;
    }
    return palace::crypto::sha256Hex(
        "logos-palace-storage-acceptance-holder-v1\n"
        + profile + "\n");
}
#endif

std::string base64Url(const std::string& bytes)
{
    return QByteArray(
               bytes.data(), static_cast<qsizetype>(bytes.size()))
        .toBase64(
            QByteArray::Base64UrlEncoding
            | QByteArray::OmitTrailingEquals)
        .toStdString();
}

std::optional<std::string> decodeBase64Url(
    const std::string& encoded,
    std::size_t maximumDecodedBytes)
{
    if (encoded.empty()
        || encoded.size() > maximumDecodedBytes * 2U) {
        return std::nullopt;
    }
    const QByteArray decoded = QByteArray::fromBase64(
        QByteArray::fromStdString(encoded),
        QByteArray::Base64UrlEncoding
            | QByteArray::AbortOnBase64DecodingErrors);
    const std::string bytes(
        decoded.constData(),
        static_cast<std::size_t>(decoded.size()));
    if (bytes.empty()
        || bytes.size() > maximumDecodedBytes
        || base64Url(bytes) != encoded) {
        return std::nullopt;
    }
    return bytes;
}

std::vector<std::uint8_t> encodeLezWords(const std::vector<std::uint32_t>& words)
{
    std::vector<std::uint8_t> bytes;
    bytes.reserve(words.size() * sizeof(std::uint32_t));
    for (const std::uint32_t word : words) {
        bytes.push_back(static_cast<std::uint8_t>(word & 0xffU));
        bytes.push_back(static_cast<std::uint8_t>((word >> 8U) & 0xffU));
        bytes.push_back(static_cast<std::uint8_t>((word >> 16U) & 0xffU));
        bytes.push_back(static_cast<std::uint8_t>((word >> 24U) & 0xffU));
    }
    return bytes;
}

std::int64_t deliveryNowSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

palace::PalaceLezAuthorityBundleExpectationV1
lezAuthorityBundleExpectation()
{
    palace::PalaceLezAuthorityBundleExpectationV1 expectation;
    expectation.scope.networkId =
        palace::PalaceLezReleaseLock::network().networkId;
    expectation.scope.programIdHex =
        palace::PalaceLezReleaseLock::network().programIdHex;
    expectation.scope.rootAccountIdHex =
        palace::PalaceLezCodec::deriveRootPda(
            expectation.scope.programIdHex);
    return expectation;
}

palace::PalaceLezExplorerFinalityLimitsV1
lezHistoryLimits()
{
    palace::PalaceLezExplorerFinalityLimitsV1 limits;
    limits.maxPages = 64U;
    limits.maxBlocks = 2048U;
    return limits;
}

bool sameSubmissionRecoveryExpectation(
    const palace::PalaceLezSubmissionRecoveryExpectationV1& first,
    const palace::PalaceLezSubmissionRecoveryExpectationV1& second)
{
    return first.programIdHex == second.programIdHex
        && first.accountIdsHex == second.accountIdsHex
        && first.instructionWords == second.instructionWords
        && first.signatureCount == second.signatureCount
        && first.minimumFinalizedBlockExclusive
            == second.minimumFinalizedBlockExclusive;
}

bool sameSubmissionIntentPayload(
    const palace::PalaceLezSubmissionIntentV1& first,
    const palace::PalaceLezSubmissionIntentV1& second)
{
    palace::PalaceLezSubmissionIntentV1 normalizedFirst = first;
    palace::PalaceLezSubmissionIntentV1 normalizedSecond = second;
    normalizedFirst.phase =
        palace::PalaceLezSubmissionIntentPhase::Prepared;
    normalizedSecond.phase =
        palace::PalaceLezSubmissionIntentPhase::Prepared;
    normalizedFirst.transactionHash.clear();
    normalizedSecond.transactionHash.clear();
    return palace::samePalaceLezSubmissionIntent(
        normalizedFirst, normalizedSecond);
}

palace::PalaceLezSubmissionRecoveryExpectationV1
submissionRecoveryExpectation(
    const palace::PalaceLezSubmissionIntentV1& intent)
{
    palace::PalaceLezSubmissionRecoveryExpectationV1 expectation;
    expectation.programIdHex = intent.plan.programIdHex;
    expectation.accountIdsHex = intent.plan.accountIdsHex;
    expectation.instructionWords = intent.plan.instructionWords;
    expectation.signatureCount =
        static_cast<std::size_t>(std::count(
            intent.plan.signingRequirements.begin(),
            intent.plan.signingRequirements.end(),
            true));
    expectation.minimumFinalizedBlockExclusive =
        intent.minimumFinalizedBlockExclusive;
    return expectation;
}

constexpr char kPublicAccountRegistrationProgramIdHex[] =
    "dcbbfebcd59399961ed9973b8307dc475fd4c5ca5779aacfe7588f7dbc3f4a71";

struct LezWalletPaths {
    bool accepted = false;
    std::string reason;
    QString config;
    QString storage;
    bool storageExists = false;
};

LezWalletPaths prepareLezWalletPaths(const std::string& persistenceRoot)
{
    if (persistenceRoot.empty())
        return {false, "missing-instance-root", {}, {}, false};

    const QString root = QDir::cleanPath(
        QFileInfo(QString::fromStdString(persistenceRoot))
            .absoluteFilePath());
    if (root.isEmpty() || QFileInfo(root).isSymLink())
        return {false, "unsafe-instance-root", {}, {}, false};
    if (!QDir().mkpath(root) || !QFileInfo(root).isDir())
        return {false, "instance-root-unavailable", {}, {}, false};

    const QString config = QDir(root).filePath(
        QStringLiteral("lez-wallet-config-v1.json"));
    const QString storage = QDir(root).filePath(
        QStringLiteral("lez-wallet-storage-v1.json"));
    if (!isUnder(QFileInfo(config).absoluteFilePath(), root)
        || !isUnder(QFileInfo(storage).absoluteFilePath(), root)) {
        return {false, "wallet-path-escape", {}, {}, false};
    }

    const std::string& expected =
        palace::PalaceLezReleaseLock::walletConfigJson();
    const QFileInfo configInfo(config);
    if (configInfo.exists()) {
        if (configInfo.isSymLink() || !configInfo.isFile()
            || configInfo.size() < 0
            || static_cast<std::uint64_t>(configInfo.size()) > 16U * 1024U) {
            return {false, "unsafe-wallet-config", {}, {}, false};
        }
        QFile input(config);
        if (!input.open(QIODevice::ReadOnly)
            || input.readAll().toStdString() != expected) {
            return {false, "wallet-config-drift", {}, {}, false};
        }
    } else {
        QSaveFile output(config);
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly)
            || !output.setPermissions(
                QFileDevice::ReadOwner | QFileDevice::WriteOwner)
            || output.write(
                   expected.data(),
                   static_cast<qint64>(expected.size()))
                != static_cast<qint64>(expected.size())
            || !output.commit()
            || !QFile::setPermissions(
                config,
                QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
            return {false, "wallet-config-write-failed", {}, {}, false};
        }
    }

    const QFileInfo storageInfo(storage);
    if (storageInfo.exists()
        && (storageInfo.isSymLink() || !storageInfo.isFile())) {
        return {false, "unsafe-wallet-storage", {}, {}, false};
    }
    return {
        true,
        "accepted",
        config,
        storage,
        storageInfo.exists(),
    };
}

void cleanse(std::string& value)
{
    if (!value.empty())
        OPENSSL_cleanse(value.data(), value.size());
    value.clear();
}

void incrementSaturated(std::uint64_t& value)
{
    if (value != std::numeric_limits<std::uint64_t>::max())
        ++value;
}

struct DeliveryStartConfig {
    bool accepted = false;
    std::string reason;
    std::string moduleConfig;
    std::string acceptanceProfile;
};

DeliveryStartConfig parseDeliveryStartConfig(const std::string& nodeConfig)
{
    if (nodeConfig.empty())
        return {false, "empty-config", {}, {}};
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray::fromStdString(nodeConfig), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return {false, "invalid-config", {}, {}};

    QJsonObject object = document.object();
    std::string profile;
#if defined(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
    const QJsonValue profileValue =
        object.take(QStringLiteral("palaceAcceptanceProfile"));
    if (!profileValue.isUndefined()) {
        if (!profileValue.isString())
            return {false, "invalid-acceptance-profile", {}, {}};
        profile = profileValue.toString().toStdString();
    }
#endif
    return {
        true,
        "accepted",
        QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString(),
        profile,
    };
}

std::optional<palace::DeliveryNativeNodeState>
parseDeliveryNativeNodeState(const std::string& payload)
{
    static constexpr std::size_t kMaximumNodeStatusBytes = 16384U;
    if (payload.empty() || payload.size() > kMaximumNodeStatusBytes)
        return std::nullopt;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray::fromStdString(payload), &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        return std::nullopt;
    }

    const QJsonValue stateValue =
        document.object().value(QStringLiteral("state"));
    if (!stateValue.isString())
        return std::nullopt;
    return palace::parseDeliveryNativeNodeStateName(
        stateValue.toString().toStdString());
}

} // namespace

PalaceCoreImpl::~PalaceCoreImpl()
{
    if (m_callbackLifetime)
        m_callbackLifetime->invalidate();
    // Aborting an active Qt reply can invoke its completion synchronously.
    // Clear the guarded owner before transport teardown.
    m_lezFinalityTransport.reset();
}

void PalaceCoreImpl::onContextReady()
{
    m_roomTransitionParticipantWriteGate
        .blockParticipantWrites();
    if (!m_callbackLifetime) {
        m_callbackLifetime =
            std::make_shared<
                palace::CallbackLifetime<PalaceCoreImpl>>(this);
    }
    registerDeliveryCallbacks();
    registerStorageCallbacks();
    if (instancePersistencePath().empty())
        return;
    m_projectionStore = std::make_unique<palace::ProjectionStore>(instancePersistencePath());
    m_actionJournalStore = std::make_unique<palace::ActionJournalStore>(instancePersistencePath());
    m_deliverySessionStore =
        std::make_unique<palace::DeliverySessionStore>(instancePersistencePath());
    bool projectionPersistenceDeferred = false;
    const auto persistStartupProjection =
        [this, &projectionPersistenceDeferred]() {
            if (!persistProjection())
                projectionPersistenceDeferred = true;
        };
    palace::PalaceRoomTransitionJournalStore
        roomTransitionPreflight(instancePersistencePath());
    palace::PalaceRoomTransitionIntentV1
        pendingRoomTransition;
    const palace::PalaceRoomTransitionJournalStatus
        roomTransitionPreflightStatus =
            roomTransitionPreflight.load(
                pendingRoomTransition);
    if (roomTransitionPreflightStatus
            != palace::PalaceRoomTransitionJournalStatus::
                Loaded
        && roomTransitionPreflightStatus
            != palace::PalaceRoomTransitionJournalStatus::
                NotFound) {
        return;
    }
    m_lezCoordinatorStore =
        std::make_unique<palace::PalaceLezCoordinatorStore>(
            instancePersistencePath());
    m_lezSubmissionIntentStore =
        std::make_unique<palace::PalaceLezSubmissionIntentStore>(
            instancePersistencePath());
    m_lezFinalityTransport =
        std::make_unique<palace::PalaceLezExplorerQtTransport>();
    m_verifiedAssetStore = std::make_unique<palace::VerifiedAssetStore>(instancePersistencePath());
    m_deliveryIdentityRegistration =
        std::make_unique<
            palace::PalaceDeliveryIdentityRegistration>(
                instancePersistencePath());
    const bool assetAuthoringReady =
        m_assetAuthoring.initialize(
            instancePersistencePath(),
            *m_verifiedAssetStore);
    if (assetAuthoringReady) {
        for (const palace::AssetAuthoringAssetV1& asset
             : m_assetAuthoring.assets()) {
            if (!asset.publishedCid.empty()) {
                m_publicationStatus[asset.handle] =
                    "published;cid=" + asset.publishedCid;
            }
        }
    }
    if (!m_projectionStore->load(m_projection)) {
        m_projection.setSyncHealth(palace::SyncHealth::Degraded);
        persistStartupProjection();
    }
    if (!assetAuthoringReady) {
        m_projection.setSyncHealth(
            palace::SyncHealth::Degraded);
        persistStartupProjection();
    }
    if (!m_actionJournalStore->load(m_actionJournal) && m_actionJournalStore->exists()) {
        m_projection.setSyncHealth(palace::SyncHealth::Degraded);
        persistStartupProjection();
    }
    if (!loadPalaceVmTurn()) {
        m_palaceVmStoreState = "degraded-invalid";
        m_projection.setSyncHealth(palace::SyncHealth::Degraded);
        persistStartupProjection();
    }
    const palace::PalaceLezCoordinatorStoreStatus coordinatorStatus =
        m_lezCoordinatorStore->load(m_lezCoordinator);
    if (coordinatorStatus
            != palace::PalaceLezCoordinatorStoreStatus::Loaded
        && coordinatorStatus
            != palace::PalaceLezCoordinatorStoreStatus::NotFound) {
        m_lezCoordinatorStoreHealthy = false;
        m_projection.setSyncHealth(palace::SyncHealth::Degraded);
        persistStartupProjection();
    }
    palace::PalaceLezSubmissionIntentV1 restoredSubmissionIntent;
    const palace::PalaceLezSubmissionIntentStoreStatus
        submissionIntentStatus =
            m_lezSubmissionIntentStore->load(
                restoredSubmissionIntent);
    if (submissionIntentStatus
        == palace::PalaceLezSubmissionIntentStoreStatus::Loaded) {
        m_lezSubmissionIntent =
            std::move(restoredSubmissionIntent);
    } else if (submissionIntentStatus
               != palace::PalaceLezSubmissionIntentStoreStatus::
                   NotFound) {
        m_lezCoordinatorStoreHealthy = false;
        m_projection.setSyncHealth(palace::SyncHealth::Degraded);
        persistStartupProjection();
    }
    const palace::DeliveryIdentityOpenStatus identityStatus =
        palace::PalaceDeliveryIdentity::load(
            instancePersistencePath(), m_deliveryIdentity);
    if (identityStatus != palace::DeliveryIdentityOpenStatus::Loaded
        && identityStatus != palace::DeliveryIdentityOpenStatus::NotFound) {
        m_projection.setSyncHealth(palace::SyncHealth::Degraded);
        persistStartupProjection();
    } else if (identityStatus
                   == palace::DeliveryIdentityOpenStatus::Loaded) {
        const palace::DeliveryIdentityRegistrationStoreStatus
            registrationStatus =
                m_deliveryIdentityRegistration->restore(
                    m_deliveryIdentity.accountId());
        if (registrationStatus
                != palace::DeliveryIdentityRegistrationStoreStatus::
                    Loaded
            && registrationStatus
                != palace::DeliveryIdentityRegistrationStoreStatus::
                    NotFound) {
            m_projection.setSyncHealth(
                palace::SyncHealth::Degraded);
            persistStartupProjection();
        }
    }

    const palace::PalaceLezAuthorityBundleExpectationV1
        authorityExpectation = lezAuthorityBundleExpectation();
    m_lezAuthorityBundleStore =
        std::make_unique<palace::PalaceLezAuthorityBundleStore>(
            instancePersistencePath(), authorityExpectation);
    m_storageMvpCatalogStore =
        std::make_unique<palace::PalaceStorageMvpCatalogStore>(
            instancePersistencePath());
    palace::PalaceLezFinalizedAuthorityBundleV1 restoredAuthority;
    const palace::PalaceLezAuthorityBundleStoreStatus authorityStatus =
        m_lezAuthorityBundleStore->load(restoredAuthority);
    if (authorityStatus
        == palace::PalaceLezAuthorityBundleStoreStatus::Loaded) {
        const palace::PalaceLezAuthorityStateUpdateV1 restored =
            palace::restoreFinalizedLezAuthorityStateV1(
                m_deliveryAuthority,
                restoredAuthority,
                authorityExpectation);
        if (restored.accepted) {
            m_lezAuthorityBundle = std::move(restoredAuthority);
            m_lezAuthorityReady = true;
            m_lezAuthorityState = "restored";
        } else {
            m_lezAuthorityState =
                "degraded-" + restored.reason;
            m_projection.setSyncHealth(
                palace::SyncHealth::Degraded);
            persistStartupProjection();
        }
    } else if (authorityStatus
               != palace::PalaceLezAuthorityBundleStoreStatus::
                   NotFound) {
        m_lezAuthorityState =
            "degraded-"
            + std::string(
                palace::palaceLezAuthorityBundleStoreStatusName(
                    authorityStatus));
        m_projection.setSyncHealth(palace::SyncHealth::Degraded);
        persistStartupProjection();
    }

    if (m_lezAuthorityReady && !restoreStorageMvpCatalog()) {
        m_projection.setSyncHealth(palace::SyncHealth::Degraded);
        persistStartupProjection();
    }

#if defined(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
    if (!m_lezAuthorityReady
        && authorityStatus
            == palace::PalaceLezAuthorityBundleStoreStatus::
                NotFound
        && !palace::bootstrapDeliveryAcceptanceAuthority(
            m_deliveryAuthority)) {
        m_projection.setSyncHealth(palace::SyncHealth::Degraded);
        persistStartupProjection();
        return;
    }
#endif
    m_deliverySession =
        std::make_unique<palace::PalaceDeliverySession>(m_deliveryAuthority);
    const bool deliverySessionExists =
        m_deliverySessionStore->exists();
    bool deliverySessionLoaded = !deliverySessionExists;
    if (deliverySessionExists) {
        deliverySessionLoaded =
            m_deliverySessionStore->load(*m_deliverySession);
    }

    const bool startupProjectionDegraded =
        m_projection.syncHealth()
        == palace::SyncHealth::Degraded;
    {
        auto recoveryWrite =
            m_roomTransitionParticipantWriteGate
                .acquireTransitionWrite();
        if (!recoveryWrite) {
            m_deliverySession.reset();
            return;
        }
        palace::PalaceRoomTransitionFileDurability
            roomTransitionDurability(
                instancePersistencePath(),
                *m_deliverySessionStore,
                *m_projectionStore);
        palace::PalaceRoomTransitionCoordinator
            roomTransitionCoordinator(
                roomTransitionDurability);
        palace::PalaceRoomTransitionResult
            roomTransitionRecovery =
                roomTransitionCoordinator.recover(
                    *m_deliverySession);
        if (!palace::
                palaceRoomTransitionStartupMayUnblockParticipantWrites(
                    roomTransitionPreflightStatus,
                    roomTransitionRecovery.status,
                    true)) {
            // Do not mutate either participant store while a valid,
            // corrupt, appeared, or disappeared intent is unresolved.
            m_deliverySession.reset();
            return;
        }
        if (roomTransitionRecovery.completed()) {
            m_deliverySession =
                std::move(
                    roomTransitionRecovery.committedSession);
            m_projection =
                std::move(
                    *roomTransitionRecovery
                         .committedProjection);
            deliverySessionLoaded = true;
            if (startupProjectionDegraded
                && m_projection.syncHealth()
                    != palace::SyncHealth::Degraded) {
                m_projection.setSyncHealth(
                    palace::SyncHealth::Degraded);
                projectionPersistenceDeferred = true;
            }
        }
        const bool projectionPersistenceComplete =
            !projectionPersistenceDeferred
            || m_projectionStore->save(m_projection);
        if (!palace::
                palaceRoomTransitionStartupMayUnblockParticipantWrites(
                    roomTransitionPreflightStatus,
                    roomTransitionRecovery.status,
                    projectionPersistenceComplete)) {
            m_deliverySession.reset();
            return;
        }
        if (!m_roomTransitionParticipantWriteGate
                 .unblockParticipantWrites(
                     recoveryWrite)) {
            m_deliverySession.reset();
            return;
        }
    }
    m_roomTransitionHealthy.store(
        true, std::memory_order_release);

    if (!deliverySessionLoaded) {
        if (deliverySessionExists) {
            m_projection.setSyncHealth(palace::SyncHealth::Degraded);
            persistProjection();
        }
    } else if (m_deliverySession->hasConfiguration()) {
            const std::string& sender =
                m_deliverySession->configuration().senderUserId;
            if (m_deliveryIdentity.valid()
                && m_deliveryIdentity.accountId() == sender) {
                m_deliveryProfile =
                    m_deliveryIdentity.accountId();
                m_deliveryDisplayName =
                    m_deliveryIdentity.displayName();
                m_deliveryKeyEpoch =
                    m_deliveryIdentity.deliveryKeyEpoch();
                m_deliverySigner = &m_deliveryIdentity;
#if defined(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
            } else if (palace::deliveryAcceptanceIdentity(
                           sender,
                           m_deliveryAcceptanceIdentity)) {
                m_deliveryProfile =
                    m_deliveryAcceptanceIdentity.userId;
                m_deliveryDisplayName =
                    m_deliveryAcceptanceIdentity.displayName;
                m_deliveryKeyEpoch =
                    m_deliveryAcceptanceIdentity.keyEpoch;
                m_deliverySigner =
                    &m_deliveryAcceptanceIdentity.signer;
#endif
            } else {
                m_projection.setSyncHealth(
                    palace::SyncHealth::Degraded);
                persistProjection();
            }
    }
#if defined(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
    if (!m_lezAuthorityReady
        && authorityStatus
            == palace::PalaceLezAuthorityBundleStoreStatus::
                NotFound) {
        m_deliverySession->replaceAllowedProps(
            palace::deliveryAcceptanceAllowedProps());
    } else {
        refreshDeliveryAllowedProps();
    }
#else
    refreshDeliveryAllowedProps();
#endif
}

bool PalaceCoreImpl::persistProjection()
{
    if (!m_projectionStore)
        return false;
    auto participantWrite =
        m_roomTransitionParticipantWriteGate
            .acquireParticipantWrite();
    return participantWrite
        && m_projectionStore->save(m_projection);
}

void PalaceCoreImpl::persistActionJournal()
{
    if (m_actionJournalStore)
        m_actionJournalStore->save(m_actionJournal);
}

bool PalaceCoreImpl::validPalaceVmTurn(
    const PalaceVmTurn& turn) const
{
    std::uint64_t actionId = 0U;
    std::uint64_t roomEpoch = 0U;
    const std::string expectedRoot =
        palace::crypto::sha256Hex(turn.resultingState);
    const std::string expectedProvisional =
        expectedDoorVmReceipt(
            turn.scriptBundleCid,
            turn.roomEpoch,
            turn.trigger,
            turn.resultingState,
            turn.expectedStateRootHex,
            false);
    const std::string expectedFinalized =
        expectedDoorVmReceipt(
            turn.scriptBundleCid,
            turn.roomEpoch,
            turn.trigger,
            turn.resultingState,
            turn.expectedStateRootHex,
            true);
    if (!validPalaceVmPhase(turn.phase)
        || !safeStatusReason(turn.reason)
        || !palace::PalaceLezCodec::parseOrderedActionId(
            turn.actionId, actionId)
        || !parseCanonicalU64(turn.roomEpoch, roomEpoch)
        || !isNonzeroLowerHexAccountId(turn.palaceIdHex)
        || !isNonzeroLowerHexAccountId(
            turn.rootAccountIdHex)
        || !isNonzeroLowerHexAccountId(
            turn.callerAccountIdHex)
        || !isNonzeroLowerHexAccountId(turn.programIdHex)
        || !isNonzeroLowerHexAccountId(turn.grantIdHex)
        || !isNonzeroLowerHexAccountId(
            turn.sharedStateIdHex)
        || !isNonzeroLowerHexAccountId(turn.roomIdHex)
        || turn.programIdHex
            != palace::PalaceLezReleaseLock::network()
                   .programIdHex
        || turn.rootAccountIdHex
            != palace::PalaceLezCodec::deriveRootPda(
                turn.programIdHex)
        || turn.script
            != "ON SELECT door\n"
               "SET door_open 1\n"
               "GOTOROOM lounge\n"
        || !palace::isSafePalaceCid(
            turn.scriptBundleCid)
        || turn.trigger != "SELECT:door"
        || turn.priorState != "door_open=0"
        || turn.allowedRooms != "atrium,lounge"
        || turn.resultingState != "door_open=1"
        || turn.navigateRoom != "lounge"
        || turn.expectedStateRootHex != expectedRoot
        || !isNonzeroLowerHexAccountId(
            turn.expectedStateRootHex)
        || turn.stateRevision == 0U
        || turn.instructionBudget != 8U
        || turn.roomLocked
        || !turn.canMutateSharedState) {
        return false;
    }

    if (turn.phase == "prepared") {
        return turn.provisionalReceipt.empty()
            && turn.finalizedReceipt.empty()
            && !turn.navigationApplied;
    }
    if (turn.phase == "degraded") {
        return (turn.provisionalReceipt.empty()
                || turn.provisionalReceipt
                    == expectedProvisional)
            && turn.finalizedReceipt.empty()
            && !turn.navigationApplied;
    }
    if (turn.provisionalReceipt != expectedProvisional)
        return false;
    if (turn.phase == "promoted") {
        return turn.finalizedReceipt == expectedFinalized;
    }
    return turn.finalizedReceipt.empty()
        && !turn.navigationApplied;
}

bool PalaceCoreImpl::loadPalaceVmTurn()
{
    static constexpr char kMagic[] =
        "PALACE_CORE_VM_TURN_V1\n";
    static constexpr qint64 kMaximumStoreBytes =
        64 * 1024;

    m_palaceVmTurn.reset();
    const QString root = QDir::cleanPath(
        QFileInfo(QString::fromStdString(
                      instancePersistencePath()))
            .absoluteFilePath());
    const QString path = QDir(root).filePath(
        QStringLiteral("palace-core-vm-turn-v1"));
    const QFileInfo info(path);
    if (!info.exists()) {
        m_palaceVmStoreState = "missing";
        return true;
    }
    const QFileDevice::Permissions unsafePermissions =
        QFileDevice::ReadGroup
        | QFileDevice::WriteGroup
        | QFileDevice::ExeGroup
        | QFileDevice::ReadOther
        | QFileDevice::WriteOther
        | QFileDevice::ExeOther;
    if (root.isEmpty() || !isUnder(info.absoluteFilePath(), root)
        || info.isSymLink() || !info.isFile()
        || info.size() <= 0
        || info.size() > kMaximumStoreBytes
        || (info.permissions() & unsafePermissions)) {
        return false;
    }

    QFile input(path);
    if (!input.open(QIODevice::ReadOnly))
        return false;
    const QByteArray record =
        input.read(kMaximumStoreBytes + 1);
    if (!input.atEnd()
        || record.size() > kMaximumStoreBytes) {
        return false;
    }
    const QByteArray magic(kMagic);
    if (!record.startsWith(magic)
        || record.size()
            <= magic.size() + 64) {
        return false;
    }
    const qsizetype checksumOffset = magic.size();
    const qsizetype payloadOffset =
        checksumOffset + 65;
    if (record.at(checksumOffset + 64) != '\n')
        return false;
    const std::string checksum =
        record.mid(checksumOffset, 64).toStdString();
    const QByteArray payload = record.mid(payloadOffset);
    if (!isLowerHexAccountId(checksum)
        || payload.isEmpty()
        || palace::crypto::sha256Hex(payload.toStdString())
            != checksum) {
        return false;
    }

    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError
        || !document.isObject()
        || document.toJson(QJsonDocument::Compact)
            != payload) {
        return false;
    }
    const QJsonObject object = document.object();
    if (!exactJsonKeys(
            object,
            {
                "phase", "reason", "action_id",
                "palace_id_hex", "root_account_id_hex",
                "caller_account_id_hex", "program_id_hex",
                "grant_id_hex", "shared_state_id_hex",
                "room_id_hex", "script", "script_bundle_cid",
                "room_epoch", "trigger", "prior_state",
                "allowed_rooms", "resulting_state",
                "expected_state_root_hex",
                "provisional_receipt", "finalized_receipt",
                "navigate_room", "state_revision",
                "instruction_budget", "room_locked",
                "can_mutate_shared_state",
                "navigation_applied",
            })) {
        return false;
    }
    const auto stringValue =
        [&object](const char* key, std::string& value) {
        const QJsonValue encoded =
            object.value(QString::fromLatin1(key));
        if (!encoded.isString())
            return false;
        value = encoded.toString().toStdString();
        return true;
    };
    const auto boolValue =
        [&object](const char* key, bool& value) {
        const QJsonValue encoded =
            object.value(QString::fromLatin1(key));
        if (!encoded.isBool())
            return false;
        value = encoded.toBool();
        return true;
    };

    PalaceVmTurn restored;
    std::string stateRevision;
    std::string instructionBudget;
    if (!stringValue("phase", restored.phase)
        || !stringValue("reason", restored.reason)
        || !stringValue("action_id", restored.actionId)
        || !stringValue(
            "palace_id_hex", restored.palaceIdHex)
        || !stringValue(
            "root_account_id_hex",
            restored.rootAccountIdHex)
        || !stringValue(
            "caller_account_id_hex",
            restored.callerAccountIdHex)
        || !stringValue(
            "program_id_hex", restored.programIdHex)
        || !stringValue(
            "grant_id_hex", restored.grantIdHex)
        || !stringValue(
            "shared_state_id_hex",
            restored.sharedStateIdHex)
        || !stringValue(
            "room_id_hex", restored.roomIdHex)
        || !stringValue("script", restored.script)
        || !stringValue(
            "script_bundle_cid",
            restored.scriptBundleCid)
        || !stringValue(
            "room_epoch", restored.roomEpoch)
        || !stringValue("trigger", restored.trigger)
        || !stringValue(
            "prior_state", restored.priorState)
        || !stringValue(
            "allowed_rooms", restored.allowedRooms)
        || !stringValue(
            "resulting_state", restored.resultingState)
        || !stringValue(
            "expected_state_root_hex",
            restored.expectedStateRootHex)
        || !stringValue(
            "provisional_receipt",
            restored.provisionalReceipt)
        || !stringValue(
            "finalized_receipt",
            restored.finalizedReceipt)
        || !stringValue(
            "navigate_room", restored.navigateRoom)
        || !stringValue(
            "state_revision", stateRevision)
        || !stringValue(
            "instruction_budget", instructionBudget)
        || !boolValue("room_locked", restored.roomLocked)
        || !boolValue(
            "can_mutate_shared_state",
            restored.canMutateSharedState)
        || !boolValue(
            "navigation_applied",
            restored.navigationApplied)
        || !parseCanonicalU64(
            stateRevision, restored.stateRevision)
        || !parseCanonicalU64(
            instructionBudget,
            restored.instructionBudget)
        || !validPalaceVmTurn(restored)) {
        return false;
    }
    m_palaceVmTurn = std::move(restored);
    m_palaceVmStoreState = "loaded";
    return true;
}

bool PalaceCoreImpl::savePalaceVmTurn(
    const PalaceVmTurn& turn) const
{
    static constexpr char kMagic[] =
        "PALACE_CORE_VM_TURN_V1\n";
    if (!validPalaceVmTurn(turn))
        return false;

    QJsonObject object;
    object.insert("phase", QString::fromStdString(turn.phase));
    object.insert("reason", QString::fromStdString(turn.reason));
    object.insert(
        "action_id", QString::fromStdString(turn.actionId));
    object.insert(
        "palace_id_hex",
        QString::fromStdString(turn.palaceIdHex));
    object.insert(
        "root_account_id_hex",
        QString::fromStdString(turn.rootAccountIdHex));
    object.insert(
        "caller_account_id_hex",
        QString::fromStdString(turn.callerAccountIdHex));
    object.insert(
        "program_id_hex",
        QString::fromStdString(turn.programIdHex));
    object.insert(
        "grant_id_hex",
        QString::fromStdString(turn.grantIdHex));
    object.insert(
        "shared_state_id_hex",
        QString::fromStdString(turn.sharedStateIdHex));
    object.insert(
        "room_id_hex",
        QString::fromStdString(turn.roomIdHex));
    object.insert(
        "script", QString::fromStdString(turn.script));
    object.insert(
        "script_bundle_cid",
        QString::fromStdString(turn.scriptBundleCid));
    object.insert(
        "room_epoch",
        QString::fromStdString(turn.roomEpoch));
    object.insert(
        "trigger", QString::fromStdString(turn.trigger));
    object.insert(
        "prior_state",
        QString::fromStdString(turn.priorState));
    object.insert(
        "allowed_rooms",
        QString::fromStdString(turn.allowedRooms));
    object.insert(
        "resulting_state",
        QString::fromStdString(turn.resultingState));
    object.insert(
        "expected_state_root_hex",
        QString::fromStdString(turn.expectedStateRootHex));
    object.insert(
        "provisional_receipt",
        QString::fromStdString(turn.provisionalReceipt));
    object.insert(
        "finalized_receipt",
        QString::fromStdString(turn.finalizedReceipt));
    object.insert(
        "navigate_room",
        QString::fromStdString(turn.navigateRoom));
    object.insert(
        "state_revision",
        QString::fromStdString(
            std::to_string(turn.stateRevision)));
    object.insert(
        "instruction_budget",
        QString::fromStdString(
            std::to_string(turn.instructionBudget)));
    object.insert("room_locked", turn.roomLocked);
    object.insert(
        "can_mutate_shared_state",
        turn.canMutateSharedState);
    object.insert(
        "navigation_applied",
        turn.navigationApplied);
    const QByteArray payload =
        QJsonDocument(object).toJson(QJsonDocument::Compact);
    const std::string record =
        std::string(kMagic)
        + palace::crypto::sha256Hex(payload.toStdString())
        + '\n' + payload.toStdString();

    const QString root = QDir::cleanPath(
        QFileInfo(QString::fromStdString(
                      instancePersistencePath()))
            .absoluteFilePath());
    const QString path = QDir(root).filePath(
        QStringLiteral("palace-core-vm-turn-v1"));
    const QFileInfo info(path);
    if (root.isEmpty()
        || !isUnder(info.absoluteFilePath(), root)
        || info.isSymLink())
        return false;

    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)
        || !output.setPermissions(
            QFileDevice::ReadOwner
            | QFileDevice::WriteOwner)
        || output.write(
               record.data(),
               static_cast<qint64>(record.size()))
            != static_cast<qint64>(record.size())
        || !output.commit()
        || !QFile::setPermissions(
            path,
            QFileDevice::ReadOwner
            | QFileDevice::WriteOwner)) {
        return false;
    }
    return true;
}

std::map<std::string, std::string>
PalaceCoreImpl::productionDeliveryAllowedProps() const
{
    if (!m_lezAuthorityReady
        || !m_storageMvpFailures.empty()) {
        return {};
    }
    const ActiveGate3Content content =
        activeGate3Content(
            m_lezAuthorityBundle,
            m_storageMvpBundle,
            m_storageMvpMode,
            m_storageMvpFetchedObjects.size());
    if (!content.accepted)
        return {};
    const std::string propId = m_storageMvpBundle.propId();
    return propId.empty()
        ? std::map<std::string, std::string>{}
        : std::map<std::string, std::string>{
            {propId, content.propCid},
        };
}

void PalaceCoreImpl::refreshDeliveryAllowedProps()
{
    std::lock_guard<std::mutex> lock(m_deliveryMutex);
    if (!m_roomTransitionHealthy.load(
            std::memory_order_acquire)
        || !m_deliverySession) {
        return;
    }
    const auto replaceAndReconcile =
        [this](std::map<std::string, std::string> allowedProps) {
            m_deliverySession->replaceAllowedProps(
                std::move(allowedProps));
            if (m_deliverySession->reconcileAuthority())
                persistDeliverySessionLocked();
        };
#if defined(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
    if (m_deliverySession->hasConfiguration()
        && m_deliverySession->configuration().networkId
            == "logos.test") {
        replaceAndReconcile(
            palace::deliveryAcceptanceAllowedProps());
        return;
    }
    if (!m_lezAuthorityReady) {
        replaceAndReconcile(
            m_lezAuthorityState == "missing"
                ? palace::deliveryAcceptanceAllowedProps()
                : std::map<std::string, std::string>{});
        return;
    }
#else
    if (!m_lezAuthorityReady) {
        replaceAndReconcile({});
        return;
    }
#endif
    replaceAndReconcile(productionDeliveryAllowedProps());
}

void PalaceCoreImpl::persistDeliverySessionLocked()
{
    if (!m_roomTransitionHealthy.load(
            std::memory_order_acquire)
        || !m_deliverySession || !m_deliverySessionStore
        || !m_deliverySession->hasConfiguration()) {
        return;
    }
    if (!m_deliverySessionStore->save(*m_deliverySession)) {
        m_projection.setSyncHealth(palace::SyncHealth::Degraded);
        persistProjection();
    }
}

void PalaceCoreImpl::registerDeliveryCallbacks()
{
    const auto callbackLifetime = m_callbackLifetime;
    if (!callbackLifetime)
        return;
    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (m_deliveryCallbackRegistrationAttempted)
            return;
        m_deliveryCallbackRegistrationAttempted = true;
    }

    const bool nodeChangedRegistered = modules().delivery_module.onNodeChanged(
        [callbackLifetime](const std::string&) {
            callbackLifetime->invoke([](PalaceCoreImpl& owner) {
                QueuedDeliveryEvent event;
                event.kind = QueuedDeliveryEventKind::NodeChanged;
                owner.enqueueDeliveryEvent(std::move(event));
            });
        });
    const bool nodeStartedRegistered = modules().delivery_module.onNodeStarted(
        [callbackLifetime](
            bool succeeded,
            const std::string&,
            std::int64_t) {
            callbackLifetime->invoke(
                [succeeded](PalaceCoreImpl& owner) {
                    QueuedDeliveryEvent event;
                    event.kind =
                        QueuedDeliveryEventKind::NodeStarted;
                    event.succeeded = succeeded;
                    owner.enqueueDeliveryEvent(
                        std::move(event));
                });
        });
    const bool nodeStoppedRegistered = modules().delivery_module.onNodeStopped(
        [callbackLifetime](
            bool succeeded,
            const std::string&,
            std::int64_t) {
            callbackLifetime->invoke(
                [succeeded](PalaceCoreImpl& owner) {
                    QueuedDeliveryEvent event;
                    event.kind =
                        QueuedDeliveryEventKind::NodeStopped;
                    event.succeeded = succeeded;
                    owner.enqueueDeliveryEvent(
                        std::move(event));
                });
        });
    const bool connectionRegistered =
        modules().delivery_module.onConnectionStateChanged(
        [callbackLifetime](
            const std::string& status,
            std::int64_t) {
            callbackLifetime->invoke(
                [&status](PalaceCoreImpl& owner) {
                    QueuedDeliveryEvent event;
                    event.kind =
                        QueuedDeliveryEventKind::
                            ConnectionChanged;
                    event.connectionStatus = status;
                    owner.enqueueDeliveryEvent(
                        std::move(event));
                });
        });
    const bool messageReceivedRegistered =
        modules().delivery_module.onMessageReceived(
        [callbackLifetime](
            const std::string&,
            const std::string& contentTopic,
            const std::vector<std::uint8_t>& payload,
            std::int64_t) {
            callbackLifetime->invoke(
                [&contentTopic, &payload](
                    PalaceCoreImpl& owner) {
                    QueuedDeliveryEvent event;
                    event.kind =
                        QueuedDeliveryEventKind::
                            MessageReceived;
                    event.contentTopic = contentTopic;
                    if (payload.size() <= 4096U) {
                        event.payload = payload;
                    } else {
                        // Preserve only an over-limit marker; never
                        // copy an attacker-sized module payload into
                        // the callback queue.
                        event.payload.resize(4097U);
                    }
                    owner.enqueueDeliveryEvent(
                        std::move(event));
                });
        });
    const bool messageSentRegistered = modules().delivery_module.onMessageSent(
        [callbackLifetime](
            const std::string& requestId,
            const std::string&,
            std::int64_t) {
            callbackLifetime->invoke(
                [&requestId](PalaceCoreImpl& owner) {
                    QueuedDeliveryEvent event;
                    event.kind =
                        QueuedDeliveryEventKind::MessageSent;
                    event.requestId = requestId;
                    owner.enqueueDeliveryEvent(
                        std::move(event));
                });
        });
    const bool messagePropagatedRegistered =
        modules().delivery_module.onMessagePropagated(
        [callbackLifetime](
            const std::string& requestId,
            const std::string&,
            std::int64_t) {
            callbackLifetime->invoke(
                [&requestId](PalaceCoreImpl& owner) {
                    QueuedDeliveryEvent event;
                    event.kind =
                        QueuedDeliveryEventKind::
                            MessagePropagated;
                    event.requestId = requestId;
                    owner.enqueueDeliveryEvent(
                        std::move(event));
                });
        });
    const bool messageErrorRegistered =
        modules().delivery_module.onMessageError(
        [callbackLifetime](
            const std::string& requestId,
            const std::string&,
            const std::string&,
            std::int64_t) {
            callbackLifetime->invoke(
                [&requestId](PalaceCoreImpl& owner) {
                    QueuedDeliveryEvent event;
                    event.kind =
                        QueuedDeliveryEventKind::MessageError;
                    event.requestId = requestId;
                    owner.enqueueDeliveryEvent(
                        std::move(event));
                });
        });

    std::lock_guard<std::mutex> lock(m_deliveryMutex);
    m_deliveryCallbacksRegistered = nodeChangedRegistered
        && nodeStartedRegistered
        && nodeStoppedRegistered
        && connectionRegistered
        && messageReceivedRegistered
        && messageSentRegistered
        && messagePropagatedRegistered
        && messageErrorRegistered;
}

void PalaceCoreImpl::registerStorageCallbacks()
{
    const auto callbackLifetime = m_callbackLifetime;
    if (!callbackLifetime)
        return;
    if (m_storageCallbackRegistrationAttempted)
        return;
    m_storageCallbackRegistrationAttempted = true;

    const bool nodeChangedRegistered =
        modules().storage_module.onNodeChanged(
            [callbackLifetime](const std::string& payload) {
                callbackLifetime->invoke(
                    [&payload](PalaceCoreImpl& owner) {
                        owner.storageNodeChanged(payload);
                    });
            });
    const bool uploadDoneRegistered =
        modules().storage_module.onStorageUploadDone(
            [callbackLifetime](const std::string& payload) {
                callbackLifetime->invoke(
                    [&payload](PalaceCoreImpl& owner) {
                        owner.storageUploadFinished(payload);
                    });
            });
    const bool downloadDoneRegistered =
        modules().storage_module.onStorageDownloadDoneV2(
            [callbackLifetime](const std::string& payload) {
                callbackLifetime->invoke(
                    [&payload](PalaceCoreImpl& owner) {
                        owner.storageDownloadFinished(payload);
                    });
            });
    m_storageCallbacksRegistered = nodeChangedRegistered
        && uploadDoneRegistered
        && downloadDoneRegistered;
}

void PalaceCoreImpl::enqueueDeliveryEvent(QueuedDeliveryEvent event)
{
    static constexpr std::size_t kMaximumQueuedEvents = 256U;
    static constexpr std::size_t kMaximumEnvelopeBytes = 4096U;
    static constexpr std::size_t kMaximumTopicBytes = 256U;
    static constexpr std::size_t kMaximumRequestIdBytes = 256U;
    static constexpr std::size_t kMaximumConnectionStateBytes = 64U;

    if (!m_roomTransitionHealthy.load(
            std::memory_order_acquire)) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_deliveryMutex);
    if (!m_roomTransitionHealthy.load(
            std::memory_order_acquire)) {
        return;
    }
    event.roomTransitionGeneration =
        m_deliveryNativeCallGate.generation();
    const bool valid = event.payload.size() <= kMaximumEnvelopeBytes
        && event.contentTopic.size() <= kMaximumTopicBytes
        && event.requestId.size() <= kMaximumRequestIdBytes
        && event.connectionStatus.size() <= kMaximumConnectionStateBytes;
    if (!valid) {
        if (event.kind == QueuedDeliveryEventKind::MessageReceived) {
            incrementSaturated(m_deliveryRejectedMessageCount);
            incrementSaturated(m_deliveryRejectionCounts[
                static_cast<std::size_t>(
                    palace::DeliveryRejectionClass::Other)]);
        }
        return;
    }
    if (m_deliveryEvents.size() >= kMaximumQueuedEvents) {
        m_deliveryEventOverflow = true;
        return;
    }
    m_deliveryEvents.push_back(std::move(event));
}

void PalaceCoreImpl::drainDeliveryEvents()
{
    if (!m_roomTransitionHealthy.load(
            std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        m_deliveryEvents.clear();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (!m_roomTransitionHealthy.load(
                std::memory_order_acquire)) {
            m_deliveryEvents.clear();
            return;
        }
        if (m_deliveryDrainingEvents)
            return;
        m_deliveryDrainingEvents = true;
    }

    static constexpr std::size_t kMaximumEventsPerDrain = 1024U;
    std::size_t processed = 0U;
    while (processed < kMaximumEventsPerDrain) {
        QueuedDeliveryEvent event;
        bool hasEvent = false;
        std::vector<palace::DeliveryRecoveryAction> recoveryActions;
        std::uint64_t batchGeneration = 0U;
        palace::PalaceRoomTransitionNativeCallToken
            batchAdmission;
        {
            std::lock_guard<std::mutex> lock(m_deliveryMutex);
            if (!m_roomTransitionHealthy.load(
                    std::memory_order_acquire)) {
                m_deliveryEvents.clear();
                m_deliveryDrainingEvents = false;
                return;
            }
            if (m_deliveryEventOverflow) {
                m_deliveryEventOverflow = false;
                m_deliveryEvents.clear();
                m_deliveryRequestCorrelation.clear();
                m_earlyDeliveryModuleEvents.clear();
                m_deliveryPresenceRefreshAt = 0;
                const palace::DeliveryRecoveryTransition recovery =
                    m_deliveryRecovery.callbackQueueOverflow(
                        m_deliveryNodeRunning
                        || m_deliveryNodeCreated
                        || m_deliveryNodeCreatePending);
                if (recovery.interruptPendingWork && m_deliverySession)
                    m_deliverySession->interrupt(true);
                if (recovery.terminal) {
                    incrementSaturated(m_deliveryRejectedMessageCount);
                    incrementSaturated(m_deliveryRejectionCounts[
                        static_cast<std::size_t>(
                            palace::DeliveryRejectionClass::Other)]);
                }
                if (recovery.interruptPendingWork || recovery.terminal)
                    persistDeliverySessionLocked();
                recoveryActions = recovery.actions;
            }
            if (m_deliveryEvents.empty() && recoveryActions.empty()) {
                m_deliveryDrainingEvents = false;
                return;
            }
            if (!m_deliveryEvents.empty()) {
                event = std::move(m_deliveryEvents.front());
                m_deliveryEvents.pop_front();
                hasEvent = true;
            }
            batchGeneration =
                m_deliveryNativeCallGate.generation();
            if (hasEvent
                && event.roomTransitionGeneration
                    != batchGeneration) {
                hasEvent = false;
                ++processed;
            }
            if (!recoveryActions.empty() || hasEvent) {
                if (!m_deliveryNativeCallGate.admitNativeCall(
                        batchGeneration,
                        true,
                        batchAdmission)) {
                    m_deliveryDrainingEvents = false;
                    return;
                }
            }
        }

        if (recoveryActions.empty() && !hasEvent)
            continue;
        executeDeliveryRecoveryActions(
            recoveryActions, batchGeneration);
        if (!hasEvent) {
            std::lock_guard<std::mutex> lock(m_deliveryMutex);
            m_deliveryNativeCallGate.completeNativeCall(
                batchAdmission,
                m_roomTransitionHealthy.load(
                    std::memory_order_acquire));
            continue;
        }

        ++processed;
        switch (event.kind) {
        case QueuedDeliveryEventKind::NodeChanged:
            break;
        case QueuedDeliveryEventKind::NodeStarted:
            deliveryNodeStarted(event.succeeded);
            break;
        case QueuedDeliveryEventKind::NodeStopped:
            deliveryNodeStopped(event.succeeded);
            break;
        case QueuedDeliveryEventKind::ConnectionChanged:
            deliveryConnectionChanged(event.connectionStatus);
            break;
        case QueuedDeliveryEventKind::MessageReceived:
            deliveryMessageReceived(event.contentTopic, event.payload);
            break;
        case QueuedDeliveryEventKind::MessageSent:
            deliveryModuleEvent(
                event.requestId, PendingDeliveryModuleEvent::Sent);
            break;
        case QueuedDeliveryEventKind::MessagePropagated:
            deliveryModuleEvent(
                event.requestId, PendingDeliveryModuleEvent::Propagated);
            break;
        case QueuedDeliveryEventKind::MessageError:
            deliveryModuleEvent(
                event.requestId, PendingDeliveryModuleEvent::Error);
            break;
        }
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        m_deliveryNativeCallGate.completeNativeCall(
            batchAdmission,
            m_roomTransitionHealthy.load(
                std::memory_order_acquire));
    }

    std::lock_guard<std::mutex> lock(m_deliveryMutex);
    m_deliveryDrainingEvents = false;
}

std::string PalaceCoreImpl::applicationRoundTrip(
    const std::string& payload) const
{
    const palace::PalaceApplicationRoundTripResultV1 result =
        palace::applicationRoundTripV1(payload);
    return result.accepted
        ? result.response
        : "rejected=application-round-trip-" + result.reason;
}

std::string PalaceCoreImpl::enterRoom(const std::string& roomId)
{
    if (roomId != "atrium" && roomId != "lounge")
        return "rejected=unknown-room";
    if (!m_roomTransitionHealthy.load(
            std::memory_order_acquire)) {
        return "rejected=room-transition-recovery";
    }

    drainDeliveryEvents();
    std::vector<palace::DeliverySessionCommand> commands;
    std::uint64_t commandGeneration = 0U;
    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        const bool transitionHealthy =
            m_roomTransitionHealthy.load(
                std::memory_order_acquire);
        if (!transitionHealthy)
            return "rejected=room-transition-recovery";
        if (m_deliverySession
            && m_deliverySession->hasConfiguration()) {
            const palace::DeliverySessionConfigV1& config =
                m_deliverySession->configuration();
            std::string finalizedRoomId;
            std::int64_t finalizedRoomEpoch = -1;
#if defined(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
            if (config.networkId == "logos.test"
                && config.palaceId == "palace-1") {
                finalizedRoomId = roomId;
                finalizedRoomEpoch =
                    m_deliveryAuthority.roomEpoch(finalizedRoomId);
            } else {
#endif
                if (!m_lezAuthorityReady)
                    return "rejected=delivery-finalized-authority-required";
                const ActiveGate3Content content =
                    activeGate3Content(
                        m_lezAuthorityBundle,
                        m_storageMvpBundle,
                        m_storageMvpMode,
                        m_storageMvpFetchedObjects.size());
                if (!content.accepted)
                    return "rejected=delivery-room-" + content.reason;
                const palace::PalaceLezRoomRecordV3& finalizedRoom =
                    roomId == "atrium"
                    ? content.atrium
                    : content.lounge;
                if (finalizedRoom.revision
                    > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                    return "rejected=delivery-room-revision-out-of-range";
                }
                finalizedRoomId =
                    palace::PalaceLezCodec::bytes32Hex(
                        finalizedRoom.roomId);
                finalizedRoomEpoch =
                    static_cast<std::int64_t>(
                        finalizedRoom.revision);
#if defined(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
            }
#endif

            auto stagedSession =
                std::make_unique<
                    palace::PalaceDeliverySession>(
                        *m_deliverySession);
            const palace::DeliverySessionTransition planned =
                stagedSession->switchRoom(
                    finalizedRoomId, finalizedRoomEpoch);
            if (!planned.accepted) {
                return "rejected=delivery-room-switch;"
                    + planned.reason;
            }
            m_roomTransitionParticipantWriteGate
                .blockParticipantWrites();
            auto transitionWrite =
                m_roomTransitionParticipantWriteGate
                    .acquireTransitionWrite();
            if (!transitionWrite) {
                m_roomTransitionHealthy.store(
                    false, std::memory_order_release);
                return "rejected=room-transition-recovery";
            }
            if (palace::palaceRoomTransitionAlreadyLive(
                    planned, m_projection, roomId)) {
                if (!m_roomTransitionParticipantWriteGate
                         .unblockParticipantWrites(
                             transitionWrite)) {
                    m_roomTransitionHealthy.store(
                        false,
                        std::memory_order_release);
                    return "rejected=room-transition-recovery";
                }
                return "ok;room="
                    + m_projection.currentRoomId()
                    + ";title="
                    + m_projection.currentRoomTitle()
                    + ";sync="
                    + palace::syncHealthName(
                        m_projection.syncHealth());
            }
            palace::PalaceProjection stagedProjection =
                m_projection;
            if (!stagedProjection.enterRoom(roomId)
                || !m_deliverySessionStore
                || !m_projectionStore) {
                if (!m_roomTransitionParticipantWriteGate
                         .unblockParticipantWrites(
                             transitionWrite)) {
                    m_roomTransitionHealthy.store(
                        false,
                        std::memory_order_release);
                    return "rejected=room-transition-recovery";
                }
                return "rejected=room-transition-invalid";
            }
            palace::PalaceRoomTransitionFileDurability
                durability(
                    instancePersistencePath(),
                    *m_deliverySessionStore,
                    *m_projectionStore);
            palace::PalaceRoomTransitionCoordinator
                coordinator(durability);
            if (!m_deliveryNativeCallGate.beginTransition(
                    m_roomTransitionHealthy.load(
                        std::memory_order_acquire))) {
                if (!m_roomTransitionParticipantWriteGate
                         .unblockParticipantWrites(
                             transitionWrite)) {
                    m_roomTransitionHealthy.store(
                        false,
                        std::memory_order_release);
                    return "rejected=room-transition-recovery";
                }
                return m_roomTransitionHealthy.load(
                           std::memory_order_acquire)
                    ? "rejected=room-transition-delivery-native-call-in-flight"
                    : "rejected=room-transition-recovery";
            }
            m_roomTransitionHealthy.store(
                false, std::memory_order_release);
            palace::PalaceRoomTransitionResult committed =
                coordinator.transition(
                    roomId,
                    *stagedSession,
                    stagedProjection);
            if (!committed.completed()) {
                m_roomTransitionHealthy.store(
                    false, std::memory_order_release);
                return "rejected=room-transition-"
                    + std::string(
                        palace::palaceRoomTransitionStatusName(
                            committed.status));
            }
            commands = planned.commands;
            m_deliverySession =
                std::move(committed.committedSession);
            m_projection =
                std::move(*committed.committedProjection);
            if (!m_roomTransitionParticipantWriteGate
                     .unblockParticipantWrites(
                         transitionWrite)) {
                m_roomTransitionHealthy.store(
                    false, std::memory_order_release);
                return "rejected=room-transition-recovery";
            }
            m_roomTransitionHealthy.store(
                true, std::memory_order_release);
            commandGeneration =
                m_deliveryNativeCallGate.generation();
            if (planned.reason != "room-unchanged") {
                m_deliveryRequestCorrelation.clear();
                m_earlyDeliveryModuleEvents.clear();
                m_deliveryEvents.clear();
                m_deliveryEventOverflow = false;
                m_deliveryPresenceRefreshAt = 0;
            }
        } else {
            bool projectionSaved = false;
            {
                auto participantWrite =
                    m_roomTransitionParticipantWriteGate
                        .acquireParticipantWrite();
                if (!participantWrite) {
                    return "rejected=room-transition-recovery";
                }
                projectionSaved =
                    m_projectionStore
                    && m_projectionStore->enterRoomDurably(
                        m_projection, roomId);
            }
            if (!projectionSaved) {
                m_projection.setSyncHealth(
                    palace::SyncHealth::Degraded);
                persistProjection();
                return "rejected=room-persistence";
            }
        }
    }

    executeDeliveryCommands(commands, commandGeneration);
    return "ok;room=" + m_projection.currentRoomId()
        + ";title=" + m_projection.currentRoomTitle()
        + ";sync=" + palace::syncHealthName(m_projection.syncHealth());
}

bool PalaceCoreImpl::preparePalaceVmTurn(
    PalaceVmTurn& turn,
    std::string& reason) const
{
    static constexpr std::uint32_t kWriteSharedState =
        1U << 3U;

    turn = {};
    reason.clear();
    if (!m_lezAuthorityReady
        || !m_lezOpenHistory.has_value()
        || !m_lezOpenHistory->authorityApplied
        || m_lezOpenHistory->palaceIdHex
            != m_deliveryAuthority.palaceId()) {
        reason = "finalized-open-authority-required";
        return false;
    }
    if (!m_deliveryIdentity.valid()
        || m_deliveryAuthority.deliveryKeyFor(
               m_deliveryIdentity.accountId(),
               m_deliveryIdentity.deliveryKeyEpoch())
            != m_deliveryIdentity.publicKey()) {
        reason = "finalized-identity-required";
        return false;
    }
    if (m_projection.currentRoomId() != "atrium") {
        reason = "atrium-required";
        return false;
    }

    const ActiveGate3Content content =
        activeGate3Content(
            m_lezAuthorityBundle,
            m_storageMvpBundle,
            m_storageMvpMode,
            m_storageMvpFetchedObjects.size());
    if (!content.accepted) {
        reason = content.reason;
        return false;
    }
    const std::string atriumId =
        palace::PalaceLezCodec::bytes32Hex(
            content.atrium.roomId);
    if (content.atrium.locked || content.lounge.locked
        || m_deliveryAuthority.isRoomLocked(atriumId)
        || m_deliveryAuthority.isRoomLocked(
            palace::PalaceLezCodec::bytes32Hex(
                content.lounge.roomId))) {
        reason = "atrium-door-locked";
        return false;
    }
    if (m_lezAuthorityBundle.checkpoint
            .lastOrderedActionId
        == std::numeric_limits<std::uint64_t>::max()) {
        reason = "ordered-action-id-exhausted";
        return false;
    }
    const std::uint64_t nextAction =
        m_lezAuthorityBundle.checkpoint
            .lastOrderedActionId
        + 1U;

    const NamedLezRecord* sharedRecord = nullptr;
    const palace::PalaceLezRoomSharedStateRecordV3*
        shared = nullptr;
    std::size_t sharedMatches = 0U;
    for (const NamedLezRecord& named : content.records) {
        const auto* candidate =
            std::get_if<
                palace::PalaceLezRoomSharedStateRecordV3>(
                &named.account.record);
        if (candidate != nullptr
            && candidate->roomId == content.atrium.roomId) {
            ++sharedMatches;
            sharedRecord = &named;
            shared = candidate;
        }
    }
    const std::string priorValue =
        shared == nullptr
        ? std::string{}
        : std::string(
              shared->value.begin(), shared->value.end());
    const std::string priorState =
        shared == nullptr
        ? std::string{}
        : shared->key + "=" + priorValue;
    if (sharedMatches != 1U || sharedRecord == nullptr
        || shared == nullptr
        || shared->key != "door_open"
        || priorValue != "0"
        || palace::PalaceLezCodec::bytes32Hex(
               shared->stateRoot)
            != palace::crypto::sha256Hex(priorState)
        || shared->revision
            == std::numeric_limits<std::uint64_t>::max()
        || shared->lastOrderedActionId
            > m_lezAuthorityBundle.checkpoint
                  .lastOrderedActionId) {
        reason = "gate3-shared-state-mismatch";
        return false;
    }

    const NamedLezRecord* grantRecord = nullptr;
    const palace::PalaceLezCapabilityGrantRecordV3*
        grant = nullptr;
    std::size_t grantMatches = 0U;
    palace::PalaceLezBytes32 caller{};
    if (!palace::PalaceLezCodec::parseBytes32Hex(
            m_deliveryIdentity.accountId(), caller)) {
        reason = "identity-account-invalid";
        return false;
    }
    for (const NamedLezRecord& named : content.records) {
        const auto* candidate =
            std::get_if<
                palace::PalaceLezCapabilityGrantRecordV3>(
                &named.account.record);
        if (candidate == nullptr
            || candidate->subjectUserId != caller
            || candidate->revoked
            || candidate->validThroughActionId < nextAction
            || (candidate->capabilities
                    & kWriteSharedState)
                != kWriteSharedState) {
            continue;
        }
        const bool scopeAccepted =
            candidate->scope.kind
                == palace::PalaceLezScopeKindV3::Palace
            || (candidate->scope.kind
                    == palace::PalaceLezScopeKindV3::Room
                && candidate->scope.roomId
                    == content.atrium.roomId);
        if (!scopeAccepted)
            continue;
        ++grantMatches;
        grantRecord = &named;
        grant = candidate;
    }
    if (grantMatches != 1U || grantRecord == nullptr
        || grant == nullptr) {
        reason = "write-shared-state-capability-required";
        return false;
    }

    turn.phase = "prepared";
    turn.reason = "prepared";
    turn.actionId = std::to_string(nextAction);
    turn.palaceIdHex =
        palace::PalaceLezCodec::bytes32Hex(
            content.root.palaceId);
    turn.rootAccountIdHex =
        m_lezAuthorityBundle.scope.rootAccountIdHex;
    turn.callerAccountIdHex =
        m_deliveryIdentity.accountId();
    turn.programIdHex =
        m_lezAuthorityBundle.scope.programIdHex;
    turn.grantIdHex =
        palace::PalaceLezCodec::bytes32Hex(
            grant->grantId);
    turn.sharedStateIdHex =
        palace::PalaceLezCodec::bytes32Hex(
            shared->sharedStateId);
    turn.roomIdHex = atriumId;
    turn.script = content.script;
    turn.scriptBundleCid = content.scriptCid;
    turn.roomEpoch =
        std::to_string(content.atrium.revision);
    turn.trigger = "SELECT:door";
    turn.priorState = priorState;
    turn.allowedRooms = "atrium,lounge";
    turn.resultingState = "door_open=1";
    turn.expectedStateRootHex =
        palace::crypto::sha256Hex(turn.resultingState);
    turn.navigateRoom = "lounge";
    turn.stateRevision = shared->revision + 1U;
    turn.instructionBudget = 8U;
    turn.roomLocked = false;
    turn.canMutateSharedState = true;
    if (!validPalaceVmTurn(turn)) {
        reason = "invalid-gate5-turn";
        return false;
    }
    reason = "prepared";
    return true;
}

bool PalaceCoreImpl::recoverFinalizedPalaceVmTurn(
    std::string& reason)
{
    reason.clear();
    if (m_palaceVmTurn.has_value()
        && !isColdVmReplayReason(
            m_palaceVmTurn->reason)) {
        reason = "vm-turn-already-tracked";
        return true;
    }
    if (!m_palaceVmTurn.has_value()
        && m_palaceVmStoreState != "missing") {
        reason = "vm-turn-store-not-missing";
        return false;
    }

    const ActiveGate3Content content =
        activeGate3Content(
            m_lezAuthorityBundle,
            m_storageMvpBundle,
            m_storageMvpMode,
            m_storageMvpFetchedObjects.size());
    const palace::ActionStatus action =
        m_actionJournal.status("10");
    const std::optional<
        palace::PalaceLezTrackedTransaction> tracked =
        action.transactionHash.empty()
        ? std::nullopt
        : trackedLezTransaction(action.transactionHash);
    const auto* update =
        tracked.has_value()
        ? std::get_if<
              palace::PalaceLezUpdateSharedStateV3>(
              &tracked->plan.instruction.payload)
        : nullptr;

    const palace::PalaceLezRoomSharedStateRecordV3*
        shared = nullptr;
    const palace::PalaceLezCapabilityGrantRecordV3*
        grant = nullptr;
    std::size_t sharedMatches = 0U;
    std::size_t grantMatches = 0U;
    if (update != nullptr && content.accepted) {
        for (const NamedLezRecord& named : content.records) {
            if (const auto* candidate =
                    std::get_if<
                        palace::
                            PalaceLezRoomSharedStateRecordV3>(
                        &named.account.record);
                candidate != nullptr
                && candidate->sharedStateId
                    == update->sharedStateId) {
                ++sharedMatches;
                shared = candidate;
                continue;
            }
            if (const auto* candidate =
                    std::get_if<
                        palace::
                            PalaceLezCapabilityGrantRecordV3>(
                        &named.account.record);
                candidate != nullptr
                && candidate->grantId == update->grantId) {
                ++grantMatches;
                grant = candidate;
            }
        }
    }

    const bool identityVerified =
        m_deliveryIdentity.valid()
        && m_deliveryAuthority.deliveryKeyFor(
               m_deliveryIdentity.accountId(),
               m_deliveryIdentity.deliveryKeyEpoch())
            == m_deliveryIdentity.publicKey();
    const bool authorityVerified =
        m_lezReady && m_lezCoordinator.running()
        && m_lezCoordinatorStoreHealthy
        && m_lezAuthorityReady
        && m_lezOpenHistory.has_value()
        && m_lezOpenHistory->authorityApplied
        && m_lezOpenHistory->palaceIdHex
            == m_deliveryAuthority.palaceId()
        && content.accepted;

    palace::PalaceVmFinalizedReplayInputV1 input;
    input.authorityVerified = authorityVerified;
    input.identityVerified = identityVerified;
    input.actionId = "10";
    input.authorityCheckpointActionId =
        m_lezAuthorityBundle.checkpoint
            .lastOrderedActionId;
    input.callerAccountIdHex =
        m_deliveryIdentity.accountId();
    input.journalStatus = action;
    input.priorSharedJournalStatus =
        m_actionJournal.status("7");
    input.trackedTransactions =
        m_lezCoordinator.transactions();
    if (content.accepted) {
        input.content = {
            true,
            palace::PalaceLezCodec::bytes32Hex(
                content.root.palaceId),
            palace::PalaceLezCodec::bytes32Hex(
                content.root.owner),
            m_lezAuthorityBundle.scope.programIdHex,
            m_lezAuthorityBundle.scope.rootAccountIdHex,
            content.rootDataSha256Hex,
            content.script,
            content.scriptCid,
            palace::PalaceLezCodec::bytes32Hex(
                content.atrium.roomId),
            palace::PalaceLezCodec::bytes32Hex(
                content.lounge.roomId),
            std::to_string(content.atrium.revision),
            content.atrium.locked,
            content.lounge.locked,
        };
    }
    if (sharedMatches == 1U && shared != nullptr) {
        input.shared = {
            true,
            palace::PalaceLezCodec::bytes32Hex(
                shared->palaceId),
            palace::PalaceLezCodec::bytes32Hex(
                shared->sharedStateId),
            palace::PalaceLezCodec::bytes32Hex(
                shared->roomId),
            shared->key,
            std::string(
                shared->value.begin(),
                shared->value.end()),
            palace::PalaceLezCodec::bytes32Hex(
                shared->stateRoot),
            shared->revision,
            shared->lastOrderedActionId,
        };
    }
    if (grantMatches == 1U && grant != nullptr) {
        const bool scopeAllowsAtrium =
            grant->scope.kind
                == palace::PalaceLezScopeKindV3::Palace
            || (grant->scope.kind
                    == palace::PalaceLezScopeKindV3::Room
                && grant->scope.roomId
                    == content.atrium.roomId);
        input.grant = {
            true,
            palace::PalaceLezCodec::bytes32Hex(
                grant->palaceId),
            palace::PalaceLezCodec::bytes32Hex(
                grant->grantId),
            palace::PalaceLezCodec::bytes32Hex(
                grant->subjectUserId),
            palace::PalaceLezCodec::bytes32Hex(
                grant->issuedBy),
            scopeAllowsAtrium,
            grant->capabilities,
            grant->validThroughActionId,
            grant->revoked,
        };
    }

    const palace::PalaceVmFinalizedReplayPlanV1 plan =
        palace::buildPalaceVmFinalizedReplayPlanV1(
            input);
    if (!plan.accepted) {
        reason = plan.reason;
        return false;
    }

    const auto replayPlanFromTurn =
        [](const PalaceVmTurn& turn) {
            palace::PalaceVmFinalizedReplayPlanV1 result;
            result.accepted = true;
            result.reason = "accepted";
            result.actionId = turn.actionId;
            result.palaceIdHex = turn.palaceIdHex;
            result.rootAccountIdHex =
                turn.rootAccountIdHex;
            result.callerAccountIdHex =
                turn.callerAccountIdHex;
            result.programIdHex = turn.programIdHex;
            result.grantIdHex = turn.grantIdHex;
            result.sharedStateIdHex =
                turn.sharedStateIdHex;
            result.roomIdHex = turn.roomIdHex;
            result.script = turn.script;
            result.scriptBundleCid =
                turn.scriptBundleCid;
            result.roomEpoch = turn.roomEpoch;
            result.trigger = turn.trigger;
            result.priorState = turn.priorState;
            result.allowedRooms = turn.allowedRooms;
            result.resultingState =
                turn.resultingState;
            result.expectedStateRootHex =
                turn.expectedStateRootHex;
            result.navigateRoom = turn.navigateRoom;
            result.stateRevision = turn.stateRevision;
            result.instructionBudget =
                turn.instructionBudget;
            result.roomLocked = turn.roomLocked;
            result.canMutateSharedState =
                turn.canMutateSharedState;
            return result;
        };

    if (m_palaceVmTurn.has_value()) {
        if (!palace::samePalaceVmFinalizedReplayPlanV1(
                replayPlanFromTurn(*m_palaceVmTurn),
                plan)) {
            reason = "cold-replay-turn-conflict";
            return false;
        }
    } else {
        PalaceVmTurn recovered;
        recovered.phase = "prepared";
        recovered.reason = "cold-replay-prepared";
        recovered.actionId = plan.actionId;
        recovered.palaceIdHex = plan.palaceIdHex;
        recovered.rootAccountIdHex =
            plan.rootAccountIdHex;
        recovered.callerAccountIdHex =
            plan.callerAccountIdHex;
        recovered.programIdHex = plan.programIdHex;
        recovered.grantIdHex = plan.grantIdHex;
        recovered.sharedStateIdHex =
            plan.sharedStateIdHex;
        recovered.roomIdHex = plan.roomIdHex;
        recovered.script = plan.script;
        recovered.scriptBundleCid =
            plan.scriptBundleCid;
        recovered.roomEpoch = plan.roomEpoch;
        recovered.trigger = plan.trigger;
        recovered.priorState = plan.priorState;
        recovered.allowedRooms = plan.allowedRooms;
        recovered.resultingState =
            plan.resultingState;
        recovered.expectedStateRootHex =
            plan.expectedStateRootHex;
        recovered.navigateRoom = plan.navigateRoom;
        recovered.stateRevision = plan.stateRevision;
        recovered.instructionBudget =
            plan.instructionBudget;
        recovered.roomLocked = plan.roomLocked;
        recovered.canMutateSharedState =
            plan.canMutateSharedState;
        if (!savePalaceVmTurn(recovered)) {
            reason = "cold-replay-prepare-save-failed";
            return false;
        }
        m_palaceVmTurn = std::move(recovered);
        m_palaceVmStoreState = "saved";
    }

    if (m_palaceVmTurn->phase == "degraded") {
        reason = "cold-replay-degraded";
        return false;
    }
    if (m_palaceVmTurn->phase == "prepared"
        && !executePreparedPalaceVmTurn(reason)) {
        return false;
    }
    if (m_palaceVmTurn->phase == "provisional") {
        PalaceVmTurn submitted = *m_palaceVmTurn;
        submitted.phase = "submitted";
        submitted.reason = "cold-replay-submitted";
        if (!savePalaceVmTurn(submitted)) {
            reason = "cold-replay-submit-state-save-failed";
            return false;
        }
        m_palaceVmTurn = std::move(submitted);
    }
    if (!promoteFinalizedPalaceVmTurn("10", reason))
        return false;
    if (!m_palaceVmTurn.has_value()
        || m_palaceVmTurn->phase != "promoted"
        || !m_palaceVmTurn->navigationApplied
        || m_projection.currentRoomId() != "lounge") {
        reason = "cold-replay-projection-mismatch";
        return false;
    }
    reason = "cold-replay-promoted";
    return true;
}

bool PalaceCoreImpl::ensurePalaceVmActionQueued(
    const PalaceVmTurn& turn,
    std::string& reason)
{
    reason.clear();
    const palace::ActionStatus current =
        m_actionJournal.status(turn.actionId);
    if (current.durableStage
            == palace::DurableActionStage::Queued
        || current.durableStage
            == palace::DurableActionStage::SubmittedToLez
        || current.durableStage
            == palace::DurableActionStage::Observed
        || current.durableStage
            == palace::DurableActionStage::Finalized) {
        reason = "queued";
        return true;
    }
    if (current.durableStage
            != palace::DurableActionStage::LocalDraft
        || !current.transactionHash.empty()) {
        reason = "action-terminal";
        return false;
    }

    palace::ActionJournal candidate = m_actionJournal;
    candidate.createDraft(turn.actionId);
    if (!candidate.queue(turn.actionId)
        || !m_actionJournalStore
        || !m_actionJournalStore->save(candidate)) {
        reason = "action-queue-save-failed";
        return false;
    }
    m_actionJournal = std::move(candidate);
    reason = "queued";
    return true;
}

bool PalaceCoreImpl::executePreparedPalaceVmTurn(
    std::string& reason)
{
    reason.clear();
    if (!m_palaceVmTurn.has_value()
        || m_palaceVmTurn->phase != "prepared") {
        reason = "vm-turn-not-prepared";
        return false;
    }
    PalaceVmTurn candidate = *m_palaceVmTurn;
    const bool coldReplay =
        isColdVmReplayReason(candidate.reason);
    const std::string receipt =
        modules().palace_vm.executeProvisionalTurn(
            candidate.actionId,
            candidate.script,
            candidate.scriptBundleCid,
            candidate.roomEpoch,
            candidate.trigger,
            candidate.priorState,
            candidate.allowedRooms,
            candidate.roomLocked,
            candidate.canMutateSharedState,
            static_cast<std::int64_t>(
                candidate.instructionBudget));
    const std::string expected =
        expectedDoorVmReceipt(
            candidate.scriptBundleCid,
            candidate.roomEpoch,
            candidate.trigger,
            candidate.resultingState,
            candidate.expectedStateRootHex,
            false);
    if (receipt != expected) {
        candidate.phase = "degraded";
        candidate.reason = "vm-provisional-mismatch";
        if (savePalaceVmTurn(candidate))
            m_palaceVmTurn = std::move(candidate);
        reason = "vm-provisional-mismatch";
        return false;
    }
    candidate.provisionalReceipt = receipt;
    candidate.phase = "provisional";
    candidate.reason =
        coldReplay
        ? "cold-replay-provisional"
        : "awaiting-lez-submit";
    if (!savePalaceVmTurn(candidate)) {
        reason = "vm-turn-save-failed";
        return false;
    }
    m_palaceVmTurn = std::move(candidate);
    m_palaceVmStoreState = "saved";
    reason = "provisional";
    return true;
}

bool PalaceCoreImpl::submitPalaceVmTurn(
    std::string& reason)
{
    reason.clear();
    if (!m_palaceVmTurn.has_value()
        || (m_palaceVmTurn->phase != "provisional"
            && m_palaceVmTurn->phase != "submitted")) {
        reason = "vm-turn-not-provisional";
        return false;
    }
    PalaceVmTurn candidate = *m_palaceVmTurn;
    const palace::ActionStatus current =
        m_actionJournal.status(candidate.actionId);
    if (current.durableStage
        == palace::DurableActionStage::Queued) {
        QJsonObject transition;
        transition.insert("kind", "update_shared_state");
        transition.insert(
            "grant_id_hex",
            QString::fromStdString(candidate.grantIdHex));
        transition.insert(
            "shared_state_id_hex",
            QString::fromStdString(
                candidate.sharedStateIdHex));
        transition.insert(
            "room_id_hex",
            QString::fromStdString(candidate.roomIdHex));
        transition.insert(
            "state_revision",
            QString::fromStdString(
                std::to_string(candidate.stateRevision)));
        transition.insert(
            "value_hex",
            QString::fromStdString(hexBytes("1")));
        transition.insert(
            "state_root_hex",
            QString::fromStdString(
                candidate.expectedStateRootHex));
        const std::string submitted =
            submitPalaceTransition(
                candidate.actionId,
                candidate.rootAccountIdHex,
                candidate.callerAccountIdHex,
                candidate.programIdHex,
                QJsonDocument(transition)
                    .toJson(QJsonDocument::Compact)
                    .toStdString());
        if (submitted.rfind("ok;", 0U) != 0U) {
            candidate.reason = "lez-submit-pending";
            if (savePalaceVmTurn(candidate))
                m_palaceVmTurn = std::move(candidate);
            reason = "lez-submit-pending";
            return false;
        }
    }

    const palace::ActionStatus after =
        m_actionJournal.status(candidate.actionId);
    if (after.durableStage
            != palace::DurableActionStage::SubmittedToLez
        && after.durableStage
            != palace::DurableActionStage::Observed
        && after.durableStage
            != palace::DurableActionStage::Finalized) {
        reason = "lez-submit-stage-mismatch";
        return false;
    }
    candidate.phase = "submitted";
    candidate.reason =
        after.durableStage
                == palace::DurableActionStage::Finalized
            ? "awaiting-vm-promotion"
            : "awaiting-stable-observation";
    if (!savePalaceVmTurn(candidate)) {
        reason = "vm-turn-save-failed";
        return false;
    }
    m_palaceVmTurn = std::move(candidate);
    reason = "submitted";
    return true;
}

std::string PalaceCoreImpl::previewSpot(
    const std::string& spotId)
{
    if (spotId != "door")
        return "rejected=unknown-spot";
    if (!isContextReady())
        return "rejected=vm-not-ready";

    PalaceVmTurn preview;
    std::string reason;
    if (!preparePalaceVmTurn(preview, reason))
        return "rejected=spot-" + reason;

    const std::string receipt =
        modules().palace_vm.executeTurn(
            preview.script,
            preview.scriptBundleCid,
            preview.roomEpoch,
            preview.trigger,
            preview.priorState,
            preview.allowedRooms,
            preview.roomLocked,
            preview.canMutateSharedState,
            static_cast<std::int64_t>(
                preview.instructionBudget));
    const std::string expected =
        expectedDoorVmReceipt(
            preview.scriptBundleCid,
            preview.roomEpoch,
            preview.trigger,
            preview.resultingState,
            preview.expectedStateRootHex,
            false);
    if (receipt != expected)
        return "rejected=spot-vm-preview-mismatch";

    return "ok;spot=door;action=" + preview.actionId
        + ";state_root=" + preview.expectedStateRootHex
        + ";receipt=" + base64Url(receipt)
        + ";receipt_sha256="
        + palace::crypto::sha256Hex(receipt)
        + ";script_cid=" + preview.scriptBundleCid
        + ";room_epoch=" + preview.roomEpoch
        + ";navigation=0";
}

std::string PalaceCoreImpl::useSpot(
    const std::string& spotId)
{
    if (spotId != "door")
        return "rejected=unknown-spot";
    if (!isContextReady())
        return "rejected=vm-not-ready";
    if (m_palaceVmTurn.has_value()) {
        if (m_palaceVmTurn->phase == "degraded") {
            return "rejected=spot-degraded;"
                + spotStatus();
        }
        return reconcileSpot();
    }

    PalaceVmTurn prepared;
    std::string reason;
    if (!preparePalaceVmTurn(prepared, reason))
        return "rejected=spot-" + reason;
    if (!savePalaceVmTurn(prepared))
        return "rejected=spot-vm-turn-save-failed";
    m_palaceVmTurn = std::move(prepared);
    m_palaceVmStoreState = "saved";
    if (!ensurePalaceVmActionQueued(
            *m_palaceVmTurn, reason)) {
        return "rejected=spot-" + reason
            + ";action=" + m_palaceVmTurn->actionId;
    }
    if (!executePreparedPalaceVmTurn(reason)) {
        return "rejected=spot-" + reason
            + ";action=" + m_palaceVmTurn->actionId;
    }
    if (!submitPalaceVmTurn(reason)) {
        return "rejected=spot-" + reason
            + ";action=" + m_palaceVmTurn->actionId;
    }
    return "ok;action=" + m_palaceVmTurn->actionId
        + ";" + spotStatus();
}

bool PalaceCoreImpl::promoteFinalizedPalaceVmTurn(
    const std::string& actionId,
    std::string& reason)
{
    reason.clear();
    if (!m_palaceVmTurn.has_value()
        || m_palaceVmTurn->actionId != actionId
        || (m_palaceVmTurn->phase != "submitted"
            && m_palaceVmTurn->phase != "finalized"
            && m_palaceVmTurn->phase != "promoted")) {
        reason = "vm-turn-not-promotable";
        return false;
    }
    PalaceVmTurn candidate = *m_palaceVmTurn;
    const bool coldReplay =
        isColdVmReplayReason(candidate.reason);
    const palace::ActionStatus action =
        m_actionJournal.status(actionId);
    const auto tracked =
        action.transactionHash.empty()
        ? std::optional<
              palace::PalaceLezTrackedTransaction>{}
        : trackedLezTransaction(action.transactionHash);
    const auto* update =
        tracked.has_value()
        ? std::get_if<
              palace::PalaceLezUpdateSharedStateV3>(
              &tracked->plan.instruction.payload)
        : nullptr;
    palace::PalaceLezBytes32 expectedGrant{};
    palace::PalaceLezBytes32 expectedShared{};
    palace::PalaceLezBytes32 expectedRoom{};
    std::uint64_t expectedAction = 0U;
    if (action.durableStage
            != palace::DurableActionStage::Finalized
        || !tracked.has_value()
        || tracked->stage
            != palace::PalaceLezTransactionStage::Finalized
        || update == nullptr
        || !palace::PalaceLezCodec::parseOrderedActionId(
            actionId, expectedAction)
        || !palace::PalaceLezCodec::parseBytes32Hex(
            candidate.grantIdHex, expectedGrant)
        || !palace::PalaceLezCodec::parseBytes32Hex(
            candidate.sharedStateIdHex, expectedShared)
        || !palace::PalaceLezCodec::parseBytes32Hex(
            candidate.roomIdHex, expectedRoom)
        || update->orderedActionId != expectedAction
        || update->grantId != expectedGrant
        || update->sharedStateId != expectedShared
        || update->roomId != expectedRoom
        || update->stateRevision
            != candidate.stateRevision
        || std::string(
               update->value.begin(),
               update->value.end())
            != "1"
        || palace::PalaceLezCodec::bytes32Hex(
               update->stateRoot)
            != candidate.expectedStateRootHex
        || tracked->plan.programIdHex
            != candidate.programIdHex
        || tracked->plan.rootAccountIdHex
            != candidate.rootAccountIdHex
        || tracked->plan.accountIdsHex.size() < 2U
        || tracked->plan.accountIdsHex[1]
            != candidate.callerAccountIdHex
        || !m_lezAuthorityReady
        || m_lezAuthorityBundle.checkpoint
               .lastOrderedActionId
            != expectedAction) {
        reason = "finalized-vm-action-mismatch";
        return false;
    }

    const std::string sharedAccountId =
        palace::PalaceLezCodec::deriveRecordPda(
            candidate.programIdHex,
            "shared",
            candidate.rootAccountIdHex,
            expectedShared);
    const auto storedShared = std::find_if(
        m_lezAuthorityBundle.accounts.begin(),
        m_lezAuthorityBundle.accounts.end(),
        [&sharedAccountId](const auto& stored) {
            return stored.accountIdHex == sharedAccountId;
        });
    palace::PalaceLezPublicAccountV3 decodedShared;
    if (storedShared
        != m_lezAuthorityBundle.accounts.end()) {
        decodedShared =
            palace::PalaceLezCodec::decodePublicAccount(
                storedShared->responseJson,
                candidate.programIdHex);
    }
    const auto* shared =
        decodedShared.accepted
        ? std::get_if<
              palace::PalaceLezRoomSharedStateRecordV3>(
              &decodedShared.record)
        : nullptr;
    if (shared == nullptr
        || shared->sharedStateId != expectedShared
        || shared->roomId != expectedRoom
        || shared->revision != candidate.stateRevision
        || shared->lastOrderedActionId != expectedAction
        || std::string(
               shared->value.begin(), shared->value.end())
            != "1"
        || palace::PalaceLezCodec::bytes32Hex(
               shared->stateRoot)
            != candidate.expectedStateRootHex) {
        reason = "finalized-shared-state-mismatch";
        return false;
    }

    if (candidate.phase == "promoted") {
        if (candidate.finalizedReceipt
            != expectedDoorVmReceipt(
                candidate.scriptBundleCid,
                candidate.roomEpoch,
                candidate.trigger,
                candidate.resultingState,
                candidate.expectedStateRootHex,
                true)) {
            reason = "persisted-vm-receipt-mismatch";
            return false;
        }
        const palace::PalaceVmFinalizedNavigationResultV1
            navigation =
                palace::
                    coordinatePalaceVmFinalizedNavigationV1(
                        candidate.navigationApplied,
                        m_projection.currentRoomId(),
                        candidate.navigateRoom,
                        coldReplay,
                        [this](const std::string& roomId) {
                            return enterRoom(roomId);
                        });
        if (!navigation.accepted) {
            reason = navigation.reason;
            return false;
        }
        if (!navigation.roomEntryAttempted) {
            reason = navigation.reason;
            return true;
        }
        if (m_projection.currentRoomId()
            != candidate.navigateRoom) {
            reason = "finalized-navigation-failed";
            return false;
        }
        candidate.navigationApplied = true;
        candidate.reason =
            coldReplay
            ? "cold-replay-navigated"
            : "navigated";
        if (!savePalaceVmTurn(candidate)) {
            reason = "navigation-status-save-failed";
            return false;
        }
        m_palaceVmTurn = std::move(candidate);
        reason = "promoted";
        return true;
    }

    const std::string expectedFinalized =
        expectedDoorVmReceipt(
            candidate.scriptBundleCid,
            candidate.roomEpoch,
            candidate.trigger,
            candidate.resultingState,
            candidate.expectedStateRootHex,
            true);
    const std::string moduleStatus =
        modules().palace_vm.finalityStatus(actionId);
    if (moduleStatus.rfind("status=promoted;", 0U)
        == 0U) {
        const palace::core_detail::
            PalaceVmPromotionRecoveryResultV1 recovered =
            palace::core_detail::
                recoverPromotedPalaceVmReceiptV1(
                    moduleStatus,
                    {
                        candidate.actionId,
                        candidate.script,
                        candidate.scriptBundleCid,
                        candidate.roomEpoch,
                        candidate.trigger,
                        candidate.priorState,
                        candidate.allowedRooms,
                        candidate.provisionalReceipt,
                        expectedFinalized,
                        candidate.instructionBudget,
                        candidate.roomLocked,
                        candidate.canMutateSharedState,
                    });
        if (!recovered.accepted) {
            candidate.phase = "degraded";
            candidate.reason = recovered.reason;
            if (savePalaceVmTurn(candidate))
                m_palaceVmTurn = std::move(candidate);
            reason = recovered.reason;
            return false;
        }
        candidate.finalizedReceipt =
            recovered.finalizedReceipt;
        candidate.phase = "promoted";
        candidate.reason =
            coldReplay
            ? "cold-replay-promotion-recovered"
            : recovered.reason;
        candidate.navigationApplied = false;
        if (!savePalaceVmTurn(candidate)) {
            reason = "vm-promotion-recovery-save-failed";
            return false;
        }
        m_palaceVmTurn = candidate;
        return promoteFinalizedPalaceVmTurn(
            actionId, reason);
    }
    if (moduleStatus.rfind("status=pending;", 0U)
        != 0U) {
        reason = "vm-provisional-state-missing";
        return false;
    }

    candidate.phase = "finalized";
    candidate.reason =
        coldReplay
        ? "cold-replay-finalized"
        : "finality-confirmed";
    if (!savePalaceVmTurn(candidate)) {
        reason = "vm-finality-state-save-failed";
        return false;
    }
    m_palaceVmTurn = candidate;

    const std::string finalizedReceipt =
        modules().palace_vm.promoteFinalizedTurn(
            candidate.actionId,
            candidate.provisionalReceipt,
            candidate.script,
            candidate.scriptBundleCid,
            candidate.roomEpoch,
            candidate.trigger,
            candidate.priorState,
            candidate.allowedRooms,
            candidate.roomLocked,
            candidate.canMutateSharedState,
            static_cast<std::int64_t>(
                candidate.instructionBudget));
    if (finalizedReceipt != expectedFinalized) {
        candidate.phase = "degraded";
        candidate.reason = "vm-promotion-mismatch";
        if (savePalaceVmTurn(candidate))
            m_palaceVmTurn = std::move(candidate);
        reason = "vm-promotion-mismatch";
        return false;
    }

    candidate.finalizedReceipt = finalizedReceipt;
    candidate.phase = "promoted";
    candidate.reason =
        coldReplay
        ? "cold-replay-promoted"
        : "promoted";
    candidate.navigationApplied = false;
    if (!savePalaceVmTurn(candidate)) {
        reason = "vm-promotion-state-save-failed";
        return false;
    }
    m_palaceVmTurn = candidate;
    return promoteFinalizedPalaceVmTurn(actionId, reason);
}

std::string PalaceCoreImpl::reconcileSpot()
{
    if (!m_palaceVmTurn.has_value())
        return "rejected=spot-no-tracked-turn";
    if (m_palaceVmTurn->phase == "degraded")
        return "rejected=spot-degraded;" + spotStatus();

    std::string reason;
    if (!ensurePalaceVmActionQueued(
            *m_palaceVmTurn, reason)) {
        return "rejected=spot-" + reason
            + ";" + spotStatus();
    }
    if (m_palaceVmTurn->phase == "prepared"
        && !executePreparedPalaceVmTurn(reason)) {
        return "rejected=spot-" + reason
            + ";" + spotStatus();
    }
    if (m_palaceVmTurn->phase == "provisional"
        && !submitPalaceVmTurn(reason)) {
        return "ok;action=" + m_palaceVmTurn->actionId
            + ";" + spotStatus();
    }

    palace::ActionStatus action =
        m_actionJournal.status(
            m_palaceVmTurn->actionId);
    if (action.durableStage
        == palace::DurableActionStage::SubmittedToLez) {
        const std::string observed =
            observePalaceTransition(
                m_palaceVmTurn->actionId);
        m_palaceVmTurn->reason =
            observed.rfind("ok;", 0U) == 0U
            ? "awaiting-explorer-finality"
            : "awaiting-stable-observation";
    } else if (action.durableStage
               == palace::DurableActionStage::Observed) {
        const std::string reconciled =
            reconcilePalaceTransition(
                m_palaceVmTurn->actionId);
        m_palaceVmTurn->reason =
            reconciled.rfind("ok;", 0U) == 0U
            ? "awaiting-explorer-finality"
            : "reconciliation-required";
    }

    action = m_actionJournal.status(
        m_palaceVmTurn->actionId);
    if (action.durableStage
        == palace::DurableActionStage::Finalized) {
        if (!promoteFinalizedPalaceVmTurn(
                m_palaceVmTurn->actionId, reason)) {
            return "ok;action="
                + m_palaceVmTurn->actionId
                + ";" + spotStatus();
        }
    } else if (action.durableStage
                   == palace::DurableActionStage::Rejected
               || action.durableStage
                   == palace::DurableActionStage::Expired
               || action.durableStage
                   == palace::DurableActionStage::Orphaned) {
        PalaceVmTurn candidate = *m_palaceVmTurn;
        candidate.phase = "degraded";
        candidate.reason = "lez-action-terminal";
        if (savePalaceVmTurn(candidate))
            m_palaceVmTurn = std::move(candidate);
        return "rejected=spot-lez-action-terminal;"
            + spotStatus();
    }
    return "ok;action=" + m_palaceVmTurn->actionId
        + ";" + spotStatus();
}

std::string PalaceCoreImpl::spotStatus()
{
    if (!m_palaceVmTurn.has_value()
        || isColdVmReplayReason(
            m_palaceVmTurn->reason)) {
        std::string recoveryReason;
        recoverFinalizedPalaceVmTurn(
            recoveryReason);
    }
    if (!m_palaceVmTurn.has_value()) {
        return "spot=door;vm=idle;action=none;"
            "navigation=0;state_root=none;reason=none;"
            "vm_store=" + m_palaceVmStoreState;
    }
    const PalaceVmTurn& turn = *m_palaceVmTurn;
    const palace::ActionStatus action =
        m_actionJournal.status(turn.actionId);
    const std::string moduleStatus =
        isContextReady()
        ? modules().palace_vm.finalityStatus(
              turn.actionId)
        : std::string("status=unavailable");
    return "spot=door;vm=" + turn.phase
        + ";action=" + turn.actionId
        + ";"
        + palace::canonicalActionStatus(action)
        + ";navigation="
        + (turn.navigationApplied ? "1" : "0")
        + ";state_root=" + turn.expectedStateRootHex
        + ";reason=" + turn.reason
        + ";vm_store=" + m_palaceVmStoreState
        + ";vm_evidence=" + base64Url(moduleStatus)
        + ";provisional_receipt_sha256="
        + (turn.provisionalReceipt.empty()
               ? std::string("none")
               : palace::crypto::sha256Hex(
                     turn.provisionalReceipt))
        + ";finalized_receipt_sha256="
        + (turn.finalizedReceipt.empty()
               ? std::string("none")
               : palace::crypto::sha256Hex(
                     turn.finalizedReceipt));
}

std::string PalaceCoreImpl::vmTurnMetrics(
    const std::string& actionId,
    const std::string& phase)
{
    if (!isContextReady()) {
        return "status=unavailable;action=" + actionId
            + ";phase=" + phase + ";reason=core-not-ready";
    }
    return modules().palace_vm.vmTurnMetrics(actionId, phase);
}

bool PalaceCoreImpl::syncLezWalletToCurrent(std::string& reason)
{
    static constexpr std::int64_t kMaximumSyncChunk = 100;
    static constexpr int kWalletFfiSuccess = 0;

    reason.clear();
    logos::CallError callError;
    const std::int64_t current =
        modules().lez_core.get_current_block_height(&callError);
    if (!callError.ok() || current < 0
        || current
            > static_cast<std::int64_t>(
                std::numeric_limits<int>::max())) {
        m_lezSyncState = "current-height-failed";
        reason = m_lezSyncState;
        return false;
    }
    std::int64_t synced =
        modules().lez_core.get_last_synced_block(&callError);
    if (!callError.ok() || synced < 0) {
        m_lezSyncState = "last-synced-height-failed";
        reason = m_lezSyncState;
        return false;
    }
    if (synced > current) {
        m_lezSyncState = "synced-height-ahead";
        reason = m_lezSyncState;
        return false;
    }

    m_lezCurrentHeight = current;
    m_lezSyncedHeight = synced;
    while (synced < current) {
        const std::int64_t target =
            std::min(current, synced + kMaximumSyncChunk);
        const int syncResult =
            modules().lez_core.sync_to_block(
                static_cast<int>(target), &callError);
        if (!callError.ok() || syncResult != kWalletFfiSuccess) {
            m_lezSyncState = "chunk-failed";
            reason = m_lezSyncState;
            return false;
        }
        const std::int64_t progressed =
            modules().lez_core.get_last_synced_block(&callError);
        if (!callError.ok() || progressed != target
            || progressed <= synced) {
            m_lezSyncState = "chunk-progress-mismatch";
            reason = m_lezSyncState;
            return false;
        }
        synced = progressed;
        m_lezSyncedHeight = synced;
    }
    if (m_lezSyncedHeight != m_lezCurrentHeight) {
        m_lezSyncState = "terminal-height-mismatch";
        reason = m_lezSyncState;
        return false;
    }
    m_lezSyncState = "current";
    reason = "current";
    return true;
}

std::string PalaceCoreImpl::startLez(const std::string& password)
{
    if (!isContextReady() || instancePersistencePath().empty())
        return "rejected=lez-not-ready";
    if (password.empty() || password.size() > 1024U)
        return "rejected=lez-invalid-password";
    if (!m_lezCoordinatorStoreHealthy || !m_lezCoordinatorStore)
        return "rejected=lez-coordinator-store";
    if (m_lezReady) {
        std::string syncReason;
        if (!syncLezWalletToCurrent(syncReason))
            return "rejected=lez-sync;reason=" + syncReason;
        return "ok;" + lezStatus();
    }

    const LezWalletPaths paths =
        prepareLezWalletPaths(instancePersistencePath());
    if (!paths.accepted)
        return "rejected=lez-" + paths.reason;

    logos::CallError callError;
    const std::string moduleName = modules().lez_core.name(&callError);
    if (!callError.ok())
        return "rejected=lez-module-name-call";
    const std::string moduleVersion =
        modules().lez_core.version(&callError);
    if (!callError.ok())
        return "rejected=lez-module-version-call";
    if (moduleName
            != palace::PalaceLezReleaseLock::expectedModuleName()
        || moduleVersion
            != palace::PalaceLezReleaseLock::network().moduleApiVersion) {
        return "rejected=lez-module-version-mismatch";
    }

    if (!m_lezWalletOpened) {
        const std::string configPath = paths.config.toStdString();
        const std::string storagePath = paths.storage.toStdString();
        if (paths.storageExists) {
            const std::int64_t opened = modules().lez_core.open(
                configPath, storagePath, &callError);
            if (!callError.ok() || opened != 0)
                return "rejected=lez-wallet-open";
            m_lezWalletState = "opened";
        } else {
            std::string mnemonic = modules().lez_core.create_new(
                configPath, storagePath, password, &callError);
            const bool created = callError.ok() && !mnemonic.empty();
            cleanse(mnemonic);
            if (!created)
                return "rejected=lez-wallet-create";
            if (modules().lez_core.save(&callError) != 0
                || !callError.ok()) {
                return "rejected=lez-wallet-save";
            }
            m_lezWalletState = "created";
        }
        m_lezWalletOpened = true;
    }

    const std::string sequencer =
        modules().lez_core.get_sequencer_addr(&callError);
    if (!callError.ok()
        || !palace::PalaceLezReleaseLock::acceptsLiveModule(
            moduleName, moduleVersion, sequencer)) {
        m_lezWalletState = "incompatible";
        return "rejected=lez-network-fingerprint";
    }

    std::string syncReason;
    if (!syncLezWalletToCurrent(syncReason))
        return "rejected=lez-sync;reason=" + syncReason;

    const palace::PalaceLezNetworkFingerprint& required =
        palace::PalaceLezReleaseLock::network();
    palace::PalaceLezNetworkFingerprint observed = required;
    observed.moduleApiVersion = moduleVersion;
    const palace::PalaceLezCompatibilityResult compatible =
        m_lezCoordinator.configureNetworkFingerprint(required, observed);
    if (!compatible.accepted || !m_lezCoordinator.activate()) {
        m_lezWalletState = "incompatible";
        return "rejected=lez-" + compatible.reason;
    }
    if (m_lezCoordinatorStore->save(m_lezCoordinator)
        != palace::PalaceLezCoordinatorStoreStatus::Saved) {
        m_lezCoordinator.interrupt();
        m_lezCoordinatorStoreHealthy = false;
        return "rejected=lez-coordinator-save";
    }
    m_lezReady = true;
    return "ok;" + lezStatus();
}

std::string PalaceCoreImpl::lezStatus() const
{
    return "wallet=" + m_lezWalletState
        + ";ready=" + (m_lezReady ? std::string("1") : std::string("0"))
        + ";compatible="
        + (m_lezCoordinator.compatibilityState()
                == palace::PalaceLezCompatibilityState::Compatible
            ? std::string("1") : std::string("0"))
        + ";running="
        + (m_lezCoordinator.running() ? std::string("1")
                                      : std::string("0"))
        + ";tracked=" + std::to_string(
            m_lezCoordinator.transactions().size())
        + ";sync=" + m_lezSyncState
        + ";current_height=" + std::to_string(m_lezCurrentHeight)
        + ";synced_height=" + std::to_string(m_lezSyncedHeight)
        + ";authority=" + m_lezAuthorityState
        + ";vm="
        + (m_palaceVmTurn.has_value()
               ? m_palaceVmTurn->phase
               : std::string("idle"))
        + ";vm_action="
        + (m_palaceVmTurn.has_value()
               ? m_palaceVmTurn->actionId
               : std::string("none"))
        + ";program="
        + palace::PalaceLezReleaseLock::network().programIdHex;
}

std::string PalaceCoreImpl::createIdentity(
    const std::string& displayName)
{
    if (!m_lezReady || !m_lezCoordinator.running()
        || instancePersistencePath().empty()) {
        return "rejected=identity-lez-not-ready";
    }
    const bool existing = m_deliveryIdentity.valid();
    if (existing) {
        if (m_deliveryIdentity.displayName() != displayName)
            return "rejected=identity-already-exists";
    } else {
        logos::CallError callError;
        const std::string accountId =
            modules().lez_core.create_account_public(&callError);
        palace::PalaceLezBytes32 accountBytes{};
        if (!callError.ok()
            || !palace::PalaceLezCodec::parseBytes32Hex(
                accountId, accountBytes)
            || palace::PalaceLezCodec::bytes32Hex(accountBytes)
                != accountId) {
            return "rejected=identity-account-create";
        }
        if (modules().lez_core.save(&callError) != 0
            || !callError.ok()) {
            return "rejected=identity-wallet-save";
        }

        palace::DeliveryIdentityMetadataV1 metadata;
        metadata.accountId = accountId;
        metadata.displayName = displayName;
        metadata.deliveryKeyEpoch = 1;
        const palace::DeliveryIdentityOpenStatus created =
            palace::PalaceDeliveryIdentity::create(
                instancePersistencePath(),
                metadata,
                m_deliveryIdentity);
        if (created
            != palace::DeliveryIdentityOpenStatus::Created) {
            return "rejected=identity-store;reason="
                + std::string(
                    palace::deliveryIdentityOpenStatusName(
                        created));
        }
    }

    m_deliveryProfile = m_deliveryIdentity.accountId();
    m_deliveryDisplayName = m_deliveryIdentity.displayName();
    m_deliveryKeyEpoch = m_deliveryIdentity.deliveryKeyEpoch();
    m_deliverySigner = &m_deliveryIdentity;

    if (!m_deliveryIdentityRegistration) {
        return "rejected=identity-registration-store-unavailable;"
            + identityStatus();
    }
    std::string registrationSyncReason;
    if (!syncLezWalletToCurrent(registrationSyncReason)
        || m_lezSyncedHeight < 0) {
        return "rejected=identity-registration-sync;reason="
            + registrationSyncReason;
    }

    const palace::DeliveryIdentityRegistrationTransitionV1
        registration =
            m_deliveryIdentityRegistration->ensureSubmitted(
                m_deliveryIdentity.accountId(),
                static_cast<std::uint64_t>(m_lezSyncedHeight),
                [this]() {
                    palace::PalaceLezSubmissionRecoveryExpectationV1
                        expectation;
                    expectation.programIdHex =
                        kPublicAccountRegistrationProgramIdHex;
                    expectation.accountIdsHex = {
                        m_deliveryIdentity.accountId(),
                    };
                    expectation.instructionWords = {1U};
                    expectation.signatureCount = 1U;
                    expectation.minimumFinalizedBlockExclusive =
                        m_deliveryIdentityRegistration
                        ? m_deliveryIdentityRegistration
                              ->minimumFinalizedBlockExclusive()
                        : 0U;
                    return lookupFinalizedLezSubmission(
                        "identity:"
                            + m_deliveryIdentity.accountId(),
                        expectation);
                },
                [this]() {
                    logos::CallError callError;
                    const std::string response =
                        modules().lez_core.register_public_account(
                            m_deliveryIdentity.accountId(),
                            &callError);
                    if (!callError.ok()) {
                        return palace::
                            DeliveryIdentityRegistrationSubmissionV1{
                                false,
                                {},
                                "module-call-failed",
                            };
                    }
                    const palace::PalaceLezSubmissionResult
                        submitted =
                            palace::PalaceLezCodec::
                                parseSubmissionResult(response);
                    if (!submitted.accepted) {
                        return palace::
                            DeliveryIdentityRegistrationSubmissionV1{
                                false,
                                {},
                                submitted.reason,
                            };
                    }
                    return palace::
                        DeliveryIdentityRegistrationSubmissionV1{
                            true,
                            submitted.transactionHash,
                            "accepted",
                        };
                },
                [this]() {
                    logos::CallError callError;
                    return modules().lez_core.save(&callError) == 0
                        && callError.ok();
                });
    if (!registration.ready) {
        return "rejected=identity-registration-pending;existing="
            + std::string(existing ? "1;" : "0;")
            + identityStatus()
            + ";reason=" + registration.reason;
    }
    return "ok;existing="
        + std::string(existing ? "1;" : "0;")
        + identityStatus();
}

std::string PalaceCoreImpl::identityStatus() const
{
    if (!m_deliveryIdentity.valid())
        return "identity=none";
    const std::string registrationPhase =
        m_deliveryIdentityRegistration
        ? m_deliveryIdentityRegistration->phaseName()
        : std::string("unavailable");
    const std::string registrationTransaction =
        m_deliveryIdentityRegistration
        ? m_deliveryIdentityRegistration->transactionHash()
        : std::string{};
    return "identity=" + m_deliveryIdentity.accountId()
        + ";display=" + m_deliveryIdentity.displayName()
        + ";delivery_key=" + m_deliveryIdentity.publicKey()
        + ";key_epoch="
        + std::to_string(m_deliveryIdentity.deliveryKeyEpoch())
        + ";registration=" + registrationPhase
        + ";registration_ready="
        + (m_deliveryIdentityRegistration
               && m_deliveryIdentityRegistration->ready()
            ? std::string("1") : std::string("0"))
        + ";registration_tx="
        + (registrationTransaction.empty()
            ? std::string("unknown")
            : registrationTransaction);
}

std::string PalaceCoreImpl::openPalace(
    const std::string& palaceUri)
{
    const auto palaceId = palaceIdFromUri(palaceUri);
    if (!palaceId.has_value())
        return "rejected=invalid-palace-uri";
    if (!m_lezReady || !m_lezCoordinator.running()
        || !m_lezFinalityTransport
        || !m_lezAuthorityBundleStore) {
        return "rejected=palace-lez-not-ready";
    }

    if (m_lezOpenHistory.has_value()) {
        PalaceLezOpenHistory& history = *m_lezOpenHistory;
        if (history.session.outcome()
                == palace::PalaceLezExplorerHistoryOutcome::Pending
            && !history.session.scanExhausted()) {
            if (history.palaceIdHex != *palaceId)
                return "rejected=palace-history-busy";
            pumpPalaceHistory();
            return "ok;" + palaceStatus();
        }
        if (history.palaceIdHex == *palaceId
            && history.session.outcome()
            == palace::PalaceLezExplorerHistoryOutcome::Rejected) {
            return "rejected=palace-history;reason="
                + history.reason;
        }
    }

    PalaceLezOpenHistory history;
    history.palaceUri = palaceUri;
    history.palaceIdHex = *palaceId;
    history.expectation.programIdHex =
        palace::PalaceLezReleaseLock::network().programIdHex;
    history.expectation.rootAccountIdHex =
        palace::PalaceLezCodec::deriveRootPda(
            history.expectation.programIdHex);
    const palace::PalaceLezExplorerHistoryUpdateV1 started =
        history.session.start(
            palace::PalaceLezReleaseLock::explorer(),
            lezHistoryLimits(),
            history.expectation);
    history.reason = started.reason;
    if (!started.accepted
        || started.outcome
            != palace::PalaceLezExplorerHistoryOutcome::Pending) {
        return "rejected=palace-history-start;reason="
            + started.reason;
    }
    m_lezOpenHistory = std::move(history);
    pumpPalaceHistory();
    return "ok;" + palaceStatus();
}

std::string PalaceCoreImpl::palaceStatus() const
{
    if (!m_lezOpenHistory.has_value()) {
        return "palace=closed;reason=not-opened;authority="
            + m_lezAuthorityState;
    }
    const PalaceLezOpenHistory& history =
        *m_lezOpenHistory;
    std::string state = "degraded";
    if (history.authorityApplied) {
        state = "open";
    } else if (history.session.outcome()
                   == palace::PalaceLezExplorerHistoryOutcome::
                       Pending
               && !history.session.scanExhausted()) {
        state = "scanning";
    } else if (history.session.outcome()
               == palace::PalaceLezExplorerHistoryOutcome::
                   Rejected) {
        state = "rejected";
    }
    std::string status =
        "palace=" + state
        + ";id=" + history.palaceIdHex
        + ";reason=" + history.reason
        + ";pages="
        + std::to_string(history.session.pagesScanned())
        + ";blocks="
        + std::to_string(history.session.blocksScanned())
        + ";authority=" + m_lezAuthorityState;
    if (m_lezAuthorityReady) {
        status += ";checkpoint="
            + std::to_string(
                m_lezAuthorityBundle.checkpoint
                    .finalizedBlockHeight)
            + ";action="
            + std::to_string(
                m_lezAuthorityBundle.checkpoint
                    .lastOrderedActionId);
    }
    status += ";vm="
        + (m_palaceVmTurn.has_value()
               ? m_palaceVmTurn->phase
               : std::string("idle"))
        + ";vm_action="
        + (m_palaceVmTurn.has_value()
               ? m_palaceVmTurn->actionId
               : std::string("none"));
    return status;
}

std::string PalaceCoreImpl::startDelivery(const std::string& nodeConfig)
{
    if (!isContextReady() || instancePersistencePath().empty())
        return "rejected=delivery-not-ready";
    if (!m_roomTransitionHealthy.load(
            std::memory_order_acquire)) {
        return "rejected=delivery-room-transition-recovery";
    }
    drainDeliveryEvents();

    const DeliveryStartConfig parsed = parseDeliveryStartConfig(nodeConfig);
    if (!parsed.accepted)
        return "rejected=delivery-" + parsed.reason;

    std::string selectedProfile = parsed.acceptanceProfile;
    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (!m_deliverySession)
            return "rejected=delivery-session-unavailable";
        if (selectedProfile.empty() && m_deliverySession->hasConfiguration())
            selectedProfile = m_deliverySession->configuration().senderUserId;
    }

#if defined(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
    palace::DeliveryAcceptanceIdentity acceptanceIdentity;
    const bool acceptance =
        !parsed.acceptanceProfile.empty()
        || (!selectedProfile.empty()
            && palace::deliveryAcceptanceIdentity(
                selectedProfile, acceptanceIdentity));
    if (acceptance
        && !palace::deliveryAcceptanceIdentity(
            selectedProfile, acceptanceIdentity)) {
        return "rejected=delivery-invalid-acceptance-profile";
    }
#else
    const bool acceptance = false;
#endif
    if (!acceptance) {
        if (!m_deliveryIdentity.valid())
            return "rejected=delivery-identity-required";
        if (selectedProfile.empty())
            selectedProfile = m_deliveryIdentity.accountId();
        if (selectedProfile != m_deliveryIdentity.accountId())
            return "rejected=delivery-profile-does-not-match-identity";
        if (m_deliveryAuthority.deliveryKeyFor(
                m_deliveryIdentity.accountId(),
                m_deliveryIdentity.deliveryKeyEpoch())
            != m_deliveryIdentity.publicKey()) {
            return "rejected=delivery-identity-not-finalized";
        }
    }

    std::vector<palace::DeliverySessionCommand> commands;
    std::vector<palace::DeliveryRecoveryAction> recoveryActions;
    std::uint64_t deliveryGeneration = 0U;
    bool alreadyActive = false;
    bool recoveryInProgress = false;
    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (!m_roomTransitionHealthy.load(
                std::memory_order_acquire)) {
            return "rejected=delivery-room-transition-recovery";
        }
        if (m_deliverySession->hasConfiguration()) {
            if (m_deliverySession->configuration().senderUserId
                != selectedProfile) {
                return "rejected=delivery-profile-does-not-match-restart-state";
            }
        } else {
            std::string roomId;
            std::int64_t roomEpoch = -1;
#if defined(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
            if (acceptance) {
                roomId = m_projection.currentRoomId();
                roomEpoch =
                    palace::deliveryAcceptanceRoomEpoch(
                        roomId);
            } else {
#endif
                roomId = m_deliveryAuthority.entryRoomId();
                roomEpoch =
                    m_deliveryAuthority.roomEpoch(roomId);
#if defined(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
            }
#endif
            palace::DeliverySessionConfigV1 config;
#if defined(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
            config.networkId = acceptance
                ? "logos.test"
                : palace::PalaceLezReleaseLock::network().networkId;
            config.palaceId = acceptance
                ? "palace-1" : m_deliveryAuthority.palaceId();
#else
            config.networkId =
                palace::PalaceLezReleaseLock::network().networkId;
            config.palaceId = m_deliveryAuthority.palaceId();
#endif
            config.roomId = roomId;
            config.roomEpoch = roomEpoch;
            config.senderUserId = selectedProfile;
#if defined(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
            config.senderKeyEpoch = acceptance
                ? acceptanceIdentity.keyEpoch
                : m_deliveryIdentity.deliveryKeyEpoch();
#else
            config.senderKeyEpoch =
                m_deliveryIdentity.deliveryKeyEpoch();
#endif
            if (roomEpoch < 0 || !m_deliverySession->configure(config))
                return "rejected=delivery-session-config";
            persistDeliverySessionLocked();
        }

#if defined(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
        if (acceptance) {
            m_deliveryAcceptanceIdentity =
                std::move(acceptanceIdentity);
            m_deliveryDisplayName =
                m_deliveryAcceptanceIdentity.displayName;
            m_deliveryKeyEpoch =
                m_deliveryAcceptanceIdentity.keyEpoch;
            m_deliverySigner =
                &m_deliveryAcceptanceIdentity.signer;
        } else {
#endif
            m_deliveryDisplayName = m_deliveryIdentity.displayName();
            m_deliveryKeyEpoch =
                m_deliveryIdentity.deliveryKeyEpoch();
            m_deliverySigner = &m_deliveryIdentity;
#if defined(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
        }
#endif
        m_deliveryProfile = selectedProfile;
        m_deliveryNodeConfig = parsed.moduleConfig;
#if defined(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
        m_deliverySession->replaceAllowedProps(
            acceptance
            ? palace::deliveryAcceptanceAllowedProps()
            : productionDeliveryAllowedProps());
#else
        m_deliverySession->replaceAllowedProps(
            productionDeliveryAllowedProps());
#endif

        const palace::DeliverySessionState state = m_deliverySession->state();
        const bool sessionActive =
            state == palace::DeliverySessionState::AwaitingCallbacks
            || state == palace::DeliverySessionState::StartingNode
            || state == palace::DeliverySessionState::WaitingForConnection
            || state == palace::DeliverySessionState::Subscribing
            || state == palace::DeliverySessionState::Online;
        recoveryInProgress = m_deliveryRecovery.recovering()
            || state == palace::DeliverySessionState::Recovering;
        alreadyActive = sessionActive && !recoveryInProgress;
        if (recoveryInProgress) {
            const palace::DeliveryRecoveryTransition recovery =
                m_deliveryRecovery.resume(
                    m_deliveryNodeRunning
                    || m_deliveryNodeCreated
                    || m_deliveryNodeCreatePending);
            if (recovery.interruptPendingWork)
                m_deliverySession->interrupt(true);
            if (recovery.interruptPendingWork)
                persistDeliverySessionLocked();
            recoveryActions = recovery.actions;
        } else if (!alreadyActive) {
            const palace::DeliverySessionTransition started =
                m_deliverySession->start();
            if (!started.accepted)
                return "rejected=delivery-session-start;" + started.reason;
            commands = started.commands;
        }
        deliveryGeneration =
            m_deliveryNativeCallGate.generation();
    }

    executeDeliveryRecoveryActions(
        recoveryActions, deliveryGeneration);
    if (!alreadyActive && !recoveryInProgress) {
        executeDeliveryCommands(commands, deliveryGeneration);
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (m_deliverySession->state()
            == palace::DeliverySessionState::Offline) {
            return "rejected=delivery-startup";
        }
    }
    return "ok;" + deliverySessionStatus();
}

std::string PalaceCoreImpl::subscribeRoom(const std::string& networkId,
                                          const std::string& palaceId,
                                          const std::string& roomId,
                                          std::int64_t roomEpoch)
{
    if (!m_roomTransitionHealthy.load(
            std::memory_order_acquire)) {
        return "rejected=delivery-room-transition-recovery";
    }
    drainDeliveryEvents();
    std::lock_guard<std::mutex> lock(m_deliveryMutex);
    if (!m_roomTransitionHealthy.load(
            std::memory_order_acquire)) {
        return "rejected=delivery-room-transition-recovery";
    }
    if (!m_deliverySession || !m_deliverySession->hasConfiguration())
        return "rejected=delivery-start-required";
    const palace::DeliverySessionConfigV1& config =
        m_deliverySession->configuration();
    if (roomEpoch < 0 || networkId != config.networkId
        || palaceId != config.palaceId || roomId != config.roomId
        || roomEpoch != config.roomEpoch) {
        return "rejected=delivery-session-subscription-mismatch";
    }
    return "ok;topic=" + palace::deriveRoomTopic(
        networkId, palaceId, roomId, roomEpoch)
        + ";state=" + palace::deliverySessionStateName(
            m_deliverySession->state());
}

std::string PalaceCoreImpl::deliverySessionStatus()
{
    drainDeliveryEvents();
    refreshDeliveryPresenceIfDue();
    std::lock_guard<std::mutex> lock(m_deliveryMutex);
    if (!m_deliverySession)
        return "state=unavailable;outbox=0;participants=0;correlated=0"
            ";received_accepted=0;received_rejected=0"
            ";rejected_scope=0;rejected_expired=0;rejected_signature=0"
            ";rejected_replay=0;rejected_payload=0;rejected_other=0";
    if (m_roomTransitionHealthy.load(
            std::memory_order_acquire)) {
        m_deliverySession->expireTransient(
            deliveryNowSeconds());
    }
    return "state=" + palace::deliverySessionStateName(
               m_deliverySession->state())
        + ";outbox=" + std::to_string(m_deliverySession->outboxSize())
        + ";participants=" + std::to_string(
            m_deliverySession->participantSnapshot().size())
        + ";correlated=" + std::to_string(
            m_deliveryRequestCorrelation.size())
        + ";callbacks=" + (m_deliveryCallbacksRegistered
            ? std::string("1") : std::string("0"))
        + ";node_running=" + (m_deliveryNodeRunning
            ? std::string("1") : std::string("0"))
        + ";recovery=" + palace::deliveryRecoveryStateName(
            m_deliveryRecovery.state())
        + ";recovery_epoch=" + std::to_string(
            m_deliveryRecovery.epoch())
        + ";received_accepted=" + std::to_string(
            m_deliveryAcceptedMessageCount)
        + ";received_rejected=" + std::to_string(
            m_deliveryRejectedMessageCount)
        + ";rejected_scope=" + std::to_string(m_deliveryRejectionCounts[
            static_cast<std::size_t>(
                palace::DeliveryRejectionClass::Scope)])
        + ";rejected_expired=" + std::to_string(m_deliveryRejectionCounts[
            static_cast<std::size_t>(
                palace::DeliveryRejectionClass::Expired)])
        + ";rejected_signature=" + std::to_string(m_deliveryRejectionCounts[
            static_cast<std::size_t>(
                palace::DeliveryRejectionClass::Signature)])
        + ";rejected_replay=" + std::to_string(m_deliveryRejectionCounts[
            static_cast<std::size_t>(
                palace::DeliveryRejectionClass::Replay)])
        + ";rejected_payload=" + std::to_string(m_deliveryRejectionCounts[
            static_cast<std::size_t>(
                palace::DeliveryRejectionClass::Payload)])
        + ";rejected_other=" + std::to_string(m_deliveryRejectionCounts[
            static_cast<std::size_t>(
                palace::DeliveryRejectionClass::Other)])
        + ";profile=" + (m_deliveryProfile.empty()
            ? std::string("none") : m_deliveryProfile);
}

std::string PalaceCoreImpl::participantProjection()
{
    drainDeliveryEvents();
    refreshDeliveryPresenceIfDue();
    std::lock_guard<std::mutex> lock(m_deliveryMutex);
    QJsonArray participants;
    if (m_deliverySession) {
        if (m_roomTransitionHealthy.load(
                std::memory_order_acquire)) {
            m_deliverySession->expireTransient(
                deliveryNowSeconds());
        }
        for (const palace::DeliveryParticipantProjectionV1& participant
             : m_deliverySession->participantSnapshot()) {
            QJsonObject object;
            object.insert(
                QStringLiteral("userId"),
                QString::fromStdString(participant.userId));
            object.insert(
                QStringLiteral("displayName"),
                QString::fromStdString(participant.displayName));
            object.insert(QStringLiteral("present"), participant.present);
            if (participant.hasMotion) {
                object.insert(
                    QStringLiteral("x"),
                    static_cast<double>(participant.motionX));
                object.insert(
                    QStringLiteral("y"),
                    static_cast<double>(participant.motionY));
            } else {
                object.insert(QStringLiteral("x"), QJsonValue::Null);
                object.insert(QStringLiteral("y"), QJsonValue::Null);
            }
            object.insert(
                QStringLiteral("speech"),
                QString::fromStdString(participant.speech));
            QJsonArray props;
            for (const std::string& propId : participant.propIds)
                props.append(QString::fromStdString(propId));
            object.insert(QStringLiteral("props"), props);
            participants.append(object);
        }
    }
    return QJsonDocument(participants)
        .toJson(QJsonDocument::Compact).toStdString();
}

std::string PalaceCoreImpl::deliveryNodeEvidence()
{
    drainDeliveryEvents();
    std::uint64_t generation = 0U;
    palace::PalaceRoomTransitionNativeCallToken admission;
    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (!m_roomTransitionHealthy.load(
                std::memory_order_acquire)) {
            return R"({"success":false,"reason":"room-transition-recovery"})";
        }
        if (!m_deliveryNodeCreated)
            return R"({"success":false,"reason":"node-not-created"})";
        generation =
            m_deliveryNativeCallGate.generation();
        if (!m_deliveryNativeCallGate.admitNativeCall(
                generation, true, admission)) {
            return R"({"success":false,"reason":"room-transition-recovery"})";
        }
    }

    const StdLogosResult addresses =
        modules().delivery_module.getNodeInfo("MyMultiaddresses");
    const StdLogosResult peerId =
        modules().delivery_module.getNodeInfo("MyPeerId");
    const StdLogosResult connected =
        modules().delivery_module.getConnectedPeersInfo();
    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (!m_deliveryNativeCallGate.completeNativeCall(
                admission,
                m_roomTransitionHealthy.load(
                    std::memory_order_acquire))) {
            return R"({"success":false,"reason":"room-transition-recovery"})";
        }
    }
    if (!addresses.success || !peerId.success || !connected.success
        || addresses.value.size() > 16384U
        || peerId.value.size() > 1024U
        || connected.value.size() > 65536U) {
        return R"({"success":false,"reason":"node-evidence-unavailable"})";
    }

    const auto evidenceValue = [](const std::string& value) {
        QJsonParseError error;
        const QJsonDocument parsed = QJsonDocument::fromJson(
            QByteArray::fromStdString(value), &error);
        if (error.error == QJsonParseError::NoError) {
            if (parsed.isArray())
                return QJsonValue(parsed.array());
            if (parsed.isObject())
                return QJsonValue(parsed.object());
        }
        return QJsonValue(QString::fromStdString(value));
    };

    QJsonObject evidence;
    evidence.insert(QStringLiteral("success"), true);
    evidence.insert(
        QStringLiteral("multiaddresses"), evidenceValue(addresses.value));
    evidence.insert(QStringLiteral("peerId"), evidenceValue(peerId.value));
    evidence.insert(
        QStringLiteral("connectedPeers"), evidenceValue(connected.value));
    const QByteArray encoded =
        QJsonDocument(evidence).toJson(QJsonDocument::Compact);
    if (encoded.size() > 90 * 1024)
        return R"({"success":false,"reason":"node-evidence-too-large"})";
    return encoded.toStdString();
}

std::string PalaceCoreImpl::say(const std::string& text)
{
    return publishDelivery(
        palace::DeliveryKind::Speech, text, 120);
}

std::string PalaceCoreImpl::move(std::int64_t x, std::int64_t y)
{
    return publishDelivery(
        palace::DeliveryKind::Motion,
        "x=" + std::to_string(x) + ";y=" + std::to_string(y),
        10);
}

std::string PalaceCoreImpl::wearProp(const std::string& propId)
{
    return publishDelivery(
        palace::DeliveryKind::WearProp, propId, 120);
}

std::string PalaceCoreImpl::removeProp(const std::string& propId)
{
    return publishDelivery(
        palace::DeliveryKind::RemoveProp, propId, 120);
}

std::string PalaceCoreImpl::refreshPresence()
{
    if (!m_roomTransitionHealthy.load(
            std::memory_order_acquire)) {
        return "rejected=delivery-room-transition-recovery";
    }
    drainDeliveryEvents();
    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (!m_roomTransitionHealthy.load(
                std::memory_order_acquire)) {
            return "rejected=delivery-room-transition-recovery";
        }
        if (!m_deliverySession
            || m_deliverySession->state()
                != palace::DeliverySessionState::Online) {
            return "rejected=delivery-session-not-online";
        }
    }
    return announceDeliveryPresence();
}

void PalaceCoreImpl::executeDeliveryCommands(
    const std::vector<palace::DeliverySessionCommand>& commands,
    const std::uint64_t generation)
{
    if (commands.empty())
        return;
    palace::PalaceRoomTransitionNativeCallToken admission;
    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (!m_deliveryNativeCallGate.admitNativeCall(
                generation,
                m_roomTransitionHealthy.load(
                    std::memory_order_acquire),
                admission)) {
            return;
        }
    }
    for (const palace::DeliverySessionCommand& command : commands)
        executeDeliveryCommand(command);
    std::lock_guard<std::mutex> lock(m_deliveryMutex);
    m_deliveryNativeCallGate.completeNativeCall(
        admission,
        m_roomTransitionHealthy.load(
            std::memory_order_acquire));
}

void PalaceCoreImpl::executeDeliveryRecoveryActions(
    const std::vector<palace::DeliveryRecoveryAction>& actions,
    const std::uint64_t generation)
{
    if (actions.empty())
        return;
    palace::PalaceRoomTransitionNativeCallToken admission;
    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (!m_deliveryNativeCallGate.admitNativeCall(
                generation,
                m_roomTransitionHealthy.load(
                    std::memory_order_acquire),
                admission)) {
            return;
        }
    }
    for (const palace::DeliveryRecoveryAction& action : actions)
        executeDeliveryRecoveryAction(action);
    std::lock_guard<std::mutex> lock(m_deliveryMutex);
    m_deliveryNativeCallGate.completeNativeCall(
        admission,
        m_roomTransitionHealthy.load(
            std::memory_order_acquire));
}

void PalaceCoreImpl::executeDeliveryRecoveryAction(
    const palace::DeliveryRecoveryAction& action)
{
    if (!m_roomTransitionHealthy.load(
            std::memory_order_acquire)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (!m_roomTransitionHealthy.load(
                std::memory_order_acquire)
            || !m_deliveryRecovery.expects(action)) {
            return;
        }
    }

    if (action.kind == palace::DeliveryRecoveryActionKind::StopNode) {
        const StdLogosResult stopped = modules().delivery_module.stop();
        std::vector<palace::DeliveryRecoveryAction> next;
        std::uint64_t nextGeneration = 0U;
        {
            std::lock_guard<std::mutex> lock(m_deliveryMutex);
            if (!m_roomTransitionHealthy.load(
                    std::memory_order_acquire)
                || !m_deliveryRecovery.expects(action)) {
                return;
            }
            next = m_deliveryRecovery.stopDispatchResult(
                action.commandId, stopped.success).actions;
            nextGeneration =
                m_deliveryNativeCallGate.generation();
        }
        executeDeliveryRecoveryActions(
            next, nextGeneration);
        return;
    }

    if (action.kind
        == palace::DeliveryRecoveryActionKind::QueryNodeStatus) {
        const std::string payload =
            modules().delivery_module.nodeStatus();
        const std::optional<palace::DeliveryNativeNodeState> nativeState =
            parseDeliveryNativeNodeState(payload);
        std::vector<palace::DeliveryRecoveryAction> next;
        std::uint64_t nextGeneration = 0U;
        {
            std::lock_guard<std::mutex> lock(m_deliveryMutex);
            if (!m_roomTransitionHealthy.load(
                    std::memory_order_acquire)
                || !m_deliveryRecovery.expects(action)) {
                return;
            }
            const palace::DeliveryRecoveryTransition settled =
                m_deliveryRecovery.nodeStatusResult(
                    action.commandId,
                    nativeState.has_value(),
                    nativeState.value_or(
                        palace::DeliveryNativeNodeState::Unknown));
            if (settled.accepted
                && settled.nativeRunning.has_value()) {
                m_deliveryNodeRunning = *settled.nativeRunning;
            }
            next = settled.actions;
            nextGeneration =
                m_deliveryNativeCallGate.generation();
        }
        executeDeliveryRecoveryActions(
            next, nextGeneration);
        return;
    }

    std::vector<palace::DeliverySessionCommand> sessionCommands;
    std::vector<palace::DeliveryRecoveryAction> next;
    std::uint64_t nextGeneration = 0U;
    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (!m_roomTransitionHealthy.load(
                std::memory_order_acquire)
            || !m_deliveryRecovery.expects(action)) {
            return;
        }

        palace::DeliverySessionTransition started;
        if (m_deliverySession)
            started = m_deliverySession->start();
        const palace::DeliveryRecoveryTransition settled =
            m_deliveryRecovery.restartSessionResult(
                action.commandId,
                m_deliverySession && started.accepted);
        if (started.accepted)
            sessionCommands = started.commands;
        if (m_deliverySession)
            persistDeliverySessionLocked();
        next = settled.actions;
        nextGeneration =
            m_deliveryNativeCallGate.generation();
    }
    executeDeliveryCommands(
        sessionCommands, nextGeneration);
    executeDeliveryRecoveryActions(
        next, nextGeneration);
}

void PalaceCoreImpl::executeDeliveryCommand(
    const palace::DeliverySessionCommand& command)
{
    if (!m_roomTransitionHealthy.load(
            std::memory_order_acquire)) {
        return;
    }
    if (command.kind
        == palace::DeliverySessionCommandKind::RegisterCallbacks) {
        std::vector<palace::DeliverySessionCommand> next;
        std::uint64_t nextGeneration = 0U;
        {
            std::lock_guard<std::mutex> lock(m_deliveryMutex);
            if (!m_roomTransitionHealthy.load(
                    std::memory_order_acquire)
                || !m_deliverySession) {
                return;
            }
            const palace::DeliverySessionTransition registered =
                m_deliverySession->callbacksRegistered(
                    m_deliveryCallbacksRegistered);
            next = registered.commands;
            nextGeneration =
                m_deliveryNativeCallGate.generation();
        }
        executeDeliveryCommands(next, nextGeneration);
        return;
    }

    if (command.kind == palace::DeliverySessionCommandKind::StartNode) {
        bool createNode = false;
        std::string nodeConfig;
        {
            std::lock_guard<std::mutex> lock(m_deliveryMutex);
            if (!m_roomTransitionHealthy.load(
                    std::memory_order_acquire)
                || !m_deliverySession
                || m_deliveryNodeCreatePending) {
                return;
            }
            createNode = !m_deliveryNodeCreated;
            if (createNode)
                m_deliveryNodeCreatePending = true;
            nodeConfig = m_deliveryNodeConfig;
        }

        if (createNode) {
            const StdLogosResult created =
                modules().delivery_module.createNode(nodeConfig);
            {
                std::lock_guard<std::mutex> lock(m_deliveryMutex);
                if (!m_roomTransitionHealthy.load(
                        std::memory_order_acquire)) {
                    return;
                }
                m_deliveryNodeCreatePending = false;
                m_deliveryNodeCreated = created.success;
            }
            if (!created.success) {
                deliveryNodeStarted(false);
                return;
            }
        }

        const StdLogosResult started = modules().delivery_module.start();
        if (!started.success)
            deliveryNodeStarted(false);
        return;
    }

    if (command.kind == palace::DeliverySessionCommandKind::Subscribe) {
        const StdLogosResult subscribed =
            modules().delivery_module.subscribe(command.contentTopic);
        bool online = false;
        std::vector<palace::DeliverySessionCommand> next;
        std::uint64_t nextGeneration = 0U;
        {
            std::lock_guard<std::mutex> lock(m_deliveryMutex);
            if (!m_roomTransitionHealthy.load(
                    std::memory_order_acquire)
                || !m_deliverySession) {
                return;
            }
            const palace::DeliverySessionTransition settled =
                m_deliverySession->subscriptionResult(subscribed.success);
            next = settled.commands;
            online = settled.accepted
                && m_deliverySession->state()
                    == palace::DeliverySessionState::Online;
            nextGeneration =
                m_deliveryNativeCallGate.generation();
        }
        executeDeliveryCommands(next, nextGeneration);
        if (online) {
            flushDeliveryOutbox();
            announceDeliveryPresence();
        }
        return;
    }

    if (command.kind != palace::DeliverySessionCommandKind::SendMessage)
        return;

    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (!m_roomTransitionHealthy.load(
                std::memory_order_acquire)) {
            return;
        }
        ++m_deliverySendCallsInFlight;
    }
    const StdLogosResult sent = modules().delivery_module.send(
        command.contentTopic, command.payload);

    std::lock_guard<std::mutex> lock(m_deliveryMutex);
    if (m_deliverySendCallsInFlight > 0U)
        --m_deliverySendCallsInFlight;
    if (!m_roomTransitionHealthy.load(
            std::memory_order_acquire)
        || !m_deliverySession) {
        if (m_deliverySendCallsInFlight == 0U)
            m_earlyDeliveryModuleEvents.clear();
        return;
    }
    if (!sent.success || sent.value.empty()
        || !m_deliveryRequestCorrelation.bind(
            sent.value, command.requestId)) {
        if (m_deliverySession->messageError(
                command.requestId, deliveryNowSeconds())) {
            persistDeliverySessionLocked();
        }
    } else {
        const auto early = m_earlyDeliveryModuleEvents.find(sent.value);
        if (early != m_earlyDeliveryModuleEvents.end()) {
            const std::vector<PendingDeliveryModuleEvent> events =
                early->second;
            m_earlyDeliveryModuleEvents.erase(early);
            for (const PendingDeliveryModuleEvent event : events) {
                const auto logical =
                    m_deliveryRequestCorrelation.logicalRequestId(
                        sent.value);
                if (!logical.has_value())
                    break;
                applyDeliveryModuleEventLocked(
                    *logical, sent.value, event);
            }
        }
    }
    if (m_deliverySendCallsInFlight == 0U)
        m_earlyDeliveryModuleEvents.clear();
}

void PalaceCoreImpl::deliveryNodeStarted(bool succeeded)
{
    std::vector<palace::DeliverySessionCommand> commands;
    std::vector<palace::DeliveryRecoveryAction> recoveryActions;
    std::uint64_t generation = 0U;
    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (!m_roomTransitionHealthy.load(
                std::memory_order_acquire)
            || !m_deliverySession) {
            return;
        }
        const palace::DeliveryRecoveryTransition recovery =
            m_deliveryRecovery.nodeStarted(succeeded);
        if (!recovery.accepted || !recovery.forwardNodeStarted)
            return;
        const palace::DeliverySessionTransition settled =
            m_deliverySession->nodeStarted(succeeded);
        if (settled.accepted) {
            m_deliveryNodeRunning = succeeded;
            persistDeliverySessionLocked();
        }
        commands = settled.commands;
        recoveryActions = recovery.actions;
        generation =
            m_deliveryNativeCallGate.generation();
    }
    executeDeliveryCommands(commands, generation);
    executeDeliveryRecoveryActions(
        recoveryActions, generation);
}

void PalaceCoreImpl::deliveryNodeStopped(bool succeeded)
{
    std::vector<palace::DeliveryRecoveryAction> recoveryActions;
    std::uint64_t generation = 0U;
    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (!m_roomTransitionHealthy.load(
                std::memory_order_acquire)) {
            return;
        }
        const palace::DeliveryRecoveryTransition recovery =
            m_deliveryRecovery.nodeStopped(succeeded);
        if (!recovery.accepted)
            return;
        m_deliveryNodeCreatePending = false;
        m_deliveryRequestCorrelation.clear();
        m_earlyDeliveryModuleEvents.clear();
        m_deliveryPresenceRefreshAt = 0;
        recoveryActions = recovery.actions;
        generation =
            m_deliveryNativeCallGate.generation();
    }
    executeDeliveryRecoveryActions(
        recoveryActions, generation);
}

void PalaceCoreImpl::deliveryConnectionChanged(const std::string& status)
{
    const auto connection = palace::parseDeliveryConnectionState(status);
    if (!connection.has_value())
        return;

    std::vector<palace::DeliverySessionCommand> commands;
    std::uint64_t generation = 0U;
    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (!m_roomTransitionHealthy.load(
                std::memory_order_acquire)
            || !m_deliverySession) {
            return;
        }
        if (*connection != palace::DeliveryConnectionState::Connected) {
            m_deliveryRequestCorrelation.clear();
            m_earlyDeliveryModuleEvents.clear();
            m_deliveryPresenceRefreshAt = 0;
        }
        commands =
            m_deliverySession->connectionStateChanged(*connection).commands;
        generation =
            m_deliveryNativeCallGate.generation();
    }
    executeDeliveryCommands(commands, generation);
}

void PalaceCoreImpl::deliveryMessageReceived(
    const std::string& contentTopic,
    const std::vector<std::uint8_t>& payload)
{
    std::lock_guard<std::mutex> lock(m_deliveryMutex);
    if (!m_roomTransitionHealthy.load(
            std::memory_order_acquire)
        || !m_deliverySession) {
        return;
    }
    const palace::DeliverySessionReceive received =
        m_deliverySession->receive(
            contentTopic, payload, deliveryNowSeconds(),
            m_deliveryVerifier);
    if (received.accepted) {
        incrementSaturated(m_deliveryAcceptedMessageCount);
        persistDeliverySessionLocked();
    } else {
        incrementSaturated(m_deliveryRejectedMessageCount);
        std::uint64_t& count = m_deliveryRejectionCounts[
            static_cast<std::size_t>(
                palace::classifyDeliveryRejection(received.reason))];
        incrementSaturated(count);
    }
}

void PalaceCoreImpl::deliveryModuleEvent(
    const std::string& moduleRequestId,
    PendingDeliveryModuleEvent event)
{
    std::lock_guard<std::mutex> lock(m_deliveryMutex);
    if (!m_roomTransitionHealthy.load(
            std::memory_order_acquire)) {
        return;
    }
    const auto logical =
        m_deliveryRequestCorrelation.logicalRequestId(moduleRequestId);
    if (logical.has_value()) {
        applyDeliveryModuleEventLocked(
            *logical, moduleRequestId, event);
        return;
    }
    if (m_deliverySendCallsInFlight == 0U
        || m_earlyDeliveryModuleEvents.size() >= 256U) {
        return;
    }
    auto& pending = m_earlyDeliveryModuleEvents[moduleRequestId];
    if (pending.size() < 3U)
        pending.push_back(event);
}

void PalaceCoreImpl::applyDeliveryModuleEventLocked(
    const std::string& logicalRequestId,
    const std::string& moduleRequestId,
    PendingDeliveryModuleEvent event)
{
    if (!m_roomTransitionHealthy.load(
            std::memory_order_acquire)
        || !m_deliverySession) {
        return;
    }

    bool changed = false;
    if (event == PendingDeliveryModuleEvent::Sent) {
        changed = m_deliverySession->messageSent(logicalRequestId);
        m_deliveryRequestCorrelation.take(moduleRequestId);
    } else if (event == PendingDeliveryModuleEvent::Propagated) {
        changed =
            m_deliverySession->messagePropagated(logicalRequestId);
        m_deliveryRequestCorrelation.take(moduleRequestId);
    } else {
        changed = m_deliverySession->messageError(
            logicalRequestId, deliveryNowSeconds());
        m_deliveryRequestCorrelation.take(moduleRequestId);
    }
    if (changed)
        persistDeliverySessionLocked();
}

void PalaceCoreImpl::flushDeliveryOutbox()
{
    std::vector<palace::DeliverySessionCommand> commands;
    std::uint64_t generation = 0U;
    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (!m_roomTransitionHealthy.load(
                std::memory_order_acquire)
            || !m_deliverySession) {
            return;
        }
        const std::size_t previousSize = m_deliverySession->outboxSize();
        commands = m_deliverySession->eligibleOutboxCommands(
            deliveryNowSeconds());
        if (m_deliverySession->outboxSize() != previousSize)
            persistDeliverySessionLocked();
        generation =
            m_deliveryNativeCallGate.generation();
    }
    executeDeliveryCommands(commands, generation);
}

std::string PalaceCoreImpl::announceDeliveryPresence()
{
    if (!m_roomTransitionHealthy.load(
            std::memory_order_acquire)) {
        return "rejected=delivery-room-transition-recovery";
    }
    std::vector<palace::DeliverySessionCommand> commands;
    std::string requestId;
    std::string reason;
    std::uint64_t generation = 0U;
    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (!m_roomTransitionHealthy.load(
                std::memory_order_acquire)) {
            return "rejected=delivery-room-transition-recovery";
        }
        if (!m_deliverySession || m_deliveryProfile.empty()
            || m_deliverySigner == nullptr)
            return "rejected=delivery-session-not-configured";
        requestId = nextDeliveryRequestIdLocked("presence");
        if (requestId.empty())
            return "rejected=delivery-request-id-exhausted";
        const std::int64_t now = deliveryNowSeconds();
        const palace::DeliverySessionTransition published =
            m_deliverySession->publish(
                requestId,
                palace::DeliveryKind::PresenceHello,
                m_deliveryDisplayName,
                now,
                120,
                *m_deliverySigner,
                m_deliveryVerifier);
        if (!published.accepted)
            return "rejected=delivery-publish;" + published.reason;
        m_deliveryPresenceRefreshAt = now + 60;
        persistDeliverySessionLocked();
        commands = published.commands;
        reason = published.reason;
        generation =
            m_deliveryNativeCallGate.generation();
    }
    executeDeliveryCommands(commands, generation);
    return "ok;request=" + requestId + ";delivery=" + reason;
}

void PalaceCoreImpl::refreshDeliveryPresenceIfDue()
{
    bool refresh = false;
    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (!m_roomTransitionHealthy.load(
                std::memory_order_acquire)) {
            return;
        }
        const std::int64_t now = deliveryNowSeconds();
        refresh = m_deliverySession
            && m_deliverySession->state()
                == palace::DeliverySessionState::Online
            && (m_deliveryPresenceRefreshAt == 0
                || m_deliveryPresenceRefreshAt <= now);
        if (refresh)
            m_deliveryPresenceRefreshAt = now + 60;
    }
    if (refresh)
        announceDeliveryPresence();
}

std::string PalaceCoreImpl::publishDelivery(
    palace::DeliveryKind kind,
    const std::string& payload,
    std::int64_t lifetimeSeconds)
{
    if (!m_roomTransitionHealthy.load(
            std::memory_order_acquire)) {
        return "rejected=delivery-room-transition-recovery";
    }
    drainDeliveryEvents();
    std::vector<palace::DeliverySessionCommand> commands;
    std::string requestId;
    std::string reason;
    std::uint64_t generation = 0U;
    {
        std::lock_guard<std::mutex> lock(m_deliveryMutex);
        if (!m_roomTransitionHealthy.load(
                std::memory_order_acquire)) {
            return "rejected=delivery-room-transition-recovery";
        }
        if (!m_deliverySession
            || !m_deliverySession->hasConfiguration()
            || m_deliveryProfile.empty()
            || m_deliverySigner == nullptr) {
            return "rejected=delivery-session-not-configured";
        }
        requestId = nextDeliveryRequestIdLocked("live");
        if (requestId.empty())
            return "rejected=delivery-request-id-exhausted";
        const std::int64_t now = deliveryNowSeconds();
        const palace::DeliverySessionTransition published =
            m_deliverySession->publish(
                requestId,
                kind,
                payload,
                now,
                lifetimeSeconds,
                *m_deliverySigner,
                m_deliveryVerifier);
        if (!published.accepted)
            return "rejected=delivery-publish;" + published.reason;
        persistDeliverySessionLocked();
        commands = published.commands;
        reason = published.reason;
        generation =
            m_deliveryNativeCallGate.generation();
    }
    executeDeliveryCommands(commands, generation);
    return "ok;request=" + requestId + ";delivery=" + reason;
}

std::string PalaceCoreImpl::nextDeliveryRequestIdLocked(
    const std::string& prefix)
{
    if (m_nextDeliveryRequestId
        == std::numeric_limits<std::uint64_t>::max()) {
        return {};
    }
    ++m_nextDeliveryRequestId;
    return prefix + "-" + std::to_string(deliveryNowSeconds())
        + "-" + std::to_string(m_nextDeliveryRequestId);
}

std::string PalaceCoreImpl::startStorage(const std::string& nodeConfig)
{
    drainStorageCallbacks();
    if (!isContextReady() || instancePersistencePath().empty()
        || nodeConfig.empty()) {
        return "rejected=storage-not-ready-or-empty-config";
    }
    if (!m_storageCallbacksRegistered)
        return "rejected=storage-callback-registration";

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray::fromStdString(nodeConfig), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return "rejected=storage-invalid-config";

    const QString instanceRoot = QDir(QString::fromStdString(instancePersistencePath())).canonicalPath();
    if (instanceRoot.isEmpty())
        return "rejected=storage-invalid-instance-root";
    const QString storageDirectory = instanceRoot + QStringLiteral("/storage");
    if (!QDir().mkpath(storageDirectory))
        return "rejected=storage-directory-create-failed";
    const QString canonicalStorageDirectory = QDir(storageDirectory).canonicalPath();
    if (!isUnder(canonicalStorageDirectory, instanceRoot))
        return "rejected=storage-directory-escaped-instance-root";

    QJsonObject config = document.object();
#if defined(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
    const QJsonValue acceptanceHolder =
        config.take(QStringLiteral("palaceAcceptanceHolderProfile"));
    std::string holderAccountId;
    if (!acceptanceHolder.isUndefined()) {
        if (!acceptanceHolder.isString())
            return "rejected=storage-invalid-acceptance-holder";
        const auto fixtureHolder = storageAcceptanceHolderId(
            acceptanceHolder.toString().toStdString());
        if (!fixtureHolder.has_value())
            return "rejected=storage-invalid-acceptance-holder";
        holderAccountId = *fixtureHolder;
    } else
#else
    std::string holderAccountId;
#endif
    if (m_deliveryIdentity.valid()
               && isLowerHexAccountId(
                   m_deliveryIdentity.accountId())) {
        holderAccountId = m_deliveryIdentity.accountId();
    } else {
        return "rejected=storage-holder-identity-unavailable";
    }
    config.insert(QStringLiteral("data-dir"), canonicalStorageDirectory);
    config.remove(QStringLiteral("log-file"));
    const std::string canonicalConfig =
        QJsonDocument(config).toJson(QJsonDocument::Compact).toStdString();

    if (!m_storageSession.hasConfiguration()) {
        palace::StorageModuleSessionConfigV1 sessionConfig;
        sessionConfig.initializationConfig = canonicalConfig;
        sessionConfig.operationIdPrefix =
            "palace-storage-"
            + std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count());
        if (!m_storageSession.configure(sessionConfig))
            return "rejected=storage-session-config";
        m_storageInitializationConfig = canonicalConfig;
        palace::StorageCatalogSessionConfigV3 catalogConfig;
        catalogConfig.localHolderAccountId = holderAccountId;
        if (!m_storageCatalog.configure(catalogConfig))
            return "rejected=storage-catalog-config";
        m_storageHolderAccountId = holderAccountId;
    } else if (m_storageInitializationConfig != canonicalConfig) {
        return "rejected=storage-config-changed";
    } else if (m_storageHolderAccountId != holderAccountId) {
        return "rejected=storage-holder-changed";
    }

    const palace::StorageModuleSessionTransition started =
        m_storageSession.start();
    if (!started.accepted)
        return "rejected=storage-start;" + started.reason;
    executeStorageCommands(started.commands);
    startRestoredStorageMvpFetchIfReady();
    return "ok;" + storageSessionStatus();
}

std::string PalaceCoreImpl::storageSessionStatus()
{
    drainStorageCallbacks();
    return "storage="
        + palace::storageModuleSessionStateName(m_storageSession.state())
        + ";pending="
        + std::to_string(m_storageSession.pendingTransferCount())
        + ";callbacks="
        + std::to_string(m_storageSession.queuedCallbackCount())
        + ";callback_registration="
        + (m_storageCallbacksRegistered ? "ready" : "failed")
        + ";reconciliation_required="
        + (m_storageSession.reconciliationRequired() ? "1" : "0")
        + ";catalog=" + m_storageMvpMode
        + ";catalog_verified="
        + std::to_string(m_storageMvpFetchedObjects.size())
        + ";retention_round="
        + std::to_string(m_storageRetentionRound)
        + ";retained="
        + std::to_string(m_storageMvpRetainedObjects.size());
}

namespace {

bool storageResultAsString(const StdLogosResult& result, std::string& out)
{
    if (!result.success)
        return false;
    if (result.value.is_string()) {
        out = result.value.get<std::string>();
        return !out.empty() && out.size() <= 8192U;
    }
    return false;
}

} // namespace

std::string PalaceCoreImpl::storagePeerEndpoint()
{
    drainStorageCallbacks();
    if (!isContextReady() || !m_storageCallbacksRegistered)
        return "rejected=storage-not-ready";
    if (!m_storageSession.hasConfiguration()
        || !m_storageSession.running()) {
        return "rejected=storage-not-running";
    }

    const StdLogosResult peerIdResult = modules().storage_module.peerId();
    const StdLogosResult sprResult = modules().storage_module.spr();
    std::string peerId;
    std::string spr;
    if (!storageResultAsString(peerIdResult, peerId))
        return "rejected=storage-peer-id";
    if (!storageResultAsString(sprResult, spr))
        return "rejected=storage-spr";

    QJsonObject endpoint;
    endpoint.insert(QStringLiteral("peerId"), QString::fromStdString(peerId));
    endpoint.insert(QStringLiteral("spr"), QString::fromStdString(spr));

    const StdLogosResult debugResult = modules().storage_module.debug();
    if (debugResult.success && debugResult.value.is_object()) {
        const auto& debug = debugResult.value;
        if (debug.contains("addrs") && debug["addrs"].is_array()) {
            QJsonArray addrs;
            for (const auto& entry : debug["addrs"]) {
                if (!entry.is_string())
                    continue;
                const std::string address = entry.get<std::string>();
                if (address.empty() || address.size() > 512U)
                    continue;
                addrs.append(QString::fromStdString(address));
            }
            if (!addrs.isEmpty())
                endpoint.insert(QStringLiteral("addrs"), addrs);
        }
        if (debug.contains("announceAddresses")
            && debug["announceAddresses"].is_array()) {
            QJsonArray announce;
            for (const auto& entry : debug["announceAddresses"]) {
                if (!entry.is_string())
                    continue;
                const std::string address = entry.get<std::string>();
                if (address.empty() || address.size() > 512U)
                    continue;
                announce.append(QString::fromStdString(address));
            }
            if (!announce.isEmpty()) {
                endpoint.insert(
                    QStringLiteral("announceAddresses"), announce);
            }
        }
        // DHT routing table peer IDs (for multi-node mesh readiness checks).
        // Prefer nodes with seen=true (actually observed), not just records.
        QJsonArray tablePeers;
        QJsonArray seenPeers;
        if (debug.contains("table") && debug["table"].is_object()) {
            const auto& table = debug["table"];
            if (table.contains("localNode")
                && table["localNode"].is_object()
                && table["localNode"].contains("peerId")
                && table["localNode"]["peerId"].is_string()) {
                const std::string id =
                    table["localNode"]["peerId"].get<std::string>();
                if (!id.empty() && id.size() <= 1024U)
                    tablePeers.append(QString::fromStdString(id));
            }
            if (table.contains("nodes") && table["nodes"].is_array()) {
                for (const auto& node : table["nodes"]) {
                    if (!node.is_object()
                        || !node.contains("peerId")
                        || !node["peerId"].is_string()) {
                        continue;
                    }
                    const std::string id =
                        node["peerId"].get<std::string>();
                    if (id.empty() || id.size() > 1024U)
                        continue;
                    const QString qid = QString::fromStdString(id);
                    tablePeers.append(qid);
                    bool seen = false;
                    if (node.contains("seen") && node["seen"].is_boolean())
                        seen = node["seen"].get<bool>();
                    if (seen)
                        seenPeers.append(qid);
                }
            }
        }
        if (!tablePeers.isEmpty())
            endpoint.insert(QStringLiteral("tablePeers"), tablePeers);
        if (!seenPeers.isEmpty())
            endpoint.insert(QStringLiteral("seenPeers"), seenPeers);
    }

    const QByteArray encoded =
        QJsonDocument(endpoint).toJson(QJsonDocument::Compact);
    if (encoded.isEmpty() || encoded.size() > 16 * 1024)
        return "rejected=storage-endpoint-too-large";
    return std::string("ok;") + encoded.toStdString();
}

std::string PalaceCoreImpl::connectStoragePeer(
    const std::string& peerId,
    const std::string& addressesJson)
{
    drainStorageCallbacks();
    if (!isContextReady() || !m_storageCallbacksRegistered)
        return "rejected=storage-not-ready";
    if (!m_storageSession.hasConfiguration()
        || !m_storageSession.running()) {
        return "rejected=storage-not-running";
    }
    if (peerId.empty() || peerId.size() > 1024U)
        return "rejected=storage-connect-peer-id";
    if (addressesJson.empty() || addressesJson.size() > 16 * 1024U)
        return "rejected=storage-connect-addresses";

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray::fromStdString(addressesJson), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray())
        return "rejected=storage-connect-addresses-json";
    const QJsonArray array = document.array();
    if (array.isEmpty() || array.size() > 16)
        return "rejected=storage-connect-addresses-count";

    std::vector<std::string> addresses;
    addresses.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue& entry : array) {
        if (!entry.isString())
            return "rejected=storage-connect-address-type";
        const std::string address = entry.toString().toStdString();
        if (address.empty() || address.size() > 512U)
            return "rejected=storage-connect-address-size";
        // Require multiaddr-shaped dial targets (ip4/ip6 + tcp).
        if (address.find("/tcp/") == std::string::npos
            || (address.rfind("/ip4/", 0) != 0
                && address.rfind("/ip6/", 0) != 0)) {
            return "rejected=storage-connect-address-shape";
        }
        addresses.push_back(address);
    }

    const StdLogosResult connected =
        modules().storage_module.connect(peerId, addresses);
    if (!connected.success)
        return "rejected=storage-connect-dispatch";
    return "ok;connect=sent;peers=" + std::to_string(addresses.size());
}

std::string PalaceCoreImpl::markStorageMaterialized()
{
    if (!isContextReady() || !m_storageSession.running())
        return "rejected=storage-not-running";
    m_storageMvpColocatedMaterialized = true;
    return "ok;materialized=1";
}

std::string PalaceCoreImpl::fetchPngDerivative(const std::string& sourceCid,
                                                const std::string& derivativeCid,
                                                std::uint64_t byteLength,
                                                const std::string& contentSha256,
                                                std::uint32_t width,
                                                std::uint32_t height)
{
    drainStorageCallbacks();
    if (!isContextReady() || !m_verifiedAssetStore
        || !m_storageSession.running()) {
        return "rejected=storage-not-running";
    }

    const QString instanceRoot = QDir(QString::fromStdString(instancePersistencePath())).canonicalPath();
    if (instanceRoot.isEmpty())
        return "rejected=storage-invalid-instance-root";
    const QString downloadsDirectory = instanceRoot + QStringLiteral("/asset_downloads");
    if (!QDir().mkpath(downloadsDirectory))
        return "rejected=asset-download-directory-create-failed";
    const QString canonicalDownloadsDirectory = QDir(downloadsDirectory).canonicalPath();
    if (!isUnder(canonicalDownloadsDirectory, instanceRoot))
        return "rejected=asset-download-directory-escaped-instance-root";

    palace::AssetRefV1 reference;
    reference.sourceCid = sourceCid;
    reference.derivativeCid = derivativeCid;
    reference.byteLength = byteLength;
    reference.mediaType = "image/png";
    reference.width = width;
    reference.height = height;
    reference.technicalProfile = "palace-png-v1";
    reference.contentSha256 = contentSha256;
    const auto pending = m_storageAssets.begin(reference, canonicalDownloadsDirectory.toStdString());
    if (!pending.has_value())
        return "rejected=invalid-or-already-pending-asset";

    const palace::StorageModuleSessionTransition fetch =
        m_storageSession.beginNetworkFetch(
            pending->operationId,
            pending->networkCid,
            pending->destinationPath,
            reference.byteLength,
            65536U);
    if (!fetch.accepted) {
        m_storageAssets.cancel(pending->operationId);
        return "rejected=storage-download;" + fetch.reason;
    }
    m_assetStatus[reference.derivativeCid] = "fetching";
    executeStorageCommands(fetch.commands);
    const auto current = m_assetStatus.find(reference.derivativeCid);
    if (current != m_assetStatus.end()
        && current->second.rfind("degraded;", 0U) == 0U) {
        return current->second;
    }
    return "ok;asset=fetching;operation=" + pending->operationId;
}

std::string PalaceCoreImpl::assetStatus(const std::string& derivativeCid) const
{
    const auto found = m_assetStatus.find(derivativeCid);
    return found == m_assetStatus.end() ? "missing" : found->second;
}

std::string PalaceCoreImpl::publishVerifiedPng(const std::string& handle)
{
    drainStorageCallbacks();
    if (!isContextReady() || !m_verifiedAssetStore
        || !m_storageSession.running()) {
        return "rejected=storage-not-running";
    }
    const palace::AssetAuthoringAssetV1* asset =
        m_assetAuthoring.asset(handle);
    if (asset == nullptr)
        return "rejected=asset-unknown";
    if (asset->reviewState != "approved")
        return "rejected=asset-not-approved";
    const auto existing = m_publicationStatus.find(handle);
    if (existing != m_publicationStatus.end()
        && (existing->second == "publishing" || existing->second.rfind("published;cid=", 0) == 0)) {
        return "ok;asset=" + existing->second;
    }

    const auto path = m_verifiedAssetStore->verifiedPngPath(handle);
    if (!path.has_value())
        return "rejected=unverified-asset-handle";

    const QFileInfo file(QString::fromStdString(*path));
    if (!file.isFile() || file.isSymLink() || file.size() <= 0)
        return "rejected=verified-asset-path";
    if (m_nextStoragePublicationId
        == std::numeric_limits<std::uint64_t>::max()) {
        return "rejected=storage-publication-id-exhausted";
    }
    const std::string operationId =
        "palace-publish-"
        + std::to_string(++m_nextStoragePublicationId);
    const palace::StorageModuleSessionTransition upload =
        m_storageSession.beginUpload(
            operationId,
            *path,
            static_cast<std::uint64_t>(file.size()),
            65536U);
    if (!upload.accepted)
        return "rejected=storage-upload;" + upload.reason;
    m_storagePublicationByOperation.emplace(operationId, handle);
    m_publicationStatus[handle] = "publishing";
    executeStorageCommands(upload.commands);
    const auto current = m_publicationStatus.find(handle);
    if (current != m_publicationStatus.end()
        && current->second.rfind("publish-failed", 0U) == 0U) {
        return "rejected=storage-upload-dispatch";
    }
    return "ok;asset=publishing";
}

std::string PalaceCoreImpl::publicationStatus(const std::string& handle) const
{
    const auto found = m_publicationStatus.find(handle);
    return found == m_publicationStatus.end() ? "unknown" : found->second;
}

std::string PalaceCoreImpl::beginAssetStage(
    const std::string& label)
{
    const palace::AssetAuthoringResult result =
        m_assetAuthoring.begin(label);
    if (!result.accepted)
        return "rejected=" + result.reason;
    return "ok;session=" + result.sessionId
        + ";next=0;maxChunkBytes="
        + std::to_string(
            palace::AssetAuthoringCatalog::MaximumChunkBytes)
        + ";maxTotalBytes="
        + std::to_string(
            palace::AssetAuthoringCatalog::MaximumAssetBytes);
}

std::string PalaceCoreImpl::appendAssetStageChunk(
    const std::string& sessionId,
    std::uint64_t sequence,
    const std::string& canonicalBase64)
{
    const palace::AssetAuthoringResult result =
        m_assetAuthoring.append(
            sessionId, sequence, canonicalBase64);
    if (!result.accepted)
        return "rejected=" + result.reason;
    return "ok;session=" + result.sessionId
        + ";next=" + std::to_string(result.nextSequence)
        + ";bytes=" + std::to_string(result.byteLength);
}

std::string PalaceCoreImpl::commitAssetStage(
    const std::string& sessionId)
{
    const palace::AssetAuthoringResult result =
        m_assetAuthoring.commit(sessionId);
    if (!result.accepted)
        return "rejected=" + result.reason;
    return "ok;handle=" + result.handle
        + ";width=" + std::to_string(result.width)
        + ";height=" + std::to_string(result.height)
        + ";bytes=" + std::to_string(result.byteLength);
}

std::string PalaceCoreImpl::cancelAssetStage(
    const std::string& sessionId)
{
    const palace::AssetAuthoringResult result =
        m_assetAuthoring.cancel(sessionId);
    if (!result.accepted)
        return "rejected=" + result.reason;
    return "ok;session=" + result.sessionId
        + ";cancelled=1";
}

std::string PalaceCoreImpl::assetAuthoringCatalog() const
{
    const palace::AssetAuthoringStateV1& authoringState =
        m_assetAuthoring.state();

    QJsonArray entries;
    for (const palace::AssetAuthoringAssetV1& asset
         : m_assetAuthoring.assets()) {
        const auto publication =
            m_publicationStatus.find(asset.handle);
        const std::string publicationState =
            publication == m_publicationStatus.end()
            ? "not-uploaded" : publication->second;

        std::string publicationLabel = publicationState;
        std::string cid = asset.publishedCid;
        constexpr char kPublishedPrefix[] = "published;cid=";
        if (publicationState.rfind(kPublishedPrefix, 0U) == 0U) {
            publicationLabel = "published";
            cid = publicationState.substr(
                sizeof(kPublishedPrefix) - 1U);
        } else if (publicationState.rfind(
                       "publish-failed", 0U) == 0U) {
            publicationLabel = "publish-failed";
        }

        QJsonObject entry;
        entry.insert(
            QStringLiteral("handle"),
            QString::fromStdString(asset.handle));
        entry.insert(
            QStringLiteral("label"),
            QString::fromStdString(asset.label));
        entry.insert(
            QStringLiteral("width"),
            static_cast<qint64>(asset.width));
        entry.insert(
            QStringLiteral("height"),
            static_cast<qint64>(asset.height));
        entry.insert(
            QStringLiteral("byteLength"),
            static_cast<qint64>(asset.byteLength));
        entry.insert(
            QStringLiteral("reviewState"),
            QString::fromStdString(asset.reviewState));
        entry.insert(
            QStringLiteral("publicationState"),
            QString::fromStdString(publicationLabel));
        entry.insert(
            QStringLiteral("cid"),
            QString::fromStdString(cid));
        QJsonArray roomAssignments;
        bool hasRoomRole = false;
        for (const auto& [roomId, assignedHandle]
             : authoringState.roomAssignments) {
            if (assignedHandle == asset.handle) {
                roomAssignments.append(
                    QString::fromStdString(roomId));
                hasRoomRole = true;
            }
        }
        QJsonArray propAssignments;
        const bool hasPropRole =
            authoringState.propAssignment.has_value()
            && authoringState.propAssignment->handle
                == asset.handle;
        if (hasPropRole) {
            propAssignments.append(
                QString::fromStdString(
                    authoringState.propAssignment->propId));
        }
        QJsonArray roles;
        if (hasRoomRole)
            roles.append(QStringLiteral("room-background"));
        if (hasPropRole)
            roles.append(QStringLiteral("prop-image"));
        entry.insert(
            QStringLiteral("roles"),
            std::move(roles));
        entry.insert(
            QStringLiteral("roomAssignments"),
            std::move(roomAssignments));
        entry.insert(
            QStringLiteral("propAssignments"),
            std::move(propAssignments));
        entries.append(std::move(entry));
    }

    QJsonObject roomAssignments;
    for (const std::string roomId : {
             std::string("atrium"),
             std::string("lounge"),
         }) {
        const auto assigned =
            authoringState.roomAssignments.find(roomId);
        roomAssignments.insert(
            QString::fromStdString(roomId),
            QString::fromStdString(
                assigned == authoringState.roomAssignments.end()
                ? std::string{} : assigned->second));
    }

    QJsonObject catalog;
    catalog.insert(QStringLiteral("version"), 1);
    catalog.insert(QStringLiteral("count"), entries.size());
    catalog.insert(
        QStringLiteral("sessionCount"),
        static_cast<qint64>(
            m_assetAuthoring.sessionCount()));
    catalog.insert(
        QStringLiteral("bundleLocked"),
        authoringState.bundleLocked);
    catalog.insert(
        QStringLiteral("roomAssignments"),
        std::move(roomAssignments));
    if (!authoringState.propAssignment.has_value()) {
        catalog.insert(
            QStringLiteral("propAssignment"),
            QJsonValue(QJsonValue::Null));
    } else {
        const palace::AssetAuthoringPropAssignmentV1& prop =
            *authoringState.propAssignment;
        QJsonObject encodedProp;
        encodedProp.insert(
            QStringLiteral("propId"),
            QString::fromStdString(prop.propId));
        encodedProp.insert(
            QStringLiteral("handle"),
            QString::fromStdString(prop.handle));
        encodedProp.insert(
            QStringLiteral("anchorX"),
            static_cast<qint64>(prop.anchorX));
        encodedProp.insert(
            QStringLiteral("anchorY"),
            static_cast<qint64>(prop.anchorY));
        encodedProp.insert(
            QStringLiteral("layer"),
            QString::fromStdString(prop.layer));
        catalog.insert(
            QStringLiteral("propAssignment"),
            std::move(encodedProp));
    }
    catalog.insert(QStringLiteral("assets"), std::move(entries));
    return QJsonDocument(catalog)
        .toJson(QJsonDocument::Compact)
        .toStdString();
}

std::string PalaceCoreImpl::reviewAsset(
    const std::string& handle,
    const std::string& decision)
{
    const auto publication =
        m_publicationStatus.find(handle);
    const bool publicationLocked =
        publication != m_publicationStatus.end()
        && (publication->second == "publishing"
            || publication->second.rfind(
                   "published;cid=", 0U) == 0U);
    const palace::AssetAuthoringAssetV1* asset =
        m_assetAuthoring.asset(handle);
    const std::string wantedReview =
        decision == "approve" ? "approved"
        : decision == "reject" ? "rejected"
        : std::string{};
    if (publicationLocked && asset != nullptr
        && asset->reviewState != wantedReview) {
        return "rejected=asset-review-locked";
    }
    const palace::AssetAuthoringResult result =
        m_assetAuthoring.review(handle, decision);
    if (!result.accepted)
        return "rejected=" + result.reason;
    return "ok;handle=" + handle
        + ";review=" + result.reason;
}

std::string PalaceCoreImpl::publishAsset(
    const std::string& handle)
{
    const palace::AssetAuthoringAssetV1* asset =
        m_assetAuthoring.asset(handle);
    if (asset == nullptr)
        return "rejected=asset-unknown";
    if (asset->reviewState != "approved")
        return "rejected=asset-not-approved";
    return publishVerifiedPng(handle);
}

std::string PalaceCoreImpl::assignRoomBackground(
    const std::string& roomId,
    const std::string& handle)
{
    const palace::AssetAuthoringResult result =
        m_assetAuthoring.assign(roomId, handle);
    if (!result.accepted)
        return "rejected=" + result.reason;
    return "ok;room=" + roomId + ";handle=" + handle;
}

std::string PalaceCoreImpl::assignPropAsset(
    const std::string& propId,
    const std::string& handle,
    std::uint32_t anchorX,
    std::uint32_t anchorY,
    const std::string& layer)
{
    const palace::AssetAuthoringResult result =
        m_assetAuthoring.assignProp(
            propId, handle, anchorX, anchorY, layer);
    if (!result.accepted)
        return "rejected=" + result.reason;
    return "ok;propId=" + propId
        + ";handle=" + handle
        + ";anchorX=" + std::to_string(anchorX)
        + ";anchorY=" + std::to_string(anchorY)
        + ";layer=" + layer;
}

std::string PalaceCoreImpl::publishMvpStorageBundle()
{
    drainStorageCallbacks();
    if (!isContextReady() || !m_storageSession.running()
        || !m_storageCatalog.hasConfiguration()) {
        return "rejected=storage-not-running";
    }
    if (m_storageMvpMode != "idle"
        && m_storageMvpMode != "publishing"
        && m_storageMvpMode != "verified") {
        return "rejected=storage-bundle-mode-" + m_storageMvpMode;
    }
    if (!m_assetAuthoring.ready()) {
        return "rejected=asset-state-unavailable";
    }
    if (m_storageMvpMode == "idle") {
        m_storageMvpPendingRoomBackgrounds.clear();
        m_storageMvpResolvedRoomBackgrounds.clear();
        if (!initializeStorageMvpBundle())
            return "rejected=storage-bundle-assets";
        if (!m_assetAuthoring.lockAssignments())
            return "rejected=asset-state-persistence";
        m_storageMvpMode = "publishing";
    }
    scheduleStorageMvpPublications();
    return "ok;" + mvpStorageBundleStatus();
}

std::string PalaceCoreImpl::mvpStorageBundleStatus()
{
    // Drain (and kick deferred peer-fetch dispatch) on status polls — never
    // from the initial fetchMvpStorageBundle return path (see below).
    drainStorageCallbacks();
    // Peer network downloads can hang forever after accept (GetProviders finds
    // a record but never delivers bytes). Cancel and retry stuck transfers so
    // the bundle can progress or degrade closed instead of spinning.
    if (m_storageMvpMode == "fetching"
        && m_storageMvpFetchSource.has_value()
        && *m_storageMvpFetchSource
            == palace::PalaceStorageMvpFetchSource::Network
        && !m_storageMvpTransfers.empty()) {
        const auto now = std::chrono::steady_clock::now();
        std::vector<std::string> timedOut;
        for (const auto& entry : m_storageMvpTransfers) {
            if (entry.second.purpose
                    != StorageMvpTransferPurpose::NetworkFetch
                || entry.second.moduleOperationId.empty()) {
                continue;
            }
            if (now - entry.second.startedAt
                < std::chrono::seconds(20)) {
                continue;
            }
            timedOut.push_back(entry.first);
        }
        for (const std::string& domainOperationId : timedOut) {
            const auto found =
                m_storageMvpTransfers.find(domainOperationId);
            if (found == m_storageMvpTransfers.end())
                continue;
            StorageMvpTransfer transfer = found->second;
            m_storageMvpTransfers.erase(found);
            (void)modules().storage_module.downloadCancelV2(
                transfer.moduleOperationId);
            // Re-open the catalog object for another fetch attempt.
            const palace::StorageCatalogTransition fetch =
                m_storageCatalog.beginLocalFetch(transfer.objectId);
            if (transfer.attempt < 8U
                && fetch.accepted
                && fetch.operation.has_value()
                && startStorageMvpCatalogDownload(
                    *fetch.operation,
                    false,
                    StorageMvpTransferPurpose::NetworkFetch,
                    transfer.attempt + 1U)) {
                continue;
            }
            m_storageMvpFailures[transfer.objectId] =
                "network-fetch-timeout";
            m_storageMvpMode = "degraded";
        }
    }
    // Keep pumping network fetches while idle-but-incomplete so a prior
    // dispatch miss does not leave the bundle stuck in fetching forever.
    if (m_storageMvpMode == "fetching"
        && m_storageMvpTransfers.empty()
        && m_storageMvpFailures.empty()
        && m_storageMvpFetchedObjects.size()
            < m_storageMvpBundle.artifactCount()) {
        (void)scheduleNextStorageMvpFetch();
    }
    if (!m_storageMvpFailures.empty()) {
        std::string firstReason = "unknown";
        std::string firstObject = "catalog";
        for (const auto& entry : m_storageMvpFailures) {
            firstObject = entry.first;
            firstReason = entry.second;
            break;
        }
        return "state=degraded;published="
            + std::to_string(m_storageMvpBundle.publishedCount())
            + ";verified="
            + std::to_string(m_storageMvpFetchedObjects.size())
            + ";total="
            + std::to_string(m_storageMvpBundle.artifactCount())
            + ";retention=degraded"
            + ";failed_object=" + firstObject
            + ";failed_reason=" + firstReason;
    }

    std::string state = "missing";
    if (m_storageMvpMode == "publishing")
        state = "fetching";
    if (m_storageMvpMode == "fetching")
        state = "fetching";
    if (m_storageMvpMode == "catalog-restored")
        state = "catalog-restored";
    if (m_storageMvpMode == "verified"
        || m_storageMvpMode == "retained") {
        state = "verified";
    }
    std::string retention = "missing";
    if (m_storageRetentionInProgress)
        retention = "fetching";
    else if (m_storageMvpRetainedObjects.size()
             == m_storageMvpBundle.artifactCount()
             && m_storageMvpBundle.complete()) {
        retention = "verified";
    }

    std::string status =
        "state=" + state
        + ";published="
        + std::to_string(m_storageMvpBundle.publishedCount())
        + ";verified="
        + std::to_string(m_storageMvpFetchedObjects.size())
        + ";total="
        + std::to_string(m_storageMvpBundle.artifactCount())
        + ";retention=" + retention
        + ";retention_round="
        + std::to_string(m_storageRetentionRound)
        + ";source="
        + (m_storageMvpFetchSource.has_value()
               ? std::string(
                     palace::palaceStorageMvpFetchSourceName(
                         *m_storageMvpFetchSource))
               : std::string("none"))
        + ";native_available="
        + std::to_string(m_storageMvpNativeAvailableCount)
        + ";native_total="
        + std::to_string(m_storageMvpNativeTotalCount);
    if (m_storageMvpBundle.complete()) {
        const std::string catalog =
            m_storageMvpBundle.canonicalCatalog();
        if (!catalog.empty())
            status += ";catalog=" + base64Url(catalog);
    }
    return status;
}

std::string PalaceCoreImpl::fetchMvpStorageBundle(
    const std::string& catalogBase64)
{
    drainStorageCallbacks();
    if (!isContextReady() || !m_storageSession.running()
        || !m_storageCatalog.hasConfiguration()) {
        return "rejected=storage-not-running";
    }
    if (m_storageMvpMode != "idle")
        return "rejected=storage-bundle-mode-" + m_storageMvpMode;
    const auto catalog = decodeBase64Url(
        catalogBase64, 16U * 1024U);
    if (!catalog.has_value()
        || !m_storageMvpBundle.restoreCanonicalCatalog(
            *catalog)) {
        return "rejected=storage-catalog-invalid";
    }
    clearStorageMvpRuntimeState();
    std::string reason;
    if (!beginStorageMvpFetch(reason))
        return "rejected=" + reason;
    // Do not call mvpStorageBundleStatus() here: it drains and would run the
    // deferred downloadToUrlV2 on this same invoke, defeating the deferral.
    return "ok;state=fetching;published="
        + std::to_string(m_storageMvpBundle.publishedCount())
        + ";verified="
        + std::to_string(m_storageMvpFetchedObjects.size())
        + ";total="
        + std::to_string(m_storageMvpBundle.artifactCount())
        + ";retention=missing"
        + ";retention_round="
        + std::to_string(m_storageRetentionRound)
        + ";source="
        + (m_storageMvpFetchSource.has_value()
               ? std::string(
                     palace::palaceStorageMvpFetchSourceName(
                         *m_storageMvpFetchSource))
               : std::string("none"))
        + ";native_available="
        + std::to_string(m_storageMvpNativeAvailableCount)
        + ";native_total="
        + std::to_string(m_storageMvpNativeTotalCount);
}

std::string PalaceCoreImpl::verifyMvpStorageRetention()
{
    drainStorageCallbacks();
    if (!isContextReady() || !m_storageSession.running()
        || !m_storageMvpBundle.complete()
        || m_storageMvpFetchedObjects.size()
            != m_storageMvpBundle.artifactCount()
        || !m_storageMvpTransfers.empty()) {
        return "rejected=storage-bundle-not-verified";
    }
    if (m_storageRetentionInProgress)
        return "ok;" + mvpStorageBundleStatus();
    if (m_storageRetentionRound
        == std::numeric_limits<std::uint64_t>::max()) {
        return "rejected=storage-retention-round-exhausted";
    }

    const std::vector<palace::PalaceStorageMvpArtifactV1> artifacts =
        m_storageMvpBundle.artifacts();
    for (const palace::PalaceStorageMvpArtifactV1& artifact
         : artifacts) {
        const StdLogosResult exists =
            modules().storage_module.exists(artifact.cid);
        if (!exists.success
            || !exists.value.is_boolean()
            || !exists.value.get<bool>()) {
            m_storageMvpFailures[artifact.objectId] =
                "local-exists-failed";
            m_storageMvpMode = "degraded";
            return "rejected=storage-retention-exists";
        }
    }

    ++m_storageRetentionRound;
    m_storageMvpRetainedObjects.clear();
    // Co-located materialize: never re-enter downloadToUrlV2 for retention.
    // BOnEa1w7 hung gate3VerifyRetention after verified=8 because local
    // verification downloads freeze after write. Re-check exists (above) plus
    // in-memory/materialized digests that already passed peer fetch.
    if (m_storageMvpColocatedMaterialized) {
        for (const palace::PalaceStorageMvpArtifactV1& artifact
             : artifacts) {
            std::string bytes = artifact.bytes;
            if (bytes.empty()
                && !loadColocatedMaterializedObjectBytes(
                    artifact, bytes)) {
                m_storageMvpFailures[artifact.objectId] =
                    "local-retention-materialized-missing";
                m_storageMvpMode = "degraded";
                return "rejected=storage-retention-materialized";
            }
            if (bytes.size()
                    != artifact.specification.byteLength
                || palace::crypto::sha256Hex(bytes)
                    != artifact.specification.contentSha256
                || (!artifact.bytes.empty()
                    && bytes != artifact.bytes)) {
                m_storageMvpFailures[artifact.objectId] =
                    "local-retention-verification-failed";
                m_storageMvpMode = "degraded";
                return "rejected=storage-retention-bytes";
            }
            m_storageMvpRetainedObjects.insert(artifact.objectId);
        }
        if (m_storageMvpRetainedObjects.size()
            == m_storageMvpBundle.artifactCount()) {
            m_storageMvpMode = "retained";
        }
        return "ok;" + mvpStorageBundleStatus();
    }
    m_storageRetentionInProgress = true;
    std::uint64_t sequence = 0U;
    for (const palace::PalaceStorageMvpArtifactV1& artifact
         : artifacts) {
        const std::string operationId =
            "palace-retain-"
            + std::to_string(m_storageRetentionRound)
            + "-" + std::to_string(++sequence);
        const std::string path =
            storageDownloadPath(operationId);
        if (path.empty()) {
            m_storageMvpFailures[artifact.objectId] =
                "retention-path";
            m_storageMvpMode = "degraded";
            m_storageRetentionInProgress = false;
            return "rejected=storage-retention-path";
        }
        const palace::StorageModuleSessionTransition verify =
            m_storageSession.beginLocalVerification(
                operationId,
                artifact.cid,
                path,
                artifact.specification.byteLength,
                65536U);
        if (!verify.accepted) {
            m_storageMvpFailures[artifact.objectId] =
                verify.reason;
            m_storageMvpMode = "degraded";
            m_storageRetentionInProgress = false;
            return "rejected=storage-retention-dispatch";
        }
        m_storageMvpTransfers.emplace(
            operationId,
            StorageMvpTransfer{
                StorageMvpTransferPurpose::RetentionVerification,
                artifact.objectId,
                path,
                m_storageRetentionRound,
            });
        executeStorageCommands(verify.commands);
    }
    return "ok;" + mvpStorageBundleStatus();
}

std::string PalaceCoreImpl::storageObjectStatus(
    const std::string& objectId)
{
    drainStorageCallbacks();
    const auto failure = m_storageMvpFailures.find(objectId);
    if (failure != m_storageMvpFailures.end()) {
        return "state=degraded;reason=" + failure->second;
    }
    const palace::StorageCatalogObjectStatus status =
        m_storageCatalog.status(
            objectId,
            static_cast<std::uint64_t>(
                std::max<std::int64_t>(0, deliveryNowSeconds())));
    if (!status.found)
        return "state=missing";
    std::string state =
        palace::storageCatalogLocalPhaseName(status.localPhase);
    std::string retention = "missing";
    if (m_storageMvpRetainedObjects.find(objectId)
        != m_storageMvpRetainedObjects.end()) {
        retention = "verified";
    } else if (m_storageRetentionInProgress) {
        const bool pending = std::any_of(
            m_storageMvpTransfers.begin(),
            m_storageMvpTransfers.end(),
            [&objectId](const auto& entry) {
                return entry.second.purpose
                        == StorageMvpTransferPurpose::
                            RetentionVerification
                    && entry.second.objectId == objectId;
            });
        if (pending)
            retention = "fetching";
    }
    return "state=" + state
        + ";publication="
        + palace::storageCatalogPublicationStageName(
            status.publicationStage)
        + ";retention=" + retention
        + ";cid=" + status.cid;
}

std::string PalaceCoreImpl::roomTitle() const
{
    return m_projection.currentRoomTitle();
}

std::string PalaceCoreImpl::roomBackgroundHandle() const
{
    if (m_storageMvpCatalogStale
        || m_storageMvpMode == "catalog-restored"
        || m_storageMvpMode == "fetching"
        || m_storageMvpMode == "degraded") {
        return {};
    }
    const auto resolved =
        m_storageMvpResolvedRoomBackgrounds.find(
            m_projection.currentRoomId());
    if (resolved
        != m_storageMvpResolvedRoomBackgrounds.end()) {
        return resolved->second;
    }
    return m_assetAuthoring.handleForRoom(m_projection.currentRoomId());
}

std::string PalaceCoreImpl::activePropAsset() const
{
    QJsonObject encoded;
    encoded.insert(QStringLiteral("version"), 1);
    std::optional<palace::PalaceStorageMvpPropAssetV1>
        prop;
    if (m_storageMvpMode == "verified"
        || m_storageMvpMode == "retained") {
        prop = m_storageMvpBundle.propAsset();
    }
    if (prop.has_value()) {
        const palace::PalaceStorageMvpArtifactV1* manifest =
            m_storageMvpBundle.artifact(
                m_storageMvpBundle.propManifestObjectId());
        if (manifest == nullptr
            || m_deliveryAuthority.isAssetBanned(
                manifest->cid, m_projection.currentRoomId())) {
            prop.reset();
        }
    }
    if (!prop.has_value()) {
        encoded.insert(
            QStringLiteral("available"), false);
        return QJsonDocument(encoded)
            .toJson(QJsonDocument::Compact)
            .toStdString();
    }
    encoded.insert(QStringLiteral("available"), true);
    encoded.insert(
        QStringLiteral("propId"),
        QString::fromStdString(prop->propId));
    encoded.insert(
        QStringLiteral("handle"),
        QString::fromStdString(prop->handle));
    encoded.insert(
        QStringLiteral("contentSha256"),
        QString::fromStdString(prop->handle));
    encoded.insert(
        QStringLiteral("width"),
        static_cast<qint64>(prop->width));
    encoded.insert(
        QStringLiteral("height"),
        static_cast<qint64>(prop->height));
    encoded.insert(
        QStringLiteral("anchorX"),
        static_cast<qint64>(prop->anchorX));
    encoded.insert(
        QStringLiteral("anchorY"),
        static_cast<qint64>(prop->anchorY));
    encoded.insert(
        QStringLiteral("layer"),
        QString::fromStdString(prop->layer));
    return QJsonDocument(encoded)
        .toJson(QJsonDocument::Compact)
        .toStdString();
}

std::string PalaceCoreImpl::syncHealth() const
{
    return palace::syncHealthName(m_projection.syncHealth());
}

std::string PalaceCoreImpl::localProjection() const
{
    return m_projection.canonicalLocalState();
}

std::string PalaceCoreImpl::banUser(
    const std::string& subjectUserIdHex)
{
    return submitHumanModeration(
        palace::PalaceHumanModerationTargetV1::User,
        subjectUserIdHex);
}

std::string PalaceCoreImpl::banProp(const std::string& propId)
{
    return submitHumanModeration(
        palace::PalaceHumanModerationTargetV1::AssetCid,
        propId);
}

std::string PalaceCoreImpl::moderationStatus() const
{
    if (!m_lezSubmissionIntent.has_value()) {
        return "state=idle;kind=;action=;target=;ban_id=";
    }

    std::string kind;
    std::string target;
    std::string banIdHex;
    if (const auto* user =
            std::get_if<palace::PalaceLezCreateUserBanV3>(
                &m_lezSubmissionIntent->plan.instruction.payload);
        user != nullptr) {
        kind = "user";
        target = palace::PalaceLezCodec::bytes32Hex(
            user->subjectUserId);
        banIdHex =
            palace::PalaceLezCodec::bytes32Hex(user->banId);
    } else if (const auto* asset =
                   std::get_if<
                       palace::PalaceLezCreateAssetBanV3>(
                       &m_lezSubmissionIntent
                            ->plan.instruction.payload);
               asset != nullptr) {
        kind = "prop";
        target = asset->cid;
        banIdHex =
            palace::PalaceLezCodec::bytes32Hex(asset->banId);
    } else {
        return "state=idle;kind=;action=;target=;ban_id=";
    }

    const palace::ActionStatus status =
        m_actionJournal.status(m_lezSubmissionIntent->actionId);
    std::string state = "pending";
    if (status.durableStage
        == palace::DurableActionStage::Finalized) {
        state = "finalized";
    } else if (status.durableStage
                   == palace::DurableActionStage::Rejected
               || status.durableStage
                   == palace::DurableActionStage::Expired
               || status.durableStage
                   == palace::DurableActionStage::Orphaned
               || m_lezSubmissionIntent->phase
                   == palace::PalaceLezSubmissionIntentPhase::
                       Rejected) {
        state = "rejected";
    }
    return "state=" + state + ";kind=" + kind
        + ";action=" + m_lezSubmissionIntent->actionId
        + ";target=" + target + ";ban_id=" + banIdHex
        + ";durable="
        + palace::actionStatusName(status.durableStage);
}

std::string PalaceCoreImpl::submitHumanModeration(
    const palace::PalaceHumanModerationTargetV1 targetKind,
    const std::string& selectedTarget)
{
    static constexpr std::uint32_t kModerateUser = 1U << 0U;
    static constexpr std::uint32_t kModerateAsset = 1U << 1U;

    if (!isContextReady() || !m_lezReady
        || !m_lezCoordinator.running()) {
        return "rejected=moderation-lez-not-ready";
    }
    if (!m_lezAuthorityReady
        || !m_lezOpenHistory.has_value()
        || !m_lezOpenHistory->authorityApplied
        || m_lezOpenHistory->palaceIdHex
            != m_deliveryAuthority.palaceId()) {
        return "rejected=moderation-finalized-authority-required";
    }
    if (!m_deliveryIdentity.valid()
        || m_deliveryAuthority.deliveryKeyFor(
               m_deliveryIdentity.accountId(),
               m_deliveryIdentity.deliveryKeyEpoch())
            != m_deliveryIdentity.publicKey()) {
        return "rejected=moderation-finalized-identity-required";
    }
    if (m_lezAuthorityBundle.checkpoint.lastOrderedActionId
        == std::numeric_limits<std::uint64_t>::max()) {
        return "rejected=moderation-action-id-exhausted";
    }

    const std::uint64_t nextActionId =
        m_lezAuthorityBundle.checkpoint.lastOrderedActionId + 1U;
    const std::string actionId = std::to_string(nextActionId);
    const std::string callerAccountIdHex =
        m_deliveryIdentity.accountId();
    palace::PalaceLezBytes32 callerAccountId{};
    if (!palace::PalaceLezCodec::parseBytes32Hex(
            callerAccountIdHex, callerAccountId)) {
        return "rejected=moderation-caller-invalid";
    }

    std::string target = selectedTarget;
    if (targetKind
        == palace::PalaceHumanModerationTargetV1::AssetCid) {
        if (selectedTarget
            != m_storageMvpBundle.propId())
            return "rejected=moderation-prop-unknown";
        const palace::PalaceStorageMvpArtifactV1* prop =
            m_storageMvpBundle.artifact(
                m_storageMvpBundle.propManifestObjectId());
        if (!m_storageMvpBundle.complete() || prop == nullptr
            || !palace::isSafePalaceCid(prop->cid)) {
            return "rejected=moderation-prop-unavailable";
        }
        target = prop->cid;
        if (m_deliveryAuthority.isAssetBanned(target, {}))
            return "rejected=moderation-prop-already-banned";
    } else {
        if (!isNonzeroLowerHexAccountId(selectedTarget))
            return "rejected=moderation-user-invalid";
        if (m_deliveryAuthority.isUserBanned(selectedTarget, {}))
            return "rejected=moderation-user-already-banned";
    }

    std::optional<palace::PalaceLezRootRecordV3> root;
    std::vector<palace::PalaceLezCapabilityGrantRecordV3>
        eligibleGrants;
    bool targetUserKnown =
        targetKind
        != palace::PalaceHumanModerationTargetV1::User;
    const std::uint32_t requiredCapability =
        targetKind
            == palace::PalaceHumanModerationTargetV1::User
        ? kModerateUser : kModerateAsset;
    for (const palace::PalaceLezFinalizedAuthorityAccountV1&
         stored : m_lezAuthorityBundle.accounts) {
        const palace::PalaceLezPublicAccountV3 decoded =
            palace::PalaceLezCodec::decodePublicAccount(
                stored.responseJson,
                m_lezAuthorityBundle.scope.programIdHex);
        if (!decoded.accepted)
            return "rejected=moderation-authority-account-invalid";
        if (stored.accountIdHex
            == m_lezAuthorityBundle.scope.rootAccountIdHex) {
            const auto* decodedRoot =
                std::get_if<palace::PalaceLezRootRecordV3>(
                    &decoded.record);
            if (decodedRoot == nullptr || root.has_value())
                return "rejected=moderation-root-invalid";
            root = *decodedRoot;
            continue;
        }
        if (const auto* profile =
                std::get_if<palace::PalaceLezUserProfileRecordV3>(
                    &decoded.record);
            profile != nullptr) {
            if (targetKind
                    == palace::PalaceHumanModerationTargetV1::User
                && palace::PalaceLezCodec::bytes32Hex(
                       profile->userId)
                    == target) {
                targetUserKnown = true;
            }
            continue;
        }
        const auto* grant =
            std::get_if<
                palace::PalaceLezCapabilityGrantRecordV3>(
                &decoded.record);
        if (grant != nullptr
            && grant->subjectUserId == callerAccountId
            && !grant->revoked
            && grant->validThroughActionId >= nextActionId
            && grant->scope.kind
                == palace::PalaceLezScopeKindV3::Palace
            && (grant->capabilities & requiredCapability)
                == requiredCapability) {
            eligibleGrants.push_back(*grant);
        }
    }
    if (!root.has_value()
        || root->lastOrderedActionId
            != m_lezAuthorityBundle.checkpoint
                   .lastOrderedActionId) {
        return "rejected=moderation-root-checkpoint-mismatch";
    }
    if (!targetUserKnown)
        return "rejected=moderation-user-unknown";
    if (targetKind
            == palace::PalaceHumanModerationTargetV1::User
        && palace::PalaceLezCodec::bytes32Hex(root->owner)
            == target) {
        return "rejected=moderation-owner-protected";
    }
    eligibleGrants.erase(
        std::remove_if(
            eligibleGrants.begin(),
            eligibleGrants.end(),
            [&root](const auto& grant) {
                return grant.palaceId != root->palaceId
                    || grant.issuedBy != root->owner;
            }),
        eligibleGrants.end());
    if (eligibleGrants.size() != 1U)
        return "rejected=moderation-grant-not-unique";

    palace::PalaceHumanModerationRequestV1 request;
    request.targetKind = targetKind;
    request.actionId = actionId;
    request.programIdHex =
        m_lezAuthorityBundle.scope.programIdHex;
    request.rootAccountIdHex =
        m_lezAuthorityBundle.scope.rootAccountIdHex;
    request.issuerAccountIdHex = callerAccountIdHex;
    request.grantIdHex = palace::PalaceLezCodec::bytes32Hex(
        eligibleGrants.front().grantId);
    request.target = target;
    const palace::PalaceHumanModerationCommandV1 command =
        palace::buildPalaceHumanModerationCommandV1(request);
    if (!command.accepted)
        return "rejected=moderation-command;reason="
            + command.reason;

    const std::string queued = submitIntent(command.actionId);
    const palace::ActionStatus queuedStatus =
        m_actionJournal.status(command.actionId);
    if (queuedStatus.durableStage
        != palace::DurableActionStage::Queued) {
        return "rejected=moderation-action-queue;"
            + queued + ";moderation="
            + (targetKind
                    == palace::PalaceHumanModerationTargetV1::User
                ? std::string("user") : std::string("prop"))
            + ";action=" + command.actionId
            + ";ban_id=" + command.banIdHex
            + ";transition_sha256="
            + command.transitionSha256Hex;
    }

    return submitPalaceTransition(
               command.actionId,
               request.rootAccountIdHex,
               request.issuerAccountIdHex,
               request.programIdHex,
               command.transitionJson)
        + ";moderation="
        + (targetKind
                == palace::PalaceHumanModerationTargetV1::User
            ? std::string("user") : std::string("prop"))
        + ";action=" + command.actionId
        + ";ban_id=" + command.banIdHex
        + ";transition_sha256="
        + command.transitionSha256Hex;
}

std::string PalaceCoreImpl::submitIntent(const std::string& actionId)
{
    std::uint64_t orderedActionId = 0;
    if (actionId != "0"
        && !palace::PalaceLezCodec::parseOrderedActionId(
            actionId, orderedActionId)) {
        return "rejected=invalid-ordered-action-id";
    }
    palace::ActionJournal candidate = m_actionJournal;
    const bool changed =
        candidate.createDraft(actionId)
        && candidate.queue(actionId);
    if (!changed)
        return result(false, m_actionJournal.status(actionId));
    if (!m_actionJournalStore
        || !m_actionJournalStore->save(candidate)) {
        return "rejected=action-journal-save";
    }
    m_actionJournal = std::move(candidate);
    return result(true, m_actionJournal.status(actionId));
}

bool PalaceCoreImpl::recoverPalaceSubmissionIntent(
    const palace::PalaceLezSubmissionIntentV1& intent,
    std::string& reason)
{
    reason.clear();
    const palace::DeliveryIdentityRegistrationRecoveryV1 recovered =
        lookupFinalizedLezSubmission(
            "transition:" + intent.actionId,
            submissionRecoveryExpectation(intent));
    if (recovered.outcome
        == palace::DeliveryIdentityRegistrationRecoveryOutcome::
            Pending) {
        reason = recovered.reason.empty()
            ? "recovery-pending" : recovered.reason;
        return false;
    }
    if (recovered.outcome
        == palace::DeliveryIdentityRegistrationRecoveryOutcome::
            Rejected) {
        reason = recovered.reason.empty()
            ? "recovery-rejected" : recovered.reason;
        return false;
    }

    const palace::PalaceLezCoordinatorUpdate coordinated =
        m_lezCoordinator.registerRecoveredSubmission(
            intent.plan,
            recovered.transactionHash,
            intent.expectedRootDataSha256Hex);
    if (!coordinated.accepted) {
        reason = "coordinator-" + coordinated.reason;
        return false;
    }
    if (!m_lezCoordinatorStore
        || m_lezCoordinatorStore->save(m_lezCoordinator)
            != palace::PalaceLezCoordinatorStoreStatus::Saved) {
        m_lezCoordinatorStoreHealthy = false;
        reason = "coordinator-save";
        return false;
    }
    m_lezCoordinatorStoreHealthy = true;

    palace::ActionJournal candidate = m_actionJournal;
    const palace::ActionStatus current =
        candidate.status(intent.actionId);
    if (current.durableStage
        == palace::DurableActionStage::Queued) {
        if (!candidate.markSubmittedToLez(
                intent.actionId,
                recovered.transactionHash)
            || !m_actionJournalStore
            || !m_actionJournalStore->save(candidate)) {
            reason = "action-journal-save";
            return false;
        }
        m_actionJournal = std::move(candidate);
    } else if ((current.durableStage
                    != palace::DurableActionStage::SubmittedToLez
                && current.durableStage
                    != palace::DurableActionStage::Observed
                && current.durableStage
                    != palace::DurableActionStage::Finalized)
               || current.transactionHash
                    != recovered.transactionHash) {
        reason = "action-journal-conflict";
        return false;
    }

    palace::PalaceLezSubmissionIntentV1 committed = intent;
    committed.phase =
        palace::PalaceLezSubmissionIntentPhase::Committed;
    committed.transactionHash = recovered.transactionHash;
    if (!m_lezSubmissionIntentStore
        || m_lezSubmissionIntentStore->save(committed)
            != palace::PalaceLezSubmissionIntentStoreStatus::Saved) {
        reason = "submission-intent-commit-save";
        return false;
    }
    m_lezSubmissionIntent = std::move(committed);
    if (m_lezPendingSubmissionRecovery.has_value()
        && m_lezPendingSubmissionRecovery->key
            == "transition:" + intent.actionId) {
        m_lezPendingSubmissionRecovery.reset();
    }
    m_lezFinalityReasons[intent.actionId] =
        "recovered-finalized-submission";
    reason = "recovered";
    return true;
}

bool PalaceCoreImpl::repairTrackedPalaceSubmissionIntent(
    const palace::PalaceLezTransactionPlanV3& requestedPlan,
    std::string& transactionHash,
    std::string& reason)
{
    transactionHash.clear();
    reason.clear();
    if (!m_lezSubmissionIntent.has_value()) {
        reason = "tracked-submission-not-found";
        return false;
    }
    palace::core_detail::PalaceLezTrackedSubmissionRecoveryResultV1
        recovered =
            palace::core_detail::recoverTrackedPalaceSubmissionV1(
                m_lezSubmissionIntent->actionId,
                requestedPlan,
                *m_lezSubmissionIntent,
                m_lezCoordinator.transactions(),
                m_actionJournal);
    if (!recovered.accepted) {
        reason = std::move(recovered.reason);
        return false;
    }

    if (palace::core_detail::persistTrackedPalaceSubmissionV1(
            m_lezCoordinator,
            m_lezCoordinatorStore.get())
        != palace::PalaceLezCoordinatorStoreStatus::Saved) {
        m_lezCoordinatorStoreHealthy = false;
        reason = "tracked-submission-coordinator-save";
        return false;
    }
    m_lezCoordinatorStoreHealthy = true;

    if (!m_actionJournalStore
        || !m_actionJournalStore->save(
            recovered.repairedJournal)) {
        reason = "tracked-submission-action-journal-save";
        return false;
    }
    m_actionJournal = std::move(recovered.repairedJournal);

    if (!m_lezSubmissionIntentStore
        || m_lezSubmissionIntentStore->save(
            recovered.committedIntent)
            != palace::PalaceLezSubmissionIntentStoreStatus::Saved) {
        reason = "tracked-submission-intent-save";
        return false;
    }
    transactionHash = recovered.transactionHash;
    m_lezSubmissionIntent =
        std::move(recovered.committedIntent);
    if (m_lezPendingSubmissionRecovery.has_value()
        && m_lezPendingSubmissionRecovery->key
            == "transition:"
                + m_lezSubmissionIntent->actionId) {
        m_lezPendingSubmissionRecovery.reset();
    }
    m_lezFinalityReasons[m_lezSubmissionIntent->actionId] =
        recovered.reason;
    reason = std::move(recovered.reason);
    return true;
}

bool PalaceCoreImpl::commitTrackedPalaceSubmissionIntent(
    const std::string& actionId,
    const std::string& transactionHash,
    std::string& reason)
{
    reason.clear();
    if (!m_lezSubmissionIntent.has_value()
        || m_lezSubmissionIntent->actionId != actionId) {
        reason = "submission-intent-not-matching";
        return false;
    }
    if (m_lezSubmissionIntent->phase
        == palace::PalaceLezSubmissionIntentPhase::Committed) {
        if (m_lezSubmissionIntent->transactionHash
            != transactionHash) {
            reason = "submission-intent-hash-conflict";
            return false;
        }
        reason = "already-committed";
        return true;
    }
    if (m_lezSubmissionIntent->phase
        != palace::PalaceLezSubmissionIntentPhase::
            MayHaveBeenSubmitted) {
        reason = "submission-intent-stage-mismatch";
        return false;
    }
    const auto tracked =
        trackedLezTransaction(transactionHash);
    if (!tracked.has_value()
        || tracked->expectedRootDataSha256Hex
            != m_lezSubmissionIntent
                ->expectedRootDataSha256Hex
        || tracked->plan.programIdHex
            != m_lezSubmissionIntent->plan.programIdHex
        || tracked->plan.rootAccountIdHex
            != m_lezSubmissionIntent->plan.rootAccountIdHex
        || tracked->plan.accountIdsHex
            != m_lezSubmissionIntent->plan.accountIdsHex
        || tracked->plan.signingRequirements
            != m_lezSubmissionIntent
                ->plan.signingRequirements
        || tracked->plan.instructionWords
            != m_lezSubmissionIntent->plan.instructionWords) {
        reason = "submission-intent-coordinator-conflict";
        return false;
    }

    palace::PalaceLezSubmissionIntentV1 committed =
        *m_lezSubmissionIntent;
    committed.phase =
        palace::PalaceLezSubmissionIntentPhase::Committed;
    committed.transactionHash = transactionHash;
    if (!m_lezSubmissionIntentStore
        || m_lezSubmissionIntentStore->save(committed)
            != palace::PalaceLezSubmissionIntentStoreStatus::Saved) {
        reason = "submission-intent-commit-save";
        return false;
    }
    m_lezSubmissionIntent = std::move(committed);
    reason = "committed";
    return true;
}

std::string PalaceCoreImpl::submitPalaceTransition(const std::string& actionId,
                                                    const std::string& stateAccountIdHex,
                                                    const std::string& callerAccountIdHex,
                                                    const std::string& programIdHex,
                                                    const std::string& transitionJson)
{
    if (!isContextReady() || !m_lezReady
        || !m_lezCoordinator.running())
        return "rejected=lez-not-ready";

    const palace::PalaceLezIntentParseResult parsed =
        palace::PalaceLezIntentParser::parse(actionId, transitionJson);
    if (!parsed.accepted)
        return "rejected=invalid-palace-transition;reason=" + parsed.reason;
    const palace::PalaceLezTransactionPlanV3 plan =
        palace::PalaceLezCodec::buildTransaction(
            programIdHex, callerAccountIdHex, parsed.instruction);
    if (!plan.accepted)
        return "rejected=invalid-palace-plan;reason=" + plan.reason;
    if (stateAccountIdHex != plan.rootAccountIdHex)
        return "rejected=palace-root-account-mismatch";
    const palace::ActionStatus initialActionStatus =
        m_actionJournal.status(actionId);
    if (initialActionStatus.durableStage
        != palace::DurableActionStage::Queued) {
        if (m_lezSubmissionIntent.has_value()
            && m_lezSubmissionIntent->actionId == actionId) {
            palace::PalaceLezSubmissionIntentV1 request =
                *m_lezSubmissionIntent;
            request.phase =
                palace::PalaceLezSubmissionIntentPhase::Prepared;
            request.transactionHash.clear();
            request.plan = plan;
            if (sameSubmissionIntentPayload(
                    request, *m_lezSubmissionIntent)) {
                std::string commitReason;
                if (commitTrackedPalaceSubmissionIntent(
                        actionId,
                        initialActionStatus.transactionHash,
                        commitReason)) {
                    return "ok;tx_hash="
                        + initialActionStatus.transactionHash + ";"
                        + palace::canonicalActionStatus(
                            initialActionStatus);
                }
            }
        }
        return "rejected=action-not-queued";
    }

    if (m_lezSubmissionIntent.has_value()
        && m_lezSubmissionIntent->actionId == actionId
        && m_lezSubmissionIntent->phase
            == palace::PalaceLezSubmissionIntentPhase::
                MayHaveBeenSubmitted) {
        std::string recoveredTransactionHash;
        std::string recoveryReason;
        if (repairTrackedPalaceSubmissionIntent(
                plan,
                recoveredTransactionHash,
                recoveryReason)) {
            return "ok;tx_hash="
                + recoveredTransactionHash + ";"
                + palace::canonicalActionStatus(
                    m_actionJournal.status(actionId));
        }
        if (recoveryReason
            != "tracked-submission-not-found") {
            return "rejected=lez-submit-intent-repair;reason="
                + recoveryReason;
        }
    }

    const std::vector<palace::PalaceLezTrackedTransaction>
        trackedTransactions = m_lezCoordinator.transactions();
    const bool unresolvedRootTransaction = std::any_of(
        trackedTransactions.begin(),
        trackedTransactions.end(),
        [&plan](const palace::PalaceLezTrackedTransaction& tracked) {
            return tracked.plan.rootAccountIdHex
                    == plan.rootAccountIdHex
                && tracked.stage
                    != palace::PalaceLezTransactionStage::Finalized;
        });
    if (unresolvedRootTransaction)
        return "rejected=lez-root-transaction-pending";

    palace::PalaceLezExpectedRootV3 expected;
    const bool initializing =
        std::holds_alternative<palace::PalaceLezInitializeV3>(
            parsed.instruction.payload);
    if (initializing) {
        if (m_lezAuthorityReady)
            return "rejected=lez-palace-already-initialized";
        expected = palace::PalaceLezCodec::expectedInitialRoot(
            callerAccountIdHex, parsed.instruction);
    } else {
        std::uint64_t orderedActionId = 0U;
        if (!palace::PalaceLezCodec::parseOrderedActionId(
                actionId, orderedActionId)
            || !m_lezAuthorityReady
            || m_lezAuthorityBundle.checkpoint.lastOrderedActionId
                == std::numeric_limits<std::uint64_t>::max()
            || orderedActionId
                != m_lezAuthorityBundle.checkpoint
                        .lastOrderedActionId
                    + 1U) {
            return "rejected=lez-authority-history-rebuild-required";
        }
        std::string syncReason;
        if (!syncLezWalletToCurrent(syncReason))
            return "rejected=lez-sync;reason=" + syncReason;
        logos::CallError accountError;
        const std::string currentResponse =
            modules().lez_core.get_account_public(
                plan.rootAccountIdHex, &accountError);
        if (!accountError.ok())
            return "rejected=lez-current-root-call";
        const palace::PalaceLezPublicAccountV3 current =
            palace::PalaceLezCodec::decodePublicAccount(
                currentResponse, programIdHex);
        const palace::PalaceLezRootRecordV3* root =
            current.accepted
            ? std::get_if<palace::PalaceLezRootRecordV3>(&current.record)
            : nullptr;
        if (root == nullptr)
            return "rejected=lez-current-root;reason=" + current.reason;
        expected = palace::PalaceLezCodec::expectedAdvancedRoot(
            *root, parsed.instruction);
    }
    if (!expected.accepted)
        return "rejected=lez-root-prediction;reason=" + expected.reason;

    std::string intentSyncReason;
    if (!syncLezWalletToCurrent(intentSyncReason)
        || m_lezSyncedHeight < 0) {
        return "rejected=lez-submit-sync;reason="
            + intentSyncReason;
    }

    palace::PalaceLezSubmissionIntentV1 prepared;
    prepared.actionId = actionId;
    prepared.phase =
        palace::PalaceLezSubmissionIntentPhase::Prepared;
    prepared.minimumFinalizedBlockExclusive =
        static_cast<std::uint64_t>(m_lezSyncedHeight);
    prepared.plan = plan;
    prepared.expectedRootDataSha256Hex =
        expected.dataSha256Hex;

    if (m_lezSubmissionIntent.has_value()
        && (m_lezSubmissionIntent->phase
                == palace::PalaceLezSubmissionIntentPhase::Prepared
            || m_lezSubmissionIntent->phase
                == palace::PalaceLezSubmissionIntentPhase::
                    MayHaveBeenSubmitted)) {
        prepared.minimumFinalizedBlockExclusive =
            m_lezSubmissionIntent
                ->minimumFinalizedBlockExclusive;
        if (!sameSubmissionIntentPayload(
                prepared, *m_lezSubmissionIntent)) {
            return "rejected=lez-submit-intent-conflict";
        }
        if (m_lezSubmissionIntent->phase
            == palace::PalaceLezSubmissionIntentPhase::
                MayHaveBeenSubmitted) {
            std::string recoveryReason;
            if (!recoverPalaceSubmissionIntent(
                    *m_lezSubmissionIntent,
                    recoveryReason)) {
                return "rejected=lez-submit-recovery-pending;reason="
                    + recoveryReason;
            }
            const palace::ActionStatus recovered =
                m_actionJournal.status(actionId);
            return "ok;tx_hash=" + recovered.transactionHash + ";"
                + palace::canonicalActionStatus(recovered);
        }
    }
    if (m_lezSubmissionIntent.has_value()
        && m_lezSubmissionIntent->phase
            == palace::PalaceLezSubmissionIntentPhase::Committed
        && m_lezSubmissionIntent->actionId == actionId) {
        return "rejected=lez-submit-intent-committed-conflict";
    }
    if (!m_lezSubmissionIntentStore) {
        return "rejected=lez-submit-intent-store-unavailable";
    }
    const palace::PalaceLezSubmissionIntentStoreStatus preparedSaved =
        m_lezSubmissionIntentStore->save(prepared);
    if (preparedSaved
        != palace::PalaceLezSubmissionIntentStoreStatus::Saved) {
        return "rejected=lez-submit-intent-prepare-save;reason="
            + std::string(
                palace::palaceLezSubmissionIntentStoreStatusName(
                    preparedSaved));
    }
    m_lezSubmissionIntent = prepared;

    palace::PalaceLezSubmissionIntentV1 mayHaveSubmitted =
        prepared;
    mayHaveSubmitted.phase =
        palace::PalaceLezSubmissionIntentPhase::
            MayHaveBeenSubmitted;
    const palace::PalaceLezSubmissionIntentStoreStatus boundarySaved =
        m_lezSubmissionIntentStore->save(mayHaveSubmitted);
    if (boundarySaved
        != palace::PalaceLezSubmissionIntentStoreStatus::Saved) {
        return "rejected=lez-submit-intent-boundary-save;reason="
            + std::string(
                palace::palaceLezSubmissionIntentStoreStatusName(
                    boundarySaved));
    }
    m_lezSubmissionIntent = mayHaveSubmitted;

    LogosList signers = LogosList::array();
    for (const bool required : plan.signingRequirements)
        signers.push_back(required);
    // The universal LEZ proxy declares instruction as CBOR `any`. Binary JSON
    // preserves the LEZ little-endian RISC Zero words as a CBOR byte string;
    // arrays of words are not carried through the universal module boundary.
    const LogosMap instructionBytes =
        LogosMap::binary(encodeLezWords(plan.instructionWords));
    logos::CallError callError;
    const std::string response = modules().lez_core.send_generic_public_transaction(
        plan.accountIdsHex,
        signers,
        instructionBytes,
        plan.programIdHex,
        &callError);
    if (!callError.ok())
        return "rejected=lez-submit-recovery-pending;reason="
            "ambiguous-module-call";
    const palace::PalaceLezSubmissionResult submitted =
        palace::PalaceLezCodec::parseSubmissionResult(response);
    if (!submitted.accepted
        && submitted.reason != "module-rejected") {
        return "rejected=lez-submit-recovery-pending;reason="
            + submitted.reason;
    }
    if (!submitted.accepted) {
        palace::PalaceLezSubmissionIntentV1 rejected =
            mayHaveSubmitted;
        rejected.phase =
            palace::PalaceLezSubmissionIntentPhase::Rejected;
        const palace::PalaceLezSubmissionIntentStoreStatus rejectedSaved =
            m_lezSubmissionIntentStore->save(rejected);
        if (rejectedSaved
            == palace::PalaceLezSubmissionIntentStoreStatus::Saved) {
            m_lezSubmissionIntent = std::move(rejected);
        }
        return "rejected=lez-submit;reason=" + submitted.reason;
    }
    const palace::PalaceLezCoordinatorUpdate coordinated =
        m_lezCoordinator.registerSubmission(
            plan, response, expected.dataSha256Hex);
    if (!coordinated.accepted) {
        return "rejected=lez-submit-recovery-pending;reason=coordinator-"
            + coordinated.reason + ";tx_hash="
            + submitted.transactionHash;
    }
    if (!m_lezCoordinatorStore
        || m_lezCoordinatorStore->save(m_lezCoordinator)
            != palace::PalaceLezCoordinatorStoreStatus::Saved) {
        m_lezCoordinatorStoreHealthy = false;
        return "rejected=lez-submit-recovery-pending;reason="
            "coordinator-save;tx_hash="
            + submitted.transactionHash;
    }
    m_lezCoordinatorStoreHealthy = true;

    palace::ActionJournal submittedJournal = m_actionJournal;
    if (!submittedJournal.markSubmittedToLez(
            actionId, submitted.transactionHash)) {
        return "rejected=lez-submit-recovery-pending;reason="
            "action-stage-changed;tx_hash="
            + submitted.transactionHash;
    }
    if (!m_actionJournalStore
        || !m_actionJournalStore->save(submittedJournal)) {
        return "rejected=lez-submit-recovery-pending;reason="
            "action-journal-save;tx_hash="
            + submitted.transactionHash;
    }
    m_actionJournal = std::move(submittedJournal);

    palace::PalaceLezSubmissionIntentV1 committed =
        mayHaveSubmitted;
    committed.phase =
        palace::PalaceLezSubmissionIntentPhase::Committed;
    committed.transactionHash = submitted.transactionHash;
    if (m_lezSubmissionIntentStore->save(committed)
        != palace::PalaceLezSubmissionIntentStoreStatus::Saved) {
        return "rejected=lez-submit-recovery-pending;reason="
            "submission-intent-commit-save;tx_hash="
            + submitted.transactionHash;
    }
    m_lezSubmissionIntent = std::move(committed);
    m_lezFinalityReasons[actionId] = "awaiting-stable-observation";
    return "ok;tx_hash=" + submitted.transactionHash + ";"
        + palace::canonicalActionStatus(m_actionJournal.status(actionId));
}

std::string PalaceCoreImpl::observePalaceTransition(
    const std::string& actionId)
{
    if (!m_lezReady || !m_lezCoordinator.running())
        return "rejected=lez-not-ready";
    const palace::ActionStatus status = m_actionJournal.status(actionId);
    if (status.durableStage != palace::DurableActionStage::SubmittedToLez
        || status.transactionHash.empty()) {
        return "rejected=action-not-submitted";
    }
    if (m_lezSubmissionIntent.has_value()
        && m_lezSubmissionIntent->actionId == actionId
        && m_lezSubmissionIntent->phase
            == palace::PalaceLezSubmissionIntentPhase::
                MayHaveBeenSubmitted) {
        std::string commitReason;
        if (!commitTrackedPalaceSubmissionIntent(
                actionId,
                status.transactionHash,
                commitReason)) {
            return "rejected=lez-submit-intent-commit;reason="
                + commitReason;
        }
    }
    const auto transaction =
        trackedLezTransaction(status.transactionHash);
    if (!transaction.has_value())
        return "rejected=lez-transaction-not-tracked";
    if (transaction->stage
            != palace::PalaceLezTransactionStage::Submitted
        && transaction->stage
            != palace::PalaceLezTransactionStage::Observed) {
        return "rejected=lez-transaction-stage";
    }

    palace::PalaceLezStableAccountBatchV1 stableAccounts;
    std::string readReason;
    if (!readStableLezAccounts(
            *transaction, stableAccounts, readReason)) {
        return "rejected=lez-stable-account-read;reason="
            + readReason;
    }

    if (transaction->stage
        == palace::PalaceLezTransactionStage::Submitted) {
        const palace::PalaceLezCoordinatorUpdate observed =
            m_lezCoordinator.observeStableRoot(
            status.transactionHash,
            stableAccounts.heightBefore,
            stableAccounts.accountResponseJson.front(),
            stableAccounts.heightAfter);
        if (!observed.accepted)
            return "rejected=lez-observation;reason="
                + observed.reason;
    }
    const auto observedTransaction =
        trackedLezTransaction(status.transactionHash);
    if (!observedTransaction.has_value()
        || observedTransaction->stage
            != palace::PalaceLezTransactionStage::Observed) {
        return "rejected=lez-observation-stage";
    }

    // Validate the exact all-account expectation before either durable stage
    // advances. On recovery, the immutable coordinator observation height can
    // predate this fresh, height-stable post-state batch.
    palace::PalaceLezTrackedTransaction expectationTransaction =
        *observedTransaction;
    expectationTransaction.observedBlockHeight =
        static_cast<std::uint64_t>(stableAccounts.heightAfter);
    const palace::PalaceLezFinalityExpectationBuildResultV1
        expectation = palace::buildPalaceLezFinalityExpectationV1(
            expectationTransaction, stableAccounts);
    if (!expectation.accepted) {
        return "rejected=lez-finality-expectation;reason="
            + expectation.reason;
    }

    if (!m_lezCoordinatorStore
        || m_lezCoordinatorStore->save(m_lezCoordinator)
            != palace::PalaceLezCoordinatorStoreStatus::Saved) {
        m_lezCoordinatorStoreHealthy = false;
        return "rejected=lez-coordinator-save";
    }
    m_lezCoordinatorStoreHealthy = true;

    palace::ActionJournal observedJournal = m_actionJournal;
    if (!observedJournal.markObserved(actionId))
        return "rejected=action-stage-changed";
    if (!m_actionJournalStore
        || !m_actionJournalStore->save(observedJournal))
        return "rejected=action-journal-save";
    m_actionJournal = std::move(observedJournal);

    std::string finalityReason;
    if (!startPalaceFinality(
            actionId,
            *observedTransaction,
            std::move(stableAccounts),
            finalityReason)) {
        return "rejected=lez-finality-start;reason="
            + finalityReason + ";"
            + actionStatus(actionId);
    }
    return "ok;height="
        + std::to_string(expectationTransaction.observedBlockHeight)
        + ";" + actionStatus(actionId);
}

std::optional<palace::PalaceLezTrackedTransaction>
PalaceCoreImpl::trackedLezTransaction(
    const std::string& transactionHash) const
{
    const std::vector<palace::PalaceLezTrackedTransaction> tracked =
        m_lezCoordinator.transactions();
    const auto transaction = std::find_if(
        tracked.begin(),
        tracked.end(),
        [&transactionHash](
            const palace::PalaceLezTrackedTransaction& value) {
            return value.transactionHash == transactionHash;
        });
    if (transaction == tracked.end())
        return std::nullopt;
    return *transaction;
}

bool PalaceCoreImpl::readStableLezAccounts(
    const palace::PalaceLezTrackedTransaction& transaction,
    palace::PalaceLezStableAccountBatchV1& stableAccounts,
    std::string& reason)
{
    stableAccounts = {};
    reason.clear();
    if (!transaction.plan.accepted
        || transaction.plan.accountIdsHex.empty()) {
        reason = "invalid-transaction-plan";
        return false;
    }
    std::string syncReason;
    if (!syncLezWalletToCurrent(syncReason)) {
        reason = "sync-" + syncReason;
        return false;
    }

    logos::CallError heightError;
    stableAccounts.heightBefore =
        modules().lez_core.get_current_block_height(&heightError);
    if (!heightError.ok() || stableAccounts.heightBefore < 0) {
        reason = "height-before";
        return false;
    }
    if (stableAccounts.heightBefore != m_lezSyncedHeight) {
        reason = "wallet-height-raced";
        return false;
    }

    stableAccounts.accountResponseJson.reserve(
        transaction.plan.accountIdsHex.size());
    for (std::size_t index = 0U;
         index < transaction.plan.accountIdsHex.size();
         ++index) {
        logos::CallError accountError;
        std::string response =
            modules().lez_core.get_account_public(
                transaction.plan.accountIdsHex[index],
                &accountError);
        if (!accountError.ok()) {
            stableAccounts = {};
            reason = "account-" + std::to_string(index);
            return false;
        }
        stableAccounts.accountResponseJson.push_back(
            std::move(response));
    }

    stableAccounts.heightAfter =
        modules().lez_core.get_current_block_height(&heightError);
    if (!heightError.ok() || stableAccounts.heightAfter < 0) {
        stableAccounts = {};
        reason = "height-after";
        return false;
    }
    if (stableAccounts.heightBefore != stableAccounts.heightAfter
        || stableAccounts.heightAfter != m_lezSyncedHeight) {
        stableAccounts = {};
        reason = "unstable-height";
        return false;
    }
    reason = "stable";
    return true;
}

bool PalaceCoreImpl::startPalaceFinality(
    const std::string& actionId,
    const palace::PalaceLezTrackedTransaction& transaction,
    palace::PalaceLezStableAccountBatchV1 stableAccounts,
    std::string& reason)
{
    reason.clear();
    if (!m_lezFinalityTransport) {
        reason = "transport-unavailable";
        return false;
    }
    palace::PalaceLezTrackedTransaction expectationTransaction =
        transaction;
    expectationTransaction.observedBlockHeight =
        static_cast<std::uint64_t>(stableAccounts.heightAfter);
    const palace::PalaceLezFinalityExpectationBuildResultV1 built =
        palace::buildPalaceLezFinalityExpectationV1(
            expectationTransaction, stableAccounts);
    if (!built.accepted) {
        reason = built.reason;
        return false;
    }

    PalaceLezPendingFinality pending;
    pending.actionId = actionId;
    pending.transactionHash = transaction.transactionHash;
    pending.transaction = transaction;
    pending.stableAccounts = std::move(stableAccounts);
    pending.expectation = built.expectation;
    const palace::PalaceLezExplorerFinalityUpdate started =
        pending.session.start(
            palace::PalaceLezReleaseLock::explorer(),
            palace::PalaceLezExplorerFinalityLimitsV1{},
            pending.expectation);
    if (!started.accepted
        || started.outcome
            != palace::PalaceLezExplorerFinalityOutcome::Pending) {
        reason = started.reason;
        return false;
    }

    m_lezPendingFinality = std::move(pending);
    m_lezFinalityReasons[actionId] = started.reason;
    pumpPalaceFinality();
    reason = palaceFinalityReason(actionId);
    return true;
}

void PalaceCoreImpl::pumpPalaceFinality()
{
    if (!m_lezPendingFinality.has_value()
        || !m_lezFinalityTransport
        || m_lezFinalityTransport->busy()) {
        return;
    }
    PalaceLezPendingFinality& pending =
        *m_lezPendingFinality;
    if (pending.session.outcome()
            != palace::PalaceLezExplorerFinalityOutcome::Pending
        || pending.session.scanExhausted()) {
        return;
    }
    const std::optional<palace::PalaceLezExplorerCommandV1>
        command = pending.session.takeNextCommand();
    if (!command.has_value())
        return;

    const std::string actionId = pending.actionId;
    const std::string transactionHash = pending.transactionHash;
    const std::weak_ptr<
        palace::CallbackLifetime<PalaceCoreImpl>> weakLifetime =
            m_callbackLifetime;
    const palace::PalaceLezExplorerQtDispatchResult dispatched =
        m_lezFinalityTransport->dispatch(
            *command,
            [weakLifetime, actionId, transactionHash](
                const palace::PalaceLezExplorerHttpResponseV1&
                    response) {
                const auto lifetime = weakLifetime.lock();
                if (!lifetime)
                    return;
                lifetime->invoke(
                    [&actionId, &transactionHash, &response](
                        PalaceCoreImpl& owner) {
                        owner.acceptPalaceFinalityResponse(
                            actionId,
                            transactionHash,
                            response);
                    });
            });
    if (dispatched.accepted)
        return;

    palace::PalaceLezExplorerHttpResponseV1 failed;
    failed.commandSequence = command->sequence;
    failed.effectiveOrigin = command->origin;
    failed.transportError =
        "dispatch-" + dispatched.reason;
    acceptPalaceFinalityResponse(
        actionId, transactionHash, failed);
}

void PalaceCoreImpl::acceptPalaceFinalityResponse(
    const std::string& actionId,
    const std::string& transactionHash,
    const palace::PalaceLezExplorerHttpResponseV1& response)
{
    if (!m_lezPendingFinality.has_value()
        || m_lezPendingFinality->actionId != actionId
        || m_lezPendingFinality->transactionHash
            != transactionHash) {
        return;
    }

    const palace::PalaceLezExplorerFinalityUpdate update =
        m_lezPendingFinality->session.acceptResponse(response);
    m_lezFinalityReasons[actionId] = update.reason;
    if (update.outcome
        == palace::PalaceLezExplorerFinalityOutcome::Finalized) {
        std::string persistReason;
        if (!persistPalaceFinalityCertificate(persistReason))
            m_lezFinalityReasons[actionId] = persistReason;
        pumpPalaceHistory();
        return;
    }
    if (update.outcome
            == palace::PalaceLezExplorerFinalityOutcome::Pending
        && !m_lezPendingFinality->session.scanExhausted()) {
        pumpPalaceFinality();
    }
    pumpPalaceHistory();
}

bool PalaceCoreImpl::finalizedAuthorityIncludes(
    const palace::PalaceLezTrackedTransaction& transaction) const
{
    const palace::PalaceLezAuthorityBundleExpectationV1 expected =
        lezAuthorityBundleExpectation();
    return m_lezAuthorityReady
        && m_lezAuthorityBundle.scope.networkId
            == expected.scope.networkId
        && m_lezAuthorityBundle.scope.programIdHex
            == transaction.plan.programIdHex
        && m_lezAuthorityBundle.scope.rootAccountIdHex
            == transaction.plan.rootAccountIdHex
        && m_lezAuthorityBundle.checkpoint.lastOrderedActionId
            == transaction.orderedActionId;
}

void PalaceCoreImpl::pumpPalaceHistory()
{
    if (!m_lezOpenHistory.has_value()
        || !m_lezFinalityTransport
        || m_lezFinalityTransport->busy()) {
        return;
    }
    PalaceLezOpenHistory& history = *m_lezOpenHistory;
    if (history.authorityApplied
        || history.session.outcome()
            != palace::PalaceLezExplorerHistoryOutcome::Pending
        || history.session.scanExhausted()) {
        return;
    }
    const std::optional<palace::PalaceLezExplorerCommandV1>
        command = history.session.takeNextCommand();
    if (!command.has_value())
        return;

    const std::string palaceIdHex = history.palaceIdHex;
    const std::weak_ptr<
        palace::CallbackLifetime<PalaceCoreImpl>> weakLifetime =
            m_callbackLifetime;
    const palace::PalaceLezExplorerQtDispatchResult dispatched =
        m_lezFinalityTransport->dispatch(
            *command,
            [weakLifetime, palaceIdHex](
                const palace::PalaceLezExplorerHttpResponseV1&
                    response) {
                const auto lifetime = weakLifetime.lock();
                if (!lifetime)
                    return;
                lifetime->invoke(
                    [&palaceIdHex, &response](
                        PalaceCoreImpl& owner) {
                        owner.acceptPalaceHistoryResponse(
                            palaceIdHex, response);
                    });
            });
    if (dispatched.accepted)
        return;

    palace::PalaceLezExplorerHttpResponseV1 failed;
    failed.commandSequence = command->sequence;
    failed.effectiveOrigin = command->origin;
    failed.transportError =
        "dispatch-" + dispatched.reason;
    acceptPalaceHistoryResponse(palaceIdHex, failed);
}

void PalaceCoreImpl::acceptPalaceHistoryResponse(
    const std::string& palaceIdHex,
    const palace::PalaceLezExplorerHttpResponseV1& response)
{
    if (!m_lezOpenHistory.has_value()
        || m_lezOpenHistory->palaceIdHex != palaceIdHex)
        return;

    PalaceLezOpenHistory& history = *m_lezOpenHistory;
    const palace::PalaceLezExplorerHistoryUpdateV1 update =
        history.session.acceptResponse(response);
    history.reason = update.reason;
    if (update.outcome
        == palace::PalaceLezExplorerHistoryOutcome::Rebuilt) {
        std::string rebuildReason;
        if (!completePalaceHistoryRebuild(rebuildReason)) {
            history.reason = rebuildReason;
            m_lezAuthorityState =
                "degraded-" + rebuildReason;
        }
        pumpPalaceFinality();
        return;
    }
    if (update.outcome
            == palace::PalaceLezExplorerHistoryOutcome::Pending
        && !history.session.scanExhausted()) {
        pumpPalaceHistory();
    } else {
        pumpPalaceFinality();
    }
}

palace::DeliveryIdentityRegistrationRecoveryV1
PalaceCoreImpl::lookupFinalizedLezSubmission(
    const std::string& key,
    const palace::PalaceLezSubmissionRecoveryExpectationV1&
        expectation)
{
    using RecoveryOutcome =
        palace::DeliveryIdentityRegistrationRecoveryOutcome;
    if (key.empty() || !m_lezFinalityTransport) {
        return {
            RecoveryOutcome::Rejected,
            {},
            "submission-recovery-unavailable",
        };
    }

    bool start = !m_lezPendingSubmissionRecovery.has_value();
    if (!start) {
        PalaceLezPendingSubmissionRecovery& pending =
            *m_lezPendingSubmissionRecovery;
        const bool same =
            pending.key == key
            && sameSubmissionRecoveryExpectation(
                pending.expectation, expectation);
        if (!same) {
            if (pending.session.outcome()
                == palace::PalaceLezSubmissionRecoveryOutcome::
                    Found) {
                return {
                    RecoveryOutcome::Pending,
                    {},
                    "submission-recovery-result-pending-consumer",
                };
            }
            if (pending.session.outcome()
                    == palace::PalaceLezSubmissionRecoveryOutcome::
                        Pending
                && !pending.session.scanExhausted()) {
                return {
                    RecoveryOutcome::Pending,
                    {},
                    "submission-recovery-busy",
                };
            }
            start = true;
        } else {
            switch (pending.session.outcome()) {
            case palace::PalaceLezSubmissionRecoveryOutcome::Found: {
                const auto result = pending.session.result();
                if (!result.has_value()) {
                    return {
                        RecoveryOutcome::Rejected,
                        {},
                        "submission-recovery-result-missing",
                    };
                }
                return {
                    RecoveryOutcome::Found,
                    result->transactionHash,
                    pending.reason,
                };
            }
            case palace::PalaceLezSubmissionRecoveryOutcome::Rejected:
            case palace::PalaceLezSubmissionRecoveryOutcome::Degraded:
                return {
                    RecoveryOutcome::Rejected,
                    {},
                    pending.reason,
                };
            case palace::PalaceLezSubmissionRecoveryOutcome::NotFound:
                start = true;
                break;
            case palace::PalaceLezSubmissionRecoveryOutcome::Pending:
                start = pending.session.scanExhausted();
                break;
            }
        }
    }

    if (start) {
        PalaceLezPendingSubmissionRecovery pending;
        pending.key = key;
        pending.expectation = expectation;
        const palace::PalaceLezSubmissionRecoveryUpdateV1 update =
            pending.session.start(
                palace::PalaceLezReleaseLock::explorer(),
                lezHistoryLimits(),
                expectation);
        pending.reason = update.reason;
        m_lezPendingSubmissionRecovery = std::move(pending);
        if (!update.accepted) {
            return {
                RecoveryOutcome::Rejected,
                {},
                update.reason,
            };
        }
    }

    pumpLezSubmissionRecovery();
    return {
        RecoveryOutcome::Pending,
        {},
        m_lezPendingSubmissionRecovery.has_value()
            ? m_lezPendingSubmissionRecovery->reason
            : std::string("submission-recovery-pending"),
    };
}

void PalaceCoreImpl::pumpLezSubmissionRecovery()
{
    if (!m_lezPendingSubmissionRecovery.has_value()
        || !m_lezFinalityTransport
        || m_lezFinalityTransport->busy()) {
        return;
    }
    PalaceLezPendingSubmissionRecovery& pending =
        *m_lezPendingSubmissionRecovery;
    if (pending.session.outcome()
            != palace::PalaceLezSubmissionRecoveryOutcome::Pending
        || pending.session.scanExhausted()) {
        return;
    }
    const auto command = pending.session.takeNextCommand();
    if (!command.has_value())
        return;

    const std::string key = pending.key;
    const std::weak_ptr<
        palace::CallbackLifetime<PalaceCoreImpl>> weakLifetime =
            m_callbackLifetime;
    const palace::PalaceLezExplorerQtDispatchResult dispatched =
        m_lezFinalityTransport->dispatch(
            *command,
            [weakLifetime, key](
                const palace::PalaceLezExplorerHttpResponseV1&
                    response) {
                const auto lifetime = weakLifetime.lock();
                if (!lifetime)
                    return;
                lifetime->invoke(
                    [&key, &response](
                        PalaceCoreImpl& owner) {
                        owner.acceptLezSubmissionRecoveryResponse(
                            key, response);
                    });
            });
    if (dispatched.accepted)
        return;

    palace::PalaceLezExplorerHttpResponseV1 failed;
    failed.commandSequence = command->sequence;
    failed.effectiveOrigin = command->origin;
    failed.transportError =
        "dispatch-" + dispatched.reason;
    acceptLezSubmissionRecoveryResponse(key, failed);
}

void PalaceCoreImpl::acceptLezSubmissionRecoveryResponse(
    const std::string& key,
    const palace::PalaceLezExplorerHttpResponseV1& response)
{
    if (!m_lezPendingSubmissionRecovery.has_value()
        || m_lezPendingSubmissionRecovery->key != key) {
        return;
    }
    PalaceLezPendingSubmissionRecovery& pending =
        *m_lezPendingSubmissionRecovery;
    const palace::PalaceLezSubmissionRecoveryUpdateV1 update =
        pending.session.acceptResponse(response);
    pending.reason = update.reason;
    if (update.outcome
            == palace::PalaceLezSubmissionRecoveryOutcome::Pending
        && !pending.session.scanExhausted()) {
        pumpLezSubmissionRecovery();
    } else {
        pumpPalaceFinality();
        pumpPalaceHistory();
    }
}

bool PalaceCoreImpl::readStableLezAccountIds(
    const std::vector<std::string>& accountIdsHex,
    const std::int64_t minimumFinalizedHeight,
    palace::PalaceLezStableAccountBatchV1& stableAccounts,
    std::string& reason)
{
    static constexpr std::size_t kMaximumAuthorityAccounts = 512U;
    static constexpr std::size_t kMaximumAccountResponseBytes =
        256U * 1024U;

    stableAccounts = {};
    reason.clear();
    if (accountIdsHex.empty()
        || accountIdsHex.size() > kMaximumAuthorityAccounts
        || minimumFinalizedHeight < 0) {
        reason = "invalid-history-account-set";
        return false;
    }

    logos::CallError callError;
    stableAccounts.heightBefore =
        modules().lez_core.get_last_synced_block(&callError);
    if (!callError.ok()
        || stableAccounts.heightBefore != m_lezSyncedHeight
        || stableAccounts.heightBefore
            < minimumFinalizedHeight) {
        reason = "history-stable-height-before-mismatch";
        return false;
    }
    stableAccounts.accountResponseJson.reserve(
        accountIdsHex.size());
    for (std::size_t index = 0U;
         index < accountIdsHex.size();
         ++index) {
        logos::CallError accountError;
        std::string response =
            modules().lez_core.get_account_public(
                accountIdsHex[index], &accountError);
        if (!accountError.ok() || response.empty()
            || response.size() > kMaximumAccountResponseBytes) {
            stableAccounts = {};
            reason = "history-account-"
                + std::to_string(index);
            return false;
        }
        stableAccounts.accountResponseJson.push_back(
            std::move(response));
    }
    stableAccounts.heightAfter =
        modules().lez_core.get_last_synced_block(&callError);
    if (!callError.ok()
        || stableAccounts.heightAfter != m_lezSyncedHeight
        || stableAccounts.heightAfter
            != stableAccounts.heightBefore) {
        stableAccounts = {};
        reason = "history-stable-height-after-mismatch";
        return false;
    }
    reason = "stable-current-history-snapshot";
    return true;
}

bool PalaceCoreImpl::completePalaceHistoryRebuild(
    std::string& reason)
{
    reason.clear();
    if (!m_lezOpenHistory.has_value()
        || !m_lezAuthorityBundleStore) {
        reason = "history-state-unavailable";
        return false;
    }
    PalaceLezOpenHistory& opening = *m_lezOpenHistory;
    const auto rebuilt = opening.session.rebuildResult();
    if (!rebuilt.has_value()
        || rebuilt->actions.empty()
        || rebuilt->uniqueAccountIdsHex.empty()) {
        reason = "history-result-missing";
        return false;
    }
    const auto* initialization =
        std::get_if<palace::PalaceLezInitializeV3>(
            &rebuilt->actions.front().instruction.payload);
    if (initialization == nullptr
        || palace::PalaceLezCodec::bytes32Hex(
               initialization->palaceId)
            != opening.palaceIdHex) {
        reason = "palace-uri-history-mismatch";
        return false;
    }
    if (rebuilt->latestFinalizedBlockId == 0U
        || rebuilt->latestFinalizedBlockId
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
        reason = "invalid-history-checkpoint";
        return false;
    }

    const std::int64_t checkpointHeight =
        static_cast<std::int64_t>(
            rebuilt->latestFinalizedBlockId);
    std::string syncReason;
    if (!syncLezWalletToCurrent(syncReason)) {
        reason = syncReason;
        return false;
    }
    palace::PalaceLezStableAccountBatchV1 stableAccounts;
    std::string readReason;
    if (!readStableLezAccountIds(
            rebuilt->uniqueAccountIdsHex,
            checkpointHeight,
            stableAccounts,
            readReason)) {
        reason = readReason;
        return false;
    }

    palace::PalaceLezAuthorityBundleCheckpointV1 checkpoint;
    checkpoint.finalizedBlockId =
        rebuilt->latestFinalizedBlockId;
    checkpoint.finalizedBlockHeight =
        rebuilt->latestFinalizedBlockId;
    checkpoint.finalizedBlockHashHex =
        rebuilt->latestFinalizedBlockHashHex;
    checkpoint.lastOrderedActionId =
        rebuilt->actions.back().orderedActionId;

    palace::AuthorityProjection candidateAuthority;
    palace::PalaceLezFinalizedAuthorityBundleV1
        candidateBundle;
    const palace::PalaceLezAuthorityBundleScopeV1 scope =
        lezAuthorityBundleExpectation().scope;
    const palace::PalaceLezAuthorityStateUpdateV1 projected =
        palace::rebuildFinalizedLezAuthorityStateV1(
            candidateAuthority,
            candidateBundle,
            scope,
            *rebuilt,
            stableAccounts,
            checkpoint);
    if (!projected.accepted) {
        reason =
            "root-ahead-or-history-mismatch-"
            + projected.reason;
        return false;
    }
    if (candidateAuthority.palaceId()
        != opening.palaceIdHex) {
        reason = "palace-uri-projection-mismatch";
        return false;
    }

    const palace::PalaceLezAuthorityBundleStoreStatus saved =
        m_lezAuthorityBundleStore->save(candidateBundle);
    if (saved
        != palace::PalaceLezAuthorityBundleStoreStatus::Saved) {
        reason = "authority-store-"
            + std::string(
                palace::palaceLezAuthorityBundleStoreStatusName(
                    saved));
        return false;
    }
    m_deliveryAuthority = std::move(candidateAuthority);
    m_lezAuthorityBundle = std::move(candidateBundle);
    m_lezAuthorityReady = true;
    persistStorageMvpCatalogIfFinalized();
    refreshDeliveryAllowedProps();
    m_lezAuthorityState =
        "rebuilt-" + std::to_string(
            checkpoint.lastOrderedActionId);
    opening.authorityApplied = true;
    opening.reason = "finalized-authority-rebuilt";
    reason = opening.reason;
    return true;
}

bool PalaceCoreImpl::persistFinalizedAuthorityState(
    const palace::PalaceLezTrackedTransaction& transaction,
    const palace::PalaceLezExplorerFinalityCertificateV1&
        certificate,
    const palace::PalaceLezStableAccountBatchV1& stableAccounts,
    std::string& reason)
{
    reason.clear();
    if (!m_lezAuthorityBundleStore) {
        reason = "authority-store-unavailable";
        return false;
    }
    if (finalizedAuthorityIncludes(transaction)) {
        const palace::PalaceLezAuthorityBundleCheckpointV1&
            checkpoint = m_lezAuthorityBundle.checkpoint;
        if (checkpoint.finalizedBlockId
                != certificate.finalizedBlockId()
            || checkpoint.finalizedBlockHeight
                != certificate.finalizedBlockHeight()
            || checkpoint.finalizedBlockHashHex
                != certificate.finalizedBlockHashHex()) {
            reason = "authority-checkpoint-mismatch";
            return false;
        }
        reason = "authority-already-finalized";
        return true;
    }
    if (m_lezAuthorityReady
        && (m_lezAuthorityBundle.checkpoint.lastOrderedActionId
                == std::numeric_limits<std::uint64_t>::max()
            || m_lezAuthorityBundle.checkpoint.lastOrderedActionId
                    + 1U
                != transaction.orderedActionId)) {
        reason = "authority-history-rebuild-required";
        return false;
    }
    if (!m_lezAuthorityReady
        && transaction.orderedActionId != 0U) {
        reason = "authority-history-rebuild-required";
        return false;
    }

    palace::AuthorityProjection candidateAuthority =
        m_lezAuthorityReady
        ? m_deliveryAuthority
        : palace::AuthorityProjection{};
    palace::PalaceLezFinalizedAuthorityBundleV1 candidateBundle =
        m_lezAuthorityBundle;
    palace::PalaceLezAuthorityBundleExpectationV1
        priorExpectation = lezAuthorityBundleExpectation();
    if (m_lezAuthorityReady) {
        priorExpectation.checkpoint = candidateBundle.checkpoint;
    } else {
        candidateBundle.scope = priorExpectation.scope;
    }

    const palace::PalaceLezAuthorityStateUpdateV1 applied =
        palace::applyFinalizedLezAuthorityActionV1(
            candidateAuthority,
            candidateBundle,
            priorExpectation,
            transaction,
            certificate,
            stableAccounts);
    if (!applied.accepted) {
        m_lezAuthorityState =
            "degraded-" + applied.reason;
        reason = "authority-" + applied.reason;
        return false;
    }
    const palace::PalaceLezAuthorityBundleStoreStatus saved =
        m_lezAuthorityBundleStore->save(candidateBundle);
    if (saved
        != palace::PalaceLezAuthorityBundleStoreStatus::Saved) {
        m_lezAuthorityState =
            "degraded-"
            + std::string(
                palace::palaceLezAuthorityBundleStoreStatusName(
                    saved));
        reason = "authority-store-"
            + std::string(
                palace::palaceLezAuthorityBundleStoreStatusName(
                    saved));
        return false;
    }

    m_deliveryAuthority = std::move(candidateAuthority);
    m_lezAuthorityBundle = std::move(candidateBundle);
    m_lezAuthorityReady = true;
    persistStorageMvpCatalogIfFinalized();
    refreshDeliveryAllowedProps();
    m_lezAuthorityState =
        "finalized-" + std::to_string(transaction.orderedActionId);
    reason = "authority-finalized";
    return true;
}

bool PalaceCoreImpl::persistPalaceFinalityCertificate(
    std::string& reason)
{
    reason.clear();
    if (!m_lezPendingFinality.has_value()) {
        reason = "finality-session-missing";
        return false;
    }
    PalaceLezPendingFinality& pending =
        *m_lezPendingFinality;
    const auto certificate =
        pending.session.finalityCertificate();
    if (!certificate.has_value()) {
        reason = "finality-certificate-missing";
        return false;
    }

    auto tracked =
        trackedLezTransaction(pending.transactionHash);
    if (!tracked.has_value()) {
        reason = "finality-transaction-not-tracked";
        return false;
    }
    if (tracked->stage
        == palace::PalaceLezTransactionStage::Observed) {
        const palace::PalaceLezCoordinatorUpdate reconciled =
            m_lezCoordinator.reconcileExplorerFinality(
                *certificate);
        if (!reconciled.accepted || !reconciled.changed
            || reconciled.stage
                != palace::PalaceLezTransactionStage::Finalized) {
            reason = "coordinator-" + reconciled.reason;
            return false;
        }
        tracked =
            trackedLezTransaction(pending.transactionHash);
    }
    if (!tracked.has_value()
        || tracked->stage
            != palace::PalaceLezTransactionStage::Finalized) {
        reason = "coordinator-finalized-stage-required";
        return false;
    }

    if (!m_lezCoordinatorStore
        || m_lezCoordinatorStore->save(m_lezCoordinator)
            != palace::PalaceLezCoordinatorStoreStatus::Saved) {
        m_lezCoordinatorStoreHealthy = false;
        reason = "coordinator-save-failed";
        return false;
    }
    m_lezCoordinatorStoreHealthy = true;

    m_lezLatestFinalizedEvidence =
        PalaceLezFinalizedEvidence{
            pending.actionId,
            *tracked,
            pending.stableAccounts,
            *certificate,
        };

    std::string authorityReason;
    if (!persistFinalizedAuthorityState(
            *tracked,
            *certificate,
            pending.stableAccounts,
            authorityReason)) {
        reason = authorityReason;
        return false;
    }

    const palace::ActionStatus current =
        m_actionJournal.status(pending.actionId);
    if (current.durableStage
        == palace::DurableActionStage::Observed) {
        palace::ActionJournal finalizedJournal = m_actionJournal;
        if (!finalizedJournal.markFinalized(pending.actionId)) {
            reason = "action-stage-changed";
            return false;
        }
        if (!m_actionJournalStore
            || !m_actionJournalStore->save(finalizedJournal)) {
            reason = "action-journal-save-failed";
            return false;
        }
        m_actionJournal = std::move(finalizedJournal);
    } else if (current.durableStage
               != palace::DurableActionStage::Finalized) {
        reason = "action-observed-stage-required";
        return false;
    }

    reason = pending.session.reason();
    if (m_palaceVmTurn.has_value()
        && m_palaceVmTurn->actionId
            == pending.actionId) {
        std::string vmReason;
        if (promoteFinalizedPalaceVmTurn(
                pending.actionId, vmReason)) {
            reason += "-vm-promoted";
        } else {
            reason += "-vm-" + vmReason;
        }
    }
    m_lezFinalityReasons[pending.actionId] = reason;
    return true;
}

std::string PalaceCoreImpl::reconcilePalaceTransition(
    const std::string& actionId)
{
    if (!m_lezReady || !m_lezCoordinator.running())
        return "rejected=lez-not-ready";
    const palace::ActionStatus status =
        m_actionJournal.status(actionId);
    if (status.transactionHash.empty())
        return "rejected=action-has-no-transaction";
    if (m_lezSubmissionIntent.has_value()
        && m_lezSubmissionIntent->actionId == actionId
        && m_lezSubmissionIntent->phase
            == palace::PalaceLezSubmissionIntentPhase::
                MayHaveBeenSubmitted) {
        std::string commitReason;
        if (!commitTrackedPalaceSubmissionIntent(
                actionId,
                status.transactionHash,
                commitReason)) {
            return "rejected=lez-submit-intent-commit;reason="
                + commitReason;
        }
    }
    const auto tracked =
        trackedLezTransaction(status.transactionHash);
    if (!tracked.has_value())
        return "rejected=lez-transaction-not-tracked";

    if (tracked->stage
        == palace::PalaceLezTransactionStage::Finalized) {
        if (m_lezPendingFinality.has_value()
            && m_lezPendingFinality->actionId == actionId
            && m_lezPendingFinality->transactionHash
                == status.transactionHash
            && m_lezPendingFinality->session.outcome()
                == palace::PalaceLezExplorerFinalityOutcome::
                    Finalized) {
            std::string persistReason;
            if (!persistPalaceFinalityCertificate(
                    persistReason)) {
                m_lezFinalityReasons[actionId] =
                    persistReason;
                return "rejected=lez-finality-persist;reason="
                    + persistReason;
            }
            return "ok;" + actionStatus(actionId);
        }
        if (status.durableStage
                == palace::DurableActionStage::Observed
            && !finalizedAuthorityIncludes(*tracked)) {
            m_lezAuthorityState =
                "history-rebuild-required";
            m_lezFinalityReasons[actionId] =
                "authority-history-rebuild-required";
            return "rejected=lez-authority-history-rebuild-required";
        }
        if (!m_lezCoordinatorStore
            || m_lezCoordinatorStore->save(m_lezCoordinator)
                != palace::PalaceLezCoordinatorStoreStatus::Saved) {
            m_lezCoordinatorStoreHealthy = false;
            return "rejected=lez-coordinator-save";
        }
        m_lezCoordinatorStoreHealthy = true;
        if (status.durableStage
            == palace::DurableActionStage::Observed) {
            palace::ActionJournal finalizedJournal =
                m_actionJournal;
            if (!finalizedJournal.markFinalized(actionId)
                || !m_actionJournalStore
                || !m_actionJournalStore->save(
                    finalizedJournal)) {
                return "rejected=action-journal-save";
            }
            m_actionJournal = std::move(finalizedJournal);
        } else if (status.durableStage
                   != palace::DurableActionStage::Finalized) {
            return "rejected=action-stage-mismatch";
        }
        if (m_lezFinalityReasons.find(actionId)
            == m_lezFinalityReasons.end()) {
            m_lezFinalityReasons[actionId] =
                "finalized-coordinator-restored";
        }
        return "ok;" + actionStatus(actionId);
    }

    if (status.durableStage
        == palace::DurableActionStage::SubmittedToLez) {
        return observePalaceTransition(actionId);
    }
    if (status.durableStage
        != palace::DurableActionStage::Observed) {
        return "rejected=action-not-observed";
    }
    if (tracked->stage
        != palace::PalaceLezTransactionStage::Observed) {
        return "rejected=lez-transaction-not-observed";
    }

    if (m_lezPendingFinality.has_value()
        && m_lezPendingFinality->actionId == actionId
        && m_lezPendingFinality->transactionHash
            == status.transactionHash) {
        PalaceLezPendingFinality& pending =
            *m_lezPendingFinality;
        if (pending.session.outcome()
                == palace::PalaceLezExplorerFinalityOutcome::Pending
            && pending.session.scanExhausted()) {
            const palace::PalaceLezExplorerFinalityUpdate restarted =
                pending.session.start(
                    palace::PalaceLezReleaseLock::explorer(),
                    palace::PalaceLezExplorerFinalityLimitsV1{},
                    pending.expectation);
            m_lezFinalityReasons[actionId] =
                restarted.reason;
            if (!restarted.accepted) {
                return "rejected=lez-finality-restart;reason="
                    + restarted.reason;
            }
        } else if (pending.session.outcome()
                   != palace::PalaceLezExplorerFinalityOutcome::Pending) {
            return "rejected=lez-finality-terminal;reason="
                + pending.session.reason();
        }
        pumpPalaceFinality();
        return "ok;" + actionStatus(actionId);
    }

    palace::PalaceLezStableAccountBatchV1 stableAccounts;
    std::string readReason;
    if (!readStableLezAccounts(
            *tracked, stableAccounts, readReason)) {
        return "rejected=lez-stable-account-read;reason="
            + readReason;
    }
    std::string startReason;
    if (!startPalaceFinality(
            actionId,
            *tracked,
            std::move(stableAccounts),
            startReason)) {
        return "rejected=lez-finality-start;reason="
            + startReason;
    }
    return "ok;" + actionStatus(actionId);
}

std::string PalaceCoreImpl::markSubmittedToLez(const std::string& actionId,
                                                const std::string& transactionHash)
{
    const bool changed = m_actionJournal.markSubmittedToLez(actionId, transactionHash);
    if (changed)
        persistActionJournal();
    return result(changed, m_actionJournal.status(actionId));
}

std::string PalaceCoreImpl::markObserved(const std::string& actionId)
{
    const bool changed = m_actionJournal.markObserved(actionId);
    if (changed)
        persistActionJournal();
    return result(changed, m_actionJournal.status(actionId));
}

std::string PalaceCoreImpl::markFinalized(const std::string& actionId)
{
    const bool changed = m_actionJournal.markFinalized(actionId);
    if (changed)
        persistActionJournal();
    return result(changed, m_actionJournal.status(actionId));
}

std::string PalaceCoreImpl::markDeliveryPublished(const std::string& actionId)
{
    const bool changed = m_actionJournal.markDeliveryPublished(actionId);
    if (changed)
        persistActionJournal();
    return result(changed, m_actionJournal.status(actionId));
}

std::string PalaceCoreImpl::actionStatus(const std::string& actionId) const
{
    std::string status = palace::canonicalActionStatus(
               m_actionJournal.status(actionId))
        + ";finality=" + palaceFinalityReason(actionId);
    if (m_palaceVmTurn.has_value()
        && m_palaceVmTurn->actionId == actionId) {
        status += ";vm=" + m_palaceVmTurn->phase
            + ";navigation="
            + (m_palaceVmTurn->navigationApplied
                   ? "1" : "0")
            + ";vm_reason="
            + m_palaceVmTurn->reason;
    }
    return status;
}

std::string PalaceCoreImpl::palaceFinalityReason(
    const std::string& actionId) const
{
    const auto reason = m_lezFinalityReasons.find(actionId);
    if (reason != m_lezFinalityReasons.end())
        return reason->second;

    const palace::ActionStatus status =
        m_actionJournal.status(actionId);
    switch (status.durableStage) {
    case palace::DurableActionStage::SubmittedToLez:
        return "awaiting-stable-observation";
    case palace::DurableActionStage::Observed:
        return "reconciliation-required";
    case palace::DurableActionStage::Finalized:
        return "finalized-coordinator-restored";
    case palace::DurableActionStage::Rejected:
    case palace::DurableActionStage::Expired:
    case palace::DurableActionStage::Orphaned:
        return "not-applicable";
    case palace::DurableActionStage::LocalDraft:
    case palace::DurableActionStage::Queued:
        return "not-started";
    }
    return "unknown";
}

bool PalaceCoreImpl::initializeStorageMvpBundle()
{
    if (m_storageMvpBundle.initialized())
        return true;
    if (!m_verifiedAssetStore)
        return false;
    const auto atriumPath = m_verifiedAssetStore->verifiedPngPath(
        m_assetAuthoring.handleForRoom("atrium"));
    const auto loungePath = m_verifiedAssetStore->verifiedPngPath(
        m_assetAuthoring.handleForRoom("lounge"));
    const auto& authoringState = m_assetAuthoring.state();
    if (!atriumPath.has_value() || !loungePath.has_value())
        return false;

    auto readAsset = [](const std::string& path) {
        QFile input(QString::fromStdString(path));
        if (!input.open(QIODevice::ReadOnly))
            return std::string{};
        const QByteArray bytes = input.read(10 * 1024 * 1024 + 1);
        if (!input.atEnd() || bytes.isEmpty()
            || bytes.size() > 10 * 1024 * 1024) {
            return std::string{};
        }
        return bytes.toStdString();
    };
    const std::string atrium = readAsset(*atriumPath);
    const std::string lounge = readAsset(*loungePath);
    std::optional<palace::PalaceStorageMvpPropInputV1>
        propInput;
    if (authoringState.propAssignment.has_value()) {
        const palace::AssetAuthoringPropAssignmentV1& propAssignment =
            *authoringState.propAssignment;
        const palace::AssetAuthoringAssetV1* propAsset =
            m_assetAuthoring.asset(propAssignment.handle);
        const auto propPath = m_verifiedAssetStore->verifiedPngPath(
            propAssignment.handle);
        if (!propPath.has_value() || propAsset == nullptr)
            return false;
        propInput = palace::PalaceStorageMvpPropInputV1{
            readAsset(*propPath),
            propAssignment.propId,
            propAsset->width,
            propAsset->height,
            propAssignment.anchorX,
            propAssignment.anchorY,
            propAssignment.layer,
        };
    }
    return m_storageMvpBundle.initialize(
        atrium, lounge, propInput);
}

std::optional<palace::PalaceStorageMvpCatalogBindingV1>
PalaceCoreImpl::storageMvpCatalogBinding() const
{
    if (!m_lezAuthorityReady)
        return std::nullopt;
    const auto root = finalizedAuthorityRootRecord(
        m_lezAuthorityBundle);
    if (!root.has_value())
        return std::nullopt;

    palace::PalaceStorageMvpCatalogBindingV1 binding;
    binding.networkId = m_lezAuthorityBundle.scope.networkId;
    binding.programIdHex = m_lezAuthorityBundle.scope.programIdHex;
    binding.rootAccountIdHex =
        m_lezAuthorityBundle.scope.rootAccountIdHex;
    binding.finalizedCheckpoint =
        m_lezAuthorityBundle.checkpoint.finalizedBlockId;
    binding.finalizedHash =
        m_lezAuthorityBundle.checkpoint.finalizedBlockHashHex;
    binding.rootManifestCid = root->activeManifestCid;
    return binding;
}

void PalaceCoreImpl::clearStorageMvpRuntimeState()
{
    m_storageMvpTransfers.clear();
    m_storageMvpScheduledPublications.clear();
    m_storageMvpFetchedObjects.clear();
    m_storageMvpRetainedObjects.clear();
    m_storageMvpPendingRoomBackgrounds.clear();
    m_storageMvpResolvedRoomBackgrounds.clear();
    m_storageMvpFailures.clear();
    m_storageMvpFetchSource.reset();
    m_storageMvpNativeAvailableCount = 0U;
    m_storageMvpNativeTotalCount = 0U;
    m_storageRetentionRound = 0U;
    m_storageRetentionInProgress = false;
    m_storageMvpFetchDispatchPending = false;
}

bool PalaceCoreImpl::restoreStorageMvpCatalog()
{
    if (!m_storageMvpCatalogStore || !m_lezAuthorityReady)
        return true;

    const auto binding = storageMvpCatalogBinding();
    const auto reject = [this](const std::string& reason) {
        clearStorageMvpRuntimeState();
        m_storageMvpBundle = palace::PalaceStorageMvpBundle{};
        m_storageMvpFailures["catalog"] = reason;
        m_storageMvpMode = "degraded";
        return false;
    };
    if (!binding.has_value())
        return true;

    palace::PalaceStorageMvpCatalogRecordV1 record;
    const palace::PalaceStorageMvpCatalogStoreStatus loaded =
        m_storageMvpCatalogStore->load(*binding, record);
    const palace::PalaceStorageMvpCatalogRecoveryAction action =
        palace::palaceStorageMvpCatalogRecoveryAction(loaded);
    if (action
        == palace::PalaceStorageMvpCatalogRecoveryAction::Idle) {
        if (loaded
            == palace::PalaceStorageMvpCatalogStoreStatus::
                BindingMismatch) {
            clearStorageMvpRuntimeState();
            m_storageMvpBundle = palace::PalaceStorageMvpBundle{};
            m_storageMvpCatalogStale = true;
            m_storageMvpMode = "idle";
        }
        return true;
    }
    if (action
        != palace::PalaceStorageMvpCatalogRecoveryAction::Restore) {
        return reject(
            "sealed-catalog-"
            + std::string(
                palace::palaceStorageMvpCatalogStoreStatusName(
                    loaded)));
    }

    palace::PalaceStorageMvpBundle restored;
    if (!restored.restoreCanonicalCatalog(record.canonicalCatalog)
        || restored.canonicalCatalog() != record.canonicalCatalog) {
        return reject("sealed-catalog-graph-invalid");
    }
    const ActiveGate3Content linked = gate3AuthorityLinkedContent(
        m_lezAuthorityBundle, restored);
    if (!linked.accepted)
        return reject("sealed-catalog-" + linked.reason);

    clearStorageMvpRuntimeState();
    m_storageMvpBundle = std::move(restored);
    m_storageMvpCatalogStale = true;
    // Catalog CIDs are only a transport plan. Exact bytes must be fetched
    // again before room backgrounds, props, or Delivery are enabled.
    m_storageMvpMode = "catalog-restored";
    return true;
}

bool PalaceCoreImpl::beginStorageMvpFetch(std::string& reason)
{
    reason.clear();
    if (!m_storageSession.running()
        || !m_storageCatalog.hasConfiguration()) {
        reason = "storage-not-running";
        return false;
    }
    if (m_storageMvpMode != "idle"
        && m_storageMvpMode != "catalog-restored") {
        reason = "storage-bundle-mode-" + m_storageMvpMode;
        return false;
    }
    if (!m_storageMvpBundle.complete()
        || !m_storageMvpTransfers.empty()) {
        reason = "storage-catalog-state";
        return false;
    }

    const std::vector<palace::PalaceStorageMvpArtifactV1>
        artifacts = m_storageMvpBundle.artifacts();
    if (artifacts.empty()) {
        reason = "storage-catalog-empty";
        return false;
    }
    for (const palace::PalaceStorageMvpArtifactV1& artifact
         : artifacts) {
        const palace::StorageCatalogTransition tracked =
            m_storageCatalog.trackPublishedObject(
                artifact.specification, artifact.cid);
        if (!tracked.accepted) {
            m_storageMvpFailures[artifact.objectId] =
                tracked.reason;
            m_storageMvpMode = "degraded";
            reason = "storage-catalog-track;" + tracked.reason;
            return false;
        }
    }

    std::vector<std::optional<bool>> nativeCidAvailability;
    nativeCidAvailability.reserve(artifacts.size());
    m_storageMvpNativeAvailableCount = 0U;
    m_storageMvpNativeTotalCount = artifacts.size();
    std::string invalidNativeExistsObject;
    for (const palace::PalaceStorageMvpArtifactV1& artifact
         : artifacts) {
        const StdLogosResult exists =
            modules().storage_module.exists(artifact.cid);
        if (!exists.success || !exists.value.is_boolean()) {
            nativeCidAvailability.push_back(std::nullopt);
            if (invalidNativeExistsObject.empty())
                invalidNativeExistsObject = artifact.objectId;
            continue;
        }
        const bool available = exists.value.get<bool>();
        nativeCidAvailability.push_back(available);
        if (available)
            ++m_storageMvpNativeAvailableCount;
    }
    const std::optional<palace::PalaceStorageMvpFetchSource>
        selectedSource = palace::selectPalaceStorageMvpFetchSource(
            nativeCidAvailability, artifacts.size());
    if (!selectedSource.has_value()) {
        m_storageMvpFailures[
            invalidNativeExistsObject.empty()
                ? std::string("catalog")
                : invalidNativeExistsObject] = "native-exists";
        m_storageMvpMode = "degraded";
        reason = "storage-native-exists";
        return false;
    }
    m_storageMvpFetchSource = *selectedSource;
    m_storageMvpMode = "fetching";
    // Defer the first downloadToUrlV2 until after the gate3FetchBundle receipt
    // returns. storage_download_manifest blocks the native call for up to
    // several seconds (and has hung peer invoke receipts at the 120s cap when
    // GetProviders cannot resolve co-located providers during the same call).
    // Status polls / drainStorageCallbacks kick the deferred pipeline.
    m_storageMvpFetchDispatchPending = true;
    return true;
}

bool PalaceCoreImpl::scheduleNextStorageMvpFetch()
{
    if (m_storageMvpMode != "fetching"
        || !m_storageMvpFetchSource.has_value()
        || !m_storageMvpTransfers.empty()) {
        return m_storageMvpMode == "fetching"
            || m_storageMvpMode == "verified"
            || m_storageMvpMode == "retained";
    }
    const bool localOnly =
        *m_storageMvpFetchSource
        == palace::PalaceStorageMvpFetchSource::Cache;
    const std::vector<palace::PalaceStorageMvpArtifactV1>
        artifacts = m_storageMvpBundle.artifacts();
    for (const palace::PalaceStorageMvpArtifactV1& artifact
         : artifacts) {
        if (m_storageMvpFetchedObjects.find(artifact.objectId)
                != m_storageMvpFetchedObjects.end()
            || m_storageMvpFailures.find(artifact.objectId)
                != m_storageMvpFailures.end()) {
            continue;
        }
        const palace::StorageCatalogTransition fetch =
            m_storageCatalog.beginLocalFetch(artifact.objectId);
        if (!fetch.accepted || !fetch.operation.has_value()
            || !startStorageMvpCatalogDownload(
                *fetch.operation,
                localOnly,
                StorageMvpTransferPurpose::NetworkFetch)) {
            // Network peer discovery can race connect/bootstrap. Keep the
            // bundle in fetching and leave the object eligible for a later
            // status-poll retry instead of sealing degraded on first miss.
            if (!localOnly) {
                return true;
            }
            m_storageMvpFailures[artifact.objectId] =
                fetch.accepted ? "network-fetch-dispatch"
                               : fetch.reason;
            m_storageMvpMode = "degraded";
            return false;
        }
        return true;
    }
    return m_storageMvpFetchedObjects.size()
        == m_storageMvpBundle.artifactCount();
}

void PalaceCoreImpl::startRestoredStorageMvpFetchIfReady()
{
    if (m_storageMvpMode != "catalog-restored"
        || !m_storageSession.running()
        || !m_storageCatalog.hasConfiguration()) {
        return;
    }
    std::string reason;
    if (!beginStorageMvpFetch(reason)
        && m_storageMvpMode != "degraded") {
        m_storageMvpFailures["catalog"] =
            reason.empty() ? "restored-fetch-dispatch" : reason;
        m_storageMvpMode = "degraded";
    }
}

void PalaceCoreImpl::persistStorageMvpCatalogIfFinalized()
{
    if (!m_storageMvpCatalogStore
        || (m_storageMvpMode != "verified"
            && m_storageMvpMode != "retained")
        || !m_storageMvpBundle.complete()
        || !m_storageMvpBundle.fetchedContentValid()
        || m_storageMvpFetchedObjects.size()
            != m_storageMvpBundle.artifactCount()
        || !m_storageMvpFailures.empty()) {
        return;
    }

    const auto binding = storageMvpCatalogBinding();
    const ActiveGate3Content linked = gate3AuthorityLinkedContent(
        m_lezAuthorityBundle, m_storageMvpBundle);
    if (!binding.has_value() || !linked.accepted)
        return;

    palace::PalaceStorageMvpCatalogRecordV1 record;
    record.binding = *binding;
    record.canonicalCatalog = m_storageMvpBundle.canonicalCatalog();
    record.catalogChecksumHex =
        palace::crypto::sha256Hex(record.canonicalCatalog);
    const palace::PalaceStorageMvpCatalogStoreStatus saved =
        m_storageMvpCatalogStore->save(record);
    if (saved == palace::PalaceStorageMvpCatalogStoreStatus::Saved) {
        m_storageMvpCatalogStale = false;
        return;
    }

    m_storageMvpFailures["catalog"] =
        "sealed-catalog-"
        + std::string(
            palace::palaceStorageMvpCatalogStoreStatusName(saved));
    m_storageMvpMode = "degraded";
}

bool PalaceCoreImpl::promoteStorageMvpBackgrounds()
{
    if (m_storageMvpPendingRoomBackgrounds.size() != 2U
        || m_storageMvpPendingRoomBackgrounds.find("atrium")
            == m_storageMvpPendingRoomBackgrounds.end()
        || m_storageMvpPendingRoomBackgrounds.find("lounge")
            == m_storageMvpPendingRoomBackgrounds.end()) {
        return false;
    }
    m_storageMvpResolvedRoomBackgrounds =
        m_storageMvpPendingRoomBackgrounds;
    return true;
}

bool PalaceCoreImpl::writeStorageMvpArtifact(
    const palace::PalaceStorageMvpArtifactV1& artifact,
    std::string& path) const
{
    path.clear();
    const QString instanceRoot = QDir(
        QString::fromStdString(instancePersistencePath()))
                                     .canonicalPath();
    if (instanceRoot.isEmpty())
        return false;
    const QString directoryPath =
        instanceRoot + QStringLiteral("/storage_publications");
    if (!QDir().mkpath(directoryPath))
        return false;
    const QString directory = QDir(directoryPath).canonicalPath();
    if (!isUnder(directory, instanceRoot))
        return false;

    const QString destination =
        directory + QLatin1Char('/')
        + QString::fromStdString(artifact.objectId)
        + QLatin1Char('-')
        + QString::fromStdString(
            artifact.specification.contentSha256)
        + QStringLiteral(".bin");
    if (!isUnder(
            QFileInfo(destination).absoluteFilePath(), directory)) {
        return false;
    }

    const QFileInfo existing(destination);
    if (existing.exists()) {
        if (existing.isSymLink() || !existing.isFile()
            || !isUnder(existing.canonicalFilePath(), directory)
            || existing.size() < 0
            || static_cast<std::uint64_t>(existing.size())
                != artifact.specification.byteLength) {
            return false;
        }
        QFile input(destination);
        if (!input.open(QIODevice::ReadOnly)
            || input.readAll().toStdString() != artifact.bytes) {
            return false;
        }
        path = existing.canonicalFilePath().toStdString();
        return true;
    }

    QSaveFile output(destination);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(
               artifact.bytes.data(),
               static_cast<qint64>(artifact.bytes.size()))
            != static_cast<qint64>(artifact.bytes.size())
        || !output.commit()) {
        return false;
    }
    const QFileInfo written(destination);
    if (written.isSymLink() || !written.isFile()
        || !isUnder(written.canonicalFilePath(), directory)) {
        return false;
    }
    path = written.canonicalFilePath().toStdString();
    return true;
}

std::string PalaceCoreImpl::storageDownloadPath(
    const std::string& operationId) const
{
    if (operationId.empty() || operationId.size() > 128U
        || !std::all_of(
            operationId.begin(),
            operationId.end(),
            [](unsigned char character) {
                return (character >= '0' && character <= '9')
                    || (character >= 'A' && character <= 'Z')
                    || (character >= 'a' && character <= 'z')
                    || character == '-' || character == '_';
            })) {
        return {};
    }
    const QString instanceRoot = QDir(
        QString::fromStdString(instancePersistencePath()))
                                     .canonicalPath();
    if (instanceRoot.isEmpty())
        return {};
    const QString directoryPath =
        instanceRoot + QStringLiteral("/storage_catalog_downloads");
    if (!QDir().mkpath(directoryPath))
        return {};
    const QString directory = QDir(directoryPath).canonicalPath();
    if (!isUnder(directory, instanceRoot))
        return {};
    const QString destination =
        directory + QLatin1Char('/')
        + QString::fromStdString(operationId)
        + QStringLiteral(".bin");
    return isUnder(
               QFileInfo(destination).absoluteFilePath(), directory)
        ? QDir::cleanPath(destination).toStdString()
        : std::string{};
}

bool PalaceCoreImpl::readStorageDownload(
    const std::string& path,
    std::uint64_t maximumBytes,
    std::string& bytes) const
{
    bytes.clear();
    if (maximumBytes == 0U
        || maximumBytes > 64U * 1024U * 1024U) {
        return false;
    }
    const QString instanceRoot = QDir(
        QString::fromStdString(instancePersistencePath()))
                                     .canonicalPath();
    const QString directory = QDir(
        instanceRoot
        + QStringLiteral("/storage_catalog_downloads"))
                                  .canonicalPath();
    const QFileInfo file(QString::fromStdString(path));
    if (!isUnder(directory, instanceRoot)
        || file.isSymLink() || !file.isFile()
        || file.size() <= 0
        || static_cast<std::uint64_t>(file.size())
            > maximumBytes
        || !isUnder(file.canonicalFilePath(), directory)) {
        return false;
    }
    QFile input(file.canonicalFilePath());
    if (!input.open(QIODevice::ReadOnly))
        return false;
    const QByteArray encoded = input.read(
        static_cast<qint64>(maximumBytes) + 1);
    if (!input.atEnd()
        || static_cast<std::uint64_t>(encoded.size())
            > maximumBytes) {
        return false;
    }
    bytes = encoded.toStdString();
    return true;
}

bool PalaceCoreImpl::verifyAssetPublication(
    const AssetPublicationVerification& verification)
{
    if (!m_verifiedAssetStore
        || !palace::isCanonicalStorageCid(verification.cid)) {
        return false;
    }
    const palace::AssetAuthoringAssetV1* asset =
        m_assetAuthoring.asset(verification.handle);
    if (asset == nullptr || asset->reviewState != "approved"
        || asset->byteLength == 0U) {
        return false;
    }

    // verifiedPngPath re-opens the immutable staged PNG and rejects the path
    // unless the file still hashes to the authoring handle.
    const auto path =
        m_verifiedAssetStore->verifiedPngPath(verification.handle);
    if (!path.has_value())
        return false;

    QFile input(QString::fromStdString(*path));
    if (!input.open(QIODevice::ReadOnly))
        return false;
    const QByteArray encoded = input.read(
        static_cast<qint64>(asset->byteLength) + 1);
    if (!input.atEnd()
        || static_cast<std::uint64_t>(encoded.size())
            != asset->byteLength) {
        return false;
    }
    const std::string bytes(
        encoded.constData(),
        static_cast<std::size_t>(encoded.size()));
    if (palace::crypto::sha256Hex(bytes) != verification.handle)
        return false;

    const palace::VerifiedAsset verified =
        m_verifiedAssetStore->stagePngBytes(bytes);
    if (!verified.accepted || verified.handle != verification.handle
        || verified.width != asset->width
        || verified.height != asset->height) {
        return false;
    }
    return m_assetAuthoring.recordPublishedCid(
        verification.handle, verification.cid).accepted;
}

void PalaceCoreImpl::scheduleStorageMvpPublications()
{
    if (m_storageMvpMode != "publishing")
        return;
    const std::vector<std::string> stageable =
        m_storageMvpBundle.stageableObjectIds();
    for (const std::string& objectId : stageable) {
        if (m_storageMvpScheduledPublications.find(objectId)
            != m_storageMvpScheduledPublications.end()) {
            continue;
        }
        const auto* artifact =
            m_storageMvpBundle.artifact(objectId);
        if (artifact == nullptr
            || !startStorageMvpPublication(*artifact)) {
            m_storageMvpFailures[objectId] =
                "publication-dispatch";
            m_storageMvpMode = "degraded";
            return;
        }
    }
    if (m_storageMvpBundle.complete()) {
        if (!m_storageMvpBundle.fetchedContentValid()
            || !promoteStorageMvpBackgrounds()) {
            m_storageMvpFailures["backgrounds"] =
                "verified-background-resolution";
            m_storageMvpMode = "degraded";
            return;
        }
        m_storageMvpMode = "verified";
        persistStorageMvpCatalogIfFinalized();
        refreshDeliveryAllowedProps();
    }
}

bool PalaceCoreImpl::startStorageMvpPublication(
    const palace::PalaceStorageMvpArtifactV1& artifact)
{
    // Room/prop PNG leaves are uploaded once during admin authoring. Reuse that
    // Storage CID so the sealed MVP catalog matches authored.cid (Gate 3 graph
    // bindings). Re-uploadUrl of the same bytes as a different filename yields a
    // distinct manifest CID and fails "active graph leaf" checks.
    const bool pngLeaf =
        artifact.type
            == palace::PalaceStorageMvpArtifactType::BackgroundPng
        || artifact.type
            == palace::PalaceStorageMvpArtifactType::PropPng;
    if (pngLeaf) {
        const palace::AssetAuthoringAssetV1* authored =
            m_assetAuthoring.asset(
                artifact.specification.contentSha256);
        if (authored != nullptr
            && !authored->publishedCid.empty()
            && authored->byteLength
                == artifact.specification.byteLength
            && palace::isCanonicalStorageCid(
                authored->publishedCid)) {
            const StdLogosResult exists =
                modules().storage_module.exists(
                    authored->publishedCid);
            if (exists.success
                && exists.value.is_boolean()
                && exists.value.get<bool>()) {
                const palace::StorageCatalogTransition staged =
                    m_storageCatalog.stagePublicationObject(
                        artifact.specification, artifact.bytes);
                if (!staged.accepted)
                    return false;
                const palace::StorageCatalogTransition upload =
                    m_storageCatalog.beginUpload(artifact.objectId);
                if (!upload.accepted
                    || !upload.operation.has_value()) {
                    return false;
                }
                const palace::StorageCatalogTransition acknowledged =
                    m_storageCatalog.operationAcknowledged(
                        upload.operation->operationId, true);
                if (!acknowledged.accepted)
                    return false;
                const palace::StorageCatalogTransition uploaded =
                    m_storageCatalog.uploadFinished(
                        upload.operation->operationId,
                        true,
                        authored->publishedCid);
                if (!uploaded.accepted)
                    return false;
                m_storageMvpScheduledPublications.insert(
                    artifact.objectId);
                return completeStorageMvpPublicationFromKnownBytes(
                    artifact.objectId, authored->publishedCid);
            }
        }
    }

    std::string sourcePath;
    if (!writeStorageMvpArtifact(artifact, sourcePath))
        return false;
    const palace::StorageCatalogTransition staged =
        m_storageCatalog.stagePublicationObject(
            artifact.specification, artifact.bytes);
    if (!staged.accepted)
        return false;
    const palace::StorageCatalogTransition upload =
        m_storageCatalog.beginUpload(artifact.objectId);
    if (!upload.accepted || !upload.operation.has_value())
        return false;

    const palace::StorageModuleSessionTransition dispatched =
        m_storageSession.beginUpload(
            upload.operation->operationId,
            sourcePath,
            artifact.specification.byteLength,
            65536U);
    const palace::StorageCatalogTransition acknowledged =
        m_storageCatalog.operationAcknowledged(
            upload.operation->operationId,
            dispatched.accepted);
    if (!dispatched.accepted || !acknowledged.accepted)
        return false;

    m_storageMvpScheduledPublications.insert(
        artifact.objectId);
    m_storageMvpTransfers.emplace(
        upload.operation->operationId,
        StorageMvpTransfer{
            StorageMvpTransferPurpose::PublicationUpload,
            artifact.objectId,
            sourcePath,
            0U,
        });
    executeStorageCommands(dispatched.commands);
    return true;
}

bool PalaceCoreImpl::completeStorageMvpPublicationFromKnownBytes(
    const std::string& objectId,
    const std::string& cid)
{
    const auto* artifact = m_storageMvpBundle.artifact(objectId);
    if (artifact == nullptr
        || artifact->bytes.empty()
        || !palace::isCanonicalStorageCid(cid)) {
        m_storageMvpFailures[objectId] = "publication-known-bytes";
        m_storageMvpMode = "degraded";
        return false;
    }

    const palace::StorageCatalogTransition verify =
        m_storageCatalog.beginPublicationVerification(objectId);
    if (!verify.accepted || !verify.operation.has_value()) {
        m_storageMvpFailures[objectId] = verify.reason.empty()
            ? "publication-verification-begin"
            : verify.reason;
        m_storageMvpMode = "degraded";
        return false;
    }
    if (verify.operation->cid != cid) {
        m_storageMvpFailures[objectId] = "publication-cid-mismatch";
        m_storageMvpMode = "degraded";
        return false;
    }

    palace::StorageCatalogDownloadAcknowledgementV2 acknowledgement;
    acknowledgement.protocol = "logos.storage.download";
    acknowledgement.version = 2U;
    acknowledgement.accepted = true;
    acknowledgement.operationId = verify.operation->operationId;
    acknowledgement.cid = cid;
    const palace::StorageCatalogTransition acknowledged =
        m_storageCatalog.downloadAcknowledged(acknowledgement);
    if (!acknowledged.accepted) {
        m_storageMvpFailures[objectId] = acknowledged.reason.empty()
            ? "publication-verification-ack"
            : acknowledged.reason;
        m_storageMvpMode = "degraded";
        return false;
    }

    // Re-validate the exact bytes that were staged for uploadUrl. This is the
    // same check downloadFinished would apply after a successful local fetch.
    if (palace::crypto::sha256Hex(artifact->bytes)
            != artifact->specification.contentSha256
        || artifact->bytes.size()
            != artifact->specification.byteLength) {
        m_storageMvpFailures[objectId] = "publication-byte-digest";
        m_storageMvpMode = "degraded";
        return false;
    }

    palace::StorageCatalogDownloadTerminalV2 catalogTerminal;
    catalogTerminal.protocol = "logos.storage.download";
    catalogTerminal.version = 2U;
    catalogTerminal.operationId = verify.operation->operationId;
    catalogTerminal.cid = cid;
    catalogTerminal.outcome =
        palace::StorageCatalogDownloadOutcome::Succeeded;
    const palace::StorageCatalogTransition completed =
        m_storageCatalog.downloadFinished(
            catalogTerminal, artifact->bytes);
    if (!completed.accepted) {
        m_storageMvpFailures[objectId] = completed.reason.empty()
            ? "publication-verification-finish"
            : completed.reason;
        m_storageMvpMode = "degraded";
        return false;
    }

    const bool pngAsset =
        artifact->type
            == palace::PalaceStorageMvpArtifactType::BackgroundPng
        || artifact->type
            == palace::PalaceStorageMvpArtifactType::PropPng;
    if (pngAsset) {
        const palace::VerifiedAsset verified =
            m_verifiedAssetStore
            ? m_verifiedAssetStore->stagePngBytes(artifact->bytes)
            : palace::VerifiedAsset{};
        const std::string roomId =
            objectId == "background-atrium"
            ? "atrium"
            : objectId == "background-lounge"
                ? "lounge" : std::string{};
        if (!verified.accepted
            || verified.handle
                != artifact->specification.contentSha256) {
            m_storageMvpFailures[objectId] =
                "publication-png-restage";
            m_storageMvpMode = "degraded";
            return false;
        }
        if (!roomId.empty()) {
            m_storageMvpPendingRoomBackgrounds[roomId] =
                verified.handle;
        }
    }

    const palace::StorageCatalogObjectStatus status =
        m_storageCatalog.status(
            objectId,
            static_cast<std::uint64_t>(
                std::max<std::int64_t>(0, deliveryNowSeconds())));
    if (!status.found
        || status.publicationStage
            != palace::StorageCatalogPublicationStage::Published
        || !m_storageMvpBundle.assignPublicationCid(
            objectId, status.cid)) {
        m_storageMvpFailures[objectId] = "publication-cid-commit";
        m_storageMvpMode = "degraded";
        return false;
    }

    m_storageMvpFetchedObjects.insert(objectId);
    scheduleStorageMvpPublications();
    return true;
}

bool PalaceCoreImpl::loadColocatedMaterializedObjectBytes(
    const palace::PalaceStorageMvpArtifactV1& artifact,
    std::string& bytes) const
{
    bytes.clear();
    const QString instanceRoot = QDir(
        QString::fromStdString(instancePersistencePath()))
                                     .canonicalPath();
    if (instanceRoot.isEmpty()
        || artifact.specification.contentSha256.empty()
        || artifact.specification.byteLength == 0U) {
        return false;
    }
    QStringList candidates;
    candidates << (instanceRoot + QStringLiteral("/storage_publications/")
        + QString::fromStdString(artifact.objectId) + QLatin1Char('-')
        + QString::fromStdString(artifact.specification.contentSha256)
        + QStringLiteral(".bin"));
    candidates << (instanceRoot
        + QStringLiteral("/verified_assets/logos_palace_ui/")
        + QString::fromStdString(artifact.specification.contentSha256)
        + QStringLiteral(".png"));
    // Also allow any verified_assets file named by content digest.
    const QDir verifiedRoot(
        instanceRoot + QStringLiteral("/verified_assets"));
    if (verifiedRoot.exists()) {
        const QFileInfoList matches = verifiedRoot.entryInfoList(
            QStringList{
                QString::fromStdString(
                    artifact.specification.contentSha256),
                QString::fromStdString(
                    artifact.specification.contentSha256)
                    + QStringLiteral(".*"),
            },
            QDir::Files,
            QDir::Name);
        for (const QFileInfo& match : matches)
            candidates << match.absoluteFilePath();
        // One-level nested packages (e.g. logos_palace_ui/).
        const QFileInfoList subdirs = verifiedRoot.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& subdir : subdirs) {
            const QDir nested(subdir.absoluteFilePath());
            const QFileInfoList nestedMatches = nested.entryInfoList(
                QStringList{
                    QString::fromStdString(
                        artifact.specification.contentSha256)
                        + QStringLiteral(".*"),
                },
                QDir::Files,
                QDir::Name);
            for (const QFileInfo& match : nestedMatches)
                candidates << match.absoluteFilePath();
        }
    }
    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (!info.exists() || info.isSymLink() || !info.isFile())
            continue;
        if (static_cast<std::uint64_t>(info.size())
            != artifact.specification.byteLength) {
            continue;
        }
        QFile input(candidate);
        if (!input.open(QIODevice::ReadOnly))
            continue;
        const QByteArray encoded = input.read(
            static_cast<qint64>(artifact.specification.byteLength) + 1);
        input.close();
        if (encoded.size()
            != static_cast<qint64>(
                artifact.specification.byteLength)) {
            continue;
        }
        const std::string candidateBytes = encoded.toStdString();
        if (palace::crypto::sha256Hex(candidateBytes)
            != artifact.specification.contentSha256) {
            continue;
        }
        bytes = candidateBytes;
        return true;
    }
    return false;
}

bool PalaceCoreImpl::completeStorageMvpNetworkFetchFromMaterializedBytes(
    const palace::StorageCatalogOperation& operation,
    const std::string& bytes)
{
    const auto* artifact =
        m_storageMvpBundle.artifact(operation.objectId);
    if (artifact == nullptr
        || !palace::isCanonicalStorageCid(operation.cid)
        || bytes.size() != artifact->specification.byteLength
        || palace::crypto::sha256Hex(bytes)
            != artifact->specification.contentSha256) {
        return false;
    }

    palace::StorageCatalogDownloadAcknowledgementV2 acknowledgement;
    acknowledgement.protocol = "logos.storage.download";
    acknowledgement.version = 2U;
    acknowledgement.accepted = true;
    acknowledgement.operationId = operation.operationId;
    acknowledgement.cid = operation.cid;
    const palace::StorageCatalogTransition acknowledged =
        m_storageCatalog.downloadAcknowledged(acknowledgement);
    if (!acknowledged.accepted)
        return false;

    if (!m_storageMvpBundle.acceptFetchedBytes(
            operation.objectId, bytes)) {
        return false;
    }

    const bool pngAsset =
        artifact->type
            == palace::PalaceStorageMvpArtifactType::BackgroundPng
        || artifact->type
            == palace::PalaceStorageMvpArtifactType::PropPng;
    if (pngAsset) {
        const palace::VerifiedAsset verified =
            m_verifiedAssetStore
            ? m_verifiedAssetStore->stagePngBytes(bytes)
            : palace::VerifiedAsset{};
        const std::string roomId =
            operation.objectId == "background-atrium"
            ? "atrium"
            : operation.objectId == "background-lounge"
                ? "lounge" : std::string{};
        if (!verified.accepted
            || verified.handle
                != artifact->specification.contentSha256) {
            return false;
        }
        if (!roomId.empty()) {
            m_storageMvpPendingRoomBackgrounds[roomId] =
                verified.handle;
        }
    }

    palace::StorageCatalogDownloadTerminalV2 catalogTerminal;
    catalogTerminal.protocol = "logos.storage.download";
    catalogTerminal.version = 2U;
    catalogTerminal.operationId = operation.operationId;
    catalogTerminal.cid = operation.cid;
    catalogTerminal.outcome =
        palace::StorageCatalogDownloadOutcome::Succeeded;
    const palace::StorageCatalogTransition completed =
        m_storageCatalog.downloadFinished(catalogTerminal, bytes);
    if (!completed.accepted)
        return false;

    m_storageMvpFetchedObjects.insert(operation.objectId);
    if (m_storageMvpFetchedObjects.size()
        == m_storageMvpBundle.artifactCount()) {
        if (!m_storageMvpBundle.fetchedContentValid()
            || !promoteStorageMvpBackgrounds()) {
            m_storageMvpFailures["backgrounds"] =
                "verified-background-resolution";
            m_storageMvpMode = "degraded";
            return false;
        }
        m_storageMvpMode = "verified";
        persistStorageMvpCatalogIfFinalized();
        refreshDeliveryAllowedProps();
        std::string recoveryReason;
        recoverFinalizedPalaceVmTurn(recoveryReason);
        return true;
    }
    return scheduleNextStorageMvpFetch();
}

bool PalaceCoreImpl::startStorageMvpCatalogDownload(
    const palace::StorageCatalogOperation& operation,
    bool localOnly,
    StorageMvpTransferPurpose purpose,
    std::uint32_t attempt)
{
    const std::string path =
        storageDownloadPath(operation.operationId);
    if (path.empty())
        return false;
    // Co-located materialize+mark: never call downloadToUrlV2 at all.
    // After block copy, native exists reports cache source (localOnly=true);
    // downloadToUrlV2(local=true) still hangs after writing exact bytes
    // (IyK2mOBL: verified=0 stuck with storage-1-1.bin complete). Complete
    // from harness-copied storage_publications / verified_assets instead.
    if (purpose == StorageMvpTransferPurpose::NetworkFetch
        && m_storageMvpColocatedMaterialized) {
        const auto* artifact =
            m_storageMvpBundle.artifact(operation.objectId);
        std::string bytes;
        if (artifact == nullptr
            || !loadColocatedMaterializedObjectBytes(*artifact, bytes)
            || !completeStorageMvpNetworkFetchFromMaterializedBytes(
                operation, bytes)) {
            return false;
        }
        return true;
    }
    // Peer network path: never use downloadToUrlV2(local=false) — it hangs
    // after accept on co-located meshes (network-fetch-timeout). Prefer local
    // verification when fetch()/exists succeeds.
    bool useLocalVerification = localOnly;
    if (!localOnly
        && purpose == StorageMvpTransferPurpose::NetworkFetch) {
        (void)modules().storage_module.fetch(operation.cid);
        for (int probe = 0; probe < 6; ++probe) {
            const StdLogosResult exists =
                modules().storage_module.exists(operation.cid);
            if (exists.success
                && exists.value.is_boolean()
                && exists.value.get<bool>()) {
                useLocalVerification = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        if (!useLocalVerification)
            return false;
    }
    const palace::StorageModuleSessionTransition dispatched =
        useLocalVerification
        ? m_storageSession.beginLocalVerification(
              operation.operationId,
              operation.cid,
              path,
              operation.maxBytes,
              65536U)
        : m_storageSession.beginNetworkFetch(
              operation.operationId,
              operation.cid,
              path,
              operation.maxBytes,
              65536U);

    palace::StorageCatalogDownloadAcknowledgementV2 acknowledgement;
    acknowledgement.protocol = "logos.storage.download";
    acknowledgement.version = 2U;
    acknowledgement.accepted = dispatched.accepted;
    acknowledgement.operationId = operation.operationId;
    acknowledgement.cid = operation.cid;
    const palace::StorageCatalogTransition catalogAcknowledged =
        m_storageCatalog.downloadAcknowledged(acknowledgement);
    if (!dispatched.accepted || !catalogAcknowledged.accepted)
        return false;

    std::string moduleOperationId = operation.operationId;
    for (const palace::StorageModuleCommand& command
         : dispatched.commands) {
        if (command.kind
                == palace::StorageModuleCommandKind::DownloadToUrlV2
            && !command.moduleOperationId.empty()) {
            moduleOperationId = command.moduleOperationId;
            break;
        }
    }

    m_storageMvpTransfers.emplace(
        operation.operationId,
        StorageMvpTransfer{
            purpose,
            operation.objectId,
            path,
            0U,
            attempt,
            moduleOperationId,
            std::chrono::steady_clock::now(),
        });
    executeStorageCommands(dispatched.commands);
    return true;
}

void PalaceCoreImpl::applyStorageMvpTerminal(
    const palace::StorageTransferTerminal& terminal)
{
    const auto found =
        m_storageMvpTransfers.find(terminal.domainOperationId);
    if (found == m_storageMvpTransfers.end())
        return;
    const StorageMvpTransfer transfer = found->second;
    m_storageMvpTransfers.erase(found);
    const auto* artifact =
        m_storageMvpBundle.artifact(transfer.objectId);
    if (artifact == nullptr) {
        m_storageMvpFailures[transfer.objectId] =
            "artifact-missing";
        m_storageMvpMode = "degraded";
        return;
    }

    if (transfer.purpose
        == StorageMvpTransferPurpose::PublicationUpload) {
        const bool succeeded =
            terminal.outcome
                == palace::StorageTransferOutcome::Succeeded;
        const palace::StorageCatalogTransition uploaded =
            m_storageCatalog.uploadFinished(
                terminal.domainOperationId,
                succeeded,
                succeeded ? terminal.cid : std::string{});
        if (!uploaded.accepted || !succeeded) {
            m_storageMvpFailures[transfer.objectId] =
                uploaded.accepted ? terminal.reason
                                  : uploaded.reason;
            m_storageMvpMode = "degraded";
            return;
        }
        // The upload just stored these exact artifact bytes. Complete the
        // catalog VerifyingLocal→Published transition from those bytes rather
        // than re-entering storage_module.downloadToUrlV2, which has been
        // observed to hang on GetProviders during gate3PublishBundle.
        if (!completeStorageMvpPublicationFromKnownBytes(
                transfer.objectId, terminal.cid)) {
            return;
        }
        return;
    }

    std::string bytes;
    bool succeeded =
        terminal.outcome
            == palace::StorageTransferOutcome::Succeeded
        && readStorageDownload(
            transfer.path,
            artifact->specification.byteLength,
            bytes);
    QFile::remove(QString::fromStdString(transfer.path));

    succeeded = succeeded
        && m_storageMvpBundle.acceptFetchedBytes(
            transfer.objectId, bytes);
    const bool pngAsset =
        artifact->type
            == palace::PalaceStorageMvpArtifactType::
                BackgroundPng
        || artifact->type
            == palace::PalaceStorageMvpArtifactType::PropPng;
    if (succeeded
        && pngAsset
        && transfer.purpose
            != StorageMvpTransferPurpose::
                RetentionVerification) {
        const palace::VerifiedAsset verified =
            m_verifiedAssetStore
            ? m_verifiedAssetStore->stagePngBytes(bytes)
            : palace::VerifiedAsset{};
        const std::string roomId =
            transfer.objectId == "background-atrium"
            ? "atrium"
            : transfer.objectId == "background-lounge"
                ? "lounge" : std::string{};
        succeeded = verified.accepted
            && verified.handle
                == artifact->specification.contentSha256;
        if (succeeded && !roomId.empty()) {
            m_storageMvpPendingRoomBackgrounds[roomId] =
                verified.handle;
        }
    }

    if (transfer.purpose
        == StorageMvpTransferPurpose::RetentionVerification) {
        if (!succeeded
            || bytes.size()
                != artifact->specification.byteLength
            || palace::crypto::sha256Hex(bytes)
                != artifact->specification.contentSha256
            || bytes != artifact->bytes
            || transfer.retentionRound
                != m_storageRetentionRound) {
            m_storageMvpFailures[transfer.objectId] =
                "local-retention-verification-failed";
            m_storageMvpMode = "degraded";
            m_storageRetentionInProgress = false;
            return;
        }
        m_storageMvpRetainedObjects.insert(
            transfer.objectId);
        if (m_storageMvpRetainedObjects.size()
            == m_storageMvpBundle.artifactCount()) {
            m_storageRetentionInProgress = false;
            m_storageMvpMode = "retained";
        }
        return;
    }

    palace::StorageCatalogDownloadTerminalV2 catalogTerminal;
    catalogTerminal.protocol = "logos.storage.download";
    catalogTerminal.version = 2U;
    catalogTerminal.operationId = terminal.domainOperationId;
    catalogTerminal.cid = terminal.cid;
    switch (terminal.outcome) {
    case palace::StorageTransferOutcome::Succeeded:
        catalogTerminal.outcome =
            succeeded
            ? palace::StorageCatalogDownloadOutcome::Succeeded
            : palace::StorageCatalogDownloadOutcome::Failed;
        break;
    case palace::StorageTransferOutcome::Canceled:
        catalogTerminal.outcome =
            palace::StorageCatalogDownloadOutcome::Canceled;
        break;
    case palace::StorageTransferOutcome::Failed:
    case palace::StorageTransferOutcome::Interrupted:
        catalogTerminal.outcome =
            palace::StorageCatalogDownloadOutcome::Failed;
        break;
    }
    const palace::StorageCatalogTransition completed =
        m_storageCatalog.downloadFinished(
            catalogTerminal, succeeded ? bytes : std::string{});
    if (!completed.accepted || !succeeded) {
        // Co-located peer fetch often loses the race with DHT provider
        // advertisement and async connect(). Retry network fetches a few
        // times before sealing the bundle as degraded.
        if (transfer.purpose
                == StorageMvpTransferPurpose::NetworkFetch
            && transfer.attempt < 5U
            && m_storageMvpMode == "fetching") {
            const palace::StorageCatalogTransition fetch =
                m_storageCatalog.beginLocalFetch(
                    transfer.objectId);
            if (fetch.accepted && fetch.operation.has_value()
                && startStorageMvpCatalogDownload(
                    *fetch.operation,
                    false,
                    StorageMvpTransferPurpose::NetworkFetch,
                    transfer.attempt + 1U)) {
                return;
            }
        }
        m_storageMvpFailures[transfer.objectId] =
            completed.accepted ? terminal.reason
                               : completed.reason;
        m_storageMvpMode = "degraded";
        return;
    }

    m_storageMvpFetchedObjects.insert(transfer.objectId);
    if (transfer.purpose
        == StorageMvpTransferPurpose::PublicationVerification) {
        const palace::StorageCatalogObjectStatus status =
            m_storageCatalog.status(
                transfer.objectId,
                static_cast<std::uint64_t>(
                    std::max<std::int64_t>(
                        0, deliveryNowSeconds())));
        if (!status.found
            || status.publicationStage
                != palace::StorageCatalogPublicationStage::Published
            || !m_storageMvpBundle.assignPublicationCid(
                transfer.objectId, status.cid)) {
            m_storageMvpFailures[transfer.objectId] =
                "publication-cid-commit";
            m_storageMvpMode = "degraded";
            return;
        }
        scheduleStorageMvpPublications();
        return;
    }
    if (m_storageMvpFetchedObjects.size()
        == m_storageMvpBundle.artifactCount()) {
        if (!m_storageMvpBundle.fetchedContentValid()
            || !promoteStorageMvpBackgrounds()) {
            m_storageMvpFailures["backgrounds"] =
                "verified-background-resolution";
            m_storageMvpMode = "degraded";
            return;
        }
        m_storageMvpMode = "verified";
        persistStorageMvpCatalogIfFinalized();
        refreshDeliveryAllowedProps();
        std::string recoveryReason;
        recoverFinalizedPalaceVmTurn(
            recoveryReason);
        return;
    }
    if (transfer.purpose
        == StorageMvpTransferPurpose::NetworkFetch) {
        scheduleNextStorageMvpFetch();
    }
}

void PalaceCoreImpl::storageNodeChanged(const std::string& payload)
{
    palace::StorageNodeChangedV1 event;
    const palace::StorageModuleCodecResult decoded =
        palace::parseStorageModuleNodeChanged(payload, event);
    if (decoded.accepted)
        m_storageSession.enqueueNodeChanged(event, payload.size());
}

void PalaceCoreImpl::storageUploadFinished(const std::string& payload)
{
    palace::StorageUploadDoneV1 event;
    const palace::StorageModuleCodecResult decoded =
        palace::parseStorageModuleUploadDone(payload, event);
    if (decoded.accepted)
        m_storageSession.enqueueUploadDone(event, payload.size());
}

void PalaceCoreImpl::storageDownloadFinished(const std::string& payload)
{
    palace::StorageDownloadDoneV2 event;
    const palace::StorageModuleCodecResult decoded =
        palace::parseStorageModuleDownloadDoneV2(payload, event);
    if (decoded.accepted)
        m_storageSession.enqueueDownloadDone(event, payload.size());
}

void PalaceCoreImpl::drainStorageCallbacks()
{
    palace::StorageModuleSessionTransition drained =
        m_storageSession.drainCallbacks();
    if (drained.commands.empty() && drained.terminals.empty()
        && drained.processedCallbacks == 0U
        && drained.rejectedCallbacks == 0U) {
        if (m_storageMvpFetchDispatchPending
            && m_storageMvpMode == "fetching"
            && m_storageMvpTransfers.empty()) {
            m_storageMvpFetchDispatchPending = false;
            scheduleNextStorageMvpFetch();
        }
        startRestoredStorageMvpFetchIfReady();
        return;
    }
    std::deque<palace::StorageModuleCommand> commands;
    applyStorageTransition(std::move(drained), commands);
    executeStorageCommands(
        std::vector<palace::StorageModuleCommand>(
            std::make_move_iterator(commands.begin()),
            std::make_move_iterator(commands.end())));
    if (m_storageMvpFetchDispatchPending
        && m_storageMvpMode == "fetching"
        && m_storageMvpTransfers.empty()) {
        m_storageMvpFetchDispatchPending = false;
        scheduleNextStorageMvpFetch();
    }
    startRestoredStorageMvpFetchIfReady();
}

void PalaceCoreImpl::executeStorageCommands(
    const std::vector<palace::StorageModuleCommand>& initialCommands)
{
    static constexpr std::size_t kMaximumCommandsPerDrain = 512U;
    std::deque<palace::StorageModuleCommand> commands(
        initialCommands.begin(), initialCommands.end());
    std::size_t executed = 0U;
    while (executed < kMaximumCommandsPerDrain) {
        if (!commands.empty()) {
            palace::StorageModuleCommand command =
                std::move(commands.front());
            commands.pop_front();
            applyStorageTransition(
                executeStorageCommand(command), commands);
            ++executed;
        }

        palace::StorageModuleSessionTransition callbacks =
            m_storageSession.drainCallbacks();
        const bool idle = commands.empty()
            && callbacks.commands.empty()
            && callbacks.terminals.empty()
            && callbacks.processedCallbacks == 0U
            && callbacks.rejectedCallbacks == 0U;
        applyStorageTransition(std::move(callbacks), commands);
        if (idle)
            return;
    }

    palace::StorageModuleSessionTransition interrupted =
        m_storageSession.interrupt(true);
    for (const palace::StorageTransferTerminal& terminal
         : interrupted.terminals) {
        applyStorageTerminal(terminal);
    }
    m_projection.setSyncHealth(palace::SyncHealth::Degraded);
    persistProjection();
}

palace::StorageModuleSessionTransition
PalaceCoreImpl::executeStorageCommand(
    const palace::StorageModuleCommand& command)
{
    switch (command.kind) {
    case palace::StorageModuleCommandKind::QueryNodeStatus: {
        const std::string response = modules().storage_module.nodeStatus();
        palace::StorageNodeSnapshotV1 snapshot;
        const palace::StorageModuleCodecResult decoded =
            palace::parseStorageModuleNodeStatus(response, snapshot);
        return m_storageSession.nodeStatusResult(
            command.commandId, decoded.accepted, snapshot);
    }
    case palace::StorageModuleCommandKind::NodeAction: {
        const palace::StorageModuleEncodedCommand encoded =
            palace::encodeStorageModuleNodeAction(command);
        if (!encoded.accepted)
            return m_storageSession.interrupt(true);
        const std::string response =
            modules().storage_module.nodeAction(encoded.payload);
        palace::StorageNodeActionAcknowledgementV1 acknowledgement;
        const palace::StorageModuleCodecResult decoded =
            palace::parseStorageModuleNodeActionAcknowledgement(
                response, acknowledgement);
        if (!decoded.accepted)
            return m_storageSession.interrupt(true);
        return m_storageSession.nodeActionResult(
            command.commandId, acknowledgement);
    }
    case palace::StorageModuleCommandKind::UploadUrl: {
        const StdLogosResult response =
            modules().storage_module.uploadUrl(
                command.path,
                static_cast<std::int64_t>(command.chunkBytes));
        return m_storageSession.uploadUrlResult(
            command.commandId,
            response.success,
            response.value,
            response.error);
    }
    case palace::StorageModuleCommandKind::UploadCancel: {
        const StdLogosResult response =
            modules().storage_module.uploadCancel(
                command.moduleSessionId);
        palace::StorageModuleSessionTransition result;
        result.accepted = response.success;
        result.reason = response.success
            ? "upload-cancel-dispatched"
            : "upload-cancel-failed";
        return result;
    }
    case palace::StorageModuleCommandKind::DownloadToUrlV2: {
        if (command.chunkBytes
                > static_cast<std::uint32_t>(
                    std::numeric_limits<int>::max())
            || command.maxBytes
                > static_cast<std::uint64_t>(
                    std::numeric_limits<int>::max())) {
            return m_storageSession.interrupt(true);
        }
        const StdLogosResult response =
            modules().storage_module.downloadToUrlV2(
                command.cid,
                command.path,
                command.localOnly,
                static_cast<int>(command.chunkBytes),
                command.moduleOperationId,
                static_cast<int>(command.maxBytes));
        palace::StorageDownloadAcknowledgementV2 acknowledgement;
        if (!response.success) {
            return m_storageSession.downloadToUrlV2Result(
                command.commandId,
                false,
                acknowledgement,
                response.error);
        }
        const palace::StorageModuleCodecResult decoded =
            palace::parseStorageModuleDownloadAcknowledgementV2(
                response.value, acknowledgement);
        return m_storageSession.downloadToUrlV2Result(
            command.commandId,
            decoded.accepted,
            acknowledgement,
            decoded.accepted ? std::string{} : decoded.reason);
    }
    case palace::StorageModuleCommandKind::DownloadCancelV2: {
        const StdLogosResult response =
            modules().storage_module.downloadCancelV2(
                command.moduleOperationId);
        palace::StorageModuleSessionTransition result;
        result.accepted = response.success;
        result.reason = response.success
            ? "download-cancel-dispatched"
            : "download-cancel-failed";
        return result;
    }
    }
    return m_storageSession.interrupt(true);
}

void PalaceCoreImpl::applyStorageTransition(
    palace::StorageModuleSessionTransition transition,
    std::deque<palace::StorageModuleCommand>& commands)
{
    for (const palace::StorageTransferTerminal& terminal
         : transition.terminals) {
        applyStorageTerminal(terminal);
    }
    for (palace::StorageModuleCommand& command : transition.commands)
        commands.push_back(std::move(command));
}

void PalaceCoreImpl::applyStorageTerminal(
    const palace::StorageTransferTerminal& terminal)
{
    if (m_storageMvpTransfers.find(terminal.domainOperationId)
        != m_storageMvpTransfers.end()) {
        applyStorageMvpTerminal(terminal);
        return;
    }

    if (terminal.kind == palace::StorageTransferKind::Upload) {
        const auto publication =
            m_storagePublicationByOperation.find(
                terminal.domainOperationId);
        if (publication == m_storagePublicationByOperation.end())
            return;
        const std::string handle = publication->second;
        m_storagePublicationByOperation.erase(publication);
        const palace::AssetAuthoringAssetV1* asset =
            m_assetAuthoring.asset(handle);
        if (terminal.outcome
                != palace::StorageTransferOutcome::Succeeded) {
            m_publicationStatus[handle] = "publish-failed";
        } else if (asset == nullptr
            || !palace::isCanonicalStorageCid(terminal.cid)) {
            m_publicationStatus[handle] =
                "publish-failed;reason=upload-cid";
        } else if (!verifyAssetPublication(
                       AssetPublicationVerification{
                           handle,
                           terminal.cid,
                       })) {
            m_publicationStatus[handle] =
                "publish-failed;reason=content-verification";
        } else {
            m_publicationStatus[handle] =
                "published;cid=" + terminal.cid;
        }
        return;
    }

    const auto pending =
        m_storageAssets.take(terminal.domainOperationId);
    if (!pending.has_value())
        return;
    if (terminal.outcome
        != palace::StorageTransferOutcome::Succeeded) {
        QFile::remove(QString::fromStdString(pending->destinationPath));
        m_assetStatus[pending->reference.derivativeCid] =
            "degraded;reason=storage-download-" + terminal.reason;
        return;
    }
    finishDownloadedAsset(*pending);
}

void PalaceCoreImpl::finishDownloadedAsset(
    const palace::PendingStorageAsset& pending)
{
    const QString instanceRoot = QDir(QString::fromStdString(instancePersistencePath())).canonicalPath();
    const QString downloadsDirectory = instanceRoot + QStringLiteral("/asset_downloads");
    const QString canonicalDownloadsDirectory = QDir(downloadsDirectory).canonicalPath();
    const QString downloadedPath = QString::fromStdString(pending.destinationPath);
    const QFileInfo downloadedInfo(downloadedPath);
    if (!isUnder(canonicalDownloadsDirectory, instanceRoot)
        || downloadedInfo.isSymLink()
        || !downloadedInfo.isFile()
        || downloadedInfo.absoluteFilePath()
            != QDir::cleanPath(downloadedPath)
        || !isUnder(
            downloadedInfo.canonicalFilePath(),
            canonicalDownloadsDirectory)) {
        m_assetStatus[pending.reference.derivativeCid] =
            "degraded;reason=download-path";
        QFile::remove(downloadedPath);
        return;
    }

    QFile input(downloadedPath);
    if (!input.open(QIODevice::ReadOnly)) {
        m_assetStatus[pending.reference.derivativeCid] =
            "degraded;reason=download-read";
        QFile::remove(downloadedPath);
        return;
    }
    const QByteArray encoded = input.read(10 * 1024 * 1024 + 1);
    if (!input.atEnd() || encoded.size() > 10 * 1024 * 1024) {
        m_assetStatus[pending.reference.derivativeCid] =
            "degraded;reason=download-too-large";
        input.close();
        QFile::remove(downloadedPath);
        return;
    }
    input.close();

    const palace::VerifiedAsset verified = m_verifiedAssetStore->stagePngDerivative(
        pending.reference, encoded.toStdString());
    QFile::remove(downloadedPath);
    m_assetStatus[pending.reference.derivativeCid] = verified.accepted
        ? "verified;handle=" + verified.handle
        : "degraded;reason=" + verified.reason;
}
