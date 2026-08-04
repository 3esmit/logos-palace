# Logos Palace MVP implementation plan

## 1. Final architecture

Logos Palace is a Basecamp package set plus one Palace-specific LEZ program.
Palace has no application-owned server. Basecamp owns process and module
lifecycle; the LEZ program owns durable shared state; Storage owns content
availability; Delivery owns low-latency presence and chat; Palace Core is the
only composition boundary between them.

```text
Basecamp UI
    |
    v
logos_palace_ui -- small semantic commands and view projections
    |
    v
palace_core -- authority, persistence, orchestration, recovery
    |        |          |          |
    v        v          v          v
palace_vm Delivery  Storage    lez_core
 (pure)   (live)    (verified) (LEZ adapter)
                                  |
                                  v
                         Palace LEZ program
```

The module boundaries are deliberately deep:

- `logos_palace_ui` exposes only user intent and immutable view data. It never
  opens a wallet, decodes an image, talks to Storage, or decides authority.
- `palace_core` validates external results, sequences durable transitions, and
  projects finalized state. It owns the small interfaces used by the UI and
  by the four infrastructure modules.
- `palace_vm` is deterministic and bounded. It has no filesystem, network,
  wallet, signing, or UI authority.
- `delivery_module` is an expiring transport. It cannot create durable
  authority or bypass moderation.
- `storage_module` publishes and retrieves typed objects. Core verifies CID,
  digest, size, media type, dimensions, and decode before an opaque Basecamp
  handle is exposed.
- `lez_core` builds exact account plans, submits through the configured LEZ
  wallet, verifies finalized account batches, and returns certificates. It
  does not own Palace policy.
- The Palace program validates callers, capabilities, account order, bounds,
  schema versions, and state transitions. All shared authority is derived
  from program-owned records.

Every cross-module call is a narrow request/result contract. A request carries
an explicit Palace, room, identity epoch, and idempotency key. A result carries
validated status plus the evidence needed by Core to persist or project it.
Unvalidated strings, host paths, transport clients, and wallet handles do not
cross the UI boundary.

## 2. User-story execution path

1. A creator starts the Storage and LEZ nodes through Basecamp/Logos Control.
2. The creator creates a Palace and receives two room records from LEZ.
3. The administrator selects backgrounds and optional props through the
   Basecamp file dialog. Core verifies, publishes, and assigns them; no asset
   is compiled into the product.
4. The creator shares the Palace address, canonical Storage catalog, and peer
   endpoint. A joiner supplies those values during onboarding.
5. Core verifies the catalog and peer fetch, registers the joiner identity,
   opens Delivery, and renders the selected room.
6. Presence, movement, speech, and props use Delivery for live projection.
   Durable changes (room lock, delegation, bans, and door transitions) use
   LEZ and become visible only after finality for the active profile.
7. On restart or provider loss, Core reloads the profile-scoped journal and
   verified assets, rebuilds authority from the exact LEZ checkpoint, and
   reconnects Delivery only after identity and catalog checks succeed.

## 3. LEZ profiles and deployment

The release profile remains pinned to the public testnet lock. The
`local-development` profile uses a real local sequencer with the same Palace
program image and schema, a separate network identifier, and explicit
`publicFinalityAvailable=false`. It is a bounded MVP execution environment,
not a fallback server and not a public-finality claim.

The local runner will accept paths to the sequencer, wallet/deployment helper,
compiled Palace program, and user-story asset manifest. It will deploy the
exact image, start the node, launch compiled Basecamp instances, and emit a
machine-readable report plus screenshots. It will not embed asset filenames or
host-specific paths in production code.

## 4. Workstreams and seams

### A. Runtime boundary

Provide one local compiled-runner entry point. Validate executable identity,
program image identity, endpoint, profile selection, and process ownership
before Basecamp starts. Keep the public-testnet runner separate so a fresh
wallet cannot trigger an unbounded history replay during local MVP work.

### B. Authoring and Storage

Exercise the real file-dialog flow with user-supplied assets. Verify the
published catalog and peer fetch from an independent joiner, then repeat after
the creator/provider stops. Keep all test assets outside Git.

### C. Delivery and projection

Run at least three compiled clients. Validate ordered per-sender traffic,
identity/signature/scope checks, live movement/speech/prop projection, and
rejection of banned senders and props.

### D. Durable authority and VM

Create the Palace, delegate moderation, lock a room, ban a user/prop, and
promote a door transition through the LEZ certificate path. Restart clients and
prove the same authority checkpoint and VM state root are recovered.

### E. Release evidence

Each run records source revision, package identities, profile/network, program
image, checks performed, and user-visible screenshots. Public updates contain
only allowlisted results and GitHub attachments; local assets and runtime
directories never enter Git.

## 5. Validation order

1. Package and program builds from a clean checkout.
2. Pure VM, Delivery, Storage, LEZ, and Core seam tests.
3. Local compiled three-client user story with independent profiles.
4. Provider-offline restart and catalog retention.
5. Release manifest and public-testnet preflight. Public finality is reported
   only when the release profile actually reaches it.

The MVP is complete when the local user story is reproducible end to end, all
durable actions survive restart, admin controls affect every active projection,
and the release checks either pass or identify a concrete upstream issue with a
linked fork fix.

