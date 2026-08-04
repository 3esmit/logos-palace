#pragma once

#include <string>

#include "palace_asset_authoring.h"

namespace palace {

enum class AssetAuthoringStoreStatus {
    Saved,
    Loaded,
    NotFound,
    InvalidArgument,
    InvalidRecord,
    InsecurePath,
    IoError,
};

// Crash-safe, owner-only sealed ledger for staged metadata, review decisions,
// verified Storage CIDs, and MVP role assignments.
class AssetAuthoringStore {
public:
    explicit AssetAuthoringStore(std::string instanceRoot);

    AssetAuthoringStoreStatus load(
        AssetAuthoringStateV1& state) const;
    AssetAuthoringStoreStatus save(
        const AssetAuthoringStateV1& state) const;

    static bool isKnownRoom(const std::string& roomId);

private:
    std::string m_instanceRoot;
};

const char* assetAuthoringStoreStatusName(
    AssetAuthoringStoreStatus status);

} // namespace palace
