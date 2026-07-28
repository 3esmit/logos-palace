#pragma once

#include <string>

namespace palace::crypto {

// Lower-case SHA-256 of arbitrary bytes.  Kept dependency-free because this
// value is part of canonical cross-client receipts and cache handles.
std::string sha256Hex(const std::string& bytes);

} // namespace palace::crypto
