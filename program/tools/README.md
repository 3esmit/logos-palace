# Palace program deployment tool

`deploy_program_ffi.cpp` submits one bounded RISC Zero program image through
the LEZ wallet FFI and prints only the canonical transaction hash. It does not
wait on the wallet's transaction lookup route and does not treat submission as
finality.

The tool creates a disposable wallet because a program-deployment transaction
does not require an account or balance. It frees the generated recovery phrase
without printing it. Use it only with a Testnet-only temporary directory.

## Build

Build the tested Palace image:

```sh
cargo build \
  --manifest-path program/palace_program/methods/Cargo.toml \
  --release
```

Point `lez_release_root` at a verified LEZ native release containing matching
`include/wallet_ffi.h` and `lib/libwallet_ffi.so`, then compile:

```sh
c++ -std=c++17 -O2 \
  -Wall -Wextra -Wpedantic -Werror \
  program/tools/deploy_program_ffi.cpp \
  -isystem "$lez_release_root/include" \
  -L"$lez_release_root/lib" \
  -Wl,-rpath,"$lez_release_root/lib" \
  -lwallet_ffi \
  -o deploy_program_ffi
```

## Submit

Create a private temporary directory. Pass the password through the dedicated
environment variable, never a command-line argument:

```sh
palace_deploy_dir="$(mktemp -d /tmp/logos-palace-lez-deploy.XXXXXX)"
chmod 700 "$palace_deploy_dir"
palace_deploy_password="$(openssl rand -hex 32)"

PALACE_LEZ_WALLET_PASSWORD="$palace_deploy_password" \
  ./deploy_program_ffi \
  program/testnet-v0.2-wallet.json \
  "$palace_deploy_dir/wallet.json" \
  program/palace_program/methods/target/riscv-guest/palace-program-methods/palace-program-guest/riscv32im-risc0-zkvm-elf/release/palace.bin

unset palace_deploy_password
```

The tool rejects an existing wallet/statistics path, a malformed image, and an
image that cannot fit Testnet's 614,200-byte transaction limit.

After recording the hash, remove only the exact temporary directory created
above. A successful response proves Sequencer acceptance, not inclusion or
finality. Release evidence must find that exact hash and exact bytecode in an
explorer block whose `bedrock_status` is `Finalized`.
