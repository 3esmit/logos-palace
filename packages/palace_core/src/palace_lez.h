#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace palace {

// Mirrors PalaceInstruction in program/palace_core. Values must retain that
// declaration order because the LEZ guest uses serde enum indexes.
enum class PalaceLezInstructionKind : std::uint32_t {
    BindDeliveryKey = 0,
    DelegateModerator = 1,
    RevokeModerator = 2,
    BanUser = 3,
    BanAsset = 4,
    SetRoomLocked = 5,
    PublishManifest = 6,
    SetSharedSpotRevision = 7,
};

struct PalaceLezInstructionV1 {
    PalaceLezInstructionKind kind = PalaceLezInstructionKind::PublishManifest;
    std::string subjectAccountIdHex;
    std::string deliveryKeyHex;
    std::uint64_t keyEpoch = 0;
    std::string roomId;
    std::string cid;
    bool locked = false;
    std::string spotId;
    std::uint64_t revision = 0;
};

struct PalaceLezSubmitRequestV1 {
    std::uint64_t orderedActionId = 0;
    std::string stateAccountIdHex;
    std::string callerAccountIdHex;
    std::string programIdHex;
    PalaceLezInstructionV1 instruction;
};

struct PalaceLezWireInstruction {
    bool accepted = false;
    std::string reason;
    std::vector<std::uint32_t> words;
};

struct PalaceLezSubmissionResult {
    bool accepted = false;
    std::string reason;
    std::string transactionHash;
};

// Owns the C++ side of the LEZ public-transaction wire contract. It encodes
// GuestInstruction::Apply using risc0_zkvm::serde's u32-word rules; the guest
// test contains a matching serializer fixture.
class PalaceLezCodec {
public:
    static bool parseOrderedActionId(const std::string& value, std::uint64_t& output);
    static PalaceLezWireInstruction encodeApply(const PalaceLezSubmitRequestV1& request);
    static PalaceLezSubmissionResult parseSubmissionResult(const std::string& responseJson);
};

} // namespace palace
