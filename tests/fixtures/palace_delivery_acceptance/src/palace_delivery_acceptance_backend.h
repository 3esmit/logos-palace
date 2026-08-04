#pragma once

#include <QTimer>

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>

#include "logos_ui_plugin_context.h"
#include "rep_palace_delivery_acceptance_source.h"

class PalaceDeliveryAcceptanceBackend
    : public PalaceDeliveryAcceptanceSimpleSource,
      public LogosUiPluginContext
{
public:
    QString start(QString entryNode, qint64 tcpPort) override;
    QString inject(QString scenario) override;

protected:
    void onContextReady() override;

private:
    enum class EventKind {
        NodeStarted,
        NodeStopped,
        ConnectionChanged,
        MessageSent,
        MessagePropagated,
        MessageError,
    };

    struct QueuedEvent {
        EventKind kind = EventKind::ConnectionChanged;
        bool succeeded = false;
        std::string value;
    };

    void enqueue(QueuedEvent event);
    void pump();
    void applyEvent(const QueuedEvent& event);
    void trySubscribe();
    void refreshNodeEvidence();
    QString rememberReceipt(const QString& value);
    void fail(const QString& reason);

    QTimer* m_pumpTimer = nullptr;
    std::mutex m_eventMutex;
    std::deque<QueuedEvent> m_events;
    std::map<std::string, std::string> m_scenarioByRequest;
    std::size_t m_nextScenario = 0;
    unsigned int m_evidenceTicks = 0;
    bool m_eventOverflow = false;
    bool m_callbacksRegistered = false;
    bool m_startAttempted = false;
    bool m_nodeCreated = false;
    bool m_nodeStarted = false;
    bool m_connected = false;
    bool m_subscribePending = false;
    bool m_subscribed = false;
    bool m_failed = false;
};
