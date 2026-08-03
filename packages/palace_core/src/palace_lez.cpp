#include "palace_lez.h"

#include "palace_lez_explorer_finality.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <type_traits>
#include <tuple>
#include <utility>

#include <openssl/evp.h>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

namespace palace {
namespace {

constexpr std::uint16_t kSchemaVersion = 3U;
constexpr std::size_t kMaxTitleBytes = 64U;
constexpr std::size_t kMaxDisplayNameBytes = 48U;
constexpr std::size_t kMaxCidBytes = 128U;
constexpr std::size_t kMaxSharedKeyBytes = 32U;
constexpr std::size_t kMaxSharedValueBytes = 512U;
constexpr std::uint32_t kMaxUsers = 64U;
constexpr std::uint32_t kMaxGrants = 64U;
constexpr std::uint32_t kMaxBans = 128U;
constexpr std::uint32_t kMaxSharedStates = 128U;
constexpr std::uint32_t kAllCapabilities = 0x1fU;
constexpr std::size_t kMaxInstructionWords = 1024U;
constexpr std::size_t kMaxTransactionAccounts = 8U;
constexpr std::size_t kMaxIndexerTransactions = 256U;
constexpr std::size_t kMaxCoordinatorTransactions = 128U;
constexpr std::size_t kMaxJsonBytes = 2U * 1024U * 1024U;
constexpr std::size_t kMaxAccountDataBytes = 4096U;
constexpr std::uint16_t kSnapshotVersion = 1U;
constexpr std::array<std::uint8_t, 4> kSnapshotMagic{{'P', 'L', 'Z', 'C'}};
constexpr std::size_t kMaxCoordinatorSnapshotBytes =
    8U
    + kMaxCoordinatorTransactions
        * (1U + 8U + 8U + 4U * 32U + 2U + kMaxInstructionWords * 4U + 1U
            + kMaxTransactionAccounts * (32U + 1U));
constexpr char kBase58Alphabet[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

template<class... Visitors>
struct Overloaded : Visitors... {
    using Visitors::operator()...;
};
template<class... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

bool isLowerHex(const std::string& value, const std::size_t length)
{
    return value.size() == length
        && std::all_of(value.begin(), value.end(), [](const unsigned char character) {
            return (character >= '0' && character <= '9')
                || (character >= 'a' && character <= 'f');
        });
}

bool parseHexBytes(
    const std::string& value,
    std::vector<std::uint8_t>& output,
    const std::size_t maximumBytes)
{
    if (value.size() % 2U != 0U || value.size() / 2U > maximumBytes
        || !isLowerHex(value, value.size())) {
        return false;
    }
    output.clear();
    output.reserve(value.size() / 2U);
    const auto nibble = [](const char character) -> std::uint8_t {
        if (character >= '0' && character <= '9')
            return static_cast<std::uint8_t>(character - '0');
        return static_cast<std::uint8_t>(character - 'a' + 10);
    };
    for (std::size_t index = 0; index < value.size(); index += 2U) {
        output.push_back(static_cast<std::uint8_t>(
            (nibble(value[index]) << 4U) | nibble(value[index + 1U])));
    }
    return true;
}

std::string bytesHex(const std::uint8_t* bytes, const std::size_t size)
{
    constexpr char alphabet[] = "0123456789abcdef";
    std::string output(size * 2U, '0');
    for (std::size_t index = 0; index < size; ++index) {
        output[index * 2U] = alphabet[bytes[index] >> 4U];
        output[index * 2U + 1U] = alphabet[bytes[index] & 0x0fU];
    }
    return output;
}

bool digestSha256(
    const std::uint8_t* bytes,
    const std::size_t size,
    PalaceLezBytes32& digest)
{
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (context == nullptr)
        return false;
    unsigned int digestSize = 0;
    const bool accepted = EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1
        && EVP_DigestUpdate(context, bytes, size) == 1
        && EVP_DigestFinal_ex(context, digest.data(), &digestSize) == 1
        && digestSize == digest.size();
    EVP_MD_CTX_free(context);
    return accepted;
}

bool isNonzero(const PalaceLezBytes32& value)
{
    return std::any_of(value.begin(), value.end(), [](const std::uint8_t byte) {
        return byte != 0U;
    });
}

bool decodeUtf8CodePoint(
    const std::string& value,
    std::size_t& cursor,
    std::uint32_t& codePoint)
{
    if (cursor >= value.size())
        return false;
    const auto first = static_cast<std::uint8_t>(value[cursor]);
    if (first <= 0x7fU) {
        codePoint = first;
        ++cursor;
        return true;
    }

    std::size_t continuationCount = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xc2U && first <= 0xdfU) {
        continuationCount = 1U;
        codePoint = first & 0x1fU;
        minimum = 0x80U;
    } else if (first >= 0xe0U && first <= 0xefU) {
        continuationCount = 2U;
        codePoint = first & 0x0fU;
        minimum = 0x800U;
    } else if (first >= 0xf0U && first <= 0xf4U) {
        continuationCount = 3U;
        codePoint = first & 0x07U;
        minimum = 0x10000U;
    } else {
        return false;
    }
    if (continuationCount > value.size() - cursor - 1U)
        return false;
    for (std::size_t index = 0; index < continuationCount; ++index) {
        const auto continuation = static_cast<std::uint8_t>(value[cursor + index + 1U]);
        if ((continuation & 0xc0U) != 0x80U)
            return false;
        codePoint = (codePoint << 6U) | (continuation & 0x3fU);
    }
    cursor += continuationCount + 1U;
    return codePoint >= minimum && codePoint <= 0x10ffffU
        && !(codePoint >= 0xd800U && codePoint <= 0xdfffU);
}

bool isValidUtf8(const std::string& value, const bool rejectControls)
{
    std::size_t cursor = 0;
    while (cursor < value.size()) {
        std::uint32_t codePoint = 0;
        if (!decodeUtf8CodePoint(value, cursor, codePoint))
            return false;
        if (rejectControls
            && (codePoint <= 0x1fU || (codePoint >= 0x7fU && codePoint <= 0x9fU))) {
            return false;
        }
    }
    return true;
}

bool isText(const std::string& value, const std::size_t maximumBytes)
{
    return !value.empty() && value.size() <= maximumBytes && isValidUtf8(value, true);
}

bool isAsciiAlphanumeric(const unsigned char character)
{
    return (character >= '0' && character <= '9')
        || (character >= 'A' && character <= 'Z')
        || (character >= 'a' && character <= 'z');
}

bool isCid(const std::string& value)
{
    return value.size() >= 4U && value.size() <= kMaxCidBytes
        && std::all_of(value.begin(), value.end(), [](const unsigned char character) {
            return isAsciiAlphanumeric(character);
        });
}

bool isAsciiIdentifier(const std::string& value, const std::size_t maximumBytes)
{
    return !value.empty() && value.size() <= maximumBytes
        && std::all_of(value.begin(), value.end(), [](const unsigned char character) {
            return isAsciiAlphanumeric(character) || character == '_' || character == '-';
        });
}

bool validScope(const PalaceLezScopeV3& scope)
{
    return scope.kind == PalaceLezScopeKindV3::Palace
        || (scope.kind == PalaceLezScopeKindV3::Room && isNonzero(scope.roomId));
}

bool validProfileInput(const PalaceLezUserProfileInputV3& profile)
{
    return isText(profile.displayName, kMaxDisplayNameBytes)
        && isNonzero(profile.deliveryKey) && profile.keyEpoch != 0U
        && (!profile.avatarManifestCid.has_value() || isCid(*profile.avatarManifestCid));
}

bool validRoomConfig(const PalaceLezRoomConfigInputV3& room)
{
    return isText(room.title, kMaxTitleBytes) && isCid(room.manifestCid)
        && isCid(room.scriptBundleCid)
        && room.vmProfile == PalaceLezVmProfileV3::IptScraeMvpV1;
}

std::uint64_t instructionActionId(const PalaceLezInstructionV3& instruction)
{
    return std::visit(
        [](const auto& payload) -> std::uint64_t {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, PalaceLezInitializeV3>)
                return 0U;
            else
                return payload.orderedActionId;
        },
        instruction.payload);
}

PalaceLezInstructionKindV3 instructionKind(const PalaceLezInstructionV3& instruction)
{
    return static_cast<PalaceLezInstructionKindV3>(instruction.payload.index());
}

std::string validateInstruction(const PalaceLezInstructionV3& instruction)
{
    return std::visit(
        Overloaded{
            [](const PalaceLezInitializeV3& value) -> std::string {
                if (!isNonzero(value.palaceId) || !isText(value.title, kMaxTitleBytes)
                    || !isCid(value.activeManifestCid) || !validProfileInput(value.ownerProfile)
                    || !isNonzero(value.ownerGrantId) || !isNonzero(value.entryRoomId)
                    || !isNonzero(value.secondaryRoomId)
                    || value.entryRoomId == value.secondaryRoomId
                    || !validRoomConfig(value.entryRoom)
                    || !validRoomConfig(value.secondaryRoom)) {
                    return "invalid-instruction-payload";
                }
                return {};
            },
            [](const PalaceLezRegisterUserV3& value) -> std::string {
                return value.orderedActionId != 0U && validProfileInput(value.profile)
                    ? std::string{}
                    : "invalid-instruction-payload";
            },
            [](const PalaceLezUpdateUserProfileV3& value) -> std::string {
                return value.orderedActionId != 0U
                        && isText(value.displayName, kMaxDisplayNameBytes)
                        && (!value.avatarManifestCid.has_value()
                            || isCid(*value.avatarManifestCid))
                    ? std::string{}
                    : "invalid-instruction-payload";
            },
            [](const PalaceLezRotateDeliveryKeyV3& value) -> std::string {
                return value.orderedActionId != 0U && isNonzero(value.deliveryKey)
                        && value.keyEpoch != 0U
                    ? std::string{}
                    : "invalid-instruction-payload";
            },
            [](const PalaceLezPublishManifestV3& value) -> std::string {
                return value.orderedActionId != 0U && isCid(value.cid)
                    ? std::string{}
                    : "invalid-instruction-payload";
            },
            [](const PalaceLezUpdateRoomV3& value) -> std::string {
                return value.orderedActionId != 0U && isNonzero(value.grantId)
                        && isNonzero(value.roomId) && validRoomConfig(value.config)
                    ? std::string{}
                    : "invalid-instruction-payload";
            },
            [](const PalaceLezGrantCapabilityV3& value) -> std::string {
                return value.orderedActionId != 0U && isNonzero(value.grantId)
                        && isNonzero(value.subjectUserId) && validScope(value.scope)
                        && value.capabilities != 0U
                        && (value.capabilities & ~kAllCapabilities) == 0U
                        && value.validThroughActionId >= value.orderedActionId
                    ? std::string{}
                    : "invalid-instruction-payload";
            },
            [](const PalaceLezRevokeCapabilityV3& value) -> std::string {
                return value.orderedActionId != 0U && isNonzero(value.grantId)
                    ? std::string{}
                    : "invalid-instruction-payload";
            },
            [](const PalaceLezSetRoomLockedV3& value) -> std::string {
                return value.orderedActionId != 0U && isNonzero(value.grantId)
                        && isNonzero(value.roomId)
                    ? std::string{}
                    : "invalid-instruction-payload";
            },
            [](const PalaceLezCreateUserBanV3& value) -> std::string {
                return value.orderedActionId != 0U && isNonzero(value.grantId)
                        && isNonzero(value.banId) && isNonzero(value.subjectUserId)
                        && validScope(value.scope)
                    ? std::string{}
                    : "invalid-instruction-payload";
            },
            [](const PalaceLezCreateAssetBanV3& value) -> std::string {
                return value.orderedActionId != 0U && isNonzero(value.grantId)
                        && isNonzero(value.banId) && isCid(value.cid)
                        && validScope(value.scope)
                    ? std::string{}
                    : "invalid-instruction-payload";
            },
            [](const PalaceLezSetBanActiveV3& value) -> std::string {
                return value.orderedActionId != 0U && isNonzero(value.grantId)
                        && isNonzero(value.banId)
                    ? std::string{}
                    : "invalid-instruction-payload";
            },
            [](const PalaceLezCreateSharedStateV3& value) -> std::string {
                return value.orderedActionId != 0U && isNonzero(value.grantId)
                        && isNonzero(value.sharedStateId) && isNonzero(value.roomId)
                        && isAsciiIdentifier(value.key, kMaxSharedKeyBytes)
                        && value.value.size() <= kMaxSharedValueBytes
                        && isNonzero(value.stateRoot)
                    ? std::string{}
                    : "invalid-instruction-payload";
            },
            [](const PalaceLezUpdateSharedStateV3& value) -> std::string {
                return value.orderedActionId != 0U && isNonzero(value.grantId)
                        && isNonzero(value.sharedStateId) && isNonzero(value.roomId)
                        && value.stateRevision != 0U
                        && value.value.size() <= kMaxSharedValueBytes
                        && isNonzero(value.stateRoot)
                    ? std::string{}
                    : "invalid-instruction-payload";
            }},
        instruction.payload);
}

class WordWriter {
public:
    void u32(const std::uint32_t value) { words.push_back(value); }

    void u64(const std::uint64_t value)
    {
        words.push_back(static_cast<std::uint32_t>(value & 0xffffffffULL));
        words.push_back(static_cast<std::uint32_t>(value >> 32U));
    }

    void fixedBytes(const PalaceLezBytes32& value)
    {
        for (const std::uint8_t byte : value)
            words.push_back(byte);
    }

    void string(const std::string& value)
    {
        words.push_back(static_cast<std::uint32_t>(value.size()));
        for (std::size_t offset = 0; offset < value.size(); offset += 4U) {
            std::uint32_t word = 0U;
            for (std::size_t byte = 0; byte < 4U && offset + byte < value.size(); ++byte) {
                word |= static_cast<std::uint32_t>(
                            static_cast<std::uint8_t>(value[offset + byte]))
                    << (byte * 8U);
            }
            words.push_back(word);
        }
    }

