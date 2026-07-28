# Changelog

All notable changes to Logos Palace are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
The project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
after its first published release.

## [Unreleased]

### Added

- Basecamp module foundation for Palace UI, Core, and deterministic VM.
- Portable LGX package outputs and contract checks.
- Verified Basecamp dynamic-asset provider boundary.
- Core-owned PNG covenant, verified-asset store, Storage V2 download flow, and
  verified-handle publication flow.
- Versioned Delivery envelope codec, topic derivation, egress preflight,
  signature/key binding, replay, expiry, motion, prop, and moderation policy.
- Product-owned Palace LEZ state, transition rules, instruction envelope,
  RISC Zero/SPEL guest, host tests, and release image build.
- Generated-API LEZ public transaction submission with canonical result
  validation.
- Checksummed atomic projection and durable action-journal persistence,
  including submitted transaction hashes and legacy orphan migration.

### Security

- QML remains outside network, arbitrary path, signing-key, and VM authority
  boundaries.
- Malformed assets, Delivery envelopes, LEZ responses, and persisted records
  fail closed at their owning boundaries.

### Known limitations

- No release-bound testnet program deployment or indexed LEZ finality recovery.
- No automated three-instance Delivery/Storage/Basecamp acceptance yet.
- No published release manifest or frozen performance baseline yet.

[Unreleased]: https://github.com/3esmit/logos-palace/commits/main
