#include "logos_test.h"

#include "palace_sha256.h"
#include "palace_storage_catalog_session.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uint64_t kNow = 1'000'000U;
constexpr char kNativeBase58CidV1[] =
    "zb2rhWeJ5rsKhbfi31kikfdS3hE2pvWzhW27Es4Jmc863vFdx";
constexpr char kOverlongVersionCidV1[] =
    "z5KmBFEjTba7gnFbbavM3Vt1u54hssGFq7Fb6hcN1KCZ6t64Rh1p";

std::string account(char digit)
{
    return std::string(64U, digit);
}

std::string cid(char suffix)
{
    std::vector<std::uint8_t> bytes = {0x01U, 0x55U, 0x12U, 0x20U};
    for (std::uint8_t index = 0U; index < 32U; ++index) {
        bytes.push_back(static_cast<std::uint8_t>(
            static_cast<unsigned char>(suffix) + index));
    }
    static constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz234567";
    std::string encoded = "b";
    std::uint32_t accumulator = 0U;
    unsigned int bitCount = 0U;
    for (std::uint8_t byte : bytes) {
        accumulator = (accumulator << 8U) | byte;
        bitCount += 8U;
        while (bitCount >= 5U) {
            bitCount -= 5U;
            encoded.push_back(alphabet[
                (accumulator >> bitCount) & 0x1fU]);
            accumulator &= bitCount == 0U
                ? 0U : ((1U << bitCount) - 1U);
        }
    }
    if (bitCount != 0U) {
        encoded.push_back(alphabet[
            (accumulator << (5U - bitCount)) & 0x1fU]);
    }
    return encoded;
}

std::string cidForBytes(const std::string& bytes)
{
    const std::string digest = palace::crypto::sha256Hex(bytes);
    std::vector<std::uint8_t> cidBytes = {
        0x01U, 0x55U, 0x12U, 0x20U,
    };
    auto nibble = [](char value) -> std::uint8_t {
        if (value >= '0' && value <= '9')
            return static_cast<std::uint8_t>(value - '0');
        return static_cast<std::uint8_t>(value - 'a' + 10);
    };
    for (std::size_t index = 0U; index < digest.size(); index += 2U) {
        cidBytes.push_back(static_cast<std::uint8_t>(
            (nibble(digest[index]) << 4U) | nibble(digest[index + 1U])));
    }

    static constexpr char alphabet[] =
        "abcdefghijklmnopqrstuvwxyz234567";
    std::string encoded = "b";
    std::uint32_t accumulator = 0U;
    unsigned int bitCount = 0U;
    for (std::uint8_t byte : cidBytes) {
        accumulator = (accumulator << 8U) | byte;
        bitCount += 8U;
        while (bitCount >= 5U) {
            bitCount -= 5U;
            encoded.push_back(alphabet[
                (accumulator >> bitCount) & 0x1fU]);
            accumulator &= bitCount == 0U
                ? 0U : ((1U << bitCount) - 1U);
        }
    }
    if (bitCount != 0U) {
        encoded.push_back(alphabet[
            (accumulator << (5U - bitCount)) & 0x1fU]);
    }
    return encoded;
}

std::string cidV0(char suffix)
{
    std::vector<std::uint8_t> bytes = {0x12U, 0x20U};
    for (std::uint8_t index = 0U; index < 32U; ++index) {
        bytes.push_back(static_cast<std::uint8_t>(
            static_cast<unsigned char>(suffix) + index));
    }
    static constexpr char alphabet[] =
        "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    std::vector<unsigned int> digits(1U, 0U);
    for (std::uint8_t byte : bytes) {
        unsigned int carry = byte;
        for (unsigned int& digit : digits) {
            const unsigned int value = digit * 256U + carry;
            digit = value % 58U;
            carry = value / 58U;
        }
        while (carry != 0U) {
            digits.push_back(carry % 58U);
            carry /= 58U;
        }
    }
    std::string encoded;
    encoded.reserve(digits.size());
    for (auto digit = digits.rbegin(); digit != digits.rend(); ++digit)
        encoded.push_back(alphabet[*digit]);
    return encoded;
}

palace::StorageCatalogSessionConfigV3 config()
{
    palace::StorageCatalogSessionConfigV3 value;
    value.localHolderAccountId = account('a');
    value.initialSessionEpoch = 41U;
    return value;
}

palace::StorageCatalogObjectSpecV2 blob(
    const std::string& objectId,
    const std::string& bytes)
{
    palace::StorageCatalogObjectSpecV2 value;
    value.objectId = objectId;
    value.kind = palace::StorageCatalogObjectKind::Blob;
    value.byteLength = bytes.size();
    value.contentSha256 = palace::crypto::sha256Hex(bytes);
    return value;
}

palace::StorageCatalogManifestChildV1 child(
    const palace::PalaceStorageCatalogSession& session,
    const std::string& objectId)
{
    const auto status = session.status(objectId, kNow);
    LOGOS_ASSERT_TRUE(status.found);
    palace::StorageCatalogManifestChildV1 value;
    value.objectId = objectId;
    value.cid = status.cid;
    value.byteLength = status.specification.byteLength;
    value.contentSha256 = status.specification.contentSha256;
    return value;
}

struct ManifestFixture {
    palace::StorageCatalogObjectSpecV2 specification;
    std::string bytes;
};

ManifestFixture manifest(
    const std::string& objectId,
    palace::StorageCatalogObjectKind kind,
    std::vector<palace::StorageCatalogManifestChildV1> children)
{
    ManifestFixture fixture;
    fixture.specification.objectId = objectId;
    fixture.specification.kind = kind;
    fixture.specification.children = std::move(children);
    fixture.bytes = palace::canonicalStorageCatalogManifestV1(
        fixture.specification.kind,
        fixture.specification.objectId,
        fixture.specification.children);
    LOGOS_ASSERT_FALSE(fixture.bytes.empty());
    fixture.specification.byteLength = fixture.bytes.size();
    fixture.specification.contentSha256 =
        palace::crypto::sha256Hex(fixture.bytes);
    return fixture;
}

palace::StorageCatalogManifestChildV1 committedChild(
    const std::string& objectId,
    const std::string& bytes)
{
    palace::StorageCatalogManifestChildV1 value;
    value.objectId = objectId;
    value.cid = cidForBytes(bytes);
    value.byteLength = bytes.size();
    value.contentSha256 = palace::crypto::sha256Hex(bytes);
    return value;
}

palace::StorageCatalogDownloadTerminalV2 terminal(
    const std::string& operationId,
    const std::string& objectCid)
{
    palace::StorageCatalogDownloadTerminalV2 value;
    value.protocol = "logos.storage.download";
    value.version = 2U;
    value.operationId = operationId;
    value.cid = objectCid;
    value.outcome = palace::StorageCatalogDownloadOutcome::Succeeded;
    return value;
}

palace::StorageCatalogDownloadAcknowledgementV2 acknowledgement(
    const palace::StorageCatalogOperation& operation,
    bool accepted = true)
{
    palace::StorageCatalogDownloadAcknowledgementV2 value;
    value.protocol = "logos.storage.download";
    value.version = 2U;
    value.accepted = accepted;
    value.operationId = operation.operationId;
    value.cid = operation.cid;
    return value;
}

std::string requireOperation(
    palace::PalaceStorageCatalogSession& session,
    const palace::StorageCatalogTransition& started)
{
    LOGOS_ASSERT_TRUE(started.accepted);
    LOGOS_ASSERT_TRUE(started.operation.has_value());
    const std::string operationId = started.operation->operationId;
    if (started.operation->kind
        == palace::StorageCatalogOperationKind::Upload) {
        LOGOS_ASSERT_TRUE(
            session.operationAcknowledged(operationId, true).accepted);
    } else {
        LOGOS_ASSERT_TRUE(session.downloadAcknowledged(
            acknowledgement(*started.operation)).accepted);
    }
    return operationId;
}

