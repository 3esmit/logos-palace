#include "logos_palace_ui_backend.h"

#include <algorithm>

#include <QByteArray>

#include <cstdint>
#include <limits>

#include "logos_sdk.h"

namespace {

constexpr int kMaximumSpotReconcilePolls = 1'200;

QString statusValue(const QString& status, const QString& name)
{
    const QString prefix = name + QLatin1Char('=');
    const QStringList fields = status.split(QLatin1Char(';'));
    const auto found = std::find_if(
        fields.begin(), fields.end(),
        [&prefix](const QString& field) {
            return field.startsWith(prefix);
        });
    return found == fields.end()
        ? QString{}
        : found->mid(prefix.size());
}

bool hasSpotAction(const QString& actionId)
{
    return !actionId.isEmpty()
        && actionId != QStringLiteral("none");
}

bool isPromotedSpot(const QString& status)
{
    return statusValue(status, QStringLiteral("vm"))
            == QStringLiteral("promoted")
        && statusValue(status, QStringLiteral("navigation"))
            == QStringLiteral("1");
}

bool isDegradedSpot(const QString& status)
{
    return statusValue(status, QStringLiteral("vm"))
        == QStringLiteral("degraded");
}

bool isLowerHex64(const QString& value)
{
    return value.size() == 64
        && std::all_of(
            value.begin(),
            value.end(),
            [](const QChar character) {
                const ushort value = character.unicode();
                return (value >= '0' && value <= '9')
                    || (value >= 'a' && value <= 'f');
            });
}

bool isAssetIdentifier(const QString& value)
{
    return !value.isEmpty() && value.size() <= 64
        && std::all_of(
            value.begin(), value.end(), [](const QChar character) {
                const ushort value = character.unicode();
                return (value >= 'a' && value <= 'z')
                    || (value >= '0' && value <= '9')
                    || value == '-' || value == '_';
            });
}

} // namespace

QString LogosPalaceUiBackend::applicationRoundTrip(
    QString payload)
{
    if (!isContextReady())
        return unavailableReceipt();
    const qsizetype bytes = payload.toUtf8().size();
    if (bytes != 0 && bytes != 256 && bytes != 4096) {
        return QStringLiteral(
            "rejected=application-round-trip-size");
    }
    return modules().palace_core.applicationRoundTrip(payload);
}

