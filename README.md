# 🏔️ Igloo

**Build cozy development environments in seconds** ❄️

Igloo is a CLI tool that creates isolated Linux development containers using [Incus](https://linuxcontainers.org/incus/). Think of it as your personal igloo in the frozen tundra of system configuration chaos—warm, safe, and exactly how you like it.

## ✨ Features

- 🐧 **Multi-distro support** — Ubuntu, Debian, Fedora, or Arch Linux
- 🏠 **Home away from home** — Your home directory and project files are automatically mounted
- 🖥️ **GUI apps just work** — Wayland and X11 passthrough with optional GPU acceleration
- 👤 **Seamless user mapping** — Same UID/GID as your host, no permission headaches
- 📜 **Custom init scripts** — Automate your environment setup
- ⚡ **Fast iteration** — Destroy and rebuild in seconds

## 🚀 Quick Start

```bash
# Initialize a new igloo in your project directory
cd ~/projects/my-awesome-app
igloo init

# Enter your cozy development environment
igloo enter

# When you're done for the day
igloo stop

# Start fresh? No problem!
igloo destroy
igloo init
```

## 📦 Installation

### From Source

```bash
git clone https://github.com/frostyard/igloo.git
cd igloo
make build
sudo cp igloo /usr/local/bin/
```

### Prerequisites

- [Incus](https://linuxcontainers.org/incus/docs/main/installing/) installed and configured
- Your user added to the `incus` group

## 🎛️ Commands

| Command         | Description                        |
| --------------- | ---------------------------------- |
| `igloo init`    | Create a new igloo environment     |
| `igloo enter`   | Enter the igloo (starts if needed) |
| `igloo stop`    | Stop the running igloo             |
| `igloo status`  | Show environment status            |
| `igloo remove`  | Remove container, keep config      |
| `igloo destroy` | Remove everything                  |

## ⚙️ Configuration

Running `igloo init` creates a `.igloo/` directory with your configuration:

```
.igloo/
├── igloo.ini          # Main configuration
└── scripts/           # Init scripts (run during provisioning)
    └── 00-example.sh.example
```

### igloo.ini

```ini
[container]
image = images:debian/trixie/cloud
name  = igloo-myproject

[packages]
install = git, vim, curl

[mounts]
home    = true
project = true

[display]
enabled = true
gpu     = true
```

### Init Scripts 📜

Drop shell scripts in `.igloo/scripts/` to customize your environment:

```bash
# .igloo/scripts/01-install-tools.sh
#!/bin/bash
apt-get install -y nodejs npm
npm install -g yarn
```

Scripts run in lexicographical order, so use numbered prefixes like `01-`, `02-`, etc.

## 🎨 Flags & Options

### igloo init

```bash
igloo init --distro ubuntu --release noble    # Use Ubuntu Noble
igloo init --distro fedora --release 43       # Use Fedora 43
igloo init --name my-dev-box                  # Custom container name
igloo init --packages "go,nodejs,python3"     # Pre-install packages
```

### igloo destroy

```bash
igloo destroy              # Remove container and .igloo directory
igloo destroy --keep-config  # Keep .igloo directory for later
igloo destroy --force      # Force remove without stopping
```

## 🗂️ Directory Layout (Inside the Container)

```
/home/youruser/
├── host/              # Your host home directory
└── workspace/
    └── myproject/     # Your project directory (where you ran igloo init)
```

## 💡 Tips & Tricks

### Run GUI Apps

```bash
igloo enter
code .                 # VS Code just works!
firefox               # Browse the web
```

### Use Your Host's Git Config

Your home directory is mounted, so `~/.gitconfig` is already available!

### Quick Rebuild

```bash
igloo destroy && igloo init   # Fresh start in ~30 seconds
```

### Multiple Projects, Multiple Igloos

Each project directory can have its own igloo. They're completely isolated!

## 🤝 Contributing

Contributions are welcome! Feel free to open issues and pull requests.

## 📄 License

MIT License — build all the igloos you want! 🏔️

---

<p align="center">
  <i>Stay frosty, friends!</i> ❄️🐧
</p>