bool publish(
    palace::PalaceStorageCatalogSession& session,
    const std::string& objectId,
    const std::string& bytes,
    const std::string& objectCid)
{
    const auto upload = session.beginUpload(objectId);
    if (!upload.accepted || !upload.operation.has_value())
        return false;
    const std::string uploadId = upload.operation->operationId;
    if (!session.operationAcknowledged(uploadId, true).accepted
        || !session.uploadFinished(
            uploadId, true, objectCid).accepted) {
        return false;
    }
    const auto verification =
        session.beginPublicationVerification(objectId);
    if (!verification.accepted
        || !verification.operation.has_value()
        || !session.downloadAcknowledged(
            acknowledgement(*verification.operation)).accepted) {
        return false;
    }
    return session.downloadFinished(
        terminal(
            verification.operation->operationId,
            objectCid),
        bytes).accepted;
}

palace::StorageCatalogHolderAttestationReceiptV1 receipt(
    const palace::StorageCatalogHolderChallengeV1& challenge)
{
    palace::StorageCatalogHolderAttestationReceiptV1 value;
    value.version = challenge.version;
    value.challengeId = challenge.challengeId;
    value.holderAccountId = challenge.holderAccountId;
    value.holderKeyEpoch = challenge.holderKeyEpoch;
    value.objectId = challenge.objectId;
    value.cid = challenge.cid;
    value.byteLength = challenge.byteLength;
    value.contentSha256 = challenge.contentSha256;
    value.issuedAtUnixSeconds = challenge.issuedAtUnixSeconds;
    value.expiresAtUnixSeconds = challenge.expiresAtUnixSeconds;
    return value;
}

class ExactVerifier final :
    public palace::StorageCatalogAttestationVerifier {
public:
    ExactVerifier(
        std::string holderAccountId,
        std::uint64_t keyEpoch,
        std::string canonicalReceipt,
        std::string signature)
        : m_holderAccountId(std::move(holderAccountId))
        , m_keyEpoch(keyEpoch)
        , m_canonicalReceipt(std::move(canonicalReceipt))
        , m_signature(std::move(signature))
    {
    }

    bool verify(
        const std::string& holderAccountId,
        std::uint64_t holderKeyEpoch,
        const std::string& canonicalReceipt,
        const std::string& signature) const override
    {
        return holderAccountId == m_holderAccountId
            && holderKeyEpoch == m_keyEpoch
            && canonicalReceipt == m_canonicalReceipt
            && signature == m_signature;
    }

private:
    std::string m_holderAccountId;
    std::uint64_t m_keyEpoch;
    std::string m_canonicalReceipt;
    std::string m_signature;
};

bool attest(
    palace::PalaceStorageCatalogSession& session,
    const std::string& objectId,
    const std::string& holderAccountId,
    std::uint64_t keyEpoch,
    std::uint64_t issuedAt,
    std::uint64_t expiresAt)
{
    const auto started = session.beginHolderAttestation(
        objectId,
        holderAccountId,
        keyEpoch,
        issuedAt,
        expiresAt);
    if (!started.accepted || !started.challenge.has_value())
        return false;
    const auto signedReceipt = receipt(*started.challenge);
    const std::string canonical =
        palace::canonicalHolderAttestationReceiptV1(signedReceipt);
    ExactVerifier verifier(
        holderAccountId, keyEpoch, canonical, "valid-signature");
    return session.completeHolderAttestation(
        signedReceipt,
        "valid-signature",
        issuedAt,
        verifier).accepted;
}

std::string replaceOnce(
    std::string value,
    const std::string& from,
    const std::string& to)
{
    const std::size_t position = value.find(from);
    LOGOS_ASSERT_NE(position, std::string::npos);
    value.replace(position, from.size(), to);
    return value;
}

std::string resignState(std::string serialized)
{
    const std::size_t checksumPosition = serialized.rfind("checksum=");
    LOGOS_ASSERT_NE(checksumPosition, std::string::npos);
    const std::string body = serialized.substr(0U, checksumPosition);
    return body + "checksum=" + palace::crypto::sha256Hex(body) + '\n';
}

} // namespace

LOGOS_TEST(storage_catalog_uses_explicit_ascii_identity_cid_and_enum_rules)
{
    auto invalidConfig = config();
    invalidConfig.localHolderAccountId = "alice";
    palace::PalaceStorageCatalogSession invalidSession;
    LOGOS_ASSERT_FALSE(invalidSession.configure(invalidConfig));

    palace::PalaceStorageCatalogSession session;
    LOGOS_ASSERT_TRUE(session.configure(config()));
    const std::string bytes = "strict-blob-bytes";
    LOGOS_ASSERT_FALSE(session.stagePublicationObject(
        blob(".", bytes), bytes).accepted);
    LOGOS_ASSERT_FALSE(session.stagePublicationObject(
        blob("..", bytes), bytes).accepted);
    LOGOS_ASSERT_FALSE(session.stagePublicationObject(
        blob("bad.id", bytes), bytes).accepted);
    auto invalidKind = blob("bad-kind", bytes);
    invalidKind.kind =
        static_cast<palace::StorageCatalogObjectKind>(99U);
    LOGOS_ASSERT_FALSE(session.stagePublicationObject(
        invalidKind, bytes).accepted);
    LOGOS_ASSERT_FALSE(session.trackPublishedObject(
        blob("legacy-cid", bytes), "bafyAsset").accepted);
    std::string upperCid = cid('a');
    upperCid[1] = 'A';
    LOGOS_ASSERT_FALSE(session.trackPublishedObject(
        blob("upper-cid", bytes), upperCid).accepted);
    LOGOS_ASSERT_FALSE(session.trackPublishedObject(
        blob("noncanonical-padding", bytes), cid('a') + "a").accepted);
    LOGOS_ASSERT_TRUE(session.trackPublishedObject(
        blob("canonical-cid", bytes), cid('a')).accepted);
    LOGOS_ASSERT_EQ(cidV0('b').size(), 46U);
    LOGOS_ASSERT_TRUE(session.trackPublishedObject(
        blob("canonical-cid-v0", bytes), cidV0('b')).accepted);
    LOGOS_ASSERT_TRUE(session.trackPublishedObject(
        blob("canonical-cid-v1-base58btc", bytes),
        kNativeBase58CidV1).accepted);
    LOGOS_ASSERT_FALSE(session.trackPublishedObject(
        blob("leading-zero-cid-v1-base58btc", bytes),
        std::string("z1") + (kNativeBase58CidV1 + 1)).accepted);
    LOGOS_ASSERT_FALSE(session.trackPublishedObject(
        blob("overlong-varint-cid-v1-base58btc", bytes),
        kOverlongVersionCidV1).accepted);
    LOGOS_ASSERT_EQ(
        palace::storageCatalogRetentionName(
            static_cast<palace::StorageCatalogRetention>(99U)),
        std::string("invalid"));
}

LOGOS_TEST(storage_catalog_accepts_native_base58btc_upload_terminal_cidv1)
{
    palace::PalaceStorageCatalogSession session;
    LOGOS_ASSERT_TRUE(session.configure(config()));
    const std::string bytes = "native-base58btc-upload";
    LOGOS_ASSERT_TRUE(session.stagePublicationObject(
        blob("native-z-upload", bytes), bytes).accepted);
    const palace::StorageCatalogTransition upload =
        session.beginUpload("native-z-upload");
    LOGOS_ASSERT_TRUE(upload.accepted);
    LOGOS_ASSERT_TRUE(upload.operation.has_value());
    LOGOS_ASSERT_TRUE(session.operationAcknowledged(
        upload.operation->operationId, true).accepted);
    LOGOS_ASSERT_TRUE(session.uploadFinished(
        upload.operation->operationId,
        true,
        kNativeBase58CidV1).accepted);
    const palace::StorageCatalogObjectStatus status =
        session.status("native-z-upload", kNow);
    LOGOS_ASSERT_TRUE(status.found);
    LOGOS_ASSERT_EQ(status.cid, std::string(kNativeBase58CidV1));
    LOGOS_ASSERT_EQ(
        static_cast<int>(status.publicationStage),
        static_cast<int>(
            palace::StorageCatalogPublicationStage::VerifyingLocal));
}