void LogosPalaceUiBackend::onContextReady()
{
    refreshRoomProjection();
    refreshDeliveryState();
    refreshDeliveryNodeEvidence();
    refreshStorageState();
    refreshLezState();
    refreshPalaceState();
    refreshModerationState();
    refreshSpotState();

    if (!m_deliveryPollTimer) {
        m_deliveryPollTimer = new QTimer(this);
        m_deliveryPollTimer->setInterval(500);
        connect(m_deliveryPollTimer, &QTimer::timeout, this, [this]() {
            refreshDeliveryState();
            refreshStorageState();
            refreshLezState();
            refreshPalaceState();
            refreshModerationState();
            refreshSpotState();
            if (m_spotTracking)
                driveSpotAction();
        });
        m_deliveryPollTimer->start();
    }

    if (!m_nodeEvidencePollTimer) {
        m_nodeEvidencePollTimer = new QTimer(this);
        m_nodeEvidencePollTimer->setInterval(3'000);
        connect(m_nodeEvidencePollTimer, &QTimer::timeout, this, [this]() {
            refreshDeliveryNodeEvidence();
        });
        m_nodeEvidencePollTimer->start();
    }

    if (!m_autoStartAttempted) {
        m_autoStartAttempted = true;
        const QByteArray config = qgetenv("PALACE_DELIVERY_CONFIG");
        if (!config.isEmpty()) {
            if (config.size() > 64 * 1024) {
                setLastActionReceipt(
                    QStringLiteral("rejected=delivery-config-too-large"));
            } else {
                startDelivery(QString::fromUtf8(config));
            }
        }
    }
}

QString LogosPalaceUiBackend::enterRoom(QString roomId)
{
    if (!isContextReady())
        return QStringLiteral("rejected=core-not-ready");
    const QString result = modules().palace_core.enterRoom(roomId);
    if (result.startsWith(QStringLiteral("ok;")))
        refreshRoomProjection();
    return result;
}

QString LogosPalaceUiBackend::previewSpot(QString spotId)
{
    if (!isContextReady())
        return rememberSpotReceipt(unavailableReceipt());
    return rememberSpotReceipt(
        modules().palace_core.previewSpot(spotId));
}

QString LogosPalaceUiBackend::useSpot(QString spotId)
{
    if (!isContextReady())
        return rememberSpotReceipt(unavailableReceipt());

    refreshSpotState();
    const QString currentStatus =
        modules().palace_core.spotStatus();
    const QString currentAction =
        statusValue(currentStatus, QStringLiteral("action"));
    if (m_spotTracking
        || (hasSpotAction(currentAction)
            && !isPromotedSpot(currentStatus)
            && !isDegradedSpot(currentStatus))) {
        return rememberSpotReceipt(
            QStringLiteral("rejected=spot-action-in-flight;action=")
            + currentAction);
    }
    if (m_spotPollBudgetExceeded) {
        return rememberSpotReceipt(
            QStringLiteral("rejected=spot-reconcile-budget-exhausted;action=")
            + currentAction);
    }

    const QString result = modules().palace_core.useSpot(spotId);
    rememberSpotReceipt(result);
    const QString status = modules().palace_core.spotStatus();
    applySpotStatus(status);
    if (result.startsWith(QStringLiteral("ok;"))) {
        m_spotPollCount = 0;
        m_spotPollBudgetExceeded = false;
        if (isPromotedSpot(status)) {
            m_spotTracking = false;
            refreshRoomProjection();
        } else if (isDegradedSpot(status)) {
            m_spotTracking = false;
        } else {
            m_spotTracking = hasSpotAction(
                statusValue(status, QStringLiteral("action")));
            if (!m_spotTracking) {
                const QString rejected =
                    QStringLiteral("rejected=spot-action-not-tracked");
                setSpotDegraded(
                    QStringLiteral("action-not-tracked"),
                    QString{});
                return rememberSpotReceipt(rejected);
            }
        }
    } else {
        m_spotTracking = false;
    }
    return result;
}

QString LogosPalaceUiBackend::reconcileSpot()
{
    if (!isContextReady())
        return rememberSpotReceipt(unavailableReceipt());
    refreshSpotState();
    if (!m_spotTracking)
        return refreshSpot();
    return driveSpotAction();
}

QString LogosPalaceUiBackend::refreshSpot()
{
    if (!isContextReady())
        return rememberSpotReceipt(unavailableReceipt());
    const QString status = modules().palace_core.spotStatus();
    const QString actionId =
        statusValue(status, QStringLiteral("action"));
    if (m_spotPollBudgetExceeded
        && hasSpotAction(actionId)
        && !isPromotedSpot(status)
        && !isDegradedSpot(status)) {
        setSpotDegraded(
            QStringLiteral("reconcile-budget-exhausted"),
            actionId);
        return rememberSpotReceipt(
            QStringLiteral("rejected=spot-reconcile-budget-exhausted;action=")
            + actionId);
    }
    applySpotStatus(status);
    if (isPromotedSpot(status)) {
        m_spotPollBudgetExceeded = false;
        refreshRoomProjection();
    }
    return rememberSpotReceipt(status);
}

QString LogosPalaceUiBackend::vmTurnMetrics(
    QString actionId,
    QString phase)
{
    if (!isContextReady())
        return rememberSpotReceipt(unavailableReceipt());
    if (actionId.size() > 20
        || (phase != QStringLiteral("provisional")
            && phase != QStringLiteral("finalized"))) {
        return rememberSpotReceipt(
            QStringLiteral("rejected=vm-turn-metrics-query"));
    }
    return rememberSpotReceipt(
        modules().palace_core.vmTurnMetrics(actionId, phase));
}

QString LogosPalaceUiBackend::startDelivery(QString nodeConfig)
{
    if (!isContextReady())
        return rememberDeliveryReceipt(unavailableReceipt());
    const QString result = modules().palace_core.startDelivery(nodeConfig);
    refreshDeliveryState();
    refreshDeliveryNodeEvidence();
    return rememberDeliveryReceipt(result);
}

QString LogosPalaceUiBackend::subscribeRoom(QString networkId,
                                            QString palaceId,
                                            QString roomId,
                                            qint64 roomEpoch)
{
    if (!isContextReady())
        return rememberDeliveryReceipt(unavailableReceipt());
    const QString result = modules().palace_core.subscribeRoom(
        networkId, palaceId, roomId, roomEpoch);
    refreshDeliveryState();
    return rememberDeliveryReceipt(result);
}

QString LogosPalaceUiBackend::say(QString text)
{
    if (!isContextReady())
        return rememberDeliveryReceipt(unavailableReceipt());
    const QString result = modules().palace_core.say(text);
    refreshDeliveryState();
    return rememberDeliveryReceipt(result);
}

QString LogosPalaceUiBackend::moveAvatar(qint64 x, qint64 y)
{
    if (!isContextReady())
        return rememberDeliveryReceipt(unavailableReceipt());
    const QString result = modules().palace_core.move(x, y);
    refreshDeliveryState();
    return rememberDeliveryReceipt(result);
}

QString LogosPalaceUiBackend::wearProp(QString propId)
{
    if (!isContextReady())
        return rememberDeliveryReceipt(unavailableReceipt());
    const QString result = modules().palace_core.wearProp(propId);
    refreshDeliveryState();
    return rememberDeliveryReceipt(result);
}

QString LogosPalaceUiBackend::removeProp(QString propId)
{
    if (!isContextReady())
        return rememberDeliveryReceipt(unavailableReceipt());
    const QString result = modules().palace_core.removeProp(propId);
    refreshDeliveryState();
    return rememberDeliveryReceipt(result);
}

QString LogosPalaceUiBackend::refreshPresence()
{
    if (!isContextReady())
        return rememberDeliveryReceipt(unavailableReceipt());
    const QString result = modules().palace_core.refreshPresence();
    refreshDeliveryState();
    return rememberDeliveryReceipt(result);
}

QString LogosPalaceUiBackend::startStorage(QString nodeConfig)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    const QString result = modules().palace_core.startStorage(nodeConfig);
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::fetchPngDerivative(QString sourceCid,
                                                  QString derivativeCid,
                                                  qint64 byteLength,
                                                  QString contentSha256,
                                                  qint64 width,
                                                  qint64 height)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    if (byteLength <= 0 || width <= 0 || height <= 0
        || width > std::numeric_limits<std::uint32_t>::max()
        || height > std::numeric_limits<std::uint32_t>::max()) {
        return rememberStorageReceipt(
            QStringLiteral("rejected=storage-invalid-png-metadata"));
    }
    const QString result = modules().palace_core.fetchPngDerivative(
        sourceCid,
        derivativeCid,
        QVariant::fromValue(static_cast<qulonglong>(byteLength)),
        contentSha256,
        QVariant::fromValue(static_cast<uint>(width)),
        QVariant::fromValue(static_cast<uint>(height)));
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::assetStatus(QString derivativeCid)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    refreshStorageState();
    return rememberStorageReceipt(
        modules().palace_core.assetStatus(derivativeCid));
}

