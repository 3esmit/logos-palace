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

Logos Palace is an x86_64-linux MVP pre-alpha. Current source includes:

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
  three-instance Delivery, and three-instance Storage;
- release orchestration with one immutable source snapshot, supervised
  program/root exclusion, transient cgroup-v2 scopes, resumable active-run
  claims, exact evidence reopening, and allowlist-only publication.

The compiled local-development MVP user story now passes with three independent
Basecamp clients: operator-selected backgrounds are published and fetched,
ordered Delivery traffic converges, the door promotes a room transition,
moderation rejects banned traffic, a missing Storage source fails closed with an
explicit degraded result, and clients recover after the provider stops.
Its report records Delivery/Storage latency, a pinned 120-frame Qt interval
window, semantic UI↔Core payload sizes/latency, and provisional/finalized VM
turn durations; transport wire bytes and per-turn VM peak memory remain
explicitly unmeasured.
The local profile deliberately makes no public-finality claim. A clean
public-testnet release run still needs public LEZ finality, creator removal, and
final performance/resource evidence.

### MVP pre-alpha release

Tag `v0.1.0-pre-alpha.1` is produced by
[the release workflow](.github/workflows/release.yml). The archive is built
from the tagged source and contains:

- all six portable LGX packages listed below;
- the repository-built `bin/palace.bin` RISC Zero program image;
- `bin/palace-image-id` for independently checking that image;
- `release.json` with byte lengths, SHA-256 digests, and the RISC Zero image ID;
- README, license, and security documentation.

Build the same archive locally with the pinned x86_64-linux toolchain:

```sh
PALACE_RELEASE_VERSION=v0.1.0-pre-alpha.1 \
  ./scripts/package-prealpha-release.sh .artifacts/prealpha-release
```

The separate Logos Control UI remains an operator add-on and is not part of
the six-package Palace product archive. The pre-alpha profile is local-
development compatible and does not claim public-testnet finality.

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

`palace_core` has one production build path. The Storage scenario always installs that
package and uses production identities.

The local compiled MVP runner may additionally install the separately supplied
Logos Control UI package so an operator can start and stop Storage through the
same Basecamp session. That utility package is not part of the Palace six-
package product set.

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
- a running per-user systemd manager;
- a unified cgroup-v2 hierarchy with writable `cgroup.kill` for the user
  manager scopes;
- Linux `pidfd_open` and `pidfd_send_signal` support;
- Rust and the RISC Zero toolchain only for direct Palace program builds.

`systemd` is used only by the official acceptance runner to create and retire
one transient Linux process scope per end-to-end scenario. It is not linked into the Palace
program or LGX modules, does not define any Palace protocol or schema, and is
not required for ordinary Basecamp execution.

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
  .#checks.x86_64-linux.palace-core-contracts
```

Run the non-live release-lock, process-scope, claim, process-ownership,
screenshot, and public-evidence seam suite:

```sh
nix build .#checks.x86_64-linux.palace-acceptance-seams
```

Process-control changes also require the live host tests. They use the running
per-user systemd manager and therefore do not run inside the Nix build sandbox:

```sh
palace_acceptance_tools="$(
  nix build --no-link --print-out-paths .#acceptance-tools
)"
PALACE_SCOPE_LIVE_TEST=1 \
PALACE_SYSTEMD_RUN="$palace_acceptance_tools/bin/systemd-run" \
PALACE_SYSTEMCTL="$palace_acceptance_tools/bin/systemctl" \
PALACE_SETSID="$palace_acceptance_tools/bin/setsid" \
PALACE_BASH="$palace_acceptance_tools/bin/bash" \
PALACE_NODE="$palace_acceptance_tools/bin/node" \
PALACE_PIDFD_SIGNAL="$palace_acceptance_tools/bin/palace-pidfd-signal" \
"$palace_acceptance_tools/bin/node" --test \
  tests/basecamp_direct_child.test.mjs \
  tests/basecamp_scope.integration.test.mjs

