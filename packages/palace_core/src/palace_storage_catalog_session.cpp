#include "palace_storage_catalog_session.h"

#include "palace_sha256.h"
#include "palace_storage_cid.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

namespace palace {
namespace {

constexpr char kHexDigits[] = "0123456789abcdef";
constexpr std::size_t kAbsoluteMaxObjects = 256U;
constexpr std::size_t kAbsoluteMaxOperations = 256U;
constexpr std::size_t kAbsoluteMaxChildren = 64U;
constexpr std::size_t kAbsoluteMaxAttestations = 16U;
constexpr std::size_t kAbsoluteMaxChallenges = 256U;
constexpr std::size_t kAbsoluteMaxCompleted = 1024U;
constexpr std::size_t kAbsoluteMaxSignatureBytes = 4096U;
constexpr std::uint64_t kAbsoluteMaxChallengeLifetime = 86400U;
constexpr std::uint64_t kAbsoluteMaxObjectBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumStateBytes = 4U * 1024U * 1024U;
constexpr const char* kDownloadProtocol = "logos.storage.download";
constexpr std::uint32_t kDownloadProtocolVersion = 2U;
constexpr std::uint32_t kAttestationVersion = 1U;
constexpr const char* kOperationIdPrefix = "storage-";
constexpr const char* kChallengeIdPrefix = "attestation-";
constexpr const char* kManifestHeader =
    "logos-palace-catalog-manifest-v1";
constexpr const char* kReceiptHeader =
    "logos-palace-holder-attestation-v1";

struct ParsedManifest {
    StorageCatalogObjectKind kind = StorageCatalogObjectKind::Blob;
    std::string objectId;
    std::vector<StorageCatalogManifestChildV1> children;
};

StorageCatalogTransition transition(bool accepted, std::string reason)
{
    return {accepted, std::move(reason), std::nullopt};
}

StorageCatalogTransition operationTransition(
    std::string reason,
    StorageCatalogOperation operation)
{
    return {true, std::move(reason), std::move(operation)};
}

StorageCatalogChallengeTransition challengeTransition(
    std::string reason,
    StorageCatalogHolderChallengeV1 challenge)
{
    return {true, std::move(reason), std::move(challenge)};
}

bool isAsciiDigit(char character)
{
    return character >= '0' && character <= '9';
}

bool isAsciiLower(char character)
{
    return character >= 'a' && character <= 'z';
}

bool isAsciiUpper(char character)
{
    return character >= 'A' && character <= 'Z';
}

bool isAsciiAlphaNumeric(char character)
{
    return isAsciiDigit(character)
        || isAsciiLower(character)
        || isAsciiUpper(character);
}

bool isObjectId(const std::string& value)
{
    if (value.empty() || value.size() > 64U
        || value == "." || value == "..") {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char character) {
        return isAsciiAlphaNumeric(character)
            || character == '_' || character == '-';
    });
}

bool isLowerHex(const std::string& value, std::size_t exactSize)
{
    return value.size() == exactSize
        && std::all_of(value.begin(), value.end(), [](char character) {
            return isAsciiDigit(character)
                || (character >= 'a' && character <= 'f');
        });
}

bool isHolderAccountId(const std::string& value)
{
    return isLowerHex(value, 64U);
}

bool isLowerHexDigest(const std::string& value)
{
    return isLowerHex(value, 64U);
}

bool validObjectKind(StorageCatalogObjectKind kind)
{
    switch (kind) {
    case StorageCatalogObjectKind::Blob:
    case StorageCatalogObjectKind::PropManifest:
    case StorageCatalogObjectKind::RoomManifest:
    case StorageCatalogObjectKind::PalaceManifest:
        return true;
    }
    return false;
}

bool validLocalPhase(StorageCatalogLocalPhase phase)
{
    switch (phase) {
    case StorageCatalogLocalPhase::Missing:
    case StorageCatalogLocalPhase::Fetching:
    case StorageCatalogLocalPhase::Verified:
        return true;
    }
    return false;
}

bool validPublicationStage(StorageCatalogPublicationStage stage)
{
    switch (stage) {
    case StorageCatalogPublicationStage::Staged:
    case StorageCatalogPublicationStage::Uploading:
    case StorageCatalogPublicationStage::VerifyingLocal:
    case StorageCatalogPublicationStage::Published:
    case StorageCatalogPublicationStage::Failed:
        return true;
    }
    return false;
}

bool validOperationKind(StorageCatalogOperationKind kind)
{
    switch (kind) {
    case StorageCatalogOperationKind::Upload:
    case StorageCatalogOperationKind::LocalFetch:
    case StorageCatalogOperationKind::PublicationVerification:
        return true;
    }
    return false;
}

bool validOperationPhase(StorageCatalogOperationPhase phase)
{
    switch (phase) {
    case StorageCatalogOperationPhase::AwaitingAcknowledgement:
    case StorageCatalogOperationPhase::Active:
        return true;
    }
    return false;
}

bool validDownloadOutcome(StorageCatalogDownloadOutcome outcome)
{
    switch (outcome) {
    case StorageCatalogDownloadOutcome::Succeeded:
    case StorageCatalogDownloadOutcome::Failed:
    case StorageCatalogDownloadOutcome::Canceled:
        return true;
    }
    return false;
}

bool validDagEdge(
    StorageCatalogObjectKind parent,
    StorageCatalogObjectKind child)
{
    switch (parent) {
    case StorageCatalogObjectKind::Blob:
        return false;
    case StorageCatalogObjectKind::PropManifest:
        return child == StorageCatalogObjectKind::Blob;
    case StorageCatalogObjectKind::RoomManifest:
        return child == StorageCatalogObjectKind::Blob
            || child == StorageCatalogObjectKind::PropManifest;
    case StorageCatalogObjectKind::PalaceManifest:
        return child == StorageCatalogObjectKind::RoomManifest;
    }
    return false;
}

bool validConfig(const StorageCatalogSessionConfigV3& config)
{
    return isHolderAccountId(config.localHolderAccountId)
        && config.maxObjects > 0U
        && config.maxObjects <= kAbsoluteMaxObjects
        && config.maxOperations > 0U
        && config.maxOperations <= kAbsoluteMaxOperations
        && config.maxChildrenPerObject > 0U
        && config.maxChildrenPerObject <= kAbsoluteMaxChildren
        && config.maxAttestationsPerObject > 0U
        && config.maxAttestationsPerObject <= kAbsoluteMaxAttestations
        && config.maxPendingAttestationChallenges > 0U
        && config.maxPendingAttestationChallenges
            <= kAbsoluteMaxChallenges
        && config.maxCompletedOperations > 0U
        && config.maxCompletedOperations <= kAbsoluteMaxCompleted
        && config.maxCompletedChallenges > 0U
        && config.maxCompletedChallenges <= kAbsoluteMaxCompleted
        && config.maxSignatureBytes > 0U
        && config.maxSignatureBytes <= kAbsoluteMaxSignatureBytes
        && config.maxChallengeLifetimeSeconds > 0U
        && config.maxChallengeLifetimeSeconds
            <= kAbsoluteMaxChallengeLifetime
        && config.maxObjectBytes > 0U
        && config.maxObjectBytes <= kAbsoluteMaxObjectBytes
        && config.initialSessionEpoch > 0U;
}

bool sameConfig(
    const StorageCatalogSessionConfigV3& left,
    const StorageCatalogSessionConfigV3& right)
{
    return left.localHolderAccountId == right.localHolderAccountId
        && left.maxObjects == right.maxObjects
        && left.maxOperations == right.maxOperations
        && left.maxChildrenPerObject == right.maxChildrenPerObject
        && left.maxAttestationsPerObject
            == right.maxAttestationsPerObject
        && left.maxPendingAttestationChallenges
            == right.maxPendingAttestationChallenges
        && left.maxCompletedOperations == right.maxCompletedOperations
        && left.maxCompletedChallenges == right.maxCompletedChallenges
        && left.maxSignatureBytes == right.maxSignatureBytes
        && left.maxChallengeLifetimeSeconds
            == right.maxChallengeLifetimeSeconds
        && left.maxObjectBytes == right.maxObjectBytes
        && left.initialSessionEpoch == right.initialSessionEpoch;
}

template <typename Integer>
bool parseInteger(const std::string& value, Integer& parsedValue)
{
    if (value.empty())
        return false;
    const auto parsed = std::from_chars(
        value.data(), value.data() + value.size(), parsedValue);
    return parsed.ec == std::errc()
        && parsed.ptr == value.data() + value.size()
        && value == std::to_string(parsedValue);
}

std::vector<std::string> split(
    const std::string& value,
    char delimiter,
    std::size_t maximumFields)
{
    std::vector<std::string> fields;
    std::size_t cursor = 0U;
    while (true) {
        if (fields.size() >= maximumFields)
            return {};
        const std::size_t next = value.find(delimiter, cursor);
        fields.push_back(value.substr(cursor, next - cursor));
        if (next == std::string::npos)
            return fields;
        cursor = next + 1U;
    }
}

std::vector<std::string> lines(
    const std::string& value,
    std::size_t maximumLines)
{
    if (value.empty() || value.back() != '\n')
        return {};
    std::vector<std::string> result;
    std::size_t cursor = 0U;
    while (cursor < value.size()) {
        if (result.size() >= maximumLines)
            return {};
        const std::size_t next = value.find('\n', cursor);
        if (next == std::string::npos || next == cursor)
            return {};
        result.push_back(value.substr(cursor, next - cursor));
        cursor = next + 1U;
    }
    return result;
}

std::string objectKindName(StorageCatalogObjectKind kind)
{
    switch (kind) {
    case StorageCatalogObjectKind::Blob: return "blob";
    case StorageCatalogObjectKind::PropManifest: return "prop_manifest";
    case StorageCatalogObjectKind::RoomManifest: return "room_manifest";
    case StorageCatalogObjectKind::PalaceManifest: return "palace_manifest";
    }
    return {};
}

bool parseObjectKindName(
    const std::string& value,
    StorageCatalogObjectKind& kind)
{
    if (value == "blob")
        kind = StorageCatalogObjectKind::Blob;
    else if (value == "prop_manifest")
        kind = StorageCatalogObjectKind::PropManifest;
    else if (value == "room_manifest")
        kind = StorageCatalogObjectKind::RoomManifest;
    else if (value == "palace_manifest")
        kind = StorageCatalogObjectKind::PalaceManifest;
    else
        return false;
    return true;
}

bool sameChild(
    const StorageCatalogManifestChildV1& left,
    const StorageCatalogManifestChildV1& right)
{
    return left.objectId == right.objectId
        && left.cid == right.cid
        && left.byteLength == right.byteLength
        && left.contentSha256 == right.contentSha256;
}

bool validBasicChild(const StorageCatalogManifestChildV1& child)
{
    return isObjectId(child.objectId)
        && isCanonicalStorageCid(child.cid)
        && child.byteLength > 0U
        && isLowerHexDigest(child.contentSha256);
}

bool canonicalChildren(
    const std::vector<StorageCatalogManifestChildV1>& children)
{
    if (children.empty())
        return false;
    for (std::size_t index = 0U; index < children.size(); ++index) {
        if (!validBasicChild(children[index])
            || (index != 0U
                && children[index - 1U].objectId
                    >= children[index].objectId)) {
            return false;
        }
    }
    return true;
}

bool parseCanonicalManifest(
    const std::string& bytes,
    ParsedManifest& parsed)
{
    const std::vector<std::string> manifestLines =
        lines(bytes, 4U + kAbsoluteMaxChildren);
    if (manifestLines.size() < 5U
        || manifestLines[0] != kManifestHeader
        || manifestLines[1].rfind("kind=", 0U) != 0U
        || !parseObjectKindName(
            manifestLines[1].substr(5U), parsed.kind)
        || parsed.kind == StorageCatalogObjectKind::Blob
        || manifestLines[2].rfind("object=", 0U) != 0U
        || !isObjectId(
            (parsed.objectId = manifestLines[2].substr(7U)))
        || manifestLines[3].rfind("children=", 0U) != 0U) {
        return false;
    }
    std::size_t childCount = 0U;
    if (!parseInteger(manifestLines[3].substr(9U), childCount)
        || childCount == 0U
        || childCount > kAbsoluteMaxChildren
        || manifestLines.size() != 4U + childCount) {
        return false;
    }
    parsed.children.clear();
    parsed.children.reserve(childCount);
    for (std::size_t index = 0U; index < childCount; ++index) {
        const std::string& line = manifestLines[index + 4U];
        if (line.rfind("child=", 0U) != 0U)
            return false;
        const std::vector<std::string> fields =
            split(line.substr(6U), ';', 4U);
        StorageCatalogManifestChildV1 child;
        if (fields.size() != 4U
            || !isObjectId((child.objectId = fields[0]))
            || !isCanonicalStorageCid((child.cid = fields[1]))
            || !parseInteger(fields[2], child.byteLength)
            || child.byteLength == 0U
            || !isLowerHexDigest(
                (child.contentSha256 = fields[3]))) {
            return false;
        }
        parsed.children.push_back(std::move(child));
    }
    return canonicalChildren(parsed.children)
        && canonicalStorageCatalogManifestV1(
            parsed.kind, parsed.objectId, parsed.children) == bytes;
}

bool parseGeneratedId(
    const std::string& value,
    const std::string& prefix,
    std::uint64_t& epoch,
    std::uint64_t& sequence)
{
    if (value.rfind(prefix, 0U) != 0U)
        return false;
    const std::size_t separator = value.find('-', prefix.size());
    if (separator == std::string::npos)
        return false;
    return parseInteger(
               value.substr(prefix.size(), separator - prefix.size()),
               epoch)
        && parseInteger(value.substr(separator + 1U), sequence)
        && epoch > 0U && sequence > 0U;
}

bool validReceiptShape(
    const StorageCatalogHolderAttestationReceiptV1& receipt)
{
    std::uint64_t epoch = 0U;
    std::uint64_t sequence = 0U;
    return receipt.version == kAttestationVersion
        && parseGeneratedId(
            receipt.challengeId,
            kChallengeIdPrefix,
            epoch,
            sequence)
        && isHolderAccountId(receipt.holderAccountId)
        && receipt.holderKeyEpoch > 0U
        && isObjectId(receipt.objectId)
        && isCanonicalStorageCid(receipt.cid)
        && receipt.byteLength > 0U
        && isLowerHexDigest(receipt.contentSha256)
        && receipt.expiresAtUnixSeconds > receipt.issuedAtUnixSeconds;
}

bool receiptMatchesChallenge(
    const StorageCatalogHolderAttestationReceiptV1& receipt,
    const StorageCatalogHolderChallengeV1& challenge)
{
    return receipt.version == challenge.version
        && receipt.challengeId == challenge.challengeId
        && receipt.holderAccountId == challenge.holderAccountId
        && receipt.holderKeyEpoch == challenge.holderKeyEpoch
        && receipt.objectId == challenge.objectId
        && receipt.cid == challenge.cid
        && receipt.byteLength == challenge.byteLength
        && receipt.contentSha256 == challenge.contentSha256
        && receipt.issuedAtUnixSeconds == challenge.issuedAtUnixSeconds
        && receipt.expiresAtUnixSeconds == challenge.expiresAtUnixSeconds;
}

StorageCatalogRetention retentionFor(
    bool localVerified,
    std::size_t attestedHolders)
{
    const std::size_t evidenceLocations =
        (localVerified ? 1U : 0U) + attestedHolders;
    if (evidenceLocations == 0U)
        return StorageCatalogRetention::Unknown;
    if (evidenceLocations == 1U)
        return StorageCatalogRetention::Degraded;
    return StorageCatalogRetention::Redundant;
}

std::string hexEncode(const std::string& value)
{
    std::string encoded;
    encoded.reserve(value.size() * 2U);
    for (const unsigned char byte : value) {
        encoded.push_back(kHexDigits[byte >> 4U]);
        encoded.push_back(kHexDigits[byte & 0x0fU]);
    }
    return encoded;
}

bool hexNibble(char character, unsigned char& value)
{
    if (character >= '0' && character <= '9') {
        value = static_cast<unsigned char>(character - '0');
        return true;
    }
    if (character >= 'a' && character <= 'f') {
        value = static_cast<unsigned char>(character - 'a' + 10);
        return true;
    }
    return false;
}

bool hexDecode(const std::string& encoded, std::string& value)
{
    if (encoded.empty() || encoded.size() > 256U
        || encoded.size() % 2U != 0U) {
        return false;
    }
    value.clear();
    value.reserve(encoded.size() / 2U);
    for (std::size_t index = 0U; index < encoded.size(); index += 2U) {
        unsigned char high = 0U;
        unsigned char low = 0U;
        if (!hexNibble(encoded[index], high)
            || !hexNibble(encoded[index + 1U], low)) {
            return false;
        }
        value.push_back(static_cast<char>((high << 4U) | low));
    }
    return true;
}

std::string encodeOptional(const std::string& value)
{
    return value.empty() ? "-" : hexEncode(value);
}

bool decodeOptional(const std::string& encoded, std::string& value)
{
    if (encoded == "-") {
        value.clear();
        return true;
    }
    return hexDecode(encoded, value);
}

std::string encodeChildIds(
    const std::vector<StorageCatalogManifestChildV1>& children)
{
    if (children.empty())
        return "-";
    std::ostringstream encoded;
    for (std::size_t index = 0U; index < children.size(); ++index) {
        if (index != 0U)
            encoded << ',';
        encoded << hexEncode(children[index].objectId);
    }
    return encoded.str();
}

bool decodeChildIds(
    const std::string& encoded,
    std::size_t maximumValues,
    std::vector<std::string>& values)
{
    values.clear();
    if (encoded == "-")
        return true;
    std::size_t cursor = 0U;
    while (true) {
        if (values.size() >= maximumValues)
            return false;
        const std::size_t next = encoded.find(',', cursor);
        std::string value;
        if (!hexDecode(encoded.substr(cursor, next - cursor), value))
            return false;
        values.push_back(std::move(value));
        if (next == std::string::npos)
            return true;
        cursor = next + 1U;
    }
}

bool parseObjectKind(
    const std::string& value,
    StorageCatalogObjectKind& kind)
{
    unsigned int parsed = 0U;
    if (!parseInteger(value, parsed))
        return false;
    switch (parsed) {
    case 0U: kind = StorageCatalogObjectKind::Blob; return true;
    case 1U: kind = StorageCatalogObjectKind::PropManifest; return true;
    case 2U: kind = StorageCatalogObjectKind::RoomManifest; return true;
    case 3U: kind = StorageCatalogObjectKind::PalaceManifest; return true;
    default: return false;
    }
}

bool parseLocalPhase(
    const std::string& value,
    StorageCatalogLocalPhase& phase)
{
    unsigned int parsed = 0U;
    if (!parseInteger(value, parsed))
        return false;
    switch (parsed) {
    case 0U: phase = StorageCatalogLocalPhase::Missing; return true;
    case 1U: phase = StorageCatalogLocalPhase::Fetching; return true;
    case 2U: phase = StorageCatalogLocalPhase::Verified; return true;
    default: return false;
    }
}

bool parsePublicationStage(
    const std::string& value,
    StorageCatalogPublicationStage& stage)
{
    unsigned int parsed = 0U;
    if (!parseInteger(value, parsed))
        return false;
    switch (parsed) {
    case 0U: stage = StorageCatalogPublicationStage::Staged; return true;
    case 1U: stage = StorageCatalogPublicationStage::Uploading; return true;
    case 2U:
        stage = StorageCatalogPublicationStage::VerifyingLocal;
        return true;
    case 3U: stage = StorageCatalogPublicationStage::Published; return true;
    case 4U: stage = StorageCatalogPublicationStage::Failed; return true;
    default: return false;
    }
}

bool parseOperationKind(
    const std::string& value,
    StorageCatalogOperationKind& kind)
{
    unsigned int parsed = 0U;
    if (!parseInteger(value, parsed))
        return false;
    switch (parsed) {
    case 0U: kind = StorageCatalogOperationKind::Upload; return true;
    case 1U: kind = StorageCatalogOperationKind::LocalFetch; return true;
    case 2U:
        kind = StorageCatalogOperationKind::PublicationVerification;
        return true;
    default: return false;
    }
}

bool parseOperationPhase(
    const std::string& value,
    StorageCatalogOperationPhase& phase)
{
    unsigned int parsed = 0U;
    if (!parseInteger(value, parsed))
        return false;
    switch (parsed) {
    case 0U:
        phase = StorageCatalogOperationPhase::AwaitingAcknowledgement;
        return true;
    case 1U: phase = StorageCatalogOperationPhase::Active; return true;
    default: return false;
    }
}

} // namespace

