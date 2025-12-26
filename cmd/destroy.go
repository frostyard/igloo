package cmd

import (
	"fmt"
	"os"

	"github.com/frostyard/igloo/internal/config"
	"github.com/frostyard/igloo/internal/incus"
	"github.com/frostyard/igloo/internal/ui"
	"github.com/spf13/cobra"
)

func destroyCmd() *cobra.Command {
	var force bool
	var keepConfig bool

	cmd := &cobra.Command{
		Use:   "destroy",
		Short: "Destroy the igloo development environment",
		Long: `Destroy removes the igloo container completely.
By default, it also removes the .igloo configuration directory.`,
		Example: `  # Destroy the igloo environment
  igloo destroy

  # Force destroy without confirmation
  igloo destroy --force

  # Keep the .igloo directory
  igloo destroy --keep-config`,
		RunE: func(cmd *cobra.Command, args []string) error {
			return runDestroy(force, keepConfig)
		},
	}

	cmd.Flags().BoolVarP(&force, "force", "f", false, "Force destroy without stopping first")
	cmd.Flags().BoolVar(&keepConfig, "keep-config", false, "Keep the .igloo configuration directory")

	return cmd
}

func runDestroy(force, keepConfig bool) error {
	styles := ui.NewStyles()

	// Load config
	cfg, err := config.Load(config.ConfigPath())
	if err != nil {
		return fmt.Errorf("failed to load config: %w", err)
	}

	client := incus.NewClient()

	// Check if instance exists
	exists, err := client.InstanceExists(cfg.Container.Name)
	if err != nil {
		return fmt.Errorf("failed to check instance: %w", err)
	}

	if exists {
		fmt.Println(styles.Info(fmt.Sprintf("Destroying container %s...", cfg.Container.Name)))
		if err := client.Delete(cfg.Container.Name, force); err != nil {
			return fmt.Errorf("failed to destroy instance: %w", err)
		}
		fmt.Println(styles.Success(fmt.Sprintf("Container %s destroyed", cfg.Container.Name)))
	} else {
		fmt.Println(styles.Warning(fmt.Sprintf("Container %s does not exist", cfg.Container.Name)))
	}

	// Remove stored config hash
	if err := config.RemoveStoredHash(cfg.Container.Name); err != nil {
		fmt.Println(styles.Warning(fmt.Sprintf("Could not remove stored hash: %v", err)))
	}

	// Remove .igloo directory unless --keep-config
	if !keepConfig {
		fmt.Println(styles.Info("Removing .igloo directory..."))
		if err := os.RemoveAll(config.ConfigDir); err != nil {
			return fmt.Errorf("failed to remove .igloo directory: %w", err)
		}
	}

	fmt.Println(styles.Success("Igloo environment destroyed"))
	return nil
}
