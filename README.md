# Logos Palace

> **The Palace made cyberspace feel like a place. Logos Palace asks whether that place can finally belong to the people inside it.**

Logos Palace is an experimental, room-first social space for [Logos Basecamp](https://logos.co/basecamp). It reimagines the essential experience of **The Palace** - graphical rooms, expressive avatars, props, doors, conversation, and programmable shared spaces - on the modern Logos stack.

People meet through a locally run Basecamp application. Live presence and conversation travel over Logos Delivery. Room backgrounds, props, scripts, and catalogs are published through Logos Storage. Durable identity, permissions, moderation, and shared state are handled by a Palace program on Logos Execution Zone (LEZ). Palace itself has no application-owned backend.

The current release is an **x86_64 Linux MVP pre-alpha**. It is an experiment in architecture and community software, not a production social network.

Download [v0.1.0-pre-alpha.1](https://github.com/3esmit/logos-palace/releases/tag/v0.1.0-pre-alpha.1) or its [x86_64 Linux archive](https://github.com/3esmit/logos-palace/releases/download/v0.1.0-pre-alpha.1/logos-palace-mvp-0.1.0-pre-alpha.1-x86_64-linux.tar.gz). The archive SHA-256 is `6b571b2b54adf21756640767a17129b21b62115758e01047753d4f41e804aebb`.

Before installing Palace, add the [3esmit release catalog](https://raw.githubusercontent.com/3esmit/logos-3esmit-release/main/logos-repo.json) to the Basecamp package manager. The catalog supplies the forked Delivery, Storage, LEZ Core, and Logos Control packages required by the Palace modules.

For a package-manager install, select `logos_palace_ui`; its catalog entry pulls
`palace_core`, `palace_vm`, and the platform dependencies. The downloadable
archive remains available for the Linux x86_64 pre-alpha flow below.

## Why this should exist

A decentralised stack is incomplete if the places where people actually meet still belong to a platform operator. Messages, files, and transactions are its bones; shared rooms, rituals, symbols, memory, and trust are the life inside them. Logos Palace exists to make the stack inhabitable - a place where a community can shape its own culture, see the rules that govern it, and be built to survive the disappearance of any company, host, or founder.

**Feeds create audiences. Rooms create presence.**

## An unfinished idea from the early internet

### The Palace: independent worlds ordinary people could build

Released publicly in 1995, The Palace replaced the scrolling text channel with a visual place. People moved avatars through illustrated rooms, spoke in comic-style bubbles, created props and identities, crossed doors, and extended rooms with the IPTSCRAE scripting language.

Its most important architectural choice was easy to miss: downloading The Palace gave people both a client and a server that could run on an ordinary personal computer. Independent communities could create their own worlds instead of waiting for one company to create every space for them. The result was a loose constellation of public, private, commercial, artistic, and deeply personal Palaces.

The original system should not be romanticised as modern peer-to-peer software. Each Palace still depended on a conventional server, the software was proprietary, and local operators held substantial power. Its pioneering contribution was different: it distributed the ability to create and host social worlds, kept those worlds radically customisable, and treated users as world-builders rather than an audience.

### Electric Communities: people first, then the network

Electric Communities grew from the experience of Lucasfilm's Habitat and asked a broader question: what infrastructure would a global society in cyberspace require?

Its early work anticipated ideas that remain central today:

- people use networks primarily to connect with other people;
- communities need persistent places, not only streams of messages;
- large-scale cyberspace must be technically decentralised and open;
- creative and entrepreneurial power must also be decentralised;
- personal machines should be able to consume and provide services;
- distributed cooperation needs security models suitable for strangers.

Electric Communities explored these ideas through secure distributed objects, capability-based authority, optimistic computation, online economies, and peer-oriented virtual environments. Douglas Crockford later described the ambition plainly: a global social network that was both secure and decentralised.

### Communities.com: the synthesis that did not arrive yet

Electric Communities later acquired The Palace and OnLive Technologies and operated as **Communities.com**. The Palace's human-facing world of rooms, avatars, props, and culture then sat under the same roof as Electric Communities' work on secure distributed social infrastructure.

That did **not** turn the original Palace into a peer-to-peer system, and the dot-com company did not complete the synthesis. But the combination left behind an important unfinished question:

> What would happen if a social world as immediate and human as The Palace were built on infrastructure that its communities could actually own?

Logos Palace is one attempt to answer it with technology that did not exist in the 1990s.

## Why Palace belongs on Logos

[*Farewell to Westphalia*](https://logos.co/book) argues that blockchain's deepest use is not merely finance but **decentralised-yet-cooperative human governance**. It treats communities - not only states or corporations - as meaningful political and cultural actors. Those communities may number billions, or they may be as small as a dozen friends.

Palace begins with the dozen friends.

A room is a small social institution. It immediately raises the questions that abstract infrastructure can hide:

- Who may enter, speak, create, moderate, or remove?
- Which events are fleeting, and which become part of shared history?
- Who owns the place's images, scripts, and cultural artifacts?
- What happens when a host disappears?
- Can the community continue when its creator leaves?
- Are its rules visible, verifiable, and open to change?

That is why Logos Palace is more than a graphical chat demo. It is a concrete test of whether the Logos stack can support a living social institution.

| Logos component | Human meaning inside a Palace | Technical role |
| --- | --- | --- |
| **Basecamp** | The participant's own front door | Runs the application and its modules locally, on hardware controlled by the user. |
| **Delivery** | What is happening now | Carries signed, expiring presence, movement, conversation, and prop traffic. It is fast, but it is not durable authority. |
| **Storage** | What the place is made of | Publishes and retrieves participant-authored backgrounds, props, scripts, and catalogs by content identity. |
| **LEZ** | The Palace's durable rules and memory | Records identities, capabilities, bans, room locks, moderation, and finalised shared transitions. |
| **Palace VM** | The room's bounded physics | Executes deterministic room behaviour without filesystem, wallet, network, or UI authority. |

The [Logos manifesto](https://logos.co/manifesto) calls for a full decentralised stack spanning communication, storage, execution, and voluntary social institutions. Basecamp keeps that stack local and composable. Palace gives it a human-scale front door: not another dashboard, but somewhere people can inhabit together.

## Principles

- **A room is not a feed.** The product is organised around shared presence, not reach, ranking, or engagement extraction.
- **No application-owned server.** Palace composes user-controlled Logos infrastructure rather than depending on a proprietary Palace backend.
- **Live is not law.** Delivery makes interaction responsive; only verified LEZ evidence can change durable authority.
- **Hosting is not ruling.** Providing Storage or network service does not itself grant a Palace capability.
- **Culture is participant-authored.** Backgrounds and optional props are selected, verified, published, and assigned by users; they are not hidden product assets.
- **Uncertainty must be visible.** Missing or conflicting evidence produces pending, offline, degraded, or rejected states instead of silently promoting cached data.
- **The founder is not the community.** The design target is for finalised records and independently retained content to survive creator removal.
- **Boundaries should not become cages.** Communities should support meaningful entry, participation, exit, and the creation of alternatives.

## What using it looks like

1. Install Palace and its release-index dependencies, then open Logos Control in Basecamp, start the required Storage and LEZ nodes, and connect Palace to the running modules.
2. The creator creates a Palace. The current MVP creates exactly two room records.
3. An administrator chooses room backgrounds and optional props. Palace Core verifies the bytes before publication and assignment.
4. The creator shares the `palace://` address, the canonical Storage catalog, and a peer endpoint.
5. A participant joins. Palace verifies the catalog, retrieves the content, checks LEZ history, registers the participant, and opens Delivery.
6. Presence, movement, speech, and props flow live through Delivery. Durable actions become authoritative only through LEZ finality for the active profile.
7. After a restart or provider loss, Palace is designed to restore its journal and verified assets, recover authority from the exact checkpoint, and reconnect only after its evidence agrees.

## Architecture at a glance

```text
                                  Palace LEZ program
                              durable rules and history
                                          ^
                                          |
Basecamp -> logos_palace_ui -> palace_core -> lez_core
                                  |   |   |
                                  |   |   +--> storage_module
                                  |   +------> delivery_module
                                  +----------> palace_vm
```

`palace_core` is the trust-bearing composition boundary. The UI sends semantic user intent and receives immutable projections; it does not receive wallet handles, signing keys, Storage clients, arbitrary local paths, or authority to decide finality.

The Palace-owned Basecamp product contains three LGX packages. Its platform
dependencies are installed from the 3esmit release catalog:

| Package | Role | Source |
| --- | --- | --- |
| `palace_vm` | bounded deterministic room-script execution | this repository |
| `palace_core` | authority, persistence, orchestration, and recovery | this repository |
| `logos_palace_ui` | Basecamp presentation and user input | this repository |
| `delivery_module` | live peer-to-peer room transport | 3esmit release catalog |
| `storage_module` | content-addressed object transport | 3esmit release catalog |
| `lez_core` | wallet and LEZ runtime bridge | 3esmit release catalog |

The Palace LEZ program is built separately from those packages, but its source, schema, state machine, SPEL guest, and RISC Zero image build are owned by this repository under [`program/`](program/).

For the complete trust model, transition lifecycle, verified-asset boundary, recovery rules, and release harness, read [`ARCHITECTURE.md`](ARCHITECTURE.md). For the end-to-end product story and validation plan, read [`MVP_IMPLEMENTATION_PLAN.md`](MVP_IMPLEMENTATION_PLAN.md).

## Current status

**MVP pre-alpha - Linux `x86_64` - tag `v0.1.0-pre-alpha.1`**

The compiled local-development story currently passes with three independent Basecamp clients:

- administrator-selected backgrounds are published and fetched;
- ordered Delivery traffic converges across clients;
- a door promotes a room transition through the local sequencer cycle;
- moderation rejects banned traffic;
- a missing Storage source fails closed with an explicit degraded result;
- clients recover after the original provider stops.

The project does **not** yet claim completion of the public-testnet MVP. A clean release run still needs public LEZ finality, creator removal with retained records and content, every required cold-start/rebuild path, final resource evidence, and a production security review.

### Privacy boundary

Logos is building a private-by-default stack. **The current Logos Palace MVP is not a private-room product.** Palace authority is public LEZ state, and private rooms and private LEZ state are explicitly out of scope for this release. Do not infer confidentiality from encrypted transport or from the broader direction of the Logos stack.

## Build and test

### Prerequisites

- Linux `x86_64`;
- Nix with flakes enabled;
- network access for pinned GitHub and Nix dependencies;
- a per-user `systemd` manager and unified cgroup v2 for the official acceptance runners;
- Rust and the RISC Zero toolchain only when building the Palace program directly.

The `systemd` and cgroup requirements apply only to the official acceptance
runners below. Ordinary Basecamp execution and the local MVP user story do not
require systemd-managed scopes.

Dependency revisions are frozen by [`flake.lock`](flake.lock).

### Build Palace and pinned release-index LGX packages

```sh
nix build \
  .#palace-vm-lgx-portable \
  .#palace-core-lgx-portable \
  .#logos-palace-ui-lgx-portable \
  .#delivery-module-lgx-portable \
  .#storage-module-lgx-portable \
  .#lez-core-lgx-portable
```

### Run contract and release-seam checks

```sh
nix build \
  .#checks.x86_64-linux.palace-vm-contracts \
  .#checks.x86_64-linux.palace-core-contracts \
  .#checks.x86_64-linux.palace-acceptance-seams
```

### Build and test the Palace program

```sh
cargo test --manifest-path program/Cargo.toml --workspace
cargo clippy --manifest-path program/Cargo.toml --workspace --all-targets -- -D warnings
cargo test --manifest-path program/palace_program/methods/guest/Cargo.toml
cargo build --manifest-path program/palace_program/methods/Cargo.toml --release
```

The final command requires the RISC Zero guest toolchain. Program schemas, bounds, capabilities, PDA seeds, and migration notes are documented in [`program/README.md`](program/README.md).

### Run the local MVP user story

This is the stack-complete local loop described above. It starts a real local
sequencer, deploys the supplied Palace program, launches three compiled
Basecamp clients, and writes a path-free report plus screenshots. Every runtime
dependency and the user-story asset manifest are explicit inputs; no asset
filename is compiled into the product.

```sh
PALACE_LEZ_PROFILE=local-development \
PALACE_LOCAL_MVP_SEQUENCER=/path/to/sequencer_service \
PALACE_LOCAL_MVP_SEQUENCER_CONFIG=/path/to/sequencer_config.json \
PALACE_LOCAL_MVP_BASECAMP=/path/to/LogosBasecamp \
LOGOS_QT_MCP=/path/to/logos-qt-mcp \
PALACE_LOCAL_MVP_LGPM=/path/to/lgpm \
PALACE_LOCAL_MVP_DEPLOY_TOOL=/path/to/deploy_program_ffi \
PALACE_LOCAL_MVP_PROGRAM=/path/to/palace.bin \
PALACE_LOCAL_MVP_LEZ_LGX=/path/to/lez_core.lgx \
PALACE_LOCAL_MVP_STORAGE_LGX=/path/to/storage.lgx \
PALACE_LOCAL_MVP_DELIVERY_LGX=/path/to/delivery.lgx \
PALACE_LOCAL_MVP_VM_LGX=/path/to/palace_vm.lgx \
PALACE_LOCAL_MVP_CONTROL_LGX=/path/to/logos_control_ui.lgx \
PALACE_LOCAL_MVP_CORE_LGX=/path/to/palace_core.lgx \
PALACE_LOCAL_MVP_UI_LGX=/path/to/logos_palace_ui.lgx \
PALACE_E2E_ASSET_INPUT_ROOT=/path/to/asset-inputs \
PALACE_E2E_ASSET_MANIFEST=/path/to/asset-inputs/manifest-v1.json \
./scripts/run-basecamp-local-mvp.sh .artifacts/local-mvp
```

Keep the asset inputs outside Git. The manifest selects the backgrounds and
optional props used by this user story.

### Run compiled Basecamp end-to-end checks

```sh
./scripts/run-basecamp-gate1.sh
./scripts/run-basecamp-gate2.sh
```

The local and public full-stack runners require explicit runtime paths and produce persistent evidence. Read [`MVP_IMPLEMENTATION_PLAN.md`](MVP_IMPLEMENTATION_PLAN.md) and the script usage before running them. The public runner creates testnet effects that cannot be undone by deleting local artifacts.

### Build the pre-alpha archive

```sh
PALACE_RELEASE_VERSION=v0.1.0-pre-alpha.1 \
  ./scripts/package-prealpha-release.sh .artifacts/prealpha-release
```

The archive contains the three Palace-owned LGX packages, the repository-built `palace.bin`, its RISC Zero image ID, a `release.json` with byte lengths and SHA-256 digests, and copies of `README.md`, `LICENSE`, and `SECURITY.md`. Delivery, Storage, LEZ Core, and Logos Control are installed from the 3esmit release catalog rather than bundled copies.

## Security and limitations

This is experimental public-testnet software.

- Current rooms do not provide confidentiality.
- Testnet keys, dependency forks, network availability, participant retention, Basecamp isolation, and the deployed program have not received a production security audit.
- Storage CIDs identify immutable manifests, but future availability still depends on independent retention and successful retrieval.
- Delivered or cached data does not become canonical merely because authoritative data is temporarily unavailable.
- Missing or conflicting evidence fails closed.
- Public-testnet effects created by the release runner cannot be undone by deleting local artifacts.

Report vulnerabilities privately as described in [`SECURITY.md`](SECURITY.md).

## Contributing

Contributions are welcome, particularly those that strengthen the room-first experience without weakening the trust boundaries: expressive identity, social presence, authoring tools, accessibility, moderation, recovery, portability, and evidence-backed integration across the Logos stack.

Read [`CONTRIBUTING.md`](CONTRIBUTING.md) and [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md) before submitting changes.

## Historical and conceptual reading

- ["Metaworlds" - Wired, June 1996](https://www.wired.com/1996/06/avatar-2/): contemporary reporting on The Palace's user-created avatars, ordinary-PC servers, scripting, and independently operated sites.
- [*From Habitat to Global Cyberspace* - Farmer, Morningstar, and Crockford](https://web.stanford.edu/class/history34q/readings/Virtual_Worlds/FromHabitatToCyberspace.html): persistent places, many-to-many communication, openness, decentralisation, commerce, and community.
- [Electric Communities archive - Douglas Crockford](https://www.crockford.com/ec.html): original papers and technical documents on distributed virtual communities and the E language.
- [Communities.com intellectual-property archive](https://www.crockford.com/ec/ccip.html): the documented transition from Electric Communities to Communities.com and its acquisition of The Palace and OnLive.
- ["It Was Twenty Years Ago Today" - Douglas Crockford](https://www.crockford.com/20js.html): a retrospective on the attempt to build a secure, decentralised global social network.
- [*Farewell to Westphalia* - Jarrad Hope and Peter Ludlow](https://logos.co/book): decentralised-yet-cooperative governance, sovereign blockchain communities, exit, records, communication, and human values in the technology stack.
- [Logos: A Declaration of Independence in Cyberspace](https://logos.co/manifesto): the case for a full privacy-preserving, decentralised stack supporting voluntary social institutions.
- [Basecamp in Testnet v0.2](https://blog.logos.co/article/basecamp-v02): why the Logos stack runs locally and composes user-installed modules without a centralised application store.
- ["Community is Infra"](https://blog.logos.co/article/radical-humility): why social trust, repair, culture, and human relationships are infrastructure rather than decoration around the code.

## Independence and attribution

Logos Palace is an independent experimental project inspired by The Palace and by the published work of Electric Communities. It is not affiliated with or endorsed by the former Palace, Electric Communities, Communities.com, or any current holder of related names or intellectual property. All trademarks belong to their respective owners.

## License

MIT. See [`LICENSE`](LICENSE).