    void optionalString(const std::optional<std::string>& value)
    {
        words.push_back(value.has_value() ? 1U : 0U);
        if (value.has_value())
            string(*value);
    }

    void byteVector(const std::vector<std::uint8_t>& value)
    {
        // serde's Vec<T> path writes the sequence length and each u8 as one word.
        words.push_back(static_cast<std::uint32_t>(value.size()));
        for (const std::uint8_t byte : value)
            words.push_back(byte);
    }

    void scope(const PalaceLezScopeV3& value)
    {
        words.push_back(static_cast<std::uint32_t>(value.kind));
        if (value.kind == PalaceLezScopeKindV3::Room)
            fixedBytes(value.roomId);
    }

    void profile(const PalaceLezUserProfileInputV3& value)
    {
        string(value.displayName);
        fixedBytes(value.deliveryKey);
        u64(value.keyEpoch);
        optionalString(value.avatarManifestCid);
    }

    void room(const PalaceLezRoomConfigInputV3& value)
    {
        string(value.title);
        string(value.manifestCid);
        string(value.scriptBundleCid);
        u32(static_cast<std::uint32_t>(value.vmProfile));
    }

    std::vector<std::uint32_t> words;
};

class WordReader {
public:
    explicit WordReader(const std::vector<std::uint32_t>& source)
        : words(source)
    {
    }

    bool u32(std::uint32_t& output)
    {
        if (cursor >= words.size())
            return fail();
        output = words[cursor++];
        return true;
    }

    bool u64(std::uint64_t& output)
    {
        std::uint32_t low = 0;
        std::uint32_t high = 0;
        if (!u32(low) || !u32(high))
            return false;
        output = static_cast<std::uint64_t>(low)
            | (static_cast<std::uint64_t>(high) << 32U);
        return true;
    }

    bool boolean(bool& output)
    {
        std::uint32_t value = 0;
        if (!u32(value) || value > 1U)
            return fail();
        output = value == 1U;
        return true;
    }

    bool fixedBytes(PalaceLezBytes32& output)
    {
        for (std::uint8_t& byte : output) {
            std::uint32_t word = 0;
            if (!u32(word) || word > 0xffU)
                return fail();
            byte = static_cast<std::uint8_t>(word);
        }
        return true;
    }

    bool string(std::string& output, const std::size_t maximumBytes)
    {
        std::uint32_t size = 0;
        if (!u32(size) || size > maximumBytes)
            return fail();
        const std::size_t wordCount = (static_cast<std::size_t>(size) + 3U) / 4U;
        if (wordCount > words.size() - cursor)
            return fail();
        output.assign(size, '\0');
        for (std::size_t index = 0; index < wordCount; ++index) {
            const std::uint32_t word = words[cursor++];
            for (std::size_t byte = 0; byte < 4U; ++byte) {
                const std::size_t destination = index * 4U + byte;
                const std::uint8_t value =
                    static_cast<std::uint8_t>((word >> (byte * 8U)) & 0xffU);
                if (destination < size)
                    output[destination] = static_cast<char>(value);
                else if (value != 0U)
                    return fail();
            }
        }
        return isValidUtf8(output, false) || fail();
    }

    bool optionalString(std::optional<std::string>& output, const std::size_t maximumBytes)
    {
        std::uint32_t tag = 0;
        if (!u32(tag) || tag > 1U)
            return fail();
        if (tag == 0U) {
            output.reset();
            return true;
        }
        std::string value;
        if (!string(value, maximumBytes))
            return false;
        output = std::move(value);
        return true;
    }

    bool byteVector(std::vector<std::uint8_t>& output, const std::size_t maximumBytes)
    {
        std::uint32_t size = 0;
        if (!u32(size) || size > maximumBytes || size > words.size() - cursor)
            return fail();
        output.clear();
        output.reserve(size);
        for (std::uint32_t index = 0; index < size; ++index) {
            std::uint32_t word = 0;
            if (!u32(word) || word > 0xffU)
                return fail();
            output.push_back(static_cast<std::uint8_t>(word));
        }
        return true;
    }

    bool scope(PalaceLezScopeV3& output)
    {
        std::uint32_t tag = 0;
        if (!u32(tag) || tag > 1U)
            return fail();
        output.kind = static_cast<PalaceLezScopeKindV3>(tag);
        output.roomId.fill(0U);
        return tag == 0U || fixedBytes(output.roomId);
    }

    bool profile(PalaceLezUserProfileInputV3& output)
    {
        return string(output.displayName, kMaxDisplayNameBytes)
            && fixedBytes(output.deliveryKey) && u64(output.keyEpoch)
            && optionalString(output.avatarManifestCid, kMaxCidBytes);
    }

    bool room(PalaceLezRoomConfigInputV3& output)
    {
        std::uint32_t vmProfile = 0;
        if (!string(output.title, kMaxTitleBytes)
            || !string(output.manifestCid, kMaxCidBytes)
            || !string(output.scriptBundleCid, kMaxCidBytes) || !u32(vmProfile)
            || vmProfile > static_cast<std::uint32_t>(PalaceLezVmProfileV3::IptScraeMvpV1)) {
            return fail();
        }
        output.vmProfile = static_cast<PalaceLezVmProfileV3>(vmProfile);
        return true;
    }

    bool finished() const { return !failed && cursor == words.size(); }
    bool failedState() const { return failed; }

private:
    bool fail()
    {
        failed = true;
        return false;
    }

    const std::vector<std::uint32_t>& words;
    std::size_t cursor = 0;
    bool failed = false;
};

void encodePayload(WordWriter& writer, const PalaceLezInstructionV3& instruction)
{
    writer.u32(static_cast<std::uint32_t>(instructionKind(instruction)));
    std::visit(
        Overloaded{
            [&](const PalaceLezInitializeV3& value) {
                writer.fixedBytes(value.palaceId);
                writer.string(value.title);
                writer.string(value.activeManifestCid);
                writer.profile(value.ownerProfile);
                writer.fixedBytes(value.ownerGrantId);
                writer.fixedBytes(value.entryRoomId);
                writer.room(value.entryRoom);
                writer.fixedBytes(value.secondaryRoomId);
                writer.room(value.secondaryRoom);
            },
            [&](const PalaceLezRegisterUserV3& value) {
                writer.u64(value.orderedActionId);
                writer.profile(value.profile);
            },
            [&](const PalaceLezUpdateUserProfileV3& value) {
                writer.u64(value.orderedActionId);
                writer.string(value.displayName);
                writer.optionalString(value.avatarManifestCid);
            },
            [&](const PalaceLezRotateDeliveryKeyV3& value) {
                writer.u64(value.orderedActionId);
                writer.fixedBytes(value.deliveryKey);
                writer.u64(value.keyEpoch);
            },
            [&](const PalaceLezPublishManifestV3& value) {
                writer.u64(value.orderedActionId);
                writer.string(value.cid);
            },
            [&](const PalaceLezUpdateRoomV3& value) {
                writer.u64(value.orderedActionId);
                writer.fixedBytes(value.grantId);
                writer.fixedBytes(value.roomId);
                writer.room(value.config);
            },
            [&](const PalaceLezGrantCapabilityV3& value) {
                writer.u64(value.orderedActionId);
                writer.fixedBytes(value.grantId);
                writer.fixedBytes(value.subjectUserId);
                writer.scope(value.scope);
                writer.u32(value.capabilities);
                writer.u32(value.delegable ? 1U : 0U);
                writer.u64(value.validThroughActionId);
            },
            [&](const PalaceLezRevokeCapabilityV3& value) {
                writer.u64(value.orderedActionId);
                writer.fixedBytes(value.grantId);
            },
            [&](const PalaceLezSetRoomLockedV3& value) {
                writer.u64(value.orderedActionId);
                writer.fixedBytes(value.grantId);
                writer.fixedBytes(value.roomId);
                writer.u32(value.locked ? 1U : 0U);
            },
            [&](const PalaceLezCreateUserBanV3& value) {
                writer.u64(value.orderedActionId);
                writer.fixedBytes(value.grantId);
                writer.fixedBytes(value.banId);
                writer.fixedBytes(value.subjectUserId);
                writer.scope(value.scope);
            },
            [&](const PalaceLezCreateAssetBanV3& value) {
                writer.u64(value.orderedActionId);
                writer.fixedBytes(value.grantId);
                writer.fixedBytes(value.banId);
                writer.string(value.cid);
                writer.scope(value.scope);
            },
            [&](const PalaceLezSetBanActiveV3& value) {
                writer.u64(value.orderedActionId);
                writer.fixedBytes(value.grantId);
                writer.fixedBytes(value.banId);
                writer.u32(value.active ? 1U : 0U);
            },
            [&](const PalaceLezCreateSharedStateV3& value) {
                writer.u64(value.orderedActionId);
                writer.fixedBytes(value.grantId);
                writer.fixedBytes(value.sharedStateId);
                writer.fixedBytes(value.roomId);
                writer.string(value.key);
                writer.byteVector(value.value);
                writer.fixedBytes(value.stateRoot);
            },
            [&](const PalaceLezUpdateSharedStateV3& value) {
                writer.u64(value.orderedActionId);
                writer.fixedBytes(value.grantId);
                writer.fixedBytes(value.sharedStateId);
                writer.fixedBytes(value.roomId);
                writer.u64(value.stateRevision);
                writer.byteVector(value.value);
                writer.fixedBytes(value.stateRoot);
            }},
        instruction.payload);
}

bool decodePayload(
    WordReader& reader,
    const std::uint32_t kind,
    PalaceLezInstructionV3& instruction)
{
    bool accepted = false;
    switch (static_cast<PalaceLezInstructionKindV3>(kind)) {
    case PalaceLezInstructionKindV3::Initialize: {
        PalaceLezInitializeV3 value;
        accepted = reader.fixedBytes(value.palaceId)
            && reader.string(value.title, kMaxTitleBytes)
            && reader.string(value.activeManifestCid, kMaxCidBytes)
            && reader.profile(value.ownerProfile) && reader.fixedBytes(value.ownerGrantId)
            && reader.fixedBytes(value.entryRoomId) && reader.room(value.entryRoom)
            && reader.fixedBytes(value.secondaryRoomId) && reader.room(value.secondaryRoom);
        instruction.payload = std::move(value);
        break;
    }
    case PalaceLezInstructionKindV3::RegisterUser: {
        PalaceLezRegisterUserV3 value;
        accepted = reader.u64(value.orderedActionId) && reader.profile(value.profile);
        instruction.payload = std::move(value);
        break;
    }
    case PalaceLezInstructionKindV3::UpdateUserProfile: {
        PalaceLezUpdateUserProfileV3 value;
        accepted = reader.u64(value.orderedActionId)
            && reader.string(value.displayName, kMaxDisplayNameBytes)
            && reader.optionalString(value.avatarManifestCid, kMaxCidBytes);
        instruction.payload = std::move(value);
        break;
    }
    case PalaceLezInstructionKindV3::RotateDeliveryKey: {
        PalaceLezRotateDeliveryKeyV3 value;
        accepted = reader.u64(value.orderedActionId) && reader.fixedBytes(value.deliveryKey)
            && reader.u64(value.keyEpoch);
        instruction.payload = std::move(value);
        break;
    }
    case PalaceLezInstructionKindV3::PublishManifest: {
        PalaceLezPublishManifestV3 value;
        accepted =
            reader.u64(value.orderedActionId) && reader.string(value.cid, kMaxCidBytes);
        instruction.payload = std::move(value);
        break;
    }
    case PalaceLezInstructionKindV3::UpdateRoom: {
        PalaceLezUpdateRoomV3 value;
        accepted = reader.u64(value.orderedActionId) && reader.fixedBytes(value.grantId)
            && reader.fixedBytes(value.roomId) && reader.room(value.config);
        instruction.payload = std::move(value);
        break;
    }
    case PalaceLezInstructionKindV3::GrantCapability: {
        PalaceLezGrantCapabilityV3 value;
        accepted = reader.u64(value.orderedActionId) && reader.fixedBytes(value.grantId)
            && reader.fixedBytes(value.subjectUserId) && reader.scope(value.scope)
            && reader.u32(value.capabilities) && reader.boolean(value.delegable)
            && reader.u64(value.validThroughActionId);
        instruction.payload = std::move(value);
        break;
    }
    case PalaceLezInstructionKindV3::RevokeCapability: {
        PalaceLezRevokeCapabilityV3 value;
        accepted = reader.u64(value.orderedActionId) && reader.fixedBytes(value.grantId);
        instruction.payload = std::move(value);
        break;
    }
    case PalaceLezInstructionKindV3::SetRoomLocked: {
        PalaceLezSetRoomLockedV3 value;
        accepted = reader.u64(value.orderedActionId) && reader.fixedBytes(value.grantId)
            && reader.fixedBytes(value.roomId) && reader.boolean(value.locked);
        instruction.payload = std::move(value);
        break;
    }
    case PalaceLezInstructionKindV3::CreateUserBan: {
        PalaceLezCreateUserBanV3 value;
        accepted = reader.u64(value.orderedActionId) && reader.fixedBytes(value.grantId)
            && reader.fixedBytes(value.banId) && reader.fixedBytes(value.subjectUserId)
            && reader.scope(value.scope);
        instruction.payload = std::move(value);
        break;
    }
    case PalaceLezInstructionKindV3::CreateAssetBan: {
        PalaceLezCreateAssetBanV3 value;
        accepted = reader.u64(value.orderedActionId) && reader.fixedBytes(value.grantId)
            && reader.fixedBytes(value.banId) && reader.string(value.cid, kMaxCidBytes)
            && reader.scope(value.scope);
        instruction.payload = std::move(value);
        break;
    }
    case PalaceLezInstructionKindV3::SetBanActive: {
        PalaceLezSetBanActiveV3 value;
        accepted = reader.u64(value.orderedActionId) && reader.fixedBytes(value.grantId)
            && reader.fixedBytes(value.banId) && reader.boolean(value.active);
        instruction.payload = std::move(value);
        break;
    }
    case PalaceLezInstructionKindV3::CreateSharedState: {
        PalaceLezCreateSharedStateV3 value;
        accepted = reader.u64(value.orderedActionId) && reader.fixedBytes(value.grantId)
            && reader.fixedBytes(value.sharedStateId) && reader.fixedBytes(value.roomId)
            && reader.string(value.key, kMaxSharedKeyBytes)
            && reader.byteVector(value.value, kMaxSharedValueBytes)
            && reader.fixedBytes(value.stateRoot);
        instruction.payload = std::move(value);
        break;
    }
    case PalaceLezInstructionKindV3::UpdateSharedState: {
        PalaceLezUpdateSharedStateV3 value;
        accepted = reader.u64(value.orderedActionId) && reader.fixedBytes(value.grantId)
            && reader.fixedBytes(value.sharedStateId) && reader.fixedBytes(value.roomId)
            && reader.u64(value.stateRevision)
            && reader.byteVector(value.value, kMaxSharedValueBytes)
            && reader.fixedBytes(value.stateRoot);
        instruction.payload = std::move(value);
        break;
    }
    default: return false;
    }
    return accepted;
}

PalaceLezBytes32 literalSeed(const std::string& value)
{
    PalaceLezBytes32 seed{};
    if (value.size() <= seed.size())
        std::copy(value.begin(), value.end(), seed.begin());
    return seed;
}

bool derivePda(
    const PalaceLezBytes32& programId,
    const std::vector<PalaceLezBytes32>& resolvedSeeds,
    PalaceLezBytes32& output)
{
    if (resolvedSeeds.empty())
        return false;
    PalaceLezBytes32 combined{};
    if (resolvedSeeds.size() == 1U) {
        combined = resolvedSeeds.front();
    } else {
        std::vector<std::uint8_t> seedBytes;
        seedBytes.reserve(resolvedSeeds.size() * 32U);
        for (const PalaceLezBytes32& seed : resolvedSeeds)
            seedBytes.insert(seedBytes.end(), seed.begin(), seed.end());
        if (!digestSha256(seedBytes.data(), seedBytes.size(), combined))
            return false;
    }

    std::array<std::uint8_t, 96> input{};
    constexpr char prefix[] = "/LEE/v0.2/AccountId/PDA/";
    static_assert(sizeof(prefix) - 1U == 24U, "PDA prefix changed");
    std::copy(prefix, prefix + 24U, input.begin());
    std::copy(programId.begin(), programId.end(), input.begin() + 32U);
    std::copy(combined.begin(), combined.end(), input.begin() + 64U);
    return digestSha256(input.data(), input.size(), output);
}

bool canonicalProgramId(const std::string& value, PalaceLezBytes32& output)
{
    return PalaceLezCodec::parseBytes32Hex(value, output) && isNonzero(output);
}

bool canonicalAccountId(const std::string& value, PalaceLezBytes32& output)
{
    return PalaceLezCodec::parseBytes32Hex(value, output) && isNonzero(output);
}

std::string childPda(
    const std::string& programIdHex,
    const std::string& tag,
    const std::string& rootHex,
    const PalaceLezBytes32& stableId)
{
    return PalaceLezCodec::deriveRecordPda(programIdHex, tag, rootHex, stableId);
}

bool exactKeys(const QJsonObject& object, const std::initializer_list<const char*> keys)
{
    if (object.size() != static_cast<int>(keys.size()))
        return false;
    for (const char* key : keys) {
        if (!object.contains(QString::fromLatin1(key)))
            return false;
    }
    return true;
}

void skipJsonWhitespace(const std::string& value, std::size_t& cursor)
{
    while (cursor < value.size()
           && (value[cursor] == ' ' || value[cursor] == '\t'
               || value[cursor] == '\n' || value[cursor] == '\r')) {
        ++cursor;
    }
}

bool parseJsonStringToken(
    const std::string& value,
    std::size_t& cursor,
    std::string& output)
{
    if (cursor >= value.size() || value[cursor] != '"')
        return false;
    const std::size_t begin = cursor++;
    bool escaped = false;
    while (cursor < value.size()) {
        const unsigned char character =
            static_cast<unsigned char>(value[cursor]);
        if (character < 0x20U)
            return false;
        if (escaped) {
            escaped = false;
            ++cursor;
            continue;
        }
        if (character == '\\') {
            escaped = true;
            ++cursor;
            continue;
        }
        if (character != '"') {
            ++cursor;
            continue;
        }

        ++cursor;
        QByteArray wrapper("[");
        wrapper.append(value.data() + begin, static_cast<qsizetype>(cursor - begin));
        wrapper.append(']');
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(wrapper, &error);
        if (error.error != QJsonParseError::NoError || !document.isArray()) {
            return false;
        }
        const QJsonArray array = document.array();
        if (array.size() != 1 || !array.at(0).isString())
            return false;
        output = array.at(0).toString().toStdString();
        return true;
    }
    return false;
}

bool parseExactStringObject(
    const std::string& value,
    std::vector<std::pair<std::string, std::string>>& fields,
    bool& duplicate)
{
    fields.clear();
    duplicate = false;
    std::size_t cursor = 0U;
    skipJsonWhitespace(value, cursor);
    if (cursor >= value.size() || value[cursor++] != '{')
        return false;
    skipJsonWhitespace(value, cursor);
    if (cursor < value.size() && value[cursor] == '}') {
        ++cursor;
        skipJsonWhitespace(value, cursor);
        return cursor == value.size();
    }

    std::set<std::string> keys;
    while (cursor < value.size()) {
        std::string key;
        if (!parseJsonStringToken(value, cursor, key))
            return false;
        if (!keys.insert(key).second) {
            duplicate = true;
            return false;
        }
        skipJsonWhitespace(value, cursor);
        if (cursor >= value.size() || value[cursor++] != ':')
            return false;
        skipJsonWhitespace(value, cursor);
        std::string fieldValue;
        if (!parseJsonStringToken(value, cursor, fieldValue))
            return false;
        fields.emplace_back(std::move(key), std::move(fieldValue));
        skipJsonWhitespace(value, cursor);
        if (cursor >= value.size())
            return false;
        if (value[cursor] == '}') {
            ++cursor;
            skipJsonWhitespace(value, cursor);
            return cursor == value.size();
        }
        if (value[cursor++] != ',')
            return false;
        skipJsonWhitespace(value, cursor);
    }
    return false;
}

bool jsonUnsigned(const QJsonValue& value, const std::uint64_t maximum, std::uint64_t& output)
{
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number
        || number > static_cast<double>(maximum)) {
        return false;
    }
    output = static_cast<std::uint64_t>(number);
    return static_cast<double>(output) == number;
}

