# Architecture

## Product boundary

Logos Palace is a Basecamp module set plus a Palace-specific LEZ program. It
does not operate an application-owned server.

All Palace business logic is source-owned here:

- `packages/palace_vm` implements deterministic room scripts;
- `packages/palace_core` composes authority, persistence, Delivery, Storage,
  LEZ, finality, and recovery;
- `packages/logos_palace_ui` implements the Basecamp UI;
- `program/palace_core` implements schema-v3 state and transitions;
- `program/palace_program` implements the LEZ instruction envelope;
- `program/palace_program/methods/guest` implements the RISC Zero/SPEL guest.

There is no external Palace-program repository or source input. External
repositories provide the platform runtimes and build tools.

## Runtime package graph

The MVP Basecamp package set contains exactly six LGX packages:

```text
logos_palace_ui
        |
        v
   palace_core
   /    |       |       \
  v     v       v        v
 VM  Delivery  Storage  LEZ Core
```

The concrete package names are `logos_palace_ui`, `palace_core`, `palace_vm`,
`delivery_module`, `storage_module`, and `lez_core`. The
`palace_delivery_acceptance` package is test-only and is installed only in its
isolated acceptance user directory. Gate 2 and the internal, non-production
Gate 3 fixture mode substitute an explicitly fixture-enabled `palace_core`
build. The compiled full MVP runs Gate 3 with production identities.
Production Core artifacts exclude the fixture authority, private test seeds,
and acceptance profile parsing.

## Module responsibilities

### `logos_palace_ui`

Owns presentation and user input. It calls only the generated `palace_core`
surface. Dynamic images are opaque verified handles resolved by Basecamp; QML
does not receive arbitrary local paths, network clients, wallet handles, or
signing keys.

### `palace_core`

Owns the trust-bearing composition:

- Delivery identity, signed envelopes, session recovery, and live projection;
- Storage operation correlation, typed catalogs, verified assets, and
  retention evidence;
- LEZ wallet lifecycle, schema-v3 instruction plans, stable account reads,
  explorer finality, and history scanning;
- finalized authority projection and checksummed local stores;
- write-ahead room transitions that coordinate Delivery session state,
  projection persistence, and native-call quiescence;
- VM invocation and durable action journaling.

External results are validated at their owning boundary before changing
user-visible or persisted state.

### `palace_vm`

Owns deterministic, bounded script execution. It has no network, wallet,
filesystem, or UI authority. Its finality API separates provisional effects
from finalized promotion. Palace Core must not apply durable shared effects or
navigation before the matching LEZ action is finalized.

### Palace LEZ program

The schema-v3 program uses a public account graph:

- one canonical `PalaceRoot` PDA derived from `["palace-root"]`;
- user-profile PDAs;
- exactly two room PDAs;
- capability-grant PDAs;
- ban PDAs;
- room-shared-state PDAs.

Every record has a type discriminator and schema version. Every transaction
starts with the root account and authenticated caller, followed by a
variant-specific exact account order. See [`program/README.md`](program/README.md)
for the schema, bounds, capabilities, and PDA seeds.

The Palace program is compiled and deployed separately from the six LGX
packages, but its source and build remain part of this repository.

## Durable LEZ action lifecycle

```text
intent
  -> exact transaction plan
  -> wallet submission
  -> height-stable account batch
  -> explorer transaction/account verification
  -> immutable finality certificate
  -> coordinator finalized
  -> finalized authority bundle
  -> durable journal finalized
```

Core binds the transaction hash, program, instruction words, ordered account
IDs, account owners, account data digests, finalized block ID/hash, and root
ordered-action cursor. Program-owned account responses merge by exact account
ID and are projected only as one complete state. The system-owned signer is
strictly parsed and then excluded from the authority bundle.

`Initialize(0)` may seed an empty, explicitly scoped bundle only when the
certificate covers every genesis account. Later actions require a complete
prior bundle and the exact prior checkpoint. Rejected merge or rebuild
operations leave both the prior bundle and live authority projection
unchanged.

Delivery publication is independent evidence. It cannot advance a durable
action to observed or finalized.

## Live Delivery path

Delivery carries expiring presence, chat, movement, and prop traffic. Core
binds every envelope to the Palace/room topic, finalized user identity and key
epoch, sequence number, timestamp window, payload bounds, and Ed25519
signature. Replay state advances only after full validation.

Delivery remains low-latency transport, not canonical authority or durable
history.

## Storage path

