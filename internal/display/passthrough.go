package display

import (
	"fmt"
	"os"

	"github.com/frostyard/igloo/internal/incus"
)

// ConfigurePassthrough sets up display passthrough for an incus instance
func ConfigurePassthrough(client *incus.Client, name string, displayType Type, enableGPU bool) error {
	uid := os.Getuid()
	gid := os.Getgid()

	switch displayType {
	case X11:
		if err := configureX11(client, name, uid, gid); err != nil {
			return err
		}
	case Wayland:
		if err := configureWayland(client, name, uid, gid); err != nil {
			return err
		}
	case None:
		// No display to configure
		return nil
	}

	// Add GPU if requested
	if enableGPU {
		if err := client.AddGPUDevice(name); err != nil {
			return fmt.Errorf("failed to add GPU device: %w", err)
		}
	}

	return nil
}

// configureX11 sets up X11 display passthrough
func configureX11(client *incus.Client, name string, uid, gid int) error {
	displayNum := GetX11Display()

	// Add X11 socket proxy using file-based socket (not abstract)
	x11Connect := fmt.Sprintf("unix:/tmp/.X11-unix/X%s", displayNum)
	x11Listen := fmt.Sprintf("unix:/tmp/.X11-unix/X%s", displayNum)

	if err := client.AddSimpleProxyDevice(name, "x11", x11Connect, x11Listen, uid, gid); err != nil {
		return fmt.Errorf("failed to add X11 proxy device: %w", err)
	}

	// Add Xauthority file if it exists
	// Check XAUTHORITY env var first (handles XWayland dynamic paths like .mutter-Xwaylandauth.*)
	xauthFile := os.Getenv("XAUTHORITY")
	if xauthFile == "" {
		xauthFile = os.Getenv("HOME") + "/.Xauthority"
	}

	if _, err := os.Stat(xauthFile); err == nil {
		username := os.Getenv("USER")
		xauthPath := fmt.Sprintf("/home/%s/.Xauthority", username)
		if err := client.AddDiskDevice(name, "xauthority", xauthFile, xauthPath); err != nil {
			return fmt.Errorf("failed to add Xauthority mount: %w", err)
		}
	} else {
		fmt.Printf("Note: No Xauthority file found at %s, X11 auth may fail\n", xauthFile)
	}

	// Set DISPLAY environment variable
	if err := client.SetConfig(name, "environment.DISPLAY", ":"+displayNum); err != nil {
		return fmt.Errorf("failed to set DISPLAY: %w", err)
	}

	return nil
}

// configureWayland sets up Wayland display passthrough
func configureWayland(client *incus.Client, name string, uid, gid int) error {
	waylandDisplay := GetWaylandDisplay()
	runtimeDir := GetXDGRuntimeDir()

	// Add Wayland socket proxy
	waylandConnect := fmt.Sprintf("unix:%s/%s", runtimeDir, waylandDisplay)
	waylandListen := fmt.Sprintf("unix:/run/user/%d/%s", uid, waylandDisplay)

	if err := client.AddProxyDevice(name, "wayland", waylandConnect, waylandListen, uid, gid); err != nil {
		return fmt.Errorf("failed to add Wayland proxy device: %w", err)
	}

	// Set Wayland environment variables
	if err := client.SetConfig(name, "environment.WAYLAND_DISPLAY", waylandDisplay); err != nil {
		return fmt.Errorf("failed to set WAYLAND_DISPLAY: %w", err)
	}

	if err := client.SetConfig(name, "environment.XDG_RUNTIME_DIR", fmt.Sprintf("/run/user/%d", uid)); err != nil {
		return fmt.Errorf("failed to set XDG_RUNTIME_DIR: %w", err)
	}

	// Also set up XWayland if X11 is available (many Wayland sessions have XWayland)
	if os.Getenv("DISPLAY") != "" {
		if err := configureX11(client, name, uid, gid); err != nil {
			// XWayland is optional, don't fail if it doesn't work
			fmt.Printf("Note: XWayland setup skipped: %v\n", err)
		}
	}

	return nil
}
