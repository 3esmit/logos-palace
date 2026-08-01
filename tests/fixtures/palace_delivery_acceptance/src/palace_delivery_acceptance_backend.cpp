#include "palace_delivery_acceptance_backend.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <array>
#include <chrono>
#include <cctype>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "logos_sdk.h"

namespace {

constexpr std::size_t kMaximumQueuedEvents = 64U;
constexpr std::size_t kMaximumRequestIdBytes = 256U;
constexpr std::int64_t kRoomEpoch = 9;
constexpr std::int64_t kInjectorKeyEpoch = 4;
constexpr int kPresenceHelloKind = 0;
constexpr int kPresenceByeKind = 1;
constexpr int kMotionKind = 2;
constexpr int kAuthorityRefreshKind = 7;
constexpr const char* kNetworkId = "logos.test";
constexpr const char* kPalaceId = "palace-1";
constexpr const char* kRoomId = "atrium";
constexpr const char* kInjectorUserId = "acceptance-injector";
constexpr const char* kExpectedRoomTopic =
    "/logos-palace/1/room-84ec4fea04b3f315a64b2d294cc0ad31/proto";
constexpr const char* kPrivateSeedHex =
    "f5e5767cf153319517630f226876b86c"
    "8160cc583bc013744c6bf255f5cc0ee5";
constexpr const char* kExpectedPublicKeyHex =
    "278117fc144c72340f67d0f2316e8386"
    "ceffbf2b2428c9c51fef7c597f1d426e";
constexpr std::array<const char*, 7> kScenarioOrder{{
    "presence-hello",
    "replay",
    "expired",
    "bad-signature",
    "wrong-epoch",
    "out-of-bounds-motion",
    "presence-bye-cleanup",
}};

struct Envelope {
    std::int64_t roomEpoch = kRoomEpoch;
    int kind = kAuthorityRefreshKind;
    std::uint64_t senderSequence = 0;
    std::int64_t createdAt = 0;
    std::int64_t expiresAt = 0;
    std::string payload;
    std::string signature;
};

struct PkeyDeleter {
    void operator()(EVP_PKEY* value) const { EVP_PKEY_free(value); }
};

struct MdContextDeleter {
    void operator()(EVP_MD_CTX* value) const { EVP_MD_CTX_free(value); }
};

struct PrivateSeed {
    std::array<unsigned char, 32> bytes{};

    ~PrivateSeed()
    {
        OPENSSL_cleanse(bytes.data(), bytes.size());
    }
};

std::string lengthEncoded(const std::string& value)
{
    return std::to_string(value.size()) + ":" + value;
}

std::string canonicalEnvelope(const Envelope& envelope)
{
    return "version=1"
        ";network=" + lengthEncoded(kNetworkId)
        + ";palace=" + lengthEncoded(kPalaceId)
        + ";room=" + lengthEncoded(kRoomId)
        + ";epoch=" + std::to_string(envelope.roomEpoch)
        + ";kind=" + std::to_string(envelope.kind)
        + ";sender=" + lengthEncoded(kInjectorUserId)
        + ";key_epoch=" + std::to_string(kInjectorKeyEpoch)
        + ";sequence=" + std::to_string(envelope.senderSequence)
        + ";created=" + std::to_string(envelope.createdAt)
        + ";expires=" + std::to_string(envelope.expiresAt)
        + ";payload=" + lengthEncoded(envelope.payload);
}

std::string encodeEnvelope(const Envelope& envelope)
{
    return canonicalEnvelope(envelope)
        + ";signature=" + lengthEncoded(envelope.signature);
}

bool decodeHex(const char* encoded,
               unsigned char* output,
               std::size_t outputLength)
{
    const std::string_view value(encoded);
    if (value.size() != outputLength * 2U)
        return false;
    const auto nibble = [](unsigned char character,
                           unsigned char& decoded) {
        if (character >= '0' && character <= '9') {
            decoded = character - '0';
        } else if (character >= 'a' && character <= 'f') {
            decoded = character - 'a' + 10U;
        } else {
            return false;
        }
        return true;
    };
    for (std::size_t index = 0; index < outputLength; ++index) {
        unsigned char high = 0;
        unsigned char low = 0;
        if (!nibble(static_cast<unsigned char>(value[index * 2U]), high)
            || !nibble(
                static_cast<unsigned char>(value[index * 2U + 1U]),
                low)) {
            return false;
        }
        output[index] =
            static_cast<unsigned char>((high << 4U) | low);
    }
    return true;
}

std::string encodeHex(const unsigned char* bytes, std::size_t length)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(length * 2U);
    for (std::size_t index = 0; index < length; ++index) {
        encoded.push_back(kDigits[bytes[index] >> 4U]);
        encoded.push_back(kDigits[bytes[index] & 0x0fU]);
    }
    return encoded;
}

