# Contributing

Contributions are welcome while Logos Palace is developed as an experimental
Basecamp MVP.

## Before changing code

Preserve these boundaries:

- Palace program and guest source stays in this repository;
- QML does not call Delivery, Storage, LEZ, or arbitrary filesystem paths;
- `palace_core` owns network, persistence, authority, and recovery composition;
- `palace_vm` remains deterministic and authority-free;
- Delivery publication is never treated as LEZ finality;
- cached or malformed external data fails closed.

Prefer a deep implementation behind a small interface. Put behavioral tests at
module, generated-API, process, and persistence seams when possible.

## Development workflow

1. Fork the repository and create a focused branch.
2. Add or update a regression/contract test for observable behavior.
3. Implement the smallest coherent change.
4. Run affected checks.
5. Open a pull request describing behavior, evidence, compatibility, and
   remaining limitations.

Do not commit credentials, testnet private keys, node data, generated build
directories, or local `.3esmit` material.

## Checks

C++ modules:

```sh
nix build \
  .#checks.x86_64-linux.palace-vm-contracts \
  .#checks.x86_64-linux.palace-core-contracts \
  .#palace-vm-lgx-portable \
  .#palace-core-lgx-portable \
  .#logos-palace-ui-lgx-portable
```

Palace LEZ program:

```sh
cargo test --manifest-path program/Cargo.toml --workspace
cargo clippy --manifest-path program/Cargo.toml --workspace --all-targets -- -D warnings
cargo test --manifest-path program/palace_program/methods/guest/Cargo.toml
cargo clippy --manifest-path program/palace_program/methods/guest/Cargo.toml --all-targets -- -D warnings
cargo build --manifest-path program/palace_program/methods/Cargo.toml --release
```

Inspect `git diff --check` and the final diff before committing.

## Commits and pull requests

Use focused Conventional Commits where practical, for example:

```text
feat(core): publish verified Storage assets
fix(program): reject unauthorized state transitions
test(delivery): cover replay after restart
docs: document Basecamp acceptance limits
```

Pull requests must state which clean runtime acceptance remains unverified.
Passing unit or package checks must not be described as full MVP acceptance.

## Reporting bugs

Open a GitHub issue with reproduction steps, expected behavior, actual
behavior, environment, and relevant non-sensitive logs. Follow
[SECURITY.md](SECURITY.md) for vulnerabilities.
