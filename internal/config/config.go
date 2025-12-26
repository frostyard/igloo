package config

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"gopkg.in/ini.v1"
)

// Directory and file paths for igloo configuration
const (
	// ConfigDir is the directory where igloo stores its configuration
	ConfigDir = ".igloo"
	// ConfigFile is the name of the configuration file
	ConfigFile = "igloo.ini"
	// ScriptsDir is the subdirectory within ConfigDir for init scripts
	ScriptsDir = "scripts"
)

// ConfigPath returns the full path to the igloo.ini file
func ConfigPath() string {
	return filepath.Join(ConfigDir, ConfigFile)
}

// ScriptsPath returns the full path to the scripts directory
func ScriptsPath() string {
	return filepath.Join(ConfigDir, ScriptsDir)
}

// TODO: Add support for XDG user config at ~/.config/igloo/config.ini
// This would allow users to set default distro, packages, display settings, etc.
// Project-level .igloo/igloo.ini would override these defaults.

// IglooConfig represents the configuration for an igloo environment
type IglooConfig struct {
	Container ContainerConfig
	Packages  PackagesConfig
	Mounts    MountsConfig
	Display   DisplayConfig
	Symlinks  []string // List of paths to symlink from ~/host/ to ~/
}

// ContainerConfig holds container-specific settings
type ContainerConfig struct {
	Image string `ini:"image"`
	Name  string `ini:"name"`
}

// PackagesConfig holds package installation settings
type PackagesConfig struct {
	Install string `ini:"install"`
}

// MountsConfig holds mount settings
type MountsConfig struct {
	Home    bool `ini:"home"`
	Project bool `ini:"project"`
}

// DisplayConfig holds display passthrough settings
type DisplayConfig struct {
	Enabled bool `ini:"enabled"`
	GPU     bool `ini:"gpu"`
}

// Load reads and parses an igloo.ini file
func Load(path string) (*IglooConfig, error) {
	cfg, err := ini.Load(path)
	if err != nil {
		return nil, fmt.Errorf("failed to load config file: %w", err)
	}

	config := &IglooConfig{}

	// Map sections to struct
	if err := cfg.Section("container").MapTo(&config.Container); err != nil {
		return nil, fmt.Errorf("failed to parse container section: %w", err)
	}

	if err := cfg.Section("packages").MapTo(&config.Packages); err != nil {
		return nil, fmt.Errorf("failed to parse packages section: %w", err)
	}

	if err := cfg.Section("mounts").MapTo(&config.Mounts); err != nil {
		return nil, fmt.Errorf("failed to parse mounts section: %w", err)
	}

	if err := cfg.Section("display").MapTo(&config.Display); err != nil {
		return nil, fmt.Errorf("failed to parse display section: %w", err)
	}

	// Parse symlinks section (comma-separated list)
	symlinksKey := cfg.Section("symlinks").Key("paths")
	if symlinksKey != nil && symlinksKey.String() != "" {
		paths := strings.Split(symlinksKey.String(), ",")
		for _, p := range paths {
			p = strings.TrimSpace(p)
			if p != "" {
				config.Symlinks = append(config.Symlinks, p)
			}
		}
	}

	return config, nil
}

// Write creates an igloo.ini file with the given configuration
func Write(path string, config *IglooConfig) error {
	cfg := ini.Empty()

	// Container section
	containerSec, err := cfg.NewSection("container")
	if err != nil {
		return err
	}
	containerSec.Comment = "Container configuration"
	if _, err := containerSec.NewKey("image", config.Container.Image); err != nil {
		return err
	}
	if _, err := containerSec.NewKey("name", config.Container.Name); err != nil {
		return err
	}

	// Packages section
	packagesSec, err := cfg.NewSection("packages")
	if err != nil {
		return err
	}
	packagesSec.Comment = "Packages to install in the container"
	if _, err := packagesSec.NewKey("install", config.Packages.Install); err != nil {
		return err
	}

	// Mounts section
	mountsSec, err := cfg.NewSection("mounts")
	if err != nil {
		return err
	}
	mountsSec.Comment = "Host directory mounts"
	if _, err := mountsSec.NewKey("home", fmt.Sprintf("%t", config.Mounts.Home)); err != nil {
		return err
	}
	if _, err := mountsSec.NewKey("project", fmt.Sprintf("%t", config.Mounts.Project)); err != nil {
		return err
	}

	// Display section
	displaySec, err := cfg.NewSection("display")
	if err != nil {
		return err
	}
	displaySec.Comment = "Display passthrough settings"
	if _, err := displaySec.NewKey("enabled", fmt.Sprintf("%t", config.Display.Enabled)); err != nil {
		return err
	}
	if _, err := displaySec.NewKey("gpu", fmt.Sprintf("%t", config.Display.GPU)); err != nil {
		return err
	}

	// Symlinks section
	if len(config.Symlinks) > 0 {
		symlinksSec, err := cfg.NewSection("symlinks")
		if err != nil {
			return err
		}
		symlinksSec.Comment = "Symlinks from ~/host/ to ~/ (files/folders that exist on host)"
		if _, err := symlinksSec.NewKey("paths", strings.Join(config.Symlinks, ", ")); err != nil {
			return err
		}
	}

	return cfg.SaveTo(path)
}

// Remove deletes the config file
func Remove(path string) error {
	return os.Remove(path)
}
