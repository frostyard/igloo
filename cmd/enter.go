package cmd

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/frostyard/igloo/internal/config"
	"github.com/frostyard/igloo/internal/incus"
	"github.com/frostyard/igloo/internal/ui"
	"github.com/spf13/cobra"
)

func enterCmd() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "enter",
		Short: "Enter the igloo development environment",
		Long: `Enter opens an interactive shell in the igloo container.
If the container is not running, it will be started first.`,
		Example: `  # Enter the igloo environment
  igloo enter`,
		RunE: func(cmd *cobra.Command, args []string) error {
			return runEnter()
		},
	}

	return cmd
}

func runEnter() error {
	styles := ui.NewStyles()

	// Load config
	cfg, err := config.Load(config.ConfigPath())
	if err != nil {
		return fmt.Errorf("failed to load config: %w\nRun 'igloo init' to create a new environment", err)
	}

	client := incus.NewClient()

	// Check if instance exists, provision if not
	exists, err := client.InstanceExists(cfg.Container.Name)
	if err != nil {
		return fmt.Errorf("failed to check instance: %w", err)
	}
	if !exists {
		fmt.Println(styles.Info("Container does not exist, provisioning..."))
		if err := provisionContainer(cfg); err != nil {
			return fmt.Errorf("failed to provision container: %w", err)
		}
	}

	// Check if instance is running
	running, err := client.IsRunning(cfg.Container.Name)
	if err != nil {
		return fmt.Errorf("failed to check instance status: %w", err)
	}

	if !running {
		fmt.Println(styles.Info("Starting container..."))
		if err := client.Start(cfg.Container.Name); err != nil {
			return fmt.Errorf("failed to start instance: %w", err)
		}

		// Wait for cloud-init if container was stopped
		fmt.Println(styles.Info("Waiting for container to be ready..."))
		if err := client.WaitForCloudInit(cfg.Container.Name); err != nil {
			fmt.Println(styles.Warning("Cloud-init wait timed out, continuing anyway..."))
		}
	}

	// Get user info
	username := os.Getenv("USER")
	cwd, err := os.Getwd()
	if err != nil {
		return fmt.Errorf("failed to get current directory: %w", err)
	}
	projectName := filepath.Base(cwd)
	workDir := fmt.Sprintf("/home/%s/workspace/%s", username, projectName)

	fmt.Println(styles.Info(fmt.Sprintf("Entering %s...", cfg.Container.Name)))

	// Execute interactive shell
	if err := client.ExecInteractive(cfg.Container.Name, username, workDir); err != nil {
		return fmt.Errorf("failed to enter container: %w", err)
	}

	return nil
}