The MVP catalog is a bounded typed graph rooted at a Palace manifest. It
contains the assigned room backgrounds, a bounded room script, and the
associated manifests. An optional prop image and placement metadata appear
only after an administrator supplies and assigns that prop through the
authoring flow.

Core generates operation IDs and destinations. Downloaded bytes must match
the expected CID, size, media type, digest, dimensions, and decoder profile
before Basecamp receives a verified handle. Publication revalidates the
handle; UI-provided paths are never accepted.

Core stages a verified PNG atomically in an owner-only, per-instance producer
root after a bounded full decode. Basecamp accepts only the lowercase
SHA-256-addressed `image://basecamp-verified/...` handle from a declared direct
Core dependency. It proves canonical producer-root containment and rechecks
the encoded digest, PNG signature/structure, decode, 4096-pixel dimension
limits, and 16 MiPixels (16,777,216 pixels). Image rendering receives only the
verified handle. The authoring surface receives bounded display, review,
publication, and room-assignment metadata, but has no Storage-client or decoder
authority. This proves byte identity, safe decode, and Core-producer
provenance; it does not assert moderation approval or future availability.

The pinned Storage API has no separate pin primitive. Retention evidence is
therefore expressed through verified local availability and successful peer
fetches, not an undocumented pin guarantee.

Room images and any explicitly authorized prop images are
administrator-authored inputs, not compiled resources. Basecamp owns the
user-mediated file selection and exposes only an opaque, per-view capability;
neither QML nor Core receives a host path. The UI streams the immutable
selection to Core through a bounded, ordered chunk protocol. Core derives the
digest and dimensions, stages only a fully verified PNG, and exposes an opaque
content handle. Upload is blocked until human approval; a returned Storage CID
is accepted only when its SHA-256 multihash equals the verified handle.
Persisted assignments supply graph leaves only for administrator-authorized
assets. Restart loading rejects checksum, record, review, publication-CID,
permission, and symlink mismatches.

When a verified graph contains an administrator-authored prop, the avatar
renderer receives its identifier, opaque handle, dimensions, anchor, and layer
only from that graph. QML does not contain a prop image or draw a substitute
asset; absent or unrecovered props are not rendered.

## Local state and restart

Each Basecamp `--user-dir` has an independent Core persistence root. Durable
records use bounded framing, checksums, atomic replacement, restrictive
permissions, and symlink/path checks where applicable.

Current stores cover the room projection, action journal, Delivery identity
and session state, room-transition intent, LEZ coordinator, and finalized
authority account bundle. Room entry persists intent, Delivery session,
projection, and intent removal in order; startup keeps participant writes
blocked until both journal observations agree and recovery completes.
Finalized bundles restore transactionally. A strict finalized-history scanner
and pure rebuild seam exist, but automatic missing-bundle reconstruction is
still an integration/release gate.

## Release orchestration and process ownership

The full runner has one deep release-control boundary:

```text
owner-only program/root lock
  -> exact flock supervisor (sole lock descriptor)
  -> pdeath-kill-coupled immutable runner
  -> prior-claim scope retirement
  -> versioned active-run claim
  -> planned transient scope + stop-before-exec barrier
  -> attested gate command
  -> exact unit/slice cleanup and unload
  -> strict report reopening
  -> completed claim
  -> allowlist-only public evidence
```

The mutable launcher archives the clean Git source into one Nix store snapshot
and hands off through pinned `flock`, `setpriv`, and Bash executables. The
canonical `0600` lock and active claim live in the fixed, mode-`0700`
`/var/tmp/logos-palace-<uid>` directory. The supervisor alone owns the lock
descriptor. The immutable runner, release mutators, gates, workers, and
Basecamp processes reject an inherited lock descriptor. Parent PID/start-time,
executable, exact argv, file identity, kernel lock row, descriptor ownership,
and contention are reopened before claim mutation.

Standalone Gate 1 and Gate 2 launchers apply the same exclusion invariant to
their artifact directory. Before scope creation, the attestor reopens the
exact supervisor-to-runner parent chain, both start times, effective UIDs,
executables, complete argument vectors, lock device/inode, and file mode. It
requires exactly one matching descriptor in the `flock` supervisor, none in
the runner or attestor, one exact `FLOCK` row in `/proc/locks`, and an
independent contention failure. A second bounded read rejects PID reuse,
reparenting, argv replacement, descriptor movement, or lock replacement
during attestation.

