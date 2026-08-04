# Contributing

Contributions are welcome while Logos Palace is developed as an experimental
Basecamp testnet MVP.

## Preserve the architecture

- Palace state, instruction, guest, and image-build source stays under
  `program/` in this repository.
- The MVP Basecamp package set remains the exact six-package set
  documented in [README.md](README.md).
- QML does not call Delivery, Storage, LEZ, wallet APIs, or arbitrary
  filesystem paths.
- `palace_core` owns network, persistence, authority, finality, and recovery
  composition.
- `palace_vm` remains deterministic and authority-free.
- Delivery publication is never treated as LEZ finality.
- Cached, malformed, or ambiguously bound external data fails closed.

Prefer a deep implementation behind a small interface. Add behavior tests at
module, generated-API, process, finality, and persistence seams.

## Development workflow

1. Fork the repository and create a focused branch.
2. Add or update a regression or contract test for observable behavior.
3. Implement the smallest coherent change.
4. Run the affected checks.
5. Inspect the diff for unrelated changes and sensitive material.
6. Open a pull request describing behavior, evidence, compatibility, and
   remaining limitations.

When a pinned platform dependency needs a fix, check its upstream repository
first. If the fix is absent, keep the fork patch focused and link its issue and
pull request when updating the pin.

Do not commit credentials, recovery phrases, private keys, node data, generated
build directories, acceptance artifacts, or local-only project material.

## Build and checks

Build the exact six portable packages:

```sh
nix build \
  .#palace-vm-lgx-portable \
  .#palace-core-lgx-portable \
  .#logos-palace-ui-lgx-portable \
  .#delivery-module-lgx-portable \
  .#storage-module-lgx-portable \
  .#lez-core-lgx-portable
```

Run C++ contract checks:

```sh
nix build \
  .#checks.x86_64-linux.palace-vm-contracts \
  .#checks.x86_64-linux.palace-core-contracts
```

Run non-live acceptance-control seams:

```sh
nix build .#checks.x86_64-linux.palace-acceptance-seams
```

Process-control changes also require the live host commands under
[Build and test](README.md#build-and-test). Those checks use the running
per-user systemd manager and cannot run inside the Nix build sandbox.

Run Palace program checks:

```sh
cargo test --manifest-path program/Cargo.toml --workspace
cargo clippy --manifest-path program/Cargo.toml --workspace --all-targets -- -D warnings
cargo test --manifest-path program/palace_program/methods/guest/Cargo.toml
cargo clippy --manifest-path program/palace_program/methods/guest/Cargo.toml --all-targets -- -D warnings
```

Build the RISC Zero guest when the toolchain is installed:

```sh
cargo build --manifest-path program/palace_program/methods/Cargo.toml --release
```

Build the complete MVP pre-alpha archive locally:

```sh
rzup install cargo-risczero 3.0.5
rzup install r0vm 3.0.5
PALACE_RELEASE_VERSION=v0.1.0-pre-alpha.1 \
  ./scripts/package-prealpha-release.sh .artifacts/prealpha-release
```

The archive includes six LGX packages, `palace.bin`, `palace-image-id`, and a
checksum manifest. Logos Control UI remains a separate operator add-on.

## Compiled acceptance

Run the narrowest relevant Basecamp harness:

```sh
./scripts/run-basecamp-gate1.sh
./scripts/run-basecamp-gate2.sh
```

The scripts fetch pinned dependencies, use temporary user directories, install
unsigned development LGXs there, and write ignored evidence under
`.artifacts/`. Production scenarios three and four are claim-bound and must
run through the full runner. Never describe a package build or one scenario as
full MVP acceptance. Pull requests must identify the source snapshot,
executed checks, and remaining runtime behavior.

The full runner has materially different effects:

```sh
./scripts/run-basecamp-mvp.sh
```

It registers public testnet identities and submits irreversible public-testnet
LEZ actions to one fixed, initially uninitialized Palace root. It holds a
canonical owner-only program/root lock, retires any exact prior v2 run scope
before release-state mutation, and records one immutable source snapshot.
After production Storage-scenario evidence exists, continue only with the exact resume
command and run directory printed by the runner. Review and sanitize reports,
logs, public account and peer IDs, timings, and screenshots before publication.

## Documentation

Keep README status, architecture, security limits, commands, and package names
consistent with the source. Check Markdown links and public-text hygiene, then
run:

```sh
git diff --check
```

## Commits and pull requests

Use focused Conventional Commits where practical:

```text
feat(core): publish verified Storage assets
fix(program): reject unauthorized state transitions
test(delivery): cover replay after restart
docs: document Basecamp acceptance limits
```

Do not include credentials, local paths, private project notes, generated
attribution, or unrelated environment details in commits or pull requests.

## Reporting bugs

Open a GitHub issue with reproduction steps, expected behavior, actual
behavior, environment, and relevant non-sensitive logs. Follow
[SECURITY.md](SECURITY.md) for vulnerabilities.
