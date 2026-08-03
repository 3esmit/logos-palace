# Palace LEZ program

This workspace owns the fixed Palace program schema, transition rules, SPEL
guest, and RISC Zero image build. LEZ and SPEL remain pinned runtime/build
dependencies.

Schema v3 uses a public account graph instead of one monolithic state account:

- `PalaceRoot`: stable Palace ID, title, owner, two room IDs, entry room,
  active manifest CID, absolute record counts, revision, and the global ordered
  action cursor.
- `UserProfile`: display name, Delivery public key and epoch, avatar manifest
  CID, and profile revision.
- two `RoomRecord` accounts: room manifest CID, script bundle CID, VM profile,
  lock state, and revision.
- `CapabilityGrant`: subject, issuer, Palace/room scope, capability bits,
  expiry, delegation flag, revocation state, and revision.
- `Ban`: one user or asset-CID target, issuer, scope, active state, and
  revision.
- `RoomSharedState`: bounded spot key/value, state root, record revision, and
  last ordered action ID.

Every record starts with a `RecordType` discriminator and `schema_version`.
Structurally valid bytes for one record type cannot be accepted as another.

## PDA graph

SPEL v0.6.0 derives deterministic public PDAs from these ordered seeds:

| Record | Seeds |
| --- | --- |
| root | `["palace-root"]` |
| user profile | `["profile", root_account_id, user_account_id]` |
| room | `["room", root_account_id, room_id]` |
| grant | `["grant", root_account_id, grant_id]` |
| ban | `["ban", root_account_id, ban_id]` |
| shared state | `["shared", root_account_id, shared_state_id]` |

Stable IDs are nonzero 32-byte values. Dynamic PDA seeds therefore have fixed
size and cannot trigger string-seed length ambiguity.

Every ordered instruction keeps accounts in this prefix:

```text
0: PalaceRoot (writable, program-owned, canonical PDA)
1: authenticated caller
```

Remaining accounts stay variant-specific and ordered as documented on
`GuestInstruction`. This root-first invariant lets an indexer rebuild all
finalized Palace actions by the canonical root account.

## Authority

The root owner alone may publish the active Palace manifest, create grants, or
revoke grants. Room publication, moderation, and shared state never inherit
owner authority implicitly; callers must present a live grant with matching
subject, scope, inclusive `valid_through_action_id`, and capability.

Capabilities are fixed bits:

- `CAP_MODERATE_USER`
- `CAP_MODERATE_ASSET`
- `CAP_SET_ROOM_LOCK`
- `CAP_WRITE_SHARED_STATE`
- `CAP_ROOM_EDIT`

Initialization creates an explicit Palace-scoped owner grant. A delegated
moderator can therefore ban users/assets or lock rooms only after the owner
grants the corresponding bits. Shared spot updates always require
`CAP_WRITE_SHARED_STATE`, including updates submitted by the owner.
Room manifest/script changes likewise require `CAP_ROOM_EDIT`.

`register_user` creates the caller's profile and a non-delegable,
Atrium-scoped `CAP_WRITE_SHARED_STATE` ingress grant together. This is the
bounded capability used by the MVP door; moderation and room-edit authority
still require explicit owner grants.

## Bounds

The core enforces these absolute schema limits before mutation:

| Field/count | Maximum |
| --- | ---: |
| rooms | exactly 2 |
| users | 64 |
| grants | 64 |
| bans | 128 |
| shared-state records | 128 |
| Palace/room title | 64 bytes |
| display name | 48 bytes |
| CID | 128 ASCII alphanumeric bytes |
| shared key | 32 ASCII identifier bytes |
| shared value | 512 bytes |

All ordered actions must use exactly
`PalaceRoot.last_ordered_action_id + 1`. Replays, gaps, overflows, failed
authorization, invalid records, and bound violations leave root and target
records unchanged. Publishing a Palace manifest replaces
`active_manifest_cid`; no append-only manifest set exists.

