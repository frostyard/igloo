# nsl integration

Living document. Rationale: [ADR-0001](../adr/0001-follow-ptyxis-upstream.md)
(Igloo is Ptyxis with nsl features, under Ptyxis's names).
Contracts: nsl's
[CLI spec, machine-readable output](https://github.com/frostyard/nsl/blob/main/docs/specs/cli.md#machine-readable-output)
([nsl ADR-0021](https://github.com/frostyard/nsl/blob/main/docs/adr/0021-machine-readable-output.md)).

## Overview

Igloo opens [nsl](https://frostyard.github.io/nsl/) machines the way Ptyxis
opens Podman, Toolbox and Distrobox containers, and manages them from a
*Machines* page in Preferences. nsl runs WSL-style Linux machines on atomic
Linux hosts: systemd-nspawn containers inside a systemd-vmspawn VM. Like Ptyxis, Igloo runs `ptyxis-agent` on the
host, outside the Flatpak sandbox, and talks to it over a private D-Bus
connection. nsl always runs on the host, through the agent.

```
Igloo (Flatpak) ──D-Bus──► ptyxis-agent (host) ──► nsl ──► machines
```

## Design

### Agent

Igloo adds to the agent:

- `PtyxisNslProvider` (`agent/ptyxis-nsl-provider.c`) exports a container,
  `nsl:NAME`, for every machine `nsl list --json` reports, except incomplete
  machines and machines being removed. It reads machine names from
  `NSL_HOME/machines` at startup, so restored sessions find their machines
  before `nsl list` finishes. It reloads when nsl changes its records, its
  default machine or a VM's runtime directory, and shortly after a terminal
  starts a machine. It never polls: an `nsl list` against a running VM counts
  as activity and would keep the VM from idling.
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

The agent looks for nsl on `PATH`, then in `~/.local/bin`,
`/home/linuxbrew/.linuxbrew/bin`, `~/.linuxbrew/bin`, `/usr/local/bin` and
`/usr/bin`, because a session started through `flatpak-spawn` often has a
minimal `PATH`.

### Reading nsl's output

`agent/ptyxis-nsl.c` reads the documents `nsl list --json`,
`nsl images --json` and `nsl config --json` print, as nsl's CLI contract
defines them. It ignores fields it does not know. `testsuite/test-nsl.c`
covers real output (fixtures in `testsuite/nsl/`). The *Resources* rows take
their ranges from `config --json`. nsl 0.8.0 introduced `--json`; with an
older nsl, machines still open from the New Tab menu, but the Machines page
asks the user to update.

### Window side

- **New Tab menu.** The dropdown next to the new tab button lists nsl
  machines in a *Machines* section and ends with *Manage Machines…*.
- **Profiles and sessions.** A profile's *Default Container* can be an nsl
  machine, and pinned or restored tabs reopen in their machine. New tabs keep
  the current machine as they keep the current container.
- **Main menu → Machines** opens the *Machines* page in Preferences
  (`src/ptyxis-machines-page.c`):
  - every machine with its distribution, tier, state and default marker;
  - *New Machine* (`src/ptyxis-machine-create-dialog.c`) asks for a name, a
    distribution from the signed nsl catalogue, isolation, default and an
    optional username, then runs `nsl create` in a terminal so downloads and
    verification are visible, and offers to open the new machine;
  - *Import Machine…* (`src/ptyxis-machine-import-dialog.c`) asks for an
    archive, then a name and isolation, then runs `nsl import` in a terminal
    and offers to open the machine;
  - per machine: *Open Terminal*, *Start*/*Stop*, *Make Default*,
    *Edit Profile…* (creates or opens a profile whose default container is
    the machine, for its own palette, font and command), *Export…* (asks for
    a file, NAME-DATE.tar by default, then runs `nsl export NAME FILE` in a
    terminal; a running machine is stopped first, after a confirmation; nsl
    never replaces a file), *Show Logs* (`nsl logs NAME`), *Recover…* (for
    an isolated machine only: `nsl recover NAME`, after a confirmation) and
    *Remove…* (`nsl stop` then
    `nsl remove --yes`, after a confirmation; a machine in the state
    `removing` or `incomplete` runs only `nsl remove --yes`, which resumes
    its unfinished removal);
  - *Resources*: shared VM memory and processors, autostart, idle timeout and
    isolated VM resources, written to `nsl.conf`;
  - *Host*: `nsl doctor`, `nsl update`, `nsl logs`, `nsl recover` (after a
    confirmation that says it restarts the shared VM from a fresh root and
    keeps machines) and `nsl shutdown`. `nsl recover` always starts the VM
    again, also a VM that was stopped; both confirmations say so.

  Long commands run in a terminal (`src/ptyxis-machine-command-dialog.c`).
  Logs and recovery commands run as argument arrays, not through a shell.
- In the Flatpak, a file outside the folders Igloo can see comes from the
  document portal, and Igloo gives nsl its real path on the host.

### Working directories

The directory a new tab starts in follows nsl's rules
(`agent/ptyxis-nsl.c` maps paths, `agent/ptyxis-nsl-container.c` applies
them):

| Requested directory | Shared machine | Isolated machine |
| --- | --- | --- |
| none (a fresh window) | guest home | guest home |
| `/mnt/host/…` (from another machine tab) | kept | guest home |
| a host directory in your home, `/run/media/USER` or `/mnt` | `/mnt/host/…`, matched by device and inode like nsl | tried as a machine path |
| anything else | tried as a machine path, else the guest home | same |

A new tab receives the previous tab's directory in host terms, so a host tab
opened from a machine tab at `/mnt/host/var/home/you/src` starts in
`/var/home/you/src`. Links clicked in a machine tab that point below
`/mnt/host` open the host file they name.

The nsl machine images report the shell's directory with OSC 7 through
`/etc/profile.d/nsl-osc7.sh`, so a new tab opened from a machine tab starts in
its current directory.

## Operational notes

- Machine states on the Machines page refresh when the page is shown, after
  an action and after starting a terminal, not continuously (the no-polling
  rule above).
- The page's *Check Again* button looks for nsl again after it is installed.
- Removing Igloo does not remove nsl or any machine.

## References

- Rationale: [ADR-0001](../adr/0001-follow-ptyxis-upstream.md)
- Contracts: nsl
  [CLI spec](https://github.com/frostyard/nsl/blob/main/docs/specs/cli.md#machine-readable-output)
- Build: [build and release](build-and-release.md)
