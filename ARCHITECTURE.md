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
isolated acceptance user directory. Gate 2 and standalone, non-production
Gate 3 substitute an explicitly fixture-enabled `palace_core` build. The
compiled full MVP runs Gate 3 with production identities. Production Core
artifacts exclude the fixture authority, private test seeds, and acceptance
profile parsing.

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
contains two room backgrounds, a transparent prop with placement metadata, a
bounded room script, and the associated manifests.

Core generates operation IDs and destinations. Downloaded bytes must match
the expected CID, size, media type, digest, dimensions, and decoder profile
before Basecamp receives a verified handle. Publication revalidates the
handle; UI-provided paths are never accepted.

The pinned Storage API has no separate pin primitive. Retention evidence is
therefore expressed through verified local availability and successful peer
fetches, not an undocumented pin guarantee.

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
only after exact claim-bound process cleanup, then publishes sanitized
evidence only after durable run completion. No individual gate should be
presented as the final result.
