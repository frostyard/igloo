package cmd

import (
	"fmt"
	"os"
	"os/exec"
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
		// Use tar piped into incus exec to copy files/dirs into the container
		cpCmd := exec.Command("sh", "-c",
			fmt.Sprintf("tar -cf - -C %s %s | incus exec %s -- tar -xf - -C %s",
				homeDir, df, name, containerHome))
		cpCmd.Stdout = os.Stdout
		cpCmd.Stderr = os.Stderr
		if err := cpCmd.Run(); err != nil {
			fmt.Println(styles.Warning(fmt.Sprintf("Could not copy %s: %v", df, err)))
			continue
		}
		// Fix ownership
		dst := filepath.Join(containerHome, df)
		if err := client.ExecAsRoot(name, "chown", "-R",
			fmt.Sprintf("%d:%d", os.Getuid(), os.Getgid()), dst); err != nil {
			fmt.Println(styles.Warning(fmt.Sprintf("Could not fix ownership for %s: %v", df, err)))
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