bool jsonSafeNonce(const QJsonValue& value, std::string& output)
{
    // Qt represents JSON numbers as doubles. Refuse valid-but-inexact u128
    // values rather than silently comparing a rounded account nonce.
    constexpr std::uint64_t maximumExactJsonInteger = (1ULL << 53U) - 1U;
    std::uint64_t nonce = 0;
    if (!jsonUnsigned(value, maximumExactJsonInteger, nonce))
        return false;
    output = std::to_string(nonce);
    return true;
}

bool canonicalBase64(const std::string& value, const std::size_t expectedBytes)
{
    if (value.size() > ((expectedBytes + 2U) / 3U) * 4U)
        return false;
    const QByteArray encoded = QByteArray::fromStdString(value);
    const QByteArray decoded = QByteArray::fromBase64(encoded);
    return decoded.size() == static_cast<qsizetype>(expectedBytes)
        && decoded.toBase64() == encoded;
}

class BorshReader {
public:
    explicit BorshReader(const std::vector<std::uint8_t>& source)
        : bytes(source)
    {
    }

    bool u8(std::uint8_t& value)
    {
        if (cursor >= bytes.size())
            return fail();
        value = bytes[cursor++];
        return true;
    }

    bool boolean(bool& value)
    {
        std::uint8_t raw = 0;
        if (!u8(raw) || raw > 1U)
            return fail();
        value = raw == 1U;
        return true;
    }

    bool u16(std::uint16_t& value)
    {
        if (bytes.size() - cursor < 2U)
            return fail();
        value = static_cast<std::uint16_t>(bytes[cursor])
            | (static_cast<std::uint16_t>(bytes[cursor + 1U]) << 8U);
        cursor += 2U;
        return true;
    }

    bool u32(std::uint32_t& value)
    {
        if (bytes.size() - cursor < 4U)
            return fail();
        value = static_cast<std::uint32_t>(bytes[cursor])
            | (static_cast<std::uint32_t>(bytes[cursor + 1U]) << 8U)
            | (static_cast<std::uint32_t>(bytes[cursor + 2U]) << 16U)
            | (static_cast<std::uint32_t>(bytes[cursor + 3U]) << 24U);
        cursor += 4U;
        return true;
    }

    bool u64(std::uint64_t& value)
    {
        std::uint32_t low = 0;
        std::uint32_t high = 0;
        if (!u32(low) || !u32(high))
            return false;
        value = static_cast<std::uint64_t>(low)
            | (static_cast<std::uint64_t>(high) << 32U);
        return true;
    }

    bool fixedBytes(PalaceLezBytes32& value)
    {
        if (bytes.size() - cursor < value.size())
            return fail();
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(cursor), value.size(), value.begin());
        cursor += value.size();
        return true;
    }

    bool string(std::string& value, const std::size_t maximumBytes)
    {
        std::uint32_t size = 0;
        if (!u32(size) || size > maximumBytes || size > bytes.size() - cursor)
            return fail();
        value.assign(
            reinterpret_cast<const char*>(bytes.data() + cursor),
            static_cast<std::size_t>(size));
        cursor += size;
        return isValidUtf8(value, false) || fail();
    }

    bool optionalString(std::optional<std::string>& value, const std::size_t maximumBytes)
    {
        std::uint8_t tag = 0;
        if (!u8(tag) || tag > 1U)
            return fail();
        if (tag == 0U) {
            value.reset();
            return true;
        }
        std::string item;
        if (!string(item, maximumBytes))
            return false;
        value = std::move(item);
        return true;
    }

    bool vector(std::vector<std::uint8_t>& value, const std::size_t maximumBytes)
    {
        std::uint32_t size = 0;
        if (!u32(size) || size > maximumBytes || size > bytes.size() - cursor)
            return fail();
        value.assign(
            bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
            bytes.begin() + static_cast<std::ptrdiff_t>(cursor + size));
        cursor += size;
        return true;
    }

    bool scope(PalaceLezScopeV3& value)
    {
        std::uint8_t tag = 0;
        if (!u8(tag) || tag > 1U)
            return fail();
        value.kind = static_cast<PalaceLezScopeKindV3>(tag);
        value.roomId.fill(0U);
        return tag == 0U || fixedBytes(value.roomId);
    }

    bool finished() const { return !failed && cursor == bytes.size(); }

private:
    bool fail()
    {
        failed = true;
        return false;
    }

    const std::vector<std::uint8_t>& bytes;
    std::size_t cursor = 0;
    bool failed = false;
};

bool validRootRecord(const PalaceLezRootRecordV3& value)
{
    return isNonzero(value.palaceId) && isText(value.title, kMaxTitleBytes)
        && isNonzero(value.owner) && isNonzero(value.entryRoomId)
        && isNonzero(value.roomIds[0]) && isNonzero(value.roomIds[1])
        && value.roomIds[0] != value.roomIds[1]
        && (value.entryRoomId == value.roomIds[0] || value.entryRoomId == value.roomIds[1])
        && isCid(value.activeManifestCid) && value.userCount >= 1U
        && value.userCount <= kMaxUsers && value.grantCount >= 1U
        && value.grantCount <= kMaxGrants && value.banCount <= kMaxBans
        && value.sharedStateCount <= kMaxSharedStates;
}

