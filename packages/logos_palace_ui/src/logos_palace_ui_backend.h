#pragma once

#include <QTimer>

#include "logos_ui_plugin_context.h"
#include "rep_logos_palace_ui_source.h"

class LogosPalaceUiBackend : public LogosPalaceUiSimpleSource,
                             public LogosUiPluginContext
{
public:
    LogosPalaceUiBackend() = default;
    ~LogosPalaceUiBackend() override;

    QString applicationRoundTrip(QString payload) override;
    QString enterRoom(QString roomId) override;
    QString previewSpot(QString spotId) override;
    QString useSpot(QString spotId) override;
    QString reconcileSpot() override;
    QString refreshSpot() override;
    QString vmTurnMetrics(QString actionId, QString phase) override;
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
    QString reviewAsset(
        QString handle,
        QString decision) override;
    QString publishAsset(QString handle) override;
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
    QString fetchMvpStorageBundle(QString catalogBase64) override;
    QString verifyMvpStorageRetention() override;
    QString storageObjectStatus(QString objectId) override;
    QString storageSessionStatus() override;
    QString storagePeerEndpoint() override;
    QString connectStoragePeer(QString peerId, QString addressesJson) override;
    QString markStorageMaterialized() override;
    QString startLez(QString password) override;
    QString createIdentity(QString displayName) override;
    QString openPalace(QString palaceUri) override;
    QString refreshPalace() override;
    QString refreshLez() override;
    QString refreshIdentity() override;
    QString banUser(QString subjectUserIdHex) override;
    QString banProp(QString propId) override;
    QString refreshModeration() override;
    QString submitPalaceTransition(QString actionId,
                                   QString stateAccountIdHex,
                                   QString callerAccountIdHex,
                                   QString programIdHex,
                                   QString transitionJson) override;
    QString observePalaceTransition(QString actionId) override;
    QString reconcilePalaceTransition(QString actionId) override;
    QString actionStatus(QString actionId) override;

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
    void refreshDeliveryState();
    void refreshDeliveryNodeEvidence();
    void refreshStorageState();
    void refreshAssetAuthoringCapabilityState();
    void refreshLezState();
    void refreshPalaceState();
    void refreshModerationState();
    void refreshRoomProjection();
    void refreshSpotState();
    QString driveSpotAction();
    void applySpotStatus(const QString& status);
    void setSpotDegraded(const QString& reason,
                         const QString& actionId);
    void stopPollingTimers();

    QTimer* m_deliveryPollTimer = nullptr;
    QTimer* m_nodeEvidencePollTimer = nullptr;
    bool m_autoStartAttempted = false;
    bool m_spotTracking = false;
    bool m_spotDriveActive = false;
    bool m_spotPollBudgetExceeded = false;
    int m_spotPollCount = 0;
    QString m_moderationActionId;
    QString m_moderationKind;
    QString m_moderationTarget;
};