std::string signEnvelope(const Envelope& envelope)
{
    PrivateSeed seed;
    if (!decodeHex(kPrivateSeedHex, seed.bytes.data(), seed.bytes.size()))
        return {};

    const std::unique_ptr<EVP_PKEY, PkeyDeleter> key(
        EVP_PKEY_new_raw_private_key(
            EVP_PKEY_ED25519, nullptr, seed.bytes.data(), seed.bytes.size()));
    if (!key)
        return {};

    std::array<unsigned char, 32> publicKey{};
    std::size_t publicKeyLength = publicKey.size();
    if (EVP_PKEY_get_raw_public_key(
            key.get(), publicKey.data(), &publicKeyLength) != 1
        || publicKeyLength != publicKey.size()
        || encodeHex(publicKey.data(), publicKey.size())
            != kExpectedPublicKeyHex) {
        return {};
    }

    const std::unique_ptr<EVP_MD_CTX, MdContextDeleter> context(
        EVP_MD_CTX_new());
    if (!context
        || EVP_DigestSignInit(
            context.get(), nullptr, nullptr, nullptr, key.get()) != 1) {
        return {};
    }

    const std::string canonical = canonicalEnvelope(envelope);
    std::array<unsigned char, 64> signature{};
    std::size_t signatureLength = signature.size();
    if (EVP_DigestSign(
            context.get(),
            signature.data(),
            &signatureLength,
            reinterpret_cast<const unsigned char*>(canonical.data()),
            canonical.size()) != 1
        || signatureLength != signature.size()) {
        return {};
    }
    return encodeHex(signature.data(), signatureLength);
}

