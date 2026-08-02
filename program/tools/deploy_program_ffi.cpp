extern "C" {
#include <wallet_ffi.h>
}

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

// Testnet's ProgramDeployment transaction adds five envelope bytes and caps
// the complete transaction at 614,200 bytes.
constexpr std::uintmax_t MaxProgramBytes = 614195U;

bool isCanonicalHash(const std::string& value) {
    return value.size() == 64U
        && std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
               return std::isdigit(ch) != 0
                   || (ch >= static_cast<unsigned char>('a')
                       && ch <= static_cast<unsigned char>('f'));
           });
}

bool isRisc0Program(const std::vector<std::uint8_t>& bytes) {
    // RISC Zero Binary Format header followed by a little-endian RISC-V ELF.
    constexpr std::size_t ElfOffset = 32U;
    return bytes.size() >= ElfOffset + 20U
        && bytes[0] == static_cast<std::uint8_t>('R')
        && bytes[1] == static_cast<std::uint8_t>('0')
        && bytes[2] == static_cast<std::uint8_t>('B')
        && bytes[3] == static_cast<std::uint8_t>('F')
        && bytes[ElfOffset] == 0x7fU
        && bytes[ElfOffset + 1U] == static_cast<std::uint8_t>('E')
        && bytes[ElfOffset + 2U] == static_cast<std::uint8_t>('L')
        && bytes[ElfOffset + 3U] == static_cast<std::uint8_t>('F')
        && bytes[ElfOffset + 5U] == 1U
        && bytes[ElfOffset + 18U] == 0xf3U
        && bytes[ElfOffset + 19U] == 0x00U;
}

std::vector<std::uint8_t> readProgram(
    const std::filesystem::path& path) {
    std::error_code error;
    const auto byteLength = std::filesystem::file_size(path, error);
    if (error || byteLength == 0U || byteLength > MaxProgramBytes) {
        return {};
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr
            << "usage: deploy_program_ffi CONFIG STORAGE ELF\n";
        return 64;
    }

    const char* password = std::getenv("PALACE_LEZ_WALLET_PASSWORD");
    if (password == nullptr || password[0] == '\0') {
        std::cerr << "PALACE_LEZ_WALLET_PASSWORD is required\n";
        return 64;
    }

    const std::filesystem::path configPath(argv[1]);
    const std::filesystem::path storagePath(argv[2]);
    const std::filesystem::path programPath(argv[3]);
    const bool storageExists = std::filesystem::exists(storagePath);
    if (!std::filesystem::is_regular_file(configPath)
        || (storageExists
                && !std::filesystem::is_regular_file(storagePath))) {
        std::cerr << "config must exist; storage must be absent or a file\n";
        return 64;
    }

    const auto program = readProgram(programPath);
    if (!isRisc0Program(program)) {
        std::cerr << "program must be a bounded RISC Zero RISC-V image\n";
        return 65;
    }

    WalletHandle* wallet = nullptr;
    if (storageExists) {
        wallet = wallet_ffi_open(
            configPath.c_str(),
            storagePath.c_str());
        if (wallet == nullptr) {
            std::cerr << "wallet open failed\n";
            return 70;
        }
    } else {
        FfiCreateWalletOutput created = wallet_ffi_create_new(
            configPath.c_str(),
            storagePath.c_str(),
            password);
        if (created.wallet == nullptr) {
            if (created.mnemonic != nullptr) {
                wallet_ffi_free_string(created.mnemonic);
            }
            std::cerr << "wallet creation failed\n";
            return 70;
        }

        wallet = created.wallet;

        // Deployment needs no funded account. Never print or retain this
        // disposable wallet's recovery phrase.
        if (created.mnemonic != nullptr) {
            wallet_ffi_free_string(created.mnemonic);
            created.mnemonic = nullptr;
        }
    }

    FfiTransactionResult result{};
    const WalletFfiError error = wallet_ffi_program_deployment(
        wallet,
        program.data(),
        program.size(),
        &result);

    std::string transactionHash;
    if (result.tx_hash != nullptr) {
        transactionHash = result.tx_hash;
    }
    const bool accepted =
        error == SUCCESS && result.success && isCanonicalHash(transactionHash);

    wallet_ffi_free_transaction_result(&result);
    wallet_ffi_destroy(wallet);

    if (!accepted) {
        std::cerr << "deployment submission failed: wallet error "
                  << static_cast<int>(error) << '\n';
        return 69;
    }

    std::cout << "{\"success\":true,\"tx_hash\":\""
              << transactionHash << "\"}\n";
    return 0;
}
