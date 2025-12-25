package config

import (
	"os"
	"path/filepath"
	"testing"
)

// TestParseOSRelease tests OS detection with mock os-release files
func TestParseOSRelease(t *testing.T) {
	tests := []struct {
		name        string
		content     string
		wantDistro  string
		wantRelease string
	}{
		{
			name: "ubuntu questing",
			content: `NAME="Ubuntu"
VERSION="25.10 (Questing Quokka)"
ID=ubuntu
VERSION_CODENAME=questing
PRETTY_NAME="Ubuntu 25.10"
`,
			wantDistro:  "ubuntu",
			wantRelease: "questing",
		},
		{
			name: "debian trixie",
			content: `PRETTY_NAME="Debian GNU/Linux 13 (trixie)"
NAME="Debian GNU/Linux"
VERSION_ID="13"
VERSION="13 (trixie)"
VERSION_CODENAME=trixie
ID=debian
`,
			wantDistro:  "debian",
			wantRelease: "trixie",
		},
		{
			name: "fedora 43",
			content: `NAME="Fedora Linux"
VERSION="43 (Workstation Edition)"
ID=fedora
VERSION_ID=43
PRETTY_NAME="Fedora Linux 43 (Workstation Edition)"
`,
			wantDistro:  "fedora",
			wantRelease: "43",
		},
		{
			name: "archlinux",
			content: `NAME="Arch Linux"
PRETTY_NAME="Arch Linux"
ID=archlinux
BUILD_ID=rolling
`,
			wantDistro:  "archlinux",
			wantRelease: "current",
		},
		{
			name: "unsupported distro falls back",
			content: `NAME="Gentoo"
ID=gentoo
VERSION_CODENAME=latest
`,
			wantDistro:  "ubuntu",
			wantRelease: "questing",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// Create a temp file with os-release content
			tmpDir := t.TempDir()
			osReleasePath := filepath.Join(tmpDir, "os-release")
			if err := os.WriteFile(osReleasePath, []byte(tt.content), 0644); err != nil {
				t.Fatalf("failed to write os-release: %v", err)
			}

			// Parse it using our helper
			distro, release := parseOSReleaseFile(osReleasePath)

			if distro != tt.wantDistro {
				t.Errorf("distro = %q, want %q", distro, tt.wantDistro)
			}
			if release != tt.wantRelease {
				t.Errorf("release = %q, want %q", release, tt.wantRelease)
			}
		})
	}
}

func TestParseOSRelease_FileNotFound(t *testing.T) {
	distro, release := parseOSReleaseFile("/nonexistent/os-release")
	if distro != "ubuntu" || release != "questing" {
		t.Errorf("expected fallback (ubuntu, questing), got (%q, %q)", distro, release)
	}
}

// parseOSReleaseFile is a testable version of DetectHostOS that accepts a custom path
func parseOSReleaseFile(path string) (distro, release string) {
	// Default fallback
	defaultDistro := "ubuntu"
	defaultRelease := "questing"

	content, err := os.ReadFile(path)
	if err != nil {
		return defaultDistro, defaultRelease
	}

	osInfo := make(map[string]string)

	for _, line := range splitLines(string(content)) {
		if len(line) == 0 {
			continue
		}
		parts := splitN(line, "=", 2)
		if len(parts) == 2 {
			key := parts[0]
			value := trim(parts[1], "\"")
			osInfo[key] = value
		}
	}

	// Get distribution ID
	distro = toLower(osInfo["ID"])

	// Check if it's a supported distro
	if !IsDistroSupported(distro) {
		return defaultDistro, defaultRelease
	}

	// Get version/release
	switch distro {
	case "fedora":
		release = osInfo["VERSION_ID"]
	case "archlinux":
		release = "current"
	default:
		release = toLower(osInfo["VERSION_CODENAME"])
	}

	// Validate the release is supported
	if err := ValidateDistro(distro, release); err != nil {
		release = GetDefaultRelease(distro)
	}

	return distro, release
}

// Helper functions to avoid importing strings in test
func splitLines(s string) []string {
	var lines []string
	start := 0
	for i := 0; i < len(s); i++ {
		if s[i] == '\n' {
			lines = append(lines, s[start:i])
			start = i + 1
		}
	}
	if start < len(s) {
		lines = append(lines, s[start:])
	}
	return lines
}

func splitN(s, sep string, n int) []string {
	if n <= 0 {
		return nil
	}
	idx := -1
	for i := 0; i <= len(s)-len(sep); i++ {
		if s[i:i+len(sep)] == sep {
			idx = i
			break
		}
	}
	if idx < 0 {
		return []string{s}
	}
	return []string{s[:idx], s[idx+len(sep):]}
}

func trim(s, cutset string) string {
	for len(s) > 0 && containsChar(cutset, s[0]) {
		s = s[1:]
	}
	for len(s) > 0 && containsChar(cutset, s[len(s)-1]) {
		s = s[:len(s)-1]
	}
	return s
}

func containsChar(s string, c byte) bool {
	for i := 0; i < len(s); i++ {
		if s[i] == c {
			return true
		}
	}
	return false
}

func toLower(s string) string {
	b := make([]byte, len(s))
	for i := 0; i < len(s); i++ {
		c := s[i]
		if c >= 'A' && c <= 'Z' {
			c += 'a' - 'A'
		}
		b[i] = c
	}
	return string(b)
}
