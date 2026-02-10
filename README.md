# Igloo

**Isolated development containers for GTK apps**

Igloo creates Linux development containers using [Incus](https://linuxcontainers.org/incus/) with automatic display passthrough. Two commands, zero configuration.

## Quick Start

```bash
cd ~/projects/my-gtk-app
igloo                      # creates and enters the container
```

That's it. Igloo auto-detects your host OS, creates a matching container, mounts your project directory, copies your dotfiles, and sets up display passthrough with GPU support.

When you're done with the container:

```bash
igloo destroy
```

## Setup Script

Optionally create a `.igloo.sh` in your project root to install dependencies on first creation:

```bash
#!/bin/bash
apt-get update
apt-get install -y build-essential libgtk-4-dev meson ninja-build
```

To reprovision after changing `.igloo.sh`, run `igloo destroy` then `igloo`.

## Commands

| Command | Description |
| --- | --- |
| `igloo` | Enter the container (creates it if needed) |
| `igloo destroy` | Remove the container completely |
| `igloo --no-gui` | Enter without display/GPU passthrough |

## What Happens on First Run

1. Detects your host distro from `/etc/os-release` (follows `ID_LIKE` for derivatives)
2. Creates an Incus container with matching image
3. Maps your user (same UID/GID, sudo access)
4. Mounts the project directory at the same absolute path
5. Copies dotfiles (`.gitconfig`, `.ssh/`, `.bashrc`, `.profile`, `.bash_profile`)
6. Sets up X11/Wayland display passthrough with GPU
7. Runs `.igloo.sh` if it exists
8. Drops you into a shell

On subsequent runs, it just enters the existing container (starting it if stopped) and refreshes display passthrough.

## Installation

### Prerequisites

- [Incus](https://linuxcontainers.org/incus/docs/main/installing/) installed and configured
- Your user added to the `incus` group

### From Source

```bash
git clone https://github.com/frostyard/igloo.git
cd igloo
make build
sudo cp igloo /usr/local/bin/
```

## Tips

- Each project directory gets its own container (`igloo-<dirname>`)
- GUI apps just work inside the container (GTK, Qt, Firefox, VS Code, etc.)
- The `--no-gui` flag is useful when SSHed into the machine or running headless builds
- Multiple projects can each have their own igloo, completely isolated

## Credits

- **[Distrobox](https://github.com/89luca89/distrobox)** -- The original inspiration
- **[Blincus](https://blincus.dev)** -- Blincus is igloo version 0

## License

MIT
