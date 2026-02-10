# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Development Commands

```bash
make build          # Build binary with version injection
make test           # Run all tests (go test -v ./...)
make fmt            # Format code (go fmt ./...)
make lint           # Run golangci-lint
make prep           # Full pipeline: deps → fmt → lint → test → build
make install        # Build and install to /usr/local/bin
make bump           # Tag new version with svu and push tag
```

Run a single test: `go test -v -run TestName ./internal/path/...`

Always run `make lint` before considering a task complete. Fix any linting issues.

## Architecture

Igloo is a CLI tool that creates Incus containers as isolated development environments for GTK apps, with automatic display passthrough.

**Two commands:** `igloo` (enter/create container) and `igloo destroy` (remove container).

### Package Layout

- `cmd/` — Cobra CLI commands. `enter.go` is the default command, `destroy.go` removes containers. `root.go` wires commands together with Fang for config binding.
- `internal/incus/` — Incus container management. Wraps the `incus` CLI (not the SDK). `client.go` handles lifecycle (create, start, delete), device management (disk, GPU, proxy), and interactive shell exec. `cloudinit.go` generates cloud-init YAML via `text/template` to provision the container user with matching UID/GID.
- `internal/host/` — Parses `/etc/os-release` to detect the host distro and map it to an Incus cloud image (`images:{distro}/{version}/cloud`). Falls back through ID → ID_LIKE → debian/trixie default.
- `internal/display/` — Detects X11/Wayland and configures display passthrough using Incus proxy devices and environment variable injection. Handles Xauthority for X11 and XWayland-under-Wayland.
- `internal/ui/` — Terminal styling with charmbracelet/lipgloss. Provides Success/Info/Warning/Error output helpers.

### Enter Flow (the main operation)

1. Derive container name from current directory (`igloo-<dirname>`)
2. If container doesn't exist: detect host OS → generate cloud-init → create instance → mount project dir → start → wait for cloud-init → copy dotfiles → configure display passthrough → run `.igloo.sh` if present
3. If container exists but stopped: start it
4. Update Xauthority on every entry, then exec interactive shell

### Key Design Decisions

- Incus is driven via CLI subprocess calls, not the Go SDK — keeps the binary small and avoids CGO
- Cloud-init handles in-container user setup so the container starts with a matching user, sudo access, and proper runtime dirs
- Display passthrough uses Incus proxy devices for Unix sockets + env var injection for DISPLAY/WAYLAND_DISPLAY
- Project directory is mounted at the same absolute path inside the container
