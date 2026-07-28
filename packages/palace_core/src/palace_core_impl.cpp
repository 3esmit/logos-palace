#include "palace_core_impl.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <optional>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include "logos_sdk.h"

#include "palace_delivery.h"
#include "palace_lez.h"

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

bool jsonSuccess(const std::string& payload)
{
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(payload));
    return document.isObject() && document.object().value(QStringLiteral("success")).toBool(false);
}

bool parseUnsigned(const QJsonValue& value, std::uint64_t& result)
{
    if (!value.isString())
        return false;
    const std::string text = value.toString().toStdString();
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char character) {
            return std::isdigit(character) != 0;
        })) {
        return false;
    }
    std::uint64_t parsed = 0;
    const auto [cursor, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc() || cursor != text.data() + text.size())
        return false;
    result = parsed;
    return true;
}

std::optional<palace::PalaceLezInstructionV1> parseTransition(const std::string& transitionJson)
{
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(transitionJson));
    if (!document.isObject())
        return std::nullopt;
    const QJsonObject object = document.object();
    const QJsonValue kindValue = object.value(QStringLiteral("kind"));
    if (!kindValue.isString())
        return std::nullopt;

    palace::PalaceLezInstructionV1 instruction;
    const std::string kind = kindValue.toString().toStdString();
    if (kind == "bind_delivery_key") {
        instruction.kind = palace::PalaceLezInstructionKind::BindDeliveryKey;
        instruction.subjectAccountIdHex = object.value(QStringLiteral("subject_account_id_hex")).toString().toStdString();
        instruction.deliveryKeyHex = object.value(QStringLiteral("delivery_key_hex")).toString().toStdString();
        if (!parseUnsigned(object.value(QStringLiteral("key_epoch")), instruction.keyEpoch))
            return std::nullopt;
    } else if (kind == "delegate_moderator") {
        instruction.kind = palace::PalaceLezInstructionKind::DelegateModerator;
        instruction.subjectAccountIdHex = object.value(QStringLiteral("subject_account_id_hex")).toString().toStdString();
    } else if (kind == "revoke_moderator") {
        instruction.kind = palace::PalaceLezInstructionKind::RevokeModerator;
        instruction.subjectAccountIdHex = object.value(QStringLiteral("subject_account_id_hex")).toString().toStdString();
    } else if (kind == "ban_user") {
        instruction.kind = palace::PalaceLezInstructionKind::BanUser;
        instruction.subjectAccountIdHex = object.value(QStringLiteral("subject_account_id_hex")).toString().toStdString();
        instruction.roomId = object.value(QStringLiteral("room_id")).toString().toStdString();
    } else if (kind == "ban_asset") {
        instruction.kind = palace::PalaceLezInstructionKind::BanAsset;
        instruction.cid = object.value(QStringLiteral("cid")).toString().toStdString();
        instruction.roomId = object.value(QStringLiteral("room_id")).toString().toStdString();
    } else if (kind == "set_room_locked") {
        if (!object.value(QStringLiteral("locked")).isBool())
            return std::nullopt;
        instruction.kind = palace::PalaceLezInstructionKind::SetRoomLocked;
        instruction.roomId = object.value(QStringLiteral("room_id")).toString().toStdString();
        instruction.locked = object.value(QStringLiteral("locked")).toBool();
    } else if (kind == "publish_manifest") {
        instruction.kind = palace::PalaceLezInstructionKind::PublishManifest;
        instruction.cid = object.value(QStringLiteral("cid")).toString().toStdString();
    } else if (kind == "set_shared_spot_revision") {
        instruction.kind = palace::PalaceLezInstructionKind::SetSharedSpotRevision;
        instruction.roomId = object.value(QStringLiteral("room_id")).toString().toStdString();
        instruction.spotId = object.value(QStringLiteral("spot_id")).toString().toStdString();
        if (!parseUnsigned(object.value(QStringLiteral("revision")), instruction.revision))
            return std::nullopt;
    } else {
        return std::nullopt;
    }
    return instruction;
}

