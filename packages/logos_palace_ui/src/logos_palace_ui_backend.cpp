#include "logos_palace_ui_backend.h"

#include <algorithm>
#include <utility>

#include <QByteArray>

#include <cstdint>
#include <limits>

#include "logos_sdk.h"

void PalaceUiController::configure(RefreshCallback refresh,
                                   RefreshCallback refreshNodeStatus)
{
    m_refresh = std::move(refresh);
    m_refreshNodeStatus = std::move(refreshNodeStatus);
    if (m_configured)
        return;

    QObject::connect(
        &m_refreshTimer, &QTimer::timeout, [this]() {
            if (m_refresh)
                m_refresh();
        });
    QObject::connect(
        &m_nodeStatusTimer, &QTimer::timeout, [this]() {
            if (m_refreshNodeStatus)
                m_refreshNodeStatus();
        });
    m_refreshTimer.setInterval(500);
    m_nodeStatusTimer.setInterval(3'000);
    m_configured = true;
}

void PalaceUiController::start()
{
    if (!m_configured)
        return;
    m_refreshTimer.start();
    m_nodeStatusTimer.start();
}

void PalaceUiController::stop()
{
    m_refreshTimer.stop();
    m_nodeStatusTimer.stop();
}

namespace {

constexpr int kMaximumSpotReconcilePolls = 1'200;
constexpr int kMaximumModerationReconcilePolls = 120;
constexpr int kMaximumDurableActionPolls = 1'200;
constexpr int kMaximumStorageBundlePolls = 1'200;
constexpr int kMaximumPalaceRegistrationPolls = 1'200;
constexpr int kMaximumOnboardingWorkflowPolls = 1'200;

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

bool isPalaceAddress(const QString& value)
{
    constexpr int palacePrefixLength = 9;
    return value.startsWith(QStringLiteral("palace://"))
        && isLowerHex64(value.mid(palacePrefixLength));
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

bool isActionId(const QString& value)
{
    return !value.isEmpty()
        && std::all_of(
            value.begin(), value.end(), [](const QChar character) {
                const ushort value = character.unicode();
                return value >= '0' && value <= '9';
            });
}

bool isRetryableDurableActionReceipt(const QString& receipt)
{
    const QString stablePrefix = QStringLiteral(
        "rejected=lez-stable-account-read;reason=");
    const QString observationPrefix = QStringLiteral(
        "rejected=lez-observation;reason=");
    if (receipt.startsWith(stablePrefix)) {
        const QString reason = receipt.mid(stablePrefix.size());
        return reason.startsWith(QStringLiteral("sync-"))
            || reason == QStringLiteral("height-before")
            || reason == QStringLiteral("height-after")
            || reason == QStringLiteral("wallet-height-raced")
            || (reason.startsWith(QStringLiteral("account-"))
                && isActionId(reason.mid(8)));
    }
    if (receipt.startsWith(observationPrefix)) {
        const QString reason = receipt.mid(observationPrefix.size());
        return reason == QStringLiteral("transaction-not-materialized")
            || reason == QStringLiteral("invalid-account-response")
            || reason == QStringLiteral("invalid-account-field")
            || reason == QStringLiteral("invalid-account-data")
            || reason == QStringLiteral("invalid-record")
            || reason == QStringLiteral("unexpected-observation-record")
            || reason == QStringLiteral("observation-mismatch")
            || reason == QStringLiteral("unstable-height");
    }
    return receipt == QStringLiteral(
               "rejected=palace-identity-registration-pending")
        || receipt == QStringLiteral(
               "rejected=lez-root-transaction-pending")
        || receipt == QStringLiteral(
               "rejected=lez-authority-history-rebuild-required");
}

bool isTerminalModerationState(const QString& state)
{
    return state == QStringLiteral("finalized")
        || state == QStringLiteral("local-committed")
        || state == QStringLiteral("rejected")
        || state == QStringLiteral("degraded");
}

bool isRetryableLocalModerationReceipt(const QString& receipt)
{
    return receipt.startsWith(
               QStringLiteral("rejected=lez-stable-account-read;reason=sync-"))
        || receipt.startsWith(
               QStringLiteral("rejected=lez-stable-account-read;reason=height-"))
        || receipt.startsWith(
               QStringLiteral("rejected=lez-stable-account-read;reason=wallet-height-raced"))
        || receipt.startsWith(
               QStringLiteral("rejected=lez-observation;reason=unstable-height"));
}

} // namespace

LogosPalaceUiBackend::~LogosPalaceUiBackend()
{
    stopPollingTimers();
}

void LogosPalaceUiBackend::stopPollingTimers()
{
    m_uiController.stop();
    m_spotTracking = false;
    m_spotDriveActive = false;
    m_moderationTracking = false;
    m_moderationDriveActive = false;
    m_durableActionTracking = false;
    m_durableActionObserved = false;
    m_storageBundleTracking = false;
    m_palaceRegistrationTracking = false;
    m_palaceRegistrationObserved = false;
    m_onboardingWorkflowTracking = false;
    m_onboardingWorkflowObserved = false;
    m_onboardingWorkflowInitialRoomReady = false;
    m_onboardingWorkflowAwaitingInitialRoom = false;
    resetAssetImportTracking();
}

void LogosPalaceUiBackend::driveAutomaticDeliveryStart()
{
    if (!isContextReady() || m_autoStartDeliveryConfig.isEmpty())
        return;

    const QString delivery = modules().palace_core.deliverySessionStatus();
    const bool nodeRunning =
        statusValue(delivery, QStringLiteral("node_running"))
            == QStringLiteral("1");
    if (!m_autoStartDeliveryPending && !nodeRunning)
        return;

    const QString identity = modules().palace_core.identityStatus();
    const QString identityId =
        statusValue(identity, QStringLiteral("identity"));
    if (identityId.isEmpty() || identityId == QStringLiteral("none"))
        return;

    const QString palace = modules().palace_core.palaceStatus();
    if (statusValue(palace, QStringLiteral("palace"))
            != QStringLiteral("open")
        || statusValue(palace, QStringLiteral("authority"))
            != QStringLiteral("local-committed")
        || statusValue(palace, QStringLiteral("entry_state"))
            != QStringLiteral("ready")) {
        return;
    }

    const QString result = modules().palace_core.startDelivery(
        m_autoStartDeliveryConfig);
    refreshDeliveryState();
    refreshDeliveryNodeStatus();
    if (!result.startsWith(QStringLiteral("rejected=")))
        m_autoStartDeliveryPending = false;
}

void LogosPalaceUiBackend::onContextReady()
{
    refreshRoomProjection();
    refreshDeliveryState();
    refreshDeliveryNodeStatus();
    refreshStorageState();
    refreshAssetAuthoringCapabilityState();
    refreshLezState();
    refreshPalaceState();
    refreshModerationState();
    refreshRoomLockState();
    refreshSpotState();

    m_uiController.configure(
        [this]() {
            // Presence is live projection maintenance, not a QML command.
            // Keep it in the controller-owned refresh loop so recovery and
            // normal clients converge without privileged UI calls.
            if (isContextReady())
                modules().palace_core.refreshPresence();
            refreshDeliveryState();
            refreshStorageState();
            refreshAssetAuthoringCapabilityState();
            refreshLezState();
            refreshPalaceState();
            driveAutomaticDeliveryStart();
            refreshModerationState();
            refreshRoomLockState();
            refreshSpotState();
            if (m_moderationTracking)
                driveLocalModerationAction();
            if (m_durableActionTracking)
                driveDurableAction();
            if (m_storageBundleTracking)
                driveStorageBundle();
            if (m_palaceRegistrationTracking)
                drivePalaceRegistration();
            if (m_onboardingWorkflowTracking)
                driveOnboardingWorkflow();
            if (m_spotTracking)
                driveSpotAction();
        },
        [this]() {
            refreshDeliveryNodeStatus();
        });
    m_uiController.start();

    if (!m_autoStartAttempted) {
        m_autoStartAttempted = true;
        const QByteArray config = qgetenv("PALACE_DELIVERY_CONFIG");
        if (!config.isEmpty()) {
            if (config.size() > 64 * 1024) {
                setLastActionReceipt(
                    QStringLiteral("rejected=delivery-config-too-large"));
            } else {
                m_autoStartDeliveryConfig = QString::fromUtf8(config);
                m_autoStartDeliveryPending = true;
                driveAutomaticDeliveryStart();
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

QString LogosPalaceUiBackend::startDelivery(QString nodeConfig)
{
    if (!isContextReady())
        return rememberDeliveryReceipt(unavailableReceipt());
    const QString result = modules().palace_core.startDelivery(nodeConfig);
    refreshDeliveryState();
    refreshDeliveryNodeStatus();
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

QString LogosPalaceUiBackend::connectStorage()
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    const QString result = modules().palace_core.connectStorage();
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
        static_cast<qulonglong>(byteLength),
        contentSha256,
        static_cast<qulonglong>(width),
        static_cast<qulonglong>(height));
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
            static_cast<qulonglong>(sequence),
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

QString LogosPalaceUiBackend::beginAssetImport(
    QString label,
    qint64 byteLength)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    if (m_assetImportTracking)
        return rememberStorageReceipt(
            QStringLiteral("rejected=asset-import-in-flight"));
    if (byteLength < 0 || byteLength > 10 * 1024 * 1024)
        return rememberStorageReceipt(
            QStringLiteral("rejected=asset-import-size"));

    const QString result = modules().palace_core.beginAssetStage(label);
    if (result.startsWith(QStringLiteral("rejected="))) {
        setAssetImportStatus(
            QStringLiteral("state=failed;") + result);
        return rememberStorageReceipt(result);
    }

    const QString session = statusValue(
        result, QStringLiteral("session"));
    if (session.isEmpty()
        || statusValue(result, QStringLiteral("next"))
            != QStringLiteral("0")) {
        const QString failure = QStringLiteral(
            "rejected=asset-import-invalid-begin");
        setAssetImportStatus(
            QStringLiteral("state=failed;") + failure);
        return rememberStorageReceipt(failure);
    }

    m_assetImportTracking = true;
    m_assetImportSession = session;
    m_assetImportExpectedSequence = 0;
    m_assetImportByteLength = byteLength;
    m_assetImportBytes = 0;
    setAssetImportStatus(
        QStringLiteral("state=reading;session=") + session
        + QStringLiteral(";next=0;bytes=0;total=")
        + QString::number(byteLength));
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::appendAssetImportChunk(
    QString base64Chunk)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    if (!m_assetImportTracking)
        return rememberStorageReceipt(
            QStringLiteral("rejected=asset-import-not-active"));
    if (m_assetImportExpectedSequence < 0) {
        const QString failure = QStringLiteral(
            "rejected=asset-import-sequence");
        setAssetImportFailure(failure);
        return rememberStorageReceipt(failure);
    }

    const QString result = modules().palace_core.appendAssetStageChunk(
        m_assetImportSession,
        static_cast<qulonglong>(m_assetImportExpectedSequence),
        base64Chunk);
    if (result.startsWith(QStringLiteral("rejected="))) {
        setAssetImportFailure(result);
        return rememberStorageReceipt(result);
    }

    const QString next = statusValue(result, QStringLiteral("next"));
    const QString bytes = statusValue(result, QStringLiteral("bytes"));
    bool nextOk = false;
    bool bytesOk = false;
    const qint64 nextSequence = next.toLongLong(&nextOk);
    const qint64 totalBytes = bytes.toLongLong(&bytesOk);
    if (!nextOk || !bytesOk
        || nextSequence != m_assetImportExpectedSequence + 1
        || totalBytes < m_assetImportBytes
        || totalBytes > m_assetImportByteLength) {
        const QString failure = QStringLiteral(
            "rejected=asset-import-invalid-append");
        setAssetImportFailure(failure);
        return rememberStorageReceipt(failure);
    }

    m_assetImportExpectedSequence = nextSequence;
    m_assetImportBytes = totalBytes;
    setAssetImportStatus(
        QStringLiteral("state=reading;session=")
        + m_assetImportSession + QStringLiteral(";next=")
        + QString::number(nextSequence) + QStringLiteral(";bytes=")
        + QString::number(totalBytes) + QStringLiteral(";total=")
        + QString::number(m_assetImportByteLength));
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::finishAssetImport()
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    if (!m_assetImportTracking)
        return rememberStorageReceipt(
            QStringLiteral("rejected=asset-import-not-active"));
    if (m_assetImportBytes != m_assetImportByteLength) {
        const QString failure = QStringLiteral(
            "rejected=asset-import-byte-length");
        setAssetImportFailure(failure);
        return rememberStorageReceipt(failure);
    }

    const QString result = modules().palace_core.commitAssetStage(
        m_assetImportSession);
    if (result.startsWith(QStringLiteral("rejected="))) {
        setAssetImportFailure(result);
        return rememberStorageReceipt(result);
    }

    setAssetImportStatus(
        QStringLiteral("state=ready;session=")
        + m_assetImportSession + QLatin1Char(';') + result);
    resetAssetImportTracking();
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::cancelAssetImport()
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    if (!m_assetImportTracking || m_assetImportSession.isEmpty()) {
        setAssetImportStatus(QStringLiteral("state=idle"));
        return rememberStorageReceipt(
            QStringLiteral("ok;asset-import=idle"));
    }

    const QString result = modules().palace_core.cancelAssetStage(
        m_assetImportSession);
    if (result.startsWith(QStringLiteral("rejected="))) {
        setAssetImportFailure(result);
        return rememberStorageReceipt(result);
    }
    resetAssetImportTracking();
    setAssetImportStatus(QStringLiteral("state=idle;") + result);
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

QString LogosPalaceUiBackend::approveAndPublishAsset(
    QString handle)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    if (!isAssetIdentifier(handle))
        return rememberStorageReceipt(
            QStringLiteral("rejected=asset-handle-invalid"));

    const QString review = modules().palace_core.reviewAsset(
        handle, QStringLiteral("approve"));
    if (review.startsWith(QStringLiteral("rejected=")))
        return rememberStorageReceipt(review);

    const QString publication = modules().palace_core.publishAsset(handle);
    refreshStorageState();
    return rememberStorageReceipt(publication);
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
            static_cast<qulonglong>(anchorX),
            static_cast<qulonglong>(anchorY),
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

QString LogosPalaceUiBackend::trackStorageBundle()
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());

    m_storageBundlePollCount = 0;
    m_storageBundleTracking = true;
    setStorageBundleWorkflowStatus(QStringLiteral("state=tracking"));
    driveStorageBundle();
    return rememberStorageReceipt(
        QStringLiteral("ok;storage-bundle-tracking=1"));
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

QString LogosPalaceUiBackend::storagePeerEndpoint()
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    const QString result = modules().palace_core.storagePeerEndpoint();
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::connectStoragePeer(
    QString peerId,
    QString addressesJson)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    const QString result = modules().palace_core.connectStoragePeer(
        peerId, addressesJson);
    refreshStorageState();
    return rememberStorageReceipt(result);
}

QString LogosPalaceUiBackend::prepareExistingPalaceStorage(
    QString catalogBase64,
    QString peerId,
    QString addressesJson,
    bool attachPeer)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    if (catalogBase64.isEmpty() || catalogBase64.toUtf8().size() > 16 * 1024)
        return rememberStorageReceipt(
            QStringLiteral("rejected=storage-catalog-invalid"));
    if (attachPeer
        && (peerId.isEmpty() || addressesJson.isEmpty()
            || addressesJson.toUtf8().size() > 64 * 1024)) {
        return rememberStorageReceipt(
            QStringLiteral("rejected=storage-peer-endpoint-required"));
    }

    const QString storage = modules().palace_core.connectStorage();
    if (storage.startsWith(QStringLiteral("rejected=")))
        return rememberStorageReceipt(storage);

    if (attachPeer) {
        const QString peer = modules().palace_core.connectStoragePeer(
            peerId, addressesJson);
        if (peer.startsWith(QStringLiteral("rejected=")))
            return rememberStorageReceipt(peer);
    }

    const QString catalog = modules().palace_core.fetchMvpStorageBundle(
        catalogBase64);
    refreshStorageState();
    return rememberStorageReceipt(catalog);
}

QString LogosPalaceUiBackend::preparePalaceOnboarding(
    QString mode,
    QString password,
    QString displayName,
    QString palaceAddress,
    QString catalogBase64,
    QString peerId,
    QString addressesJson)
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());

    const bool create = mode == QStringLiteral("create");
    const bool join = mode == QStringLiteral("join");
    const bool recover = mode == QStringLiteral("recover");
    if (!create && !join && !recover)
        return rememberLezReceipt(
            QStringLiteral("rejected=onboarding-mode-invalid"));
    if ((join || recover) && !isPalaceAddress(palaceAddress))
        return rememberLezReceipt(
            QStringLiteral("rejected=invalid-palace-uri"));

    const QString lez = modules().palace_core.startLez(password);
    refreshLezState();
    if (lez.startsWith(QStringLiteral("rejected=")))
        return rememberLezReceipt(lez);

    const QString identity = modules().palace_core.createIdentity(
        displayName);
    refreshLezState();
    if (identity.startsWith(QStringLiteral("rejected=")))
        return rememberLezReceipt(identity);

    if (create)
    {
        setOnboardingWorkflowStatus(
            QStringLiteral("state=authoring-rooms"));
        return rememberLezReceipt(
            QStringLiteral("ok;phase=authoring-rooms"));
    }

    return resumeExistingPalace(
        palaceAddress, catalogBase64, peerId, addressesJson, join);
}

QString LogosPalaceUiBackend::beginPalaceCreation(QString title)
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());
    if (title.trimmed().isEmpty() || title.toUtf8().size() > 128)
        return rememberLezReceipt(
            QStringLiteral("rejected=palace-title-invalid"));

    m_onboardingWorkflowTracking = true;
    m_onboardingWorkflowExistingPalace = false;
    m_onboardingWorkflowObserved = false;
    m_onboardingWorkflowPollCount = 0;
    m_onboardingWorkflowPhase = QStringLiteral(
        "checking-room-setup");
    m_onboardingWorkflowTitle = title.trimmed();
    m_onboardingWorkflowPalaceUri.clear();
    m_onboardingWorkflowActionId.clear();
    m_onboardingWorkflowInitialRoomReady = false;
    m_onboardingWorkflowAwaitingInitialRoom = false;
    setOnboardingWorkflowStatus(
        QStringLiteral("state=checking-room-setup"));
    driveOnboardingWorkflow();
    return rememberLezReceipt(
        QStringLiteral("ok;workflow=palace-creation"));
}

