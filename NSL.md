# Ptyxis with nsl machines

This fork of [Ptyxis](https://gitlab.gnome.org/chergert/ptyxis) integrates
[nsl](https://frostyard.github.io/nsl/) machines the way Ptyxis integrates
Podman, Toolbox and Distrobox containers. nsl runs WSL-style Linux machines on
atomic Linux hosts: systemd-nspawn containers inside a systemd-vmspawn VM.

It installs as `io.github.frostyard.Ptyxis`, so it sits beside upstream
Ptyxis (`org.gnome.Ptyxis` or `app.devsuite.Ptyxis`) and keeps its own
settings.

## What it adds

- **New Tab menu.** The dropdown next to the new tab button lists nsl machines
  in a *Machines* section. Picking one opens a tab in that machine. The menu
  ends with *Manage Machines…*.
- **Profiles and sessions.** A profile's *Default Container* can be an nsl
  machine, and pinned or restored tabs reopen in their machine. New tabs keep
  the current machine as they keep the current container.
- **Main menu → Machines** opens the *Machines* page in Preferences:
  - every machine with its distribution, tier, state and default marker;
  - *New Machine* asks for a name, a distribution from the signed nsl
    catalogue, isolation, default and an optional username, then runs
    `nsl create` in a terminal so downloads and verification are visible, and
    offers to open the new machine;
  - per machine: *Open Terminal*, *Start*/*Stop*, *Make Default*,
    *Edit Profile…* (creates or opens a profile whose default container is the
    machine, for its own palette, font and command) and *Remove…*
    (`nsl stop` then `nsl remove --yes`, after a confirmation);
  - *Resources*: shared VM memory and processors, autostart, idle timeout and
    isolated VM resources, written to `nsl.conf`;
  - *Host*: `nsl doctor`, `nsl update` and `nsl shutdown`.

If nsl is not installed, the page links to its installation guide.

## Build and install the Flatpak

You need the GNOME 51 SDK and flatpak-builder:

```bash
flatpak install --user flathub org.gnome.Sdk//51 org.flatpak.Builder
```

Build and install for your user (on hosts where home is under `/var/home`,
flatpak-builder needs `--filesystem=home` to see the checkout):

```bash
flatpak run --filesystem=home org.flatpak.Builder --user --install --force-clean build-dir io.github.frostyard.Ptyxis.json
```

```bash
flatpak run io.github.frostyard.Ptyxis
```

To produce a single-file bundle for another computer:

```bash
flatpak run --filesystem=home org.flatpak.Builder --repo=repo --force-clean build-dir io.github.frostyard.Ptyxis.json
```

```bash
flatpak build-bundle repo ptyxis-nsl.flatpak io.github.frostyard.Ptyxis
```

The manifest builds VTE 0.84.1 with fast_float and simdutf on top of the GNOME
51 runtime, then builds this checkout.

### Develop inside the build environment

Build the dependencies once, then use meson inside the SDK:

```bash
flatpak run --filesystem=home org.flatpak.Builder --user --force-clean --stop-at=ptyxis build-dir io.github.frostyard.Ptyxis.json
```

```bash
flatpak build --filesystem=home build-dir sh -c 'meson setup _build --prefix=/app -Dapp-id=io.github.frostyard.Ptyxis -Dgschema-id=io.github.frostyard.Ptyxis -Dgschema-path=/io/github/frostyard/Ptyxis/ && ninja -C _build && meson test -C _build'
```

## Requirements

nsl must be installed on the host; the Flatpak does not bundle it. The agent
looks for it on `PATH`, then in `~/.local/bin`, `/home/linuxbrew/.linuxbrew/bin`,
`~/.linuxbrew/bin`, `/usr/local/bin` and `/usr/bin`, because a session started
through `flatpak-spawn` often has a minimal `PATH`. The page's *Check Again*
button looks again after you install it.

## How it works

Ptyxis runs `ptyxis-agent` on the host, outside the Flatpak sandbox, and talks
to it over a private D-Bus connection. This fork adds to the agent:

- `PtyxisNslProvider` (`agent/ptyxis-nsl-provider.c`) exports a container,
  `nsl:NAME`, for every machine `nsl list` reports, except incomplete machines
  and machines being removed. It reads machine names from `NSL_HOME/machines`
  at startup, so restored sessions find their machines before `nsl list`
  finishes. It reloads when nsl changes its records, its default machine or a
  VM's runtime directory, and shortly after a terminal starts a machine. It
  never polls: an `nsl list` against a running VM counts as activity and would
  keep the VM from idling.
- `PtyxisNslContainer` (`agent/ptyxis-nsl-container.c`) spawns terminals with
  `nsl run -m NAME --cd / -- env VARS… /bin/sh -c SCRIPT DIR ARGV…`. nsl only
  forwards terminal and locale variables, so `env` carries the rest
  (`VTE_VERSION`, `PTYXIS_PROFILE`, proxies). The script changes to the
  requested directory, or to the guest home, and starts the account's login
  shell when the requested program is missing in the machine.
- `org.gnome.Ptyxis.Machines` (`agent/ptyxis-machines-impl.c`, interface in
  `agent/org.gnome.Ptyxis.Agent.xml`) lists machines and images, reads and
  writes `nsl.conf`, and runs short nsl commands for the Preferences page.
  Writes keep the file's comments and are checked with `nsl config`; a file
  nsl rejects is restored.

The directory a new tab starts in follows nsl's rules:

| Requested directory | Shared machine | Isolated machine |
| --- | --- | --- |
| none (a fresh window) | guest home | guest home |
| `/mnt/host/…` (from another machine tab) | kept | guest home |
| a host directory in your home, `/run/media/USER` or `/mnt` | `/mnt/host/…`, matched by device and inode like nsl | tried as a machine path |
| anything else | tried as a machine path, else the guest home | same |

Links clicked in a machine tab that point below `/mnt/host` open the host
file they name.

nsl prints tables for people rather than JSON, so `agent/ptyxis-nsl.c` splits
`nsl list`, `nsl images` and `nsl config` at the column offsets of each header
(`testsuite/test-nsl.c` covers real output). A `--json` option in nsl would
make this contract explicit.

## Known limitations

- A tab only learns its machine's working directory when the guest shell
  reports it with OSC 7, as `vte.sh` does. The nsl Debian image ships no such
  script, so a new tab opened from a machine tab starts in the guest home
  instead of the current directory. Installing `vte.sh` (Debian:
  `libvte-2.91-common`) in a machine, or adding it to nsl's machine images,
  fixes this.
- Machine states on the Machines page refresh when the page is shown, after an
  action and after starting a terminal, not continuously.
- Export and import are not in the interface yet; use `nsl export` and
  `nsl import`.
