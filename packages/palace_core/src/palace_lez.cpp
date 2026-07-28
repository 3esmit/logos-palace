#include "palace_lez.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace palace {
namespace {

constexpr std::size_t kAccountIdHexLength = 64U;
constexpr std::size_t kMaxIdentifierLength = 64U;
constexpr std::size_t kMaxCidLength = 128U;

bool isHex(const std::string& value, std::size_t expectedLength)
{
    return value.size() == expectedLength
        && std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return std::isxdigit(character) != 0;
        });
}

bool isIdentifier(const std::string& value)
{
    return !value.empty() && value.size() <= kMaxIdentifierLength
        && std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return std::isalnum(character) != 0 || character == '_' || character == '-';
        });
}

bool isCid(const std::string& value)
{
    return value.size() >= 2U && value.size() <= kMaxCidLength
        && std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return std::isalnum(character) != 0;
        });
}

bool decodeHex32(const std::string& value, std::array<unsigned char, 32>& output)
{
    if (!isHex(value, kAccountIdHexLength))
        return false;
    const QByteArray bytes = QByteArray::fromHex(QByteArray::fromStdString(value));
    if (bytes.size() != static_cast<int>(output.size()))
        return false;
    std::copy(bytes.begin(), bytes.end(), output.begin());
    return true;
}

void writeBytes(std::vector<std::uint32_t>& words, const unsigned char* bytes, std::size_t size)
{
    for (std::size_t offset = 0; offset < size; offset += sizeof(std::uint32_t)) {
        std::uint32_t word = 0;
        for (std::size_t byte = 0; byte < sizeof(std::uint32_t) && offset + byte < size; ++byte)
            word |= static_cast<std::uint32_t>(bytes[offset + byte]) << (byte * 8U);
        words.push_back(word);
    }
}

void writeAccountId(std::vector<std::uint32_t>& words, const std::array<unsigned char, 32>& accountId)
{
    // serde serializes [u8; 32] as 32 independently padded u8 words.
    for (const unsigned char byte : accountId)
        words.push_back(byte);
}

void writeString(std::vector<std::uint32_t>& words, const std::string& value)
{
    words.push_back(static_cast<std::uint32_t>(value.size()));
    writeBytes(words, reinterpret_cast<const unsigned char*>(value.data()), value.size());
}

void writeU64(std::vector<std::uint32_t>& words, std::uint64_t value)
{
    words.push_back(static_cast<std::uint32_t>(value & 0xffffffffULL));
    words.push_back(static_cast<std::uint32_t>(value >> 32U));
}

bool hasSubmissionShape(const QJsonObject& result)
{
    const QJsonValue success = result.value(QStringLiteral("success"));
    const QJsonValue hash = result.value(QStringLiteral("tx_hash"));
    const QJsonValue error = result.value(QStringLiteral("error"));
    return success.isBool() && hash.isString()
        && (error.isUndefined() || error.isNull() || error.isString());
}

} // namespace

bool PalaceLezCodec::parseOrderedActionId(const std::string& value, std::uint64_t& output)
{
    if (value.empty() || value == "0" || (value.size() > 1U && value.front() == '0')
        || !std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return character >= '0' && character <= '9';
        })) {
        return false;
    }

    std::uint64_t parsed = 0;
    const auto [cursor, error] =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc() || cursor != value.data() + value.size())
        return false;
    output = parsed;
    return true;
}

