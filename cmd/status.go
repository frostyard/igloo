package cmd

import (
	"fmt"

	"github.com/frostyard/igloo/internal/config"
	"github.com/frostyard/igloo/internal/incus"
	"github.com/frostyard/igloo/internal/ui"
	"github.com/spf13/cobra"
)

func statusCmd() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "status",
		Short: "Show the status of the igloo development environment",
		Long:  `Status displays information about the igloo container.`,
		Example: `  # Show environment status
  igloo status`,
		RunE: func(cmd *cobra.Command, args []string) error {
			return runStatus()
		},
	}

	return cmd
}

func runStatus() error {
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

	fmt.Println(styles.Header("Igloo Environment Status"))
	fmt.Println()

	fmt.Printf("  %s %s\n", styles.Label("Name:"), cfg.Container.Name)
	fmt.Printf("  %s %s\n", styles.Label("Image:"), cfg.Container.Image)

	if !exists {
		fmt.Printf("  %s %s\n", styles.Label("Status:"), styles.Error("not created"))
		return nil
	}

	// Get instance status
	running, err := client.IsRunning(cfg.Container.Name)
	if err != nil {
		return fmt.Errorf("failed to check instance status: %w", err)
	}

	if running {
		fmt.Printf("  %s %s\n", styles.Label("Status:"), styles.Success("running"))
	} else {
		fmt.Printf("  %s %s\n", styles.Label("Status:"), styles.Warning("stopped"))
	}

	// Show mount info
	fmt.Println()
	fmt.Println(styles.Header("Mounts"))
	if cfg.Mounts.Home {
		fmt.Printf("  %s ~/host\n", styles.Label("Home:"))
	}
	if cfg.Mounts.Project {
		fmt.Printf("  %s ~/workspace/<project>\n", styles.Label("Project:"))
	}

	// Show display info
	if cfg.Display.Enabled {
		fmt.Println()
		fmt.Println(styles.Header("Display"))
		fmt.Printf("  %s enabled\n", styles.Label("Passthrough:"))
		if cfg.Display.GPU {
			fmt.Printf("  %s enabled\n", styles.Label("GPU:"))
		}
	}

	// Show packages
	if cfg.Packages.Install != "" {
		fmt.Println()
		fmt.Println(styles.Header("Packages"))
		fmt.Printf("  %s\n", cfg.Packages.Install)
	}

	return nil
}