bool decodeRecord(
    const std::vector<std::uint8_t>& data,
    PalaceLezRecordTypeV3& recordType,
    PalaceLezRecordV3& record,
    std::string& reason)
{
    BorshReader reader(data);
    std::uint8_t rawType = 0;
    std::uint16_t schema = 0;
    if (!reader.u8(rawType) || rawType > 5U) {
        reason = "unsupported-record-type";
        return false;
    }
    if (!reader.u16(schema) || schema != kSchemaVersion) {
        reason = "unsupported-schema";
        return false;
    }
    recordType = static_cast<PalaceLezRecordTypeV3>(rawType);
    bool accepted = false;
    switch (recordType) {
    case PalaceLezRecordTypeV3::PalaceRoot: {
        PalaceLezRootRecordV3 value;
        accepted = reader.fixedBytes(value.palaceId)
            && reader.string(value.title, kMaxTitleBytes) && reader.fixedBytes(value.owner)
            && reader.fixedBytes(value.entryRoomId) && reader.fixedBytes(value.roomIds[0])
            && reader.fixedBytes(value.roomIds[1])
            && reader.string(value.activeManifestCid, kMaxCidBytes)
            && reader.u32(value.userCount) && reader.u32(value.grantCount)
            && reader.u32(value.banCount) && reader.u32(value.sharedStateCount)
            && reader.u64(value.revision) && reader.u64(value.lastOrderedActionId)
            && validRootRecord(value);
        record = std::move(value);
        break;
    }
    case PalaceLezRecordTypeV3::UserProfile: {
        PalaceLezUserProfileRecordV3 value;
        accepted = reader.fixedBytes(value.palaceId) && reader.fixedBytes(value.userId)
            && reader.string(value.displayName, kMaxDisplayNameBytes)
            && reader.fixedBytes(value.deliveryKey) && reader.u64(value.keyEpoch)
            && reader.optionalString(value.avatarManifestCid, kMaxCidBytes)
            && reader.u64(value.profileRevision) && isNonzero(value.palaceId)
            && isNonzero(value.userId) && isText(value.displayName, kMaxDisplayNameBytes)
            && isNonzero(value.deliveryKey) && value.keyEpoch != 0U
            && (!value.avatarManifestCid.has_value() || isCid(*value.avatarManifestCid));
        record = std::move(value);
        break;
    }
    case PalaceLezRecordTypeV3::Room: {
        PalaceLezRoomRecordV3 value;
        std::uint8_t vm = 0;
        accepted = reader.fixedBytes(value.palaceId) && reader.fixedBytes(value.roomId)
            && reader.string(value.title, kMaxTitleBytes)
            && reader.string(value.manifestCid, kMaxCidBytes)
            && reader.string(value.scriptBundleCid, kMaxCidBytes) && reader.u8(vm)
            && vm <= 1U && reader.boolean(value.locked) && reader.u64(value.revision);
        value.vmProfile = static_cast<PalaceLezVmProfileV3>(vm);
        accepted = accepted && isNonzero(value.palaceId) && isNonzero(value.roomId)
            && isText(value.title, kMaxTitleBytes) && isCid(value.manifestCid)
            && isCid(value.scriptBundleCid)
            && value.vmProfile == PalaceLezVmProfileV3::IptScraeMvpV1;
        record = std::move(value);
        break;
    }
    case PalaceLezRecordTypeV3::CapabilityGrant: {
        PalaceLezCapabilityGrantRecordV3 value;
        accepted = reader.fixedBytes(value.palaceId) && reader.fixedBytes(value.grantId)
            && reader.fixedBytes(value.subjectUserId) && reader.fixedBytes(value.issuedBy)
            && reader.scope(value.scope) && reader.u32(value.capabilities)
            && reader.boolean(value.delegable) && reader.u64(value.validThroughActionId)
            && reader.boolean(value.revoked) && reader.u64(value.revision)
            && isNonzero(value.palaceId) && isNonzero(value.grantId)
            && isNonzero(value.subjectUserId) && isNonzero(value.issuedBy)
            && validScope(value.scope) && value.capabilities != 0U
            && (value.capabilities & ~kAllCapabilities) == 0U
            && value.validThroughActionId != 0U;
        record = std::move(value);
        break;
    }
    case PalaceLezRecordTypeV3::Ban: {
        PalaceLezBanRecordV3 value;
        std::uint8_t target = 0;
        accepted = reader.fixedBytes(value.palaceId) && reader.fixedBytes(value.banId)
            && reader.u8(target) && target <= 1U;
        value.targetKind = static_cast<PalaceLezBanTargetKindV3>(target);
        if (accepted) {
            accepted = target == 0U
                ? reader.fixedBytes(value.targetUserId)
                : reader.string(value.targetAssetCid, kMaxCidBytes);
        }
        accepted = accepted && reader.fixedBytes(value.issuer) && reader.scope(value.scope)
            && reader.boolean(value.active) && reader.u64(value.revision)
            && isNonzero(value.palaceId) && isNonzero(value.banId)
            && isNonzero(value.issuer) && validScope(value.scope)
            && ((target == 0U && isNonzero(value.targetUserId))
                || (target == 1U && isCid(value.targetAssetCid)));
        record = std::move(value);
        break;
    }
    case PalaceLezRecordTypeV3::RoomSharedState: {
        PalaceLezRoomSharedStateRecordV3 value;
        accepted = reader.fixedBytes(value.palaceId)
            && reader.fixedBytes(value.sharedStateId) && reader.fixedBytes(value.roomId)
            && reader.string(value.key, kMaxSharedKeyBytes)
            && reader.vector(value.value, kMaxSharedValueBytes)
            && reader.fixedBytes(value.stateRoot) && reader.u64(value.revision)
            && reader.u64(value.lastOrderedActionId) && isNonzero(value.palaceId)
            && isNonzero(value.sharedStateId) && isNonzero(value.roomId)
            && isAsciiIdentifier(value.key, kMaxSharedKeyBytes)
            && isNonzero(value.stateRoot) && value.revision != 0U
            && value.lastOrderedActionId != 0U
            && value.revision <= value.lastOrderedActionId;
        record = std::move(value);
        break;
    }
    }
    if (!accepted) {
        reason = "invalid-record";
        return false;
    }
    if (!reader.finished()) {
        reason = "trailing-record-data";
        return false;
    }
    return true;
}

bool samePlan(
    const PalaceLezTransactionPlanV3& left,
    const PalaceLezTransactionPlanV3& right)
{
    return left.accepted && right.accepted && left.programIdHex == right.programIdHex
        && left.rootAccountIdHex == right.rootAccountIdHex
        && left.accountIdsHex == right.accountIdsHex
        && left.signingRequirements == right.signingRequirements
        && left.instructionWords == right.instructionWords;
}

bool planMatchesIndexer(
    const PalaceLezTransactionPlanV3& plan,
    const PalaceLezIndexerTransaction& transaction)
{
    if (!plan.accepted || transaction.programIdHex != plan.programIdHex
        || transaction.signatureCount != 1U
        || transaction.instructionWords != plan.instructionWords
        || transaction.accounts.size() != plan.accountIdsHex.size()) {
        return false;
    }
    for (std::size_t index = 0; index < transaction.accounts.size(); ++index) {
        if (transaction.accounts[index].accountIdHex != plan.accountIdsHex[index])
            return false;
    }
    return true;
}

std::string accountIdBase58FromHex(const std::string& accountIdHex)
{
    PalaceLezBytes32 accountId{};
    return PalaceLezCodec::parseBytes32Hex(accountIdHex, accountId)
        ? PalaceLezCodec::accountIdBase58(accountId)
        : std::string{};
}

bool planMatchesExplorerCertificate(
    const PalaceLezTransactionPlanV3& plan,
    const PalaceLezExplorerFinalityCertificateV1& certificate)
{
    if (!plan.accepted
        || certificate.programIdBase58()
            != accountIdBase58FromHex(plan.programIdHex)
        || certificate.instructionWords() != plan.instructionWords
        || certificate.accountIdsBase58().size()
            != plan.accountIdsHex.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < plan.accountIdsHex.size(); ++index) {
        if (certificate.accountIdsBase58()[index]
            != accountIdBase58FromHex(plan.accountIdsHex[index])) {
            return false;
        }
    }
    return true;
}

bool validExplorerCertificate(
    const PalaceLezExplorerFinalityCertificateV1& certificate)
{
    PalaceLezBytes32 transactionHash{};
    PalaceLezBytes32 programId{};
    PalaceLezBytes32 blockHash{};
    if (certificate.certificateVersion() != 1U
        || !PalaceLezCodec::parseBytes32Hex(
            certificate.transactionHashHex(), transactionHash)
        || !isNonzero(transactionHash)
        || !PalaceLezCodec::parseAccountIdBase58(
            certificate.programIdBase58(), programId)
        || !isNonzero(programId)
        || certificate.accountIdsBase58().empty()
        || certificate.accountIdsBase58().size() > kMaxTransactionAccounts
        || certificate.instructionWords().empty()
        || certificate.instructionWords().size() > kMaxInstructionWords
        || !isLowerHex(certificate.instructionWordsSha256Hex(), 64U)
        || PalaceLezExplorerFinalitySession::instructionWordsSha256Hex(
               certificate.instructionWords())
            != certificate.instructionWordsSha256Hex()
        || certificate.finalizedBlockId() == 0U
        || certificate.finalizedBlockHeight()
            != certificate.finalizedBlockId()
        || !PalaceLezCodec::parseBytes32Hex(
            certificate.finalizedBlockHashHex(), blockHash)
        || !isNonzero(blockHash)
        || certificate.accountEvidence().size()
            != certificate.accountIdsBase58().size()) {
        return false;
    }

    std::set<std::string> distinctAccounts;
    for (std::size_t index = 0U;
         index < certificate.accountIdsBase58().size();
         ++index) {
        PalaceLezBytes32 accountId{};
        PalaceLezBytes32 owner{};
        PalaceLezBytes32 digest{};
        const std::string& expectedAccount =
            certificate.accountIdsBase58()[index];
        const PalaceLezExplorerFinalityAccountEvidenceV1& evidence =
            certificate.accountEvidence()[index];
        if (!PalaceLezCodec::parseAccountIdBase58(
                expectedAccount, accountId)
            || !isNonzero(accountId)
            || !distinctAccounts.insert(expectedAccount).second
            || evidence.accountIdBase58 != expectedAccount
            || !PalaceLezCodec::parseAccountIdBase58(
                evidence.programOwnerBase58, owner)
            || !PalaceLezCodec::parseBytes32Hex(
                evidence.dataSha256Hex, digest)) {
            return false;
        }
    }
    return true;
}

class SnapshotWriter {
public:
    void u8(const std::uint8_t value) { bytes.push_back(value); }

    void u16(const std::uint16_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    }

    void u32(const std::uint32_t value)
    {
        for (std::size_t shift = 0; shift < 32U; shift += 8U)
            bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }

    void u64(const std::uint64_t value)
    {
        for (std::size_t shift = 0; shift < 64U; shift += 8U)
            bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }

    void fixed(const PalaceLezBytes32& value)
    {
        bytes.insert(bytes.end(), value.begin(), value.end());
    }

    void string(const std::string& value)
    {
        u32(static_cast<std::uint32_t>(value.size()));
        bytes.insert(bytes.end(), value.begin(), value.end());
    }

    std::vector<std::uint8_t> bytes;
};

class SnapshotReader {
public:
    explicit SnapshotReader(const std::vector<std::uint8_t>& source)
        : bytes(source)
    {
    }

    bool u8(std::uint8_t& value)
    {
        if (cursor >= bytes.size())
            return false;
        value = bytes[cursor++];
        return true;
    }

    bool u16(std::uint16_t& value)
    {
        std::uint8_t low = 0;
        std::uint8_t high = 0;
        if (!u8(low) || !u8(high))
            return false;
        value = static_cast<std::uint16_t>(low)
            | (static_cast<std::uint16_t>(high) << 8U);
        return true;
    }

    bool u32(std::uint32_t& value)
    {
        value = 0U;
        for (std::size_t shift = 0; shift < 32U; shift += 8U) {
            std::uint8_t byte = 0;
            if (!u8(byte))
                return false;
            value |= static_cast<std::uint32_t>(byte) << shift;
        }
        return true;
    }

    bool u64(std::uint64_t& value)
    {
        value = 0U;
        for (std::size_t shift = 0; shift < 64U; shift += 8U) {
            std::uint8_t byte = 0;
            if (!u8(byte))
                return false;
            value |= static_cast<std::uint64_t>(byte) << shift;
        }
        return true;
    }

    bool fixed(PalaceLezBytes32& value)
    {
        if (bytes.size() - cursor < value.size())
            return false;
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(cursor), value.size(), value.begin());
        cursor += value.size();
        return true;
    }

    bool finished() const { return cursor == bytes.size(); }

private:
    const std::vector<std::uint8_t>& bytes;
    std::size_t cursor = 0;
};

PalaceLezCoordinatorUpdate coordinatorError(
    const std::string& reason,
    const PalaceLezTransactionStage stage = PalaceLezTransactionStage::Submitted)
{
    return {false, false, reason, stage};
}

PalaceLezCoordinatorUpdate coordinatorNoChange(
    const std::string& reason,
    const PalaceLezTransactionStage stage)
{
    return {true, false, reason, stage};
}

PalaceLezCoordinatorUpdate coordinatorChanged(
    const std::string& reason,
    const PalaceLezTransactionStage stage)
{
    return {true, true, reason, stage};
}

bool validFingerprintField(const std::string& value, const std::size_t maximum)
{
    return !value.empty() && value.size() <= maximum
        && std::all_of(value.begin(), value.end(), [](const unsigned char character) {
            return character >= 0x21U && character <= 0x7eU;
        });
}

bool validNetworkFingerprint(const PalaceLezNetworkFingerprint& value)
{
    PalaceLezBytes32 programId{};
    PalaceLezBytes32 bytecodeDigest{};
    return validFingerprintField(value.networkId, 64U)
        && validFingerprintField(value.moduleApiVersion, 32U)
        && isLowerHex(value.moduleRevision, 40U)
        && isLowerHex(value.runtimeRevision, 40U)
        && validFingerprintField(value.publicContractVersion, 32U)
        && isLowerHex(value.publicContractRevision, 40U)
        && canonicalProgramId(value.programIdHex, programId)
        && PalaceLezCodec::parseBytes32Hex(
            value.programBytecodeSha256Hex,
            bytecodeDigest)
        && isNonzero(bytecodeDigest);
}

} // namespace

