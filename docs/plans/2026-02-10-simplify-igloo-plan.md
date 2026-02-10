# Simplify Igloo Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Strip igloo to a two-command GTK dev container tool with no config file, auto-detected host distro with ID_LIKE fallback, and always-on display passthrough.

**Architecture:** Replace the config-driven approach (INI file, multi-distro validation, config hashing) with convention-based defaults. All intelligence moves into a single `enter` code path. Host distro detection uses `/etc/os-release` with `ID_LIKE` fallback for derivatives. Display passthrough refreshes on every entry.

**Tech Stack:** Go, cobra, incus CLI, charmbracelet/lipgloss for terminal styling. Drops gopkg.in/ini.v1.

---

### Task 1: Create internal/host package with OS detection

The new `internal/host` package replaces `internal/config/hostdetect.go` and `internal/config/distros.go`. It reads `/etc/os-release`, tries `ID` first, then walks the `ID_LIKE` chain to find a distro that Incus has a cloud image for. No hardcoded release lists — just known distro families.

**Files:**
- Create: `internal/host/detect.go`
- Create: `internal/host/detect_test.go`

**Step 1: Write the failing test**

Create `internal/host/detect_test.go`:

```go
package host

import (
	"os"
	"path/filepath"
	"testing"
)

func TestDetectOS(t *testing.T) {
	tests := []struct {
		name      string
		content   string
		wantID    string
		wantVer   string
		wantImage string
	}{
		{
			name: "debian trixie",
			content: `ID=debian
VERSION_CODENAME=trixie
`,
			wantID:    "debian",
			wantVer:   "trixie",
			wantImage: "images:debian/trixie/cloud",
		},
		{
			name: "ubuntu noble",
			content: `ID=ubuntu
VERSION_CODENAME=noble
`,
			wantID:    "ubuntu",
			wantVer:   "noble",
			wantImage: "images:ubuntu/noble/cloud",
		},
		{
			name: "pop os falls back via ID_LIKE to ubuntu",
			content: `ID=pop
VERSION_CODENAME=noble
ID_LIKE="ubuntu debian"
`,
			wantID:    "ubuntu",
			wantVer:   "noble",
			wantImage: "images:ubuntu/noble/cloud",
		},
		{
			name: "linuxmint falls back via ID_LIKE to ubuntu",
			content: `ID=linuxmint
VERSION_CODENAME=wilma
ID_LIKE="ubuntu debian"
UBUNTU_CODENAME=noble
`,
			wantID:    "ubuntu",
			wantVer:   "noble",
			wantImage: "images:ubuntu/noble/cloud",
		},
		{
			name: "unknown distro with debian ID_LIKE",
			content: `ID=mxlinux
VERSION_CODENAME=libretto
ID_LIKE="debian"
`,
			wantID:    "debian",
			wantVer:   "libretto",
			wantImage: "images:debian/libretto/cloud",
		},
		{
			name: "completely unknown falls back to debian trixie",
			content: `ID=gentoo
`,
			wantID:    "debian",
			wantVer:   "trixie",
			wantImage: "images:debian/trixie/cloud",
		},
		{
			name: "missing file falls back to debian trixie",
			content: "",
			wantID:    "debian",
			wantVer:   "trixie",
			wantImage: "images:debian/trixie/cloud",
		},
		{
			name: "fedora uses VERSION_ID",
			content: `ID=fedora
VERSION_ID=43
`,
			wantID:    "fedora",
			wantVer:   "43",
			wantImage: "images:fedora/43/cloud",
		},
		{
			name: "arch uses current",
			content: `ID=archlinux
`,
			wantID:    "archlinux",
			wantVer:   "current",
			wantImage: "images:archlinux/current/cloud",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			var path string
			if tt.content == "" {
				path = "/nonexistent/os-release"
			} else {
				tmpDir := t.TempDir()
				path = filepath.Join(tmpDir, "os-release")
				if err := os.WriteFile(path, []byte(tt.content), 0644); err != nil {
					t.Fatalf("failed to write os-release: %v", err)
				}
			}

			info := detectFromFile(path)

			if info.ID != tt.wantID {
				t.Errorf("ID = %q, want %q", info.ID, tt.wantID)
			}
			if info.Version != tt.wantVer {
				t.Errorf("Version = %q, want %q", info.Version, tt.wantVer)
			}
			if info.Image() != tt.wantImage {
				t.Errorf("Image() = %q, want %q", info.Image(), tt.wantImage)
			}
		})
	}
}

func TestContainerName(t *testing.T) {
	name := ContainerName("/home/bjk/projects/my-gtk-app")
	if name != "igloo-my-gtk-app" {
		t.Errorf("ContainerName() = %q, want %q", name, "igloo-my-gtk-app")
	}
}
```

