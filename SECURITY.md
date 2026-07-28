# Security Policy

## Supported versions

Logos Palace has no published stable release. The unreleased default branch is
maintained on a best-effort basis for testnet development only.

## Reporting a vulnerability

Do not open a public issue for a vulnerability.

Use GitHub's private vulnerability reporting for this repository:

<https://github.com/3esmit/logos-palace/security/advisories/new>

Include:

- affected commit or package version;
- reproduction or proof of concept;
- impact and required preconditions;
- whether credentials, keys, or network access are involved;
- any suggested mitigation.

Do not include live private keys, seed phrases, personal data, or credentials.

## Security boundaries

High-impact areas include:

- Basecamp/QML filesystem and module isolation;
- verified dynamic-asset path containment and decoder limits;
- Delivery key binding, signatures, replay, expiry, bounds, and moderation;
- LEZ signer/account/PDA checks and client-side finality interpretation;
- Storage operation correlation, path containment, size limits, hashes, and
  content-type validation;
- deterministic VM resource/capability enforcement;
- persisted-state integrity and restart reconciliation.

## Current limitations

The MVP is testnet software. It does not yet provide a reviewed production
keystore, finalized release deployment, complete multi-device key rotation,
private rooms, private LEZ state, or production availability guarantees.
Finding that a documented release gate is unfinished is not itself a
vulnerability, but bypassing a stated authority or isolation boundary is.