std::string storageCatalogLocalPhaseName(StorageCatalogLocalPhase phase)
{
    switch (phase) {
    case StorageCatalogLocalPhase::Missing: return "missing";
    case StorageCatalogLocalPhase::Fetching: return "fetching";
    case StorageCatalogLocalPhase::Verified: return "verified";
    }
    return "invalid";
}

std::string storageCatalogPublicationStageName(
    StorageCatalogPublicationStage stage)
{
    switch (stage) {
    case StorageCatalogPublicationStage::Staged: return "staged";
    case StorageCatalogPublicationStage::Uploading: return "uploading";
    case StorageCatalogPublicationStage::VerifyingLocal:
        return "verifying_local";
    case StorageCatalogPublicationStage::Published: return "published";
    case StorageCatalogPublicationStage::Failed: return "failed";
    }
    return "invalid";
}

std::string storageCatalogRetentionName(StorageCatalogRetention retention)
{
    switch (retention) {
    case StorageCatalogRetention::Unknown: return "unknown";
    case StorageCatalogRetention::Degraded: return "degraded";
    case StorageCatalogRetention::Redundant: return "redundant";
    }
    return "invalid";
}

std::string canonicalStorageCatalogManifestV1(
    StorageCatalogObjectKind kind,
    const std::string& objectId,
    const std::vector<StorageCatalogManifestChildV1>& children)
{
    if (!validObjectKind(kind)
        || kind == StorageCatalogObjectKind::Blob
        || !isObjectId(objectId)
        || !canonicalChildren(children)) {
        return {};
    }
    std::ostringstream encoded;
    encoded << kManifestHeader << '\n'
            << "kind=" << objectKindName(kind) << '\n'
            << "object=" << objectId << '\n'
            << "children=" << children.size() << '\n';
    for (const StorageCatalogManifestChildV1& child : children) {
        encoded << "child=" << child.objectId
                << ';' << child.cid
                << ';' << child.byteLength
                << ';' << child.contentSha256 << '\n';
    }
    return encoded.str();
}

std::string canonicalHolderAttestationReceiptV1(
    const StorageCatalogHolderAttestationReceiptV1& receipt)
{
    if (!validReceiptShape(receipt))
        return {};
    std::ostringstream encoded;
    encoded << kReceiptHeader << '\n'
            << "version=" << receipt.version << '\n'
            << "challenge=" << receipt.challengeId << '\n'
            << "holder=" << receipt.holderAccountId << '\n'
            << "key_epoch=" << receipt.holderKeyEpoch << '\n'
            << "object=" << receipt.objectId << '\n'
            << "cid=" << receipt.cid << '\n'
            << "byte_length=" << receipt.byteLength << '\n'
            << "sha256=" << receipt.contentSha256 << '\n'
            << "issued_at=" << receipt.issuedAtUnixSeconds << '\n'
            << "expires_at=" << receipt.expiresAtUnixSeconds << '\n';
    return encoded.str();
}

bool PalaceStorageCatalogSession::configure(
    const StorageCatalogSessionConfigV3& config)
{
    if (m_configured || !m_objects.empty() || !m_operations.empty()
        || !m_challenges.empty()
        || !m_completedOperationIds.empty()
        || !m_completedChallengeIds.empty()
        || !validConfig(config)) {
        return false;
    }
    m_config = config;
    m_configured = true;
    m_reconciliationRequired = false;
    m_sessionEpoch = config.initialSessionEpoch;
    m_lastOperationSequence = 0U;
    m_lastChallengeSequence = 0U;
    return true;
}

bool PalaceStorageCatalogSession::hasConfiguration() const
{
    return m_configured;
}

const StorageCatalogSessionConfigV3&
PalaceStorageCatalogSession::configuration() const
{
    return m_config;
}

bool PalaceStorageCatalogSession::reconciliationRequired() const
{
    return m_reconciliationRequired;
}

std::uint64_t PalaceStorageCatalogSession::sessionEpoch() const
{
    return m_sessionEpoch;
}

std::uint64_t PalaceStorageCatalogSession::lastOperationSequence() const
{
    return m_lastOperationSequence;
}

std::uint64_t PalaceStorageCatalogSession::lastChallengeSequence() const
{
    return m_lastChallengeSequence;
}