Immediately after lock attestation and before any release-state mutation, the
immutable runner securely reopens the prior active claim. A v2 claim causes
its exact run slice to be killed if populated, emptied, stopped, and unloaded.
A legacy claim passes only with no claim-bound process. Malformed or
cross-owner state fails closed.

Each Gate 1–4 attempt is planned durably before `systemd-run`. A shell barrier
stops the scope before the gate command can execute; the runner then proves
the exact unit is a direct child of the per-run slice and contains only the
stopped leader. Unit-addressed release reopens the PID start time, cgroup, and
sole-member proof before signaling the unit. The leader then becomes a
parent-death guardian holding close-on-exec descriptors for the exact
`cgroup.kill` and `cgroup.procs` files; loss of the lock-coupled runner kills
the complete gate scope. After its direct gate child exits, the guardian reads
membership twice. Any member other than the guardian causes an exact cgroup
kill and a failing guardian exit before signal-handler removal or descriptor
close, so a daemon cannot survive guardian disarm. A launch marker is removed
only after matching attestation.

Harness-owned child identity is captured as PID plus `/proc/<pid>/stat` start
time. When Gate 1–3 must terminate a direct child, a helper compiled from this
repository calls `pidfd_open`, reopens and compares that start time, then calls
`pidfd_send_signal`. Destructive cleanup never targets a remembered numeric
PID or process group. A child that does not exit within the bounded wait makes
the gate fail; the enclosing exact cgroup remains responsible for descendant
or daemon residue. Gate 4 requests graceful worker shutdown and likewise
rejects residue instead of signaling a stored numeric identity.

On interruption, the runner kills and unloads the exact slice, records a
nonzero or unknown outcome, archives the attempt, and creates a new unit for
the retry. If exact scope cleanup itself fails, the signal or transition path
exits immediately without waiting on a numeric background PID or continuing
report work. A passing report is reusable only when no launch marker remains
and its scope evidence proves status zero, no residue, and an unloaded slice.

These `systemd` and cgroup controls belong to the Linux acceptance and release
evidence harness. They do not participate in Palace state transitions,
contracts, module APIs, persisted schemas, or normal Basecamp runtime
composition.

The active claim binds source commit, snapshot NAR, runner digest, runtime
manifest, run directory, GC root, and process-scope identity. Safe pre-Gate-3
roll-forward retires the predecessor slice and brackets replacement with two
claim-bound process scans. Gate-3-entered claims cannot roll forward, except
for one audited pre-public-write fingerprint rejection whose immutable source,
reports, and retirement certificate all match exact digests. That recovery
retains predecessor-local state and still rejects every other entered claim.
Completed claims bind the compiled report digest and must reopen all evidence
before public projection.

## Trust and failure model

- Testnet LEZ is canonical for Palace authority.
- Explorer data is accepted only through pinned schema and exact evidence.
- Delivery is authenticated transport, not finality.
- Storage CIDs identify bytes; manifests and decoder checks determine use.
- Basecamp verified handles expose approved assets without exposing paths.
- Provider status grants no Palace capability.
- Missing or conflicting data produces pending, offline, degraded, or
  rejected state; cached data is never silently promoted.
- Removing the creator must not remove finalized records or independently
  retained active content.
- The release controller assumes the local Unix account and immutable Nix
  store are trusted. It defends against crashes, stale PIDs, PID reuse,
  escaped descendants, symlink/path replacement, malformed evidence, and
  concurrent launch attempts; it is not a hostile same-UID sandbox.

## Current integration limits

- The Basecamp door path is not yet proven through one complete
  provisional-VM → LEZ-finality → finalized-VM promotion cycle.
- Automatic finalized-history rebuild is not yet wired into every cold-start
  path.
- Full identity registration, moderation, restart, and creator-removal
  behavior still needs one compiled multi-instance release run.
- Testnet keys, dependency forks, and the deployed program are not
  production-audited.
- Private rooms and private LEZ state are out of scope for this MVP.

## Release evidence

Unit and contract tests verify seams; portable LGX builds verify packaging.
Gate 1–3 scripts exercise progressively broader compiled Basecamp slices.
Release completion still requires one recorded clean-source run covering all
MVP behavior, hostile inputs, restart/rebuild, creator removal, and
latency/resource measurements. The full runner accepts a runtime-gate pass
only after exact claim-bound process cleanup. It requires ten exact decoded
Gate 4–6 PNG screenshots plus one separately bound Gate 3 Moderation screenshot
and publishes sanitized evidence only after durable run completion.
Interrupted-attempt history remains local and is not projected. No individual
gate should be presented as the final result.
