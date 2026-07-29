## Behavior

Describe the user-visible or protocol behavior changed.

## Evidence

List exact checks and compiled acceptance gates run. Include:

- source commit and immutable product snapshot;
- report paths and SHA-256 hashes;
- screenshot links and SHA-256 hashes when UI behavior changed;
- evidence sanitization performed;
- remaining gates or unverified behavior.

## Compatibility and security

Identify schema, package, persistence, authority, finality, or trust-boundary
effects. State remaining limitations.

## Checklist

- [ ] Change is focused and preserves repository-owned `program/` source.
- [ ] Tests cover observable seams and failure behavior.
- [ ] Documentation matches implementation.
- [ ] No credentials, keys, recovery phrases, private data, or local artifacts
      are included.