QString LogosPalaceUiBackend::resumePalaceOnboarding()
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());
    if (m_onboardingWorkflowPhase.isEmpty())
        return rememberLezReceipt(
            QStringLiteral("rejected=onboarding-workflow-not-started"));

    m_onboardingWorkflowTracking = true;
    m_onboardingWorkflowPollCount = 0;
    setOnboardingWorkflowStatus(
        QStringLiteral("state=") + m_onboardingWorkflowPhase);
    driveOnboardingWorkflow();
    return rememberLezReceipt(
        QStringLiteral("ok;workflow=palace-onboarding-resumed"));
}

QString LogosPalaceUiBackend::resumeExistingPalace(
    QString palaceAddress,
    QString catalogBase64,
    QString peerId,
    QString addressesJson,
    bool attachPeer)
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    if (!isPalaceAddress(palaceAddress))
        return rememberStorageReceipt(
            QStringLiteral("rejected=invalid-palace-uri"));

    const QString storage = prepareExistingPalaceStorage(
        catalogBase64, peerId, addressesJson, attachPeer);
    if (storage.startsWith(QStringLiteral("rejected=")))
        return rememberStorageReceipt(storage);

    m_onboardingWorkflowTracking = true;
    m_onboardingWorkflowExistingPalace = true;
    m_onboardingWorkflowObserved = false;
    m_onboardingWorkflowPollCount = 0;
    m_onboardingWorkflowPhase = QStringLiteral(
        "fetching-storage-catalog");
    m_onboardingWorkflowPalaceUri = palaceAddress;
    m_onboardingWorkflowCatalog = catalogBase64;
    m_onboardingWorkflowPeerId = peerId;
    m_onboardingWorkflowAddressesJson = addressesJson;
    m_onboardingWorkflowAttachPeer = attachPeer;
    m_onboardingWorkflowInitialRoomReady = false;
    m_onboardingWorkflowAwaitingInitialRoom = false;
    setOnboardingWorkflowStatus(
        QStringLiteral("state=fetching-storage-catalog;") + storage);
    driveOnboardingWorkflow();
    return rememberStorageReceipt(storage);
}

