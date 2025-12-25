package config

import (
	"os"
	"path/filepath"
	"testing"
)

func TestLoad(t *testing.T) {
	// Create a temporary config file
	tmpDir := t.TempDir()
	configPath := filepath.Join(tmpDir, "igloo.ini")

	content := `[container]
image = images:ubuntu/questing
name = test-igloo

[packages]
install = vim, git, curl

[mounts]
home = true
project = true

[display]
enabled = true
gpu = false
`

	if err := os.WriteFile(configPath, []byte(content), 0644); err != nil {
		t.Fatalf("failed to write test config: %v", err)
	}

	cfg, err := Load(configPath)
	if err != nil {
		t.Fatalf("Load() failed: %v", err)
	}

	// Verify container section
	if cfg.Container.Image != "images:ubuntu/questing" {
		t.Errorf("Container.Image = %q, want %q", cfg.Container.Image, "images:ubuntu/questing")
	}
	if cfg.Container.Name != "test-igloo" {
		t.Errorf("Container.Name = %q, want %q", cfg.Container.Name, "test-igloo")
	}

	// Verify packages section
	if cfg.Packages.Install != "vim, git, curl" {
		t.Errorf("Packages.Install = %q, want %q", cfg.Packages.Install, "vim, git, curl")
	}

	// Verify mounts section
	if !cfg.Mounts.Home {
		t.Error("Mounts.Home = false, want true")
	}
	if !cfg.Mounts.Project {
		t.Error("Mounts.Project = false, want true")
	}

	// Verify display section
	if !cfg.Display.Enabled {
		t.Error("Display.Enabled = false, want true")
	}
	if cfg.Display.GPU {
		t.Error("Display.GPU = true, want false")
	}

}

func TestLoad_FileNotFound(t *testing.T) {
	_, err := Load("/nonexistent/path/igloo.ini")
	if err == nil {
		t.Error("Load() should fail for nonexistent file")
	}
}

func TestLoad_InvalidINI(t *testing.T) {
	tmpDir := t.TempDir()
	configPath := filepath.Join(tmpDir, "invalid.ini")

	// Write invalid INI content (unclosed section)
	if err := os.WriteFile(configPath, []byte("[container\n"), 0644); err != nil {
		t.Fatalf("failed to write test config: %v", err)
	}

	_, err := Load(configPath)
	if err == nil {
		t.Error("Load() should fail for invalid INI")
	}
}

func TestWrite(t *testing.T) {
	tmpDir := t.TempDir()
	configPath := filepath.Join(tmpDir, "igloo.ini")

	cfg := &IglooConfig{
		Container: ContainerConfig{
			Image: "images:debian/trixie",
			Name:  "my-igloo",
		},
		Packages: PackagesConfig{
			Install: "neovim, tmux",
		},
		Mounts: MountsConfig{
			Home:    true,
			Project: true,
		},
		Display: DisplayConfig{
			Enabled: true,
			GPU:     true,
		},
	}

	if err := Write(configPath, cfg); err != nil {
		t.Fatalf("Write() failed: %v", err)
	}

	// Read it back and verify
	loaded, err := Load(configPath)
	if err != nil {
		t.Fatalf("Load() failed after Write(): %v", err)
	}

	if loaded.Container.Image != cfg.Container.Image {
		t.Errorf("Container.Image = %q, want %q", loaded.Container.Image, cfg.Container.Image)
	}
	if loaded.Container.Name != cfg.Container.Name {
		t.Errorf("Container.Name = %q, want %q", loaded.Container.Name, cfg.Container.Name)
	}
	if loaded.Packages.Install != cfg.Packages.Install {
		t.Errorf("Packages.Install = %q, want %q", loaded.Packages.Install, cfg.Packages.Install)
	}
	if loaded.Mounts.Home != cfg.Mounts.Home {
		t.Errorf("Mounts.Home = %v, want %v", loaded.Mounts.Home, cfg.Mounts.Home)
	}
	if loaded.Mounts.Project != cfg.Mounts.Project {
		t.Errorf("Mounts.Project = %v, want %v", loaded.Mounts.Project, cfg.Mounts.Project)
	}
	if loaded.Display.Enabled != cfg.Display.Enabled {
		t.Errorf("Display.Enabled = %v, want %v", loaded.Display.Enabled, cfg.Display.Enabled)
	}
	if loaded.Display.GPU != cfg.Display.GPU {
		t.Errorf("Display.GPU = %v, want %v", loaded.Display.GPU, cfg.Display.GPU)
	}
}

func TestRemove(t *testing.T) {
	tmpDir := t.TempDir()
	configPath := filepath.Join(tmpDir, "igloo.ini")

	// Create a file to remove
	if err := os.WriteFile(configPath, []byte("[container]\n"), 0644); err != nil {
		t.Fatalf("failed to write test config: %v", err)
	}

	if err := Remove(configPath); err != nil {
		t.Fatalf("Remove() failed: %v", err)
	}

	// Verify file is gone
	if _, err := os.Stat(configPath); !os.IsNotExist(err) {
		t.Error("file should not exist after Remove()")
	}
}

func TestRemove_NonexistentFile(t *testing.T) {
	err := Remove("/nonexistent/path/igloo.ini")
	if err == nil {
		t.Error("Remove() should fail for nonexistent file")
	}
}