PalaceLezWireInstruction PalaceLezCodec::encodeApply(const PalaceLezSubmitRequestV1& request)
{
    if (request.orderedActionId == 0)
        return {false, "invalid-ordered-action-id", {}};

    std::array<unsigned char, 32> stateAccountId{};
    std::array<unsigned char, 32> callerAccountId{};
    std::array<unsigned char, 32> programId{};
    if (!decodeHex32(request.stateAccountIdHex, stateAccountId)
        || !decodeHex32(request.callerAccountIdHex, callerAccountId)
        || !decodeHex32(request.programIdHex, programId)) {
        return {false, "invalid-account-or-program-id", {}};
    }

    std::array<unsigned char, 32> subject{};
    std::array<unsigned char, 32> deliveryKey{};
    const auto needsSubject = request.instruction.kind == PalaceLezInstructionKind::BindDeliveryKey
        || request.instruction.kind == PalaceLezInstructionKind::DelegateModerator
        || request.instruction.kind == PalaceLezInstructionKind::RevokeModerator
        || request.instruction.kind == PalaceLezInstructionKind::BanUser;
    if (needsSubject && !decodeHex32(request.instruction.subjectAccountIdHex, subject))
        return {false, "invalid-subject-account-id", {}};
    if (request.instruction.kind == PalaceLezInstructionKind::BindDeliveryKey) {
        if (!decodeHex32(request.instruction.deliveryKeyHex, deliveryKey)
            || std::all_of(deliveryKey.begin(), deliveryKey.end(), [](unsigned char byte) {
                return byte == 0;
            })) {
            return {false, "invalid-delivery-key", {}};
        }
    }

    const auto requireRoom = [&]() { return isIdentifier(request.instruction.roomId); };
    switch (request.instruction.kind) {
    case PalaceLezInstructionKind::BindDeliveryKey:
    case PalaceLezInstructionKind::DelegateModerator:
    case PalaceLezInstructionKind::RevokeModerator: break;
    case PalaceLezInstructionKind::BanUser:
        if (!requireRoom())
            return {false, "invalid-room-id", {}};
        break;
    case PalaceLezInstructionKind::BanAsset:
        if (!requireRoom())
            return {false, "invalid-room-id", {}};
        if (!isCid(request.instruction.cid))
            return {false, "invalid-cid", {}};
        break;
    case PalaceLezInstructionKind::SetRoomLocked:
        if (!requireRoom())
            return {false, "invalid-room-id", {}};
        break;
    case PalaceLezInstructionKind::PublishManifest:
        if (!isCid(request.instruction.cid))
            return {false, "invalid-cid", {}};
        break;
    case PalaceLezInstructionKind::SetSharedSpotRevision:
        if (!requireRoom() || !isIdentifier(request.instruction.spotId))
            return {false, "invalid-room-or-spot-id", {}};
        if (request.instruction.revision == 0)
            return {false, "invalid-spot-revision", {}};
        break;
    }

    std::vector<std::uint32_t> words;
    words.reserve(128U);
    words.push_back(1U); // GuestInstruction::Apply
    writeU64(words, request.orderedActionId);
    words.push_back(static_cast<std::uint32_t>(request.instruction.kind));
    switch (request.instruction.kind) {
    case PalaceLezInstructionKind::BindDeliveryKey:
        writeAccountId(words, subject);
        writeAccountId(words, deliveryKey);
        writeU64(words, request.instruction.keyEpoch);
        break;
    case PalaceLezInstructionKind::DelegateModerator:
    case PalaceLezInstructionKind::RevokeModerator:
        writeAccountId(words, subject);
        break;
    case PalaceLezInstructionKind::BanUser:
        writeAccountId(words, subject);
        writeString(words, request.instruction.roomId);
        break;
    case PalaceLezInstructionKind::BanAsset:
        writeString(words, request.instruction.cid);
        writeString(words, request.instruction.roomId);
        break;
    case PalaceLezInstructionKind::SetRoomLocked:
        writeString(words, request.instruction.roomId);
        words.push_back(request.instruction.locked ? 1U : 0U);
        break;
    case PalaceLezInstructionKind::PublishManifest:
        writeString(words, request.instruction.cid);
        break;
    case PalaceLezInstructionKind::SetSharedSpotRevision:
        writeString(words, request.instruction.roomId);
        writeString(words, request.instruction.spotId);
        writeU64(words, request.instruction.revision);
        break;
    }
    return {true, "accepted", std::move(words)};
}

PalaceLezSubmissionResult PalaceLezCodec::parseSubmissionResult(const std::string& responseJson)
{
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(responseJson));
    if (!document.isObject() || !hasSubmissionShape(document.object()))
        return {false, "invalid-submit-response", {}};

    const QJsonObject result = document.object();
    const QJsonValue error = result.value(QStringLiteral("error"));
    const bool hasError = error.isString() && !error.toString().isEmpty();
    const std::string hash = result.value(QStringLiteral("tx_hash")).toString().toStdString();
    if (!result.value(QStringLiteral("success")).toBool() || hasError)
        return {false, "module-rejected", {}};
    if (!isHex(hash, kAccountIdHexLength))
        return {false, "invalid-transaction-hash", {}};
    return {true, "accepted", hash};
}

} // namespace palace