LOGOS_TEST(storage_catalog_requires_published_children_and_bound_manifests)
{
    palace::PalaceStorageCatalogSession session;
    LOGOS_ASSERT_TRUE(session.configure(config()));
    const std::string childBytes = "background-image-bytes";
    LOGOS_ASSERT_TRUE(session.stagePublicationObject(
        blob("background", childBytes), childBytes).accepted);

    palace::StorageCatalogManifestChildV1 premature;
    premature.objectId = "background";
    premature.cid = cid('b');
    premature.byteLength = childBytes.size();
    premature.contentSha256 = palace::crypto::sha256Hex(childBytes);
    const auto prematureManifest = manifest(
        "prop",
        palace::StorageCatalogObjectKind::PropManifest,
        {premature});
    LOGOS_ASSERT_FALSE(session.stagePublicationObject(
        prematureManifest.specification,
        prematureManifest.bytes).accepted);

    LOGOS_ASSERT_TRUE(publish(
        session, "background", childBytes, cid('b')));
    const auto propManifest = manifest(
        "prop",
        palace::StorageCatalogObjectKind::PropManifest,
        {child(session, "background")});
    LOGOS_ASSERT_TRUE(session.stagePublicationObject(
        propManifest.specification,
        propManifest.bytes).accepted);
    LOGOS_ASSERT_TRUE(publish(
        session, "prop", propManifest.bytes, cid('c')));

    const auto roomManifest = manifest(
        "room",
        palace::StorageCatalogObjectKind::RoomManifest,
        {child(session, "background"), child(session, "prop")});
    LOGOS_ASSERT_TRUE(session.stagePublicationObject(
        roomManifest.specification,
        roomManifest.bytes).accepted);
    LOGOS_ASSERT_TRUE(publish(
        session, "room", roomManifest.bytes, cid('d')));
}

LOGOS_TEST(storage_catalog_rejects_manifest_child_and_canonical_byte_tampering)
{
    palace::PalaceStorageCatalogSession session;
    LOGOS_ASSERT_TRUE(session.configure(config()));
    const std::string firstBytes = "first-child-bytes";
    const std::string secondBytes = "second-child-distinct-bytes";
    LOGOS_ASSERT_TRUE(session.trackPublishedObject(
        blob("a-child", firstBytes), cid('a')).accepted);
    LOGOS_ASSERT_TRUE(session.trackPublishedObject(
        blob("b-child", secondBytes), cid('b')).accepted);
    const auto first = child(session, "a-child");
    const auto second = child(session, "b-child");

    auto wrongCid = first;
    wrongCid.cid = cid('c');
    const auto cidTampered = manifest(
        "prop-cid",
        palace::StorageCatalogObjectKind::PropManifest,
        {wrongCid});
    LOGOS_ASSERT_FALSE(session.stagePublicationObject(
        cidTampered.specification, cidTampered.bytes).accepted);

    auto wrongLength = first;
    ++wrongLength.byteLength;
    const auto lengthTampered = manifest(
        "prop-length",
        palace::StorageCatalogObjectKind::PropManifest,
        {wrongLength});
    LOGOS_ASSERT_FALSE(session.stagePublicationObject(
        lengthTampered.specification, lengthTampered.bytes).accepted);

    auto wrongDigest = first;
    wrongDigest.contentSha256 = std::string(64U, '0');
    const auto digestTampered = manifest(
        "prop-digest",
        palace::StorageCatalogObjectKind::PropManifest,
        {wrongDigest});
    LOGOS_ASSERT_FALSE(session.stagePublicationObject(
        digestTampered.specification, digestTampered.bytes).accepted);

    const auto canonical = manifest(
        "prop-order",
        palace::StorageCatalogObjectKind::PropManifest,
        {first, second});
    std::ostringstream reordered;
    reordered
        << "logos-palace-catalog-manifest-v1\n"
        << "kind=prop_manifest\n"
        << "object=prop-order\n"
        << "children=2\n"
        << "child=" << second.objectId << ';' << second.cid << ';'
        << second.byteLength << ';' << second.contentSha256 << '\n'
        << "child=" << first.objectId << ';' << first.cid << ';'
        << first.byteLength << ';' << first.contentSha256 << '\n';
    auto nonCanonicalSpec = canonical.specification;
    const std::string nonCanonicalBytes = reordered.str();
    nonCanonicalSpec.byteLength = nonCanonicalBytes.size();
    nonCanonicalSpec.contentSha256 =
        palace::crypto::sha256Hex(nonCanonicalBytes);
    LOGOS_ASSERT_FALSE(session.stagePublicationObject(
        nonCanonicalSpec, nonCanonicalBytes).accepted);

    auto wrongObjectSpec = canonical.specification;
    const std::string wrongObjectBytes = replaceOnce(
        canonical.bytes, "object=prop-order", "object=prop-other");
    wrongObjectSpec.byteLength = wrongObjectBytes.size();
    wrongObjectSpec.contentSha256 =
        palace::crypto::sha256Hex(wrongObjectBytes);
    LOGOS_ASSERT_FALSE(session.stagePublicationObject(
        wrongObjectSpec, wrongObjectBytes).accepted);

    auto wrongKindSpec = canonical.specification;
    const std::string wrongKindBytes =
        palace::canonicalStorageCatalogManifestV1(
            palace::StorageCatalogObjectKind::RoomManifest,
            canonical.specification.objectId,
            canonical.specification.children);
    LOGOS_ASSERT_FALSE(wrongKindBytes.empty());
    wrongKindSpec.byteLength = wrongKindBytes.size();
    wrongKindSpec.contentSha256 =
        palace::crypto::sha256Hex(wrongKindBytes);
    LOGOS_ASSERT_FALSE(session.stagePublicationObject(
        wrongKindSpec, wrongKindBytes).accepted);

    LOGOS_ASSERT_TRUE(session.stagePublicationObject(
        canonical.specification, canonical.bytes).accepted);
}

LOGOS_TEST(storage_catalog_remote_manifest_download_reuses_strict_codec)
{
    palace::PalaceStorageCatalogSession session;
    LOGOS_ASSERT_TRUE(session.configure(config()));
    const std::string childBytes = "remote-child-bytes";
    LOGOS_ASSERT_TRUE(session.trackPublishedObject(
        blob("child", childBytes), cid('a')).accepted);
    const auto canonical = manifest(
        "remote-prop",
        palace::StorageCatalogObjectKind::PropManifest,
        {child(session, "child")});
    LOGOS_ASSERT_TRUE(session.trackPublishedObject(
        canonical.specification, cid('b')).accepted);

    const std::string fetch = requireOperation(
        session, session.beginLocalFetch("remote-prop"));
    const std::string malformed = replaceOnce(
        canonical.bytes, "object=remote-prop", "object=remote-fake");
    LOGOS_ASSERT_FALSE(session.downloadFinished(
        terminal(fetch, cid('b')), malformed).accepted);
    LOGOS_ASSERT_TRUE(
        session.status("remote-prop", kNow).localPhase
        == palace::StorageCatalogLocalPhase::Missing);

    const std::string retry = requireOperation(
        session, session.beginLocalFetch("remote-prop"));
    LOGOS_ASSERT_TRUE(session.downloadFinished(
        terminal(retry, cid('b')), canonical.bytes).accepted);
    LOGOS_ASSERT_TRUE(
        session.status("remote-prop", kNow).localPhase
        == palace::StorageCatalogLocalPhase::Verified);
}

