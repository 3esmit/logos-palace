#include <logos_test.h>

#include "palace_lez_explorer_qt_transport.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <QByteArray>
#include <QEventLoop>
#include <QHostAddress>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace {

QByteArray httpResponse(
    const QByteArray& status,
    const QByteArray& contentType,
    const QByteArray& body,
    const QByteArray& additionalHeaders = {})
{
    QByteArray response = "HTTP/1.1 " + status + "\r\n";
    if (!contentType.isEmpty())
        response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size())
        + "\r\n";
    response += additionalHeaders;
    response += "Connection: close\r\n\r\n";
    response += body;
    return response;
}

class LocalHttpFixture {
public:
    LocalHttpFixture(
        QByteArray response,
        int responseDelayMilliseconds = 0,
        qsizetype splitOffset = -1)
        : response_(std::move(response))
        , responseDelayMilliseconds_(responseDelayMilliseconds)
        , splitOffset_(splitOffset)
    {
        QObject::connect(
            &server_,
            &QTcpServer::newConnection,
            &server_,
            [this]() { acceptConnections(); });
        LOGOS_ASSERT_TRUE(
            server_.listen(QHostAddress::LocalHost, 0U));
    }

    std::string origin() const
    {
        return "http://127.0.0.1:"
            + std::to_string(server_.serverPort());
    }

    const QByteArray& requestBytes() const
    {
        return request_;
    }

    int requestCount() const
    {
        return requestCount_;
    }

private:
    void acceptConnections()
    {
        while (server_.hasPendingConnections()) {
            QTcpSocket* socket = server_.nextPendingConnection();
            LOGOS_ASSERT_TRUE(socket != nullptr);
            QObject::connect(
                socket,
                &QTcpSocket::readyRead,
                socket,
                [this, socket]() { receiveRequest(socket); });
        }
    }

    void receiveRequest(QTcpSocket* socket)
    {
        request_.append(socket->readAll());
        if (requestComplete_)
            return;
        const qsizetype headerEnd = request_.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QByteArray header = request_.left(headerEnd);
        qsizetype contentLength = 0;
        const QList<QByteArray> lines = header.split('\n');
        for (QByteArray line : lines) {
            line = line.trimmed();
            if (!line.toLower().startsWith("content-length:"))
                continue;
            bool accepted = false;
            const qlonglong length =
                line.mid(line.indexOf(':') + 1).trimmed().toLongLong(
                    &accepted);
            LOGOS_ASSERT_TRUE(accepted);
            LOGOS_ASSERT_TRUE(length >= 0);
            contentLength = static_cast<qsizetype>(length);
        }
        if (request_.size() < headerEnd + 4 + contentLength)
            return;
        requestComplete_ = true;
        ++requestCount_;
        sendResponse(socket);
    }

    void sendResponse(QTcpSocket* socket)
    {
        if (responseDelayMilliseconds_ < 0)
            return;
        const QPointer<QTcpSocket> guarded(socket);
        const auto send = [guarded, response = response_, split = splitOffset_]() {
            if (guarded.isNull())
                return;
            if (split > 0 && split < response.size()) {
                guarded->write(response.left(split));
                guarded->flush();
                QTimer::singleShot(
                    20,
                    guarded,
                    [guarded, tail = response.mid(split)]() {
                        if (guarded.isNull())
                            return;
                        guarded->write(tail);
                        guarded->disconnectFromHost();
                    });
                return;
            }
            guarded->write(response);
            guarded->disconnectFromHost();
        };
        if (responseDelayMilliseconds_ == 0)
            send();
        else
            QTimer::singleShot(
                responseDelayMilliseconds_,
                socket,
                send);
    }

    QTcpServer server_;
    QByteArray response_;
    int responseDelayMilliseconds_ = 0;
    qsizetype splitOffset_ = -1;
    QByteArray request_;
    bool requestComplete_ = false;
    int requestCount_ = 0;
};

palace::PalaceLezExplorerQtTransportConfigV1 transportConfig(
    const std::uint32_t timeoutMilliseconds = 2000U)
{
    palace::PalaceLezExplorerQtTransportConfigV1 config;
    config.transferTimeoutMilliseconds = timeoutMilliseconds;
    config.maxRequestBodyBytes = 1024U;
    config.maxAllowedResponseBytes = 64U * 1024U;
    config.allowInsecureLoopbackForTests = true;
    return config;
}

palace::PalaceLezExplorerCommandV1 command(
    const std::string& origin,
    const std::size_t maximumResponseBytes = 4096U,
    const std::uint64_t sequence = 41U)
{
    palace::PalaceLezExplorerCommandV1 value;
    value.sequence = sequence;
    value.kind = palace::PalaceLezExplorerCommandKind::Blocks;
    value.method = "POST";
    value.origin = origin;
    value.path = "/api/get_blocks3022937127152978530";
    value.requestContentType =
        "application/x-www-form-urlencoded";
    value.formBody = "limit=2&before=40948";
    value.maxResponseBytes = maximumResponseBytes;
    return value;
}

