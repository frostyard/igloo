# 0001 — Follow Ptyxis upstream and rename only what users see

- **Status:** Accepted
- **Date:** 2026-10-05

## Context

Igloo is [Ptyxis](https://gitlab.gnome.org/GNOME/ptyxis) with nsl machines
added (commits `62f122f` and `1e87a19`). Ptyxis is the default terminal in
Fedora and Ubuntu, so a fork that kept its name and icon would confuse users
and send reports about nsl features to upstream. Ptyxis keeps changing, and
Igloo wants those changes: a fork that renames its files and symbols turns
every upstream change into a conflict.

## Decision

- Igloo follows Ptyxis upstream and merges its changes.
- Only what users see is renamed: the application name, icons, the
  application and GSettings IDs (`io.github.frostyard.Igloo`), the desktop
  file, metainfo, the About dialog and the Flatpak manifest
  (`io.github.frostyard.Igloo.json`). The meson option `generic` selects
  them; its default is `igloo`, and the upstream choices build as before.
- Source files, code symbols, `ptyxis-agent` and the internal D-Bus names
  keep Ptyxis's names.
- Upstream's files stay where upstream has them (`README.ptyxis.md` holds
  upstream's README); the license stays GPL-3.0-or-later.

## Consequences

- Upstream changes merge with conflicts only where Igloo changed the same
  lines.
- A reader sees `ptyxis` in file and symbol names; `AGENTS.md` and
  `README.md` say why.
- Problems with Igloo's nsl features go to frostyard/igloo, not to Ptyxis.

## Alternatives considered

- **Keep the Ptyxis name and icon:** confuses users of the distribution's
  Ptyxis and misdirects bug reports. Rejected.
- **Rename files and symbols too:** every upstream merge conflicts. Rejected.

## References

- Shapes: [design: nsl integration](../design/nsl-integration.md),
  [design: build and release](../design/build-and-release.md)
- Commit: `24213bb` (Rename the fork to Igloo)
- Amended by: [ADR-0003](0003-rename-the-application-id.md) (the
  application ID is now `org.frostyard.Igloo`)