std::string sha256Hex(const std::string& value)
{
    return QCryptographicHash::hash(
               QByteArray(value.data(), static_cast<qsizetype>(value.size())),
               QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}

std::string roomTopic()
{
    const std::string preimage =
        "logos-palace-room-v1|" + lengthEncoded(kNetworkId)
        + lengthEncoded(kPalaceId)
        + lengthEncoded(kRoomId)
        + std::to_string(kRoomEpoch);
    return "/logos-palace/1/room-" + sha256Hex(preimage).substr(0, 32U)
        + "/proto";
}

std::int64_t nowSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string normalizedState(const std::string& value)
{
    std::string normalized;
    normalized.reserve(value.size());
    for (const unsigned char character : value) {
        if (std::isalnum(character) != 0)
            normalized.push_back(
                static_cast<char>(std::tolower(character)));
    }
    return normalized;
}

bool validLoopbackEntryNode(const QString& entryNode,
                            std::uint16_t localPort)
{
    if (entryNode.isEmpty() || entryNode.toUtf8().size() > 512)
        return false;
    static const QRegularExpression expression(
        QStringLiteral(
            "^/ip4/127\\.0\\.0\\.1/tcp/([0-9]{1,5})/"
            "p2p/([1-9A-HJ-NP-Za-km-z]{20,128})$"));
    const QRegularExpressionMatch match = expression.match(entryNode);
    if (!match.hasMatch())
        return false;
    bool converted = false;
    const unsigned int entryPort =
        match.captured(1).toUInt(&converted);
    return converted && entryPort > 0U && entryPort <= 65535U
        && entryPort != localPort;
}

std::optional<Envelope> scenarioEnvelope(const std::string& scenario)
{
    const std::int64_t now = nowSeconds();
    if (now <= 2
        || now > std::numeric_limits<std::int64_t>::max() - 120) {
        return std::nullopt;
    }

    Envelope envelope;
    if (scenario == "presence-hello") {
        envelope.kind = kPresenceHelloKind;
        envelope.senderSequence = 1;
        envelope.createdAt = now;
        envelope.expiresAt = now + 120;
        envelope.payload = "Acceptance Injector";
    } else if (scenario == "replay") {
        envelope.kind = kPresenceHelloKind;
        envelope.senderSequence = 1;
        envelope.createdAt = now;
        envelope.expiresAt = now + 119;
        envelope.payload = "Acceptance Injector";
    } else if (scenario == "expired") {
        envelope.senderSequence = 2;
        envelope.createdAt = now - 2;
        envelope.expiresAt = now - 1;
    } else if (scenario == "bad-signature") {
        envelope.senderSequence = 2;
        envelope.createdAt = now;
        envelope.expiresAt = now + 120;
    } else if (scenario == "wrong-epoch") {
        envelope.roomEpoch = 8;
        envelope.senderSequence = 2;
        envelope.createdAt = now;
        envelope.expiresAt = now + 120;
    } else if (scenario == "out-of-bounds-motion") {
        envelope.kind = kMotionKind;
        envelope.senderSequence = 2;
        envelope.createdAt = now;
        envelope.expiresAt = now + 10;
        envelope.payload = "x=10001;y=50";
    } else if (scenario == "presence-bye-cleanup") {
        envelope.kind = kPresenceByeKind;
        envelope.senderSequence = 2;
        envelope.createdAt = now;
        envelope.expiresAt = now + 120;
    } else {
        return std::nullopt;
    }

    envelope.signature = signEnvelope(envelope);
    if (envelope.signature.empty())
        return std::nullopt;
    if (scenario == "bad-signature")
        envelope.signature.assign(128U, '0');
    return envelope;
}

QJsonValue evidenceValue(const QString& value)
{
    QJsonParseError error;
    const QJsonDocument parsed = QJsonDocument::fromJson(
        value.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError) {
        if (parsed.isArray())
            return parsed.array();
        if (parsed.isObject())
            return parsed.object();
    }
    return value;
}

} // namespace

void PalaceDeliveryAcceptanceBackend::onContextReady()
{
    if (roomTopic() != kExpectedRoomTopic) {
        fail(QStringLiteral("topic-contract"));
        return;
    }

    const bool nodeStarted = modules().delivery_module.onNodeStarted(
        [this](bool succeeded, const QString&, int) {
            enqueue({EventKind::NodeStarted, succeeded, {}});
        });
    const bool nodeStopped = modules().delivery_module.onNodeStopped(
        [this](bool succeeded, const QString&, int) {
            enqueue({EventKind::NodeStopped, succeeded, {}});
        });
    const bool connection =
        modules().delivery_module.onConnectionStateChanged(
            [this](const QString& state, int) {
                enqueue(
                    {EventKind::ConnectionChanged, false,
                     state.toStdString()});
            });
    const bool sent = modules().delivery_module.onMessageSent(
        [this](const QString& requestId, const QString&, int) {
            enqueue(
                {EventKind::MessageSent, false,
                 requestId.toStdString()});
        });
    const bool propagated =
        modules().delivery_module.onMessagePropagated(
            [this](const QString& requestId, const QString&, int) {
                enqueue(
                    {EventKind::MessagePropagated, false,
                     requestId.toStdString()});
            });
    const bool error = modules().delivery_module.onMessageError(
        [this](const QString& requestId,
               const QString&,
               const QString&,
               int) {
            enqueue(
                {EventKind::MessageError, false,
                 requestId.toStdString()});
        });

    m_callbacksRegistered =
        nodeStarted && nodeStopped && connection && sent && propagated
        && error;
    if (!m_callbacksRegistered) {
        fail(QStringLiteral("callback-registration"));
        return;
    }

    setStatus(QStringLiteral("state=ready;callbacks=1"));
    m_pumpTimer = new QTimer(this);
    m_pumpTimer->setInterval(100);
    connect(m_pumpTimer, &QTimer::timeout, this, [this]() { pump(); });
    m_pumpTimer->start();
}