bool waitForCompletion(bool& completed, const int timeoutMilliseconds)
{
    if (completed)
        return true;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (completed)
            loop.quit();
    });
    timeout.start(timeoutMilliseconds);
    poll.start(1);
    loop.exec();
    return completed;
}

} // namespace

LOGOS_TEST(lez_explorer_qt_transport_posts_form_and_accumulates_bounded_chunks)
{
    const QByteArray body =
        "[{\"bedrock_status\":\"Finalized\"}]";
    const QByteArray response =
        httpResponse("200 OK", "application/json", body);
    LocalHttpFixture fixture(response, 0, response.indexOf("\r\n\r\n") + 6);
    palace::PalaceLezExplorerQtTransport transport(transportConfig());

    int callbackCount = 0;
    bool completed = false;
    palace::PalaceLezExplorerHttpResponseV1 captured;
    const auto dispatched = transport.dispatch(
        command(fixture.origin()),
        [&](const palace::PalaceLezExplorerHttpResponseV1& result) {
            ++callbackCount;
            captured = result;
            completed = true;
        });
    LOGOS_ASSERT_TRUE(dispatched.accepted);
    LOGOS_ASSERT_TRUE(transport.busy());
    LOGOS_ASSERT_EQ(transport.activeCommandSequence(), 41U);
    LOGOS_ASSERT_TRUE(waitForCompletion(completed, 3000));

    LOGOS_ASSERT_EQ(callbackCount, 1);
    LOGOS_ASSERT_FALSE(transport.busy());
    LOGOS_ASSERT_EQ(captured.commandSequence, 41U);
    LOGOS_ASSERT_EQ(captured.statusCode, 200);
    LOGOS_ASSERT_EQ(captured.contentType, "application/json");
    LOGOS_ASSERT_EQ(captured.effectiveOrigin, fixture.origin());
    LOGOS_ASSERT_FALSE(captured.redirected);
    LOGOS_ASSERT_TRUE(captured.transportError.empty());
    LOGOS_ASSERT_EQ(captured.body, body.toStdString());
    LOGOS_ASSERT_EQ(fixture.requestCount(), 1);
    LOGOS_ASSERT_TRUE(fixture.requestBytes().startsWith(
        "POST /api/get_blocks3022937127152978530 HTTP/1.1\r\n"));
    LOGOS_ASSERT_TRUE(fixture.requestBytes().toLower().contains(
        "content-type: application/x-www-form-urlencoded\r\n"));
    LOGOS_ASSERT_TRUE(fixture.requestBytes().endsWith(
        "\r\n\r\nlimit=2&before=40948"));
}

LOGOS_TEST(lez_explorer_qt_transport_allows_only_one_inflight_request)
{
    LocalHttpFixture fixture(
        httpResponse("200 OK", "application/json", "[]"),
        100);
    palace::PalaceLezExplorerQtTransport transport(transportConfig());
    bool completed = false;
    int firstCallbacks = 0;
    int secondCallbacks = 0;
    LOGOS_ASSERT_TRUE(transport.dispatch(
        command(fixture.origin(), 4096U, 1U),
        [&](const palace::PalaceLezExplorerHttpResponseV1&) {
            ++firstCallbacks;
            completed = true;
        }).accepted);
    const auto second = transport.dispatch(
        command(fixture.origin(), 4096U, 2U),
        [&](const palace::PalaceLezExplorerHttpResponseV1&) {
            ++secondCallbacks;
        });
    LOGOS_ASSERT_FALSE(second.accepted);
    LOGOS_ASSERT_EQ(second.reason, "transport-busy");
    LOGOS_ASSERT_TRUE(waitForCompletion(completed, 3000));
    LOGOS_ASSERT_EQ(firstCallbacks, 1);
    LOGOS_ASSERT_EQ(secondCallbacks, 0);
}