bool PalaceLezCodec::parseOrderedActionId(const std::string& value, std::uint64_t& output)
{
    if (value.empty() || value == "0" || (value.size() > 1U && value.front() == '0')
        || !std::all_of(value.begin(), value.end(), [](const unsigned char character) {
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

PalaceLezWireInstruction PalaceLezCodec::encodeApply(
    const PalaceLezSubmitRequestV1& request)
{
    if (request.orderedActionId == 0U)
        return {false, "invalid-ordered-action-id"};
    PalaceLezBytes32 state{};
    PalaceLezBytes32 caller{};
    PalaceLezBytes32 program{};
    if (!canonicalAccountId(request.stateAccountIdHex, state)
        || !canonicalAccountId(request.callerAccountIdHex, caller)
        || !canonicalProgramId(request.programIdHex, program)) {
        return {false, "invalid-account-or-program-id"};
    }
    PalaceLezBytes32 subject{};
    PalaceLezBytes32 deliveryKey{};
    const bool needsSubject =
        request.instruction.kind == PalaceLezInstructionKind::BindDeliveryKey
        || request.instruction.kind == PalaceLezInstructionKind::DelegateModerator
        || request.instruction.kind == PalaceLezInstructionKind::RevokeModerator
        || request.instruction.kind == PalaceLezInstructionKind::BanUser;
    if (needsSubject
        && !canonicalAccountId(request.instruction.subjectAccountIdHex, subject)) {
        return {false, "invalid-subject-account-id"};
    }
    if (request.instruction.kind == PalaceLezInstructionKind::BindDeliveryKey
        && (!parseBytes32Hex(request.instruction.deliveryKeyHex, deliveryKey)
            || !isNonzero(deliveryKey))) {
        return {false, "invalid-delivery-key"};
    }
    switch (request.instruction.kind) {
    case PalaceLezInstructionKind::BindDeliveryKey:
    case PalaceLezInstructionKind::DelegateModerator:
    case PalaceLezInstructionKind::RevokeModerator: break;
    case PalaceLezInstructionKind::BanUser:
        if (!isAsciiIdentifier(request.instruction.roomId, 64U))
            return {false, "invalid-room-id"};
        break;
    case PalaceLezInstructionKind::BanAsset:
        if (!isAsciiIdentifier(request.instruction.roomId, 64U))
            return {false, "invalid-room-id"};
        if (!isCid(request.instruction.cid))
            return {false, "invalid-cid"};
        break;
    case PalaceLezInstructionKind::SetRoomLocked:
        if (!isAsciiIdentifier(request.instruction.roomId, 64U))
            return {false, "invalid-room-id"};
        break;
    case PalaceLezInstructionKind::PublishManifest:
        if (!isCid(request.instruction.cid))
            return {false, "invalid-cid"};
        break;
    case PalaceLezInstructionKind::SetSharedSpotRevision:
        if (!isAsciiIdentifier(request.instruction.roomId, 64U)
            || !isAsciiIdentifier(request.instruction.spotId, 64U)) {
            return {false, "invalid-room-or-spot-id"};
        }
        if (request.instruction.revision == 0U)
            return {false, "invalid-spot-revision"};
        break;
    }

    WordWriter writer;
    writer.u32(1U);
    writer.u64(request.orderedActionId);
    writer.u32(static_cast<std::uint32_t>(request.instruction.kind));
    switch (request.instruction.kind) {
    case PalaceLezInstructionKind::BindDeliveryKey:
        writer.fixedBytes(subject);
        writer.fixedBytes(deliveryKey);
        writer.u64(request.instruction.keyEpoch);
        break;
    case PalaceLezInstructionKind::DelegateModerator:
    case PalaceLezInstructionKind::RevokeModerator:
        writer.fixedBytes(subject);
        break;
    case PalaceLezInstructionKind::BanUser:
        writer.fixedBytes(subject);
        writer.string(request.instruction.roomId);
        break;
    case PalaceLezInstructionKind::BanAsset:
        writer.string(request.instruction.cid);
        writer.string(request.instruction.roomId);
        break;
    case PalaceLezInstructionKind::SetRoomLocked:
        writer.string(request.instruction.roomId);
        writer.u32(request.instruction.locked ? 1U : 0U);
        break;
    case PalaceLezInstructionKind::PublishManifest:
        writer.string(request.instruction.cid);
        break;
    case PalaceLezInstructionKind::SetSharedSpotRevision:
        writer.string(request.instruction.roomId);
        writer.string(request.instruction.spotId);
        writer.u64(request.instruction.revision);
        break;
    }
    PalaceLezWireInstruction result;
    result.accepted = true;
    result.reason = "accepted";
    result.words = std::move(writer.words);
    return result;
}

bool PalaceLezCodec::parseBytes32Hex(
    const std::string& value,
    PalaceLezBytes32& output)
{
    if (!isLowerHex(value, 64U))
        return false;
    std::vector<std::uint8_t> bytes;
    if (!parseHexBytes(value, bytes, output.size()) || bytes.size() != output.size())
        return false;
    std::copy(bytes.begin(), bytes.end(), output.begin());
    return true;
}

std::string PalaceLezCodec::bytes32Hex(const PalaceLezBytes32& value)
{
    return bytesHex(value.data(), value.size());
}

std::string PalaceLezCodec::accountIdBase58(const PalaceLezBytes32& value)
{
    std::size_t leadingZeros = 0;
    while (leadingZeros < value.size() && value[leadingZeros] == 0U)
        ++leadingZeros;
    std::vector<std::uint8_t> digits;
    for (const std::uint8_t byte : value) {
        unsigned int carry = byte;
        for (std::uint8_t& digit : digits) {
            carry += static_cast<unsigned int>(digit) * 256U;
            digit = static_cast<std::uint8_t>(carry % 58U);
            carry /= 58U;
        }
        while (carry > 0U) {
            digits.push_back(static_cast<std::uint8_t>(carry % 58U));
            carry /= 58U;
        }
    }
    std::string output(leadingZeros, '1');
    for (auto iterator = digits.rbegin(); iterator != digits.rend(); ++iterator)
        output.push_back(kBase58Alphabet[*iterator]);
    return output;
}

bool PalaceLezCodec::parseAccountIdBase58(
    const std::string& value,
    PalaceLezBytes32& output)
{
    if (value.empty() || value.size() > 44U)
        return false;
    std::size_t leadingZeros = 0;
    while (leadingZeros < value.size() && value[leadingZeros] == '1')
        ++leadingZeros;
    std::vector<std::uint8_t> bytes;
    for (const char character : value) {
        const char* found = std::strchr(kBase58Alphabet, character);
        if (found == nullptr)
            return false;
        unsigned int carry = static_cast<unsigned int>(found - kBase58Alphabet);
        for (std::uint8_t& byte : bytes) {
            carry += static_cast<unsigned int>(byte) * 58U;
            byte = static_cast<std::uint8_t>(carry & 0xffU);
            carry >>= 8U;
        }
        while (carry > 0U) {
            bytes.push_back(static_cast<std::uint8_t>(carry & 0xffU));
            carry >>= 8U;
        }
    }
    if (leadingZeros + bytes.size() != output.size())
        return false;
    output.fill(0U);
    for (std::size_t index = 0; index < bytes.size(); ++index)
        output[output.size() - 1U - index] = bytes[index];
    return accountIdBase58(output) == value;
}

std::string PalaceLezCodec::deriveRootPda(const std::string& programIdHex)
{
    PalaceLezBytes32 programId{};
    PalaceLezBytes32 output{};
    if (!canonicalProgramId(programIdHex, programId)
        || !derivePda(programId, {literalSeed("palace-root")}, output)) {
        return {};
    }
    return bytes32Hex(output);
}

std::string PalaceLezCodec::deriveRecordPda(
    const std::string& programIdHex,
    const std::string& recordTag,
    const std::string& rootAccountIdHex,
    const PalaceLezBytes32& stableId)
{
    static const std::set<std::string> tags{"profile", "room", "grant", "ban", "shared"};
    PalaceLezBytes32 programId{};
    PalaceLezBytes32 root{};
    PalaceLezBytes32 output{};
    if (tags.count(recordTag) != 1U || !canonicalProgramId(programIdHex, programId)
        || !canonicalAccountId(rootAccountIdHex, root) || !isNonzero(stableId)
        || !derivePda(programId, {literalSeed(recordTag), root, stableId}, output)) {
        return {};
    }
    return bytes32Hex(output);
}

PalaceLezWireInstruction PalaceLezCodec::encodeInstruction(
    const PalaceLezInstructionV3& instruction)
{
    const std::string reason = validateInstruction(instruction);
    if (!reason.empty())
        return {false, reason, {}, instruction};
    WordWriter writer;
    writer.words.reserve(256U);
    encodePayload(writer, instruction);
    if (writer.words.empty() || writer.words.size() > kMaxInstructionWords)
        return {false, "instruction-too-large", {}, instruction};
    return {true, "accepted", std::move(writer.words), instruction};
}

PalaceLezWireInstruction PalaceLezCodec::decodeInstruction(
    const std::vector<std::uint32_t>& words)
{
    if (words.empty() || words.size() > kMaxInstructionWords)
        return {false, "invalid-instruction-words", {}, {}};
    WordReader reader(words);
    std::uint32_t kind = 0;
    PalaceLezInstructionV3 instruction;
    if (!reader.u32(kind) || kind > 13U)
        return {false, "unsupported-instruction-kind", {}, {}};
    if (!decodePayload(reader, kind, instruction) || reader.failedState())
        return {false, "invalid-instruction-words", {}, {}};
    if (!reader.finished())
        return {false, "trailing-instruction-words", {}, {}};
    const std::string reason = validateInstruction(instruction);
    if (!reason.empty())
        return {false, reason, {}, {}};
    return {true, "accepted", words, std::move(instruction)};
}

PalaceLezTransactionPlanV3 PalaceLezCodec::buildTransaction(
    const std::string& programIdHex,
    const std::string& signerAccountIdHex,
    const PalaceLezInstructionV3& instruction)
{
    PalaceLezTransactionPlanV3 result;
    result.programIdHex = programIdHex;
    result.instruction = instruction;
    PalaceLezBytes32 programId{};
    PalaceLezBytes32 signer{};
    if (!canonicalProgramId(programIdHex, programId))
        return {false, "invalid-program-id"};
    if (!canonicalAccountId(signerAccountIdHex, signer))
        return {false, "invalid-signer-account-id"};
    const PalaceLezWireInstruction wire = encodeInstruction(instruction);
    if (!wire.accepted)
        return {false, wire.reason};

    const std::string root = deriveRootPda(programIdHex);
    if (root.empty())
        return {false, "pda-derivation-failed"};
    result.rootAccountIdHex = root;
    result.accountIdsHex = {root, signerAccountIdHex};
    result.instructionWords = wire.words;

    const auto add = [&](const std::string& tag, const PalaceLezBytes32& id) -> bool {
        const std::string pda = childPda(programIdHex, tag, root, id);
        if (pda.empty())
            return false;
        result.accountIdsHex.push_back(pda);
        return true;
    };

    const bool accountsAccepted = std::visit(
        Overloaded{
            [&](const PalaceLezInitializeV3& value) {
                return add("profile", signer) && add("room", value.entryRoomId)
                    && add("room", value.secondaryRoomId) && add("grant", value.ownerGrantId);
            },
            [&](const PalaceLezRegisterUserV3&) {
                return add("profile", signer) && add("grant", signer);
            },
            [&](const PalaceLezUpdateUserProfileV3&) { return add("profile", signer); },
            [&](const PalaceLezRotateDeliveryKeyV3&) { return add("profile", signer); },
            [&](const PalaceLezPublishManifestV3&) { return true; },
            [&](const PalaceLezUpdateRoomV3& value) {
                return add("grant", value.grantId) && add("room", value.roomId);
            },
            [&](const PalaceLezGrantCapabilityV3& value) {
                return add("profile", value.subjectUserId) && add("grant", value.grantId);
            },
            [&](const PalaceLezRevokeCapabilityV3& value) {
                return add("grant", value.grantId);
            },
            [&](const PalaceLezSetRoomLockedV3& value) {
                return add("grant", value.grantId) && add("room", value.roomId);
            },
            [&](const PalaceLezCreateUserBanV3& value) {
                return add("grant", value.grantId) && add("profile", value.subjectUserId)
                    && add("ban", value.banId);
            },
            [&](const PalaceLezCreateAssetBanV3& value) {
                return add("grant", value.grantId) && add("ban", value.banId);
            },
            [&](const PalaceLezSetBanActiveV3& value) {
                return add("grant", value.grantId) && add("ban", value.banId);
            },
            [&](const PalaceLezCreateSharedStateV3& value) {
                return add("grant", value.grantId) && add("room", value.roomId)
                    && add("shared", value.sharedStateId);
            },
            [&](const PalaceLezUpdateSharedStateV3& value) {
                return add("grant", value.grantId) && add("room", value.roomId)
                    && add("shared", value.sharedStateId);
            }},
        instruction.payload);
    if (!accountsAccepted || result.accountIdsHex.size() > kMaxTransactionAccounts)
        return {false, "pda-derivation-failed"};
    result.signingRequirements.assign(result.accountIdsHex.size(), false);
    result.signingRequirements[1] = true;
    result.accepted = true;
    result.reason = "accepted";
    return result;
}

std::vector<std::uint8_t> PalaceLezCodec::encodeRootRecord(
    const PalaceLezRootRecordV3& record)
{
    if (!validRootRecord(record))
        return {};
    SnapshotWriter writer;
    writer.u8(static_cast<std::uint8_t>(
        PalaceLezRecordTypeV3::PalaceRoot));
    writer.u16(kSchemaVersion);
    writer.fixed(record.palaceId);
    writer.string(record.title);
    writer.fixed(record.owner);
    writer.fixed(record.entryRoomId);
    writer.fixed(record.roomIds[0]);
    writer.fixed(record.roomIds[1]);
    writer.string(record.activeManifestCid);
    writer.u32(record.userCount);
    writer.u32(record.grantCount);
    writer.u32(record.banCount);
    writer.u32(record.sharedStateCount);
    writer.u64(record.revision);
    writer.u64(record.lastOrderedActionId);
    return std::move(writer.bytes);
}

PalaceLezExpectedRootV3 PalaceLezCodec::expectedInitialRoot(
    const std::string& ownerAccountIdHex,
    const PalaceLezInstructionV3& instruction)
{
    const PalaceLezInitializeV3* initialize =
        std::get_if<PalaceLezInitializeV3>(&instruction.payload);
    PalaceLezBytes32 owner{};
    if (initialize == nullptr
        || !validateInstruction(instruction).empty()
        || !canonicalAccountId(ownerAccountIdHex, owner)) {
        return {false, "invalid-initial-root-input", {}, {}, {}};
    }

    PalaceLezRootRecordV3 record;
    record.palaceId = initialize->palaceId;
    record.title = initialize->title;
    record.owner = owner;
    record.entryRoomId = initialize->entryRoomId;
    record.roomIds = {
        initialize->entryRoomId,
        initialize->secondaryRoomId,
    };
    record.activeManifestCid = initialize->activeManifestCid;
    record.userCount = 1U;
    record.grantCount = 1U;
    record.banCount = 0U;
    record.sharedStateCount = 0U;
    record.revision = 0U;
    record.lastOrderedActionId = 0U;

    std::vector<std::uint8_t> encoded = encodeRootRecord(record);
    const std::string digest = sha256Hex(encoded);
    if (encoded.empty() || digest.empty())
        return {false, "invalid-initial-root-state", {}, {}, {}};
    return {
        true,
        "accepted",
        std::move(record),
        std::move(encoded),
        digest,
    };
}

PalaceLezExpectedRootV3 PalaceLezCodec::expectedAdvancedRoot(
    const PalaceLezRootRecordV3& current,
    const PalaceLezInstructionV3& instruction)
{
    const std::string validation = validateInstruction(instruction);
    const std::uint64_t actionId = instructionActionId(instruction);
    if (!validation.empty()
        || std::holds_alternative<PalaceLezInitializeV3>(
            instruction.payload)
        || !validRootRecord(current)
        || current.revision != current.lastOrderedActionId
        || current.lastOrderedActionId
            == std::numeric_limits<std::uint64_t>::max()
        || current.revision == std::numeric_limits<std::uint64_t>::max()
        || actionId != current.lastOrderedActionId + 1U) {
        return {false, "invalid-root-advance", {}, {}, {}};
    }

    PalaceLezRootRecordV3 next = current;
    ++next.revision;
    next.lastOrderedActionId = actionId;
    bool bounded = true;
    std::visit(
        Overloaded{
            [&](const PalaceLezInitializeV3&) {
                bounded = false;
            },
            [&](const PalaceLezRegisterUserV3&) {
                bounded = next.userCount < kMaxUsers;
                if (bounded)
                    ++next.userCount;
                if (bounded && next.grantCount >= kMaxGrants)
                    bounded = false;
                if (bounded)
                    ++next.grantCount;
            },
            [&](const PalaceLezPublishManifestV3& value) {
                next.activeManifestCid = value.cid;
            },
            [&](const PalaceLezGrantCapabilityV3&) {
                bounded = next.grantCount < kMaxGrants;
                if (bounded)
                    ++next.grantCount;
            },
            [&](const PalaceLezCreateUserBanV3&) {
                bounded = next.banCount < kMaxBans;
                if (bounded)
                    ++next.banCount;
            },
            [&](const PalaceLezCreateAssetBanV3&) {
                bounded = next.banCount < kMaxBans;
                if (bounded)
                    ++next.banCount;
            },
            [&](const PalaceLezCreateSharedStateV3&) {
                bounded =
                    next.sharedStateCount < kMaxSharedStates;
                if (bounded)
                    ++next.sharedStateCount;
            },
            [&](const auto&) {
            }},
        instruction.payload);
    if (!bounded)
        return {false, "root-limit-exceeded", {}, {}, {}};

    std::vector<std::uint8_t> encoded = encodeRootRecord(next);
    const std::string digest = sha256Hex(encoded);
    if (encoded.empty() || digest.empty())
        return {false, "invalid-advanced-root-state", {}, {}, {}};
    return {
        true,
        "accepted",
        std::move(next),
        std::move(encoded),
        digest,
    };
}

PalaceLezSubmissionResult
PalaceLezCodec::parsePublicAccountRegistrationSubmissionResult(
    const std::string& responseJson)
{
    if (responseJson.empty() || responseJson.size() > 64U * 1024U)
        return {false, "invalid-submit-response", {}};
    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray::fromStdString(responseJson), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {false, "invalid-submit-response", {}};
    const QJsonObject object = document.object();
    const bool convenienceResponse =
        exactKeys(object, {"success", "tx_hash", "error"});
    const bool genericResponse =
        exactKeys(object, {"success", "tx_hash", "secrets", "error"});
    if ((!convenienceResponse && !genericResponse)
        || !object.value(QStringLiteral("success")).isBool()
        || !object.value(QStringLiteral("tx_hash")).isString()
        || !object.value(QStringLiteral("error")).isString()) {
        return {false, "invalid-submit-response", {}};
    }
    if (genericResponse) {
        const QJsonValue secrets = object.value(QStringLiteral("secrets"));
        if (!secrets.isArray())
            return {false, "invalid-submit-response", {}};
        if (!secrets.toArray().empty())
            return {false, "unexpected-public-transaction-secrets", {}};
    }
    const bool success = object.value(QStringLiteral("success")).toBool();
    const std::string hash =
        object.value(QStringLiteral("tx_hash")).toString().toStdString();
    const std::string rejection =
        object.value(QStringLiteral("error")).toString().toStdString();
    if (!success || !rejection.empty())
        return {false, "module-rejected", {}};
    if (!isLowerHex(hash, 64U))
        return {false, "invalid-transaction-hash", {}};
    return {true, "accepted", hash};
}

PalaceLezSubmissionResult PalaceLezCodec::parseSubmissionResult(
    const std::string& responseJson)
{
    if (responseJson.empty() || responseJson.size() > 64U * 1024U)
        return {false, "invalid-submit-response", {}};
    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray::fromStdString(responseJson), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {false, "invalid-submit-response", {}};
    const QJsonObject object = document.object();
    if (!exactKeys(object, {"success", "tx_hash", "secrets", "error"})
        || !object.value(QStringLiteral("success")).isBool()
        || !object.value(QStringLiteral("tx_hash")).isString()
        || !object.value(QStringLiteral("secrets")).isArray()
        || !object.value(QStringLiteral("error")).isString()) {
        return {false, "invalid-submit-response", {}};
    }
    const QJsonArray secrets = object.value(QStringLiteral("secrets")).toArray();
    if (!secrets.empty())
        return {false, "unexpected-public-transaction-secrets", {}};
    const bool success = object.value(QStringLiteral("success")).toBool();
    const std::string hash =
        object.value(QStringLiteral("tx_hash")).toString().toStdString();
    const std::string rejection =
        object.value(QStringLiteral("error")).toString().toStdString();
    if (!success || !rejection.empty())
        return {false, "module-rejected", {}};
    if (!isLowerHex(hash, 64U))
        return {false, "invalid-transaction-hash", {}};
    return {true, "accepted", hash};
}

PalaceLezRawAccountV1 PalaceLezCodec::parsePublicAccountSnapshot(
    const std::string& responseJson)
{
    PalaceLezRawAccountV1 result;
    if (responseJson.empty() || responseJson.size() > kMaxAccountDataBytes * 2U + 1024U) {
        result.reason = "invalid-account-response";
        return result;
    }

    std::vector<std::pair<std::string, std::string>> fields;
    bool duplicate = false;
    if (!parseExactStringObject(responseJson, fields, duplicate)) {
        result.reason = duplicate ? "duplicate-json-key" : "invalid-account-response";
        return result;
    }
    const auto field = [&fields](const std::string& key) -> const std::string* {
        const auto found = std::find_if(
            fields.begin(),
            fields.end(),
            [&key](const auto& item) { return item.first == key; });
        return found == fields.end() ? nullptr : &found->second;
    };
    const std::string* programOwner = field("program_owner");
    const std::string* balance = field("balance");
    const std::string* nonce = field("nonce");
    const std::string* dataHex = field("data");
    if (fields.size() != 4U || programOwner == nullptr || balance == nullptr
        || nonce == nullptr || dataHex == nullptr) {
        result.reason = "invalid-account-response";
        return result;
    }
    result.programOwnerHex = *programOwner;
    result.balanceLeHex = *balance;
    result.nonceLeHex = *nonce;
    if (!isLowerHex(result.programOwnerHex, 64U)
        || !isLowerHex(result.balanceLeHex, 32U)
        || !isLowerHex(result.nonceLeHex, 32U)
        || dataHex->size() % 2U != 0U) {
        result.reason = "invalid-account-field";
        return result;
    }
    if (!parseHexBytes(*dataHex, result.data, kMaxAccountDataBytes)) {
        result.reason = "invalid-account-data";
        return result;
    }
    result.dataSha256Hex = sha256Hex(result.data);
    if (result.dataSha256Hex.empty()) {
        result.reason = "digest-failed";
        return result;
    }
    result.accepted = true;
    result.reason = "accepted";
    return result;
}

PalaceLezPublicAccountV3 PalaceLezCodec::decodePublicAccount(
    const std::string& responseJson,
    const std::string& expectedProgramIdHex)
{
    PalaceLezPublicAccountV3 result;
    PalaceLezBytes32 programId{};
    if (!canonicalProgramId(expectedProgramIdHex, programId)) {
        result.reason = "invalid-program-id";
        return result;
    }
    const PalaceLezRawAccountV1 raw = parsePublicAccountSnapshot(responseJson);
    result.programOwnerHex = raw.programOwnerHex;
    result.balanceLeHex = raw.balanceLeHex;
    result.nonceLeHex = raw.nonceLeHex;
    result.dataSha256Hex = raw.dataSha256Hex;
    if (!raw.accepted) {
        result.reason = raw.reason;
        return result;
    }
    if (raw.programOwnerHex != expectedProgramIdHex) {
        result.reason = "account-program-owner-mismatch";
        return result;
    }
    if (raw.data.empty()) {
        result.reason = "invalid-account-data";
        return result;
    }
    std::string recordReason;
    if (!decodeRecord(raw.data, result.recordType, result.record, recordReason)) {
        result.reason = recordReason;
        return result;
    }
    result.accepted = true;
    result.reason = "accepted";
    return result;
}

PalaceLezIndexerParseResult PalaceLezCodec::parseFinalizedTransactions(
    const std::string& responseJson)
{
    PalaceLezIndexerParseResult result;
    if (responseJson.empty() || responseJson.size() > kMaxJsonBytes) {
        result.reason = "indexer-unavailable";
        return result;
    }
    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray::fromStdString(responseJson), &error);
    if (error.error != QJsonParseError::NoError) {
        result.reason = "invalid-indexer-response";
        return result;
    }
    QJsonArray array;
    if (document.isArray()) {
        array = document.array();
    } else if (document.isObject()) {
        const QJsonObject envelope = document.object();
        const QJsonValue id = envelope.value(QStringLiteral("id"));
        if (!exactKeys(envelope, {"jsonrpc", "result", "id"})
            || envelope.value(QStringLiteral("jsonrpc")).toString()
                != QStringLiteral("2.0")
            || !envelope.value(QStringLiteral("result")).isArray()
            || !(id.isNull() || id.isDouble() || id.isString())) {
            result.reason = "invalid-indexer-response";
            return result;
        }
        array = envelope.value(QStringLiteral("result")).toArray();
    } else {
        result.reason = "invalid-indexer-response";
        return result;
    }
    if (array.size() > static_cast<int>(kMaxIndexerTransactions)) {
        result.reason = "indexer-response-too-large";
        return result;
    }
    std::set<std::string> hashes;
    result.transactions.reserve(array.size());
    for (const QJsonValue& transactionValue : array) {
        if (!transactionValue.isObject()) {
            result.reason = "invalid-indexer-transaction";
            return result;
        }
        const QJsonObject taggedTransaction = transactionValue.toObject();
        if (!exactKeys(taggedTransaction, {"Public"})
            || !taggedTransaction.value(QStringLiteral("Public")).isObject()) {
            result.reason = "invalid-indexer-transaction";
            return result;
        }
        const QJsonObject transactionObject =
            taggedTransaction.value(QStringLiteral("Public")).toObject();
        if (!exactKeys(transactionObject, {"hash", "message", "witness_set"})
            || !transactionObject.value(QStringLiteral("hash")).isString()
            || !transactionObject.value(QStringLiteral("message")).isObject()
            || !transactionObject.value(QStringLiteral("witness_set")).isObject()) {
            result.reason = "invalid-indexer-transaction";
            return result;
        }
        PalaceLezIndexerTransaction transaction;
        transaction.hash =
            transactionObject.value(QStringLiteral("hash")).toString().toStdString();
        if (!isLowerHex(transaction.hash, 64U)
            || !hashes.insert(transaction.hash).second) {
            result.reason = "invalid-indexer-transaction";
            return result;
        }

        const QJsonObject message =
            transactionObject.value(QStringLiteral("message")).toObject();
        if (!exactKeys(
                message,
                {"program_id", "account_ids", "nonces", "instruction_data"})
            || !message.value(QStringLiteral("program_id")).isString()
            || !message.value(QStringLiteral("account_ids")).isArray()
            || !message.value(QStringLiteral("nonces")).isArray()
            || !message.value(QStringLiteral("instruction_data")).isArray()) {
            result.reason = "invalid-indexer-transaction";
            return result;
        }
        PalaceLezBytes32 programId{};
        const std::string programIdBase58 =
            message.value(QStringLiteral("program_id")).toString().toStdString();
        if (!parseAccountIdBase58(programIdBase58, programId) || !isNonzero(programId)) {
            result.reason = "invalid-indexer-transaction";
            return result;
        }
        transaction.programIdHex = bytes32Hex(programId);

        const QJsonArray accounts =
            message.value(QStringLiteral("account_ids")).toArray();
        const QJsonArray nonces = message.value(QStringLiteral("nonces")).toArray();
        if (accounts.size() < 2 || accounts.size() > static_cast<int>(kMaxTransactionAccounts)) {
            result.reason = "invalid-indexer-accounts";
            return result;
        }
        if (nonces.size() != accounts.size()) {
            result.reason = "invalid-indexer-accounts";
            return result;
        }
        std::set<std::string> accountIds;
        transaction.accounts.reserve(accounts.size());
        for (qsizetype index = 0; index < accounts.size(); ++index) {
            const QJsonValue accountValue = accounts.at(index);
            if (!accountValue.isString()) {
                result.reason = "invalid-indexer-accounts";
                return result;
            }
            PalaceLezBytes32 accountId{};
            const std::string base58 =
                accountValue.toString().toStdString();
            std::string nonce;
            if (!parseAccountIdBase58(base58, accountId) || !isNonzero(accountId)
                || !jsonSafeNonce(nonces.at(index), nonce)) {
                result.reason = "invalid-indexer-accounts";
                return result;
            }
            const std::string accountIdHex = bytes32Hex(accountId);
            if (!accountIds.insert(accountIdHex).second) {
                result.reason = "invalid-indexer-accounts";
                return result;
            }
            transaction.accounts.push_back({accountIdHex, nonce});
        }

        const QJsonArray instruction =
            message.value(QStringLiteral("instruction_data")).toArray();
        if (instruction.empty()
            || instruction.size() > static_cast<int>(kMaxInstructionWords)) {
            result.reason = "invalid-indexer-instruction";
            return result;
        }
        transaction.instructionWords.reserve(instruction.size());
        for (const QJsonValue& wordValue : instruction) {
            std::uint64_t word = 0;
            if (!jsonUnsigned(wordValue, std::numeric_limits<std::uint32_t>::max(), word)) {
                result.reason = "invalid-indexer-instruction";
                return result;
            }
            transaction.instructionWords.push_back(static_cast<std::uint32_t>(word));
        }

        const QJsonObject witness =
            transactionObject.value(QStringLiteral("witness_set")).toObject();
        if (!exactKeys(witness, {"signatures_and_public_keys", "proof"})
            || !witness.value(QStringLiteral("signatures_and_public_keys")).isArray()
            || !witness.value(QStringLiteral("proof")).isNull()) {
            result.reason = "invalid-indexer-witness";
            return result;
        }
        const QJsonArray signatures =
            witness.value(QStringLiteral("signatures_and_public_keys")).toArray();
        if (signatures.size() > 16) {
            result.reason = "invalid-indexer-witness";
            return result;
        }
        for (const QJsonValue& signatureValue : signatures) {
            if (!signatureValue.isArray()) {
                result.reason = "invalid-indexer-witness";
                return result;
            }
            const QJsonArray pair = signatureValue.toArray();
            if (pair.size() != 2 || !pair.at(0).isString() || !pair.at(1).isString()
                || !isLowerHex(pair.at(0).toString().toStdString(), 128U)
                || !canonicalBase64(pair.at(1).toString().toStdString(), 32U)) {
                result.reason = "invalid-indexer-witness";
                return result;
            }
        }
        transaction.signatureCount = static_cast<std::uint32_t>(signatures.size());
        result.transactions.push_back(std::move(transaction));
    }
    result.accepted = true;
    result.reason = "accepted";
    return result;
}

PalaceLezRebuildResultV3 PalaceLezCodec::rebuildFinalizedHistory(
    const std::string& programIdHex,
    const std::string& rootAccountIdHex,
    const std::string& responseJson)
{
    PalaceLezRebuildResultV3 result;
    PalaceLezBytes32 programId{};
    PalaceLezBytes32 root{};
    if (!canonicalProgramId(programIdHex, programId)
        || !canonicalAccountId(rootAccountIdHex, root)
        || deriveRootPda(programIdHex) != rootAccountIdHex) {
        result.reason = "invalid-program-or-root";
        return result;
    }
    const PalaceLezIndexerParseResult parsed = parseFinalizedTransactions(responseJson);
    if (!parsed.accepted) {
        result.reason = parsed.reason;
        return result;
    }
    if (parsed.transactions.empty()) {
        result.reason = "empty-finalized-history";
        return result;
    }
    result.actions.reserve(parsed.transactions.size());
    std::uint64_t expectedAction = 0U;
    for (std::size_t index = 0; index < parsed.transactions.size(); ++index) {
        const PalaceLezIndexerTransaction& transaction = parsed.transactions[index];
        if (transaction.programIdHex != programIdHex
            || transaction.accounts.front().accountIdHex != rootAccountIdHex
            || transaction.signatureCount != 1U) {
            result.reason = "finalized-transaction-mismatch";
            result.actions.clear();
            return result;
        }
        const PalaceLezWireInstruction decoded =
            decodeInstruction(transaction.instructionWords);
        if (!decoded.accepted) {
            result.reason = decoded.reason;
            result.actions.clear();
            return result;
        }
        const std::uint64_t action = instructionActionId(decoded.instruction);
        if ((index == 0U
                && instructionKind(decoded.instruction)
                    != PalaceLezInstructionKindV3::Initialize)
            || (index != 0U
                && instructionKind(decoded.instruction)
                    == PalaceLezInstructionKindV3::Initialize)
            || action != expectedAction) {
            result.reason = "non-chronological-finalized-history";
            result.actions.clear();
            return result;
        }
        if (transaction.accounts.size() < 2U) {
            result.reason = "finalized-transaction-mismatch";
            result.actions.clear();
            return result;
        }
        const PalaceLezTransactionPlanV3 plan = buildTransaction(
            programIdHex,
            transaction.accounts[1].accountIdHex,
            decoded.instruction);
        if (!planMatchesIndexer(plan, transaction)) {
            result.reason = "finalized-transaction-mismatch";
            result.actions.clear();
            return result;
        }
        PalaceLezFinalizedActionV3 finalized;
        finalized.transactionHash = transaction.hash;
        finalized.orderedActionId = action;
        finalized.instruction = decoded.instruction;
        for (const PalaceLezIndexerAccountRef& account : transaction.accounts)
            finalized.accountIdsHex.push_back(account.accountIdHex);
        result.actions.push_back(std::move(finalized));
        if (expectedAction == std::numeric_limits<std::uint64_t>::max()
            || (index + 1U < parsed.transactions.size()
                && ++expectedAction == 0U)) {
            result.reason = "non-chronological-finalized-history";
            result.actions.clear();
            return result;
        }
    }
    result.accepted = true;
    result.reason = "accepted";
    return result;
}

std::string PalaceLezCodec::sha256Hex(const std::vector<std::uint8_t>& bytes)
{
    PalaceLezBytes32 digest{};
    const std::uint8_t empty = 0U;
    const std::uint8_t* input = bytes.empty() ? &empty : bytes.data();
    return digestSha256(input, bytes.size(), digest) ? bytes32Hex(digest) : std::string{};
}

PalaceLezCompatibilityResult
PalaceLezTransactionCoordinator::configureNetworkFingerprint(
    const PalaceLezNetworkFingerprint& required,
    const PalaceLezNetworkFingerprint& observed)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_)
        return {false, "interrupt-before-network-check", compatibilityState_};
    if (!validNetworkFingerprint(required) || !validNetworkFingerprint(observed)) {
        compatibilityState_ = PalaceLezCompatibilityState::Incompatible;
        return {false, "invalid-network-fingerprint", compatibilityState_};
    }
    const bool matching = required.networkId == observed.networkId
        && required.moduleApiVersion == observed.moduleApiVersion
        && required.moduleRevision == observed.moduleRevision
        && required.runtimeRevision == observed.runtimeRevision
        && required.publicContractVersion == observed.publicContractVersion
        && required.publicContractRevision == observed.publicContractRevision
        && required.programIdHex == observed.programIdHex
        && required.programBytecodeSha256Hex == observed.programBytecodeSha256Hex;
    compatibilityState_ = matching ? PalaceLezCompatibilityState::Compatible
                                   : PalaceLezCompatibilityState::Incompatible;
    return matching
        ? PalaceLezCompatibilityResult{true, "compatible", compatibilityState_}
        : PalaceLezCompatibilityResult{false, "network-fingerprint-mismatch",
              compatibilityState_};
}

PalaceLezCompatibilityState
PalaceLezTransactionCoordinator::compatibilityState() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return compatibilityState_;
}

