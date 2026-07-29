# Logos Palace

Logos Palace is an experimental, room-first social space for Logos Basecamp.
The public MVP targets one Palace with two rooms, participant-hosted assets,
signed live room traffic, deterministic shared interactions, and finalized
authority on Logos Execution Zone (LEZ).

This repository owns the complete Palace product. In particular,
[`program/`](program/) contains the Palace state machine, instruction schema,
SPEL guest, and RISC Zero image build. It is not an external repository,
submodule, or generated input. Basecamp, Delivery, Storage, LEZ, SPEL, and the
module builder remain pinned platform dependencies.

## Status

Logos Palace is an unreleased testnet MVP under active development. Current
source includes:

- three product modules: `palace_vm`, `palace_core`, and `logos_palace_ui`;
- deterministic VM execution with provisional and finalized receipt seams;
- signed Delivery envelopes, replay/expiry/bounds enforcement, restart state,
  and finalized moderation checks;
- fail-closed room-transition recovery that durably coordinates Delivery
  session state with the visible room projection;
- a typed Storage catalog, verified PNG handling, publication/fetch
  correlation, local verification, and retention evidence;
- the repository-owned schema-v3 Palace program with 14 instruction variants
  and a public PDA account graph;
- a fixed LEZ testnet release fingerprint, exact-account finality
  certificates, finalized authority projection, and secure restart stores;
- Basecamp acceptance harnesses that run compiled packages for UI/restart,
  three-instance Delivery, and three-instance Storage.

The full compiled Basecamp MVP has not yet passed one final end-to-end release
run. In particular, the LEZ-backed door flow, automatic authority rebuild when
no local bundle exists, all restart paths, creator removal, and final
performance/resource evidence remain release gates. Package builds, contract
tests, and individual gate reports do not by themselves establish MVP
completion.

See [Architecture](ARCHITECTURE.md), [CHANGELOG](CHANGELOG.md),
[Security](SECURITY.md), and [Support](SUPPORT.md).

## Exact six-package Basecamp set

Each MVP acceptance user directory must contain exactly these six installable
LGX packages:

| Package | Role | Source |
| --- | --- | --- |
| `palace_vm` | bounded deterministic room-script execution | this repository |
| `palace_core` | authority, persistence, and protocol composition | this repository |
| `logos_palace_ui` | Basecamp presentation and input | this repository |
| `delivery_module` | live peer-to-peer room transport | pinned flake input |
| `storage_module` | content-addressed object transport | pinned flake input |
| `lez_core` | wallet and LEZ runtime bridge | pinned flake input |

`palace_delivery_acceptance` is a test-only adversarial fixture. It is not part
of the six-package MVP installation. Gate 2 and standalone, non-production
Gate 3 use a separately named, fixture-enabled Core build. The compiled full
MVP runs Gate 3 with production identities. The production `palace_core`
artifact excludes acceptance identities and profile hooks.

## Architecture

```text
Basecamp
   |
   v
logos_palace_ui
   |
   v
palace_core ----> palace_vm
   |  |  \
   |  |   +----> storage_module
   |  +--------> delivery_module
   +-----------> lez_core ----> LEZ testnet
   |
   +------------> finalized explorer evidence

program/** --builds--> Palace RISC Zero/SPEL guest deployed on LEZ
```

`palace_core` owns network calls, persistence, authority projection, asset
validation, and recovery composition. QML does not call Delivery, Storage,
LEZ, or arbitrary filesystem paths directly. `palace_vm` has no network,
wallet, filesystem, or UI authority.

## Prerequisites

- Linux `x86_64`;
- Nix with flakes enabled;
- network access for pinned GitHub/Nix dependencies;
- Rust and the RISC Zero toolchain only for direct Palace program builds.

Dependency revisions are frozen by [`flake.lock`](flake.lock). The current
flake pins maintained forks for Basecamp, Delivery, Storage, and the LEZ
module.

## Build and test

Run commands from the repository root.

Build the exact six portable LGX packages:

```sh
nix build \
  .#palace-vm-lgx-portable \
  .#palace-core-lgx-portable \
  .#logos-palace-ui-lgx-portable \
  .#delivery-module-lgx-portable \
  .#storage-module-lgx-portable \
  .#lez-core-lgx-portable
```

Build C++ contract checks:

```sh
nix build \
  .#checks.x86_64-linux.palace-vm-contracts \
  .#checks.x86_64-linux.palace-core-contracts \
  .#checks.x86_64-linux.palace-core-production-fixture-audit \
  .#checks.x86_64-linux.palace-core-acceptance-fixture-audit
```

Build and test the repository-owned Palace program:

```sh
cargo test --manifest-path program/Cargo.toml --workspace
cargo clippy --manifest-path program/Cargo.toml --workspace --all-targets -- -D warnings
cargo test --manifest-path program/palace_program/methods/guest/Cargo.toml
cargo clippy --manifest-path program/palace_program/methods/guest/Cargo.toml --all-targets -- -D warnings
cargo build --manifest-path program/palace_program/methods/Cargo.toml --release
```

The final command requires the RISC Zero guest toolchain. Program architecture,
bounds, and migration notes live in [`program/README.md`](program/README.md).

## Compiled Basecamp acceptance

The acceptance scripts build the locked source snapshot, install the exact
package set into temporary Basecamp user directories, and write ignored
per-run evidence under `.artifacts/`.

```sh
./scripts/run-basecamp-gate1.sh
./scripts/run-basecamp-gate2.sh
./scripts/run-basecamp-gate3.sh
```

| Harness | Scope |
| --- | --- |
| Gate 1 | exact-six install, module loading, verified room images, local room transition, and projection restart |
| Gate 2 | three Palace instances, signed Delivery traffic, adversarial input, ordering/replay behavior, and restart |
| Gate 3 | typed Storage publication, peer fetch, local verification, creator shutdown, retained fetch, and cold-peer recovery |

These are development slices. Read each generated JSON report before citing a
result, and state the source snapshot and remaining unverified gates.

### Full public-testnet runner

`scripts/run-basecamp-mvp.sh` compiles Gate 0–6 into one resumable report. It
creates and registers three public testnet identities, initializes the fixed
Palace root, and submits the documented LEZ actions. Those public testnet
effects cannot be undone by deleting local artifacts.

Run it only when the fixed Palace root is still uninitialized and no other run
owns that program/root pair. The runner:

- holds one owner-only global program/root lock;
- archives one immutable Nix source snapshot and executes every gate from it;
- treats terminal process cleanup as part of every runtime-gate pass;
- publishes sanitized evidence only after the compiled report and durable run
  completion are reopened and verified;
- stores persistent user state and reports in one owner-only run directory;
- refuses a new production run after Gate 3 evidence exists;
- resumes only when given that exact run directory and source snapshot.

```sh
./scripts/run-basecamp-mvp.sh
```

If interrupted, use the exact resume command printed by the runner. Do not
copy state into a new run or rerun Gate 3 separately. Review and sanitize the
generated JSON, logs, account IDs, peer IDs, timing data, and screenshots
before publishing them. Until the compiled report says `fullMvp: "passed"`
with no pending gates, this command has not established MVP completion.

## Security and limitations

This is public testnet software. Palace authority is public LEZ state; current
rooms do not provide confidentiality. Key custody, dependency forks, network
availability, participant retention, Basecamp isolation, and the deployed
program have not received a production security review. Cached or delivered
data never becomes canonical authority merely because a network is
unavailable.

Report vulnerabilities privately as described in [SECURITY.md](SECURITY.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) and
[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

## License

MIT. See [LICENSE](LICENSE).