LOGOS_TEST(lez_explorer_qt_transport_reports_manual_redirect_without_following)
{
    LocalHttpFixture fixture(httpResponse(
        "302 Found",
        "application/json",
        {},
        "Location: https://attacker.invalid/finality\r\n"));
    palace::PalaceLezExplorerQtTransport transport(transportConfig());
    bool completed = false;
    int callbackCount = 0;
    palace::PalaceLezExplorerHttpResponseV1 captured;
    LOGOS_ASSERT_TRUE(transport.dispatch(
        command(fixture.origin()),
        [&](const palace::PalaceLezExplorerHttpResponseV1& result) {
            ++callbackCount;
            captured = result;
            completed = true;
        }).accepted);
    LOGOS_ASSERT_TRUE(waitForCompletion(completed, 3000));
    LOGOS_ASSERT_EQ(callbackCount, 1);
    LOGOS_ASSERT_EQ(captured.statusCode, 302);
    LOGOS_ASSERT_TRUE(captured.redirected);
    LOGOS_ASSERT_EQ(captured.effectiveOrigin, fixture.origin());
    LOGOS_ASSERT_EQ(fixture.requestCount(), 1);
}

LOGOS_TEST(lez_explorer_qt_transport_aborts_overflow_and_completes_once)
{
    LocalHttpFixture fixture(httpResponse(
        "200 OK",
        "application/json",
        "0123456789abcdef"));
    palace::PalaceLezExplorerQtTransport transport(transportConfig());
    bool completed = false;
    int callbackCount = 0;
    palace::PalaceLezExplorerHttpResponseV1 captured;
    LOGOS_ASSERT_TRUE(transport.dispatch(
        command(fixture.origin(), 8U),
        [&](const palace::PalaceLezExplorerHttpResponseV1& result) {
            ++callbackCount;
            captured = result;
            completed = true;
        }).accepted);
    LOGOS_ASSERT_TRUE(waitForCompletion(completed, 3000));
    LOGOS_ASSERT_EQ(callbackCount, 1);
    LOGOS_ASSERT_EQ(captured.transportError, "response-too-large");
    LOGOS_ASSERT_TRUE(captured.body.size() <= 8U);
    LOGOS_ASSERT_FALSE(transport.busy());
}

LOGOS_TEST(lez_explorer_qt_transport_surfaces_transfer_timeout_once)
{
    LocalHttpFixture fixture({}, -1);
    palace::PalaceLezExplorerQtTransport transport(
        transportConfig(100U));
    bool completed = false;
    int callbackCount = 0;
    palace::PalaceLezExplorerHttpResponseV1 captured;
    LOGOS_ASSERT_TRUE(transport.dispatch(
        command(fixture.origin()),
        [&](const palace::PalaceLezExplorerHttpResponseV1& result) {
            ++callbackCount;
            captured = result;
            completed = true;
        }).accepted);
    LOGOS_ASSERT_TRUE(waitForCompletion(completed, 3000));
    LOGOS_ASSERT_EQ(callbackCount, 1);
    LOGOS_ASSERT_FALSE(captured.transportError.empty());
    LOGOS_ASSERT_FALSE(transport.busy());
}

LOGOS_TEST(lez_explorer_qt_transport_rejects_invalid_commands_without_callback)
{
    LocalHttpFixture fixture(
        httpResponse("200 OK", "application/json", "[]"));
    palace::PalaceLezExplorerQtTransport transport(transportConfig());
    int callbackCount = 0;
    const auto completion =
        [&](const palace::PalaceLezExplorerHttpResponseV1&) {
            ++callbackCount;
        };

    auto invalid = command(fixture.origin());
    invalid.method = "GET";
    LOGOS_ASSERT_FALSE(transport.dispatch(invalid, completion).accepted);
    invalid = command(fixture.origin());
    invalid.path += "?confused=true";
    LOGOS_ASSERT_FALSE(transport.dispatch(invalid, completion).accepted);
    invalid = command(fixture.origin());
    invalid.maxResponseBytes = 0U;
    LOGOS_ASSERT_FALSE(transport.dispatch(invalid, completion).accepted);
    invalid = command("http://192.0.2.1:8080");
    LOGOS_ASSERT_FALSE(transport.dispatch(invalid, completion).accepted);
    LOGOS_ASSERT_FALSE(
        transport.dispatch(command(fixture.origin()), {}).accepted);
    LOGOS_ASSERT_EQ(callbackCount, 0);
    LOGOS_ASSERT_FALSE(transport.busy());
}

LOGOS_TEST(lez_explorer_qt_transport_destruction_aborts_with_one_completion)
{
    LocalHttpFixture fixture({}, -1);
    int callbackCount = 0;
    palace::PalaceLezExplorerHttpResponseV1 captured;
    {
        auto transport =
            std::make_unique<palace::PalaceLezExplorerQtTransport>(
                transportConfig());
        LOGOS_ASSERT_TRUE(transport->dispatch(
            command(fixture.origin()),
            [&](const palace::PalaceLezExplorerHttpResponseV1& result) {
                ++callbackCount;
                captured = result;
            }).accepted);
        transport.reset();
    }
    LOGOS_ASSERT_EQ(callbackCount, 1);
    LOGOS_ASSERT_EQ(captured.transportError, "transport-destroyed");
}