bool PalaceLezTransactionCoordinator::activate()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (compatibilityState_ != PalaceLezCompatibilityState::Compatible)
        return false;
    const bool changed = !running_;
    running_ = true;
    return changed;
}

void PalaceLezTransactionCoordinator::interrupt()
{
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
}

bool PalaceLezTransactionCoordinator::running() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

bool PalaceLezTransactionCoordinator::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_)
        return false;
    transactions_.clear();
    return true;
}

PalaceLezCoordinatorUpdate PalaceLezTransactionCoordinator::registerSubmission(
    const PalaceLezTransactionPlanV3& plan,
    const std::string& submissionResponseJson,
    const std::string& expectedRootDataSha256Hex)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_)
        return coordinatorError("coordinator-interrupted");
    if (!plan.accepted || plan.accountIdsHex.size() < 2U
        || !isLowerHex(expectedRootDataSha256Hex, 64U)) {
        return coordinatorError("invalid-submission-plan");
    }
    const PalaceLezWireInstruction decoded =
        PalaceLezCodec::decodeInstruction(plan.instructionWords);
    if (!decoded.accepted)
        return coordinatorError("invalid-submission-plan");
    const PalaceLezTransactionPlanV3 rebuilt = PalaceLezCodec::buildTransaction(
        plan.programIdHex,
        plan.accountIdsHex[1],
        decoded.instruction);
    if (!samePlan(plan, rebuilt))
        return coordinatorError("invalid-submission-plan");
    const PalaceLezSubmissionResult submission =
        PalaceLezCodec::parseSubmissionResult(submissionResponseJson);
    if (!submission.accepted)
        return coordinatorError(submission.reason);
    for (const PalaceLezTrackedTransaction& tracked : transactions_) {
        if (tracked.transactionHash == submission.transactionHash)
            return coordinatorError("duplicate-transaction", tracked.stage);
        if (tracked.orderedActionId == instructionActionId(decoded.instruction)
            && tracked.plan.rootAccountIdHex == plan.rootAccountIdHex) {
            return coordinatorError("duplicate-ordered-action", tracked.stage);
        }
    }
    if (transactions_.size() >= kMaxCoordinatorTransactions)
        return coordinatorError("coordinator-capacity-exceeded");
    PalaceLezTrackedTransaction tracked;
    tracked.stage = PalaceLezTransactionStage::Submitted;
    tracked.transactionHash = submission.transactionHash;
    tracked.orderedActionId = instructionActionId(decoded.instruction);
    tracked.expectedRootDataSha256Hex = expectedRootDataSha256Hex;
    tracked.plan = plan;
    tracked.plan.instruction = decoded.instruction;
    transactions_.push_back(std::move(tracked));
    return coordinatorChanged("submitted", PalaceLezTransactionStage::Submitted);
}

