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
`delivery_module`, `storage_module`, and `lez_core`. `palace_core` has one
production build path. Test fixtures are compiled only by the contract-test
target under `packages/palace_core/tests`.

## Module responsibilities

### `logos_palace_ui`

Owns presentation and user input. It calls only the generated `palace_core`
surface. Dynamic images are opaque verified handles resolved by Basecamp; QML
does not receive arbitrary local paths, network clients, wallet handles, or
signing keys.

The QML composition root coordinates focused product surfaces:

- `onboarding/` presents explicit Create and Join flows;
- `room/` owns the room canvas, participant projection, toolbar, speech, and
  chat controls;
- `admin/` owns capability-driven room, asset, people, prop, and diagnostics
  panels;
- `components/` contains shared readiness, status, and empty-state visuals.

`PalaceUiController` in the generated UI backend owns refresh scheduling,
bounded onboarding and asset-import workflows, durable-action and
Palace-identity observation/reconciliation, retry classification, and
cancellation on teardown. `LogosPalaceUiBackend` exposes semantic LEZ,
identity, Storage-preparation, Storage-bundle, registration, and asset-import
commands used by Create, Join, Recover, and authoring. QML presents workflow
state and submits product intent; it does not invoke Core stage sequencing,
durable-action, Storage-bundle, or registration observation loops.

The UI exposes human node and recovery labels. LEZ, Storage, and Delivery
receipts remain secondary diagnostics; they are not the user-facing workflow.

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

Application behavior is split into typed, independently tested boundaries
where state ownership is already clear: `PalaceInitialAuthoring` builds create
and initial-room instructions, `AssetAuthoringCatalog` owns staged asset
state, Storage catalog/session classes own verified content lifecycle,
`PalaceRoomTransitionCoordinator` owns crash-safe room entry, and the LEZ
authority/coordinator/replay classes own durable action and finality state.
`PalaceCoreImpl` remains the outer universal-module composition boundary that
connects those services to platform callbacks and converts typed results to
the existing string surface; it is not a second application state model.

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

Core generates operation IDs and destinations. Downloaded bytes must correlate
with the expected canonical Storage CID and match the expected size, media
type, digest, dimensions, and decoder profile before Basecamp receives a
verified handle. Publication revalidates the handle; UI-provided paths are
never accepted.

Core stages a verified PNG atomically in an owner-only, per-instance producer
root after a bounded full decode. The UI controller owns the import session,
byte budget, chunk sequence, commit, and cancellation boundary; the file
bridge supplies only the selected opaque capability and immutable chunk bytes.
Basecamp accepts only the lowercase
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
user-mediated file selection: an opaque per-view request ID is followed only by
the matching asynchronous completion capability; neither QML nor Core receives
a host path. The UI invalidates abandoned requests, releases selected
capabilities before commit, and streams the immutable selection to Core through
a bounded, ordered chunk protocol. Core derives the digest and dimensions,
stages only a fully verified PNG, and exposes an opaque content handle. Upload
is blocked until human approval; a returned canonical Storage CID is accepted
only after Core locally retrieves its bounded dataset and confirms the exact
PNG bytes, digest, decode, and dimensions match the verified handle.
Persisted assignments supply graph leaves only for administrator-authorized
assets. Restart loading rejects checksum, record, review, publication-CID,
permission, and symlink mismatches.

The creator shares the canonical catalog value produced by publication and the
running Storage peer endpoint. A joiner supplies the Palace address and both
values through onboarding; Core attaches to the Control-managed Storage node,
dials the shared peer, fetches every catalog object, and opens LEZ history only
after exact verification succeeds. This keeps room joining a normal user
story while preserving the same opaque-handle and fail-closed boundaries as
creator authoring.

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
still an integration/release blocker.

## Product validation

The supported compiled story is [`scripts/run-palace-e2e.sh`](scripts/run-palace-e2e.sh). It receives runtime and asset inputs explicitly, installs independent Basecamp profiles, drives normal UI controls, and writes one path-free user-story report plus local screenshots. The report covers operator startup, onboarding, authoring, room interaction, administration, durable door transition, provider shutdown, and restart recovery.

Unit tests cover Core and VM contracts. QML/component checks cover the composition root and focused product surfaces. Binary audit checks ensure released Core/UI artifacts contain no fixture identities, embedded assets, or test-only commands. Public-testnet finality, creator-removal retention, and reconstruction evidence remain separate work tracked by issue #2.


## Trust and failure model

- Testnet LEZ is canonical for Palace authority.
- Explorer data is accepted only through pinned schema and exact evidence.
- Delivery is authenticated transport, not finality.
- Storage CIDs identify immutable Storage manifests; bounded retrieved bytes,
  manifest commitments, and decoder checks determine use.
- Basecamp verified handles expose approved assets without exposing paths.
- Provider status grants no Palace capability.
- Missing or conflicting data produces pending, offline, degraded, or
  rejected state; cached data is never silently promoted.
- Removing the creator must not remove finalized records or independently
  retained active content.
- The local runner assumes the local Unix account and immutable Nix store are
  trusted; it is not a hostile same-UID sandbox.

## Current integration limits

- The local-development compiled story proves the door path through a
  provisional-VM → local-sequencer promotion cycle; one public LEZ-finality →
  finalized-VM release cycle remains outstanding.
- Local profile authority rebuild and Storage retention are proven after a
  provider restart; every public finalized-history cold-start path still needs
  release evidence.
- Identity registration, moderation, restart, and operator-selected asset
  recovery pass in the local three-client story. Creator removal still needs a
  compiled release run.
- Testnet keys, dependency forks, and the deployed program are not
  production-audited.
- Private rooms and private LEZ state are out of scope for this MVP.