LOGOS_TEST(storage_catalog_bootstraps_finalized_palace_and_admits_bound_tree)
{
    const std::string leafBytes = "bootstrap-prop-image";
    const std::string propBytes =
        palace::canonicalStorageCatalogManifestV1(
            palace::StorageCatalogObjectKind::PropManifest,
            "prop",
            {committedChild("image", leafBytes)});
    const std::string roomBytes =
        palace::canonicalStorageCatalogManifestV1(
            palace::StorageCatalogObjectKind::RoomManifest,
            "room",
            {committedChild("prop", propBytes)});
    const std::string palaceBytes =
        palace::canonicalStorageCatalogManifestV1(
            palace::StorageCatalogObjectKind::PalaceManifest,
            "palace",
            {committedChild("room", roomBytes)});
    LOGOS_ASSERT_FALSE(propBytes.empty());
    LOGOS_ASSERT_FALSE(roomBytes.empty());
    LOGOS_ASSERT_FALSE(palaceBytes.empty());

    palace::PalaceStorageCatalogSession session;
    LOGOS_ASSERT_TRUE(session.configure(config()));
    LOGOS_ASSERT_TRUE(session.admitFinalizedPalaceManifest(
        cidForBytes(palaceBytes), palaceBytes).accepted);

    const auto palaceStatus = session.status("palace", kNow);
    LOGOS_ASSERT_TRUE(palaceStatus.found);
    LOGOS_ASSERT_TRUE(palaceStatus.specificationAdmitted);
    LOGOS_ASSERT_TRUE(
        palaceStatus.localPhase
        == palace::StorageCatalogLocalPhase::Verified);
    LOGOS_ASSERT_EQ(palaceStatus.specification.children.size(), 1U);
    LOGOS_ASSERT_EQ(
        palaceStatus.specification.children[0].objectId,
        std::string("room"));

    auto roomStatus = session.status("room", kNow);
    LOGOS_ASSERT_TRUE(roomStatus.found);
    LOGOS_ASSERT_FALSE(roomStatus.specificationAdmitted);
    LOGOS_ASSERT_TRUE(
        roomStatus.specification.kind
        == palace::StorageCatalogObjectKind::RoomManifest);
    LOGOS_ASSERT_FALSE(session.beginHolderAttestation(
        "room", account('b'), 1U, kNow, kNow + 10U).accepted);

    const auto roomStarted = session.beginLocalFetch("room");
    const std::string roomFetch = requireOperation(session, roomStarted);
    LOGOS_ASSERT_EQ(
        roomStarted.operation->cid, cidForBytes(roomBytes));
    LOGOS_ASSERT_EQ(
        roomStarted.operation->maxBytes,
        static_cast<std::uint64_t>(roomBytes.size()));
    LOGOS_ASSERT_TRUE(session.downloadFinished(
        terminal(roomFetch, cidForBytes(roomBytes)),
        roomBytes).accepted);
    roomStatus = session.status("room", kNow);
    LOGOS_ASSERT_TRUE(roomStatus.specificationAdmitted);
    LOGOS_ASSERT_TRUE(
        roomStatus.localPhase
        == palace::StorageCatalogLocalPhase::Verified);

    auto propStatus = session.status("prop", kNow);
    LOGOS_ASSERT_TRUE(propStatus.found);
    LOGOS_ASSERT_FALSE(propStatus.specificationAdmitted);
    const std::string propFetch = requireOperation(
        session, session.beginLocalFetch("prop"));
    LOGOS_ASSERT_TRUE(session.downloadFinished(
        terminal(propFetch, cidForBytes(propBytes)),
        propBytes).accepted);
    propStatus = session.status("prop", kNow);
    LOGOS_ASSERT_TRUE(propStatus.specificationAdmitted);
    LOGOS_ASSERT_TRUE(
        propStatus.specification.kind
        == palace::StorageCatalogObjectKind::PropManifest);

    const auto leafStatusBefore = session.status("image", kNow);
    LOGOS_ASSERT_TRUE(leafStatusBefore.found);
    LOGOS_ASSERT_FALSE(leafStatusBefore.specificationAdmitted);
    const std::string leafFetch = requireOperation(
        session, session.beginLocalFetch("image"));
    LOGOS_ASSERT_TRUE(session.downloadFinished(
        terminal(leafFetch, cidForBytes(leafBytes)),
        leafBytes).accepted);
    const auto leafStatusAfter = session.status("image", kNow);
    LOGOS_ASSERT_TRUE(leafStatusAfter.specificationAdmitted);
    LOGOS_ASSERT_TRUE(
        leafStatusAfter.specification.kind
        == palace::StorageCatalogObjectKind::Blob);
    LOGOS_ASSERT_TRUE(
        leafStatusAfter.localPhase
        == palace::StorageCatalogLocalPhase::Verified);

    LOGOS_ASSERT_EQ(
        session.admitFinalizedPalaceManifest(
            cidForBytes(palaceBytes), palaceBytes).reason,
        std::string("duplicate-object-id"));
}

LOGOS_TEST(storage_catalog_bootstrap_accepts_manifest_cids_and_verifies_bytes)
{
    const std::string assetBytes = "asset-bytes";
    auto assetCommitment = committedChild("asset", assetBytes);
    assetCommitment.cid = cid('q');
    const std::string roomBytes =
        palace::canonicalStorageCatalogManifestV1(
            palace::StorageCatalogObjectKind::RoomManifest,
            "room",
            {assetCommitment});
    const auto roomCommitment = committedChild("room", roomBytes);
    const std::string palaceBytes =
        palace::canonicalStorageCatalogManifestV1(
            palace::StorageCatalogObjectKind::PalaceManifest,
            "palace",
            {roomCommitment});

    // Native Storage returns a canonical manifest CID that is independent of
    // the raw dataset SHA-256. It remains only a transport locator: exact
    // child bytes must still satisfy the manifest commitment before admission.
    auto nativeRoomCommitment = roomCommitment;
    nativeRoomCommitment.cid = cid('r');
    const std::string nativePalaceBytes =
        palace::canonicalStorageCatalogManifestV1(
            palace::StorageCatalogObjectKind::PalaceManifest,
            "palace",
            {nativeRoomCommitment});
    palace::PalaceStorageCatalogSession native;
    LOGOS_ASSERT_TRUE(native.configure(config()));
    LOGOS_ASSERT_TRUE(native.admitFinalizedPalaceManifest(
        cid('s'), nativePalaceBytes).accepted);
    const std::string nativeRoomFetch = requireOperation(
        native, native.beginLocalFetch("room"));
    LOGOS_ASSERT_TRUE(native.downloadFinished(
        terminal(nativeRoomFetch, nativeRoomCommitment.cid),
        roomBytes).accepted);
    const std::string nativeAssetFetch = requireOperation(
        native, native.beginLocalFetch("asset"));
    LOGOS_ASSERT_TRUE(native.downloadFinished(
        terminal(nativeAssetFetch, assetCommitment.cid),
        assetBytes).accepted);

    palace::PalaceStorageCatalogSession invalidRoot;
    LOGOS_ASSERT_TRUE(invalidRoot.configure(config()));
    LOGOS_ASSERT_EQ(invalidRoot.admitFinalizedPalaceManifest(
        "not-a-canonical-cid", nativePalaceBytes).reason,
        std::string("bootstrap-manifest-cid-invalid"));

    palace::PalaceStorageCatalogSession session;
    LOGOS_ASSERT_TRUE(session.configure(config()));

    const std::string malformed =
        palaceBytes.substr(0U, palaceBytes.size() - 1U);
    LOGOS_ASSERT_EQ(
        session.admitFinalizedPalaceManifest(
            cidForBytes(malformed), malformed).reason,
        std::string("bootstrap-palace-manifest-invalid"));

    const std::string nonCanonical = replaceOnce(
        palaceBytes, "children=1", "children=01");
    LOGOS_ASSERT_EQ(
        session.admitFinalizedPalaceManifest(
            cidForBytes(nonCanonical), nonCanonical).reason,
        std::string("bootstrap-palace-manifest-invalid"));

    const std::string trailing = palaceBytes + "trailing=1\n";
    LOGOS_ASSERT_EQ(
        session.admitFinalizedPalaceManifest(
            cidForBytes(trailing), trailing).reason,
        std::string("bootstrap-palace-manifest-invalid"));

    std::ostringstream duplicate;
    duplicate
        << "logos-palace-catalog-manifest-v1\n"
        << "kind=palace_manifest\n"
        << "object=palace\n"
        << "children=2\n"
        << "child=" << roomCommitment.objectId << ';'
        << roomCommitment.cid << ';' << roomCommitment.byteLength << ';'
        << roomCommitment.contentSha256 << '\n'
        << "child=" << roomCommitment.objectId << ';'
        << roomCommitment.cid << ';' << roomCommitment.byteLength << ';'
        << roomCommitment.contentSha256 << '\n';
    LOGOS_ASSERT_EQ(
        session.admitFinalizedPalaceManifest(
            cidForBytes(duplicate.str()), duplicate.str()).reason,
        std::string("bootstrap-palace-manifest-invalid"));

    auto wrongDigestChild = roomCommitment;
    wrongDigestChild.contentSha256 = std::string(64U, '0');
    const std::string wrongChildDigest =
        palace::canonicalStorageCatalogManifestV1(
            palace::StorageCatalogObjectKind::PalaceManifest,
            "palace",
            {wrongDigestChild});
    palace::PalaceStorageCatalogSession wrongCommitment;
    LOGOS_ASSERT_TRUE(wrongCommitment.configure(config()));
    LOGOS_ASSERT_TRUE(wrongCommitment.admitFinalizedPalaceManifest(
        cid('t'), wrongChildDigest).accepted);
    const std::string wrongCommitmentFetch = requireOperation(
        wrongCommitment, wrongCommitment.beginLocalFetch("room"));
    LOGOS_ASSERT_EQ(wrongCommitment.downloadFinished(
        terminal(wrongCommitmentFetch, wrongDigestChild.cid),
        roomBytes).reason,
        std::string("bootstrap-object-verification-failed"));

    auto boundedConfig = config();
    boundedConfig.maxObjectBytes = palaceBytes.size() - 1U;
    palace::PalaceStorageCatalogSession bounded;
    LOGOS_ASSERT_TRUE(bounded.configure(boundedConfig));
    LOGOS_ASSERT_EQ(
        bounded.admitFinalizedPalaceManifest(
            cidForBytes(palaceBytes), palaceBytes).reason,
        std::string("bootstrap-manifest-size-invalid"));

    LOGOS_ASSERT_TRUE(session.admitFinalizedPalaceManifest(
        cidForBytes(palaceBytes), palaceBytes).accepted);
}