PalaceLezCoordinatorUpdate
PalaceLezTransactionCoordinator::registerRecoveredSubmission(
    const PalaceLezTransactionPlanV3& plan,
    const std::string& transactionHash,
    const std::string& expectedRootDataSha256Hex)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_)
        return coordinatorError("coordinator-interrupted");
    if (!plan.accepted || plan.accountIdsHex.size() < 2U
        || !isLowerHex(transactionHash, 64U)
        || !isLowerHex(expectedRootDataSha256Hex, 64U)) {
        return coordinatorError("invalid-recovered-submission");
    }
    const PalaceLezWireInstruction decoded =
        PalaceLezCodec::decodeInstruction(plan.instructionWords);
    if (!decoded.accepted)
        return coordinatorError("invalid-recovered-submission");
    const PalaceLezTransactionPlanV3 rebuilt =
        PalaceLezCodec::buildTransaction(
            plan.programIdHex,
            plan.accountIdsHex[1],
            decoded.instruction);
    if (!samePlan(plan, rebuilt))
        return coordinatorError("invalid-recovered-submission");

    const std::uint64_t orderedActionId =
        instructionActionId(decoded.instruction);
    for (const PalaceLezTrackedTransaction& tracked : transactions_) {
        if (tracked.transactionHash == transactionHash
            || (tracked.orderedActionId == orderedActionId
                && tracked.plan.rootAccountIdHex
                    == plan.rootAccountIdHex)) {
            if (tracked.transactionHash == transactionHash
                && tracked.orderedActionId == orderedActionId
                && tracked.expectedRootDataSha256Hex
                    == expectedRootDataSha256Hex
                && samePlan(tracked.plan, plan)) {
                return coordinatorNoChange(
                    "recovered-submission-already-tracked",
                    tracked.stage);
            }
            return coordinatorError(
                "recovered-submission-conflict",
                tracked.stage);
        }
    }
    if (transactions_.size() >= kMaxCoordinatorTransactions)
        return coordinatorError("coordinator-capacity-exceeded");

    PalaceLezTrackedTransaction tracked;
    tracked.stage = PalaceLezTransactionStage::Submitted;
    tracked.transactionHash = transactionHash;
    tracked.orderedActionId = orderedActionId;
    tracked.expectedRootDataSha256Hex =
        expectedRootDataSha256Hex;
    tracked.plan = plan;
    tracked.plan.instruction = decoded.instruction;
    transactions_.push_back(std::move(tracked));
    return coordinatorChanged(
        "recovered-submission",
        PalaceLezTransactionStage::Submitted);
}