StorageCatalogTransition PalaceStorageCatalogSession::stagePublicationObject(
    const StorageCatalogObjectSpecV2& specification,
    const std::string& fullBytes)
{
    if (!canAct())
        return transition(false, "restart-reconciliation-required");
    if (m_objects.size() >= m_config.maxObjects)
        return transition(false, "object-capacity-exceeded");
    if (m_objects.find(specification.objectId) != m_objects.end())
        return transition(false, "duplicate-object-id");
    if (!validSpecification(specification))
        return transition(false, "invalid-object-specification");
    for (const StorageCatalogManifestChildV1& child : specification.children) {
        const auto found = m_objects.find(child.objectId);
        if (found == m_objects.end()
            || found->second.publicationStage
                != StorageCatalogPublicationStage::Published) {
            return transition(false, "child-not-published");
        }
    }
    if (!verifyFullBytes(specification, fullBytes))
        return transition(false, "local-object-verification-failed");

    ObjectRecord object;
    object.specification = specification;
    object.localPhase = StorageCatalogLocalPhase::Verified;
    object.publicationStage = StorageCatalogPublicationStage::Staged;
    m_objects.emplace(specification.objectId, std::move(object));
    return transition(true, "object-staged");
}

StorageCatalogTransition PalaceStorageCatalogSession::trackPublishedObject(
    const StorageCatalogObjectSpecV2& specification,
    const std::string& cid)
{
    if (!canAct())
        return transition(false, "restart-reconciliation-required");
    if (m_objects.size() >= m_config.maxObjects)
        return transition(false, "object-capacity-exceeded");
    if (m_objects.find(specification.objectId) != m_objects.end())
        return transition(false, "duplicate-object-id");
    if (!validSpecification(specification)
        || !isCanonicalStorageCid(cid))
        return transition(false, "invalid-published-object");
    for (const StorageCatalogManifestChildV1& child : specification.children) {
        const auto found = m_objects.find(child.objectId);
        if (found == m_objects.end()
            || found->second.publicationStage
                != StorageCatalogPublicationStage::Published) {
            return transition(false, "child-not-published");
        }
    }

    ObjectRecord object;
    object.specification = specification;
    object.localPhase = StorageCatalogLocalPhase::Missing;
    object.publicationStage = StorageCatalogPublicationStage::Published;
    object.cid = cid;
    m_objects.emplace(specification.objectId, std::move(object));
    return transition(true, "published-object-tracked");
}

StorageCatalogTransition
PalaceStorageCatalogSession::admitFinalizedPalaceManifest(
    const std::string& expectedCid,
    const std::string& fullBytes)
{
    if (!canAct())
        return transition(false, "restart-reconciliation-required");
    if (fullBytes.empty() || fullBytes.size() > m_config.maxObjectBytes)
        return transition(false, "bootstrap-manifest-size-invalid");

    const std::string digest = crypto::sha256Hex(fullBytes);
    // Native Storage CIDs identify its immutable upload manifest, rather
    // than necessarily the raw data digest. The fetched manifest is parsed
    // here, and every child is later admitted only after its committed bytes
    // match contentSha256.
    if (!isCanonicalStorageCid(expectedCid))
        return transition(false, "bootstrap-manifest-cid-invalid");

    ParsedManifest parsed;
    if (!parseCanonicalManifest(fullBytes, parsed)
        || parsed.kind != StorageCatalogObjectKind::PalaceManifest
        || parsed.children.size() > m_config.maxChildrenPerObject) {
        return transition(false, "bootstrap-palace-manifest-invalid");
    }
    if (m_objects.find(parsed.objectId) != m_objects.end())
        return transition(false, "duplicate-object-id");

    for (const StorageCatalogManifestChildV1& child : parsed.children) {
        if (child.objectId == parsed.objectId)
            return transition(false, "bootstrap-manifest-cycle");
        if (child.byteLength > m_config.maxObjectBytes
            || !isCanonicalStorageCid(child.cid)) {
            return transition(false, "bootstrap-child-commitment-invalid");
        }
        if (m_objects.find(child.objectId) != m_objects.end())
            return transition(false, "bootstrap-child-parent-mismatch");
    }
    if (m_objects.size() >= m_config.maxObjects
        || parsed.children.size()
            >= m_config.maxObjects - m_objects.size()) {
        return transition(false, "object-capacity-exceeded");
    }

    PalaceStorageCatalogSession admitted = *this;
    for (const StorageCatalogManifestChildV1& child : parsed.children) {
        ObjectRecord committedChild;
        committedChild.specification.objectId = child.objectId;
        committedChild.specification.kind =
            StorageCatalogObjectKind::RoomManifest;
        committedChild.specification.byteLength = child.byteLength;
        committedChild.specification.contentSha256 =
            child.contentSha256;
        committedChild.bootstrapAdmissionPending = true;
        committedChild.localPhase = StorageCatalogLocalPhase::Missing;
        committedChild.publicationStage =
            StorageCatalogPublicationStage::Published;
        committedChild.cid = child.cid;
        admitted.m_objects.emplace(
            committedChild.specification.objectId,
            std::move(committedChild));
    }

    ObjectRecord palaceManifest;
    palaceManifest.specification.objectId = parsed.objectId;
    palaceManifest.specification.kind = parsed.kind;
    palaceManifest.specification.byteLength = fullBytes.size();
    palaceManifest.specification.contentSha256 = digest;
    palaceManifest.specification.children = std::move(parsed.children);
    palaceManifest.localPhase = StorageCatalogLocalPhase::Verified;
    palaceManifest.publicationStage =
        StorageCatalogPublicationStage::Published;
    palaceManifest.cid = expectedCid;
    if (!admitted.validSpecification(palaceManifest.specification)
        || !admitted.m_objects.emplace(
            palaceManifest.specification.objectId,
            std::move(palaceManifest)).second
        || !admitted.validState()) {
        return transition(false, "bootstrap-palace-admission-invalid");
    }
    *this = std::move(admitted);
    return transition(true, "bootstrap-palace-manifest-admitted");
}

StorageCatalogTransition PalaceStorageCatalogSession::beginUpload(
    const std::string& objectId)
{
    return beginOperation(objectId, StorageCatalogOperationKind::Upload);
}

StorageCatalogTransition
PalaceStorageCatalogSession::beginPublicationVerification(
    const std::string& objectId)
{
    return beginOperation(
        objectId,
        StorageCatalogOperationKind::PublicationVerification);
}

StorageCatalogTransition PalaceStorageCatalogSession::beginLocalFetch(
    const std::string& objectId)
{
    return beginOperation(
        objectId, StorageCatalogOperationKind::LocalFetch);
}

StorageCatalogTransition
PalaceStorageCatalogSession::operationAcknowledged(
    const std::string& operationId,
    bool acceptedByModule)
{
    if (!canAct())
        return transition(false, "restart-reconciliation-required");
    if (wasOperationCompleted(operationId))
        return transition(false, "operation-already-completed");
    const auto operation = m_operations.find(operationId);
    if (operation == m_operations.end())
        return transition(false, "operation-not-found");
    if (operation->second.kind != StorageCatalogOperationKind::Upload)
        return transition(false, "typed-download-acknowledgement-required");
    return acknowledgeReservedOperation(operationId, acceptedByModule);
}

StorageCatalogTransition
PalaceStorageCatalogSession::downloadAcknowledged(
    const StorageCatalogDownloadAcknowledgementV2& acknowledgement)
{
    if (!canAct())
        return transition(false, "restart-reconciliation-required");
    if (acknowledgement.protocol != kDownloadProtocol
        || acknowledgement.version != kDownloadProtocolVersion) {
        return transition(false, "download-acknowledgement-protocol-mismatch");
    }
    if (wasOperationCompleted(acknowledgement.operationId))
        return transition(false, "operation-already-completed");
    const auto operation = m_operations.find(acknowledgement.operationId);
    if (operation == m_operations.end()
        || operation->second.kind == StorageCatalogOperationKind::Upload
        || acknowledgement.operationId == acknowledgement.cid
        || operation->second.cid != acknowledgement.cid) {
        return transition(
            false, "download-acknowledgement-correlation-mismatch");
    }
    return acknowledgeReservedOperation(
        acknowledgement.operationId,
        acknowledgement.accepted);
}

StorageCatalogTransition
PalaceStorageCatalogSession::acknowledgeReservedOperation(
    const std::string& operationId,
    bool acceptedByModule)
{
    const auto operation = m_operations.find(operationId);
    if (operation == m_operations.end())
        return transition(false, "operation-not-found");
    if (operation->second.phase
        != StorageCatalogOperationPhase::AwaitingAcknowledgement) {
        return transition(false, "operation-already-acknowledged");
    }
    const auto object = m_objects.find(operation->second.objectId);
    if (object == m_objects.end())
        return transition(false, "operation-object-missing");

    if (!acceptedByModule) {
        const StorageCatalogOperation rejected = operation->second;
        m_operations.erase(operation);
        rollbackAcknowledgement(rejected);
        rememberCompletedOperation(rejected.operationId);
        return transition(true, "operation-acknowledgement-rejected");
    }

    switch (operation->second.kind) {
    case StorageCatalogOperationKind::Upload:
        if (object->second.publicationStage
                != StorageCatalogPublicationStage::Staged
            || object->second.localPhase
                != StorageCatalogLocalPhase::Verified) {
            return transition(false, "operation-acknowledgement-state-mismatch");
        }
        object->second.publicationStage =
            StorageCatalogPublicationStage::Uploading;
        break;
    case StorageCatalogOperationKind::LocalFetch:
        if (object->second.publicationStage
                != StorageCatalogPublicationStage::Published
            || object->second.localPhase
                != StorageCatalogLocalPhase::Missing) {
            return transition(false, "operation-acknowledgement-state-mismatch");
        }
        object->second.localPhase = StorageCatalogLocalPhase::Fetching;
        break;
    case StorageCatalogOperationKind::PublicationVerification:
        if (object->second.publicationStage
                != StorageCatalogPublicationStage::VerifyingLocal
            || object->second.localPhase
                == StorageCatalogLocalPhase::Fetching) {
            return transition(false, "operation-acknowledgement-state-mismatch");
        }
        object->second.localPhase = StorageCatalogLocalPhase::Fetching;
        break;
    }
    operation->second.phase = StorageCatalogOperationPhase::Active;
    return transition(true, "operation-acknowledged");
}

StorageCatalogTransition PalaceStorageCatalogSession::uploadFinished(
    const std::string& operationId,
    bool succeeded,
    const std::string& cid)
{
    if (!canAct())
        return transition(false, "restart-reconciliation-required");
    if (wasOperationCompleted(operationId))
        return transition(false, "operation-already-completed");
    const auto operation = m_operations.find(operationId);
    if (operation == m_operations.end()
        || operation->second.kind != StorageCatalogOperationKind::Upload) {
        return transition(false, "upload-operation-mismatch");
    }
    if (operation->second.phase
        != StorageCatalogOperationPhase::Active) {
        return transition(false, "operation-not-acknowledged");
    }
    const auto object = m_objects.find(operation->second.objectId);
    if (object == m_objects.end()
        || object->second.publicationStage
            != StorageCatalogPublicationStage::Uploading) {
        return transition(false, "upload-state-mismatch");
    }
    if ((succeeded
            && (!isCanonicalStorageCid(cid)
                || cid == operationId))
        || (!succeeded && !cid.empty())) {
        return transition(false, "upload-terminal-invalid");
    }

    const StorageCatalogOperation completed = operation->second;
    m_operations.erase(operation);
    rememberCompletedOperation(completed.operationId);
    if (!succeeded) {
        object->second.publicationStage =
            StorageCatalogPublicationStage::Failed;
        return transition(true, "upload-failed");
    }
    object->second.cid = cid;
    object->second.publicationStage =
        StorageCatalogPublicationStage::VerifyingLocal;
    return transition(true, "upload-awaiting-local-verification");
}

