#pragma once

#include "palace_lez_explorer_finality.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>

class QNetworkReply;
class QUrl;

namespace palace {

struct PalaceLezExplorerQtTransportConfigV1 {
    std::uint32_t transferTimeoutMilliseconds = 15000U;
    std::size_t maxRequestBodyBytes = 16U * 1024U;
    std::size_t maxAllowedResponseBytes = 4U * 1024U * 1024U;

    // Test-only escape hatch. Production commands must use HTTPS. When true,
    // HTTP remains restricted to literal loopback hosts.
    bool allowInsecureLoopbackForTests = false;
};

struct PalaceLezExplorerQtDispatchResult {
    bool accepted = false;
    std::string reason;
};

using PalaceLezExplorerQtCompletion =
    std::function<void(const PalaceLezExplorerHttpResponseV1&)>;

// Core-only Qt transport. QML never owns or observes the network manager.
// Accepted requests produce exactly one completion while this object remains
// alive. Destruction aborts an active request and delivers its final error
// synchronously before member teardown.
class PalaceLezExplorerQtTransport final : public QObject {
public:
    explicit PalaceLezExplorerQtTransport(
        PalaceLezExplorerQtTransportConfigV1 config = {},
        QObject* parent = nullptr);
    ~PalaceLezExplorerQtTransport() override;

    PalaceLezExplorerQtDispatchResult dispatch(
        const PalaceLezExplorerCommandV1& command,
        PalaceLezExplorerQtCompletion completion);

    bool busy() const;
    std::uint64_t activeCommandSequence() const;
    const PalaceLezExplorerQtTransportConfigV1& configuration() const;

private:
    static std::string canonicalOrigin(const QUrl& url);
    PalaceLezExplorerQtDispatchResult validateCommand(
        const PalaceLezExplorerCommandV1& command,
        const PalaceLezExplorerQtCompletion& completion,
        QUrl& url) const;
    void inspectMetadata(QNetworkReply* reply);
    void readAvailable(QNetworkReply* reply);
    void failOverflow(QNetworkReply* reply);
    void finishReply(QNetworkReply* reply);
    void deliverReply(
        QNetworkReply* reply,
        const std::string& forcedTransportError = {});
    void clearRequestState();

    PalaceLezExplorerQtTransportConfigV1 config_;
    QNetworkAccessManager manager_;
    QNetworkReply* reply_ = nullptr;
    PalaceLezExplorerCommandV1 command_;
    PalaceLezExplorerQtCompletion completion_;
    QByteArray responseBody_;
    bool redirectAttempted_ = false;
    bool responseOverflow_ = false;
    bool responseReadError_ = false;
    bool completionDelivered_ = false;
    bool destroying_ = false;
    std::string forcedTransportError_;
};

} // namespace palace