PalaceLezCoordinatorUpdate PalaceLezTransactionCoordinator::observeStableRoot(
    const std::string& transactionHash,
    const std::int64_t heightBefore,
    const std::string& rootAccountJson,
    const std::int64_t heightAfter)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_)
        return coordinatorError("coordinator-interrupted");
    auto iterator = std::find_if(
        transactions_.begin(),
        transactions_.end(),
        [&](const PalaceLezTrackedTransaction& transaction) {
            return transaction.transactionHash == transactionHash;
        });
    if (iterator == transactions_.end())
        return coordinatorError("unknown-transaction");
    if (iterator->stage == PalaceLezTransactionStage::Finalized)
        return coordinatorNoChange("already-finalized", iterator->stage);
    if (iterator->stage == PalaceLezTransactionStage::Observed)
        return coordinatorNoChange("already-observed", iterator->stage);
    if (heightBefore < 0 || heightAfter < 0)
        return coordinatorError("invalid-block-height", iterator->stage);
    if (heightBefore != heightAfter)
        return coordinatorError("unstable-height", iterator->stage);
    const PalaceLezPublicAccountV3 account = PalaceLezCodec::decodePublicAccount(
        rootAccountJson,
        iterator->plan.programIdHex);
    if (!account.accepted)
        return coordinatorError(account.reason, iterator->stage);
    if (account.recordType != PalaceLezRecordTypeV3::PalaceRoot)
        return coordinatorError("unexpected-observation-record", iterator->stage);
    const PalaceLezRootRecordV3* root =
        std::get_if<PalaceLezRootRecordV3>(&account.record);
    if (root == nullptr || root->lastOrderedActionId != iterator->orderedActionId
        || account.dataSha256Hex != iterator->expectedRootDataSha256Hex) {
        return coordinatorError("observation-mismatch", iterator->stage);
    }
    iterator->stage = PalaceLezTransactionStage::Observed;
    iterator->observedBlockHeight = static_cast<std::uint64_t>(heightBefore);
    return coordinatorChanged("observed", iterator->stage);
}

PalaceLezCoordinatorUpdate PalaceLezTransactionCoordinator::reconcileFinality(
    const std::string& transactionHash,
    const std::string& indexerTransactionsJson)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_)
        return coordinatorError("coordinator-interrupted");
    auto iterator = std::find_if(
        transactions_.begin(),
        transactions_.end(),
        [&](const PalaceLezTrackedTransaction& transaction) {
            return transaction.transactionHash == transactionHash;
        });
    if (iterator == transactions_.end())
        return coordinatorError("unknown-transaction");
    if (iterator->stage == PalaceLezTransactionStage::Finalized)
        return coordinatorNoChange("already-finalized", iterator->stage);
    const PalaceLezIndexerParseResult parsed =
        PalaceLezCodec::parseFinalizedTransactions(indexerTransactionsJson);
    if (!parsed.accepted)
        return coordinatorError(parsed.reason, iterator->stage);
    const PalaceLezIndexerTransaction* matching = nullptr;
    bool samePayloadDifferentHash = false;
    for (const PalaceLezIndexerTransaction& transaction : parsed.transactions) {
        if (transaction.hash == transactionHash)
            matching = &transaction;
        else if (planMatchesIndexer(iterator->plan, transaction))
            samePayloadDifferentHash = true;
    }
    if (matching == nullptr) {
        return samePayloadDifferentHash
            ? coordinatorError("transaction-hash-mismatch", iterator->stage)
            : coordinatorNoChange("indexer-lag", iterator->stage);
    }
    if (!planMatchesIndexer(iterator->plan, *matching))
        return coordinatorError("finalized-transaction-mismatch", iterator->stage);
    if (iterator->stage != PalaceLezTransactionStage::Observed)
        return coordinatorError("observation-required", iterator->stage);
    iterator->stage = PalaceLezTransactionStage::Finalized;
    return coordinatorChanged("finalized", iterator->stage);
}

PalaceLezCoordinatorUpdate
PalaceLezTransactionCoordinator::reconcileExplorerFinality(
    const PalaceLezExplorerFinalityCertificateV1& certificate)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_)
        return coordinatorError("coordinator-interrupted");
    if (!validExplorerCertificate(certificate))
        return coordinatorError("invalid-finality-certificate");

    auto iterator = std::find_if(
        transactions_.begin(),
        transactions_.end(),
        [&](const PalaceLezTrackedTransaction& transaction) {
            return transaction.transactionHash
                == certificate.transactionHashHex();
        });
    if (iterator == transactions_.end()) {
        const bool samePayloadDifferentHash = std::any_of(
            transactions_.begin(),
            transactions_.end(),
            [&](const PalaceLezTrackedTransaction& transaction) {
                return planMatchesExplorerCertificate(
                    transaction.plan, certificate);
            });
        return coordinatorError(
            samePayloadDifferentHash
                ? "transaction-hash-mismatch"
                : "unknown-transaction");
    }
    if (!planMatchesExplorerCertificate(iterator->plan, certificate)) {
        return coordinatorError(
            "finalized-transaction-mismatch", iterator->stage);
    }
    if (iterator->stage == PalaceLezTransactionStage::Finalized) {
        return coordinatorError(
            "duplicate-finality-certificate", iterator->stage);
    }
    if (iterator->stage != PalaceLezTransactionStage::Observed)
        return coordinatorError("observation-required", iterator->stage);
    if (certificate.finalizedBlockHeight()
        > iterator->observedBlockHeight) {
        return coordinatorError(
            "finality-block-height-mismatch", iterator->stage);
    }
    const std::string programIdBase58 =
        accountIdBase58FromHex(iterator->plan.programIdHex);
    const PalaceLezExplorerFinalityAccountEvidenceV1& rootEvidence =
        certificate.accountEvidence().front();
    if (rootEvidence.programOwnerBase58 != programIdBase58
        || rootEvidence.dataSha256Hex
            != iterator->expectedRootDataSha256Hex) {
        return coordinatorError(
            "finalized-account-state-mismatch", iterator->stage);
    }
    for (std::size_t index = 0U;
         index < certificate.accountEvidence().size();
         ++index) {
        if (!iterator->plan.signingRequirements[index]
            && certificate.accountEvidence()[index].programOwnerBase58
                != programIdBase58) {
            return coordinatorError(
                "finalized-account-owner-mismatch", iterator->stage);
        }
    }
    iterator->stage = PalaceLezTransactionStage::Finalized;
    return coordinatorChanged("finalized", iterator->stage);
}

PalaceLezCoordinatorSnapshot PalaceLezTransactionCoordinator::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PalaceLezTrackedTransaction> sorted = transactions_;
    std::sort(
        sorted.begin(),
        sorted.end(),
        [](const PalaceLezTrackedTransaction& left, const PalaceLezTrackedTransaction& right) {
            return std::tie(left.orderedActionId, left.transactionHash)
                < std::tie(right.orderedActionId, right.transactionHash);
        });
    SnapshotWriter writer;
    writer.bytes.insert(writer.bytes.end(), kSnapshotMagic.begin(), kSnapshotMagic.end());
    writer.u16(kSnapshotVersion);
    writer.u16(static_cast<std::uint16_t>(sorted.size()));
    for (const PalaceLezTrackedTransaction& transaction : sorted) {
        PalaceLezBytes32 hash{};
        PalaceLezBytes32 program{};
        PalaceLezBytes32 root{};
        PalaceLezBytes32 digest{};
        if (!PalaceLezCodec::parseBytes32Hex(transaction.transactionHash, hash)
            || !PalaceLezCodec::parseBytes32Hex(transaction.plan.programIdHex, program)
            || !PalaceLezCodec::parseBytes32Hex(transaction.plan.rootAccountIdHex, root)
            || !PalaceLezCodec::parseBytes32Hex(
                transaction.expectedRootDataSha256Hex,
                digest)
            || transaction.plan.instructionWords.size() > kMaxInstructionWords
            || transaction.plan.accountIdsHex.size() > kMaxTransactionAccounts
            || transaction.plan.accountIdsHex.size()
                != transaction.plan.signingRequirements.size()) {
            return {false, "invalid-coordinator-state", {}};
        }
        writer.u8(static_cast<std::uint8_t>(transaction.stage));
        writer.u64(transaction.orderedActionId);
        writer.u64(transaction.observedBlockHeight);
        writer.fixed(hash);
        writer.fixed(program);
        writer.fixed(root);
        writer.fixed(digest);
        writer.u16(static_cast<std::uint16_t>(transaction.plan.instructionWords.size()));
        for (const std::uint32_t word : transaction.plan.instructionWords)
            writer.u32(word);
        writer.u8(static_cast<std::uint8_t>(transaction.plan.accountIdsHex.size()));
        for (std::size_t index = 0; index < transaction.plan.accountIdsHex.size(); ++index) {
            PalaceLezBytes32 account{};
            if (!PalaceLezCodec::parseBytes32Hex(
                    transaction.plan.accountIdsHex[index],
                    account)) {
                return {false, "invalid-coordinator-state", {}};
            }
            writer.fixed(account);
            writer.u8(transaction.plan.signingRequirements[index] ? 1U : 0U);
        }
    }
    if (writer.bytes.size() > kMaxCoordinatorSnapshotBytes)
        return {false, "invalid-coordinator-state", {}};
    return {true, "accepted", std::move(writer.bytes)};
}

PalaceLezCoordinatorUpdate PalaceLezTransactionCoordinator::restore(
    const std::vector<std::uint8_t>& bytes)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_)
        return coordinatorError("interrupt-before-restore");
    if (bytes.empty() || bytes.size() > kMaxCoordinatorSnapshotBytes)
        return coordinatorError("invalid-snapshot");
    SnapshotReader reader(bytes);
    for (const std::uint8_t expected : kSnapshotMagic) {
        std::uint8_t actual = 0;
        if (!reader.u8(actual) || actual != expected)
            return coordinatorError("invalid-snapshot");
    }
    std::uint16_t version = 0;
    std::uint16_t count = 0;
    if (!reader.u16(version) || version != kSnapshotVersion || !reader.u16(count)
        || count > kMaxCoordinatorTransactions) {
        return coordinatorError("invalid-snapshot");
    }
    std::vector<PalaceLezTrackedTransaction> restored;
    restored.reserve(count);
    std::set<std::string> hashes;
    std::set<std::pair<std::string, std::uint64_t>> actions;
    for (std::uint16_t item = 0; item < count; ++item) {
        std::uint8_t stage = 0;
        PalaceLezBytes32 hash{};
        PalaceLezBytes32 program{};
        PalaceLezBytes32 root{};
        PalaceLezBytes32 digest{};
        PalaceLezTrackedTransaction transaction;
        if (!reader.u8(stage) || stage > 2U || !reader.u64(transaction.orderedActionId)
            || !reader.u64(transaction.observedBlockHeight) || !reader.fixed(hash)
            || !reader.fixed(program) || !reader.fixed(root) || !reader.fixed(digest)) {
            return coordinatorError("invalid-snapshot");
        }
        transaction.stage = static_cast<PalaceLezTransactionStage>(stage);
        transaction.transactionHash = PalaceLezCodec::bytes32Hex(hash);
        transaction.expectedRootDataSha256Hex = PalaceLezCodec::bytes32Hex(digest);
        transaction.plan.programIdHex = PalaceLezCodec::bytes32Hex(program);
        transaction.plan.rootAccountIdHex = PalaceLezCodec::bytes32Hex(root);
        std::uint16_t wordCount = 0;
        if (!reader.u16(wordCount) || wordCount == 0U || wordCount > kMaxInstructionWords)
            return coordinatorError("invalid-snapshot");
        transaction.plan.instructionWords.reserve(wordCount);
        for (std::uint16_t word = 0; word < wordCount; ++word) {
            std::uint32_t value = 0;
            if (!reader.u32(value))
                return coordinatorError("invalid-snapshot");
            transaction.plan.instructionWords.push_back(value);
        }
        std::uint8_t accountCount = 0;
        if (!reader.u8(accountCount) || accountCount < 2U
            || accountCount > kMaxTransactionAccounts) {
            return coordinatorError("invalid-snapshot");
        }
        for (std::uint8_t accountIndex = 0; accountIndex < accountCount; ++accountIndex) {
            PalaceLezBytes32 account{};
            std::uint8_t signing = 0;
            if (!reader.fixed(account) || !reader.u8(signing) || signing > 1U)
                return coordinatorError("invalid-snapshot");
            transaction.plan.accountIdsHex.push_back(PalaceLezCodec::bytes32Hex(account));
            transaction.plan.signingRequirements.push_back(signing == 1U);
        }
        const PalaceLezWireInstruction decoded =
            PalaceLezCodec::decodeInstruction(transaction.plan.instructionWords);
        if (!decoded.accepted
            || transaction.orderedActionId != instructionActionId(decoded.instruction)) {
            return coordinatorError("invalid-snapshot");
        }
        const PalaceLezTransactionPlanV3 plan = PalaceLezCodec::buildTransaction(
            transaction.plan.programIdHex,
            transaction.plan.accountIdsHex[1],
            decoded.instruction);
        transaction.plan.accepted = true;
        transaction.plan.reason = "accepted";
        transaction.plan.instruction = decoded.instruction;
        if (!samePlan(transaction.plan, plan)
            || !hashes.insert(transaction.transactionHash).second
            || !actions.insert({transaction.plan.rootAccountIdHex, transaction.orderedActionId})
                    .second
            || (transaction.stage == PalaceLezTransactionStage::Submitted
                && transaction.observedBlockHeight != 0U)) {
            return coordinatorError("invalid-snapshot");
        }
        restored.push_back(std::move(transaction));
    }
    if (!reader.finished())
        return coordinatorError("invalid-snapshot");
    transactions_ = std::move(restored);
    return coordinatorChanged("restored", PalaceLezTransactionStage::Submitted);
}

std::vector<PalaceLezTrackedTransaction>
PalaceLezTransactionCoordinator::transactions() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return transactions_;
}

} // namespace palace
