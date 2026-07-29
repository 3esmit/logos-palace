#include "logos_test.h"

#include "palace_storage_cid.h"

#include <string>

namespace {

constexpr char kNativeBase58CidV1[] =
    "zb2rhWeJ5rsKhbfi31kikfdS3hE2pvWzhW27Es4Jmc863vFdx";
constexpr char kOverlongVersionCidV1[] =
    "z5KmBFEjTba7gnFbbavM3Vt1u54hssGFq7Fb6hcN1KCZ6t64Rh1p";
constexpr char kOverlongCodecCidV1[] =
    "z4ZYyzU35QmbF84UJdZsMRPmTRgJiXLoADkwoCXNypkap4HtLFp";
constexpr char kWrongMultihashCidV1[] =
    "zb2t1ZRNxvomGqkUm2sBgpYswDjNLtwjSDJBSg6W1wwQY86ea";
constexpr char kWrongDigestLengthCidV1[] =
    "z8iCiHQZLLjGf23iJ6N4FNdJqpoiz291YDC7NqVB52BM16HB";

} // namespace

LOGOS_TEST(storage_cid_accepts_canonical_native_base58btc_cidv1)
{
    std::string digest;
    LOGOS_ASSERT_TRUE(palace::canonicalStorageCidSha256(
        kNativeBase58CidV1, digest));
    LOGOS_ASSERT_EQ(
        digest,
        std::string(
            "000102030405060708090a0b0c0d0e0f"
            "101112131415161718191a1b1c1d1e1f"));
    LOGOS_ASSERT_TRUE(
        palace::isCanonicalStorageCid(kNativeBase58CidV1));
}

LOGOS_TEST(storage_cid_rejects_noncanonical_or_malformed_base58btc_cidv1)
{
    std::string digest = "must-clear";
    LOGOS_ASSERT_FALSE(palace::canonicalStorageCidSha256(
        std::string("z1") + (kNativeBase58CidV1 + 1),
        digest));
    LOGOS_ASSERT_TRUE(digest.empty());
    LOGOS_ASSERT_FALSE(
        palace::isCanonicalStorageCid(kOverlongVersionCidV1));
    LOGOS_ASSERT_FALSE(
        palace::isCanonicalStorageCid(kOverlongCodecCidV1));
    LOGOS_ASSERT_FALSE(
        palace::isCanonicalStorageCid(kWrongMultihashCidV1));
    LOGOS_ASSERT_FALSE(
        palace::isCanonicalStorageCid(kWrongDigestLengthCidV1));

    std::string invalidCharacter = kNativeBase58CidV1;
    invalidCharacter.back() = '0';
    LOGOS_ASSERT_FALSE(
        palace::isCanonicalStorageCid(invalidCharacter));
    std::string wrongMultibase = kNativeBase58CidV1;
    wrongMultibase.front() = 'Z';
    LOGOS_ASSERT_FALSE(
        palace::isCanonicalStorageCid(wrongMultibase));
}
