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