StorageCatalogTransition PalaceStorageCatalogSession::downloadFinished(
    const StorageCatalogDownloadTerminalV2& terminal,
    const std::string& fullBytes)
{
    if (!canAct())
        return transition(false, "restart-reconciliation-required");
    if (terminal.protocol != kDownloadProtocol
        || terminal.version != kDownloadProtocolVersion
        || !validDownloadOutcome(terminal.outcome)) {
        return transition(false, "download-protocol-mismatch");
    }
    if (wasOperationCompleted(terminal.operationId))
        return transition(false, "operation-already-completed");
    const auto operation = m_operations.find(terminal.operationId);
    if (operation == m_operations.end()
        || operation->second.kind == StorageCatalogOperationKind::Upload
        || terminal.cid != operation->second.cid
        || terminal.operationId == terminal.cid) {
        return transition(false, "download-operation-or-cid-mismatch");
    }
    if (operation->second.phase
        != StorageCatalogOperationPhase::Active) {
        return transition(false, "operation-not-acknowledged");
    }
    const auto object = m_objects.find(operation->second.objectId);
    if (object == m_objects.end() || object->second.cid != terminal.cid)
        return transition(false, "download-object-mismatch");

    const StorageCatalogOperation completed = operation->second;
    m_operations.erase(operation);
    rememberCompletedOperation(completed.operationId);
    if (terminal.outcome != StorageCatalogDownloadOutcome::Succeeded) {
        if (completed.kind
            == StorageCatalogOperationKind::PublicationVerification) {
            object->second.publicationStage =
                StorageCatalogPublicationStage::Failed;
            object->second.localPhase = StorageCatalogLocalPhase::Missing;
        } else {
            object->second.localPhase = StorageCatalogLocalPhase::Missing;
        }
        return transition(
            true,
            terminal.outcome == StorageCatalogDownloadOutcome::Canceled
                ? "download-canceled" : "download-failed");
    }
    if (object->second.bootstrapAdmissionPending) {
        PalaceStorageCatalogSession admitted = *this;
        const StorageCatalogTransition admission =
            admitted.admitBootstrapRetrievedObject(
                completed.objectId, fullBytes);
        ObjectRecord& admittedObject =
            admitted.m_objects.at(completed.objectId);
        if (!admission.accepted) {
            admittedObject.localPhase = StorageCatalogLocalPhase::Missing;
            if (!admitted.validState())
                return transition(false, "bootstrap-rejection-state-invalid");
            *this = std::move(admitted);
            return admission;
        }
        admittedObject.localPhase = StorageCatalogLocalPhase::Verified;
        if (!admitted.validState())
            return transition(false, "bootstrap-admission-state-invalid");
        *this = std::move(admitted);
        return transition(true, "bootstrap-object-admitted");
    }
    if (!verifyFullBytes(object->second.specification, fullBytes)) {
        if (completed.kind
            == StorageCatalogOperationKind::PublicationVerification) {
            object->second.publicationStage =
                StorageCatalogPublicationStage::Failed;
        }
        object->second.localPhase = StorageCatalogLocalPhase::Missing;
        return transition(false, "retrieved-object-verification-failed");
    }
    if (completed.kind
        == StorageCatalogOperationKind::PublicationVerification) {
        object->second.localPhase = StorageCatalogLocalPhase::Verified;
        object->second.publicationStage =
            StorageCatalogPublicationStage::Published;
        return transition(true, "object-published");
    }
    object->second.localPhase = StorageCatalogLocalPhase::Verified;
    return transition(true, "local-object-verified");
}

StorageCatalogTransition PalaceStorageCatalogSession::retryOperation(
    const std::string& operationId)
{
    if (!canAct())
        return transition(false, "restart-reconciliation-required");
    if (wasOperationCompleted(operationId))
        return transition(false, "operation-already-completed");
    const auto operation = m_operations.find(operationId);
    if (operation == m_operations.end())
        return transition(false, "operation-not-found");
    if (m_lastOperationSequence
        == std::numeric_limits<std::uint64_t>::max()) {
        return transition(false, "operation-id-exhausted");
    }
    PalaceStorageCatalogSession retried = *this;
    const StorageCatalogOperation previous =
        retried.m_operations.at(operationId);
    ObjectRecord& object = retried.m_objects.at(previous.objectId);
    retried.m_operations.erase(operationId);
    retried.rememberCompletedOperation(previous.operationId);
    switch (previous.kind) {
    case StorageCatalogOperationKind::Upload:
        object.localPhase = StorageCatalogLocalPhase::Verified;
        object.publicationStage = StorageCatalogPublicationStage::Staged;
        object.cid.clear();
        break;
    case StorageCatalogOperationKind::PublicationVerification:
        object.localPhase = StorageCatalogLocalPhase::Missing;
        object.publicationStage =
            StorageCatalogPublicationStage::VerifyingLocal;
        break;
    case StorageCatalogOperationKind::LocalFetch:
        object.localPhase = StorageCatalogLocalPhase::Missing;
        break;
    }
    const std::optional<StorageCatalogOperation> replacement =
        retried.enqueueReplacement(previous);
    if (!replacement.has_value() || !retried.validState())
        return transition(false, "operation-retry-state-invalid");
    *this = std::move(retried);
    return operationTransition("operation-retried", *replacement);
}

StorageCatalogTransition PalaceStorageCatalogSession::abandonOperation(
    const std::string& operationId)
{
    if (!canAct())
        return transition(false, "restart-reconciliation-required");
    if (wasOperationCompleted(operationId))
        return transition(false, "operation-already-completed");
    const auto operation = m_operations.find(operationId);
    if (operation == m_operations.end())
        return transition(false, "operation-not-found");
    const StorageCatalogOperation abandoned = operation->second;
    m_operations.erase(operation);
    settleAbandonedOperation(abandoned);
    rememberCompletedOperation(abandoned.operationId);
    return transition(true, "operation-abandoned");
}

StorageCatalogTransition PalaceStorageCatalogSession::retryPublication(
    const std::string& objectId,
    const std::string& fullBytes)
{
    if (!canAct())
        return transition(false, "restart-reconciliation-required");
    const auto object = m_objects.find(objectId);
    if (object == m_objects.end())
        return transition(false, "object-not-found");
    if (object->second.publicationStage
            != StorageCatalogPublicationStage::Failed
        || hasObjectOperation(objectId)
        || !childrenPublished(object->second)) {
        return transition(false, "object-not-retryable");
    }
    if (!verifyFullBytes(object->second.specification, fullBytes))
        return transition(false, "local-object-verification-failed");
    object->second.localPhase = StorageCatalogLocalPhase::Verified;
    object->second.publicationStage =
        StorageCatalogPublicationStage::Staged;
    object->second.cid.clear();
    object->second.attestations.clear();
    return transition(true, "object-restaged");
}

StorageCatalogTransition PalaceStorageCatalogSession::markLocalMissing(
    const std::string& objectId)
{
    if (!canAct())
        return transition(false, "restart-reconciliation-required");
    const auto object = m_objects.find(objectId);
    if (object == m_objects.end())
        return transition(false, "object-not-found");
    if (object->second.publicationStage
            != StorageCatalogPublicationStage::Published
        || hasObjectOperation(
            objectId, StorageCatalogOperationKind::LocalFetch)) {
        return transition(false, "local-state-not-changeable");
    }
    object->second.localPhase = StorageCatalogLocalPhase::Missing;
    return transition(true, "local-object-missing");
}

StorageCatalogChallengeTransition
PalaceStorageCatalogSession::beginHolderAttestation(
    const std::string& objectId,
    const std::string& holderAccountId,
    std::uint64_t holderKeyEpoch,
    std::uint64_t issuedAtUnixSeconds,
    std::uint64_t expiresAtUnixSeconds)
{
    if (!canAct())
        return {false, "restart-reconciliation-required", std::nullopt};
    pruneExpiredAttestations(issuedAtUnixSeconds);
    pruneExpiredChallenges(issuedAtUnixSeconds);
    const auto object = m_objects.find(objectId);
    if (object == m_objects.end())
        return {false, "object-not-found", std::nullopt};
    if (object->second.publicationStage
            != StorageCatalogPublicationStage::Published
        || object->second.bootstrapAdmissionPending
        || !isHolderAccountId(holderAccountId)
        || holderAccountId == m_config.localHolderAccountId
        || holderKeyEpoch == 0U
        || expiresAtUnixSeconds <= issuedAtUnixSeconds
        || expiresAtUnixSeconds - issuedAtUnixSeconds
            > m_config.maxChallengeLifetimeSeconds) {
        return {false, "invalid-attestation-challenge", std::nullopt};
    }
    if (m_challenges.size()
        >= m_config.maxPendingAttestationChallenges) {
        return {false, "attestation-challenge-capacity-exceeded",
                std::nullopt};
    }
    if (object->second.attestations.find(holderAccountId)
            != object->second.attestations.end()
        || object->second.attestations.size()
                + challengeReservations(objectId)
            >= m_config.maxAttestationsPerObject) {
        return {false, "holder-attestation-not-independent",
                std::nullopt};
    }
    for (const auto& active : m_challenges) {
        if (active.second.objectId == objectId
            && active.second.holderAccountId == holderAccountId) {
            return {false, "attestation-challenge-already-active",
                    std::nullopt};
        }
    }
    const std::optional<std::string> challengeId = generateChallengeId();
    if (!challengeId.has_value())
        return {false, "challenge-id-exhausted", std::nullopt};

    StorageCatalogHolderChallengeV1 challenge;
    challenge.challengeId = *challengeId;
    challenge.holderAccountId = holderAccountId;
    challenge.holderKeyEpoch = holderKeyEpoch;
    challenge.objectId = objectId;
    challenge.cid = object->second.cid;
    challenge.byteLength = object->second.specification.byteLength;
    challenge.contentSha256 =
        object->second.specification.contentSha256;
    challenge.issuedAtUnixSeconds = issuedAtUnixSeconds;
    challenge.expiresAtUnixSeconds = expiresAtUnixSeconds;
    m_challenges.emplace(challenge.challengeId, challenge);
    return challengeTransition(
        "attestation-challenge-issued", std::move(challenge));
}

StorageCatalogTransition
PalaceStorageCatalogSession::completeHolderAttestation(
    const StorageCatalogHolderAttestationReceiptV1& receipt,
    const std::string& signature,
    std::uint64_t nowUnixSeconds,
    const StorageCatalogAttestationVerifier& verifier)
{
    if (!canAct())
        return transition(false, "restart-reconciliation-required");
    if (wasChallengeCompleted(receipt.challengeId))
        return transition(false, "attestation-challenge-already-completed");
    const auto challenge = m_challenges.find(receipt.challengeId);
    if (challenge == m_challenges.end())
        return transition(false, "attestation-challenge-not-found");

    const StorageCatalogHolderChallengeV1 expected = challenge->second;
    auto failSettled = [this, &expected](const char* reason) {
        settleChallenge(expected.challengeId);
        return transition(false, reason);
    };
    if (!validReceiptShape(receipt)
        || !receiptMatchesChallenge(receipt, expected)) {
        return failSettled("attestation-receipt-mismatch");
    }
    if (nowUnixSeconds < expected.issuedAtUnixSeconds)
        return failSettled("attestation-receipt-not-yet-valid");
    if (nowUnixSeconds > expected.expiresAtUnixSeconds)
        return failSettled("attestation-receipt-expired");
    if (signature.empty()
        || signature.size() > m_config.maxSignatureBytes) {
        return failSettled("attestation-signature-invalid");
    }
    const std::string canonicalReceipt =
        canonicalHolderAttestationReceiptV1(receipt);
    if (canonicalReceipt.empty()
        || !verifier.verify(
            receipt.holderAccountId,
            receipt.holderKeyEpoch,
            canonicalReceipt,
            signature)) {
        return failSettled("attestation-signature-invalid");
    }
    const auto object = m_objects.find(expected.objectId);
    if (object == m_objects.end()
        || object->second.publicationStage
            != StorageCatalogPublicationStage::Published
        || object->second.cid != expected.cid) {
        return failSettled("attestation-object-state-mismatch");
    }
    pruneExpiredAttestations(nowUnixSeconds);
    AttestationRecord attestation;
    attestation.holderKeyEpoch = expected.holderKeyEpoch;
    attestation.issuedAtUnixSeconds = expected.issuedAtUnixSeconds;
    attestation.expiresAtUnixSeconds = expected.expiresAtUnixSeconds;
    if (!object->second.attestations.emplace(
            expected.holderAccountId, attestation).second) {
        return failSettled("attestation-already-recorded");
    }
    settleChallenge(expected.challengeId);
    return transition(true, "holder-attestation-recorded");
}

StorageCatalogTransition
PalaceStorageCatalogSession::abandonHolderAttestation(
    const std::string& challengeId)
{
    if (!canAct())
        return transition(false, "restart-reconciliation-required");
    if (wasChallengeCompleted(challengeId))
        return transition(false, "attestation-challenge-already-completed");
    if (m_challenges.find(challengeId) == m_challenges.end())
        return transition(false, "attestation-challenge-not-found");
    settleChallenge(challengeId);
    return transition(true, "attestation-challenge-abandoned");
}

bool PalaceStorageCatalogSession::revokeHolderAttestation(
    const std::string& objectId,
    const std::string& holderAccountId)
{
    if (!canAct() || !isHolderAccountId(holderAccountId))
        return false;
    const auto object = m_objects.find(objectId);
    return object != m_objects.end()
        && object->second.attestations.erase(holderAccountId) == 1U;
}

