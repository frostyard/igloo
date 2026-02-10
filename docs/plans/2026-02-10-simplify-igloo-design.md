# Simplify Igloo: GTK Dev Container Tool

## Goal

Strip igloo down from a general-purpose container tool to a focused, opinionated GTK development container tool. Two commands, no config file, half the code.

## Commands

### `igloo`

The only command used day-to-day. No subcommand needed.

1. Derives container name from current directory (e.g. `igloo-my-gtk-app`)
2. If container doesn't exist:
   - Detects host distro from `/etc/os-release` (`ID` first, then `ID_LIKE` fallback chain) to find an Incus-supported image
   - Creates container with cloud-init for UID/GID mapping
   - Mounts current project directory at the same absolute path inside the container
   - Copies dotfiles (`.gitconfig`, `.ssh/`, `.bashrc`, `.profile`, `.bash_profile`) from host home into container home
   - Sets up display passthrough (X11 or Wayland, auto-detected) with GPU
   - Runs `.igloo.sh` from project root inside the container if it exists
   - Stores provisioned marker in container metadata
3. If container exists but stopped, starts it
4. Refreshes display passthrough (every entry, handles display server restarts)
5. Drops into interactive shell

**Flag:** `--no-gui` skips display/GPU setup for headless use.

### `igloo destroy`

Removes the container completely. That's it.

## Project Setup

No `init` command. Optionally create one file:

**`.igloo.sh`** -- A plain shell script that runs inside the container on first creation.

```sh
#!/bin/bash
apt-get update
apt-get install -y build-essential libgtk-4-dev meson ninja-build
```

If the file doesn't exist, igloo creates a bare container. To reprovision after changing `.igloo.sh`, run `igloo destroy` then `igloo`.

## Distro Detection

Read `/etc/os-release`. Use `ID` and `VERSION_ID` to look up an Incus image. If the `ID` doesn't match a known image, walk the `ID_LIKE` chain (space-separated list) until a match is found. This handles Debian derivatives (Pop!_OS -> ubuntu -> debian).

## Display Passthrough

Always on unless `--no-gui` is passed. On every entry:

- Detect display server via `WAYLAND_DISPLAY` and `DISPLAY` env vars
- **Wayland:** Proxy the Wayland socket into the container, set `WAYLAND_DISPLAY`
- **X11:** Proxy X11 socket, handle Xauthority
- **GPU:** Add host GPU as container device for hardware-accelerated rendering

Refreshing on every entry (not just creation) fixes display server restart issues.

## File Access

- **Project directory:** Mounted at the same absolute path inside the container
- **Dotfiles:** Copied (not symlinked) on creation: `.gitconfig`, `.ssh/`, `.bashrc`, `.profile`, `.bash_profile`
- **Home directory:** Not mounted. Container has its own isolated home.

## Codebase Structure

```
igloo/
├── main.go                    # Entry point, version info
├── cmd/
│   ├── root.go                # Root command, registers enter + destroy
│   ├── enter.go               # Default command: create/enter container
│   └── destroy.go             # Remove container completely
├── internal/
│   ├── host/
│   │   └── detect.go          # /etc/os-release parsing, ID_LIKE fallback
│   ├── incus/
│   │   ├── client.go          # Incus operations (create, start, exec, delete)
│   │   └── cloudinit.go       # Cloud-init for user mapping
│   ├── display/
│   │   ├── detect.go          # X11 vs Wayland detection
│   │   └── passthrough.go     # Socket proxying, Xauth, GPU device
│   └── ui/
│       └── styles.go          # Terminal styling
├── go.mod
├── Makefile
└── .goreleaser.yaml
```

## What Gets Removed

**Commands eliminated:** `init`, `provision`, `status`, `stop`, `remove` (5 of 7)

**Packages removed or replaced:**
- `internal/config/` (5 files: config.go, hash.go, distros.go, hostdetect.go, tests) replaced by `internal/host/detect.go`
- `internal/script/` removed; `.igloo.sh` execution lives in `enter.go`

**Config files eliminated:**
- `.igloo/igloo.ini`
- `.igloo/scripts/` directory

**Dependencies droppable:**
- `gopkg.in/ini.v1` (no more INI parsing)

**Estimated reduction:** ~3,300 LOC to ~1,500 LOC.
