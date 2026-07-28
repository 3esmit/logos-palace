# Architecture

## Product boundary

Logos Palace is a Basecamp module set, not an application-owned server. The
public MVP composes shared Logos infrastructure:

- LEZ stores ordered Palace authority and shared state;
- Delivery carries expiring live room traffic;
- Storage carries content-addressed manifests, scripts, and media;
- Basecamp hosts isolated installable modules;
- Palace Core composes those systems into one client projection.

Palace-specific state, instructions, schemas, guest code, and image build are
owned by this repository under `program/`. External LEZ repositories supply
runtime and tooling only.

## Modules

### `logos_palace_ui`

Owns presentation and user input. It can call only the small `palace_core`
surface. Dynamic images are opaque verified handles resolved by Basecamp; QML
does not receive local paths or network APIs.

### `palace_core`

Owns authority, local projection, persistence, Delivery, Storage, LEZ
submission/recovery, and VM composition. It validates every external result
before changing user-visible state.

Important boundaries:

- Storage downloads use Core-generated operation IDs and destinations;
- Storage publication accepts only a revalidated verified-asset handle;
- Delivery messages advance replay state only after schema, topic, time,
  authority, signature, payload, and bounds checks;
- Delivery publication never implies LEZ finality;
- LEZ submission advances the journal only after a canonical successful
  transaction hash;
- corrupt persisted projections or journals fail closed and degrade sync
  health.

### `palace_vm`

Owns deterministic bounded script execution. It has no network, wallet,
filesystem, or UI authority. Shared effects leave the VM as typed effects and
become authoritative only through Palace Core and the Palace LEZ program.

### Palace LEZ program

`program/palace_core` owns versioned state and deterministic transition rules.
`program/palace_program` owns the LEZ instruction envelope.
`program/palace_program/methods/guest` owns the RISC Zero/SPEL guest.

The guest uses one canonical `palace-state` PDA. Initialization requires an
authenticated owner; later transitions require the canonical owned state and
an authenticated caller. Business rules remain in the product repository.

## Durable action lifecycle

```text
local_draft
    -> queued
    -> submitted_to_lez
    -> observed
    -> finalized

queued | submitted_to_lez -> rejected
queued | submitted_to_lez | observed -> expired
submitted_to_lez | observed -> orphaned
```

Delivery publication is an independent observation. It cannot advance a
durable action to observed or finalized.

## Local state

Each Basecamp `--user-dir` receives an independent Core persistence root.
Checksummed records currently preserve room projection and action lifecycle.
The release bar additionally requires finalized LEZ checkpoints, authority
projection, Delivery subscriptions/sequences/outbox, Storage metadata, VM
receipts, and schema migration state to reconstruct after restart.

## Trust and failure model

- Testnet LEZ is canonical for Palace authority, not local cache or Delivery.
- Delivery is authenticated low-latency transport, not durable finality.
- Storage CIDs identify bytes; Palace manifests and local decoder checks decide
  whether bytes are renderable.
- Provider status grants no Palace authority.
- Missing network data produces pending, offline, degraded, or explicit
  failure state. It does not silently promote cached state.
- Removing the creator must not remove finalized records or independently
  retained active content.

## Release evidence

Completion requires compiled clean-install Basecamp acceptance with three
isolated instances, redundant Storage, live Delivery, deployed Palace LEZ
program, restart reconstruction, creator removal, hostile inputs, and recorded
latency/resource measurements. Unit tests support those seams but do not
replace runtime evidence.

`scripts/run-basecamp-gate1.sh` covers only the first compiled slice: exact
package installation, module loading, verified room images, door navigation,
and projection restart. Later gate reports must extend this evidence rather
than treating Gate 1 as full MVP acceptance.
