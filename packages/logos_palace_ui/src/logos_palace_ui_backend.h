#pragma once

#include <functional>

#include <QTimer>

#include "logos_ui_plugin_context.h"
#include "rep_logos_palace_ui_source.h"

// Owns UI-side workflow polling and bounded workflow transitions. Palace Core
// remains the authority; this controller keeps durable-action polling,
// recovery refresh, and multi-call asset publication out of QML and can be
// stopped as one unit when the UI context is torn down.
class PalaceUiController
{
public:
    using RefreshCallback = std::function<void()>;

    void configure(RefreshCallback refresh,
                   RefreshCallback refreshNodeStatus);
    void start();
    void stop();

private:
    QTimer m_refreshTimer;
    QTimer m_nodeStatusTimer;
    RefreshCallback m_refresh;
    RefreshCallback m_refreshNodeStatus;
    bool m_configured = false;
};

class LogosPalaceUiBackend : public LogosPalaceUiSimpleSource,
                             public LogosUiPluginContext
{
public:
    LogosPalaceUiBackend() = default;
    ~LogosPalaceUiBackend() override;

    QString enterRoom(QString roomId) override;
    QString previewSpot(QString spotId) override;
    QString useSpot(QString spotId) override;
    QString reconcileSpot() override;
    QString refreshSpot() override;
    QString startDelivery(QString nodeConfig) override;
    QString subscribeRoom(QString networkId,
                          QString palaceId,
                          QString roomId,
                          qint64 roomEpoch) override;
    QString say(QString text) override;
    QString moveAvatar(qint64 x, qint64 y) override;
    QString wearProp(QString propId) override;
    QString removeProp(QString propId) override;
    QString refreshPresence() override;
    QString startStorage(QString nodeConfig) override;
    QString connectStorage() override;
    QString fetchPngDerivative(QString sourceCid,
                               QString derivativeCid,
                               qint64 byteLength,
                               QString contentSha256,
                               qint64 width,
                               qint64 height) override;
    QString assetStatus(QString derivativeCid) override;
    QString publishVerifiedPng(QString handle) override;
    QString publicationStatus(QString handle) override;
    QString beginAssetStage(QString label) override;
    QString appendAssetStageChunk(
        QString sessionId,
        qint64 sequence,
        QString base64Chunk) override;
    QString commitAssetStage(QString sessionId) override;
    QString cancelAssetStage(QString sessionId) override;
    QString beginAssetImport(QString label, qint64 byteLength) override;
    QString appendAssetImportChunk(QString base64Chunk) override;
    QString finishAssetImport() override;
    QString cancelAssetImport() override;
    QString reviewAsset(
        QString handle,
        QString decision) override;
    QString publishAsset(QString handle) override;
    QString approveAndPublishAsset(QString handle) override;
    QString assignRoomBackground(
        QString roomId,
        QString handle) override;
    QString assignPropAsset(
        QString propId,
        QString handle,
        qint64 anchorX,
        qint64 anchorY,
        QString layer) override;
    QString refreshAssetAuthoring() override;
    QString publishMvpStorageBundle() override;
    QString mvpStorageBundleStatus() override;
    QString trackStorageBundle() override;
    QString fetchMvpStorageBundle(QString catalogBase64) override;
    QString verifyMvpStorageRetention() override;
    QString storageObjectStatus(QString objectId) override;
    QString storageSessionStatus() override;
    QString storagePeerEndpoint() override;
    QString connectStoragePeer(QString peerId, QString addressesJson) override;
    QString prepareExistingPalaceStorage(
        QString catalogBase64,
        QString peerId,
        QString addressesJson,
        bool attachPeer) override;
    QString preparePalaceOnboarding(
        QString mode,
        QString password,
        QString displayName,
        QString palaceAddress,
        QString catalogBase64,
        QString peerId,
        QString addressesJson) override;
    QString beginPalaceCreation(QString title) override;
    QString resumePalaceOnboarding() override;
    QString resumeExistingPalace(
        QString palaceAddress,
        QString catalogBase64,
        QString peerId,
        QString addressesJson,
        bool attachPeer) override;
    QString markStorageMaterialized() override;
    QString startLez(QString password) override;
    QString createIdentity(QString displayName) override;
    QString createPalace(QString title) override;
    QString createInitialRoomState() override;
    QString openPalace(QString palaceUri) override;
    QString registerPalaceUser() override;
    QString trackPalaceRegistration() override;
    QString refreshPalace() override;
    QString refreshLez() override;
    QString refreshIdentity() override;
    QString banUser(QString subjectUserIdHex) override;
    QString banProp(QString propId) override;
    QString delegateModerator(QString subjectUserIdHex) override;
    QString setRoomLocked(QString roomId, bool locked) override;
    QString refreshModeration() override;
    QString submitPalaceTransition(QString actionId,
                                   QString stateAccountIdHex,
                                   QString callerAccountIdHex,
                                   QString programIdHex,
                                   QString transitionJson) override;
    QString observePalaceTransition(QString actionId) override;
    QString reconcilePalaceTransition(QString actionId) override;
    QString actionStatus(QString actionId) override;
    QString trackDurableAction(QString actionId) override;

protected:
    void onContextReady() override;

private:
    QString unavailableReceipt() const;
    QString rememberDeliveryReceipt(const QString& receipt);
    QString rememberStorageReceipt(const QString& receipt);
    QString rememberLezReceipt(const QString& receipt);
    QString rememberSpotReceipt(const QString& receipt);
    QString rememberModerationReceipt(
        const QString& kind,
        const QString& target,
        const QString& receipt);
    void applyModerationActionStatus(
        const QString& actionId,
        const QString& status);
    void startLocalModerationTracking(const QString& receipt);
    QString driveLocalModerationAction();
    void refreshDeliveryState();
    void refreshDeliveryNodeStatus();
    void refreshStorageState();
    void refreshAssetAuthoringCapabilityState();
    void refreshLezState();
    void refreshPalaceState();
    void refreshModerationState();
    void refreshRoomLockState();
    void refreshRoomProjection();
    void refreshSpotState();
    void driveAutomaticDeliveryStart();
    void driveDurableAction();
    void driveStorageBundle();
    void drivePalaceRegistration();
    void driveOnboardingWorkflow();
    void setOnboardingWorkflowFailure(const QString& receipt);
    bool isRetryableOnboardingOpenReceipt(const QString& receipt) const;
    void resetOnboardingWorkflowTracking();
    void setAssetImportFailure(const QString& receipt);
    void resetAssetImportTracking();
    QString driveSpotAction();
    void applySpotStatus(const QString& status);
    void setSpotDegraded(const QString& reason,
                         const QString& actionId);
    void stopPollingTimers();

    PalaceUiController m_uiController;
    bool m_autoStartAttempted = false;
    bool m_autoStartDeliveryPending = false;
    QString m_autoStartDeliveryConfig;
    bool m_spotTracking = false;
    bool m_spotDriveActive = false;
    bool m_spotPollBudgetExceeded = false;
    int m_spotPollCount = 0;
    bool m_localDevelopmentProfile = false;
    bool m_moderationTracking = false;
    bool m_moderationDriveActive = false;
    int m_moderationPollCount = 0;
    QString m_moderationActionId;
    QString m_moderationKind;
    QString m_moderationTarget;
    bool m_durableActionTracking = false;
    bool m_durableActionObserved = false;
    int m_durableActionPollCount = 0;
    QString m_durableActionId;
    bool m_storageBundleTracking = false;
    int m_storageBundlePollCount = 0;
    bool m_palaceRegistrationTracking = false;
    bool m_palaceRegistrationObserved = false;
    int m_palaceRegistrationPollCount = 0;
    QString m_palaceRegistrationActionId;
    bool m_onboardingWorkflowTracking = false;
    bool m_onboardingWorkflowExistingPalace = false;
    bool m_onboardingWorkflowObserved = false;
    int m_onboardingWorkflowPollCount = 0;
    QString m_onboardingWorkflowPhase;
    QString m_onboardingWorkflowTitle;
    QString m_onboardingWorkflowPalaceUri;
    QString m_onboardingWorkflowActionId;
    QString m_onboardingWorkflowCatalog;
    QString m_onboardingWorkflowPeerId;
    QString m_onboardingWorkflowAddressesJson;
    bool m_onboardingWorkflowAttachPeer = false;
    bool m_onboardingWorkflowInitialRoomReady = false;
    bool m_onboardingWorkflowAwaitingInitialRoom = false;
    bool m_assetImportTracking = false;
    QString m_assetImportSession;
    qint64 m_assetImportExpectedSequence = 0;
    qint64 m_assetImportByteLength = 0;
    qint64 m_assetImportBytes = 0;
};
