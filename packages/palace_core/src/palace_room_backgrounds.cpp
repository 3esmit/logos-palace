#include "palace_room_backgrounds.h"

#include "palace_asset.h"
#include "palace_verified_asset_store.h"

#include <cstddef>

namespace palace {
namespace {

constexpr char kAtriumPng[] =
    "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52"
    "\x00\x00\x00\x10\x00\x00\x00\x09\x08\x02\x00\x00\x00\xb4\x48\x3b"
    "\x65\x00\x00\x00\x73\x49\x44\x41\x54\x78\xda\x95\x91\xcb\x09\x80"
    "\x30\x10\x44\xa7\x29\xff\x5a\x87\x7f\x6d\xc1\xbf\x62\x63\x56\x62"
    "\x3f\x0e\x04\x96\x25\x87\x60\x20\x87\xc7\x9b\xc3\x4c\x58\xbc\xcf"
    "\xed\xf5\xb0\x77\xe9\x35\x16\x4b\x1d\x09\xd0\x5a\x86\x40\x69\x00"
    "\x73\x15\x6e\x6d\x72\x0e\xb9\x01\x06\x96\x21\x50\x8a\xc1\xd1\x67"
    "\x53\x19\xac\x4d\x4c\x60\x60\x40\x0c\x81\x52\x1b\x48\x35\x83\x3f"
    "\xf3\x60\x8a\x18\xe8\x0d\x8e\x79\x60\x11\x03\xbd\xc1\x3d\x0f\xf2"
    "\x7d\x5d\xed\x98\x07\xdf\x3b\x7c\x56\x81\xc4\x54\x9e\xdf\xf7\x62"
    "\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82";

constexpr char kLoungePng[] =
    "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52"
    "\x00\x00\x00\x10\x00\x00\x00\x09\x08\x02\x00\x00\x00\xb4\x48\x3b"
    "\x65\x00\x00\x00\x74\x49\x44\x41\x54\x78\xda\x95\x91\x49\x0a\x80"
    "\x30\x10\x04\xfb\x43\xee\x7a\x76\xd7\xab\xbb\xbe\xc0\xe7\xf8\x10"
    "\x5f\x69\x43\x60\x18\x72\x08\x06\x72\x28\xaa\x0f\xdd\x61\x70\x3f"
    "\xaf\xd7\x43\xda\xed\xc5\x78\x45\xf5\x22\x40\x6b\x19\x02\xa5\x01"
    "\x84\xd5\x9c\xb4\x5b\x3e\x9c\x06\x18\x58\x86\x40\x29\x06\x59\x7f"
    "\x04\xe5\x14\x37\x2b\x81\x81\x01\x31\x04\x4a\x6d\x20\xd5\x0c\xfe"
    "\xcc\x83\x29\x62\xa0\x37\x38\xe6\x81\x45\x0c\xf4\x06\xf7\x3c\xc8"
    "\xf7\x75\xb5\x63\x1e\x7c\xef\xf0\x01\xc1\xbf\x98\xd4\xf9\xe9\x88"
    "\xcc\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82";

constexpr char kAtriumDigest[] =
    "3bd13dc41f3e27a7eabf45c73188498b95e3e5e475e6fcb967308477afd522be";
constexpr char kLoungeDigest[] =
    "d2068f9cc4848b29882e532580c2b455ef5d243e7b38f16d590937fbef720486";

VerifiedAsset stageFixture(const VerifiedAssetStore& store,
                           const std::string& roomId,
                           const char* digest,
                           const char* bytes,
                           std::size_t byteCount)
{
    AssetRefV1 reference;
    reference.sourceCid = "builtin-room-" + roomId + "-source-v1";
    reference.derivativeCid = "builtin-room-" + roomId + "-png-v1";
    reference.byteLength = byteCount;
    reference.mediaType = "image/png";
    reference.width = 16;
    reference.height = 9;
    reference.technicalProfile = "palace-png-v1";
    reference.contentSha256 = digest;
    return store.stagePngDerivative(reference, std::string(bytes, byteCount));
}

} // namespace

bool RoomBackgroundCatalog::stageBuiltInFixtures(const VerifiedAssetStore& store)
{
    m_handles.clear();

    const VerifiedAsset atrium = stageFixture(
        store, "atrium", kAtriumDigest, kAtriumPng, sizeof(kAtriumPng) - 1U);
    if (atrium.accepted)
        m_handles.emplace("atrium", atrium.handle);

    const VerifiedAsset lounge = stageFixture(
        store, "lounge", kLoungeDigest, kLoungePng, sizeof(kLoungePng) - 1U);
    if (lounge.accepted)
        m_handles.emplace("lounge", lounge.handle);

    return m_handles.size() == 2U;
}

std::string RoomBackgroundCatalog::handleForRoom(const std::string& roomId) const
{
    const auto found = m_handles.find(roomId);
    return found == m_handles.end() ? std::string() : found->second;
}

} // namespace palace