palace_product_snapshot="$(
  nix flake archive --json . |
    "$palace_acceptance_tools/bin/jq" -r '.path'
)"
PALACE_STANDALONE_LIVE_TEST=1 \
PALACE_PRODUCT_SNAPSHOT="$palace_product_snapshot" \
PALACE_ACCEPTANCE_TOOLS="$palace_acceptance_tools" \
"$palace_acceptance_tools/bin/node" --test \
  tests/basecamp_standalone_scope.integration.test.mjs
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
```

| Harness | Scope |
| --- | --- |
| Scenario 1 | exact-six install, module loading, verified room images, local room transition, and projection restart |
| Scenario 2 | three Palace instances, signed Delivery traffic, adversarial input, ordering/replay behavior, and restart |
| Scenario 3 | typed Storage publication, peer fetch, local verification, creator shutdown, retained fetch, and cold-peer recovery |
| Scenario 4 | public LEZ finality, restart/cold rebuild, moderation, creator removal, and exact screenshot/resource evidence |

The first two scenarios have standalone scoped launchers. Production scenarios
three and four are claim-bound and run only through the full runner. Read each
generated JSON report before citing a result, and state the source snapshot
and remaining unverified behavior.

### Local compiled MVP user story

Use the local runner for the stack-complete MVP loop without replaying public
testnet history. It starts a real local sequencer, deploys the supplied Palace
program, launches three compiled Basecamp clients, and writes a path-free
`local-mvp-report.json` plus screenshots under the chosen artifacts directory.
Every runtime dependency and the asset manifest are explicit inputs:

```sh
PALACE_LEZ_PROFILE=local-development \
PALACE_LOCAL_MVP_SEQUENCER=/path/to/sequencer_service \
PALACE_LOCAL_MVP_SEQUENCER_CONFIG=/path/to/sequencer_config.json \
PALACE_LOCAL_MVP_BASECAMP=/path/to/LogosBasecamp \
LOGOS_QT_MCP=/path/to/logos-qt-mcp \
PALACE_LOCAL_MVP_LGPM=/path/to/lgpm \
PALACE_LOCAL_MVP_DEPLOY_TOOL=/path/to/deploy_program_ffi \
PALACE_LOCAL_MVP_PROGRAM=/path/to/palace.bin \
PALACE_LOCAL_MVP_LEZ_LGX=/path/to/lez_core.lgx \
PALACE_LOCAL_MVP_STORAGE_LGX=/path/to/storage.lgx \
PALACE_LOCAL_MVP_DELIVERY_LGX=/path/to/delivery.lgx \
PALACE_LOCAL_MVP_VM_LGX=/path/to/palace_vm.lgx \
PALACE_LOCAL_MVP_CONTROL_LGX=/path/to/logos_control_ui.lgx \
PALACE_LOCAL_MVP_CORE_LGX=/path/to/palace_core.lgx \
PALACE_LOCAL_MVP_UI_LGX=/path/to/logos_palace_ui.lgx \
PALACE_E2E_ASSET_INPUT_ROOT=/path/to/asset-inputs \
PALACE_E2E_ASSET_MANIFEST=/path/to/asset-inputs/manifest-v1.json \
./scripts/run-basecamp-local-mvp.sh .artifacts/local-mvp
```

The asset manifest selects backgrounds and optional props; no product asset
filename is compiled into the runner.

Before creating a scope, each standalone launcher attests its exact lock
chain. It reopens the supervisor and runner parent relationship, start times,
executables, complete argument vectors, lock-file identity, descriptor
ownership, contention, and the matching kernel `FLOCK` row in `/proc/locks`.
The supervisor must own the sole lock descriptor; the runner and attestor must
own none. A second read must prove that the chain did not change during
attestation.

### Verified assets

“Verified” means a PNG crossed both trust boundaries with its byte identity
and technical constraints:

1. Palace Core either binds Storage-fetched bytes to typed catalog metadata or
   derives metadata from an administrator-selected room or prop image streamed
   through its bounded authoring protocol. Basecamp returns an opaque per-view
   selection request ID, then delivers a selection capability only through the
   matching asynchronous completion; neither exposes a host path. Core checks
   media type,
   encoded size,
   dimensions, SHA-256 digest, the 10 MiB encoded limit, 4096-pixel dimension
   limits, 16 MiPixels (16,777,216 pixels), PNG structure, and a full bounded
   decode before atomically staging the bytes inside that Core instance.
2. Basecamp accepted only
   `image://basecamp-verified/<lowercase-sha256>` from a declared direct Core
   dependency, proved canonical selected-profile producer-root containment,
   and rechecked the digest, PNG format, bounded decode, dimensions, and pixel
   budget.

