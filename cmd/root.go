package cmd

import (
	"github.com/spf13/cobra"
)

// RootCmd returns the root command for igloo.
// Running `igloo` with no subcommand enters the container.
func RootCmd() *cobra.Command {
	enter := enterCmd()

	enter.AddCommand(destroyCmd())

	return enter
}
