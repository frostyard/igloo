# 0002 — Adopt the frostyard repository baseline

- **Status:** Accepted
- **Date:** 2026-11-06

## Context

frostyard/core holds every code repository to 1 baseline
([core ADR-0071](https://github.com/frostyard/core/blob/main/docs/adr/0071-hold-every-code-repository-to-one-baseline.md),
[spec](https://github.com/frostyard/core/blob/main/docs/specs/repository-baseline.md),
rows B1 to B17). Igloo is a C application that builds only in the GNOME 51
SDK: its meson build needs GTK, libadwaita and VTE 0.84, which the manifest
builds. Its CI already built the Flatpak with its tests in
`.github/workflows/flatpak.yml`, which also publishes releases. Igloo keeps
Ptyxis's files ([ADR-0001](0001-follow-ptyxis-upstream.md)), including the
license file `COPYING`.

## Decision

- The repository carries the baseline files of bketelsen/agentic-template:
  `AGENTS.md` and its aliases, `.memory/corrections.jsonl`, the `docs/`
  shape, the risk-path list and its caller, the PR template, `SECURITY.md`,
  and the template gate (`scripts/check-template.mjs`) with core's 2
  vendored scripts.
- The make gate (core ADR-0044): `make verify` runs the template gate and
  the Node tests, with Node from `mise.toml` and `mise.lock`; `make test`
  builds Igloo in the GNOME 51 SDK and runs its meson tests; `make check` is
  `verify`; `make ci` is `verify`, `corrections-base` and `test`.
- The GNOME SDK and flatpak-builder are not mise tools. A developer installs
  them from Flathub; CI runs `make test` in the
  `ghcr.io/flathub-infra/flatpak-github-actions:gnome-51` image, the image
  that `flatpak.yml` uses.
- `.github/workflows/ci.yml` runs `make ci` as 2 jobs (core ADR-0073):
  `verify` (`make verify`, `make corrections-base`) and `test`
  (`make test`). `flatpak.yml` stays as it is: it builds and tests the
  release configuration, keeps the bundle and publishes on `v*` tags.
- `LICENSE` is a byte-identical copy of `COPYING`; a Node test checks it.
  `COPYING` stays for upstream.
- The root `README.md` is an `app` README (core ADR-0072); its internals and
  build steps move to `docs/design/`.

## Consequences

- `make verify` needs only Node and is fast; `make test` and `make ci` need
  flatpak and the GNOME 51 SDK, and the first run builds VTE.
- A pull request builds Igloo twice: once in `ci.yml` (`make test`, a debug
  build) and once in `flatpak.yml` (the release configuration).
- A pull request that touches a path in `.github/risk-paths.json` gets
  `risk:high` and declares tier 3.
- A docs site (core ADR-0070, row B9) is separate work: until it exists,
  the README's documentation line links `docs/README.md`.

## Alternatives considered

- **`make test` with the modified manifest that flatpak-builder's action
  writes:** the action adds `run-tests` to the manifest at run time; a
  second copy of that logic in the Makefile would drift from it. Rejected
  for the documented "develop inside the build environment" steps.
- **Run `make test` from `flatpak.yml` instead of the action:** loses the
  action's cache, bundle and artifact handling, and changes the release
  workflow. Rejected.
- **Rename `COPYING` to `LICENSE`:** upstream keeps `COPYING`. Rejected.

## References

- Builds on: [ADR-0001](0001-follow-ptyxis-upstream.md)
- Shapes: [design: build and release](../design/build-and-release.md),
  [`Makefile`](../../Makefile), [`.github/risk-paths.json`](../../.github/risk-paths.json),
  [org-wide decisions](../org-adrs.md)
- Amended in part by: [core ADR-0081](https://github.com/frostyard/core/blob/main/docs/adr/0081-store-each-correction-in-a-file-of-its-own.md)