Image loading receives only the opaque digest handle. The authoring surface
also receives bounded display, review, publication, and draft-assignment
metadata, but no host path, Storage client, or decoder authority.
When a graph contains an administrator-authored prop, the avatar renderer
consumes only its graph-derived metadata and verified handle; it contains no
compiled prop artwork.
Verification does not mean moderation approval, permanent availability, or a
provider-retention guarantee.

### Joining an existing Palace

The Palace creator can select and copy the bounded room catalog shown after
room setup publication, along with the running Storage peer endpoint. A
joiner enters the `palace://` address and pastes both values into the
onboarding form. Palace attaches to the Storage node started in Logos Control,
dials the shared peer, fetches and verifies every catalog object, then opens
the LEZ history. Missing, malformed, or degraded peer/catalog data keeps the
room closed and reports the failure; it never substitutes compiled artwork or
host paths.

### Full public-testnet runner

`scripts/run-basecamp-mvp.sh` compiles its internal stages 0–6 into one resumable report. It
creates and registers three public testnet identities, initializes the fixed
Palace root, and submits the documented LEZ actions. Those public testnet
effects cannot be undone by deleting local artifacts.

Run it only when the fixed Palace root is still uninitialized and no other run
owns that program/root pair. The runner:

- stores the canonical `0600` program/root lock inside the fixed,
  owner-only `/var/tmp/logos-palace-<uid>` claim directory;
- keeps that lock in an exact external `flock --close` supervisor so no scenario
  or application process inherits the descriptor;
- kills the immutable runner and release mutators if the supervisor dies;
- after lock attestation and before release mutation, validates the prior
  active claim and retires its exact v2 scope; a legacy claim passes only when
  it has no bound process;
- archives one immutable Nix source snapshot and executes every scenario from it;
- launches every runtime scenario behind a stop-before-exec barrier in a unique
  transient scope, releases only the exact sole stopped unit leader, and keeps
  a parent-death guardian holding open descriptors for that scope's
  `cgroup.kill` and `cgroup.procs`; after the scenario child exits, the guardian
  reads membership twice and kills the complete scope if daemonized residue
  remains before it can disarm; PASS requires the exact unit and run slice to
  be empty and unloaded;
- identifies each harness-owned direct child by PID plus `/proc` start time;
  the repository-built helper opens a pidfd, rechecks that start time, and
  uses `pidfd_send_signal` for required `SIGTERM` or `SIGKILL` delivery;
  cleanup never sends a destructive signal to a stored numeric PID or process
  group, and any remaining residue belongs to exact outer-scope cleanup;
- archives interrupted scope attempts before a retry and rejects any pending
  launch marker or nonzero/unknown command outcome during PASS reopening;
- publishes sanitized evidence only after the compiled report and durable run
  completion are reopened and verified;
- stores persistent user state and reports in one owner-only run directory;
- refuses a new production run after Storage-scenario evidence exists, except for
  allowlisted audited pre-public-write failures with exact source and report
  digests;
- resumes only when given that exact run directory and source snapshot.

An interruption first requests exact unit/slice cleanup. If that cleanup
cannot be proven, the handler exits immediately with failure: it does not wait
on a stored background PID, continue report processing, or enter an unbounded
child wait.

```sh
./scripts/run-basecamp-mvp.sh
```

If interrupted, use the exact resume command printed by the runner. Do not
copy state into a new run or rerun the Storage scenario separately. Review and sanitize the
generated JSON, logs, account IDs, peer IDs, timing data, ten exact stage 4–6
screenshots, and the separate Storage moderation screenshot. All eleven are
fully decoded 1600×900 PNG files. Until the compiled report says
`fullMvp: "passed"` with no pending stages, this command has not established
MVP completion.

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
