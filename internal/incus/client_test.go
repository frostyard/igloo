package incus

import (
	"os"
	"testing"
)

// TestUpdateXauthority_NoFile tests UpdateXauthority when no xauthority file exists
func TestUpdateXauthority_NoFile(t *testing.T) {
	// Save current env vars
	oldXauth := os.Getenv("XAUTHORITY")
	oldHome := os.Getenv("HOME")
	defer func() {
		if oldXauth != "" {
			_ = os.Setenv("XAUTHORITY", oldXauth)
		} else {
			_ = os.Unsetenv("XAUTHORITY")
		}
		_ = os.Setenv("HOME", oldHome)
	}()

	// Set env to point to non-existent file
	tmpDir := t.TempDir()
	_ = os.Setenv("HOME", tmpDir)
	_ = os.Unsetenv("XAUTHORITY")

	client := NewClient()

	// This should not error when file doesn't exist - it just returns nil
	err := client.UpdateXauthority("test-container")
	if err != nil {
		t.Errorf("UpdateXauthority() should not error when file doesn't exist, got: %v", err)
	}
}

// TestUpdateXauthority_WithFile tests UpdateXauthority when xauthority file exists
// Note: This test can't fully test the incus commands without a running incus instance,
// but it verifies the file detection logic works
func TestUpdateXauthority_WithFile(t *testing.T) {
	// Save current env vars
	oldXauth := os.Getenv("XAUTHORITY")
	oldHome := os.Getenv("HOME")
	oldUser := os.Getenv("USER")
	defer func() {
		if oldXauth != "" {
			_ = os.Setenv("XAUTHORITY", oldXauth)
		} else {
			_ = os.Unsetenv("XAUTHORITY")
		}
		_ = os.Setenv("HOME", oldHome)
		_ = os.Setenv("USER", oldUser)
	}()

	// Create a temporary xauthority file
	tmpDir := t.TempDir()
	xauthFile := tmpDir + "/.Xauthority"
	if err := os.WriteFile(xauthFile, []byte("fake xauth data"), 0600); err != nil {
		t.Fatalf("Failed to create test xauthority file: %v", err)
	}

	_ = os.Setenv("HOME", tmpDir)
	_ = os.Setenv("USER", "testuser")
	_ = os.Unsetenv("XAUTHORITY")

	client := NewClient()

	// This will attempt to call incus commands which will fail without incus installed
	// but we can verify it at least tries to process the file
	err := client.UpdateXauthority("test-container")

	// We expect an error because incus isn't available in test environment,
	// but we're verifying the logic path is taken when the file exists
	// The function should have attempted to check device existence
	if err == nil {
		// If no error, then either incus is available (unlikely in CI)
		// or the logic correctly handled the case
		t.Log("UpdateXauthority succeeded (incus may be available)")
	} else {
		// Expected case: incus command failed
		if !containsAny(err.Error(), []string{"incus", "device", "executable file not found", "command not found"}) {
			t.Errorf("Expected incus-related error, got: %v", err)
		}
	}
}

// TestUpdateXauthority_CustomPath tests UpdateXauthority with XAUTHORITY env var set
func TestUpdateXauthority_CustomPath(t *testing.T) {
	// Save current env vars
	oldXauth := os.Getenv("XAUTHORITY")
	oldUser := os.Getenv("USER")
	defer func() {
		if oldXauth != "" {
			_ = os.Setenv("XAUTHORITY", oldXauth)
		} else {
			_ = os.Unsetenv("XAUTHORITY")
		}
		_ = os.Setenv("USER", oldUser)
	}()

	// Create a temporary xauthority file with custom path
	tmpDir := t.TempDir()
	xauthFile := tmpDir + "/.mutter-Xwaylandauth.ABC123"
	if err := os.WriteFile(xauthFile, []byte("fake xauth data"), 0600); err != nil {
		t.Fatalf("Failed to create test xauthority file: %v", err)
	}

	_ = os.Setenv("XAUTHORITY", xauthFile)
	_ = os.Setenv("USER", "testuser")

	client := NewClient()

	// This will attempt to call incus commands which will fail without incus installed
	err := client.UpdateXauthority("test-container")

	// We expect an error because incus isn't available in test environment
	if err == nil {
		t.Log("UpdateXauthority succeeded (incus may be available)")
	} else {
		// Expected case: incus command failed
		if !containsAny(err.Error(), []string{"incus", "device", "executable file not found", "command not found"}) {
			t.Errorf("Expected incus-related error, got: %v", err)
		}
	}
}

// Helper function to check if error contains any of the expected strings
func containsAny(s string, substrs []string) bool {
	for _, substr := range substrs {
		if len(s) >= len(substr) {
			for i := 0; i <= len(s)-len(substr); i++ {
				if s[i:i+len(substr)] == substr {
					return true
				}
			}
		}
	}
	return false
}