StorageCatalogObjectStatus PalaceStorageCatalogSession::status(
    const std::string& objectId,
    std::uint64_t nowUnixSeconds) const
{
    StorageCatalogObjectStatus result;
    result.reconciliationRequired = m_reconciliationRequired;
    const auto object = m_objects.find(objectId);
    if (object == m_objects.end())
        return result;
    result.found = true;
    result.specificationAdmitted =
        !object->second.bootstrapAdmissionPending;
    result.specification = object->second.specification;
    result.publicationStage = object->second.publicationStage;
    result.cid = object->second.cid;
    if (m_reconciliationRequired) {
        result.localPhase = StorageCatalogLocalPhase::Missing;
        if (result.publicationStage
                == StorageCatalogPublicationStage::Staged
            || result.publicationStage
                == StorageCatalogPublicationStage::Uploading) {
            result.publicationStage =
                StorageCatalogPublicationStage::Failed;
            result.cid.clear();
        }
        result.retention = StorageCatalogRetention::Unknown;
        return result;
    }
    result.localPhase = object->second.localPhase;
    for (const auto& attestation : object->second.attestations) {
        if (nowUnixSeconds >= attestation.second.issuedAtUnixSeconds
            && nowUnixSeconds <= attestation.second.expiresAtUnixSeconds) {
            result.attestedHolderIds.insert(attestation.first);
        }
    }
    result.retention = retentionFor(
        result.localPhase == StorageCatalogLocalPhase::Verified,
        result.attestedHolderIds.size());
    return result;
}

std::vector<StorageCatalogOperation>
PalaceStorageCatalogSession::pendingOperations() const
{
    if (m_reconciliationRequired)
        return {};
    std::vector<StorageCatalogOperation> operations;
    operations.reserve(m_operations.size());
    for (const auto& operation : m_operations)
        operations.push_back(operation.second);
    return operations;
}

std::vector<StorageCatalogHolderChallengeV1>
PalaceStorageCatalogSession::pendingAttestationChallenges() const
{
    if (m_reconciliationRequired)
        return {};
    std::vector<StorageCatalogHolderChallengeV1> challenges;
    challenges.reserve(m_challenges.size());
    for (const auto& challenge : m_challenges)
        challenges.push_back(challenge.second);
    return challenges;
}

std::vector<std::string>
PalaceStorageCatalogSession::completedOperationIds() const
{
    return {
        m_completedOperationIds.begin(),
        m_completedOperationIds.end(),
    };
}

std::vector<std::string>
PalaceStorageCatalogSession::completedChallengeIds() const
{
    return {
        m_completedChallengeIds.begin(),
        m_completedChallengeIds.end(),
    };
}

StorageCatalogReconciliation
PalaceStorageCatalogSession::reconcileAfterRestart(
    std::uint64_t newSessionEpoch)
{
    if (!m_configured || !validState())
        return {false, "session-state-invalid", {}};
    if (newSessionEpoch <= m_sessionEpoch) {
        return {
            false,
            m_reconciliationRequired
                ? "restart-epoch-must-be-strictly-newer"
                : "session-epoch-not-increasing",
            {},
        };
    }

    PalaceStorageCatalogSession reconciled = *this;
    std::vector<StorageCatalogOperation> previousOperations;
    previousOperations.reserve(reconciled.m_operations.size());
    for (const auto& operation : reconciled.m_operations)
        previousOperations.push_back(operation.second);
    reconciled.m_operations.clear();
    for (const StorageCatalogOperation& operation : previousOperations)
        reconciled.rememberCompletedOperation(operation.operationId);

    std::vector<std::string> previousChallenges;
    previousChallenges.reserve(reconciled.m_challenges.size());
    for (const auto& challenge : reconciled.m_challenges)
        previousChallenges.push_back(challenge.first);
    reconciled.m_challenges.clear();
    for (const std::string& challengeId : previousChallenges)
        reconciled.rememberCompletedChallenge(challengeId);

    for (auto& object : reconciled.m_objects) {
        ObjectRecord& value = object.second;
        value.attestations.clear();
        value.localPhase = StorageCatalogLocalPhase::Missing;
        switch (value.publicationStage) {
        case StorageCatalogPublicationStage::Staged:
        case StorageCatalogPublicationStage::Uploading:
            value.publicationStage =
                StorageCatalogPublicationStage::Failed;
            value.cid.clear();
            break;
        case StorageCatalogPublicationStage::VerifyingLocal:
        case StorageCatalogPublicationStage::Published:
        case StorageCatalogPublicationStage::Failed:
            break;
        }
    }

    reconciled.m_sessionEpoch = newSessionEpoch;
    reconciled.m_lastOperationSequence = 0U;
    reconciled.m_lastChallengeSequence = 0U;
    reconciled.m_reconciliationRequired = false;
    std::vector<StorageCatalogOperation> requeued;
    for (const StorageCatalogOperation& previous : previousOperations) {
        if (previous.kind == StorageCatalogOperationKind::Upload)
            continue;
        const std::optional<StorageCatalogOperation> replacement =
            reconciled.enqueueReplacement(previous);
        if (!replacement.has_value())
            return {false, "restart-requeue-failed", {}};
        requeued.push_back(*replacement);
    }
    if (!reconciled.validState())
        return {false, "restart-reconciliation-invalid", {}};
    *this = std::move(reconciled);
    return {true, "restart-reconciled", std::move(requeued)};
}

std::string PalaceStorageCatalogSession::canonicalState() const
{
    if (!m_configured || !validState())
        return {};

    std::ostringstream body;
    body << "version=3\n"
         << "config;" << hexEncode(m_config.localHolderAccountId)
         << ';' << m_config.maxObjects
         << ';' << m_config.maxOperations
         << ';' << m_config.maxChildrenPerObject
         << ';' << m_config.maxAttestationsPerObject
         << ';' << m_config.maxPendingAttestationChallenges
         << ';' << m_config.maxCompletedOperations
         << ';' << m_config.maxCompletedChallenges
         << ';' << m_config.maxSignatureBytes
         << ';' << m_config.maxChallengeLifetimeSeconds
         << ';' << m_config.maxObjectBytes
         << ';' << m_config.initialSessionEpoch << '\n'
         << "state;" << m_sessionEpoch
         << ';' << m_lastOperationSequence
         << ';' << m_lastChallengeSequence << '\n';

    std::vector<const std::pair<const std::string, ObjectRecord>*> objects;
    objects.reserve(m_objects.size());
    for (const auto& object : m_objects)
        objects.push_back(&object);
    std::sort(
        objects.begin(),
        objects.end(),
        [](const auto* left, const auto* right) {
            if (left->second.specification.kind
                != right->second.specification.kind) {
                return left->second.specification.kind
                    < right->second.specification.kind;
            }
            return left->first < right->first;
        });

    for (const auto* object : objects) {
        body << "object;" << hexEncode(object->first)
             << ';' << static_cast<unsigned int>(
                    object->second.specification.kind)
             << ';' << object->second.specification.byteLength
             << ';' << object->second.specification.contentSha256
             << ';' << encodeChildIds(
                    object->second.specification.children)
             << ';' << static_cast<unsigned int>(
                    object->second.localPhase)
             << ';' << static_cast<unsigned int>(
                    object->second.publicationStage)
             << ';' << encodeOptional(object->second.cid)
             << ';' << (object->second.bootstrapAdmissionPending ? '1' : '0')
             << '\n';
    }
    for (const auto* object : objects) {
        for (const auto& attestation : object->second.attestations) {
            body << "attestation;" << hexEncode(object->first)
                 << ';' << attestation.first
                 << ';' << attestation.second.holderKeyEpoch
                 << ';' << attestation.second.issuedAtUnixSeconds
                 << ';' << attestation.second.expiresAtUnixSeconds
                 << '\n';
        }
    }
    for (const auto& operation : m_operations) {
        body << "operation;" << hexEncode(operation.first)
             << ';' << static_cast<unsigned int>(operation.second.kind)
             << ';' << static_cast<unsigned int>(operation.second.phase)
             << ';' << hexEncode(operation.second.objectId)
             << ';' << encodeOptional(operation.second.cid)
             << ';' << (operation.second.localOnly ? '1' : '0')
             << ';' << operation.second.maxBytes << '\n';
    }
    for (const auto& challenge : m_challenges) {
        body << "challenge;" << challenge.second.version
             << ';' << hexEncode(challenge.first)
             << ';' << challenge.second.holderAccountId
             << ';' << challenge.second.holderKeyEpoch
             << ';' << hexEncode(challenge.second.objectId)
             << ';' << challenge.second.cid
             << ';' << challenge.second.byteLength
             << ';' << challenge.second.contentSha256
             << ';' << challenge.second.issuedAtUnixSeconds
             << ';' << challenge.second.expiresAtUnixSeconds
             << '\n';
    }
    for (const std::string& operationId : m_completedOperationIds)
        body << "completed_operation;" << hexEncode(operationId) << '\n';
    for (const std::string& challengeId : m_completedChallengeIds)
        body << "completed_challenge;" << hexEncode(challengeId) << '\n';

    const std::string canonicalBody = body.str();
    const std::string serialized = canonicalBody + "checksum="
        + crypto::sha256Hex(canonicalBody) + '\n';
    return serialized.size() <= kMaximumStateBytes
        ? serialized : std::string{};
}

