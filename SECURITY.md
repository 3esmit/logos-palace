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

## Development acceptance safety

The Basecamp gate scripts install unsigned LGXs only into temporary,
test-specific user directories. Do not reuse their `--allow-unsigned` workflow
for a production profile. Evidence under `.artifacts/` may contain peer IDs,
public account IDs, screenshots, timing, and logs; review it before sharing.

The full MVP runner is not a disposable local fixture. It registers public
testnet identities, requires the fixed Palace root to be uninitialized, and
submits public LEZ transactions whose effects remain after local artifacts are
deleted. Its owner-only global program/root lock prevents concurrent local
runs. If interrupted after production Gate 3 begins, resume only the exact
recorded run and immutable source snapshot; starting over can conflict with
already accepted transactions.

The checked-in `program/testnet-v0.2-wallet.json` is public network
configuration, not a wallet or credential. Deployment tools require a
disposable testnet wallet directory and must never print or commit its recovery
phrase.

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
