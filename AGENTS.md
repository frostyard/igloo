# Igloo

Igloo is a GNOME terminal, based on Ptyxis, that opens
[nsl](https://github.com/frostyard/nsl) machines the way Ptyxis opens Podman,
Toolbox and Distrobox containers. It ships as the Flatpak
`org.frostyard.Igloo`. Start at [docs/README.md](docs/README.md).

This file (`AGENTS.md`) is the CANONICAL agent instructions: `CLAUDE.md`,
`GEMINI.md`, `.cursorrules` and `.github/copilot-instructions.md` are
symlinks to it, and `.claude/skills` symlinks to `.agents/skills/`
(core ADR-0018 and ADR-0029). Edit only the canonical paths; keep content
tool-agnostic.

## Skills (follow these for common tasks)

Step-by-step procedures live in [.agents/skills/](.agents/skills/); follow
them rather than improvising, whichever agent you are. Start a new one from
`.agents/skills/TEMPLATE/SKILL.md` and add a bullet here.

- *(none yet: add the first one when a procedure repeats)*

## Code conventions (live: the code exists)

- **Upstream names stay.** Igloo follows Ptyxis upstream
  ([ADR-0001](docs/adr/0001-follow-ptyxis-upstream.md)). Source files, code
  symbols (`ptyxis_*`, `Ptyxis*`), the agent and its D-Bus names
  (`org.gnome.Ptyxis.*`) keep Ptyxis's names so that upstream changes merge
  cleanly. Rename only what users see. The meson option `generic` names the
  application; its default `igloo` sets the application and GSettings IDs to
  `org.frostyard.Igloo` (`meson.build`).
- **The previous ID stays handled.** Before the rename, the ID was
  `io.github.frostyard.Igloo` ([ADR-0003](docs/adr/0003-rename-the-application-id.md)).
  Keep `src/ptyxis-igloo-migration.c` (called first in `main()`), the
  `X-Flatpak-RenamedFrom` and `<replaces>` entries in `data/`, and the
  end-of-life image step in `.github/workflows/flatpak.yml`. A change that
  reads or writes settings or files under `APP_ID` must keep the migration
  before it.
- **Upstream files stay.** `README.ptyxis.md`, `NEWS`, `ptyxis.doap`,
  `org.gnome.Ptyxis.Devel.json`, `.gitlab-ci.yml`, `.gitlab/`, `.foundry/`,
  `subprojects/`, `man/` and `po/` come from Ptyxis. Do not delete or rewrite
  them; an upstream merge updates them.
- **License.** Igloo is GPL-3.0-or-later, like Ptyxis. `COPYING` is
  upstream's license file and `LICENSE` is a byte-identical copy of it for
  the frostyard baseline; `make verify` checks that they match. A new source
  file carries the GPL-3.0-or-later header and SPDX line of
  `src/ptyxis-machines-page.c`. Never remove a copyright line.
- **The nsl integration** is in `agent/ptyxis-nsl*.c`,
  `agent/ptyxis-machines-impl.c` and `src/ptyxis-machine*.c`
  ([design](docs/design/nsl-integration.md)). nsl runs on the host, through
  `ptyxis-agent`, never inside the Flatpak sandbox.
- **nsl's machine-readable output is the contract.** `agent/ptyxis-nsl.c`
  reads `nsl list --json`, `nsl images --json` and `nsl config --json` as
  nsl's [CLI spec](https://github.com/frostyard/nsl/blob/main/docs/specs/cli.md#machine-readable-output)
  defines them, and ignores fields it does not know. When nsl's interface
  changes, Igloo follows in a PR of its own; update the fixtures in
  `testsuite/nsl/` from real nsl output and keep the minimum nsl version
  (0.8.0) in `README.md` true.
- **Never poll nsl.** An `nsl list` against a running VM counts as activity
  and keeps the VM from idling. `agent/ptyxis-nsl-provider.c` reloads at
  startup, when nsl's files change, after a Machines page command or a
  rescan (*Check Again*), and twice (after 5 s and 20 s) after a terminal
  starts a machine; never on a repeating timer.
- **Moving docs keeps every fact.** When text moves out of `README.md` or
  between docs, each fact of the old text lands in its new place, or the PR
  says why it is dropped. Compare the old text sentence by sentence before
  you call the move done.
- **Tests** are C programs in `testsuite/`, registered in
  `testsuite/meson.build`. `make test` builds and runs them in the GNOME 51
  SDK.
- Run `make verify` before you call any change done, and `make test` for a
  change to C, meson or UI files. CI runs `make ci`'s 3 parts as the jobs
  `verify` and `test` of `.github/workflows/ci.yml`; `make ci BASE=origin/main`
  runs the same locally, including the check that `.memory/corrections.jsonl`
  is append-only against the PR base.
- `mise.toml` and `mise.lock` pin Node for `make verify` (core ADR-0043); run
  `mise install`. Change a pin only with `mise lock`, in a commit of its own.
  The GNOME SDK and flatpak-builder come from Flathub (see
  [build and release](docs/design/build-and-release.md)).

**Test reliability.** No sleeps for timing (wait on a condition or inject a
clock). No network and no wall clock in unit tests (inject them). A flaky test
gets an issue, then a fix or a quarantine (skip with the issue link) in the
same PR. Never retry a job or a test until it is green.

**Blocker to guard.** When a review (agent cross-review, owner review, or a
human) finds a blocking point, the PR that fixes it also adds a guard: a
targeted test that fails without the fix (preferred), or an `AGENTS.md` rule
when no test can catch it. The same PR appends 1 line to
`.memory/corrections.jsonl` (`evidence`: the PR and the review;
`promoted_to`: the test or the rule). See `.memory/README.md`.

**Risk paths.** `.github/risk-paths.json` lists the high-risk paths; a PR that
touches one gets `risk:high` from `.github/workflows/risk-paths.yml` and
declares tier 3. Add a path when you add a file of that kind (signing, auth,
publish, secrets, destructive operations, CI).

**Vendored files.** `scripts/lib/risk-paths.mjs` and
`scripts/check-corrections.mjs` are copies from frostyard/core; never edit them
by hand (`make verify` checks their hashes). `scripts/check-template.mjs` and
`test/check-template.test.mjs` are copies from bketelsen/agentic-template.

## Repository boundary

- Product code is C (`src/`, `agent/`), meson, GTK UI files and data. Build
  outputs (`build-dir/`, `_build/`, `.flatpak-builder/`, `repo/`, bundles)
  stay out of git (`.gitignore`).
- A release is a `v*` tag: `.github/workflows/flatpak.yml` builds the
  Flatpak, pushes the OCI image to `ghcr.io/frostyard/igloo`, attests it and
  moves `latest`, which the Frostyard remote serves. Pushes to `main` publish
  nothing. Only the maintainer tags.
- Report problems with Ptyxis itself upstream; this repository holds the
  nsl features and the Igloo name.

## Documentation rules (enforced)

Docs live in `docs/` in four categories. **Every new doc starts from its
category's `TEMPLATE.md`** and follows its structure:

- `docs/adr/`: why we decided. Immutable once Accepted; reversals are new
  ADRs that mark the old one Superseded. A new org-wide rule goes in a core
  ADR listed in `docs/org-adrs.md`; a new project rule goes in a new ADR.
- `docs/design/`: how it fits together. Living; updated in place to match
  reality.
- `docs/specs/`: exact contracts. Change only alongside implementing code.
- `docs/plans/`: order of work, open work only. Phases with "Done when"
  outcomes.

### Cross-linking is mandatory

A doc without its required links is incomplete. Do not finish a docs change
until they exist, in both directions:

- **ADR** → links every design doc/spec it shapes, and prior ADRs it builds on.
- **Design doc** → links the ADR(s) providing its rationale, the spec(s)
  pinning its contracts, and the plan that builds it.
- **Spec** → links its motivating ADR(s) and the design doc showing where it
  fits.
- **Plan** → every phase links the design docs/specs it implements; resolved
  open questions become ADRs.

When you touch a doc, verify its links still hold (targets exist, section
anchors valid) and add the back-links on the targets. Use relative paths.

### Housekeeping

- New doc ⇒ add a line to the index in [docs/README.md](docs/README.md).
- New significant decision ⇒ new ADR *first*, then update the affected design
  docs/specs in the same change.
- Convert relative dates ("next weekend") to absolute dates in all docs.
- The root `README.md` follows core's README spec for the kind `app`
  (core ADR-0072); user guides, internals and release steps go in `docs/`.
