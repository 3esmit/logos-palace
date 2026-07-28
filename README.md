# Logos Palace

Logos Palace is an experimental, room-first social space for Logos Basecamp.
The MVP targets one public Palace with two rooms, participant-hosted assets,
signed live room traffic, deterministic shared interactions, and finalized
authority on Logos Execution Zone (LEZ).

This repository owns the Palace product, including its LEZ program and
RISC Zero/SPEL guest. LEZ, Delivery, Storage, and Basecamp remain runtime
dependencies.

## Status

The project is an unreleased testnet MVP under active development.

Implemented:

- three installable Logos modules: `logos_palace_ui`, `palace_core`, and
  `palace_vm`;
- bounded deterministic VM execution and receipt fixtures;
- Basecamp verified-asset provider boundary;
- signed Delivery envelope codec, policy, replay, expiry, bounds, and
  moderation checks;
- verified Storage download and publication paths that never accept a UI
  filesystem path;
- product-owned Palace LEZ state, instructions, guest, ELF/image build, and
  Core submission codec;
- checksummed local projection and durable action-journal persistence.

Not yet release-complete:

- fixed testnet deployment and release-bound Palace program ID;
- indexed observed/finalized LEZ recovery;
- live three-instance Delivery and redundant Storage acceptance;
- full restart reconstruction and creator-removal acceptance;
- frozen performance baselines and release manifest.

See [Architecture](ARCHITECTURE.md) for boundaries and
[CHANGELOG](CHANGELOG.md) for user-visible progress.

## Architecture

```text
logos_palace_ui
        |
        v
   palace_core
   /    |    \
  v     v     v
VM  Delivery Storage
        |
        v
 LEZ runtime <--- product-owned Palace guest in program/
```

`palace_core` owns network, persistence, authority, asset, and recovery
composition. QML does not call Delivery, Storage, LEZ, or arbitrary filesystem
paths directly. `palace_vm` is deterministic and has no network or filesystem
authority. The Palace LEZ program source lives under [`program/`](program/).

## Prerequisites

- Linux `x86_64`;
- Nix with flakes enabled;
- Rust and the RISC Zero toolchain only when building the LEZ guest directly.

Dependency revisions are frozen in [`flake.lock`](flake.lock). Maintained forks
under `github.com/3esmit` are preferred where available.

## Build

From the repository root:

```sh
nix build \
  .#palace-vm-lgx-portable \
  .#palace-core-lgx-portable \
  .#logos-palace-ui-lgx-portable
```

Build and run C++ contract checks:

```sh
nix build \
  .#checks.x86_64-linux.palace-vm-contracts \
  .#checks.x86_64-linux.palace-core-contracts
```

Build and test the Palace LEZ program:

```sh
cargo test --manifest-path program/Cargo.toml --workspace
cargo clippy --manifest-path program/Cargo.toml --workspace --all-targets -- -D warnings
cargo test --manifest-path program/palace_program/methods/guest/Cargo.toml
cargo clippy --manifest-path program/palace_program/methods/guest/Cargo.toml --all-targets -- -D warnings
cargo build --manifest-path program/palace_program/methods/Cargo.toml --release
```

The resulting `.lgx` packages are Nix build outputs. Clean Basecamp
installation and multi-instance runtime acceptance remain release gates; a
successful package build alone is not an MVP acceptance result.

Run the compiled Gate 1 acceptance test:

```sh
./scripts/run-basecamp-gate1.sh
```

The script builds the exact locked Basecamp, package manager, runtime modules,
and Palace LGXs; installs all six packages into a clean user directory; renders
both verified room backgrounds; exercises the door; restarts Basecamp; and
verifies the checksummed restored projection. Its ignored evidence bundle is
written to `.artifacts/basecamp-gate1/`.

## Security and limitations

This is testnet software. Key custody, network availability, LEZ finality,
participant retention, and Basecamp module isolation are not claimed to be
production-ready. Cached data never becomes canonical authority merely because
the network is unavailable.

Report vulnerabilities according to [SECURITY.md](SECURITY.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT. See [LICENSE](LICENSE).