bool PalaceStorageCatalogSession::restoreCanonicalState(
    const std::string& serialized)
{
    static constexpr const char* kPrefix = "version=3\n";
    static constexpr const char* kChecksumMarker = "checksum=";
    if (!m_configured || !validConfig(m_config)
        || serialized.empty() || serialized.size() > kMaximumStateBytes
        || serialized.rfind(kPrefix, 0U) != 0U
        || serialized.back() != '\n') {
        return false;
    }
    const std::size_t checksumPosition = serialized.rfind(kChecksumMarker);
    if (checksumPosition == std::string::npos
        || (checksumPosition != 0U
            && serialized[checksumPosition - 1U] != '\n')) {
        return false;
    }
    const std::string body = serialized.substr(0U, checksumPosition);
    const std::string checksum = serialized.substr(
        checksumPosition + std::char_traits<char>::length(kChecksumMarker),
        serialized.size() - checksumPosition
            - std::char_traits<char>::length(kChecksumMarker) - 1U);
    if (checksum.size() != 64U || checksum != crypto::sha256Hex(body))
        return false;

    PalaceStorageCatalogSession restored;
    if (!restored.configure(m_config))
        return false;
    bool sawConfig = false;
    bool sawState = false;
    unsigned int section = 0U;
    std::istringstream input(
        body.substr(std::char_traits<char>::length(kPrefix)));
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty())
            return false;
        const std::vector<std::string> fields = split(line, ';', 13U);
        if (fields.empty())
            return false;

        if (fields[0] == "config") {
            if (sawConfig || sawState || fields.size() != 13U)
                return false;
            StorageCatalogSessionConfigV3 parsedConfig;
            std::uint64_t maxObjects = 0U;
            std::uint64_t maxOperations = 0U;
            std::uint64_t maxChildren = 0U;
            std::uint64_t maxAttestations = 0U;
            std::uint64_t maxChallenges = 0U;
            std::uint64_t maxCompletedOperations = 0U;
            std::uint64_t maxCompletedChallenges = 0U;
            std::uint64_t maxSignatureBytes = 0U;
            if (!hexDecode(
                    fields[1], parsedConfig.localHolderAccountId)
                || !parseInteger(fields[2], maxObjects)
                || !parseInteger(fields[3], maxOperations)
                || !parseInteger(fields[4], maxChildren)
                || !parseInteger(fields[5], maxAttestations)
                || !parseInteger(fields[6], maxChallenges)
                || !parseInteger(
                    fields[7], maxCompletedOperations)
                || !parseInteger(
                    fields[8], maxCompletedChallenges)
                || !parseInteger(fields[9], maxSignatureBytes)
                || !parseInteger(
                    fields[10],
                    parsedConfig.maxChallengeLifetimeSeconds)
                || !parseInteger(
                    fields[11], parsedConfig.maxObjectBytes)
                || !parseInteger(
                    fields[12], parsedConfig.initialSessionEpoch)
                || maxObjects > std::numeric_limits<std::size_t>::max()
                || maxOperations
                    > std::numeric_limits<std::size_t>::max()
                || maxChildren > std::numeric_limits<std::size_t>::max()
                || maxAttestations
                    > std::numeric_limits<std::size_t>::max()
                || maxChallenges
                    > std::numeric_limits<std::size_t>::max()
                || maxCompletedOperations
                    > std::numeric_limits<std::size_t>::max()
                || maxCompletedChallenges
                    > std::numeric_limits<std::size_t>::max()
                || maxSignatureBytes
                    > std::numeric_limits<std::size_t>::max()) {
                return false;
            }
            parsedConfig.maxObjects =
                static_cast<std::size_t>(maxObjects);
            parsedConfig.maxOperations =
                static_cast<std::size_t>(maxOperations);
            parsedConfig.maxChildrenPerObject =
                static_cast<std::size_t>(maxChildren);
            parsedConfig.maxAttestationsPerObject =
                static_cast<std::size_t>(maxAttestations);
            parsedConfig.maxPendingAttestationChallenges =
                static_cast<std::size_t>(maxChallenges);
            parsedConfig.maxCompletedOperations =
                static_cast<std::size_t>(maxCompletedOperations);
            parsedConfig.maxCompletedChallenges =
                static_cast<std::size_t>(maxCompletedChallenges);
            parsedConfig.maxSignatureBytes =
                static_cast<std::size_t>(maxSignatureBytes);
            if (!validConfig(parsedConfig)
                || !sameConfig(m_config, parsedConfig)) {
                return false;
            }
            sawConfig = true;
            continue;
        }

        if (fields[0] == "state") {
            if (!sawConfig || sawState || fields.size() != 4U
                || !parseInteger(fields[1], restored.m_sessionEpoch)
                || !parseInteger(
                    fields[2], restored.m_lastOperationSequence)
                || !parseInteger(
                    fields[3], restored.m_lastChallengeSequence)) {
                return false;
            }
            sawState = true;
            continue;
        }
        if (!sawConfig || !sawState)
            return false;

        if (fields[0] == "object") {
            if (section > 0U || fields.size() != 10U
                || restored.m_objects.size()
                    >= restored.m_config.maxObjects) {
                return false;
            }
            ObjectRecord object;
            std::vector<std::string> childIds;
            std::string bootstrapPending;
            if (!hexDecode(fields[1], object.specification.objectId)
                || !parseObjectKind(fields[2], object.specification.kind)
                || !parseInteger(
                    fields[3], object.specification.byteLength)
                || !isLowerHexDigest(fields[4])
                || !decodeChildIds(
                    fields[5],
                    restored.m_config.maxChildrenPerObject,
                    childIds)
                || !parseLocalPhase(fields[6], object.localPhase)
                || !parsePublicationStage(
                    fields[7], object.publicationStage)
                || !decodeOptional(fields[8], object.cid)
                || ((bootstrapPending = fields[9]) != "0"
                    && bootstrapPending != "1")) {
                return false;
            }
            object.bootstrapAdmissionPending =
                bootstrapPending == "1";
            object.specification.contentSha256 = fields[4];
            for (const std::string& childId : childIds) {
                const auto child = restored.m_objects.find(childId);
                if (child == restored.m_objects.end())
                    return false;
                StorageCatalogManifestChildV1 childRecord;
                childRecord.objectId = childId;
                childRecord.cid = child->second.cid;
                childRecord.byteLength =
                    child->second.specification.byteLength;
                childRecord.contentSha256 =
                    child->second.specification.contentSha256;
                object.specification.children.push_back(
                    std::move(childRecord));
            }
            if ((!object.bootstrapAdmissionPending
                    && !restored.validSpecification(
                        object.specification))
                || (!object.cid.empty()
                    && !isCanonicalStorageCid(object.cid))
                || !restored.m_objects.emplace(
                    object.specification.objectId,
                    std::move(object)).second) {
                return false;
            }
            continue;
        }

        if (fields[0] == "attestation") {
            if (section > 1U || fields.size() != 6U) {
                return false;
            }
            section = 1U;
            std::string objectId;
            std::string holderAccountId;
            AttestationRecord attestation;
            if (!hexDecode(fields[1], objectId)
                || !isHolderAccountId(
                    (holderAccountId = fields[2]))
                || !parseInteger(
                    fields[3], attestation.holderKeyEpoch)
                || !parseInteger(
                    fields[4], attestation.issuedAtUnixSeconds)
                || !parseInteger(
                    fields[5], attestation.expiresAtUnixSeconds)) {
                return false;
            }
            const auto object = restored.m_objects.find(objectId);
            if (object == restored.m_objects.end()
                || !object->second.attestations.emplace(
                    holderAccountId, attestation).second) {
                return false;
            }
            continue;
        }

        if (fields[0] == "operation") {
            if (section > 2U || fields.size() != 8U
                || restored.m_operations.size()
                    >= restored.m_config.maxOperations) {
                return false;
            }
            section = 2U;
            StorageCatalogOperation operation;
            std::string localOnly;
            if (!hexDecode(fields[1], operation.operationId)
                || !parseOperationKind(fields[2], operation.kind)
                || !parseOperationPhase(fields[3], operation.phase)
                || !hexDecode(fields[4], operation.objectId)
                || !decodeOptional(fields[5], operation.cid)
                || ((localOnly = fields[6]) != "0"
                    && localOnly != "1")
                || !parseInteger(fields[7], operation.maxBytes)
                || !restored.m_operations.emplace(
                    operation.operationId, operation).second) {
                return false;
            }
            restored.m_operations.at(
                operation.operationId).localOnly = localOnly == "1";
            continue;
        }

        if (fields[0] == "challenge") {
            if (section > 3U || fields.size() != 11U
                || restored.m_challenges.size()
                    >= restored.m_config
                        .maxPendingAttestationChallenges) {
                return false;
            }
            section = 3U;
            StorageCatalogHolderChallengeV1 challenge;
            if (!parseInteger(fields[1], challenge.version)
                || !hexDecode(fields[2], challenge.challengeId)
                || !isHolderAccountId(
                    (challenge.holderAccountId = fields[3]))
                || !parseInteger(
                    fields[4], challenge.holderKeyEpoch)
                || !hexDecode(fields[5], challenge.objectId)
                || !isCanonicalStorageCid(
                    (challenge.cid = fields[6]))
                || !parseInteger(fields[7], challenge.byteLength)
                || !isLowerHexDigest(
                    (challenge.contentSha256 = fields[8]))
                || !parseInteger(
                    fields[9], challenge.issuedAtUnixSeconds)
                || !parseInteger(
                    fields[10], challenge.expiresAtUnixSeconds)
                || !restored.m_challenges.emplace(
                    challenge.challengeId, challenge).second) {
                return false;
            }
            continue;
        }

        if (fields[0] == "completed_operation") {
            if (section > 4U || fields.size() != 2U
                || restored.m_completedOperationIds.size()
                    >= restored.m_config.maxCompletedOperations) {
                return false;
            }
            section = 4U;
            std::string operationId;
            if (!hexDecode(fields[1], operationId)
                || !restored.m_completedOperationIndex
                        .insert(operationId).second) {
                return false;
            }
            restored.m_completedOperationIds.push_back(
                std::move(operationId));
            continue;
        }

        section = 5U;
        if (fields[0] != "completed_challenge"
            || fields.size() != 2U
            || restored.m_completedChallengeIds.size()
                >= restored.m_config.maxCompletedChallenges) {
            return false;
        }
        std::string challengeId;
        if (!hexDecode(fields[1], challengeId)
            || !restored.m_completedChallengeIndex
                    .insert(challengeId).second) {
            return false;
        }
        restored.m_completedChallengeIds.push_back(
            std::move(challengeId));
    }

    if (!sawConfig || !sawState || !restored.validState()
        || restored.canonicalState() != serialized) {
        return false;
    }
    *this = std::move(restored);
    m_reconciliationRequired = true;
    return true;
}

bool PalaceStorageCatalogSession::canAct() const
{
    return m_configured && !m_reconciliationRequired;
}

bool PalaceStorageCatalogSession::validSpecification(
    const StorageCatalogObjectSpecV2& specification) const
{
    if (!validObjectKind(specification.kind)
        || !isObjectId(specification.objectId)
        || specification.byteLength == 0U
        || specification.byteLength > m_config.maxObjectBytes
        || !isLowerHexDigest(specification.contentSha256)
        || specification.children.size()
            > m_config.maxChildrenPerObject) {
        return false;
    }
    if (specification.kind == StorageCatalogObjectKind::Blob)
        return specification.children.empty();
    if (!canonicalChildren(specification.children))
        return false;

    for (const StorageCatalogManifestChildV1& child :
         specification.children) {
        const auto found = m_objects.find(child.objectId);
        if (found == m_objects.end()
            || found->second.publicationStage
                != StorageCatalogPublicationStage::Published
            || !validDagEdge(
                specification.kind,
                found->second.specification.kind)
            || child.cid != found->second.cid
            || child.byteLength
                != found->second.specification.byteLength
            || child.contentSha256
                != found->second.specification.contentSha256) {
            return false;
        }
    }
    return true;
}

bool PalaceStorageCatalogSession::verifyFullBytes(
    const StorageCatalogObjectSpecV2& specification,
    const std::string& fullBytes) const
{
    if (fullBytes.size() != specification.byteLength
        || fullBytes.size() > m_config.maxObjectBytes
        || crypto::sha256Hex(fullBytes)
            != specification.contentSha256) {
        return false;
    }
    if (specification.kind == StorageCatalogObjectKind::Blob)
        return specification.children.empty();

    ParsedManifest manifest;
    if (!parseCanonicalManifest(fullBytes, manifest)
        || manifest.kind != specification.kind
        || manifest.objectId != specification.objectId
        || manifest.children.size() != specification.children.size()) {
        return false;
    }
    for (std::size_t index = 0U;
         index < manifest.children.size();
         ++index) {
        if (!sameChild(
                manifest.children[index],
                specification.children[index])) {
            return false;
        }
    }
    return true;
}

StorageCatalogTransition
PalaceStorageCatalogSession::admitBootstrapRetrievedObject(
    const std::string& objectId,
    const std::string& fullBytes)
{
    const auto object = m_objects.find(objectId);
    if (object == m_objects.end()
        || !object->second.bootstrapAdmissionPending) {
        return transition(false, "bootstrap-object-not-pending");
    }
    const ObjectRecord& committed = object->second;
    if (fullBytes.empty()
        || fullBytes.size() != committed.specification.byteLength
        || fullBytes.size() > m_config.maxObjectBytes
        || crypto::sha256Hex(fullBytes)
            != committed.specification.contentSha256
        || !isCanonicalStorageCid(committed.cid)) {
        return transition(false, "bootstrap-object-verification-failed");
    }

    ParsedManifest parsed;
    const bool isManifest = parseCanonicalManifest(fullBytes, parsed);
    StorageCatalogObjectKind admittedKind =
        StorageCatalogObjectKind::Blob;
    if (committed.specification.kind
        == StorageCatalogObjectKind::RoomManifest) {
        if (!isManifest
            || parsed.kind != StorageCatalogObjectKind::RoomManifest) {
            return transition(false, "bootstrap-child-kind-mismatch");
        }
        admittedKind = parsed.kind;
    } else if (isManifest) {
        admittedKind = parsed.kind;
    } else if (fullBytes.rfind(kManifestHeader, 0U) == 0U) {
        return transition(false, "bootstrap-child-manifest-invalid");
    }
    if (isManifest && parsed.objectId != objectId)
        return transition(false, "bootstrap-child-parent-mismatch");

    bool referenced = false;
    for (const auto& parentEntry : m_objects) {
        const ObjectRecord& parent = parentEntry.second;
        if (parent.bootstrapAdmissionPending)
            continue;
        for (const StorageCatalogManifestChildV1& child :
             parent.specification.children) {
            if (child.objectId != objectId)
                continue;
            referenced = true;
            if (child.cid != committed.cid
                || child.byteLength
                    != committed.specification.byteLength
                || child.contentSha256
                    != committed.specification.contentSha256
                || !validDagEdge(
                    parent.specification.kind, admittedKind)) {
                return transition(
                    false, "bootstrap-child-parent-mismatch");
            }
        }
    }
    if (!referenced)
        return transition(false, "bootstrap-child-parent-missing");

    std::vector<StorageCatalogManifestChildV1> admittedChildren;
    if (admittedKind != StorageCatalogObjectKind::Blob) {
        if (!isManifest
            || parsed.children.size() > m_config.maxChildrenPerObject) {
            return transition(false, "bootstrap-child-manifest-invalid");
        }
        admittedChildren = parsed.children;
    }

    std::size_t newObjects = 0U;
    for (const StorageCatalogManifestChildV1& child :
         admittedChildren) {
        if (child.objectId == objectId
            || objectHasPath(child.objectId, objectId)) {
            return transition(false, "bootstrap-manifest-cycle");
        }
        if (child.byteLength > m_config.maxObjectBytes
            || !isCanonicalStorageCid(child.cid)) {
            return transition(
                false, "bootstrap-child-commitment-invalid");
        }
        const auto existing = m_objects.find(child.objectId);
        if (existing == m_objects.end()) {
            ++newObjects;
            continue;
        }
        const ObjectRecord& existingObject = existing->second;
        if (existingObject.publicationStage
                != StorageCatalogPublicationStage::Published
            || existingObject.cid != child.cid
            || existingObject.specification.byteLength
                != child.byteLength
            || existingObject.specification.contentSha256
                != child.contentSha256
            || !validDagEdge(
                admittedKind,
                existingObject.specification.kind)) {
            return transition(
                false, "bootstrap-child-parent-mismatch");
        }
    }
    if (newObjects > m_config.maxObjects - std::min(
            m_config.maxObjects, m_objects.size())) {
        return transition(false, "object-capacity-exceeded");
    }

    PalaceStorageCatalogSession updated = *this;
    for (const StorageCatalogManifestChildV1& child :
         admittedChildren) {
        if (updated.m_objects.find(child.objectId)
            != updated.m_objects.end()) {
            continue;
        }
        ObjectRecord committedChild;
        committedChild.specification.objectId = child.objectId;
        committedChild.specification.kind =
            admittedKind == StorageCatalogObjectKind::PalaceManifest
            ? StorageCatalogObjectKind::RoomManifest
            : StorageCatalogObjectKind::Blob;
        committedChild.specification.byteLength = child.byteLength;
        committedChild.specification.contentSha256 =
            child.contentSha256;
        committedChild.bootstrapAdmissionPending = true;
        committedChild.localPhase = StorageCatalogLocalPhase::Missing;
        committedChild.publicationStage =
            StorageCatalogPublicationStage::Published;
        committedChild.cid = child.cid;
        updated.m_objects.emplace(
            committedChild.specification.objectId,
            std::move(committedChild));
    }

    ObjectRecord& admitted = updated.m_objects.at(objectId);
    admitted.specification.kind = admittedKind;
    admitted.specification.children = std::move(admittedChildren);
    admitted.bootstrapAdmissionPending = false;
    if (!updated.validSpecification(admitted.specification))
        return transition(false, "bootstrap-child-admission-invalid");
    *this = std::move(updated);
    return transition(true, "bootstrap-child-specification-admitted");
}

