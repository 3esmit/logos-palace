# Changelog

All notable changes to Logos Palace are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
The project will use [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
after its first published release.

## [Unreleased]

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
  fully decoded screenshot files.

### Changed

- Palace program source is maintained under `program/` in this repository. It
  is no longer modeled as an external repository or build input.
- Palace state moved from the incompatible schema-v2 monolithic account to
  the schema-v3 typed public account graph.
- Durable actions require stable state observation plus exact explorer
  finality; Delivery publication cannot promote them.
- Basecamp dependency pin advanced to the verified-asset bridge candidate at
  its pinned revision, with installed-variant producer-capability recovery for
  portable packages.

### Security

- QML remains outside network, arbitrary-path, signing-key, wallet, and VM
  authority boundaries.
- Malformed assets, Delivery envelopes, LEZ responses, explorer evidence, and
  persisted records fail closed at their owning boundaries.
- Finalized authority replacement, incremental merge, and history rebuild are
  transactional and bind exact account IDs, PDAs, owners, digests, order, and
  checkpoints.

### Known limitations

- No final compiled Basecamp run yet proves the entire MVP in one source
  snapshot.
- The LEZ-backed door path and automatic cold-start history rebuild remain
  integration gates.
- Creator removal, complete restart recovery, and performance/resource
  evidence remain release gates.
- The testnet deployment and dependency forks have not received a production
  security audit.
- Private rooms and private LEZ state are not implemented.

[Unreleased]: https://github.com/3esmit/logos-palace/commits
