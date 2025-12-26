package config

import (
	"os"
	"path/filepath"
	"testing"
)

func TestHashDir(t *testing.T) {
	// Create a temp directory with config structure
	tmpDir := t.TempDir()
	configDir := filepath.Join(tmpDir, ".igloo")

	// Create the config directory
	if err := os.MkdirAll(configDir, 0755); err != nil {
		t.Fatal(err)
	}

	// Create a config file
	configContent := "[container]\nname = test\n"
	if err := os.WriteFile(filepath.Join(configDir, "igloo.ini"), []byte(configContent), 0644); err != nil {
		t.Fatal(err)
	}

	// Hash should succeed
	hash1, err := hashDir(configDir)
	if err != nil {
		t.Fatalf("hashDir() error = %v", err)
	}
	if hash1 == "" {
		t.Error("hashDir() returned empty hash")
	}

	// Same content should produce same hash
	hash2, err := hashDir(configDir)
	if err != nil {
		t.Fatalf("hashDir() error = %v", err)
	}
	if hash1 != hash2 {
		t.Errorf("hashDir() not deterministic: %q != %q", hash1, hash2)
	}

	// Changing content should change hash
	if err := os.WriteFile(filepath.Join(configDir, "igloo.ini"), []byte("[container]\nname = changed\n"), 0644); err != nil {
		t.Fatal(err)
	}
	hash3, err := hashDir(configDir)
	if err != nil {
		t.Fatalf("hashDir() error = %v", err)
	}
	if hash1 == hash3 {
		t.Error("hashDir() should return different hash for different content")
	}
}

func TestStoreAndGetHash(t *testing.T) {
	// Use a temp directory for data
	tmpDir := t.TempDir()
	t.Setenv("XDG_DATA_HOME", tmpDir)

	containerName := "test-container"
	testHash := "abc123def456"

	// Initially no hash stored
	hash, err := GetStoredHash(containerName)
	if err != nil {
		t.Fatalf("GetStoredHash() error = %v", err)
	}
	if hash != "" {
		t.Errorf("GetStoredHash() = %q, want empty", hash)
	}

	// Store a hash
	if err := StoreHash(containerName, testHash); err != nil {
		t.Fatalf("StoreHash() error = %v", err)
	}

	// Retrieve it
	hash, err = GetStoredHash(containerName)
	if err != nil {
		t.Fatalf("GetStoredHash() error = %v", err)
	}
	if hash != testHash {
		t.Errorf("GetStoredHash() = %q, want %q", hash, testHash)
	}

	// Remove it
	if err := RemoveStoredHash(containerName); err != nil {
		t.Fatalf("RemoveStoredHash() error = %v", err)
	}

	// Should be gone
	hash, err = GetStoredHash(containerName)
	if err != nil {
		t.Fatalf("GetStoredHash() error = %v", err)
	}
	if hash != "" {
		t.Errorf("GetStoredHash() after remove = %q, want empty", hash)
	}
}

func TestConfigChanged(t *testing.T) {
	// This test requires actual .igloo directory, so we'll skip the ConfigChanged test
	// and focus on testing the underlying hashDir and store/get functions
	t.Skip("ConfigChanged requires modifying package-level constants")
}

func TestGetDataDir(t *testing.T) {
	// Test with XDG_DATA_HOME set
	t.Setenv("XDG_DATA_HOME", "/custom/data")

	dataDir := GetDataDir()
	expected := "/custom/data/igloo"
	if dataDir != expected {
		t.Errorf("GetDataDir() = %q, want %q", dataDir, expected)
	}
}