**Step 2: Run test to verify it fails**

Run: `cd /home/bjk/projects/frostyard/igloo/.worktrees/simplify && go test ./internal/host/...`
Expected: FAIL — package doesn't exist yet

**Step 3: Write the implementation**

Create `internal/host/detect.go`:

```go
package host

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// Known distro families that have Incus cloud images.
var knownDistros = map[string]bool{
	"debian":    true,
	"ubuntu":    true,
	"fedora":    true,
	"archlinux": true,
}

// Distros that use VERSION_ID instead of VERSION_CODENAME.
var usesVersionID = map[string]bool{
	"fedora": true,
}

// Distros that have a fixed version string.
var fixedVersion = map[string]string{
	"archlinux": "current",
}

// OSInfo holds the detected host OS information.
type OSInfo struct {
	ID      string // distro family (debian, ubuntu, etc.)
	Version string // release codename or version number
}

// Image returns the Incus image string for this OS.
func (o OSInfo) Image() string {
	return fmt.Sprintf("images:%s/%s/cloud", o.ID, o.Version)
}

// DetectOS reads /etc/os-release and returns OS info with ID_LIKE fallback.
func DetectOS() OSInfo {
	return detectFromFile("/etc/os-release")
}

// ContainerName derives the igloo container name from a project directory path.
func ContainerName(projectDir string) string {
	return "igloo-" + filepath.Base(projectDir)
}

func detectFromFile(path string) OSInfo {
	fallback := OSInfo{ID: "debian", Version: "trixie"}

	fields := parseOSRelease(path)
	if len(fields) == 0 {
		return fallback
	}

	id := strings.ToLower(fields["ID"])
	if id == "" {
		return fallback
	}

	// Try the primary ID first, then walk ID_LIKE.
	candidates := []string{id}
	if idLike := fields["ID_LIKE"]; idLike != "" {
		candidates = append(candidates, strings.Fields(idLike)...)
	}

	for _, candidate := range candidates {
		if !knownDistros[candidate] {
			continue
		}
		ver := versionFor(candidate, fields)
		if ver == "" {
			continue
		}
		return OSInfo{ID: candidate, Version: ver}
	}

	return fallback
}

// versionFor returns the version string for a known distro, given os-release fields.
func versionFor(distro string, fields map[string]string) string {
	if v, ok := fixedVersion[distro]; ok {
		return v
	}
	if usesVersionID[distro] {
		return fields["VERSION_ID"]
	}
	// For distros using codename: prefer UBUNTU_CODENAME (set by Ubuntu derivatives),
	// then VERSION_CODENAME.
	if distro == "ubuntu" || distro == "debian" {
		if uc := fields["UBUNTU_CODENAME"]; uc != "" && distro == "ubuntu" {
			return strings.ToLower(uc)
		}
	}
	return strings.ToLower(fields["VERSION_CODENAME"])
}

func parseOSRelease(path string) map[string]string {
	f, err := os.Open(path)
	if err != nil {
		return nil
	}
	defer f.Close()

	fields := make(map[string]string)
	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := scanner.Text()
		k, v, ok := strings.Cut(line, "=")
		if !ok {
			continue
		}
		fields[k] = strings.Trim(v, "\"")
	}
	return fields
}
```

**Step 4: Run test to verify it passes**

Run: `cd /home/bjk/projects/frostyard/igloo/.worktrees/simplify && go test ./internal/host/... -v`
Expected: PASS

**Step 5: Commit**

```bash
git add internal/host/detect.go internal/host/detect_test.go
git commit -m "feat: add internal/host package with ID_LIKE distro detection"
```

---

### Task 2: Decouple cloud-init from config package

