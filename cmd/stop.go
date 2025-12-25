package cmd

import (
	"fmt"

	"github.com/frostyard/igloo/internal/config"
	"github.com/frostyard/igloo/internal/incus"
	"github.com/frostyard/igloo/internal/ui"
	"github.com/spf13/cobra"
)

func stopCmd() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "stop",
		Short: "Stop the igloo development environment",
		Long:  `Stop shuts down the igloo container without destroying it.`,
		Example: `  # Stop the igloo environment
  igloo stop`,
		RunE: func(cmd *cobra.Command, args []string) error {
			return runStop()
		},
	}

	return cmd
}

func runStop() error {
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
		return fmt.Errorf("instance %s does not exist", cfg.Container.Name)
	}

	// Check if already stopped
	running, err := client.IsRunning(cfg.Container.Name)
	if err != nil {
		return fmt.Errorf("failed to check instance status: %w", err)
	}
	if !running {
		fmt.Println(styles.Info(fmt.Sprintf("Container %s is already stopped", cfg.Container.Name)))
		return nil
	}

	fmt.Println(styles.Info(fmt.Sprintf("Stopping %s...", cfg.Container.Name)))
	if err := client.Stop(cfg.Container.Name); err != nil {
		return fmt.Errorf("failed to stop instance: %w", err)
	}

	fmt.Println(styles.Success(fmt.Sprintf("Container %s stopped", cfg.Container.Name)))
	return nil
}
