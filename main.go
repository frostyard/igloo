package main

import (
	"context"
	"os"
	"os/signal"

	"github.com/charmbracelet/fang"
	"github.com/frostyard/igloo/cmd"
)

var (
	version = "dev"
	commit  = "none"
)

func main() {
	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt)
	defer cancel()

	if err := fang.Execute(ctx, cmd.RootCmd(),
		fang.WithVersion(version),
		fang.WithCommit(commit),
		fang.WithNotifySignal(os.Interrupt),
	); err != nil {
		os.Exit(1)
	}
}
