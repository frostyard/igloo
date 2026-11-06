# Documentation

Docs are split by the question they answer:

| Directory | Question | Contents |
| --- | --- | --- |
| [adr/](adr/) | **Why** did we choose this? | Architecture Decision Records — immutable once accepted; superseded, never edited |
| [design/](design/) | **How** does it fit together? | Living documents describing the current architecture |
| [specs/](specs/) | **What exactly** is the contract? | Precise, testable interface definitions |
| [plans/](plans/) | **When/in what order** do we build? | Open work only; updated as work lands |

Igloo is Ptyxis with nsl machines added. Upstream's README is
[README.ptyxis.md](../README.ptyxis.md); it describes everything Igloo
inherits.

## Index

### Decisions (ADRs)

- [0001 — Follow Ptyxis upstream and rename only what users see](adr/0001-follow-ptyxis-upstream.md)
- [0002 — Adopt the frostyard repository baseline](adr/0002-adopt-the-repository-baseline.md)

### Org-wide decisions

- [Org-wide decisions (frostyard/core ADRs)](org-adrs.md)

### Design

- [nsl integration](design/nsl-integration.md) — the agent, the Machines page, working directories
- [Build and release](design/build-and-release.md) — the Flatpak, the make gate, CI and tag publication

### Specs

*(none yet: Igloo reads nsl's [CLI contract](https://github.com/frostyard/nsl/blob/main/docs/specs/cli.md#machine-readable-output))*

### Plans

*(none yet)*

## Conventions

- **New docs start from their category's `TEMPLATE.md`** (in each directory).
- New decision → new ADR with the next number; if it reverses an old one, mark
  the old one `Superseded by NNNN` rather than editing it.
- Design docs are updated in place to always reflect reality.
- Specs change only alongside the code that implements them.
- Cross-links between categories are mandatory in both directions — see the
  documentation rules in [AGENTS.md](../AGENTS.md).
- Adding a doc means adding it to the index above.