QString PalaceDeliveryAcceptanceBackend::start(QString entryNode,
                                                qint64 tcpPort)
{
    if (!isContextReady() || !m_callbacksRegistered || m_failed)
        return rememberReceipt(
            QStringLiteral("rejected=acceptance-not-ready"));
    if (m_startAttempted)
        return rememberReceipt(
            QStringLiteral("rejected=acceptance-start-once"));
    if (tcpPort < 1024 || tcpPort > 65535
        || !validLoopbackEntryNode(
            entryNode, static_cast<std::uint16_t>(tcpPort))) {
        return rememberReceipt(
            QStringLiteral("rejected=acceptance-local-config"));
    }
    m_startAttempted = true;

    QJsonObject config;
    config.insert(QStringLiteral("mode"), QStringLiteral("Edge"));
    config.insert(QStringLiteral("relay"), true);
    config.insert(QStringLiteral("store"), false);
    config.insert(QStringLiteral("clusterId"), 4242);
    config.insert(QStringLiteral("numShardsInNetwork"), 1);
    config.insert(
        QStringLiteral("entryNodes"), QJsonArray{entryNode});
    config.insert(
        QStringLiteral("tcpPort"), static_cast<int>(tcpPort));
    config.insert(
        QStringLiteral("nat"), QStringLiteral("extip:127.0.0.1"));
    config.insert(
        QStringLiteral("listenAddress"), QStringLiteral("127.0.0.1"));
    config.insert(
        QStringLiteral("nodekey"),
        QStringLiteral(
            "4444444444444444444444444444444444444444444444444444444444444444"));
    config.insert(QStringLiteral("discv5Discovery"), false);
    config.insert(QStringLiteral("websocketSupport"), false);
    config.insert(QStringLiteral("quicSupport"), false);
    config.insert(QStringLiteral("logLevel"), QStringLiteral("WARN"));
    const QString encoded = QString::fromUtf8(
        QJsonDocument(config).toJson(QJsonDocument::Compact));

    const LogosResult created =
        modules().delivery_module.createNode(encoded);
    if (!created.success) {
        fail(QStringLiteral("create-node"));
        return rememberReceipt(
            QStringLiteral("rejected=acceptance-create-node"));
    }
    m_nodeCreated = true;
    setStatus(QStringLiteral("state=starting;callbacks=1"));

    const LogosResult started = modules().delivery_module.start();
    if (!started.success) {
        fail(QStringLiteral("start-node"));
        return rememberReceipt(
            QStringLiteral("rejected=acceptance-start-node"));
    }
    refreshNodeEvidence();
    return rememberReceipt(QStringLiteral("ok;state=starting"));
}

