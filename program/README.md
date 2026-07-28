# Palace LEZ program

`palace_program_core` holds Borsh/Serde state and transition rules.
`palace_program` exposes the LEZ instruction envelope. The guest builds a
single public `palace-state` PDA, initialized by an authenticated owner and
owned by the program thereafter.

LEZ and SPEL are runtime dependencies only; all Palace state, instruction,
guest, and image-build sources live in this repository.

State schema v2 adds a strictly increasing `ordered_action_id` to every
transition. Schema-v1 account bytes are intentionally incompatible; reinitialize
Palace testnet state after deploying the schema-v2 guest image.

Run deterministic contract checks:

```sh
cargo test --manifest-path program/Cargo.toml --workspace
cargo clippy --manifest-path program/Cargo.toml --workspace --all-targets -- -D warnings
```

Run guest adapter checks:

```sh
cargo test --manifest-path program/palace_program/methods/guest/Cargo.toml
```

Build the guest image from the methods crate after installing the RISC Zero
toolchain:

```sh
cargo build --manifest-path program/palace_program/methods/Cargo.toml --release
```
