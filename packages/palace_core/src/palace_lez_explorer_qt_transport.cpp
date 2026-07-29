#include "palace_lez_explorer_qt_transport.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QUrl>
#include <QVariant>

namespace palace {
namespace {

constexpr std::uint32_t kMaximumTransferTimeoutMilliseconds = 120000U;
constexpr std::size_t kMaximumRequestBodyBytes = 64U * 1024U;
constexpr std::size_t kMaximumResponseBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaximumOriginBytes = 512U;
constexpr std::size_t kMaximumPathBytes = 512U;
constexpr char kFormContentType[] =
    "application/x-www-form-urlencoded";

bool isAscii(const std::string& value)
{
    return std::all_of(value.begin(), value.end(), [](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte >= 0x20U && byte <= 0x7eU;
    });
}

bool isPathCharacter(const char character)
{
    return (character >= '0' && character <= '9')
        || (character >= 'A' && character <= 'Z')
        || (character >= 'a' && character <= 'z')
        || character == '/' || character == '_' || character == '-'
        || character == '.';
}

bool isFormCharacter(const char character)
{
    return (character >= '0' && character <= '9')
        || (character >= 'A' && character <= 'Z')
        || (character >= 'a' && character <= 'z')
        || character == '=' || character == '&' || character == '_'
        || character == '-' || character == '.' || character == ':';
}

bool validConfig(const PalaceLezExplorerQtTransportConfigV1& config)
{
    return config.transferTimeoutMilliseconds != 0U
        && config.transferTimeoutMilliseconds
            <= kMaximumTransferTimeoutMilliseconds
        && config.maxRequestBodyBytes != 0U
        && config.maxRequestBodyBytes <= kMaximumRequestBodyBytes
        && config.maxAllowedResponseBytes != 0U
        && config.maxAllowedResponseBytes <= kMaximumResponseBytes;
}

QByteArray byteArray(const std::string& value)
{
    if (value.size()
        > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {};
    }
    return QByteArray(
        value.data(),
        static_cast<qsizetype>(value.size()));
}

std::string boundedErrorString(const QString& value)
{
    constexpr qsizetype maximumBytes = 1024;
    QByteArray bytes = value.toUtf8();
    if (bytes.size() > maximumBytes)
        bytes.truncate(maximumBytes);
    return std::string(
        bytes.constData(),
        static_cast<std::size_t>(bytes.size()));
}

} // namespace

PalaceLezExplorerQtTransport::PalaceLezExplorerQtTransport(
    PalaceLezExplorerQtTransportConfigV1 config,
    QObject* parent)
    : QObject(parent)
    , config_(std::move(config))
{
}

PalaceLezExplorerQtTransport::~PalaceLezExplorerQtTransport()
{
    destroying_ = true;
    if (reply_ == nullptr)
        return;
    forcedTransportError_ = "transport-destroyed";
    QNetworkReply* activeReply = reply_;
    activeReply->abort();
    // Qt guarantees abort() emits finished(). Retain a defensive completion
    // path for a custom network backend that marks finished without emitting.
    if (reply_ == activeReply && activeReply->isFinished())
        deliverReply(activeReply, forcedTransportError_);
}

PalaceLezExplorerQtDispatchResult
PalaceLezExplorerQtTransport::dispatch(
    const PalaceLezExplorerCommandV1& command,
    PalaceLezExplorerQtCompletion completion)
{
    if (destroying_)
        return {false, "transport-destroying"};
    if (reply_ != nullptr)
        return {false, "transport-busy"};
    if (QThread::currentThread() != thread())
        return {false, "transport-thread-mismatch"};

    QUrl url;
    const PalaceLezExplorerQtDispatchResult validation =
        validateCommand(command, completion, url);
    if (!validation.accepted)
        return validation;

    QNetworkRequest request(url);
    request.setRawHeader(
        QByteArrayLiteral("Content-Type"),
        QByteArrayLiteral(kFormContentType));
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(std::chrono::milliseconds(
        config_.transferTimeoutMilliseconds));

    command_ = command;
    completion_ = std::move(completion);
    responseBody_.clear();
    redirectAttempted_ = false;
    responseOverflow_ = false;
    responseReadError_ = false;
    completionDelivered_ = false;
    forcedTransportError_.clear();

    const QByteArray requestBody = byteArray(command.formBody);
    reply_ = manager_.post(request, requestBody);
    if (reply_ == nullptr) {
        clearRequestState();
        return {false, "network-request-creation-failed"};
    }

    QNetworkReply* const activeReply = reply_;
    connect(
        activeReply,
        &QNetworkReply::metaDataChanged,
        this,
        [this, activeReply]() { inspectMetadata(activeReply); });
    connect(
        activeReply,
        &QNetworkReply::readyRead,
        this,
        [this, activeReply]() { readAvailable(activeReply); });
    connect(
        activeReply,
        &QNetworkReply::redirected,
        this,
        [this, activeReply](const QUrl&) {
            if (reply_ == activeReply)
                redirectAttempted_ = true;
        });
    connect(
        activeReply,
        &QNetworkReply::finished,
        this,
        [this, activeReply]() { finishReply(activeReply); });
    return {true, "accepted"};
}

bool PalaceLezExplorerQtTransport::busy() const
{
    return reply_ != nullptr;
}

std::uint64_t
PalaceLezExplorerQtTransport::activeCommandSequence() const
{
    return reply_ == nullptr ? 0U : command_.sequence;
}

const PalaceLezExplorerQtTransportConfigV1&
PalaceLezExplorerQtTransport::configuration() const
{
    return config_;
}

std::string PalaceLezExplorerQtTransport::canonicalOrigin(
    const QUrl& url)
{
    const QString scheme = url.scheme().toLower();
    QString host = url.host(QUrl::FullyDecoded).toLower();
    if (host.contains(QLatin1Char(':')))
        host = QLatin1Char('[') + host + QLatin1Char(']');
    QString value = scheme + QStringLiteral("://") + host;
    const int port = url.port(-1);
    if (port >= 0)
        value += QLatin1Char(':') + QString::number(port);
    const QByteArray encoded = value.toUtf8();
    return std::string(
        encoded.constData(),
        static_cast<std::size_t>(encoded.size()));
}

PalaceLezExplorerQtDispatchResult
PalaceLezExplorerQtTransport::validateCommand(
    const PalaceLezExplorerCommandV1& command,
    const PalaceLezExplorerQtCompletion& completion,
    QUrl& url) const
{
    if (!validConfig(config_))
        return {false, "invalid-transport-config"};
    if (!completion)
        return {false, "missing-transport-completion"};
    if (command.sequence == 0U || command.method != "POST"
        || command.requestContentType != kFormContentType) {
        return {false, "invalid-transport-command"};
    }
    if (command.maxResponseBytes == 0U
        || command.maxResponseBytes > config_.maxAllowedResponseBytes
        || command.formBody.empty()
        || command.formBody.size() > config_.maxRequestBodyBytes) {
        return {false, "invalid-transport-bounds"};
    }
    if (command.origin.empty()
        || command.origin.size() > kMaximumOriginBytes
        || command.path.size() > kMaximumPathBytes
        || !isAscii(command.origin) || !isAscii(command.path)
        || !isAscii(command.formBody)
        || command.path.empty() || command.path.front() != '/'
        || !std::all_of(
            command.path.begin(),
            command.path.end(),
            isPathCharacter)
        || !std::all_of(
            command.formBody.begin(),
            command.formBody.end(),
            isFormCharacter)) {
        return {false, "invalid-transport-encoding"};
    }

    const QUrl origin(
        QString::fromUtf8(
            command.origin.data(),
            static_cast<qsizetype>(command.origin.size())),
        QUrl::StrictMode);
    if (!origin.isValid() || origin.host().isEmpty()
        || !origin.userInfo().isEmpty() || !origin.path().isEmpty()
        || origin.hasQuery() || origin.hasFragment()
        || canonicalOrigin(origin) != command.origin) {
        return {false, "invalid-transport-origin"};
    }
    const QString scheme = origin.scheme().toLower();
    if (scheme != QStringLiteral("https")) {
        const QString host = origin.host(QUrl::FullyDecoded).toLower();
        if (!config_.allowInsecureLoopbackForTests
            || scheme != QStringLiteral("http")
            || (host != QStringLiteral("127.0.0.1")
                && host != QStringLiteral("::1"))) {
            return {false, "insecure-transport-origin"};
        }
    }

    url = origin;
    url.setPath(
        QString::fromUtf8(
            command.path.data(),
            static_cast<qsizetype>(command.path.size())),
        QUrl::DecodedMode);
    if (!url.isValid() || canonicalOrigin(url) != command.origin)
        return {false, "invalid-transport-url"};
    return {true, "accepted"};
}

void PalaceLezExplorerQtTransport::inspectMetadata(
    QNetworkReply* reply)
{
    if (reply_ != reply || completionDelivered_)
        return;
    if (reply->attribute(
            QNetworkRequest::RedirectionTargetAttribute).isValid()) {
        redirectAttempted_ = true;
    }
    const QVariant length =
        reply->header(QNetworkRequest::ContentLengthHeader);
    bool converted = false;
    const qlonglong contentLength = length.toLongLong(&converted);
    if (converted && contentLength >= 0
        && static_cast<unsigned long long>(contentLength)
            > static_cast<unsigned long long>(
                command_.maxResponseBytes)) {
        if (reply->isFinished()) {
            responseOverflow_ = true;
            forcedTransportError_ = "response-too-large";
        } else {
            failOverflow(reply);
        }
    }
}

void PalaceLezExplorerQtTransport::readAvailable(
    QNetworkReply* reply)
{
    if (reply_ != reply || completionDelivered_ || responseOverflow_)
        return;
    const std::size_t accumulated =
        static_cast<std::size_t>(responseBody_.size());
    const std::size_t remaining =
        command_.maxResponseBytes - accumulated;
    const qint64 available = reply->bytesAvailable();
    if (available < 0) {
        responseReadError_ = true;
        forcedTransportError_ = "response-read-failed";
        reply->abort();
        return;
    }
    if (static_cast<unsigned long long>(available)
        > static_cast<unsigned long long>(remaining)) {
        failOverflow(reply);
        return;
    }
    if (available == 0)
        return;
    const QByteArray chunk = reply->read(available);
    if (chunk.size() != available) {
        responseReadError_ = true;
        forcedTransportError_ = "response-read-failed";
        reply->abort();
        return;
    }
    responseBody_.append(chunk);
}

void PalaceLezExplorerQtTransport::failOverflow(
    QNetworkReply* reply)
{
    if (reply_ != reply || completionDelivered_ || responseOverflow_)
        return;
    responseOverflow_ = true;
    forcedTransportError_ = "response-too-large";
    if (!reply->isFinished())
        reply->abort();
}

void PalaceLezExplorerQtTransport::finishReply(
    QNetworkReply* reply)
{
    if (reply_ != reply || completionDelivered_)
        return;
    inspectMetadata(reply);
    if (reply_ != reply || completionDelivered_)
        return;
    if (!responseOverflow_ && !responseReadError_)
        readAvailable(reply);
    if (reply_ == reply && !completionDelivered_)
        deliverReply(reply, forcedTransportError_);
}

void PalaceLezExplorerQtTransport::deliverReply(
    QNetworkReply* reply,
    const std::string& forcedTransportError)
{
    if (reply_ != reply || completionDelivered_)
        return;
    completionDelivered_ = true;

    PalaceLezExplorerHttpResponseV1 response;
    response.commandSequence = command_.sequence;
    const QVariant status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    response.statusCode = status.isValid() ? status.toInt() : 0;
    const QByteArray contentType =
        reply->rawHeader(QByteArrayLiteral("Content-Type"));
    response.contentType = std::string(
        contentType.constData(),
        static_cast<std::size_t>(contentType.size()));
    response.effectiveOrigin = canonicalOrigin(reply->url());
    response.redirected = redirectAttempted_
        || reply->attribute(
            QNetworkRequest::RedirectionTargetAttribute).isValid();
    if (!forcedTransportError.empty()) {
        response.transportError = forcedTransportError;
    } else if (reply->error() != QNetworkReply::NoError) {
        response.transportError =
            boundedErrorString(reply->errorString());
    }
    response.body = std::string(
        responseBody_.constData(),
        static_cast<std::size_t>(responseBody_.size()));

    PalaceLezExplorerQtCompletion completion =
        std::move(completion_);
    reply_ = nullptr;
    clearRequestState();
    reply->deleteLater();
    if (completion) {
        try {
            completion(response);
        } catch (...) {
            // Exceptions must not cross the Qt signal boundary. Delivery has
            // already occurred exactly once.
        }
    }
}

void PalaceLezExplorerQtTransport::clearRequestState()
{
    command_ = {};
    completion_ = {};
    responseBody_.clear();
    redirectAttempted_ = false;
    responseOverflow_ = false;
    responseReadError_ = false;
    completionDelivered_ = false;
    forcedTransportError_.clear();
}

} // namespace palace