QString LogosPalaceUiBackend::publishVerifiedPng(QString handle)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    const QString result =
        modules().palace_core.publishVerifiedPng(handle);
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::publicationStatus(QString handle)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    refreshStorageState();
    return rememberStorageReceipt(
        modules().palace_core.publicationStatus(handle));
}

QString LogosPalaceUiBackend::beginAssetStage(QString label)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    const QString result =
        modules().palace_core.beginAssetStage(label);
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::appendAssetStageChunk(
    QString sessionId,
    qint64 sequence,
    QString base64Chunk)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    if (sequence < 0) {
        return rememberStorageReceipt(
            QStringLiteral("rejected=asset-chunk-sequence"));
    }
    const QString result =
        modules().palace_core.appendAssetStageChunk(
            sessionId,
            QVariant::fromValue(
                static_cast<qulonglong>(sequence)),
            base64Chunk);
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::commitAssetStage(
    QString sessionId)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    const QString result =
        modules().palace_core.commitAssetStage(sessionId);
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::cancelAssetStage(
    QString sessionId)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    const QString result =
        modules().palace_core.cancelAssetStage(sessionId);
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::reviewAsset(
    QString handle,
    QString decision)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    const QString result =
        modules().palace_core.reviewAsset(
            handle, decision);
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::publishAsset(
    QString handle)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    const QString result =
        modules().palace_core.publishAsset(handle);
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::assignRoomBackground(
    QString roomId,
    QString handle)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    const QString result =
        modules().palace_core.assignRoomBackground(
            roomId, handle);
    refreshStorageState();
    refreshRoomProjection();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::assignPropAsset(
    QString propId,
    QString handle,
    qint64 anchorX,
    qint64 anchorY,
    QString layer)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    if (anchorX < 0 || anchorY < 0
        || anchorX > std::numeric_limits<std::uint32_t>::max()
        || anchorY > std::numeric_limits<std::uint32_t>::max()) {
        return rememberStorageReceipt(
            QStringLiteral("rejected=prop-anchor-invalid"));
    }
    const QString result =
        modules().palace_core.assignPropAsset(
            propId,
            handle,
            QVariant::fromValue(
                static_cast<qulonglong>(anchorX)),
            QVariant::fromValue(
                static_cast<qulonglong>(anchorY)),
            layer);
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::refreshAssetAuthoring()
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    refreshStorageState();
    return rememberStorageReceipt(
        QStringLiteral("ok;backgrounds=refreshed"));
}

