package cmd

import (
	"fmt"

	"github.com/frostyard/igloo/internal/config"
	"github.com/frostyard/igloo/internal/incus"
	"github.com/frostyard/igloo/internal/ui"
	"github.com/spf13/cobra"
)

func removeCmd() *cobra.Command {
	var force bool

	cmd := &cobra.Command{
		Use:   "remove",
		Short: "Remove the igloo container but keep the configuration",
		Long: `Remove deletes the igloo container but preserves the .igloo configuration directory.
This allows you to recreate the environment later with the same settings using 'igloo init'.`,
		Example: `  # Remove the igloo container
  igloo remove

  # Force remove without stopping first
  igloo remove --force`,
		RunE: func(cmd *cobra.Command, args []string) error {
			return runRemove(force)
		},
	}

	cmd.Flags().BoolVarP(&force, "force", "f", false, "Force remove without stopping first")

	return cmd
}

func runRemove(force bool) error {
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

	if !exists {
		fmt.Println(styles.Warning(fmt.Sprintf("Container %s does not exist", cfg.Container.Name)))
		return nil
	}

	fmt.Println(styles.Info(fmt.Sprintf("Removing container %s...", cfg.Container.Name)))
	if err := client.Delete(cfg.Container.Name, force); err != nil {
		return fmt.Errorf("failed to remove instance: %w", err)
	}

	// Remove stored config hash so next enter will re-provision
	if err := config.RemoveStoredHash(cfg.Container.Name); err != nil {
		fmt.Println(styles.Warning(fmt.Sprintf("Could not remove stored hash: %v", err)))
	}

	fmt.Println(styles.Success(fmt.Sprintf("Container %s removed (.igloo preserved)", cfg.Container.Name)))
	return nil
}
