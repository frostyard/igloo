# 0003 — Rename the application ID to org.frostyard.Igloo

- **Status:** Accepted
- **Date:** 2026-11-06

## Context

[core ADR-0016](https://github.com/frostyard/core/blob/main/docs/adr/0016-reverse-dns-org-frostyard-identifiers.md)
names every Frostyard application `org.frostyard.<AppName>`: the GObject
application ID, the desktop file, the icon, and the vendor. Igloo shipped as
`io.github.frostyard.Igloo` ([ADR-0001](0001-follow-ptyxis-upstream.md)), and
the Frostyard Flatpak remote serves that ID to installed copies.

The ID is also the Flatpak ref, the folder `~/.var/app/<ID>`, the GSettings
schema ID and path, the D-Bus name of the application, and the name of 2
folders and 1 desktop-file entry that Igloo writes:
`$XDG_CONFIG_HOME/<ID>/session.gvariant`, `$XDG_DATA_HOME/<ID>/palettes/`,
and the `<ID>.desktop:new-window` line of `xdg-terminals.list`. Inside the
Flatpak, GLib keeps settings in `$XDG_CONFIG_HOME/glib-2.0/settings/keyfile`,
with 1 group for each settings path.

Flatpak renames an application with an end-of-life rebase: the old ref
carries the commit metadata `ostree.endoflife-rebase`, `flatpak update`
installs the new ref and removes the old one, and the first run of the new
ID moves `~/.var/app/<old ID>` to `~/.var/app/<new ID>`. An OCI remote reads
that metadata from the image label
`org.flatpak.commit-metadata.ostree.endoflife-rebase`.

## Decision

- The application ID, GSettings schema ID and D-Bus name are
  `org.frostyard.Igloo`; the settings path is `/org/frostyard/Igloo/`; the
  manifest is `org.frostyard.Igloo.json`; the AppStream developer ID is
  `org.frostyard`. Development builds add `.Devel` as before.
- Each `v*` tag publishes the old ID once more as the image tag
  `io.github.frostyard.Igloo` of `ghcr.io/frostyard/igloo`: a copy of the
  new build under the old ref, marked end-of-life with a rebase to
  `org.frostyard.Igloo` (`flatpak build-commit-from --end-of-life-rebase`).
  It is pushed and attested after `latest` moves to the new ID, so an
  index built at any moment never offers a rebase to an ID that it does
  not list; as with `latest`, the tag that the index reads moves only
  after the digest is attested.
- The desktop file names the old one in `X-Flatpak-RenamedFrom`, and the
  AppStream data names the old ID in `<replaces>`.
- At startup, before it reads settings or files, Igloo moves what still
  carries the old ID (`src/ptyxis-igloo-migration.c`): the 2 folders under
  `$XDG_CONFIG_HOME` and `$XDG_DATA_HOME`, the settings groups under
  `io/github/frostyard/Igloo` in the keyfile, and the `xdg-terminals.list`
  line. Each step runs only when the old item exists and the new one does
  not, so it runs once and never overwrites newer data. It holds an
  exclusive lock (`flock` on `$XDG_CONFIG_HOME/.igloo-migration.lock`)
  while it reads and writes, so 2 processes that start at once cannot
  write a stale copy of the settings.

## Consequences

- An installed copy moves to the new ID with `flatpak update` or GNOME
  Software, keeps its settings, profiles, saved session and palettes, and
  only 1 Igloo stays installed.
- frostyard/flatpak-index must list the image tag
  `io.github.frostyard.Igloo` next to `latest`; without it, installed copies
  of the old ID get no more updates.
- The end-of-life image and the migration stay until no installed copy of
  the old ID is left; removing them is a new decision.
- A copy of the new ID that runs before the rebase (a pull request bundle)
  creates `~/.var/app/org.frostyard.Igloo` first. Flatpak then does not
  move the old folder, and the old settings stay in
  `~/.var/app/io.github.frostyard.Igloo`.
- A build outside Flatpak keeps settings in dconf; the migration does not
  move dconf paths. Igloo ships only as a Flatpak.

## Alternatives considered

- **Keep `io.github.frostyard.Igloo` and record an exception in core:**
  leaves Igloo the only Frostyard app outside ADR-0016. Rejected.
- **A new ID with no rebase:** users would have to find and install the new
  app, and their settings would stay behind in the old one. Rejected.
- **Re-publish the last old build as the end-of-life image:** needs the old
  image in the release job; the new build under the old ref carries the same
  mark, and `flatpak update` installs the new ref, not the old commit.
  Rejected.

## References

- Builds on: [ADR-0001](0001-follow-ptyxis-upstream.md),
  [ADR-0002](0002-adopt-the-repository-baseline.md)
- Shapes: [design: build and release](../design/build-and-release.md),
  [`src/ptyxis-igloo-migration.c`](../../src/ptyxis-igloo-migration.c),
  [`.github/workflows/flatpak.yml`](../../.github/workflows/flatpak.yml),
  [org-wide decisions](../org-adrs.md)