QByteArray encodeLezWords(const std::vector<std::uint32_t>& words)
{
    QByteArray bytes;
    bytes.reserve(static_cast<int>(words.size() * sizeof(std::uint32_t)));
    for (const std::uint32_t word : words) {
        bytes.append(static_cast<char>(word & 0xffU));
        bytes.append(static_cast<char>((word >> 8U) & 0xffU));
        bytes.append(static_cast<char>((word >> 16U) & 0xffU));
        bytes.append(static_cast<char>((word >> 24U) & 0xffU));
    }
    return bytes;
}

} // namespace

void PalaceCoreImpl::onContextReady()
{
    if (instancePersistencePath().empty())
        return;
    m_projectionStore = std::make_unique<palace::ProjectionStore>(instancePersistencePath());
    m_actionJournalStore = std::make_unique<palace::ActionJournalStore>(instancePersistencePath());
    m_verifiedAssetStore = std::make_unique<palace::VerifiedAssetStore>(instancePersistencePath());
    modules().storage_module.onStorageStart(
        [this](const std::string& payload) { storageStartFinished(payload); });
    modules().storage_module.onStorageDownloadDoneV2(
        [this](const std::string& payload) { storageDownloadFinished(payload); });
    if (!m_projectionStore->load(m_projection)) {
        m_projection.setSyncHealth(palace::SyncHealth::Degraded);
        persistProjection();
    }
    if (!m_actionJournalStore->load(m_actionJournal) && m_actionJournalStore->exists()) {
        m_projection.setSyncHealth(palace::SyncHealth::Degraded);
        persistProjection();
    }
}

void PalaceCoreImpl::persistProjection()
{
    if (m_projectionStore)
        m_projectionStore->save(m_projection);
}

void PalaceCoreImpl::persistActionJournal()
{
    if (m_actionJournalStore)
        m_actionJournalStore->save(m_actionJournal);
}

std::string PalaceCoreImpl::enterRoom(const std::string& roomId)
{
    if (!m_projection.enterRoom(roomId))
        return "rejected=unknown-room";
    persistProjection();
    return "ok;room=" + m_projection.currentRoomId()
        + ";title=" + m_projection.currentRoomTitle()
        + ";sync=" + palace::syncHealthName(m_projection.syncHealth());
}

std::string PalaceCoreImpl::useSpot(const std::string& spotId)
{
    if (spotId != "door")
        return "rejected=unknown-spot";
    if (!isContextReady())
        return "rejected=vm-not-ready";

    const std::string receipt = modules().palace_vm.executeTurn(
        "ON SELECT door\nSAY Welcome to the Lounge\nGOTOROOM lounge\n",
        "fixture-script-bundle-v1",
        "1",
        "SELECT:door",
        "door_open=0",
        "atrium,lounge",
        false,
        false,
        8);
    if (receipt.rfind("accepted=1;", 0) != 0
        || receipt.find("15:navigate:lounge") == std::string::npos) {
        return "rejected=vm-turn;" + receipt;
    }
    return enterRoom("lounge") + ";vm_receipt=" + receipt;
}

std::string PalaceCoreImpl::startDelivery(const std::string& nodeConfig)
{
    if (!isContextReady() || nodeConfig.empty())
        return "rejected=delivery-not-ready-or-empty-config";
    if (!m_deliveryNodeCreated) {
        const StdLogosResult created = modules().delivery_module.createNode(nodeConfig);
        if (!created.success)
            return "rejected=delivery-create-node;" + created.error;
        m_deliveryNodeCreated = true;
    }
    const StdLogosResult started = modules().delivery_module.start();
    if (!started.success)
        return "rejected=delivery-start;" + started.error;
    return "ok;delivery=start-requested";
}

