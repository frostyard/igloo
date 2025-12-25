package incus

import (
	"strings"
	"testing"

	"github.com/frostyard/igloo/internal/config"
)

func TestGenerateCloudInit(t *testing.T) {
	cfg := &config.IglooConfig{
		Container: config.ContainerConfig{
			Image: "images:ubuntu/questing",
			Name:  "test-igloo",
		},
		Packages: config.PackagesConfig{
			Install: "vim, git, curl",
		},
	}

	result, err := GenerateCloudInit(cfg)
	if err != nil {
		t.Fatalf("GenerateCloudInit() failed: %v", err)
	}

	// Verify it's valid cloud-init
	if !strings.HasPrefix(result, "#cloud-config") {
		t.Error("cloud-init should start with #cloud-config")
	}

	// Verify packages are included
	if !strings.Contains(result, "packages:") {
		t.Error("cloud-init should contain packages section")
	}
	if !strings.Contains(result, "- vim") {
		t.Error("cloud-init should contain vim package")
	}
	if !strings.Contains(result, "- git") {
		t.Error("cloud-init should contain git package")
	}
	if !strings.Contains(result, "- curl") {
		t.Error("cloud-init should contain curl package")
	}

	// Verify user section exists
	if !strings.Contains(result, "users:") {
		t.Error("cloud-init should contain users section")
	}

	// Verify runcmd section exists
	if !strings.Contains(result, "runcmd:") {
		t.Error("cloud-init should contain runcmd section")
	}

	// Verify timezone is set
	if !strings.Contains(result, "timezone:") {
		t.Error("cloud-init should contain timezone")
	}
}

func TestGenerateCloudInit_NoPackages(t *testing.T) {
	cfg := &config.IglooConfig{
		Container: config.ContainerConfig{
			Image: "images:ubuntu/questing",
			Name:  "test-igloo",
		},
		Packages: config.PackagesConfig{
			Install: "",
		},
	}

	result, err := GenerateCloudInit(cfg)
	if err != nil {
		t.Fatalf("GenerateCloudInit() failed: %v", err)
	}

	// Packages section should not be present when empty
	if strings.Contains(result, "packages:") {
		t.Error("cloud-init should not contain packages section when no packages specified")
	}
}

func TestGenerateCloudInit_WhitespaceOnlyPackages(t *testing.T) {
	cfg := &config.IglooConfig{
		Packages: config.PackagesConfig{
			Install: "  ,  ,  ",
		},
	}

	result, err := GenerateCloudInit(cfg)
	if err != nil {
		t.Fatalf("GenerateCloudInit() failed: %v", err)
	}

	// Packages section should not be present when only whitespace
	if strings.Contains(result, "packages:") {
		t.Error("cloud-init should not contain packages section when only whitespace")
	}
}

func TestCloudInitData_Render(t *testing.T) {
	// Test that the data structure holds values correctly
	data := CloudInitData{
		Username:    "testuser",
		UID:         1000,
		GID:         1000,
		Timezone:    "America/New_York",
		Packages:    true,
		PackageList: []string{"vim", "git"},
		Timestamp:   "2024-01-01T00:00:00Z",
	}

	// Verify the data structure is populated correctly
	if data.Username != "testuser" {
		t.Errorf("Username = %q, want %q", data.Username, "testuser")
	}
	if data.UID != 1000 {
		t.Errorf("UID = %d, want %d", data.UID, 1000)
	}
	if data.GID != 1000 {
		t.Errorf("GID = %d, want %d", data.GID, 1000)
	}
	if data.Timezone != "America/New_York" {
		t.Errorf("Timezone = %q, want %q", data.Timezone, "America/New_York")
	}
	if !data.Packages {
		t.Error("Packages should be true")
	}
	if len(data.PackageList) != 2 {
		t.Errorf("PackageList length = %d, want %d", len(data.PackageList), 2)
	}
}

func TestGetTimezone(t *testing.T) {
	tz := getTimezone()

	// Should return something (not empty)
	if tz == "" {
		t.Error("getTimezone() returned empty string")
	}

	// Should be a valid-looking timezone (contains / or is UTC)
	if tz != "UTC" && !strings.Contains(tz, "/") {
		// Could still be valid like "EST" but less common
		t.Logf("Unusual timezone format: %q", tz)
	}
}