bool PalaceStorageCatalogSession::validBootstrapPendingObject(
    const ObjectRecord& object) const
{
    if (!object.bootstrapAdmissionPending
        || !isObjectId(object.specification.objectId)
        || (object.specification.kind != StorageCatalogObjectKind::Blob
            && object.specification.kind
                != StorageCatalogObjectKind::RoomManifest)
        || object.specification.byteLength == 0U
        || object.specification.byteLength > m_config.maxObjectBytes
        || !isLowerHexDigest(object.specification.contentSha256)
        || !object.specification.children.empty()
        || object.publicationStage
            != StorageCatalogPublicationStage::Published
        || (object.localPhase != StorageCatalogLocalPhase::Missing
            && object.localPhase != StorageCatalogLocalPhase::Fetching)
        || !object.attestations.empty()
        || !isCanonicalStorageCid(object.cid)) {
        return false;
    }

    bool referenced = false;
    for (const auto& parentEntry : m_objects) {
        const ObjectRecord& parent = parentEntry.second;
        if (parent.bootstrapAdmissionPending)
            continue;
        for (const StorageCatalogManifestChildV1& child :
             parent.specification.children) {
            if (child.objectId != object.specification.objectId)
                continue;
            if (child.cid != object.cid
                || child.byteLength != object.specification.byteLength
                || child.contentSha256
                    != object.specification.contentSha256
                || !validDagEdge(
                    parent.specification.kind,
                    object.specification.kind)) {
                return false;
            }
            referenced = true;
        }
    }
    return referenced;
}

bool PalaceStorageCatalogSession::objectHasPath(
    const std::string& fromObjectId,
    const std::string& toObjectId) const
{
    std::vector<std::string> pending = {fromObjectId};
    std::set<std::string> visited;
    while (!pending.empty()) {
        const std::string current = std::move(pending.back());
        pending.pop_back();
        if (current == toObjectId)
            return true;
        if (!visited.insert(current).second)
            continue;
        const auto object = m_objects.find(current);
        if (object == m_objects.end()
            || object->second.bootstrapAdmissionPending) {
            continue;
        }
        for (const StorageCatalogManifestChildV1& child :
             object->second.specification.children) {
            pending.push_back(child.objectId);
        }
    }
    return false;
}

bool PalaceStorageCatalogSession::childrenPublished(
    const ObjectRecord& object) const
{
    return std::all_of(
        object.specification.children.begin(),
        object.specification.children.end(),
        [this](const StorageCatalogManifestChildV1& child) {
            const auto found = m_objects.find(child.objectId);
            return found != m_objects.end()
                && found->second.publicationStage
                    == StorageCatalogPublicationStage::Published
                && found->second.cid == child.cid;
        });
}

bool PalaceStorageCatalogSession::hasObjectOperation(
    const std::string& objectId,
    std::optional<StorageCatalogOperationKind> kind) const
{
    return std::any_of(
        m_operations.begin(),
        m_operations.end(),
        [&objectId, kind](const auto& operation) {
            return operation.second.objectId == objectId
                && (!kind.has_value()
                    || operation.second.kind == *kind);
        });
}

std::size_t PalaceStorageCatalogSession::challengeReservations(
    const std::string& objectId) const
{
    return static_cast<std::size_t>(std::count_if(
        m_challenges.begin(),
        m_challenges.end(),
        [&objectId](const auto& challenge) {
            return challenge.second.objectId == objectId;
        }));
}

bool PalaceStorageCatalogSession::validState() const
{
    if (!m_configured || !validConfig(m_config)
        || m_sessionEpoch < m_config.initialSessionEpoch
        || m_objects.size() > m_config.maxObjects
        || m_operations.size() > m_config.maxOperations
        || m_challenges.size()
            > m_config.maxPendingAttestationChallenges
        || m_completedOperationIds.size()
            > m_config.maxCompletedOperations
        || m_completedChallengeIds.size()
            > m_config.maxCompletedChallenges
        || m_completedOperationIds.size()
            != m_completedOperationIndex.size()
        || m_completedChallengeIds.size()
            != m_completedChallengeIndex.size()) {
        return false;
    }

    std::set<std::string> completedOperations;
    for (const std::string& operationId : m_completedOperationIds) {
        std::uint64_t epoch = 0U;
        std::uint64_t sequence = 0U;
        if (!completedOperations.insert(operationId).second
            || m_completedOperationIndex.find(operationId)
                == m_completedOperationIndex.end()
            || !parseGeneratedId(
                operationId, kOperationIdPrefix, epoch, sequence)
            || epoch < m_config.initialSessionEpoch
            || epoch > m_sessionEpoch
            || (epoch == m_sessionEpoch
                && sequence > m_lastOperationSequence)
            || m_operations.find(operationId) != m_operations.end()) {
            return false;
        }
    }
    std::set<std::string> completedChallenges;
    for (const std::string& challengeId : m_completedChallengeIds) {
        std::uint64_t epoch = 0U;
        std::uint64_t sequence = 0U;
        if (!completedChallenges.insert(challengeId).second
            || m_completedChallengeIndex.find(challengeId)
                == m_completedChallengeIndex.end()
            || !parseGeneratedId(
                challengeId, kChallengeIdPrefix, epoch, sequence)
            || epoch < m_config.initialSessionEpoch
            || epoch > m_sessionEpoch
            || (epoch == m_sessionEpoch
                && sequence > m_lastChallengeSequence)
            || m_challenges.find(challengeId) != m_challenges.end()) {
            return false;
        }
    }

    std::map<std::string, std::size_t> awaitingUploads;
    std::map<std::string, std::size_t> activeUploads;
    std::map<std::string, std::size_t> awaitingLocalDownloads;
    std::map<std::string, std::size_t> activeLocalDownloads;
    std::map<std::string, std::size_t> awaitingVerifications;
    std::map<std::string, std::size_t> activeVerifications;
    for (const auto& operation : m_operations) {
        const StorageCatalogOperation& value = operation.second;
        const auto object = m_objects.find(value.objectId);
        std::uint64_t epoch = 0U;
        std::uint64_t sequence = 0U;
        if (operation.first != value.operationId
            || !validOperationKind(value.kind)
            || !validOperationPhase(value.phase)
            || !parseGeneratedId(
                value.operationId,
                kOperationIdPrefix,
                epoch,
                sequence)
            || epoch != m_sessionEpoch
            || sequence > m_lastOperationSequence
            || object == m_objects.end()
            || value.operationId == value.cid
            || value.maxBytes
                != object->second.specification.byteLength) {
            return false;
        }
        const bool awaiting =
            value.phase
            == StorageCatalogOperationPhase::AwaitingAcknowledgement;
        switch (value.kind) {
        case StorageCatalogOperationKind::Upload:
            if (object->second.bootstrapAdmissionPending
                || !value.cid.empty() || value.localOnly
                || object->second.localPhase
                    != StorageCatalogLocalPhase::Verified
                || (awaiting
                    && object->second.publicationStage
                        != StorageCatalogPublicationStage::Staged)
                || (!awaiting
                    && object->second.publicationStage
                        != StorageCatalogPublicationStage::Uploading)) {
                return false;
            }
            if (awaiting)
                ++awaitingUploads[value.objectId];
            else
                ++activeUploads[value.objectId];
            break;
        case StorageCatalogOperationKind::LocalFetch:
            if (value.cid != object->second.cid || value.localOnly
                || object->second.publicationStage
                    != StorageCatalogPublicationStage::Published
                || (awaiting
                    && object->second.localPhase
                        != StorageCatalogLocalPhase::Missing)
                || (!awaiting
                    && object->second.localPhase
                        != StorageCatalogLocalPhase::Fetching)) {
                return false;
            }
            if (awaiting)
                ++awaitingLocalDownloads[value.objectId];
            else
                ++activeLocalDownloads[value.objectId];
            break;
        case StorageCatalogOperationKind::PublicationVerification:
            if (object->second.bootstrapAdmissionPending
                || value.cid != object->second.cid || !value.localOnly
                || object->second.publicationStage
                    != StorageCatalogPublicationStage::VerifyingLocal
                || (awaiting
                    && object->second.localPhase
                        == StorageCatalogLocalPhase::Fetching)
                || (!awaiting
                    && object->second.localPhase
                        != StorageCatalogLocalPhase::Fetching)) {
                return false;
            }
            if (awaiting)
                ++awaitingVerifications[value.objectId];
            else
                ++activeVerifications[value.objectId];
            break;
        }
    }

    std::map<std::string, std::size_t> challengeCounts;
    std::set<std::pair<std::string, std::string>> challengeHolders;
    for (const auto& challengeEntry : m_challenges) {
        const StorageCatalogHolderChallengeV1& challenge =
            challengeEntry.second;
        const auto object = m_objects.find(challenge.objectId);
        std::uint64_t epoch = 0U;
        std::uint64_t sequence = 0U;
        if (challengeEntry.first != challenge.challengeId
            || challenge.version != kAttestationVersion
            || !parseGeneratedId(
                challenge.challengeId,
                kChallengeIdPrefix,
                epoch,
                sequence)
            || epoch != m_sessionEpoch
            || sequence > m_lastChallengeSequence
            || !isHolderAccountId(challenge.holderAccountId)
            || challenge.holderAccountId
                == m_config.localHolderAccountId
            || challenge.holderKeyEpoch == 0U
            || challenge.expiresAtUnixSeconds
                <= challenge.issuedAtUnixSeconds
            || challenge.expiresAtUnixSeconds
                    - challenge.issuedAtUnixSeconds
                > m_config.maxChallengeLifetimeSeconds
            || object == m_objects.end()
            || object->second.publicationStage
                != StorageCatalogPublicationStage::Published
            || object->second.bootstrapAdmissionPending
            || challenge.cid != object->second.cid
            || challenge.byteLength
                != object->second.specification.byteLength
            || challenge.contentSha256
                != object->second.specification.contentSha256
            || object->second.attestations.find(
                challenge.holderAccountId)
                != object->second.attestations.end()
            || !challengeHolders.emplace(
                challenge.objectId,
                challenge.holderAccountId).second) {
            return false;
        }
        ++challengeCounts[challenge.objectId];
    }

    for (const auto& object : m_objects) {
        const ObjectRecord& value = object.second;
        if (object.first != value.specification.objectId
            || (value.bootstrapAdmissionPending
                ? !validBootstrapPendingObject(value)
                : !validSpecification(value.specification))
            || !validLocalPhase(value.localPhase)
            || !validPublicationStage(value.publicationStage)
            || value.attestations.size()
                    + challengeCounts[object.first]
                > m_config.maxAttestationsPerObject
            || (value.publicationStage
                    != StorageCatalogPublicationStage::Published
                && (!value.attestations.empty()
                    || challengeCounts[object.first] != 0U))
            || (value.publicationStage
                    != StorageCatalogPublicationStage::Staged
                && value.publicationStage
                    != StorageCatalogPublicationStage::Failed
                && !value.bootstrapAdmissionPending
                && !childrenPublished(value))) {
            return false;
        }
        for (const auto& attestation : value.attestations) {
            if (!isHolderAccountId(attestation.first)
                || attestation.first
                    == m_config.localHolderAccountId
                || attestation.second.holderKeyEpoch == 0U
                || attestation.second.expiresAtUnixSeconds
                    <= attestation.second.issuedAtUnixSeconds
                || attestation.second.expiresAtUnixSeconds
                        - attestation.second.issuedAtUnixSeconds
                    > m_config.maxChallengeLifetimeSeconds) {
                return false;
            }
        }
        switch (value.publicationStage) {
        case StorageCatalogPublicationStage::Staged:
            if (value.localPhase
                    != StorageCatalogLocalPhase::Verified
                || !value.cid.empty()
                || awaitingUploads[object.first] > 1U
                || activeUploads[object.first] != 0U
                || awaitingLocalDownloads[object.first] != 0U
                || activeLocalDownloads[object.first] != 0U
                || awaitingVerifications[object.first] != 0U
                || activeVerifications[object.first] != 0U) {
                return false;
            }
            break;
        case StorageCatalogPublicationStage::Uploading:
            if (value.localPhase
                    != StorageCatalogLocalPhase::Verified
                || !value.cid.empty()
                || activeUploads[object.first] != 1U
                || awaitingUploads[object.first] != 0U) {
                return false;
            }
            break;
        case StorageCatalogPublicationStage::VerifyingLocal:
            if (!isCanonicalStorageCid(value.cid)
                || awaitingVerifications[object.first] > 1U
                || activeVerifications[object.first] > 1U
                || awaitingVerifications[object.first]
                        + activeVerifications[object.first]
                    > 1U
                || (value.localPhase
                        == StorageCatalogLocalPhase::Fetching
                    && activeVerifications[object.first] != 1U)
                || (value.localPhase
                        != StorageCatalogLocalPhase::Fetching
                    && activeVerifications[object.first] != 0U)) {
                return false;
            }
            break;
        case StorageCatalogPublicationStage::Published:
            if (!isCanonicalStorageCid(value.cid)
                || awaitingLocalDownloads[object.first] > 1U
                || activeLocalDownloads[object.first] > 1U
                || awaitingLocalDownloads[object.first]
                        + activeLocalDownloads[object.first]
                    > 1U
                || (value.localPhase
                        == StorageCatalogLocalPhase::Fetching
                    && activeLocalDownloads[object.first] != 1U)
                || (value.localPhase
                        != StorageCatalogLocalPhase::Fetching
                    && activeLocalDownloads[object.first] != 0U)) {
                return false;
            }
            break;
        case StorageCatalogPublicationStage::Failed:
            if (value.localPhase == StorageCatalogLocalPhase::Fetching
                || hasObjectOperation(object.first)
                || (!value.cid.empty()
                    && !isCanonicalStorageCid(value.cid))) {
                return false;
            }
            break;
        }
    }
    return true;
}