QString LogosPalaceUiBackend::markStorageMaterialized()
{
    if (!isContextReady())
        return rememberStorageReceipt(unavailableReceipt());
    const QString result = modules().palace_core.markStorageMaterialized();
    refreshStorageState();
    return rememberStorageReceipt(result);
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

QString LogosPalaceUiBackend::createPalace(QString title)
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());
    const QString result = modules().palace_core.createPalace(title);
    refreshLezState();
    refreshPalaceState();
    return rememberLezReceipt(result);
}

QString LogosPalaceUiBackend::createInitialRoomState()
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());
    const QString result = modules().palace_core.createInitialRoomState();
    refreshLezState();
    refreshPalaceState();
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

QString LogosPalaceUiBackend::registerPalaceUser()
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());
    const QString result = modules().palace_core.registerPalaceUser();
    refreshLezState();
    refreshPalaceState();
    return rememberLezReceipt(result);
}

QString LogosPalaceUiBackend::trackPalaceRegistration()
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());

    m_palaceRegistrationActionId.clear();
    m_palaceRegistrationObserved = false;
    m_palaceRegistrationPollCount = 0;
    m_palaceRegistrationTracking = true;
    setPalaceRegistrationWorkflowStatus(
        QStringLiteral("state=tracking;action=none;registration=pending"));
    drivePalaceRegistration();
    return rememberLezReceipt(
        QStringLiteral("ok;palace-registration-tracking=1"));
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
    setModerationCapabilityState(
        modules().palace_core.moderationCapabilityStatus());
    const QString receipt = rememberModerationReceipt(
        QStringLiteral("user"),
        subjectUserIdHex,
        result);
    startLocalModerationTracking(receipt);
    return receipt;
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
    setModerationCapabilityState(
        modules().palace_core.moderationCapabilityStatus());
    const QString receipt = rememberModerationReceipt(
        QStringLiteral("prop"), propId, result);
    startLocalModerationTracking(receipt);
    return receipt;
}

