#pragma once

#include "palace_lez.h"

#include <cstdint>
#include <string>

namespace palace {

enum class PalaceLezSubmissionIntentPhase : std::uint8_t {
    Prepared = 0U,
    MayHaveBeenSubmitted = 1U,
    Committed = 2U,
    Rejected = 3U,
};

struct PalaceLezSubmissionIntentV1 {
    std::string actionId;
    PalaceLezSubmissionIntentPhase phase =
        PalaceLezSubmissionIntentPhase::Prepared;
    std::uint64_t minimumFinalizedBlockExclusive = 0U;
    PalaceLezTransactionPlanV3 plan;
    std::string expectedRootDataSha256Hex;
    std::string transactionHash;
};

enum class PalaceLezSubmissionIntentStoreStatus : std::uint8_t {
    Saved = 0U,
    Loaded = 1U,
    NotFound = 2U,
    InvalidArgument = 3U,
    InvalidRecord = 4U,
    InsecurePermissions = 5U,
    IoError = 6U,
};

// Single unresolved Palace-root submission. Saves are checksummed, flushed,
// atomically renamed, and constrained to the write-ahead phase graph.
class PalaceLezSubmissionIntentStore final {
public:
    explicit PalaceLezSubmissionIntentStore(std::string directory);

    PalaceLezSubmissionIntentStoreStatus save(
        const PalaceLezSubmissionIntentV1& intent) const;
    PalaceLezSubmissionIntentStoreStatus load(
        PalaceLezSubmissionIntentV1& intent) const;

private:
    std::string directory_;
};

bool samePalaceLezSubmissionIntent(
    const PalaceLezSubmissionIntentV1& first,
    const PalaceLezSubmissionIntentV1& second);
const char* palaceLezSubmissionIntentStoreStatusName(
    PalaceLezSubmissionIntentStoreStatus status);

} // namespace palace
