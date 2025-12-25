package cmd

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/frostyard/igloo/internal/config"
	"github.com/frostyard/igloo/internal/display"
	"github.com/frostyard/igloo/internal/incus"
	"github.com/frostyard/igloo/internal/script"
	"github.com/frostyard/igloo/internal/ui"
)

// provisionContainer creates and configures an incus container from an existing igloo.ini
func provisionContainer(cfg *config.IglooConfig) error {
	styles := ui.NewStyles()
	client := incus.NewClient()

	// Get current working directory
	cwd, err := os.Getwd()
	if err != nil {
		return fmt.Errorf("failed to get current directory: %w", err)
	}
	projectName := filepath.Base(cwd)
	username := os.Getenv("USER")
	name := cfg.Container.Name
	image := cfg.Container.Image

	// Check if instance already exists
	exists, err := client.InstanceExists(name)
	if err != nil {
		return fmt.Errorf("failed to check instance: %w", err)
	}
	if exists {
		return nil // Already exists, nothing to do
	}

	fmt.Println(styles.Info(fmt.Sprintf("Creating container %s from %s...", name, image)))

	// Generate cloud-init config
	cloudInit, err := incus.GenerateCloudInit(cfg)
	if err != nil {
		return fmt.Errorf("failed to generate cloud-init: %w", err)
	}

	// Create instance with cloud-init
	if err := client.Create(name, image, cloudInit); err != nil {
		return fmt.Errorf("failed to create instance: %w", err)
	}

	// Add mount devices
	if cfg.Mounts.Home {
		homeDir := os.Getenv("HOME")
		hostPath := fmt.Sprintf("/home/%s/host", username)
		fmt.Println(styles.Info(fmt.Sprintf("Mounting home directory at %s...", hostPath)))
		if err := client.AddDiskDevice(name, "home", homeDir, hostPath); err != nil {
			return fmt.Errorf("failed to add home mount: %w", err)
		}
	}

	if cfg.Mounts.Project {
		workspacePath := fmt.Sprintf("/home/%s/workspace/%s", username, projectName)
		fmt.Println(styles.Info(fmt.Sprintf("Mounting project directory at %s...", workspacePath)))
		if err := client.AddDiskDevice(name, "project", cwd, workspacePath); err != nil {
			return fmt.Errorf("failed to add project mount: %w", err)
		}
	}

	// Start the instance first (before display passthrough, so /run/user exists)
	fmt.Println(styles.Info("Starting container..."))
	if err := client.Start(name); err != nil {
		return fmt.Errorf("failed to start instance: %w", err)
	}

	// Wait for cloud-init to complete (this creates /run/user/<uid>)
	fmt.Println(styles.Info("Waiting for cloud-init to complete..."))
	if err := client.WaitForCloudInit(name); err != nil {
		return fmt.Errorf("cloud-init failed: %w", err)
	}

	// Add display passthrough (now /run/user/<uid> exists)
	if cfg.Display.Enabled {
		fmt.Println(styles.Info("Configuring display passthrough..."))
		displayType := display.Detect()
		if err := display.ConfigurePassthrough(client, name, displayType, cfg.Display.GPU); err != nil {
			fmt.Println(styles.Warning(fmt.Sprintf("Display passthrough configuration failed: %v", err)))
			fmt.Println(styles.Warning("GUI applications may not work correctly"))
		}
	}

	// Run scripts from .igloo/scripts directory if present
	runner := script.NewRunner(client, name, username, projectName, cwd)
	scripts, err := runner.GetScripts()
	if err != nil {
		return fmt.Errorf("failed to check for scripts: %w", err)
	}
	if len(scripts) > 0 {
		fmt.Println(styles.Info(fmt.Sprintf("Running %d init script(s) from .igloo/scripts/...", len(scripts))))
		for _, s := range scripts {
			fmt.Println(styles.Info(fmt.Sprintf("  → %s", s)))
		}
		if err := runner.RunScripts(); err != nil {
			return fmt.Errorf("init scripts failed: %w", err)
		}
	}

	fmt.Println(styles.Success(fmt.Sprintf("Igloo environment '%s' is ready!", name)))

	return nil
}
