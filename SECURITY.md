# Security Policy

## Supported versions

Logos Palace has no published stable release. The unreleased default branch is
maintained on a best-effort basis for public testnet development only.

## Reporting a vulnerability

Do not open a public issue for a vulnerability.

Email the maintainer at
[`3esmit@gmail.com`](mailto:3esmit@gmail.com). Use a subject that identifies
Logos Palace as a security report, but do not put vulnerability details in a
public GitHub issue.

Include:

- affected commit or package version;
- reproduction or proof of concept;
- impact and required preconditions;
- whether credentials, keys, personal data, or network access are involved;
- suggested mitigation, if known.

Do not include live private keys, seed phrases, credentials, or personal data.

## Security model

Canonical Palace authority comes from the pinned LEZ testnet program. Delivery,
Storage, explorer responses, Basecamp provider state, and local caches are
untrusted inputs.

High-impact boundaries include:

- Basecamp/QML filesystem and module isolation;
- verified dynamic-asset path containment, size/hash checks, and decoder
  limits;
- Delivery identity/key binding, signatures, replay, expiry, bounds,
  reordering, and moderation;
- LEZ wallet lifecycle, signer/account/PDA checks, ordered actions, and
  client-side finality interpretation;
- explorer origin/schema pinning, transaction matching, finalized block
  evidence, account owners, and data digests;
- Storage operation correlation, generated destinations, CIDs, manifests,
  retention evidence, and restart reconciliation;
- deterministic VM resource limits and provisional/finalized effect
  separation;
- persisted-state framing, checksums, permissions, atomic replacement, and
  restart reconstruction.

Failure at one boundary must not silently promote data at another. In
particular:

- Delivery publication is not LEZ finality;
- a CID alone does not make bytes safe to render;
- provider status grants no Palace capability;
- cached authority is not advanced while finality evidence is missing;
- a submitted transaction is not finalized until exact explorer evidence is
  verified.

## Development E2E safety

The supported launcher, [`scripts/run-palace-e2e.sh`](scripts/run-palace-e2e.sh), is a local-development workflow. It starts a local sequencer, installs explicit LGX inputs into three temporary Basecamp profiles, and drives the compiled UI through normal user controls. It must receive canonical runtime paths and an asset manifest located outside Git. The manifest may select ignored local assets, but the runner never stages or embeds them.

The launcher uses unsigned local LGX installation only inside its temporary profiles. Do not reuse that option for a release or personal profile. Evidence under `.artifacts/` can contain peer IDs, public account IDs, screenshots, and logs; review it before sharing and remove credentials, recovery phrases, private keys, personal data, and unrelated local paths.

The local story does not submit public-testnet transactions and does not claim public finality. Public deployment and finalized-history work remains tracked by issue #2. Logos Control owns Storage and LEZ node startup; Palace does not manage system services, cgroups, or an application server.

## Current limitations

This is public testnet software. It does not provide:

- a reviewed production keystore or production program deployment;
- private rooms, private Delivery traffic, or private LEZ state;
- complete multi-device key rotation and recovery;
- production availability or participant-retention guarantees;
- a completed security audit of the product, pinned forks, or deployed guest;
- one final compiled Basecamp run covering all restart, history-rebuild,
  moderation, creator-removal, and VM-finality paths.

Finding that a documented release gate is unfinished is not itself a
vulnerability. Bypassing a stated authority, finality, isolation, signature,
path, or bounds check is security-relevant.