## Schema-v2 migration

Schema v3 intentionally breaks the schema-v2 `palace-state` account and
two-variant guest wire format. Testnet deployments must:

1. deploy the schema-v3 guest image;
2. initialize the new `palace-root` graph;
3. update clients to the schema-v3 `GuestInstruction` variants and documented
   account lists;
4. rebuild projections from finalized schema-v3 root history.

Do not reinterpret schema-v2 bytes as schema-v3 records.

## Verification

Run deterministic contract and public wire checks:

```sh
cargo test --manifest-path program/Cargo.toml --workspace
cargo clippy --manifest-path program/Cargo.toml --workspace --all-targets -- -D warnings
```

Run the pinned SPEL guest validation, PDA, ownership, and RISC Zero word
fixtures:

```sh
cargo test --manifest-path program/palace_program/methods/guest/Cargo.toml
cargo clippy --manifest-path program/palace_program/methods/guest/Cargo.toml --all-targets -- -D warnings
```

Build the embedded guest image after installing the RISC Zero toolchain:

```sh
cargo build --manifest-path program/palace_program/methods/Cargo.toml --release
```

## Pinned deployed release

Production preflight trusts one reviewed public release manifest and one
Nix-built RISC Zero image-ID verifier. The manifest binds the six installed
LGX artifacts, their source revisions and SHA-256 digests, the Palace schema
and Delivery/Storage/VM profiles, the LEZ network endpoints, and the deployed
program/root identity. Deployed bytecode is fetched directly from the
pinned finalized explorer transaction and streamed to the verifier over
standard input. The deployed binary is intentionally not stored in this
repository.

| Field | Pinned value |
| --- | --- |
| repository manifest | `program/release/release.json` (schema `logos.palace.release`, version 2) |
| byte length | `297312` |
| SHA-256 | `69099492e8f860ab338846f33762f5fb563ffc40121779ec1e15ef0b35da7171` |
| RISC Zero image / program ID | `e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61` |
| deployment transaction | `98711414b02a12a9abfdd17780337f7f32c962df1dea2f50e3deeecba3c7a0b5` |
| finalized block ID | `41029` |
| finalized block hash | `0ea1852f8c91d9ba8003844d1c98c68ba1b6ddc08a7c9dbf23dec95eb0460b92` |
| Palace root account, hex | `12ff117a38d756f132cf616cea36fa007653c3c99727c1b475503caa345cbf2a` |
| Palace root account, base58 | `2H9vVPVToHwyHer6e7dAUGA7ToQmi4HSRscjounkkig9` |

Build the pinned verifier/manifest output and run its offline negative
fixtures:

```sh
nix build --no-link .#checks.x86_64-linux.palace-release-artifact
release_artifact="$(
  nix build --no-link --print-out-paths .#palace-release-artifact
)"
cat "$release_artifact/share/logos-palace/release.json"
if printf %s "not-a-risc0-executable" |
  "$release_artifact/bin/palace-image-id" -; then
  exit 1
fi
```

Production preflight requires the explorer response to match the pinned
transaction, finalized block, byte length, and SHA-256. It then computes the
RISC Zero image ID from those exact fetched bytes through
`palace-image-id -`, and verifies the root account's hex, base58, and PDA
bindings before any Palace action. No temporary bytecode file is created.

The pinned values above were established through a local-only manual
comparison of a source-built candidate with the finalized explorer bytecode.
That candidate contained build-environment paths and is deliberately not
committed. The source build command in the previous section creates a new
candidate; it does not replace or authorize the deployed release.

Before the next deployment, make the build path-independent by supplying
`--remap-path-prefix` mappings for every absolute source, Cargo-home, and
toolchain root, then scan the candidate for absolute paths. Refresh
`program/release/release.json` only through an explicit reviewed release after
matching the clean candidate against its finalized deployment transaction.
Runtime code must never trust or copy an arbitrary Cargo `target` artifact.
