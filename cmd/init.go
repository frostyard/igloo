package cmd

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/frostyard/igloo/internal/config"
	"github.com/frostyard/igloo/internal/ui"
	"github.com/spf13/cobra"
)

func initCmd() *cobra.Command {
	var distro string
	var release string
	var name string
	var packages string

	cmd := &cobra.Command{
		Use:   "init",
		Short: "Initialize a new igloo development environment",
		Long: `Initialize creates an .igloo/igloo.ini configuration file and sets up
an incus container for development. The container will have:

- A user matching your host UID/GID
- Your home directory mounted at ~/host
- The project directory mounted at ~/workspace/<project>
- Display passthrough for GUI applications`,
		Example: `  # Initialize with host OS defaults
  igloo init

  # Initialize with Ubuntu Questing
  igloo init --distro ubuntu --release questing

  # Initialize with custom name and packages
  igloo init --name myproject-dev --packages "git,curl,vim"`,
		RunE: func(cmd *cobra.Command, args []string) error {
			return runInit(distro, release, name, packages)
		},
	}

	cmd.Flags().StringVarP(&distro, "distro", "d", "", "Linux distribution (ubuntu, debian, fedora, archlinux)")
	cmd.Flags().StringVarP(&release, "release", "r", "", "Distribution release (e.g., questing, trixie, 43, current)")
	cmd.Flags().StringVarP(&name, "name", "n", "", "Container name (default: igloo-<dirname>)")
	cmd.Flags().StringVarP(&packages, "packages", "p", "", "Comma-separated list of packages to install")

	return cmd
}

func runInit(distro, release, name, packages string) error {
	styles := ui.NewStyles()

	// Check if .igloo directory already exists
	if _, err := os.Stat(config.ConfigDir); err == nil {
		return fmt.Errorf(".igloo directory already exists in this project")
	}

	// Get current working directory
	cwd, err := os.Getwd()
	if err != nil {
		return fmt.Errorf("failed to get current directory: %w", err)
	}
	projectName := filepath.Base(cwd)

	// Detect distro/release from host if not specified
	if distro == "" || release == "" {
		hostDistro, hostRelease := config.DetectHostOS()
		if distro == "" {
			distro = hostDistro
		}
		if release == "" {
			release = hostRelease
		}
		fmt.Println(styles.Info(fmt.Sprintf("Detected host OS: %s/%s", distro, release)))
	}

	// Validate distro/release
	if err := config.ValidateDistro(distro, release); err != nil {
		return err
	}

	// Set default container name
	if name == "" {
		name = "igloo-" + projectName
	}

	// Build image name
	image := fmt.Sprintf("images:%s/%s/cloud", distro, release)

	// Create config
	cfg := &config.IglooConfig{
		Container: config.ContainerConfig{
			Image: image,
			Name:  name,
		},
		Packages: config.PackagesConfig{
			Install: packages,
		},
		Mounts: config.MountsConfig{
			Home:    true,
			Project: true,
		},
		Display: config.DisplayConfig{
			Enabled: true,
			GPU:     true,
		},
	}

	// Create .igloo directory and write config file
	fmt.Println(styles.Info("Creating .igloo directory..."))
	if err := os.MkdirAll(config.ConfigDir, 0755); err != nil {
		return fmt.Errorf("failed to create .igloo directory: %w", err)
	}

	fmt.Println(styles.Info("Writing .igloo/igloo.ini..."))
	if err := config.Write(config.ConfigPath(), cfg); err != nil {
		return fmt.Errorf("failed to write config: %w", err)
	}

	// Create scripts directory with example script
	scriptsDir := config.ScriptsPath()
	if err := os.MkdirAll(scriptsDir, 0755); err != nil {
		return fmt.Errorf("failed to create scripts directory: %w", err)
	}

	exampleScript := `#!/bin/bash
# Example igloo init script
#
# Scripts in .igloo/scripts/ run in lexicographical order during 'igloo init'
# and when re-provisioning with 'igloo enter' (if container doesn't exist).
#
# Scripts run as root inside the container. The project directory is mounted
# at ~/workspace/<project-name>/ so you can access project files.
#
# Common uses:
#   - Install additional packages: apt-get install -y nodejs npm
#   - Configure development tools: git config --global user.name "Your Name"
#   - Set up databases: systemctl enable postgresql
#   - Install language-specific tools: pip install poetry
#
# Naming convention: Use numbered prefixes for ordering (e.g., 01-packages.sh, 02-config.sh)
#
# To enable this script, rename it to remove the .example suffix:
#   mv 00-example.sh.example 00-example.sh

echo "Hello from igloo init script!"
echo "Container user: $USER"
echo "Working directory: $(pwd)"
`
	examplePath := filepath.Join(scriptsDir, "00-example.sh.example")
	if err := os.WriteFile(examplePath, []byte(exampleScript), 0644); err != nil {
		return fmt.Errorf("failed to write example script: %w", err)
	}

	// Provision the container
	if err := provisionContainer(cfg); err != nil {
		return err
	}

	fmt.Println(styles.Info("Run 'igloo enter' to start working"))

	return nil
}