QString LogosPalaceUiBackend::publishMvpStorageBundle()
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    const QString result =
        modules().palace_core.publishMvpStorageBundle();
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::mvpStorageBundleStatus()
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    const QString result =
        modules().palace_core.mvpStorageBundleStatus();
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::fetchMvpStorageBundle(
    QString catalogBase64)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    const QString result =
        modules().palace_core.fetchMvpStorageBundle(catalogBase64);
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::verifyMvpStorageRetention()
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    const QString result =
        modules().palace_core.verifyMvpStorageRetention();
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::storageObjectStatus(QString objectId)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    const QString result =
        modules().palace_core.storageObjectStatus(objectId);
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::storageSessionStatus()
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    const QString status = modules().palace_core.storageSessionStatus();
    setStorageStatus(status);
    return rememberStorageReceipt(status);
}

QString LogosPalaceUiBackend::startLez(QString password)
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());
    if (password.isEmpty() || password.toUtf8().size() > 1024)
        return rememberLezReceipt(
            QStringLiteral("rejected=lez-invalid-password"));
    const QString result = modules().palace_core.startLez(password);
    refreshLezState();
    return rememberLezReceipt(result);
}

QString LogosPalaceUiBackend::createIdentity(QString displayName)
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());
    if (displayName.isEmpty() || displayName.toUtf8().size() > 48)
        return rememberLezReceipt(
            QStringLiteral("rejected=identity-invalid-display-name"));
    const QString result =
        modules().palace_core.createIdentity(displayName);
    refreshLezState();
    return rememberLezReceipt(result);
}

QString LogosPalaceUiBackend::openPalace(QString palaceUri)
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());
    if (palaceUri.toUtf8().size() > 80)
        return rememberLezReceipt(
            QStringLiteral("rejected=invalid-palace-uri"));
    const QString result =
        modules().palace_core.openPalace(palaceUri);
    refreshPalaceState();
    refreshLezState();
    return rememberLezReceipt(result);
}

QString LogosPalaceUiBackend::refreshPalace()
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());
    const QString result = modules().palace_core.palaceStatus();
    setPalaceState(result);
    return rememberLezReceipt(result);
}

QString LogosPalaceUiBackend::refreshLez()
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());
    const QString result = modules().palace_core.lezStatus();
    setLezState(result);
    return rememberLezReceipt(result);
}