Currently `GenerateCloudInit` takes `*config.IglooConfig`. Change it to take simple parameters so it has no dependency on the config package (which we'll delete later).

**Files:**
- Modify: `internal/incus/cloudinit.go`
- Modify: `internal/incus/cloudinit_test.go`

**Step 1: Write the failing test**

Replace `internal/incus/cloudinit_test.go` — remove all references to `config.IglooConfig`:

```go
package incus

import (
	"strings"
	"testing"
)

func TestGenerateCloudInit(t *testing.T) {
	result, err := GenerateCloudInit()
	if err != nil {
		t.Fatalf("GenerateCloudInit() failed: %v", err)
	}

	if !strings.HasPrefix(result, "#cloud-config") {
		t.Error("should start with #cloud-config")
	}
	if !strings.Contains(result, "users:") {
		t.Error("should contain users section")
	}
	if !strings.Contains(result, "runcmd:") {
		t.Error("should contain runcmd section")
	}
	if !strings.Contains(result, "timezone:") {
		t.Error("should contain timezone")
	}
	// Should NOT contain packages section (packages come from .igloo.sh now)
	if strings.Contains(result, "packages:") {
		t.Error("should not contain packages section")
	}
}
```

**Step 2: Run test to verify it fails**

Run: `cd /home/bjk/projects/frostyard/igloo/.worktrees/simplify && go test ./internal/incus/... -run TestGenerateCloudInit`
Expected: FAIL — signature mismatch

**Step 3: Write the implementation**

Replace `internal/incus/cloudinit.go` — remove config import, remove package handling, take no args (reads user/tz from environment):

```go
package incus

import (
	"bytes"
	"fmt"
	"os"
	"os/user"
	"strings"
	"text/template"
	"time"
)

const cloudInitTemplate = `#cloud-config
# Generated by igloo on {{.Timestamp}}

users:
  - name: {{.Username}}
    uid: {{.UID}}
    groups: sudo, video, render, audio
    shell: /bin/bash
    sudo: ALL=(ALL) NOPASSWD:ALL
    lock_passwd: true

timezone: {{.Timezone}}

runcmd:
  - mkdir -p /home/{{.Username}}
  - chown {{.UID}}:{{.GID}} /home/{{.Username}}
  - mkdir -p /run/user/{{.UID}}
  - chown {{.UID}}:{{.GID}} /run/user/{{.UID}}
  - chmod 700 /run/user/{{.UID}}
`

type cloudInitData struct {
	Username  string
	UID       int
	GID       int
	Timezone  string
	Timestamp string
}

// GenerateCloudInit creates a cloud-init config for user mapping and basic setup.
func GenerateCloudInit() (string, error) {
	currentUser, err := user.Current()
	if err != nil {
		return "", fmt.Errorf("failed to get current user: %w", err)
	}

	data := cloudInitData{
		Username:  currentUser.Username,
		UID:       os.Getuid(),
		GID:       os.Getgid(),
		Timezone:  getTimezone(),
		Timestamp: time.Now().Format(time.RFC3339),
	}

	tmpl, err := template.New("cloud-init").Parse(cloudInitTemplate)
	if err != nil {
		return "", fmt.Errorf("failed to parse cloud-init template: %w", err)
	}

	var buf bytes.Buffer
	if err := tmpl.Execute(&buf, data); err != nil {
		return "", fmt.Errorf("failed to execute cloud-init template: %w", err)
	}

	return buf.String(), nil
}

func getTimezone() string {
	if data, err := os.ReadFile("/etc/timezone"); err == nil {
		return strings.TrimSpace(string(data))
	}
	if target, err := os.Readlink("/etc/localtime"); err == nil {
		parts := strings.Split(target, "/zoneinfo/")
		if len(parts) == 2 {
			return parts[1]
		}
	}
	if tz := os.Getenv("TZ"); tz != "" {
		return tz
	}
	return "UTC"
}
```

**Step 4: Run test to verify it passes**

Run: `cd /home/bjk/projects/frostyard/igloo/.worktrees/simplify && go test ./internal/incus/... -v`
Expected: PASS

**Step 5: Commit**

```bash
git add internal/incus/cloudinit.go internal/incus/cloudinit_test.go
git commit -m "refactor: decouple cloud-init from config package"
```

---

### Task 3: Rewrite cmd/enter.go as the default command

This is the main rewrite. The enter command becomes the root command's default action. It handles the full lifecycle: detect OS, create container if needed, mount project, copy dotfiles, set up display, run `.igloo.sh`, and exec shell.

**Files:**
- Rewrite: `cmd/enter.go`

**Step 1: Write the implementation**

No unit test for this command (it orchestrates incus CLI calls). Replace `cmd/enter.go`:

```go
package cmd

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/frostyard/igloo/internal/display"
	"github.com/frostyard/igloo/internal/host"
	"github.com/frostyard/igloo/internal/incus"
	"github.com/frostyard/igloo/internal/ui"
	"github.com/spf13/cobra"
)

var noGUI bool

func enterCmd() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "igloo",
		Short: "Enter the igloo development container",
		Long: `Creates and enters an isolated development container.

If no container exists for the current directory, one is created automatically
using the host OS, with display passthrough and GPU support enabled.

Place a .igloo.sh script in your project root to run custom setup on first creation.`,
		Example: `  # Enter (or create) the container
  igloo

  # Enter without GUI support
  igloo --no-gui`,
		RunE: func(cmd *cobra.Command, args []string) error {
			return runEnter()
		},
	}

	cmd.Flags().BoolVar(&noGUI, "no-gui", false, "Skip display and GPU passthrough")

	return cmd
}

// dotfiles to copy from host home to container home on creation.
var dotfiles = []string{
	".gitconfig",
	".ssh",
	".bashrc",
	".profile",
	".bash_profile",
}

func runEnter() error {
	styles := ui.NewStyles()
	client := incus.NewClient()

	cwd, err := os.Getwd()
	if err != nil {
		return fmt.Errorf("failed to get current directory: %w", err)
	}
	name := host.ContainerName(cwd)
	username := os.Getenv("USER")
	homeDir := os.Getenv("HOME")

	exists, err := client.InstanceExists(name)
	if err != nil {
		return fmt.Errorf("failed to check instance: %w", err)
	}

	if !exists {
		if err := provision(client, styles, name, username, homeDir, cwd); err != nil {
			return err
		}
	}

	// Start if stopped
	running, err := client.IsRunning(name)
	if err != nil {
		return fmt.Errorf("failed to check instance status: %w", err)
	}
	if !running {
		fmt.Println(styles.Info("Starting container..."))
		if err := client.Start(name); err != nil {
			return fmt.Errorf("failed to start instance: %w", err)
		}
		fmt.Println(styles.Info("Waiting for container to be ready..."))
		if err := client.WaitForCloudInit(name); err != nil {
			fmt.Println(styles.Warning("Cloud-init wait timed out, continuing anyway..."))
		}
	}

	// Refresh display passthrough on every entry
	if !noGUI {
		if err := client.UpdateXauthority(name); err != nil {
			fmt.Println(styles.Warning(fmt.Sprintf("Could not update Xauthority: %v", err)))
		}
	}

	fmt.Println(styles.Info(fmt.Sprintf("Entering %s...", name)))
	return client.ExecInteractive(name, username, cwd)
}

func provision(client *incus.Client, styles *ui.Styles, name, username, homeDir, cwd string) error {
	// Detect host OS
	osInfo := host.DetectOS()
	image := osInfo.Image()
	fmt.Println(styles.Info(fmt.Sprintf("Detected host: %s/%s", osInfo.ID, osInfo.Version)))
	fmt.Println(styles.Info(fmt.Sprintf("Creating container %s from %s...", name, image)))

	// Generate cloud-init
	cloudInit, err := incus.GenerateCloudInit()
	if err != nil {
		return fmt.Errorf("failed to generate cloud-init: %w", err)
	}

	// Create instance
	if err := client.Create(name, image, cloudInit); err != nil {
		return fmt.Errorf("failed to create instance: %w", err)
	}

	// Mount project directory at the same absolute path
	fmt.Println(styles.Info(fmt.Sprintf("Mounting project at %s...", cwd)))
	if err := client.AddDiskDevice(name, "project", cwd, cwd); err != nil {
		return fmt.Errorf("failed to mount project directory: %w", err)
	}

	// Start container (before display passthrough, so /run/user exists)
	fmt.Println(styles.Info("Starting container..."))
	if err := client.Start(name); err != nil {
		return fmt.Errorf("failed to start instance: %w", err)
	}

	fmt.Println(styles.Info("Waiting for cloud-init to complete..."))
	if err := client.WaitForCloudInit(name); err != nil {
		return fmt.Errorf("cloud-init failed: %w", err)
	}

	// Copy dotfiles from host home into container home
	fmt.Println(styles.Info("Copying dotfiles..."))
	containerHome := fmt.Sprintf("/home/%s", username)
	for _, df := range dotfiles {
		src := filepath.Join(homeDir, df)
		if _, err := os.Stat(src); os.IsNotExist(err) {
			continue
		}
		dst := filepath.Join(containerHome, df)
		parentDir := filepath.Dir(dst)
		// Use tar to copy (handles both files and directories like .ssh/)
		cpCmd := fmt.Sprintf(
			"tar -cf - -C %s %s | incus exec %s -- tar -xf - -C %s && incus exec %s -- chown -R %s:%s %s",
			homeDir, df, name, parentDir, name, username, username, dst,
		)
		if err := execShell(cpCmd); err != nil {
			fmt.Println(styles.Warning(fmt.Sprintf("Could not copy %s: %v", df, err)))
		}
	}

	// Display passthrough
	if !noGUI {
		fmt.Println(styles.Info("Configuring display passthrough..."))
		displayType := display.Detect()
		if err := display.ConfigurePassthrough(client, name, displayType, true); err != nil {
			fmt.Println(styles.Warning(fmt.Sprintf("Display passthrough failed: %v", err)))
			fmt.Println(styles.Warning("GUI applications may not work correctly"))
		}
	}

	// Run .igloo.sh if it exists
	scriptPath := filepath.Join(cwd, ".igloo.sh")
	if _, err := os.Stat(scriptPath); err == nil {
		fmt.Println(styles.Info("Running .igloo.sh..."))
		// The project dir is mounted at cwd inside the container
		containerScript := filepath.Join(cwd, ".igloo.sh")
		if err := client.ExecAsRoot(name, "chmod", "+x", containerScript); err != nil {
			return fmt.Errorf("failed to make .igloo.sh executable: %w", err)
		}
		if err := client.ExecAsRoot(name, "/bin/bash", containerScript); err != nil {
			return fmt.Errorf(".igloo.sh failed: %w", err)
		}
	}

	fmt.Println(styles.Success(fmt.Sprintf("Container %s ready!", name)))
	return nil
}

func execShell(command string) error {
	cmd := execCommand("sh", "-c", command)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}
```

Note: `execCommand` is `exec.Command` — we need to add the import. The actual import will be `os/exec` and the call will be `exec.Command`. Let me fix that in the actual code — use `exec.Command` directly.

**Step 2: Verify it compiles**

Run: `cd /home/bjk/projects/frostyard/igloo/.worktrees/simplify && go build ./cmd/...`
Expected: May have compile errors from other files still importing config — that's OK, we fix in later tasks.

**Step 3: Commit**

```bash
git add cmd/enter.go
git commit -m "feat: rewrite enter command with unified create/enter flow"
```

---

### Task 4: Simplify cmd/destroy.go

Destroy no longer depends on config. It derives the container name from the current directory, same as enter.

**Files:**
- Rewrite: `cmd/destroy.go`

**Step 1: Write the implementation**

Replace `cmd/destroy.go`:

```go
package cmd

import (
	"fmt"
	"os"

	"github.com/frostyard/igloo/internal/host"
	"github.com/frostyard/igloo/internal/incus"
	"github.com/frostyard/igloo/internal/ui"
	"github.com/spf13/cobra"
)

func destroyCmd() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "destroy",
		Short: "Destroy the igloo container for this project",
		Long:  `Removes the igloo container completely. The project files are untouched.`,
		Example: `  # Destroy the container
  igloo destroy`,
		RunE: func(cmd *cobra.Command, args []string) error {
			return runDestroy()
		},
	}

	return cmd
}

func runDestroy() error {
	styles := ui.NewStyles()
	client := incus.NewClient()

	cwd, err := os.Getwd()
	if err != nil {
		return fmt.Errorf("failed to get current directory: %w", err)
	}
	name := host.ContainerName(cwd)

	exists, err := client.InstanceExists(name)
	if err != nil {
		return fmt.Errorf("failed to check instance: %w", err)
	}

	if !exists {
		fmt.Println(styles.Warning(fmt.Sprintf("Container %s does not exist", name)))
		return nil
	}

	fmt.Println(styles.Info(fmt.Sprintf("Destroying container %s...", name)))
	if err := client.Delete(name, true); err != nil {
		return fmt.Errorf("failed to destroy instance: %w", err)
	}

	fmt.Println(styles.Success(fmt.Sprintf("Container %s destroyed", name)))
	return nil
}
```

**Step 2: Commit**

```bash
git add cmd/destroy.go
git commit -m "refactor: simplify destroy command, remove config dependency"
```

---

### Task 5: Rewrite cmd/root.go and clean up incus/client.go

Root command registers only enter (as default) and destroy. Also clean client.go to remove the unused `Stop` method (containers are only destroyed, never just stopped).

**Files:**
- Rewrite: `cmd/root.go`
- Modify: `internal/incus/client.go` — remove `Stop` method

**Step 1: Write the implementation**

Replace `cmd/root.go`:

```go
package cmd

import (
	"github.com/spf13/cobra"
)

// RootCmd returns the root command for igloo.
// Running `igloo` with no subcommand enters the container.
func RootCmd() *cobra.Command {
	enter := enterCmd()

	enter.AddCommand(destroyCmd())

	return enter
}
```

The trick: make the enter command the root itself. `igloo` runs enter. `igloo destroy` runs destroy as a subcommand.

**Step 2: Remove `Stop` from client.go**

In `internal/incus/client.go`, delete the `Stop` method (lines 85-91). It is no longer called by any command.

**Step 3: Remove `incus/client_test.go` if it only tests removed functionality**

Check `internal/incus/client_test.go` — if tests reference config package or removed methods, update them.

**Step 4: Verify it compiles (ignoring deleted command files for now)**

Run: `cd /home/bjk/projects/frostyard/igloo/.worktrees/simplify && go vet ./cmd/... ./internal/...`

**Step 5: Commit**

```bash
git add cmd/root.go internal/incus/client.go internal/incus/client_test.go
git commit -m "refactor: make enter the root command, add destroy as subcommand"
```

---

### Task 6: Delete old files

Remove all files that are no longer needed.

**Files to delete:**
- `cmd/init.go`
- `cmd/provision.go`
- `cmd/status.go`
- `cmd/stop.go`
- `cmd/remove.go`
- `internal/config/config.go`
- `internal/config/config_test.go`
- `internal/config/distros.go`
- `internal/config/distros_test.go`
- `internal/config/hash.go`
- `internal/config/hash_test.go`
- `internal/config/hostdetect.go`
- `internal/config/hostdetect_test.go`
- `internal/script/runner.go`
- `internal/script/runner_test.go`

**Step 1: Delete the files**

```bash
cd /home/bjk/projects/frostyard/igloo/.worktrees/simplify
rm cmd/init.go cmd/provision.go cmd/status.go cmd/stop.go cmd/remove.go
rm -r internal/config
rm -r internal/script
```

**Step 2: Verify everything compiles**

Run: `cd /home/bjk/projects/frostyard/igloo/.worktrees/simplify && go build ./...`
Expected: PASS — no remaining references to deleted packages

**Step 3: Run all tests**

Run: `cd /home/bjk/projects/frostyard/igloo/.worktrees/simplify && go test ./...`
Expected: PASS

**Step 4: Commit**

```bash
git add -A
git commit -m "refactor: remove old commands, config package, and script package"
```

---

### Task 7: Remove ini dependency and tidy modules

**Files:**
- Modify: `go.mod`
- Modify: `go.sum`

**Step 1: Remove the dependency and tidy**

```bash
cd /home/bjk/projects/frostyard/igloo/.worktrees/simplify
go mod tidy
```

**Step 2: Verify gopkg.in/ini.v1 is gone from go.mod**

```bash
grep ini go.mod
```

Expected: No output

**Step 3: Run all tests one final time**

```bash
cd /home/bjk/projects/frostyard/igloo/.worktrees/simplify && go test ./...
```

Expected: PASS

**Step 4: Verify binary builds and runs**

```bash
cd /home/bjk/projects/frostyard/igloo/.worktrees/simplify && go build -o igloo . && ./igloo --help
```

Expected: Shows help with just the root command (enter behavior) and `destroy` subcommand.

**Step 5: Commit**

```bash
git add go.mod go.sum
git commit -m "chore: remove gopkg.in/ini.v1 dependency, go mod tidy"
```

---

### Task 8: Update README.md

Update the README to reflect the simplified two-command interface.

**Files:**
- Modify: `README.md`

**Step 1: Read current README**

Read `README.md` to understand current structure.

**Step 2: Rewrite to reflect new design**

Key changes:
- Update description to focus on GTK dev containers
- Replace command reference with just `igloo` and `igloo destroy`
- Replace config section with `.igloo.sh` explanation
- Remove references to `igloo init`, INI config, multi-distro flags
- Keep installation and build instructions

**Step 3: Commit**

```bash
git add README.md
git commit -m "docs: update README for simplified two-command interface"
```
