package incus

import (
	"strings"
	"testing"
)

func TestGenerateCloudInit(t *testing.T) {
	result, err := GenerateCloudInit()
	if err != nil {
		t.Fatalf("GenerateCloudInit() failed: %v", err)
	}

	if !strings.HasPrefix(result, "#cloud-config") {
		t.Error("should start with #cloud-config")
	}
	if !strings.Contains(result, "users:") {
		t.Error("should contain users section")
	}
	if !strings.Contains(result, "runcmd:") {
		t.Error("should contain runcmd section")
	}
	if !strings.Contains(result, "timezone:") {
		t.Error("should contain timezone")
	}
	// Should NOT contain packages section (packages come from .igloo.sh now)
	if strings.Contains(result, "packages:") {
		t.Error("should not contain packages section")
	}
}
