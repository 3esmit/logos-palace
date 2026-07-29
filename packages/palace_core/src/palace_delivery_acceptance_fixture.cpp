#include "palace_delivery_acceptance_fixture.h"

#include <array>
#include <utility>

namespace palace {
namespace {

struct FixtureVector {
    const char* userId;
    const char* displayName;
    std::int64_t keyEpoch;
    const char* privateKeyHex;
    const char* publicKeyHex;
};

// RFC 8032 Ed25519 test vectors. Keeping this list in an acceptance-named
// translation unit prevents fixture credentials from being mistaken for a
// production keystore or finalized authority source.
constexpr std::array<FixtureVector, 3> kFixtureVectors{{
    {
        "alice",
        "Alice",
        1,
        "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
    },
    {
        "bob",
        "Bob",
        2,
        "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
        "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
    },
    {
        "carol",
        "Carol",
        3,
        "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
        "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
    },
}};

constexpr const char* kAcceptanceInjectorPublicKey =
    "278117fc144c72340f67d0f2316e8386"
    "ceffbf2b2428c9c51fef7c597f1d426e";

// SHA-256 fixture roots keep the acceptance snapshot inside the same exact
// 32-byte finalized-authority contract as production projections.
constexpr const char* kAtriumSharedStateRoot =
    "42acbc2f585d51964c35ddf0b33ac1e8"
    "d72b7bab46956c8f932dbaffe1f176e4";
constexpr const char* kLoungeSharedStateRoot =
    "dc6630ae2a3eb10d5b8053181f687ab0"
    "46b97fc134f8caa564c8685ad193d14d";

bool loadVector(const FixtureVector& vector, DeliveryAcceptanceIdentity& identity)
{
    DeliveryAcceptanceIdentity candidate;
    if (!Ed25519KeyPair::fromPrivateKeyHex(
            vector.privateKeyHex, candidate.signer)
        || candidate.signer.publicKeyHex() != vector.publicKeyHex) {
        return false;
    }
    candidate.userId = vector.userId;
    candidate.displayName = vector.displayName;
    candidate.keyEpoch = vector.keyEpoch;
    identity = std::move(candidate);
    return true;
}

} // namespace

bool bootstrapDeliveryAcceptanceAuthority(AuthorityProjection& authority)
{
    AuthoritySnapshotV1 snapshot;
    snapshot.palaceId = "palace-1";
    snapshot.ownerUserId = "alice";
    snapshot.entryRoomId = "atrium";
    snapshot.rooms = {
        {"atrium", false, kAtriumSharedStateRoot, 9},
        {"lounge", false, kLoungeSharedStateRoot, 4},
    };

    for (const FixtureVector& vector : kFixtureVectors) {
        DeliveryAcceptanceIdentity identity;
        if (!loadVector(vector, identity))
            return false;
        snapshot.users.push_back(
            {identity.userId, identity.signer.publicKeyHex(), identity.keyEpoch});
    }
    snapshot.users.push_back(
        {"acceptance-injector", kAcceptanceInjectorPublicKey, 4});
    return authority.replaceFinalized(snapshot, 1);
}

bool deliveryAcceptanceIdentity(const std::string& userId,
                                DeliveryAcceptanceIdentity& identity)
{
    for (const FixtureVector& vector : kFixtureVectors) {
        if (userId == vector.userId)
            return loadVector(vector, identity);
    }
    return false;
}

std::int64_t deliveryAcceptanceRoomEpoch(const std::string& roomId)
{
    if (roomId == "atrium")
        return 9;
    if (roomId == "lounge")
        return 4;
    return -1;
}

std::map<std::string, std::string> deliveryAcceptanceAllowedProps()
{
    return {{"hat", "fixture-cid-hat"}};
}

} // namespace palace
