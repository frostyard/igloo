package config

import (
	"fmt"
	"slices"
)

// SupportedDistros maps distribution names to their supported releases
var SupportedDistros = map[string][]string{
	"ubuntu":    {"questing", "plucky", "noble", "jammy", "focal"},
	"debian":    {"trixie", "bookworm", "bullseye"},
	"fedora":    {"43", "42", "41", "40", "39"},
	"archlinux": {"current"},
}

// DefaultRelease returns the default (latest) release for a distro
var DefaultRelease = map[string]string{
	"ubuntu":    "questing",
	"debian":    "trixie",
	"fedora":    "43",
	"archlinux": "current",
}

// ValidateDistro checks if the distro and release combination is supported
func ValidateDistro(distro, release string) error {
	releases, ok := SupportedDistros[distro]
	if !ok {
		return fmt.Errorf("unsupported distribution: %s\nSupported: ubuntu, debian, fedora, archlinux", distro)
	}

	if !slices.Contains(releases, release) {
		return fmt.Errorf("unsupported release '%s' for %s\nSupported releases: %v", release, distro, releases)
	}

	return nil
}

// GetDefaultRelease returns the default release for a distro
func GetDefaultRelease(distro string) string {
	if release, ok := DefaultRelease[distro]; ok {
		return release
	}
	return ""
}

// IsDistroSupported checks if a distro name is in our supported list
func IsDistroSupported(distro string) bool {
	_, ok := SupportedDistros[distro]
	return ok
}