LOGOS_TEST(storage_catalog_bootstrap_rejects_child_kind_parent_and_cycle)
{
    const std::string leafBytes = "leaf";
    const std::string propInsteadOfRoom =
        palace::canonicalStorageCatalogManifestV1(
            palace::StorageCatalogObjectKind::PropManifest,
            "room",
            {committedChild("leaf", leafBytes)});
    const std::string wrongKindRoot =
        palace::canonicalStorageCatalogManifestV1(
            palace::StorageCatalogObjectKind::PalaceManifest,
            "palace",
            {committedChild("room", propInsteadOfRoom)});
    palace::PalaceStorageCatalogSession wrongKind;
    LOGOS_ASSERT_TRUE(wrongKind.configure(config()));
    LOGOS_ASSERT_TRUE(wrongKind.admitFinalizedPalaceManifest(
        cidForBytes(wrongKindRoot), wrongKindRoot).accepted);
    const std::string wrongKindFetch = requireOperation(
        wrongKind, wrongKind.beginLocalFetch("room"));
    LOGOS_ASSERT_EQ(
        wrongKind.downloadFinished(
            terminal(
                wrongKindFetch,
                cidForBytes(propInsteadOfRoom)),
            propInsteadOfRoom).reason,
        std::string("bootstrap-child-kind-mismatch"));
    LOGOS_ASSERT_FALSE(
        wrongKind.status("room", kNow).specificationAdmitted);

    const std::string wrongObjectRoom =
        palace::canonicalStorageCatalogManifestV1(
            palace::StorageCatalogObjectKind::RoomManifest,
            "other-room",
            {committedChild("leaf", leafBytes)});
    const std::string wrongObjectRoot =
        palace::canonicalStorageCatalogManifestV1(
            palace::StorageCatalogObjectKind::PalaceManifest,
            "palace",
            {committedChild("room", wrongObjectRoom)});
    palace::PalaceStorageCatalogSession wrongObject;
    LOGOS_ASSERT_TRUE(wrongObject.configure(config()));
    LOGOS_ASSERT_TRUE(wrongObject.admitFinalizedPalaceManifest(
        cidForBytes(wrongObjectRoot), wrongObjectRoot).accepted);
    const std::string wrongObjectFetch = requireOperation(
        wrongObject, wrongObject.beginLocalFetch("room"));
    LOGOS_ASSERT_EQ(
        wrongObject.downloadFinished(
            terminal(
                wrongObjectFetch,
                cidForBytes(wrongObjectRoom)),
            wrongObjectRoom).reason,
        std::string("bootstrap-child-parent-mismatch"));

    const std::string malformedRoom =
        "logos-palace-catalog-manifest-v1\nkind=room_manifest\n";
    const std::string malformedRoot =
        palace::canonicalStorageCatalogManifestV1(
            palace::StorageCatalogObjectKind::PalaceManifest,
            "palace",
            {committedChild("room", malformedRoom)});
    palace::PalaceStorageCatalogSession malformed;
    LOGOS_ASSERT_TRUE(malformed.configure(config()));
    LOGOS_ASSERT_TRUE(malformed.admitFinalizedPalaceManifest(
        cidForBytes(malformedRoot), malformedRoot).accepted);
    const std::string malformedFetch = requireOperation(
        malformed, malformed.beginLocalFetch("room"));
    LOGOS_ASSERT_EQ(
        malformed.downloadFinished(
            terminal(malformedFetch, cidForBytes(malformedRoom)),
            malformedRoom).reason,
        std::string("bootstrap-child-kind-mismatch"));

    const std::string cyclePayload = "cycle-target";
    const std::string cycleRoom =
        palace::canonicalStorageCatalogManifestV1(
            palace::StorageCatalogObjectKind::RoomManifest,
            "room",
            {committedChild("room", cyclePayload)});
    const std::string cycleRoot =
        palace::canonicalStorageCatalogManifestV1(
            palace::StorageCatalogObjectKind::PalaceManifest,
            "palace",
            {committedChild("room", cycleRoom)});
    palace::PalaceStorageCatalogSession cycle;
    LOGOS_ASSERT_TRUE(cycle.configure(config()));
    LOGOS_ASSERT_TRUE(cycle.admitFinalizedPalaceManifest(
        cidForBytes(cycleRoot), cycleRoot).accepted);
    const std::string cycleFetch = requireOperation(
        cycle, cycle.beginLocalFetch("room"));
    LOGOS_ASSERT_EQ(
        cycle.downloadFinished(
            terminal(cycleFetch, cidForBytes(cycleRoom)),
            cycleRoom).reason,
        std::string("bootstrap-manifest-cycle"));
}

LOGOS_TEST(storage_catalog_bootstrap_pending_fetch_survives_restart_and_caps)
{
    const std::string leafBytes = "restart-leaf";
    auto leafCommitment = committedChild("leaf", leafBytes);
    leafCommitment.cid = cid('n');
    const std::string roomBytes =
        palace::canonicalStorageCatalogManifestV1(
            palace::StorageCatalogObjectKind::RoomManifest,
            "room",
            {leafCommitment});
    auto roomCommitment = committedChild("room", roomBytes);
    roomCommitment.cid = cid('o');
    const std::string palaceBytes =
        palace::canonicalStorageCatalogManifestV1(
            palace::StorageCatalogObjectKind::PalaceManifest,
            "palace",
            {roomCommitment});

    palace::PalaceStorageCatalogSession source;
    LOGOS_ASSERT_TRUE(source.configure(config()));
    LOGOS_ASSERT_TRUE(source.admitFinalizedPalaceManifest(
        cid('p'), palaceBytes).accepted);
    const std::string oldFetch = requireOperation(
        source, source.beginLocalFetch("room"));
    const std::string serialized = source.canonicalState();
    LOGOS_ASSERT_FALSE(serialized.empty());

    palace::PalaceStorageCatalogSession restored;
    LOGOS_ASSERT_TRUE(restored.configure(config()));
    LOGOS_ASSERT_TRUE(restored.restoreCanonicalState(serialized));
    LOGOS_ASSERT_TRUE(restored.reconciliationRequired());
    LOGOS_ASSERT_FALSE(
        restored.status("room", kNow).specificationAdmitted);
    const auto reconciliation = restored.reconcileAfterRestart(42U);
    LOGOS_ASSERT_TRUE(reconciliation.accepted);
    LOGOS_ASSERT_EQ(reconciliation.requeuedOperations.size(), 1U);
    LOGOS_ASSERT_TRUE(restored.downloadAcknowledged(
        acknowledgement(reconciliation.requeuedOperations[0])).accepted);
    LOGOS_ASSERT_TRUE(restored.downloadFinished(
        terminal(
            reconciliation.requeuedOperations[0].operationId,
            roomCommitment.cid),
        roomBytes).accepted);
    LOGOS_ASSERT_TRUE(
        restored.status("room", kNow).specificationAdmitted);
    LOGOS_ASSERT_EQ(
        restored.downloadFinished(
            terminal(oldFetch, roomCommitment.cid),
            roomBytes).reason,
        std::string("operation-already-completed"));

    const std::string admittedState = restored.canonicalState();
    palace::PalaceStorageCatalogSession roundtrip;
    LOGOS_ASSERT_TRUE(roundtrip.configure(config()));
    LOGOS_ASSERT_TRUE(roundtrip.restoreCanonicalState(admittedState));
    LOGOS_ASSERT_TRUE(roundtrip.reconcileAfterRestart(43U).accepted);
    LOGOS_ASSERT_TRUE(
        roundtrip.status("room", kNow).specificationAdmitted);
    LOGOS_ASSERT_FALSE(
        roundtrip.status("leaf", kNow).specificationAdmitted);
    const std::string leafFetch = requireOperation(
        roundtrip, roundtrip.beginLocalFetch("leaf"));
    LOGOS_ASSERT_TRUE(roundtrip.downloadFinished(
        terminal(leafFetch, leafCommitment.cid), leafBytes).accepted);
    LOGOS_ASSERT_TRUE(
        roundtrip.status("leaf", kNow).specificationAdmitted);

    auto capacityConfig = config();
    capacityConfig.maxObjects = 2U;
    palace::PalaceStorageCatalogSession capacity;
    LOGOS_ASSERT_TRUE(capacity.configure(capacityConfig));
    LOGOS_ASSERT_TRUE(capacity.admitFinalizedPalaceManifest(
        cid('p'), palaceBytes).accepted);
    const std::string capacityFetch = requireOperation(
        capacity, capacity.beginLocalFetch("room"));
    LOGOS_ASSERT_EQ(
        capacity.downloadFinished(
            terminal(capacityFetch, roomCommitment.cid),
            roomBytes).reason,
        std::string("object-capacity-exceeded"));
    LOGOS_ASSERT_FALSE(
        capacity.status("room", kNow).specificationAdmitted);
    LOGOS_ASSERT_FALSE(capacity.status("leaf", kNow).found);
}

