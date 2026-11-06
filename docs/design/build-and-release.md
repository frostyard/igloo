# Build and release

Living document. Rationale: [ADR-0002](../adr/0002-adopt-the-repository-baseline.md)
(the make gate and CI), [ADR-0001](../adr/0001-follow-ptyxis-upstream.md)
(the upstream build that Igloo keeps).
Contracts: none in this repository; the Frostyard remote's rules are in
[frostyard/flatpak-index](https://github.com/frostyard/flatpak-index).

## Overview

Igloo builds as a Flatpak from `io.github.frostyard.Igloo.json` on the GNOME
51 runtime. The manifest builds fast_float, simdutf and VTE 0.84.1, then this
checkout with meson (`-Dgeneric=igloo -Dlibc-compat=true`). A `v*` tag
publishes the Flatpak as an attested OCI image that the Frostyard remote
serves.

```
checkout ──► flatpak-builder (GNOME 51 SDK) ──► bundle (every run)
                                            └─► OCI image ──► ghcr.io ──► Frostyard remote (v* tags)
```

## Design

### The make gate

| Target | What it runs | Needs |
| --- | --- | --- |
| `make verify` | `scripts/check-template.mjs` and the Node tests in `test/` | Node 22 or later (`mise install`) |
| `make test` | flatpak-builder builds the dependencies into `build-dir` (`--stop-at=ptyxis`); then, inside that build environment, `meson setup _build` and `meson test` build Igloo and run `testsuite/` | flatpak, flatpak-builder and the GNOME 51 SDK |
| `make check` | `verify` | as `verify` |
| `make ci` | `verify`, `corrections-base` (with `BASE`) and `test` | all of the above |

`make test` is a debug build, so assertions and cast checks are on in the
tests. On a developer host, `FLATPAK_BUILDER` is the `org.flatpak.Builder`
Flatpak; CI's container has `flatpak-builder` itself. Install the tools once:

```bash
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub org.gnome.Sdk//51 org.flatpak.Builder
```

### CI

- `.github/workflows/ci.yml` runs the parts of `make ci` as 2 jobs (core
  ADR-0073): `verify` runs `make verify` and `make corrections-base` on
  Ubuntu with Node from `mise.lock`; `test` runs `make test` in the
  `ghcr.io/flathub-infra/flatpak-github-actions:gnome-51` container, which
  has the GNOME 51 SDK and flatpak-builder.
- `.github/workflows/flatpak.yml` builds the manifest with its tests on every
  push and pull request, as the release builds it, and keeps the bundle
  `igloo-flatpak` as an artifact. To try a pull request, unzip that artifact
  and run `flatpak install --user igloo.flatpak`.

### Release

On a `v*` tag, `flatpak.yml` also exports an OCI image with
`flatpak build-bundle --oci`, pushes it to `ghcr.io/frostyard/igloo` (the
version tag and `sha-<commit>`), attests its digest, and then moves `latest`.
The Frostyard remote lists only digests that this workflow attested on a `v*`
tag, so a push to `main` does not reach users until the next tag. With the
secret `FLATPAK_INDEX_DISPATCH_TOKEN`, the workflow also asks
frostyard/flatpak-index to refresh at once instead of at its next hourly run.

To check a published image:

```bash
gh attestation verify oci://ghcr.io/frostyard/igloo:latest --repo frostyard/igloo \
  --signer-workflow github.com/frostyard/igloo/.github/workflows/flatpak.yml
```

## Build by hand

Build and install for your user (on hosts where home is under `/var/home`,
flatpak-builder needs `--filesystem=home` to see the checkout):

```bash
flatpak run --filesystem=home org.flatpak.Builder --user --install --force-clean build-dir io.github.frostyard.Igloo.json
flatpak run io.github.frostyard.Igloo
```

A single-file bundle for another computer:

```bash
flatpak run --filesystem=home org.flatpak.Builder --repo=repo --force-clean build-dir io.github.frostyard.Igloo.json
flatpak build-bundle repo igloo.flatpak io.github.frostyard.Igloo
```

Develop inside the build environment (what `make test` does):

```bash
flatpak run --filesystem=home org.flatpak.Builder --user --force-clean --stop-at=ptyxis build-dir io.github.frostyard.Igloo.json
flatpak build --filesystem=home build-dir sh -c 'meson setup _build --prefix=/app && ninja -C _build && meson test -C _build'
```

The meson option `generic` names the application. It defaults to `igloo`,
which also sets the application and GSettings IDs to
`io.github.frostyard.Igloo`; `ptyxis`, `terminal` and `builder` build
upstream's names.

## Operational notes

- flatpak-builder keeps built dependencies in `.flatpak-builder/`; they
  rebuild only when the manifest changes. `build-dir/`, `_build/`, `repo/`
  and bundles are in `.gitignore`.
- `org.gnome.Ptyxis.Devel.json` and `.gitlab-ci.yml` are upstream's nightly
  build for GNOME's GitLab; nothing here runs them.

## References

- Rationale: [ADR-0002](../adr/0002-adopt-the-repository-baseline.md),
  [ADR-0001](../adr/0001-follow-ptyxis-upstream.md)
- Related: [nsl integration](nsl-integration.md)
