package display

import (
	"os"
)

// Type represents the display server type
type Type int

const (
	// None indicates no display server detected
	None Type = iota
	// X11 indicates X11 display server
	X11
	// Wayland indicates Wayland display server
	Wayland
)

// String returns the string representation of the display type
func (t Type) String() string {
	switch t {
	case X11:
		return "X11"
	case Wayland:
		return "Wayland"
	default:
		return "None"
	}
}

// Detect determines which display server is in use on the host
func Detect() Type {
	// Check for Wayland first (it's more modern and sometimes X11 vars are set for XWayland)
	waylandDisplay := os.Getenv("WAYLAND_DISPLAY")
	if waylandDisplay != "" {
		return Wayland
	}

	// Check for X11
	display := os.Getenv("DISPLAY")
	if display != "" {
		return X11
	}

	return None
}

// GetX11Display returns the X11 display number (e.g., "0" from ":0")
func GetX11Display() string {
	display := os.Getenv("DISPLAY")
	if display == "" {
		return "0"
	}
	// Remove the leading colon if present
	if len(display) > 0 && display[0] == ':' {
		display = display[1:]
	}
	// Handle display.screen format (e.g., "0.0")
	for i, c := range display {
		if c == '.' {
			return display[:i]
		}
	}
	return display
}

// GetWaylandDisplay returns the Wayland display socket name
func GetWaylandDisplay() string {
	display := os.Getenv("WAYLAND_DISPLAY")
	if display == "" {
		return "wayland-0"
	}
	return display
}

// GetXDGRuntimeDir returns the XDG_RUNTIME_DIR path
func GetXDGRuntimeDir() string {
	dir := os.Getenv("XDG_RUNTIME_DIR")
	if dir == "" {
		// Fall back to common default
		return "/run/user/" + os.Getenv("UID")
	}
	return dir
}
