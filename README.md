<!-- readme-kind: app -->
<img src="data/icons/igloo.svg" alt="Igloo icon" width="112" align="right">

# Igloo

[![Flatpak](https://github.com/frostyard/igloo/actions/workflows/flatpak.yml/badge.svg)](https://github.com/frostyard/igloo/actions/workflows/flatpak.yml)

**A terminal for nsl machines and containers.**

Igloo is a GNOME terminal for people who work in
[nsl](https://frostyard.github.io/nsl/) machines: it opens them the way it
opens Podman, Toolbox and Distrobox containers. It is based on
[Ptyxis](https://gitlab.gnome.org/GNOME/ptyxis), keeps everything Ptyxis does,
and installs as `io.github.frostyard.Igloo`, beside Ptyxis, with its own
settings.

**Status:** pre-release. It follows Ptyxis 50; the nsl features need nsl 0.8.0 or later.

**Documentation:** [docs/README.md](docs/README.md)

## What it does

- **Opens machines from the New Tab menu.** The dropdown next to the new tab
  button lists nsl machines in a *Machines* section; a pick opens a tab in
  that machine. [Design](docs/design/nsl-integration.md#window-side)
- **Keeps machines in profiles and sessions.** A profile's *Default
  Container* can be an nsl machine, and pinned or restored tabs reopen in
  their machine.
- **Manages machines.** *Main menu → Machines* creates, imports, starts,
  stops, exports and removes machines, and edits a profile for each.
  Long nsl commands run in a terminal, so downloads and verification are
  visible.
- **Sets nsl's resources.** Shared VM memory and processors, autostart and
  the idle timeout, written to `nsl.conf`; and `nsl doctor`, `nsl update`
  and `nsl shutdown`.
- **Follows your directory.** A new tab starts where the last one was, in
  host or machine terms, as nsl maps them.
  [Working directories](docs/design/nsl-integration.md#working-directories)

## Requirements

nsl 0.8.0 or later must be installed on the host; the Flatpak does not
bundle it. With an older nsl, machines still open from the New Tab menu, but
the Machines page asks you to update. If nsl is missing, the page links to
its installation guide.

## Install

Igloo is on the [Frostyard Flatpak remote](https://github.com/frostyard/flatpak-index).
Add it and Flathub (for the GNOME 51 runtime) once, for your user, then
install Igloo and update it with `flatpak update`. A `--user` install gets a
missing runtime only from a user remote, so Flathub must be a user remote
too:

```sh
flatpak remote-add --user --if-not-exists frostyard https://frostyard.github.io/flatpak-index/frostyard.flatpakrepo
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user frostyard io.github.frostyard.Igloo
```

Every pull request's [Flatpak run](https://github.com/frostyard/igloo/actions/workflows/flatpak.yml)
keeps a bundle, `igloo-flatpak`, to try a change before it merges. To build
it yourself, see [build and release](docs/design/build-and-release.md).

## Remove

```sh
flatpak uninstall --user io.github.frostyard.Igloo
```

This keeps Igloo's settings and saved sessions; add `--delete-data` to remove
`~/.var/app/io.github.frostyard.Igloo` too. Removing Igloo does not remove
nsl or any machine (use `nsl remove` for machines). If no other app needs the
Frostyard remote, remove it with `flatpak remote-delete --user frostyard`.

## How it works

Like Ptyxis, Igloo runs `ptyxis-agent` on the host, outside the Flatpak
sandbox. Igloo adds nsl to the agent: it lists machines with
`nsl list --json`, opens terminals with `nsl run`, and runs the Machines
page's nsl commands. It never polls nsl, so idle VMs can stop. Source files
and symbols keep Ptyxis's names so that upstream changes merge cleanly.
[nsl integration](docs/design/nsl-integration.md) has the details, and
[README.ptyxis.md](README.ptyxis.md) describes everything Igloo inherits.
Report problems with Igloo's nsl features
[here](https://github.com/frostyard/igloo/issues), not to Ptyxis.

## Limits

- Machine states on the Machines page refresh when the page is shown, after
  an action and after starting a terminal, not continuously.

## Credits

Igloo is [Ptyxis](https://gitlab.gnome.org/GNOME/ptyxis) by Christian Hergert
and its contributors; its icons are by Jakub Steiner. The Igloo icon and the
nsl integration are by Frostyard.

## Develop

```sh
mise install   # Node for make verify
make verify    # agent surfaces, docs index, risk paths, corrections
make test      # build in the GNOME 51 SDK and run the meson tests
make ci        # what CI runs
```

[Build and release](docs/design/build-and-release.md) covers the GNOME SDK,
flatpak-builder and the release.

## Where to go next

- [AGENTS.md](AGENTS.md): the rules for agents and contributors.
- [docs/README.md](docs/README.md): the decisions, designs, specs and plans.
- [SECURITY.md](SECURITY.md): report a vulnerability privately.
- [LICENSE](LICENSE): GNU General Public License, version 3 or later, like Ptyxis.