LOGOS_TEST(storage_catalog_requires_exact_ack_terminal_and_generated_ids)
{
    palace::PalaceStorageCatalogSession session;
    LOGOS_ASSERT_TRUE(session.configure(config()));
    const std::string bytes = "network-object-bytes";
    LOGOS_ASSERT_TRUE(session.trackPublishedObject(
        blob("asset", bytes), cid('a')).accepted);

    const auto reserved = session.beginLocalFetch("asset");
    LOGOS_ASSERT_TRUE(reserved.accepted);
    LOGOS_ASSERT_TRUE(reserved.operation.has_value());
    LOGOS_ASSERT_EQ(
        reserved.operation->operationId,
        std::string("storage-41-1"));
    LOGOS_ASSERT_EQ(
        session.downloadFinished(
            terminal(reserved.operation->operationId, cid('a')),
            bytes).reason,
        std::string("operation-not-acknowledged"));
    LOGOS_ASSERT_EQ(
        session.operationAcknowledged(
            reserved.operation->operationId, true).reason,
        std::string("typed-download-acknowledgement-required"));

    auto wrongAck = acknowledgement(*reserved.operation);
    wrongAck.protocol = "logos.storage.legacy";
    LOGOS_ASSERT_FALSE(
        session.downloadAcknowledged(wrongAck).accepted);
    wrongAck = acknowledgement(*reserved.operation);
    wrongAck.version = 1U;
    LOGOS_ASSERT_FALSE(
        session.downloadAcknowledged(wrongAck).accepted);
    wrongAck = acknowledgement(*reserved.operation);
    wrongAck.operationId = "storage-41-99";
    LOGOS_ASSERT_FALSE(
        session.downloadAcknowledged(wrongAck).accepted);
    wrongAck = acknowledgement(*reserved.operation);
    wrongAck.cid = cid('b');
    LOGOS_ASSERT_FALSE(
        session.downloadAcknowledged(wrongAck).accepted);
    LOGOS_ASSERT_TRUE(
        session.pendingOperations()[0].phase
        == palace::StorageCatalogOperationPhase::AwaitingAcknowledgement);
    LOGOS_ASSERT_TRUE(session.downloadAcknowledged(
        acknowledgement(*reserved.operation)).accepted);

    auto wrongTerminal = terminal(
        reserved.operation->operationId, cid('a'));
    wrongTerminal.version = 1U;
    LOGOS_ASSERT_FALSE(
        session.downloadFinished(wrongTerminal, bytes).accepted);
    wrongTerminal = terminal(
        reserved.operation->operationId, cid('b'));
    LOGOS_ASSERT_FALSE(
        session.downloadFinished(wrongTerminal, bytes).accepted);
    LOGOS_ASSERT_TRUE(session.downloadFinished(
        terminal(reserved.operation->operationId, cid('a')),
        bytes).accepted);
    LOGOS_ASSERT_EQ(
        session.downloadFinished(
            terminal(reserved.operation->operationId, cid('a')),
            bytes).reason,
        std::string("operation-already-completed"));

    LOGOS_ASSERT_TRUE(session.markLocalMissing("asset").accepted);
    const auto rejected = session.beginLocalFetch("asset");
    LOGOS_ASSERT_TRUE(rejected.accepted);
    LOGOS_ASSERT_TRUE(session.downloadAcknowledged(
        acknowledgement(*rejected.operation, false)).accepted);
    LOGOS_ASSERT_TRUE(
        session.status("asset", kNow).localPhase
        == palace::StorageCatalogLocalPhase::Missing);
}

LOGOS_TEST(storage_catalog_attestation_retention_counts_evidence_locations)
{
    palace::PalaceStorageCatalogSession session;
    LOGOS_ASSERT_TRUE(session.configure(config()));
    const std::string bytes = "attested-object-bytes";
    LOGOS_ASSERT_TRUE(session.trackPublishedObject(
        blob("remote", bytes), cid('a')).accepted);
    LOGOS_ASSERT_TRUE(
        session.status("remote", kNow).retention
        == palace::StorageCatalogRetention::Unknown);

    LOGOS_ASSERT_TRUE(attest(
        session,
        "remote",
        account('b'),
        7U,
        kNow,
        kNow + 100U));
    auto status = session.status("remote", kNow);
    LOGOS_ASSERT_EQ(status.attestedHolderIds.size(), 1U);
    LOGOS_ASSERT_TRUE(
        status.retention == palace::StorageCatalogRetention::Degraded);

    LOGOS_ASSERT_TRUE(attest(
        session,
        "remote",
        account('c'),
        9U,
        kNow,
        kNow + 100U));
    status = session.status("remote", kNow);
    LOGOS_ASSERT_EQ(status.attestedHolderIds.size(), 2U);
    LOGOS_ASSERT_TRUE(
        status.retention == palace::StorageCatalogRetention::Redundant);

    const std::string localBytes = "locally-verified-object";
    LOGOS_ASSERT_TRUE(session.stagePublicationObject(
        blob("local", localBytes), localBytes).accepted);
    LOGOS_ASSERT_TRUE(publish(
        session, "local", localBytes, cid('b')));
    LOGOS_ASSERT_TRUE(
        session.status("local", kNow).retention
        == palace::StorageCatalogRetention::Degraded);
    LOGOS_ASSERT_TRUE(attest(
        session,
        "local",
        account('b'),
        10U,
        kNow,
        kNow + 100U));
    LOGOS_ASSERT_TRUE(
        session.status("local", kNow).retention
        == palace::StorageCatalogRetention::Redundant);
}

