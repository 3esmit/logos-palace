#pragma once

#include <string>

namespace palace {

class PalaceLezTransactionCoordinator;

enum class PalaceLezCoordinatorStoreStatus {
    Saved,
    Loaded,
    NotFound,
    InvalidArgument,
    InvalidRecord,
    InsecurePermissions,
    UnsafePath,
    IoError,
    CoordinatorRejected,
};

// Durable file boundary for the LEZ transaction coordinator. The fixed child
// filename keeps callers from supplying a path outside instancePersistenceRoot.
// Loading validates the complete outer record and coordinator snapshot before
// replacing coordinator state.
class PalaceLezCoordinatorStore {
public:
    explicit PalaceLezCoordinatorStore(std::string instancePersistenceRoot);

    PalaceLezCoordinatorStoreStatus save(
        const PalaceLezTransactionCoordinator& coordinator) const;
    PalaceLezCoordinatorStoreStatus load(
        PalaceLezTransactionCoordinator& coordinator) const;

private:
    std::string instancePersistenceRoot_;
};

const char* palaceLezCoordinatorStoreStatusName(
    PalaceLezCoordinatorStoreStatus status);

} // namespace palace