QString PalaceDeliveryAcceptanceBackend::inject(QString scenario)
{
    if (!isContextReady() || m_failed || !m_subscribed)
        return rememberReceipt(
            QStringLiteral("rejected=acceptance-not-online"));
    if (scenario.toUtf8().size() > 64
        || m_nextScenario >= kScenarioOrder.size()) {
        return rememberReceipt(
            QStringLiteral("rejected=acceptance-scenario-complete"));
    }

    const std::string requested = scenario.toStdString();
    const std::string expected = kScenarioOrder[m_nextScenario];
    if (requested != expected) {
        return rememberReceipt(
            QStringLiteral("rejected=acceptance-scenario-order;expected=")
            + QString::fromStdString(expected));
    }

    const std::optional<Envelope> envelope =
        scenarioEnvelope(requested);
    if (!envelope.has_value()) {
        fail(QStringLiteral("envelope-construction"));
        return rememberReceipt(
            QStringLiteral("rejected=acceptance-envelope"));
    }
    const std::string wire = encodeEnvelope(*envelope);
    if (wire.empty() || wire.size() > 4096U) {
        fail(QStringLiteral("wire-size"));
        return rememberReceipt(
            QStringLiteral("rejected=acceptance-wire-size"));
    }

    const LogosResult sent = modules().delivery_module.send(
        QString::fromLatin1(kExpectedRoomTopic),
        QByteArray(wire.data(), static_cast<qsizetype>(wire.size())));
    const std::string moduleRequestId =
        sent.success ? sent.value.toString().toStdString() : std::string{};
    if (!sent.success || moduleRequestId.empty()
        || moduleRequestId.size() > kMaximumRequestIdBytes) {
        fail(QStringLiteral("send"));
        return rememberReceipt(
            QStringLiteral("rejected=acceptance-send"));
    }
    if (!m_scenarioByRequest.emplace(moduleRequestId, requested).second) {
        fail(QStringLiteral("request-correlation"));
        return rememberReceipt(
            QStringLiteral("rejected=acceptance-request-correlation"));
    }

    ++m_nextScenario;
    return rememberReceipt(
        QStringLiteral("ok;scenario=")
        + QString::fromStdString(requested)
        + QStringLiteral(";request_sha256=")
        + QString::fromStdString(sha256Hex(moduleRequestId))
        + QStringLiteral(";wire_sha256=")
        + QString::fromStdString(sha256Hex(wire)));
}

void PalaceDeliveryAcceptanceBackend::enqueue(QueuedEvent event)
{
    if (event.value.size() > kMaximumRequestIdBytes) {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        m_eventOverflow = true;
        return;
    }
    std::lock_guard<std::mutex> lock(m_eventMutex);
    if (m_events.size() >= kMaximumQueuedEvents) {
        m_eventOverflow = true;
        return;
    }
    m_events.push_back(std::move(event));
}

void PalaceDeliveryAcceptanceBackend::pump()
{
    std::deque<QueuedEvent> events;
    bool overflow = false;
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        overflow = m_eventOverflow;
        m_eventOverflow = false;
        events.swap(m_events);
    }
    if (overflow) {
        fail(QStringLiteral("event-overflow"));
        return;
    }
    for (const QueuedEvent& event : events)
        applyEvent(event);

    if (++m_evidenceTicks >= 10U) {
        m_evidenceTicks = 0U;
        refreshNodeEvidence();
    }
}

void PalaceDeliveryAcceptanceBackend::applyEvent(
    const QueuedEvent& event)
{
    if (m_failed)
        return;
    if (event.kind == EventKind::NodeStarted) {
        if (!event.succeeded) {
            fail(QStringLiteral("node-start-callback"));
            return;
        }
        m_nodeStarted = true;
        setStatus(QStringLiteral("state=waiting_connection;callbacks=1"));
        trySubscribe();
        return;
    }
    if (event.kind == EventKind::NodeStopped) {
        m_nodeStarted = false;
        m_connected = false;
        m_subscribed = false;
        fail(QStringLiteral("node-stopped"));
        return;
    }
    if (event.kind == EventKind::ConnectionChanged) {
        const std::string state = normalizedState(event.value);
        if (state == "connected"
            || state == "partiallyconnected"
            || state == "fullyconnected") {
            m_connected = true;
            trySubscribe();
        } else if (state == "connecting") {
            m_connected = false;
            setStatus(
                QStringLiteral("state=waiting_connection;callbacks=1"));
        } else if (state == "disconnected"
                   || state == "notconnected") {
            m_connected = false;
            m_subscribed = false;
            setStatus(
                QStringLiteral("state=waiting_connection;callbacks=1"));
        }
        return;
    }

    const auto correlated =
        m_scenarioByRequest.find(event.value);
    if (correlated == m_scenarioByRequest.end())
        return;
    const QString scenario =
        QString::fromStdString(correlated->second);
    if (event.kind == EventKind::MessageError) {
        m_scenarioByRequest.erase(correlated);
        fail(QStringLiteral("message-error-") + scenario);
    } else if (event.kind == EventKind::MessageSent) {
        m_scenarioByRequest.erase(correlated);
        setStatus(
            QStringLiteral("state=online;callbacks=1;last_sent=")
            + scenario);
    } else if (event.kind == EventKind::MessagePropagated) {
        m_scenarioByRequest.erase(correlated);
        setStatus(
            QStringLiteral("state=online;callbacks=1;last_propagated=")
            + scenario);
    }
}