LOGOS_TEST(storage_catalog_attestation_receipt_tamper_settles_and_replays)
{
    palace::PalaceStorageCatalogSession session;
    LOGOS_ASSERT_TRUE(session.configure(config()));
    const std::string bytes = "receipt-tamper-object";
    LOGOS_ASSERT_TRUE(session.trackPublishedObject(
        blob("asset", bytes), cid('a')).accepted);

    const auto challenge = session.beginHolderAttestation(
        "asset",
        account('b'),
        3U,
        kNow,
        kNow + 100U);
    LOGOS_ASSERT_TRUE(challenge.accepted);
    auto tampered = receipt(*challenge.challenge);
    tampered.cid = cid('b');
    const std::string canonical =
        palace::canonicalHolderAttestationReceiptV1(tampered);
    ExactVerifier verifier(
        tampered.holderAccountId,
        tampered.holderKeyEpoch,
        canonical,
        "signature");
    LOGOS_ASSERT_EQ(
        session.completeHolderAttestation(
            tampered,
            "signature",
            kNow,
            verifier).reason,
        std::string("attestation-receipt-mismatch"));
    LOGOS_ASSERT_TRUE(
        session.pendingAttestationChallenges().empty());
    LOGOS_ASSERT_EQ(
        session.completeHolderAttestation(
            tampered,
            "signature",
            kNow,
            verifier).reason,
        std::string("attestation-challenge-already-completed"));
    LOGOS_ASSERT_TRUE(
        session.status("asset", kNow).attestedHolderIds.empty());
}

LOGOS_TEST(storage_catalog_attestation_rejects_wrong_key_signature_and_time)
{
    palace::PalaceStorageCatalogSession session;
    LOGOS_ASSERT_TRUE(session.configure(config()));
    const std::string bytes = "receipt-failure-object";
    LOGOS_ASSERT_TRUE(session.trackPublishedObject(
        blob("asset", bytes), cid('a')).accepted);

    const auto wrongKeyChallenge = session.beginHolderAttestation(
        "asset", account('b'), 4U, kNow, kNow + 100U);
    auto wrongKeyReceipt = receipt(*wrongKeyChallenge.challenge);
    const std::string wrongKeyCanonical =
        palace::canonicalHolderAttestationReceiptV1(wrongKeyReceipt);
    ExactVerifier wrongKeyVerifier(
        account('b'), 5U, wrongKeyCanonical, "signature");
    LOGOS_ASSERT_EQ(
        session.completeHolderAttestation(
            wrongKeyReceipt,
            "signature",
            kNow,
            wrongKeyVerifier).reason,
        std::string("attestation-signature-invalid"));

    const auto wrongSignatureChallenge =
        session.beginHolderAttestation(
            "asset", account('c'), 6U, kNow, kNow + 100U);
    auto wrongSignatureReceipt =
        receipt(*wrongSignatureChallenge.challenge);
    const std::string wrongSignatureCanonical =
        palace::canonicalHolderAttestationReceiptV1(
            wrongSignatureReceipt);
    ExactVerifier wrongSignatureVerifier(
        account('c'),
        6U,
        wrongSignatureCanonical,
        "expected-signature");
    LOGOS_ASSERT_EQ(
        session.completeHolderAttestation(
            wrongSignatureReceipt,
            "tampered-signature",
            kNow,
            wrongSignatureVerifier).reason,
        std::string("attestation-signature-invalid"));

    const auto expiredChallenge = session.beginHolderAttestation(
        "asset", account('d'), 8U, kNow, kNow + 10U);
    auto expiredReceipt = receipt(*expiredChallenge.challenge);
    const std::string expiredCanonical =
        palace::canonicalHolderAttestationReceiptV1(expiredReceipt);
    ExactVerifier expiredVerifier(
        account('d'), 8U, expiredCanonical, "signature");
    LOGOS_ASSERT_EQ(
        session.completeHolderAttestation(
            expiredReceipt,
            "signature",
            kNow + 11U,
            expiredVerifier).reason,
        std::string("attestation-receipt-expired"));

    const auto oversizedChallenge = session.beginHolderAttestation(
        "asset", account('e'), 9U, kNow, kNow + 100U);
    auto oversizedReceipt = receipt(*oversizedChallenge.challenge);
    const std::string oversizedCanonical =
        palace::canonicalHolderAttestationReceiptV1(oversizedReceipt);
    ExactVerifier oversizedVerifier(
        account('e'), 9U, oversizedCanonical, "unused");
    LOGOS_ASSERT_EQ(
        session.completeHolderAttestation(
            oversizedReceipt,
            std::string(
                session.configuration().maxSignatureBytes + 1U,
                'x'),
            kNow,
            oversizedVerifier).reason,
        std::string("attestation-signature-invalid"));
    LOGOS_ASSERT_TRUE(
        session.pendingAttestationChallenges().empty());
    LOGOS_ASSERT_EQ(session.completedChallengeIds().size(), 4U);
}

LOGOS_TEST(storage_catalog_attestation_caps_expiry_and_abandon_are_explicit)
{
    auto limits = config();
    limits.maxPendingAttestationChallenges = 1U;
    limits.maxAttestationsPerObject = 2U;
    palace::PalaceStorageCatalogSession session;
    LOGOS_ASSERT_TRUE(session.configure(limits));
    const std::string bytes = "bounded-challenge-object";
    LOGOS_ASSERT_TRUE(session.trackPublishedObject(
        blob("asset", bytes), cid('a')).accepted);

    LOGOS_ASSERT_FALSE(session.beginHolderAttestation(
        "asset",
        account('a'),
        1U,
        kNow,
        kNow + 100U).accepted);
    LOGOS_ASSERT_FALSE(session.beginHolderAttestation(
        "asset",
        std::string(64U, 'A'),
        1U,
        kNow,
        kNow + 100U).accepted);
    LOGOS_ASSERT_FALSE(session.beginHolderAttestation(
        "asset",
        account('b'),
        1U,
        kNow,
        kNow + limits.maxChallengeLifetimeSeconds + 1U).accepted);

    const auto first = session.beginHolderAttestation(
        "asset", account('b'), 1U, kNow, kNow + 10U);
    LOGOS_ASSERT_TRUE(first.accepted);
    LOGOS_ASSERT_FALSE(session.beginHolderAttestation(
        "asset", account('c'), 2U, kNow, kNow + 10U).accepted);
    LOGOS_ASSERT_TRUE(session.abandonHolderAttestation(
        first.challenge->challengeId).accepted);
    LOGOS_ASSERT_EQ(
        session.abandonHolderAttestation(
            first.challenge->challengeId).reason,
        std::string("attestation-challenge-already-completed"));

    LOGOS_ASSERT_TRUE(attest(
        session,
        "asset",
        account('b'),
        1U,
        kNow,
        kNow + 10U));
    const auto second = session.beginHolderAttestation(
        "asset", account('c'), 2U, kNow, kNow + 10U);
    LOGOS_ASSERT_TRUE(second.accepted);
    LOGOS_ASSERT_FALSE(session.beginHolderAttestation(
        "asset", account('d'), 3U, kNow, kNow + 10U).accepted);
    LOGOS_ASSERT_TRUE(
        session.status("asset", kNow + 11U)
            .attestedHolderIds.empty());
    LOGOS_ASSERT_TRUE(session.abandonHolderAttestation(
        second.challenge->challengeId).accepted);
    LOGOS_ASSERT_TRUE(session.beginHolderAttestation(
        "asset", account('b'), 4U, kNow + 11U, kNow + 20U).accepted);
}

LOGOS_TEST(storage_catalog_retry_abandon_and_recent_replay_are_bounded)
{
    auto limits = config();
    limits.maxCompletedOperations = 2U;
    limits.maxCompletedChallenges = 2U;
    palace::PalaceStorageCatalogSession session;
    LOGOS_ASSERT_TRUE(session.configure(limits));
    const std::string bytes = "retry-and-replay-object";
    LOGOS_ASSERT_TRUE(session.trackPublishedObject(
        blob("asset", bytes), cid('a')).accepted);

    const std::string firstFetch = requireOperation(
        session, session.beginLocalFetch("asset"));
    const auto retried = session.retryOperation(firstFetch);
    LOGOS_ASSERT_TRUE(retried.accepted);
    LOGOS_ASSERT_TRUE(retried.operation.has_value());
    LOGOS_ASSERT_NE(
        retried.operation->operationId, firstFetch);
    LOGOS_ASSERT_EQ(
        session.downloadFinished(
            terminal(firstFetch, cid('a')), bytes).reason,
        std::string("operation-already-completed"));
    LOGOS_ASSERT_TRUE(session.abandonOperation(
        retried.operation->operationId).accepted);

    for (char holder : {'b', 'c', 'd'}) {
        const auto challenge = session.beginHolderAttestation(
            "asset",
            account(holder),
            1U,
            kNow,
            kNow + 10U);
        LOGOS_ASSERT_TRUE(challenge.accepted);
        LOGOS_ASSERT_TRUE(session.abandonHolderAttestation(
            challenge.challenge->challengeId).accepted);
    }
    LOGOS_ASSERT_EQ(session.completedOperationIds().size(), 2U);
    LOGOS_ASSERT_EQ(session.completedChallengeIds().size(), 2U);
}

