package cmd

import (
	"github.com/spf13/cobra"
)

// RootCmd returns the root command for igloo
func RootCmd() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "igloo",
		Short: "Manage incus-based development environments",
		Long: `Igloo creates and manages incus containers for local development.

It allows developers to use tools that aren't available on the host system
by running them in a container while sharing the project directory and display.`,
		Example: `  # Initialize a new igloo environment in the current directory
  igloo init

  # Initialize with a specific distro and release
  igloo init --distro ubuntu --release questing

  # Enter the igloo environment
  igloo enter

  # Check environment status
  igloo status

  # Stop the environment
  igloo stop

  # Destroy the environment
  igloo destroy`,
		SilenceUsage: true,
	}

	cmd.AddCommand(initCmd())
	cmd.AddCommand(enterCmd())
	cmd.AddCommand(stopCmd())
	cmd.AddCommand(removeCmd())
	cmd.AddCommand(destroyCmd())
	cmd.AddCommand(statusCmd())

	return cmd
}
