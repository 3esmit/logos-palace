#pragma once

#include "palace_lez_explorer_finality.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace palace::lez_explorer_protocol {

enum class TransactionKind : std::uint8_t {
    Public = 0U,
    PrivacyPreserving = 1U,
    ProgramDeployment = 2U,
};

struct TransactionV1 {
    TransactionKind kind = TransactionKind::Public;
    std::string hashHex;
    std::string programIdBase58;
    std::vector<std::string> accountIdsBase58;
    std::vector<std::uint32_t> instructionWords;
    std::size_t nonceCount = 0U;
    std::size_t signatureCount = 0U;
};

struct BlockV1 {
    std::uint64_t id = 0U;
    std::uint64_t timestamp = 0U;
    std::string previousHashHex;
    std::string hashHex;
    std::string bedrockStatus;
    std::vector<TransactionV1> transactions;
};

struct BlockPageParseResultV1 {
    bool accepted = false;
    std::string reason;
    std::vector<BlockV1> blocks;
};

// Shared strict explorer boundary. JSON objects retain decoded keys while
// parsing, so duplicate keys cannot be hidden by a DOM conversion.
BlockPageParseResultV1 parseBlockPage(
    const std::string& body,
    const PalaceLezExplorerFinalityLimitsV1& limits);

bool networkFingerprintAccepted(
    const PalaceLezExplorerNetworkFingerprintV1& fingerprint);
bool limitsAccepted(
    const PalaceLezExplorerFinalityLimitsV1& limits);
bool responseMetadataAccepted(
    const PalaceLezExplorerHttpResponseV1& response,
    const PalaceLezExplorerCommandV1& command,
    const PalaceLezExplorerNetworkFingerprintV1& fingerprint,
    std::string& reason);

PalaceLezExplorerCommandV1 makeBlocksCommand(
    const PalaceLezExplorerNetworkFingerprintV1& fingerprint,
    const PalaceLezExplorerFinalityLimitsV1& limits,
    std::uint64_t sequence,
    std::size_t blocksScanned,
    std::optional<std::uint64_t> before);

} // namespace palace::lez_explorer_protocol