LOGOS_TEST(storage_catalog_restore_blocks_claims_until_strict_reconciliation)
{
    palace::PalaceStorageCatalogSession source;
    LOGOS_ASSERT_TRUE(source.configure(config()));
    const std::string bytes = "restart-published-object";
    LOGOS_ASSERT_TRUE(source.trackPublishedObject(
        blob("asset", bytes), cid('a')).accepted);
    LOGOS_ASSERT_TRUE(attest(
        source,
        "asset",
        account('b'),
        3U,
        kNow,
        kNow + 100U));
    const std::string oldFetch = requireOperation(
        source, source.beginLocalFetch("asset"));
    const auto oldChallenge = source.beginHolderAttestation(
        "asset",
        account('c'),
        4U,
        kNow,
        kNow + 100U);
    LOGOS_ASSERT_TRUE(oldChallenge.accepted);
    const auto oldReceipt = receipt(*oldChallenge.challenge);
    const std::string oldCanonicalReceipt =
        palace::canonicalHolderAttestationReceiptV1(oldReceipt);
    ExactVerifier oldVerifier(
        account('c'),
        4U,
        oldCanonicalReceipt,
        "signature");

    const std::string volatileBytes = "volatile-staged-object";
    LOGOS_ASSERT_TRUE(source.stagePublicationObject(
        blob("volatile", volatileBytes), volatileBytes).accepted);
    const std::string serialized = source.canonicalState();
    LOGOS_ASSERT_FALSE(serialized.empty());

    palace::PalaceStorageCatalogSession restored;
    LOGOS_ASSERT_TRUE(restored.configure(config()));
    LOGOS_ASSERT_TRUE(restored.restoreCanonicalState(serialized));
    LOGOS_ASSERT_TRUE(restored.reconciliationRequired());
    LOGOS_ASSERT_EQ(restored.canonicalState(), serialized);
    const auto blockedStatus = restored.status("asset", kNow);
    LOGOS_ASSERT_TRUE(blockedStatus.found);
    LOGOS_ASSERT_TRUE(blockedStatus.reconciliationRequired);
    LOGOS_ASSERT_TRUE(
        blockedStatus.localPhase
        == palace::StorageCatalogLocalPhase::Missing);
    LOGOS_ASSERT_TRUE(blockedStatus.attestedHolderIds.empty());
    LOGOS_ASSERT_TRUE(
        blockedStatus.retention
        == palace::StorageCatalogRetention::Unknown);
    LOGOS_ASSERT_TRUE(
        restored.status("volatile", kNow).publicationStage
        == palace::StorageCatalogPublicationStage::Failed);
    LOGOS_ASSERT_TRUE(restored.pendingOperations().empty());
    LOGOS_ASSERT_TRUE(
        restored.pendingAttestationChallenges().empty());
    LOGOS_ASSERT_EQ(
        restored.beginLocalFetch("asset").reason,
        std::string("restart-reconciliation-required"));
    LOGOS_ASSERT_EQ(
        restored.downloadFinished(
            terminal(oldFetch, cid('a')), bytes).reason,
        std::string("restart-reconciliation-required"));
    LOGOS_ASSERT_FALSE(restored.beginHolderAttestation(
        "asset",
        account('d'),
        5U,
        kNow,
        kNow + 100U).accepted);

    const auto sameEpoch = restored.reconcileAfterRestart(41U);
    LOGOS_ASSERT_FALSE(sameEpoch.accepted);
    LOGOS_ASSERT_TRUE(restored.reconciliationRequired());
    const auto reconciliation = restored.reconcileAfterRestart(42U);
    LOGOS_ASSERT_TRUE(reconciliation.accepted);
    LOGOS_ASSERT_FALSE(restored.reconciliationRequired());
    LOGOS_ASSERT_EQ(reconciliation.requeuedOperations.size(), 1U);
    LOGOS_ASSERT_TRUE(
        restored.status("asset", kNow).attestedHolderIds.empty());
    LOGOS_ASSERT_TRUE(
        restored.status("volatile", kNow).publicationStage
        == palace::StorageCatalogPublicationStage::Failed);
    LOGOS_ASSERT_EQ(
        restored.downloadFinished(
            terminal(oldFetch, cid('a')), bytes).reason,
        std::string("operation-already-completed"));
    LOGOS_ASSERT_EQ(
        restored.completeHolderAttestation(
            oldReceipt,
            "signature",
            kNow,
            oldVerifier).reason,
        std::string("attestation-challenge-already-completed"));

    const auto replacement = reconciliation.requeuedOperations[0];
    LOGOS_ASSERT_TRUE(restored.downloadAcknowledged(
        acknowledgement(replacement)).accepted);
    LOGOS_ASSERT_TRUE(restored.downloadFinished(
        terminal(replacement.operationId, cid('a')), bytes).accepted);

    const std::string reconciledState = restored.canonicalState();
    palace::PalaceStorageCatalogSession roundtrip;
    LOGOS_ASSERT_TRUE(roundtrip.configure(config()));
    LOGOS_ASSERT_TRUE(
        roundtrip.restoreCanonicalState(reconciledState));
    LOGOS_ASSERT_TRUE(roundtrip.reconciliationRequired());
    LOGOS_ASSERT_FALSE(roundtrip.reconcileAfterRestart(42U).accepted);
    LOGOS_ASSERT_TRUE(roundtrip.reconcileAfterRestart(43U).accepted);

    auto mismatchedConfig = config();
    mismatchedConfig.maxObjects = 2U;
    palace::PalaceStorageCatalogSession mismatched;
    LOGOS_ASSERT_TRUE(mismatched.configure(mismatchedConfig));
    const std::string before = mismatched.canonicalState();
    LOGOS_ASSERT_FALSE(
        mismatched.restoreCanonicalState(serialized));
    LOGOS_ASSERT_EQ(mismatched.canonicalState(), before);
}

LOGOS_TEST(storage_catalog_schema_v3_preserves_overflow_without_wrap)
{
    palace::PalaceStorageCatalogSession source;
    LOGOS_ASSERT_TRUE(source.configure(config()));
    const std::string bytes = "overflow-state-object";
    LOGOS_ASSERT_TRUE(source.trackPublishedObject(
        blob("asset", bytes), cid('a')).accepted);
    std::string serialized = source.canonicalState();
    const std::string oldState = "state;41;0;0\n";
    const std::string exhaustedState = "state;41;"
        + std::to_string(std::numeric_limits<std::uint64_t>::max())
        + ';'
        + std::to_string(std::numeric_limits<std::uint64_t>::max())
        + '\n';
    serialized = replaceOnce(
        serialized, oldState, exhaustedState);
    serialized = resignState(std::move(serialized));

    palace::PalaceStorageCatalogSession restored;
    LOGOS_ASSERT_TRUE(restored.configure(config()));
    LOGOS_ASSERT_TRUE(restored.restoreCanonicalState(serialized));
    LOGOS_ASSERT_TRUE(restored.reconciliationRequired());
    LOGOS_ASSERT_EQ(
        restored.lastOperationSequence(),
        std::numeric_limits<std::uint64_t>::max());
    LOGOS_ASSERT_EQ(
        restored.lastChallengeSequence(),
        std::numeric_limits<std::uint64_t>::max());
    LOGOS_ASSERT_FALSE(restored.reconcileAfterRestart(41U).accepted);
    LOGOS_ASSERT_TRUE(restored.reconcileAfterRestart(42U).accepted);
    LOGOS_ASSERT_EQ(restored.lastOperationSequence(), 0U);
    LOGOS_ASSERT_EQ(restored.lastChallengeSequence(), 0U);
    LOGOS_ASSERT_TRUE(restored.beginLocalFetch("asset").accepted);
}