QString LogosPalaceUiBackend::delegateModerator(QString subjectUserIdHex)
{
    if (!isContextReady())
        return rememberModerationReceipt(
            QStringLiteral("moderator"), subjectUserIdHex,
            unavailableReceipt());
    if (!isLowerHex64(subjectUserIdHex))
        return rememberModerationReceipt(
            QStringLiteral("moderator"), subjectUserIdHex,
            QStringLiteral("rejected=moderator-user-invalid"));
    const QString result = modules().palace_core.delegateModerator(
        subjectUserIdHex);
    refreshLezState();
    refreshModerationState();
    const QString receipt = rememberModerationReceipt(
        QStringLiteral("moderator"), subjectUserIdHex, result);
    startLocalModerationTracking(receipt);
    return receipt;
}

QString LogosPalaceUiBackend::setRoomLocked(QString roomId, bool locked)
{
    if (!isContextReady())
        return rememberModerationReceipt(
            QStringLiteral("room-lock"), roomId,
            unavailableReceipt());
    if (roomId.isEmpty() || roomId.size() > 128)
        return rememberModerationReceipt(
            QStringLiteral("room-lock"), roomId,
            QStringLiteral("rejected=room-lock-room-invalid"));
    const QString result = modules().palace_core.setRoomLocked(
        roomId, locked);
    refreshLezState();
    refreshPalaceState();
    refreshModerationState();
    refreshRoomLockState();
    const QString receipt = rememberModerationReceipt(
        QStringLiteral("room-lock"), roomId, result);
    startLocalModerationTracking(receipt);
    return receipt;
}

QString LogosPalaceUiBackend::refreshModeration()
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());
    const QString result =
        modules().palace_core.moderationStatus();
    setModerationState(result);
    setModerationCapabilityState(
        modules().palace_core.moderationCapabilityStatus());
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