QString LogosPalaceUiBackend::refreshIdentity()
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());
    const QString result = modules().palace_core.identityStatus();
    setIdentityState(result);
    return rememberLezReceipt(result);
}

QString LogosPalaceUiBackend::banUser(QString subjectUserIdHex)
{
    if (!isContextReady())
        return rememberModerationReceipt(
            QStringLiteral("user"),
            subjectUserIdHex,
            unavailableReceipt());
    if (!isLowerHex64(subjectUserIdHex))
        return rememberModerationReceipt(
            QStringLiteral("user"),
            subjectUserIdHex,
            QStringLiteral("rejected=moderation-user-invalid"));

    setModerationState(
        QStringLiteral("state=submitting;kind=user;action=;target=")
        + subjectUserIdHex);
    const QString result =
        modules().palace_core.banUser(subjectUserIdHex);
    refreshLezState();
    return rememberModerationReceipt(
        QStringLiteral("user"),
        subjectUserIdHex,
        result);
}

QString LogosPalaceUiBackend::banProp(QString propId)
{
    if (!isContextReady())
        return rememberModerationReceipt(
            QStringLiteral("prop"), propId, unavailableReceipt());
    if (!isAssetIdentifier(propId))
        return rememberModerationReceipt(
            QStringLiteral("prop"),
            propId,
            QStringLiteral("rejected=moderation-prop-invalid"));

    setModerationState(
        QStringLiteral("state=submitting;kind=prop;action=;target=")
        + propId);
    const QString result = modules().palace_core.banProp(propId);
    refreshLezState();
    return rememberModerationReceipt(
        QStringLiteral("prop"), propId, result);
}

QString LogosPalaceUiBackend::refreshModeration()
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());
    const QString result =
        modules().palace_core.moderationStatus();
    setModerationState(result);
    m_moderationActionId =
        statusValue(result, QStringLiteral("action"));
    m_moderationKind =
        statusValue(result, QStringLiteral("kind"));
    m_moderationTarget =
        statusValue(result, QStringLiteral("target"));
    return rememberLezReceipt(result);
}

QString LogosPalaceUiBackend::submitPalaceTransition(
    QString actionId,
    QString stateAccountIdHex,
    QString callerAccountIdHex,
    QString programIdHex,
    QString transitionJson)
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());
    if (transitionJson.toUtf8().size() > 64 * 1024)
        return rememberLezReceipt(
            QStringLiteral("rejected=palace-transition-too-large"));

    const QString queued =
        modules().palace_core.submitIntent(actionId);
    if (!queued.contains(QStringLiteral("durable=queued"))) {
        return rememberLezReceipt(
            QStringLiteral("rejected=palace-action-queue;")
            + queued);
    }
    const QString result =
        modules().palace_core.submitPalaceTransition(
            actionId,
            stateAccountIdHex,
            callerAccountIdHex,
            programIdHex,
            transitionJson);
    refreshLezState();
    return rememberLezReceipt(result);
}

QString LogosPalaceUiBackend::observePalaceTransition(QString actionId)
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());
    const QString result =
        modules().palace_core.observePalaceTransition(actionId);
    refreshLezState();
    return rememberLezReceipt(result);
}

QString LogosPalaceUiBackend::reconcilePalaceTransition(QString actionId)
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());
    const QString result =
        modules().palace_core.reconcilePalaceTransition(actionId);
    refreshLezState();
    return rememberLezReceipt(result);
}

QString LogosPalaceUiBackend::actionStatus(QString actionId)
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());
    refreshLezState();
    const QString result =
        modules().palace_core.actionStatus(actionId);
    applyModerationActionStatus(actionId, result);
    return rememberLezReceipt(result);
}

QString LogosPalaceUiBackend::unavailableReceipt() const
{
    return QStringLiteral("rejected=core-not-ready");
}

QString LogosPalaceUiBackend::rememberDeliveryReceipt(const QString& receipt)
{
    setLastActionReceipt(receipt);
    return receipt;
}

