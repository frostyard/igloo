package cmd

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strings"

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
If the container is not running, it will be started first.
If the .igloo configuration has changed, you will be prompted to rebuild.`,
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

	if exists {
		// Check if config has changed since last provision
		changed, currentHash, err := config.ConfigChanged(cfg.Container.Name)
		if err != nil {
			fmt.Println(styles.Warning(fmt.Sprintf("Could not check for config changes: %v", err)))
		} else if changed {
			fmt.Println(styles.Warning("Configuration in .igloo/ has changed since last provision."))
			fmt.Print(styles.Info("Rebuild container to apply changes? [y/N]: "))

			reader := bufio.NewReader(os.Stdin)
			response, _ := reader.ReadString('\n')
			response = strings.TrimSpace(strings.ToLower(response))

			if response == "y" || response == "yes" {
				fmt.Println(styles.Info("Removing old container..."))
				if err := client.Delete(cfg.Container.Name, true); err != nil {
					return fmt.Errorf("failed to remove container: %w", err)
				}
				exists = false
			} else {
				// Update stored hash to current so we don't keep asking
				if err := config.StoreHash(cfg.Container.Name, currentHash); err != nil {
					fmt.Println(styles.Warning(fmt.Sprintf("Could not update config hash: %v", err)))
				}
			}
		} else if currentHash != "" {
			// No stored hash yet (first run with existing container) - store it now
			storedHash, _ := config.GetStoredHash(cfg.Container.Name)
			if storedHash == "" {
				if err := config.StoreHash(cfg.Container.Name, currentHash); err != nil {
					fmt.Println(styles.Warning(fmt.Sprintf("Could not store config hash: %v", err)))
				}
			}
		}
	}

	if !exists {
		fmt.Println(styles.Info("Container does not exist, provisioning..."))
		if err := provisionContainer(cfg); err != nil {
			return fmt.Errorf("failed to provision container: %w", err)
		}

		// Store the config hash after successful provision
		currentHash, err := config.HashConfigDir()
		if err == nil {
			if err := config.StoreHash(cfg.Container.Name, currentHash); err != nil {
				fmt.Println(styles.Warning(fmt.Sprintf("Could not store config hash: %v", err)))
			}
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
