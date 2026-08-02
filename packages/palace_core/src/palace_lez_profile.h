#pragma once

#include <optional>
#include <string>

#include "palace_lez.h"

namespace palace {

// A profile is selected once, during startup, before any Palace state store
// is opened. Its values are compiled into the client; user input never
// supplies an origin, program identity, or finality policy.
struct PalaceLezProfileV1 {
    std::string id;
    PalaceLezNetworkFingerprint network;
    std::string walletConfigJson;
    std::string expectedModuleName;
    std::string expectedSequencerOrigin;
    bool publicFinalityAvailable = false;

    bool acceptsLiveModule(
        const std::string& moduleName,
        const std::string& moduleVersion,
        const std::string& sequencerOrigin) const;
};

enum class PalaceLezProfileBindingStatusV1 {
    Bound,
    InvalidArgument,
    UnknownProfile,
    UnsafeHostRoot,
    InsecurePermissions,
    LegacyDirectRootState,
    BindingInvalid,
    BindingMismatch,
    IoError,
};

struct PalaceLezProfileBindingResultV1 {
    PalaceLezProfileBindingStatusV1 status =
        PalaceLezProfileBindingStatusV1::InvalidArgument;
    std::string reason;
    std::optional<PalaceLezProfileV1> profile;
    // Canonical, profile-specific root. All durable Palace stores must use
    // this path rather than the caller's host root.
    std::string persistenceRoot;

    bool accepted() const
    {
        return status == PalaceLezProfileBindingStatusV1::Bound
            && profile.has_value() && !persistenceRoot.empty();
    }
};

// Empty selector is the release profile. No selector other than the compiled
// identifiers is accepted.
std::optional<PalaceLezProfileV1> resolvePalaceLezProfileV1(
    const std::string& selector);

// Creates or validates an atomic, owner-only profile binding and returns an
// isolated root at <host>/palace-profiles/<profile-id>. A safe, recognized
// pre-profile direct-root install is upgraded only into release; local
// development rejects that state rather than crossing profile boundaries.
PalaceLezProfileBindingResultV1 bindPalaceLezProfileV1(
    const std::string& hostRoot,
    const std::string& selector);

const char* palaceLezProfileBindingStatusNameV1(
    PalaceLezProfileBindingStatusV1 status);

} // namespace palace