QString LogosPalaceUiBackend::rememberStorageReceipt(const QString& receipt)
{
    setLastActionReceipt(receipt);
    return receipt;
}

QString LogosPalaceUiBackend::rememberLezReceipt(const QString& receipt)
{
    setLastActionReceipt(receipt);
    return receipt;
}

QString LogosPalaceUiBackend::rememberModerationReceipt(
    const QString& kind,
    const QString& target,
    const QString& receipt)
{
    const QString action =
        statusValue(receipt, QStringLiteral("action"));
    m_moderationActionId = action;
    m_moderationKind = kind;
    m_moderationTarget = target;
    const QString state =
        receipt.startsWith(QStringLiteral("ok;"))
        ? QStringLiteral("submitted")
        : QStringLiteral("rejected");
    setModerationState(
        QStringLiteral("state=") + state
        + QStringLiteral(";kind=") + kind
        + QStringLiteral(";action=") + action
        + QStringLiteral(";target=") + target);
    return rememberLezReceipt(receipt);
}

void LogosPalaceUiBackend::applyModerationActionStatus(
    const QString& actionId,
    const QString& status)
{
    if (actionId.isEmpty()
        || actionId != m_moderationActionId)
        return;
    const QString durable =
        statusValue(status, QStringLiteral("durable"));
    QString state = QStringLiteral("pending");
    if (durable == QStringLiteral("finalized"))
        state = QStringLiteral("finalized");
    else if (durable == QStringLiteral("rejected")
             || durable == QStringLiteral("expired")
             || durable == QStringLiteral("orphaned")) {
        state = QStringLiteral("rejected");
    }
    setModerationState(
        QStringLiteral("state=") + state
        + QStringLiteral(";kind=") + m_moderationKind
        + QStringLiteral(";action=") + m_moderationActionId
        + QStringLiteral(";target=") + m_moderationTarget);
}

QString LogosPalaceUiBackend::rememberSpotReceipt(const QString& receipt)
{
    setSpotReceipt(receipt);
    setLastActionReceipt(receipt);
    return receipt;
}

void LogosPalaceUiBackend::refreshDeliveryState()
{
    if (!isContextReady())
        return;
    setDeliverySessionStatus(
        modules().palace_core.deliverySessionStatus());
    setParticipantProjection(
        modules().palace_core.participantProjection());
}

void LogosPalaceUiBackend::refreshDeliveryNodeEvidence()
{
    if (!isContextReady())
        return;
    setDeliveryNodeEvidence(
        modules().palace_core.deliveryNodeEvidence());
}

void LogosPalaceUiBackend::refreshStorageState()
{
    if (!isContextReady())
        return;
    setStorageStatus(
        modules().palace_core.storageSessionStatus());
    setAssetAuthoringState(
        modules().palace_core.assetAuthoringCatalog());
    setActivePropAsset(
        modules().palace_core.activePropAsset());
    // Storage verification completes asynchronously. Refresh the visible
    // projection after each storage poll so a restored visitor replaces the
    // placeholder only after the exact room graph resolves.
    refreshRoomProjection();
}

void LogosPalaceUiBackend::refreshLezState()
{
    if (!isContextReady())
        return;
    setLezState(modules().palace_core.lezStatus());
    setIdentityState(modules().palace_core.identityStatus());
}

void LogosPalaceUiBackend::refreshPalaceState()
{
    if (!isContextReady())
        return;
    setPalaceState(modules().palace_core.palaceStatus());
}

void LogosPalaceUiBackend::refreshModerationState()
{
    if (!isContextReady())
        return;
    const QString status =
        modules().palace_core.moderationStatus();
    setModerationState(status);
    m_moderationActionId =
        statusValue(status, QStringLiteral("action"));
    m_moderationKind =
        statusValue(status, QStringLiteral("kind"));
    m_moderationTarget =
        statusValue(status, QStringLiteral("target"));
}