std::string PalaceCoreImpl::subscribeRoom(const std::string& networkId,
                                          const std::string& palaceId,
                                          const std::string& roomId,
                                          std::int64_t roomEpoch)
{
    if (!isContextReady() || !m_deliveryNodeCreated || roomEpoch < 0)
        return "rejected=delivery-node-not-created";
    const std::string topic = palace::deriveRoomTopic(networkId, palaceId, roomId, roomEpoch);
    const StdLogosResult subscribed = modules().delivery_module.subscribe(topic);
    if (!subscribed.success)
        return "rejected=delivery-subscribe;" + subscribed.error;
    return "ok;topic=" + topic;
}

std::string PalaceCoreImpl::startStorage(const std::string& nodeConfig)
{
    if (!isContextReady() || instancePersistencePath().empty() || nodeConfig.empty())
        return "rejected=storage-not-ready-or-empty-config";
    if (m_storageRunning)
        return "ok;storage=running";
    if (m_storageStartRequested)
        return "ok;storage=start-requested";

    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(
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
    config.insert(QStringLiteral("data-dir"), canonicalStorageDirectory);
    config.remove(QStringLiteral("log-file"));
    const std::string canonicalConfig = QJsonDocument(config).toJson(QJsonDocument::Compact).toStdString();

    if (!m_storageNodeCreated) {
        if (!modules().storage_module.init(canonicalConfig))
            return "rejected=storage-init";
        m_storageNodeCreated = true;
    }
    m_storageStartRequested = true;
    if (!modules().storage_module.start()) {
        m_storageStartRequested = false;
        return "rejected=storage-start";
    }
    return "ok;storage=start-requested";
}

std::string PalaceCoreImpl::fetchPngDerivative(const std::string& sourceCid,
                                                const std::string& derivativeCid,
                                                std::uint64_t byteLength,
                                                const std::string& contentSha256,
                                                std::uint32_t width,
                                                std::uint32_t height)
{
    if (!isContextReady() || !m_verifiedAssetStore || !m_storageRunning)
        return "rejected=storage-not-running";

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

    m_assetStatus[reference.derivativeCid] = "downloading";
    const StdLogosResult accepted = modules().storage_module.downloadToUrlV2(
        reference.derivativeCid, pending->destinationPath, false, 65536,
        pending->operationId, 10 * 1024 * 1024);
    if (!accepted.success) {
        m_storageAssets.cancel(pending->operationId);
        m_assetStatus.erase(reference.derivativeCid);
        return "rejected=storage-download;" + accepted.error;
    }
    return "ok;asset=download-requested;operation=" + pending->operationId;
}

std::string PalaceCoreImpl::assetStatus(const std::string& derivativeCid) const
{
    const auto found = m_assetStatus.find(derivativeCid);
    return found == m_assetStatus.end() ? "unknown" : found->second;
}

std::string PalaceCoreImpl::roomTitle() const
{
    return m_projection.currentRoomTitle();
}

std::string PalaceCoreImpl::syncHealth() const
{
    return palace::syncHealthName(m_projection.syncHealth());
}

std::string PalaceCoreImpl::localProjection() const
{
    return m_projection.canonicalLocalState();
}

std::string PalaceCoreImpl::submitIntent(const std::string& actionId)
{
    const bool changed = m_actionJournal.createDraft(actionId) && m_actionJournal.queue(actionId);
    if (changed)
        persistActionJournal();
    return result(changed, m_actionJournal.status(actionId));
}

std::string PalaceCoreImpl::submitPalaceTransition(const std::string& actionId,
                                                    const std::string& stateAccountIdHex,
                                                    const std::string& callerAccountIdHex,
                                                    const std::string& programIdHex,
                                                    const std::string& transitionJson)
{
    if (!isContextReady())
        return "rejected=lez-not-ready";
    if (m_actionJournal.status(actionId).durableStage != palace::DurableActionStage::Queued)
        return "rejected=action-not-queued";

    const auto instruction = parseTransition(transitionJson);
    if (!instruction.has_value())
        return "rejected=invalid-palace-transition";
    palace::PalaceLezSubmitRequestV1 request;
    request.stateAccountIdHex = stateAccountIdHex;
    request.callerAccountIdHex = callerAccountIdHex;
    request.programIdHex = programIdHex;
    request.instruction = *instruction;
    const palace::PalaceLezWireInstruction wire = palace::PalaceLezCodec::encodeApply(request);
    if (!wire.accepted)
        return "rejected=invalid-palace-wire;reason=" + wire.reason;

    const QStringList accounts {
        QString::fromStdString(request.stateAccountIdHex),
        QString::fromStdString(request.callerAccountIdHex),
    };
    const QVariantList signers {false, true};
    logos::CallError callError;
    const QString response = modules().lez_core.send_generic_public_transaction(
        accounts,
        signers,
        QVariant::fromValue(encodeLezWords(wire.words)),
        QString::fromStdString(request.programIdHex),
        &callError);
    if (!callError.ok())
        return "rejected=lez-submit-call-failed";
    const palace::PalaceLezSubmissionResult submitted =
        palace::PalaceLezCodec::parseSubmissionResult(response.toStdString());
    if (!submitted.accepted)
        return "rejected=lez-submit;reason=" + submitted.reason;
    if (!m_actionJournal.markSubmittedToLez(actionId, submitted.transactionHash))
        return "rejected=action-stage-changed";
    persistActionJournal();
    return "ok;tx_hash=" + submitted.transactionHash + ";"
        + palace::canonicalActionStatus(m_actionJournal.status(actionId));
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
    return palace::canonicalActionStatus(m_actionJournal.status(actionId));
}

void PalaceCoreImpl::storageStartFinished(const std::string& payload)
{
    m_storageStartRequested = false;
    m_storageRunning = jsonSuccess(payload);
}

void PalaceCoreImpl::storageDownloadFinished(const std::string& payload)
{
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(payload));
    if (!document.isObject())
        return;
    const QJsonObject result = document.object();
    const QJsonValue operationValue = result.value(QStringLiteral("moduleOperationId"));
    const QJsonValue outcomeValue = result.value(QStringLiteral("outcome"));
    if (!operationValue.isString() || !outcomeValue.isString())
        return;

    const auto pending = m_storageAssets.take(operationValue.toString().toStdString());
    if (!pending.has_value())
        return;
    if (outcomeValue.toString() != QStringLiteral("succeeded")) {
        m_assetStatus[pending->reference.derivativeCid] = "download-failed";
        return;
    }

    const QString instanceRoot = QDir(QString::fromStdString(instancePersistencePath())).canonicalPath();
    const QString downloadsDirectory = instanceRoot + QStringLiteral("/asset_downloads");
    const QString canonicalDownloadsDirectory = QDir(downloadsDirectory).canonicalPath();
    const QString downloadedPath = QString::fromStdString(pending->destinationPath);
    const QFileInfo downloadedInfo(downloadedPath);
    if (!isUnder(canonicalDownloadsDirectory, instanceRoot)
        || downloadedInfo.isSymLink()
        || !downloadedInfo.isFile()
        || !isUnder(downloadedInfo.absolutePath(), canonicalDownloadsDirectory)) {
        m_assetStatus[pending->reference.derivativeCid] = "download-path-rejected";
        return;
    }

    QFile input(downloadedPath);
    if (!input.open(QIODevice::ReadOnly)) {
        m_assetStatus[pending->reference.derivativeCid] = "download-read-failed";
        return;
    }
    const QByteArray encoded = input.read(10 * 1024 * 1024 + 1);
    if (!input.atEnd() || encoded.size() > 10 * 1024 * 1024) {
        m_assetStatus[pending->reference.derivativeCid] = "download-too-large";
        input.close();
        QFile::remove(downloadedPath);
        return;
    }
    input.close();

    const palace::VerifiedAsset verified = m_verifiedAssetStore->stagePngDerivative(
        pending->reference, encoded.toStdString());
    QFile::remove(downloadedPath);
    m_assetStatus[pending->reference.derivativeCid] = verified.accepted
        ? "verified;handle=" + verified.handle
        : "rejected=" + verified.reason;
}