void PalaceDeliveryAcceptanceBackend::trySubscribe()
{
    if (m_failed || m_subscribed || m_subscribePending
        || !m_nodeStarted || !m_connected) {
        return;
    }
    m_subscribePending = true;
    const LogosResult subscribed =
        modules().delivery_module.subscribe(
            QString::fromLatin1(kExpectedRoomTopic));
    m_subscribePending = false;
    if (!subscribed.success) {
        fail(QStringLiteral("subscribe"));
        return;
    }
    m_subscribed = true;
    setStatus(QStringLiteral("state=online;callbacks=1"));
    refreshNodeEvidence();
}

void PalaceDeliveryAcceptanceBackend::refreshNodeEvidence()
{
    if (!isContextReady() || !m_nodeCreated)
        return;
    const LogosResult addresses =
        modules().delivery_module.getNodeInfo(
            QStringLiteral("MyMultiaddresses"));
    const LogosResult peerId =
        modules().delivery_module.getNodeInfo(
            QStringLiteral("MyPeerId"));
    const LogosResult connected =
        modules().delivery_module.getConnectedPeersInfo();
    const QString addressValue = addresses.value.toString();
    const QString peerIdValue = peerId.value.toString();
    const QString connectedValue = connected.value.toString();
    if (!addresses.success || !peerId.success || !connected.success
        || addressValue.toUtf8().size() > 16384
        || peerIdValue.toUtf8().size() > 1024
        || connectedValue.toUtf8().size() > 65536) {
        setNodeEvidence(
            QStringLiteral(
                "{\"success\":false,\"reason\":\"node-evidence-unavailable\"}"));
        return;
    }

    QJsonObject evidence;
    evidence.insert(QStringLiteral("success"), true);
    evidence.insert(
        QStringLiteral("multiaddresses"),
        evidenceValue(addressValue));
    evidence.insert(
        QStringLiteral("peerId"), evidenceValue(peerIdValue));
    evidence.insert(
        QStringLiteral("connectedPeers"),
        evidenceValue(connectedValue));
    const QByteArray encoded =
        QJsonDocument(evidence).toJson(QJsonDocument::Compact);
    if (encoded.size() > 90 * 1024) {
        setNodeEvidence(
            QStringLiteral(
                "{\"success\":false,\"reason\":\"node-evidence-too-large\"}"));
        return;
    }
    setNodeEvidence(QString::fromUtf8(encoded));
}

QString PalaceDeliveryAcceptanceBackend::rememberReceipt(
    const QString& value)
{
    setReceipt(value);
    return value;
}

void PalaceDeliveryAcceptanceBackend::fail(const QString& reason)
{
    m_failed = true;
    m_subscribed = false;
    setStatus(
        QStringLiteral("state=failed;callbacks=")
        + (m_callbacksRegistered
               ? QStringLiteral("1")
               : QStringLiteral("0"))
        + QStringLiteral(";reason=") + reason);
}