QString LogosPalaceUiBackend::trackDurableAction(QString actionId)
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());
    if (!isActionId(actionId) || actionId.size() > 20)
        return rememberLezReceipt(
            QStringLiteral("rejected=palace-action-invalid"));

    m_durableActionId = actionId;
    m_durableActionObserved = false;
    m_durableActionPollCount = 0;
    m_durableActionTracking = true;
    setDurableActionStatus(
        QStringLiteral("state=tracking;action=") + actionId
        + QStringLiteral(";durable=submitted_to_lez"));
    driveDurableAction();
    return rememberLezReceipt(
        QStringLiteral("ok;action=") + actionId
        + QStringLiteral(";tracking=1"));
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

void LogosPalaceUiBackend::driveStorageBundle()
{
    if (!m_storageBundleTracking || !isContextReady())
        return;
    if (m_storageBundlePollCount >= kMaximumStorageBundlePolls) {
        m_storageBundleTracking = false;
        setStorageBundleWorkflowStatus(
            QStringLiteral("rejected=storage-bundle-poll-budget"));
        return;
    }

    ++m_storageBundlePollCount;
    const QString result = modules().palace_core.mvpStorageBundleStatus();
    refreshStorageState();
    setStorageBundleWorkflowStatus(result);

    const QString state = statusValue(result, QStringLiteral("state"));
    if (result.startsWith(QStringLiteral("rejected="))
        || state == QStringLiteral("degraded")
        || state == QStringLiteral("verified")
        || state == QStringLiteral("retained")) {
        m_storageBundleTracking = false;
    }
}

void LogosPalaceUiBackend::drivePalaceRegistration()
{
    if (!m_palaceRegistrationTracking || !isContextReady())
        return;
    if (m_palaceRegistrationPollCount >= kMaximumPalaceRegistrationPolls) {
        m_palaceRegistrationTracking = false;
        setPalaceRegistrationWorkflowStatus(
            QStringLiteral("rejected=palace-identity-poll-budget"));
        return;
    }

    ++m_palaceRegistrationPollCount;
    if (m_palaceRegistrationActionId.isEmpty()) {
        const QString result = modules().palace_core.registerPalaceUser();
        refreshLezState();
        refreshPalaceState();
        setPalaceRegistrationWorkflowStatus(result);

        if (result.startsWith(QStringLiteral("rejected="))) {
            if (!isRetryableDurableActionReceipt(result))
                m_palaceRegistrationTracking = false;
            return;
        }
        if (statusValue(result, QStringLiteral("registration"))
                == QStringLiteral("already")) {
            m_palaceRegistrationTracking = false;
            return;
        }

        const QString actionId = statusValue(
            result, QStringLiteral("action"));
        if (!isActionId(actionId)) {
            m_palaceRegistrationTracking = false;
            setPalaceRegistrationWorkflowStatus(
                QStringLiteral("rejected=palace-identity-action-missing"));
            return;
        }
        m_palaceRegistrationActionId = actionId;
    }

    const QString result = m_palaceRegistrationObserved
        ? modules().palace_core.reconcilePalaceTransition(
              m_palaceRegistrationActionId)
        : modules().palace_core.observePalaceTransition(
              m_palaceRegistrationActionId);
    refreshLezState();
    refreshPalaceState();
    setPalaceRegistrationWorkflowStatus(result);

    if (result.startsWith(QStringLiteral("rejected="))) {
        if (!isRetryableDurableActionReceipt(result))
            m_palaceRegistrationTracking = false;
        return;
    }

    if (statusValue(result, QStringLiteral("durable"))
            == QStringLiteral("observed")) {
        m_palaceRegistrationObserved = true;
    }
    const QString durable = statusValue(
        result, QStringLiteral("durable"));
    const QString completion = statusValue(
        result, QStringLiteral("completion"));
    if (durable == QStringLiteral("finalized")
        || completion == QStringLiteral("local-committed")
        || durable == QStringLiteral("rejected")
        || durable == QStringLiteral("expired")
        || durable == QStringLiteral("orphaned")) {
        m_palaceRegistrationTracking = false;
    }
}

void LogosPalaceUiBackend::setOnboardingWorkflowFailure(
    const QString& receipt)
{
    m_onboardingWorkflowTracking = false;
    setOnboardingWorkflowStatus(receipt);
}

void LogosPalaceUiBackend::setAssetImportFailure(
    const QString& receipt)
{
    setAssetImportStatus(
        QStringLiteral("state=failed;session=")
        + m_assetImportSession + QLatin1Char(';') + receipt);
}

void LogosPalaceUiBackend::resetAssetImportTracking()
{
    m_assetImportTracking = false;
    m_assetImportSession.clear();
    m_assetImportExpectedSequence = 0;
    m_assetImportByteLength = 0;
    m_assetImportBytes = 0;
}

bool LogosPalaceUiBackend::isRetryableOnboardingOpenReceipt(
    const QString& receipt) const
{
    return isRetryableDurableActionReceipt(receipt)
        || (statusValue(receipt, QStringLiteral("palace"))
                == QStringLiteral("rejected")
            && statusValue(receipt, QStringLiteral("reason"))
                == QStringLiteral("local-committed-initialize-not-found"));
}

void LogosPalaceUiBackend::resetOnboardingWorkflowTracking()
{
    m_onboardingWorkflowTracking = false;
    m_onboardingWorkflowObserved = false;
    m_onboardingWorkflowPollCount = 0;
    m_onboardingWorkflowActionId.clear();
}

