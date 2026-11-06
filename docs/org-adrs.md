# Org-wide decisions (frostyard/core ADRs)

Conventions this repository follows that are decided at the org level are
recorded as ADRs in
[frostyard/core](https://github.com/frostyard/core/tree/main/docs/adr).
The ones that bind Igloo:

- [ADR-0018 — Org-wide agent instruction and knowledge surfaces](https://github.com/frostyard/core/blob/main/docs/adr/0018-org-wide-agent-instruction-and-knowledge-surfaces.md) — `AGENTS.md` is canonical; `CLAUDE.md`, `GEMINI.md`, `.cursorrules` and `.github/copilot-instructions.md` are symlinks to it, and `.memory/corrections.jsonl` is the append-only corrections inbox ([`.memory/README.md`](../.memory/README.md))
- [ADR-0019 — Governance as code and risk tiers](https://github.com/frostyard/core/blob/main/docs/adr/0019-governance-as-code-and-risk-tiers.md) — [`.github/pull_request_template.md`](../.github/pull_request_template.md) has a `Risk tier` section
- [ADR-0021 — SHA-pinned actions and least-privilege CI workflows](https://github.com/frostyard/core/blob/main/docs/adr/0021-sha-pinned-actions-and-least-privilege-ci.md) — every action in [`.github/workflows/`](../.github/workflows/) is pinned to a commit SHA with a version comment, every workflow has read-only or empty top-level permissions, and every checkout sets `persist-credentials: false`
- [ADR-0022 — make ci gate](https://github.com/frostyard/core/blob/main/docs/adr/0022-make-ci-gate-and-test-naming-filter.md) and [ADR-0038 — make ci stays canonical](https://github.com/frostyard/core/blob/main/docs/adr/0038-scope-the-test-name-filter-to-chairlift.md) — `make ci` is the credential-free gate and runs every test; the test-name filter is chairlift's only
- [ADR-0025 — One docs/ tree per repository, in core's four-category shape](https://github.com/frostyard/core/blob/main/docs/adr/0025-consolidate-repository-docs-into-docs.md) — this `docs/` tree, indexed in [README.md](README.md)
- [ADR-0029 — ACMM conformance via canonical aliases](https://github.com/frostyard/core/blob/main/docs/adr/0029-acmm-conformance-via-canonical-aliases.md) — the alias symlinks above and `.claude/skills` → `.agents/skills`
- [ADR-0040 — Publish the repository settings contract](https://github.com/frostyard/core/blob/main/docs/adr/0040-publish-the-repository-settings-contract.md) — [`SECURITY.md`](../SECURITY.md) names GitHub private vulnerability reporting and the versions that get fixes
- [ADR-0043 — Pin repository tools in mise and name the verify gate](https://github.com/frostyard/core/blob/main/docs/adr/0043-pin-repository-tools-in-mise-and-name-the-verify-gate.md) — [`mise.toml`](../mise.toml) and [`mise.lock`](../mise.lock) pin Node; the GNOME SDK and flatpak-builder, which mise cannot install, come from Flathub and from CI's container image ([ADR-0002](adr/0002-adopt-the-repository-baseline.md)); `make verify` changes no file
- [ADR-0044 — Expose the make gate triad in every repository](https://github.com/frostyard/core/blob/main/docs/adr/0044-expose-the-make-gate-triad-in-every-repository.md) — [`Makefile`](../Makefile): `make verify`, `make check` and `make ci`; `ci` runs `verify`
- [ADR-0051 — ADRs state facts only](https://github.com/frostyard/core/blob/main/docs/adr/0051-adrs-state-facts-not-process.md) — the ADRs in [`adr/`](adr/)
- [ADR-0068 — Label pull requests that touch high-risk paths](https://github.com/frostyard/core/blob/main/docs/adr/0068-label-pull-requests-that-touch-high-risk-paths.md) — [`.github/risk-paths.json`](../.github/risk-paths.json) and the [`risk-paths.yml`](../.github/workflows/risk-paths.yml) caller
- [ADR-0070 — Build tool docs sites with ProperDocs and MaterialX on GitHub Pages](https://github.com/frostyard/core/blob/main/docs/adr/0070-build-tool-docs-sites-with-materialx-on-github-pages.md) — Igloo is an application that people install, so it gets a site; until it exists, the README's documentation line links this index
- [ADR-0071 — Hold every code repository to one baseline](https://github.com/frostyard/core/blob/main/docs/adr/0071-hold-every-code-repository-to-one-baseline.md) — rows B1 to B17; see [ADR-0002](adr/0002-adopt-the-repository-baseline.md). [`LICENSE`](../LICENSE) is GPL-3.0-or-later, a copy of upstream's `COPYING`
- [ADR-0072 — Write every README to one standard](https://github.com/frostyard/core/blob/main/docs/adr/0072-write-every-readme-to-one-standard.md) — `README.md` is an `app` README
- [ADR-0073 — Let CI run the make ci targets as separate jobs](https://github.com/frostyard/core/blob/main/docs/adr/0073-let-ci-run-the-make-ci-targets-as-separate-jobs.md) — [`ci.yml`](../.github/workflows/ci.yml) runs `make verify` and `make corrections-base` in the job `verify`, and `make test` in the job `test`

Known departure:
[ADR-0016 — Reverse-DNS org.frostyard.* identifiers](https://github.com/frostyard/core/blob/main/docs/adr/0016-reverse-dns-org-frostyard-identifiers.md)
names application IDs `org.frostyard.<AppName>`. Igloo's application ID is
`io.github.frostyard.Igloo`, from before this list; it is the Flatpak ID,
the GSettings ID and the settings path of installed copies, so a change is a
breaking change with a migration of its own.

Not applicable: the Go and svu ADRs
([ADR-0012](https://github.com/frostyard/core/blob/main/docs/adr/0012-svu-versioning-and-rolling-dev-prerelease.md),
[ADR-0053](https://github.com/frostyard/core/blob/main/docs/adr/0053-name-the-svu-config-svu-yml.md)) — Igloo is C, and its
version follows Ptyxis's (`meson.build`); the package and sysext ADRs
([ADR-0009](https://github.com/frostyard/core/blob/main/docs/adr/0009-single-artifact-origin-repository-frostyard-org.md),
[ADR-0055](https://github.com/frostyard/core/blob/main/docs/adr/0055-publish-debian-packages-through-the-apt-publisher.md),
[ADR-0008](https://github.com/frostyard/core/blob/main/docs/adr/0008-sysext-distribution-and-update-contract.md)) — Igloo ships
only a Flatpak, through the Frostyard remote.

When changing behavior covered by one of these, update or supersede the ADR
in frostyard/core first, then change this repository in the same effort.