StorageCatalogTransition PalaceStorageCatalogSession::beginOperation(
    const std::string& objectId,
    StorageCatalogOperationKind kind)
{
    if (!canAct())
        return transition(false, "restart-reconciliation-required");
    if (!validOperationKind(kind))
        return transition(false, "invalid-operation-kind");
    const auto object = m_objects.find(objectId);
    if (object == m_objects.end())
        return transition(false, "object-not-found");
    if (m_operations.size() >= m_config.maxOperations)
        return transition(false, "operation-capacity-exceeded");

    switch (kind) {
    case StorageCatalogOperationKind::Upload:
        if (object->second.publicationStage
                != StorageCatalogPublicationStage::Staged
            || object->second.localPhase
                != StorageCatalogLocalPhase::Verified) {
            return transition(false, "object-not-staged");
        }
        if (!childrenPublished(object->second))
            return transition(false, "children-not-published");
        if (hasObjectOperation(objectId))
            return transition(false, "operation-already-active");
        break;
    case StorageCatalogOperationKind::PublicationVerification:
        if (object->second.publicationStage
                != StorageCatalogPublicationStage::VerifyingLocal
            || object->second.localPhase
                == StorageCatalogLocalPhase::Fetching
            || hasObjectOperation(objectId)) {
            return transition(
                false, "publication-verification-not-ready");
        }
        break;
    case StorageCatalogOperationKind::LocalFetch:
        if (object->second.publicationStage
                != StorageCatalogPublicationStage::Published
            || object->second.localPhase
                != StorageCatalogLocalPhase::Missing
            || hasObjectOperation(
                objectId, StorageCatalogOperationKind::LocalFetch)) {
            return transition(false, "local-fetch-not-ready");
        }
        break;
    }

    const std::optional<std::string> operationId =
        generateOperationId(object->second.cid);
    if (!operationId.has_value())
        return transition(false, "operation-id-exhausted");
    StorageCatalogOperation operation;
    operation.kind = kind;
    operation.operationId = *operationId;
    operation.objectId = objectId;
    operation.cid =
        kind == StorageCatalogOperationKind::Upload
        ? std::string{} : object->second.cid;
    operation.localOnly =
        kind == StorageCatalogOperationKind::PublicationVerification;
    operation.maxBytes = object->second.specification.byteLength;
    m_operations.emplace(operation.operationId, operation);
    return operationTransition(
        "operation-awaiting-acknowledgement", std::move(operation));
}

std::optional<StorageCatalogOperation>
PalaceStorageCatalogSession::enqueueReplacement(
    const StorageCatalogOperation& previous)
{
    const auto object = m_objects.find(previous.objectId);
    if (object == m_objects.end()
        || m_operations.size() >= m_config.maxOperations) {
        return std::nullopt;
    }
    switch (previous.kind) {
    case StorageCatalogOperationKind::Upload:
        if (object->second.publicationStage
                != StorageCatalogPublicationStage::Staged
            || object->second.localPhase
                != StorageCatalogLocalPhase::Verified
            || !object->second.cid.empty()
            || !childrenPublished(object->second)) {
            return std::nullopt;
        }
        break;
    case StorageCatalogOperationKind::PublicationVerification:
        if (object->second.publicationStage
                != StorageCatalogPublicationStage::VerifyingLocal
            || object->second.localPhase
                == StorageCatalogLocalPhase::Fetching) {
            return std::nullopt;
        }
        break;
    case StorageCatalogOperationKind::LocalFetch:
        if (object->second.publicationStage
                != StorageCatalogPublicationStage::Published
            || object->second.localPhase
                != StorageCatalogLocalPhase::Missing) {
            return std::nullopt;
        }
        break;
    }
    const std::optional<std::string> operationId =
        generateOperationId(object->second.cid);
    if (!operationId.has_value())
        return std::nullopt;
    StorageCatalogOperation replacement = previous;
    replacement.phase =
        StorageCatalogOperationPhase::AwaitingAcknowledgement;
    replacement.operationId = *operationId;
    replacement.cid =
        previous.kind == StorageCatalogOperationKind::Upload
        ? std::string{} : object->second.cid;
    replacement.localOnly =
        previous.kind
        == StorageCatalogOperationKind::PublicationVerification;
    replacement.maxBytes = object->second.specification.byteLength;
    if (!m_operations.emplace(
            replacement.operationId, replacement).second) {
        return std::nullopt;
    }
    return replacement;
}

std::optional<std::string>
PalaceStorageCatalogSession::generateOperationId(const std::string& cid)
{
    if (m_lastOperationSequence
        == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    const std::uint64_t sequence = m_lastOperationSequence + 1U;
    const std::string operationId = std::string(kOperationIdPrefix)
        + std::to_string(m_sessionEpoch) + '-' + std::to_string(sequence);
    if (operationId == cid
        || m_operations.find(operationId) != m_operations.end()
        || wasOperationCompleted(operationId)) {
        return std::nullopt;
    }
    m_lastOperationSequence = sequence;
    return operationId;
}

std::optional<std::string>
PalaceStorageCatalogSession::generateChallengeId()
{
    if (m_lastChallengeSequence
        == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    const std::uint64_t sequence = m_lastChallengeSequence + 1U;
    const std::string challengeId = std::string(kChallengeIdPrefix)
        + std::to_string(m_sessionEpoch) + '-' + std::to_string(sequence);
    if (m_challenges.find(challengeId) != m_challenges.end()
        || wasChallengeCompleted(challengeId)) {
        return std::nullopt;
    }
    m_lastChallengeSequence = sequence;
    return challengeId;
}

void PalaceStorageCatalogSession::rememberCompletedOperation(
    const std::string& operationId)
{
    if (!m_completedOperationIndex.insert(operationId).second)
        return;
    m_completedOperationIds.push_back(operationId);
    while (m_completedOperationIds.size()
           > m_config.maxCompletedOperations) {
        m_completedOperationIndex.erase(m_completedOperationIds.front());
        m_completedOperationIds.pop_front();
    }
}

void PalaceStorageCatalogSession::rememberCompletedChallenge(
    const std::string& challengeId)
{
    if (!m_completedChallengeIndex.insert(challengeId).second)
        return;
    m_completedChallengeIds.push_back(challengeId);
    while (m_completedChallengeIds.size()
           > m_config.maxCompletedChallenges) {
        m_completedChallengeIndex.erase(m_completedChallengeIds.front());
        m_completedChallengeIds.pop_front();
    }
}

bool PalaceStorageCatalogSession::wasOperationCompleted(
    const std::string& operationId) const
{
    return m_completedOperationIndex.find(operationId)
        != m_completedOperationIndex.end();
}

bool PalaceStorageCatalogSession::wasChallengeCompleted(
    const std::string& challengeId) const
{
    return m_completedChallengeIndex.find(challengeId)
        != m_completedChallengeIndex.end();
}

void PalaceStorageCatalogSession::rollbackAcknowledgement(
    const StorageCatalogOperation& operation)
{
    const auto object = m_objects.find(operation.objectId);
    if (object == m_objects.end())
        return;
    switch (operation.kind) {
    case StorageCatalogOperationKind::Upload:
        object->second.localPhase = StorageCatalogLocalPhase::Verified;
        object->second.publicationStage =
            StorageCatalogPublicationStage::Staged;
        object->second.cid.clear();
        break;
    case StorageCatalogOperationKind::PublicationVerification:
        if (object->second.localPhase == StorageCatalogLocalPhase::Fetching)
            object->second.localPhase = StorageCatalogLocalPhase::Missing;
        break;
    case StorageCatalogOperationKind::LocalFetch:
        object->second.localPhase = StorageCatalogLocalPhase::Missing;
        break;
    }
}

void PalaceStorageCatalogSession::settleAbandonedOperation(
    const StorageCatalogOperation& operation)
{
    const auto object = m_objects.find(operation.objectId);
    if (object == m_objects.end())
        return;
    switch (operation.kind) {
    case StorageCatalogOperationKind::Upload:
        object->second.publicationStage =
            StorageCatalogPublicationStage::Failed;
        break;
    case StorageCatalogOperationKind::PublicationVerification:
        object->second.localPhase = StorageCatalogLocalPhase::Missing;
        object->second.publicationStage =
            StorageCatalogPublicationStage::Failed;
        break;
    case StorageCatalogOperationKind::LocalFetch:
        object->second.localPhase = StorageCatalogLocalPhase::Missing;
        break;
    }
}

void PalaceStorageCatalogSession::settleChallenge(
    const std::string& challengeId)
{
    const auto challenge = m_challenges.find(challengeId);
    if (challenge == m_challenges.end())
        return;
    m_challenges.erase(challenge);
    rememberCompletedChallenge(challengeId);
}

void PalaceStorageCatalogSession::pruneExpiredAttestations(
    std::uint64_t nowUnixSeconds)
{
    for (auto& object : m_objects) {
        auto& attestations = object.second.attestations;
        for (auto attestation = attestations.begin();
             attestation != attestations.end();) {
            if (attestation->second.expiresAtUnixSeconds
                < nowUnixSeconds) {
                attestation = attestations.erase(attestation);
            } else {
                ++attestation;
            }
        }
    }
}

void PalaceStorageCatalogSession::pruneExpiredChallenges(
    std::uint64_t nowUnixSeconds)
{
    std::vector<std::string> expired;
    for (const auto& challenge : m_challenges) {
        if (challenge.second.expiresAtUnixSeconds < nowUnixSeconds)
            expired.push_back(challenge.first);
    }
    for (const std::string& challengeId : expired)
        settleChallenge(challengeId);
}

} // namespace palace