void LogosPalaceUiBackend::driveOnboardingWorkflow()
{
    if (!m_onboardingWorkflowTracking || !isContextReady())
        return;
    if (m_onboardingWorkflowPollCount >=
        kMaximumOnboardingWorkflowPolls) {
        setOnboardingWorkflowFailure(
            QStringLiteral("rejected=onboarding-workflow-poll-budget"));
        return;
    }
    ++m_onboardingWorkflowPollCount;

    const auto publish = [this](const QString& phase,
                                const QString& receipt) {
        QString status = QStringLiteral("state=") + phase;
        if (!receipt.isEmpty())
            status += QLatin1Char(';') + receipt;
        setOnboardingWorkflowStatus(status);
    };
    const auto retryOrFail = [this](const QString& receipt,
                                    bool retryable) {
        if (retryable)
            setOnboardingWorkflowStatus(
                QStringLiteral("state=") + m_onboardingWorkflowPhase
                + QLatin1Char(';') + receipt);
        else
            setOnboardingWorkflowFailure(receipt);
    };
    const auto actionFinished = [](const QString& receipt) {
        const QString durable = statusValue(
            receipt, QStringLiteral("durable"));
        const QString completion = statusValue(
            receipt, QStringLiteral("completion"));
        return durable == QStringLiteral("finalized")
            || completion == QStringLiteral("local-committed");
    };

    if (m_onboardingWorkflowExistingPalace) {
        if (m_onboardingWorkflowPhase
                == QStringLiteral("fetching-storage-catalog")) {
            const QString result = modules().palace_core
                .mvpStorageBundleStatus();
            refreshStorageState();
            setStorageBundleWorkflowStatus(result);
            publish(m_onboardingWorkflowPhase, result);
            const QString state = statusValue(
                result, QStringLiteral("state"));
            if (result.startsWith(QStringLiteral("rejected="))
                || state == QStringLiteral("degraded")) {
                setOnboardingWorkflowFailure(result);
                return;
            }
            if (state == QStringLiteral("verified")
                || state == QStringLiteral("retained")) {
                m_onboardingWorkflowPhase =
                    QStringLiteral("opening-palace");
            } else {
                return;
            }
        }

        if (m_onboardingWorkflowPhase
                == QStringLiteral("opening-palace")) {
            const QString result = modules().palace_core.openPalace(
                m_onboardingWorkflowPalaceUri);
            refreshPalaceState();
            refreshLezState();
            publish(m_onboardingWorkflowPhase, result);
            const QString palace = statusValue(
                result, QStringLiteral("palace"));
            if (result.startsWith(QStringLiteral("rejected="))
                || palace == QStringLiteral("rejected")
                || palace == QStringLiteral("degraded")) {
                retryOrFail(result, isRetryableOnboardingOpenReceipt(result));
                return;
            }
            if (palace != QStringLiteral("open"))
                return;
            m_onboardingWorkflowPhase =
                QStringLiteral("registering-palace-identity");
        }

        if (m_onboardingWorkflowPhase
                == QStringLiteral("registering-palace-identity")) {
            const QString result = modules().palace_core
                .registerPalaceUser();
            refreshLezState();
            refreshPalaceState();
            publish(m_onboardingWorkflowPhase, result);
            if (result.startsWith(QStringLiteral("rejected="))) {
                retryOrFail(result,
                            isRetryableDurableActionReceipt(result));
                return;
            }
            if (statusValue(result, QStringLiteral("registration"))
                    == QStringLiteral("already")) {
                m_onboardingWorkflowTracking = false;
                m_onboardingWorkflowPhase = QStringLiteral("complete");
                publish(m_onboardingWorkflowPhase, result);
                return;
            }
            const QString actionId = statusValue(
                result, QStringLiteral("action"));
            if (!isActionId(actionId)) {
                setOnboardingWorkflowFailure(
                    QStringLiteral("rejected=palace-identity-action-missing"));
                return;
            }
            m_onboardingWorkflowActionId = actionId;
            m_onboardingWorkflowObserved = false;
            m_onboardingWorkflowPhase =
                QStringLiteral("confirming-palace-identity");
            return;
        }

        if (m_onboardingWorkflowPhase
                == QStringLiteral("confirming-palace-identity")) {
            const QString result = m_onboardingWorkflowObserved
                ? modules().palace_core.reconcilePalaceTransition(
                      m_onboardingWorkflowActionId)
                : modules().palace_core.observePalaceTransition(
                      m_onboardingWorkflowActionId);
            refreshLezState();
            refreshPalaceState();
            publish(m_onboardingWorkflowPhase, result);
            if (result.startsWith(QStringLiteral("rejected="))) {
                retryOrFail(result,
                            isRetryableDurableActionReceipt(result));
                return;
            }
            if (statusValue(result, QStringLiteral("durable"))
                    == QStringLiteral("observed"))
                m_onboardingWorkflowObserved = true;
            if (actionFinished(result)) {
                m_onboardingWorkflowTracking = false;
                m_onboardingWorkflowPhase = QStringLiteral("complete");
                publish(m_onboardingWorkflowPhase, result);
            }
            return;
        }
        return;
    }

    if (m_onboardingWorkflowPhase
            == QStringLiteral("checking-room-setup")) {
        const QString result = modules().palace_core
            .mvpStorageBundleStatus();
        refreshStorageState();
        setStorageBundleWorkflowStatus(result);
        publish(m_onboardingWorkflowPhase, result);
        const QString state = statusValue(result, QStringLiteral("state"));
        if (result.startsWith(QStringLiteral("rejected="))
            || state == QStringLiteral("degraded")) {
            setOnboardingWorkflowFailure(result);
            return;
        }
        if (state != QStringLiteral("verified")
            && state != QStringLiteral("retained"))
            return;
        m_onboardingWorkflowPhase = QStringLiteral("creating-palace");
    }

    if (m_onboardingWorkflowPhase
            == QStringLiteral("creating-palace")) {
        const QString result = modules().palace_core.createPalace(
            m_onboardingWorkflowTitle);
        refreshLezState();
        refreshPalaceState();
        publish(m_onboardingWorkflowPhase, result);
        if (result.startsWith(QStringLiteral("rejected="))) {
            retryOrFail(result, isRetryableDurableActionReceipt(result));
            return;
        }
        const QString palaceUri = statusValue(
            result, QStringLiteral("palace_uri"));
        if (!isPalaceAddress(palaceUri)) {
            setOnboardingWorkflowFailure(
                QStringLiteral("rejected=palace-uri-missing"));
            return;
        }
        m_onboardingWorkflowPalaceUri = palaceUri;
        m_onboardingWorkflowActionId = QStringLiteral("0");
        m_onboardingWorkflowObserved = false;
        m_onboardingWorkflowAwaitingInitialRoom = false;
        m_onboardingWorkflowPhase = QStringLiteral("confirming-creation");
        return;
    }

    const bool confirmingCreation = m_onboardingWorkflowPhase
        == QStringLiteral("confirming-creation");
    const bool confirmingInitialRoom = m_onboardingWorkflowPhase
        == QStringLiteral("confirming-initial-room-state");
    if (confirmingCreation || confirmingInitialRoom) {
        const QString result = m_onboardingWorkflowObserved
            ? modules().palace_core.reconcilePalaceTransition(
                  m_onboardingWorkflowActionId)
            : modules().palace_core.observePalaceTransition(
                  m_onboardingWorkflowActionId);
        refreshLezState();
        refreshPalaceState();
        publish(m_onboardingWorkflowPhase, result);
        if (result.startsWith(QStringLiteral("rejected="))) {
            retryOrFail(result, isRetryableDurableActionReceipt(result));
            return;
        }
        if (statusValue(result, QStringLiteral("durable"))
                == QStringLiteral("observed"))
            m_onboardingWorkflowObserved = true;
        if (!actionFinished(result))
            return;
        if (confirmingCreation) {
            m_onboardingWorkflowPhase =
                QStringLiteral("preparing-initial-room-state");
            m_onboardingWorkflowObserved = false;
            return;
        }
        m_onboardingWorkflowInitialRoomReady = true;
        m_onboardingWorkflowAwaitingInitialRoom = false;
        m_onboardingWorkflowPhase = QStringLiteral("opening-created-palace");
        m_onboardingWorkflowObserved = false;
        return;
    }

    if (m_onboardingWorkflowPhase
            == QStringLiteral("preparing-initial-room-state")) {
        const QString result = modules().palace_core.openPalace(
            m_onboardingWorkflowPalaceUri);
        refreshPalaceState();
        refreshLezState();
        publish(m_onboardingWorkflowPhase, result);
        const QString palace = statusValue(
            result, QStringLiteral("palace"));
        if (result.startsWith(QStringLiteral("rejected="))
            || palace == QStringLiteral("rejected")
            || palace == QStringLiteral("degraded")) {
            retryOrFail(result, isRetryableOnboardingOpenReceipt(result));
            return;
        }
        if (palace == QStringLiteral("open"))
            m_onboardingWorkflowPhase =
                QStringLiteral("creating-initial-room-state");
        return;
    }

    if (m_onboardingWorkflowPhase
            == QStringLiteral("creating-initial-room-state")) {
        const QString result = modules().palace_core
            .createInitialRoomState();
        refreshLezState();
        refreshPalaceState();
        publish(m_onboardingWorkflowPhase, result);
        if (result.startsWith(QStringLiteral("rejected="))) {
            setOnboardingWorkflowFailure(result);
            return;
        }
        if (statusValue(result, QStringLiteral("initial_room_state"))
                == QStringLiteral("ready")) {
            m_onboardingWorkflowInitialRoomReady = true;
            m_onboardingWorkflowPhase =
                QStringLiteral("opening-created-palace");
            return;
        }
        const QString actionId = statusValue(
            result, QStringLiteral("action"));
        if (!isActionId(actionId)) {
            setOnboardingWorkflowFailure(
                QStringLiteral("rejected=initial-room-state-action"));
            return;
        }
        m_onboardingWorkflowActionId = actionId;
        m_onboardingWorkflowObserved = false;
        m_onboardingWorkflowAwaitingInitialRoom = true;
        m_onboardingWorkflowPhase =
            QStringLiteral("confirming-initial-room-state");
        return;
    }

    if (m_onboardingWorkflowPhase
            == QStringLiteral("opening-created-palace")) {
        const QString result = modules().palace_core.openPalace(
            m_onboardingWorkflowPalaceUri);
        refreshPalaceState();
        refreshLezState();
        publish(m_onboardingWorkflowPhase, result);
        const QString palace = statusValue(
            result, QStringLiteral("palace"));
        if (result.startsWith(QStringLiteral("rejected="))
            || palace == QStringLiteral("rejected")
            || palace == QStringLiteral("degraded")) {
            retryOrFail(result, isRetryableOnboardingOpenReceipt(result));
            return;
        }
        if (palace != QStringLiteral("open"))
            return;
        if (m_onboardingWorkflowInitialRoomReady) {
            resetOnboardingWorkflowTracking();
            m_onboardingWorkflowPhase = QStringLiteral("complete");
            publish(m_onboardingWorkflowPhase, result);
        } else {
            m_onboardingWorkflowPhase =
                QStringLiteral("preparing-initial-room-state");
        }
    }
}

