# Changelog

All notable changes to Logos Palace are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
The project will use [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
after its first published release.

## [0.2.0-pre-alpha.1] - 2026-08-08

### Added

- Basecamp `0.2.4-alpha.1` compatibility baseline.
- Explicit Create Palace and Join Palace onboarding choices with actionable
  node, catalog, and peer-readiness states.
- Focused onboarding, room, admin, and async-status QML components.
- Supported local user-story launcher at `scripts/run-palace-e2e.sh`, with
  runtime executables, package outputs, and user-selected asset manifests
  supplied as explicit inputs.

### Changed

- Removed the application-only round-trip measurement API and production
  acceptance fixture build path.
- Renamed shipped UI/Core behavior around rooms, Storage, authority, doors,
  durable actions, and delivery-node status.
- Moved onboarding preparation, Storage-bundle tracking, Palace identity
  registration, and durable-action observation/reconciliation behind the UI
  backend controller boundary.
- Moved asset-import session ownership, ordered chunk validation, byte budgets,
  commit, retry-safe failure, and cancellation behind semantic controller
  commands; extracted room utility panels from the composition root.
- Local release story now documents Logos Control startup, admin moderation,
  Storage degradation, and restart recovery as user outcomes.

### Known limitations

- The archive is published for x86_64 Linux. Public-testnet deployment,
  finalized public history, creator-removal retention, and reconstruction
  evidence remain tracked by issue #2.

## [0.1.0-pre-alpha.7] - 2026-08-07

This release is the current x86_64 Linux local-development MVP baseline. It
does not claim public-testnet finality.

## [0.1.0-pre-alpha.1] - 2026-08-04

This pre-alpha release packages the stack-complete local MVP for x86_64-linux.
The generated archive contains the six portable LGX packages, the
repository-built Palace RISC Zero program image, the image-ID verifier, and a
checksum manifest. It is produced by the tagged GitHub Actions workflow and
does not claim public-testnet finality.

### Added

- Three Basecamp product modules: Palace UI, Palace Core, and deterministic
  Palace VM.
- Reproducible portable outputs for the exact six-package MVP set:
  `palace_vm`, `palace_core`, `logos_palace_ui`, `delivery_module`,
  `storage_module`, and `lez_core`.
- Basecamp verified-asset provider integration with opaque image handles and a
  visible degraded fallback.
- Signed Delivery envelopes, authority-backed identity/key binding, replay and
  expiry enforcement, bounded reordering, restart state, and adversarial
  acceptance coverage.
- Typed Storage catalog and exact MVP object graph, verified PNG
  publication/fetch paths, retention evidence, restart reconciliation, and
  creator-removal acceptance harness.
- Repository-owned schema-v3 Palace LEZ program with 14 instruction variants,
  public PDA records, capability grants, bans, shared state, SPEL guest, and
  RISC Zero image build.
- Fixed LEZ testnet release fingerprint covering module/runtime revisions,
  program ID, bytecode digest, Sequencer, and explorer schema.
- Exact-account explorer finality certificates, finalized history scanning,
  coordinator recovery, transactional authority projection, and secure
  finalized-account bundle persistence.
- VM provisional/finalized execution journal and one-shot promotion rules.
- Checksummed atomic room projection, durable action journal, and write-ahead
  Delivery/projection room-transition recovery.
- Basecamp Gate 1–3 harnesses for compiled-package runs with per-run JSON
  evidence.
- Resumable Gate 0–6 runner with immutable source/runtime bindings,
  claim-bound terminal process cleanup, completed-run attestation, and
  allowlist-only public evidence.
- Supervised release exclusion with a sole close-on-exec lock descriptor,
  death-coupled immutable mutators, versioned claim roll-forward, and
  contention/crash regression coverage.
- Stop-before-exec transient gate scopes with exact cgroup-v2 unit/slice
  attestation, sole-leader unit-addressed release, parent-death cgroup
  guardians, durable launch markers, interrupted-attempt history, PID reuse
  defense, and unload-before-PASS validation.
- Repository-built pidfd helper with captured child start-time identity,
  post-`pidfd_open` identity recheck, and `pidfd_send_signal` delivery;
  destructive cleanup no longer targets stored numeric PIDs or process
  groups, while exact outer cgroups own residual descendants.
- Guardian-held `cgroup.kill` and `cgroup.procs` descriptors with two-read
  sole-guardian proof, daemon-residue kill before disarm, and immediate
  failure when interruption cleanup cannot be proven.
- Standalone Gate 1 and Gate 2 lock attestation covering exact parent and
  start-time chains, executable/argv identity, sole descriptor ownership,
  contention, and the matching `/proc/locks` kernel row.
- Exact creator process identity on resume and raw application latency
  aggregates recomputed from 20 ordered samples at each supported payload
  size.
- Strict compiled/public evidence bindings for runtime NARs, process
  executable mappings, command outcomes, claim completion, metrics, and ten
  fully decoded Gate 4–6 screenshot files.
- Administrator-authored room and prop images with opaque Basecamp file
  selection, bounded chunked staging, durable moderation and draft
  assignments, approval-gated Logos Storage publication, CID-to-content
  binding, and one separately bound, fully decoded Gate 3 authoring
  screenshot.
- Qt Quick animation-frame interval evidence with an exact 120-frame window,
  source-pinned timing semantics, and a separate framebuffer capture fence.
- Local compiled MVP evidence now includes provider-offline missing-object
  degradation, pinned frame intervals, and provisional/finalized VM turn
  durations plus semantic UI/Core payload measurements without embedding asset
  data in the product.

### Changed

- Exact audited pre-public-write Gate 3 failures can retire their claims
  without discarding predecessor-local state; every other entered claim remains
  non-roll-forwardable.
- Palace program source is maintained under `program/` in this repository. It
  is no longer modeled as an external repository or build input.
- Palace state moved from the incompatible schema-v2 monolithic account to
  the schema-v3 typed public account graph.
- Durable actions require stable state observation plus exact explorer
  finality; Delivery publication cannot promote them.
- Basecamp dependency pin advanced to the verified-asset bridge candidate at
  its pinned revision, with installed-variant producer-capability recovery for
  portable packages.
- Gate 2 acceptance accounting binds every accepted Delivery envelope to
  exact persisted sender egress and receiver ingress sequences, including
  periodic presence envelopes interleaved with ordered speech.

### Security

- QML remains outside network, arbitrary-path, signing-key, wallet, and VM
  authority boundaries.
- Malformed assets, Delivery envelopes, LEZ responses, explorer evidence, and
  persisted records fail closed at their owning boundaries.
- Finalized authority replacement, incremental merge, and history rebuild are
  transactional and bind exact account IDs, PDAs, owners, digests, order, and
  checkpoints.

### Known limitations

- A public-testnet release run has not yet been demonstrated from one clean
  source snapshot; the local-development profile is the reproducible MVP
  acceptance profile.
- Public finality, creator removal from the public Storage/Delivery topology,
  and final performance/resource evidence remain release gates.
- The testnet deployment and dependency forks have not received a production
  security audit.
- Private rooms and private LEZ state are not implemented.

[0.1.0-pre-alpha.1]: https://github.com/3esmit/logos-palace/releases/tag/v0.1.0-pre-alpha.1
[Unreleased]: https://github.com/3esmit/logos-palace/commits