void LogosPalaceUiBackend::refreshRoomProjection()
{
    if (!isContextReady())
        return;
    setRoomTitle(modules().palace_core.roomTitle());
    setRoomBackgroundHandle(
        modules().palace_core.roomBackgroundHandle());
    setSyncHealth(modules().palace_core.syncHealth());
}

void LogosPalaceUiBackend::refreshSpotState()
{
    if (!isContextReady())
        return;

    const QString status = modules().palace_core.spotStatus();
    const QString actionId =
        statusValue(status, QStringLiteral("action"));
    if (isPromotedSpot(status)) {
        applySpotStatus(status);
        m_spotTracking = false;
        m_spotPollBudgetExceeded = false;
        refreshRoomProjection();
        return;
    }
    if (isDegradedSpot(status)) {
        applySpotStatus(status);
        m_spotTracking = false;
        return;
    }
    if (!hasSpotAction(actionId)) {
        applySpotStatus(status);
        m_spotTracking = false;
        m_spotPollBudgetExceeded = false;
        m_spotPollCount = 0;
        return;
    }
    if (m_spotPollBudgetExceeded) {
        setSpotDegraded(
            QStringLiteral("reconcile-budget-exhausted"),
            actionId);
        m_spotTracking = false;
        return;
    }

    applySpotStatus(status);
    m_spotTracking = true;
}

QString LogosPalaceUiBackend::driveSpotAction()
{
    if (!isContextReady())
        return rememberSpotReceipt(unavailableReceipt());
    if (m_spotDriveActive) {
        return rememberSpotReceipt(
            QStringLiteral("rejected=spot-reconcile-reentrant"));
    }

    const QString before = modules().palace_core.spotStatus();
    const QString actionId =
        statusValue(before, QStringLiteral("action"));
    if (!hasSpotAction(actionId)
        || isPromotedSpot(before)
        || isDegradedSpot(before)) {
        applySpotStatus(before);
        m_spotTracking = false;
        if (isPromotedSpot(before))
            refreshRoomProjection();
        return rememberSpotReceipt(before);
    }
    if (m_spotPollCount >= kMaximumSpotReconcilePolls) {
        m_spotTracking = false;
        m_spotPollBudgetExceeded = true;
        setSpotDegraded(
            QStringLiteral("reconcile-budget-exhausted"),
            actionId);
        return rememberSpotReceipt(
            QStringLiteral("rejected=spot-reconcile-budget-exhausted;action=")
            + actionId
            + QStringLiteral(";attempts=")
            + QString::number(m_spotPollCount));
    }

    m_spotDriveActive = true;
    const QString receipt = modules().palace_core.reconcileSpot();
    m_spotDriveActive = false;
    ++m_spotPollCount;
    rememberSpotReceipt(receipt);

    const QString after = modules().palace_core.spotStatus();
    applySpotStatus(after);
    if (isPromotedSpot(after)) {
        m_spotTracking = false;
        refreshRoomProjection();
        return receipt;
    }
    if (isDegradedSpot(after)) {
        m_spotTracking = false;
        return receipt;
    }
    if (receipt.startsWith(QStringLiteral("rejected="))) {
        m_spotTracking = false;
        setSpotDegraded(
            QStringLiteral("core-reconcile-rejected"),
            actionId);
        return receipt;
    }

    const QString afterAction =
        statusValue(after, QStringLiteral("action"));
    if (!hasSpotAction(afterAction)) {
        m_spotTracking = false;
        setSpotDegraded(
            QStringLiteral("action-lost"),
            actionId);
        return rememberSpotReceipt(
            QStringLiteral("rejected=spot-action-lost;action=")
            + actionId);
    }
    m_spotTracking = true;
    return receipt;
}

void LogosPalaceUiBackend::applySpotStatus(const QString& status)
{
    setSpotState(status);
    setSpotActionId(
        statusValue(status, QStringLiteral("action")));
}

void LogosPalaceUiBackend::setSpotDegraded(
    const QString& reason,
    const QString& actionId)
{
    setSpotActionId(actionId);
    setSpotState(
        QStringLiteral("vm=degraded;action=")
        + actionId
        + QStringLiteral(";navigation=0;reason=")
        + reason);
}