void LogosPalaceUiBackend::driveDurableAction()
{
    if (!m_durableActionTracking || !isContextReady())
        return;
    if (m_durableActionPollCount >= kMaximumDurableActionPolls) {
        m_durableActionTracking = false;
        setDurableActionStatus(
            QStringLiteral("rejected=palace-action-poll-budget"));
        return;
    }

    ++m_durableActionPollCount;
    const QString result = m_durableActionObserved
        ? modules().palace_core.reconcilePalaceTransition(
              m_durableActionId)
        : modules().palace_core.observePalaceTransition(
              m_durableActionId);
    refreshLezState();

    if (result.startsWith(QStringLiteral("rejected="))) {
        setDurableActionStatus(result);
        if (!isRetryableDurableActionReceipt(result))
            m_durableActionTracking = false;
        return;
    }

    setDurableActionStatus(result);
    if (statusValue(result, QStringLiteral("durable"))
            == QStringLiteral("observed")) {
        m_durableActionObserved = true;
    }

    const QString durable = statusValue(
        result, QStringLiteral("durable"));
    const QString completion = statusValue(
        result, QStringLiteral("completion"));
    if (durable == QStringLiteral("finalized")
        || completion == QStringLiteral("local-committed")
        || durable == QStringLiteral("rejected")
        || durable == QStringLiteral("expired")
        || durable == QStringLiteral("orphaned")) {
        m_durableActionTracking = false;
    }
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
    else if (durable == QStringLiteral("observed")
             && m_localDevelopmentProfile) {
        state = QStringLiteral("local-committed");
    }
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

void LogosPalaceUiBackend::refreshDeliveryNodeStatus()
{
    if (!isContextReady())
        return;
    setDeliveryNodeStatus(
        modules().palace_core.deliveryNodeStatus());
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
    refreshAssetAuthoringCapabilityState();
    // Storage verification completes asynchronously. Refresh the visible
    // projection after each storage poll so a restored visitor replaces the
    // placeholder only after the exact room graph resolves.
    refreshRoomProjection();
}

void LogosPalaceUiBackend::refreshAssetAuthoringCapabilityState()
{
    if (!isContextReady())
        return;
    setAssetAuthoringCapabilityState(
        modules().palace_core.assetAuthoringCapabilityStatus());
}

void LogosPalaceUiBackend::refreshLezState()
{
    if (!isContextReady())
        return;
    const QString status = modules().palace_core.lezStatus();
    setLezState(status);
    m_localDevelopmentProfile =
        statusValue(status, QStringLiteral("profile"))
        == QStringLiteral("local-development");
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
    setModerationCapabilityState(
        modules().palace_core.moderationCapabilityStatus());
    m_moderationActionId =
        statusValue(status, QStringLiteral("action"));
    m_moderationKind =
        statusValue(status, QStringLiteral("kind"));
    m_moderationTarget =
        statusValue(status, QStringLiteral("target"));
    const QString state = statusValue(status, QStringLiteral("state"));
    const QString palaceStatus =
        modules().palace_core.palaceStatus();
    bool localAuthorityRebuildPending = false;
    if (m_localDevelopmentProfile
        && state == QStringLiteral("local-committed")
        && !m_moderationActionId.isEmpty()) {
        bool actionOk = false;
        bool checkpointOk = false;
        const qulonglong action =
            m_moderationActionId.toULongLong(&actionOk);
        const qulonglong checkpoint =
            statusValue(palaceStatus, QStringLiteral("action"))
                .toULongLong(&checkpointOk);
        localAuthorityRebuildPending = actionOk && checkpointOk
            && action > checkpoint;
    }
    if (!m_localDevelopmentProfile || m_moderationActionId.isEmpty()
        || (isTerminalModerationState(state)
            && !localAuthorityRebuildPending)) {
        m_moderationTracking = false;
    } else if (!m_moderationDriveActive) {
        m_moderationTracking = true;
    }
}

void LogosPalaceUiBackend::refreshRoomLockState()
{
    if (!isContextReady())
        return;
    const QString status =
        modules().palace_core.moderationCapabilityStatus();
    const QString room = modules().palace_core.roomTitle();
    const QString lockCapability = statusValue(
        status, QStringLiteral("can_set_room_lock"));
    const QString locked = statusValue(
        modules().palace_core.roomLockStatus(),
        QStringLiteral("locked"));
    setRoomLockState(
        QStringLiteral("room=") + room
        + QStringLiteral(";locked=") + (locked.isEmpty() ? "0" : locked)
        + QStringLiteral(";can_set_room_lock=")
        + (lockCapability.isEmpty() ? "0" : lockCapability));
}

void LogosPalaceUiBackend::startLocalModerationTracking(
    const QString& receipt)
{
    if (!m_localDevelopmentProfile
        || !receipt.startsWith(QStringLiteral("ok;"))
        || m_moderationActionId.isEmpty()) {
        m_moderationTracking = false;
        return;
    }
    m_moderationPollCount = 0;
    m_moderationTracking = true;
}

QString LogosPalaceUiBackend::driveLocalModerationAction()
{
    if (!isContextReady())
        return rememberLezReceipt(unavailableReceipt());
    if (!m_localDevelopmentProfile || !m_moderationTracking)
        return refreshModeration();
    if (m_moderationDriveActive) {
        return rememberLezReceipt(
            QStringLiteral("rejected=moderation-reconcile-reentrant"));
    }

    const QString before = modules().palace_core.moderationStatus();
    const QString actionId =
        statusValue(before, QStringLiteral("action"));
    const QString state = statusValue(before, QStringLiteral("state"));
    const QString palaceStatus =
        modules().palace_core.palaceStatus();
    bool localAuthorityRebuildPending = false;
    if (m_localDevelopmentProfile
        && state == QStringLiteral("local-committed")
        && !actionId.isEmpty()) {
        bool actionOk = false;
        bool checkpointOk = false;
        const qulonglong action = actionId.toULongLong(&actionOk);
        const qulonglong checkpoint =
            statusValue(palaceStatus, QStringLiteral("action"))
                .toULongLong(&checkpointOk);
        localAuthorityRebuildPending = actionOk && checkpointOk
            && action > checkpoint;
    }
    if (actionId.isEmpty()
        || (isTerminalModerationState(state)
            && !localAuthorityRebuildPending)) {
        setModerationState(before);
        m_moderationTracking = false;
        refreshPalaceState();
        refreshAssetAuthoringCapabilityState();
        return rememberLezReceipt(before);
    }
    if (actionId != m_moderationActionId) {
        m_moderationTracking = false;
        return rememberLezReceipt(
            QStringLiteral("rejected=moderation-action-changed"));
    }
    if (m_moderationPollCount >= kMaximumModerationReconcilePolls) {
        m_moderationTracking = false;
        setModerationState(
            QStringLiteral("state=degraded;kind=") + m_moderationKind
            + QStringLiteral(";action=") + actionId
            + QStringLiteral(";target=") + m_moderationTarget
            + QStringLiteral(";reason=reconcile-budget-exhausted"));
        return rememberLezReceipt(
            QStringLiteral("rejected=moderation-reconcile-budget-exhausted;action=")
            + actionId
            + QStringLiteral(";attempts=")
            + QString::number(m_moderationPollCount));
    }

    m_moderationDriveActive = true;
    const QString durable =
        statusValue(before, QStringLiteral("durable"));
    const QString receipt = durable == QStringLiteral("submitted_to_lez")
        ? modules().palace_core.observePalaceTransition(actionId)
        : modules().palace_core.reconcilePalaceTransition(actionId);
    m_moderationDriveActive = false;
    ++m_moderationPollCount;
    refreshLezState();
    refreshPalaceState();
    refreshModerationState();
    refreshAssetAuthoringCapabilityState();

    if (receipt.startsWith(QStringLiteral("rejected="))
        && !isRetryableLocalModerationReceipt(receipt)) {
        m_moderationTracking = false;
        setModerationState(
            QStringLiteral("state=degraded;kind=") + m_moderationKind
            + QStringLiteral(";action=") + actionId
            + QStringLiteral(";target=") + m_moderationTarget
            + QStringLiteral(";reason=core-reconcile-rejected"));
    }
    return rememberLezReceipt(receipt);
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
