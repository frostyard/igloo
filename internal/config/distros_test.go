package config

import (
	"testing"
)

func TestValidateDistro(t *testing.T) {
	tests := []struct {
		name    string
		distro  string
		release string
		wantErr bool
	}{
		// Valid combinations
		{"ubuntu questing", "ubuntu", "questing", false},
		{"ubuntu noble", "ubuntu", "noble", false},
		{"ubuntu jammy", "ubuntu", "jammy", false},
		{"ubuntu focal", "ubuntu", "focal", false},
		{"debian trixie", "debian", "trixie", false},
		{"debian bookworm", "debian", "bookworm", false},
		{"debian bullseye", "debian", "bullseye", false},
		{"fedora 43", "fedora", "43", false},
		{"fedora 41", "fedora", "41", false},
		{"fedora 40", "fedora", "40", false},
		{"archlinux current", "archlinux", "current", false},

		// Invalid distros
		{"unsupported distro", "gentoo", "latest", true},
		{"empty distro", "", "questing", true},
		{"typo distro", "ubunutu", "questing", true},

		// Invalid releases
		{"ubuntu invalid release", "ubuntu", "bionic", true},
		{"debian invalid release", "debian", "buster", true},
		{"fedora invalid release", "fedora", "38", true},
		{"archlinux invalid release", "archlinux", "rolling", true},
		{"empty release", "ubuntu", "", true},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := ValidateDistro(tt.distro, tt.release)
			if (err != nil) != tt.wantErr {
				t.Errorf("ValidateDistro(%q, %q) error = %v, wantErr %v", tt.distro, tt.release, err, tt.wantErr)
			}
		})
	}
}

func TestGetDefaultRelease(t *testing.T) {
	tests := []struct {
		distro string
		want   string
	}{
		{"ubuntu", "questing"},
		{"debian", "trixie"},
		{"fedora", "43"},
		{"archlinux", "current"},
		{"unknown", ""},
	}

	for _, tt := range tests {
		t.Run(tt.distro, func(t *testing.T) {
			got := GetDefaultRelease(tt.distro)
			if got != tt.want {
				t.Errorf("GetDefaultRelease(%q) = %q, want %q", tt.distro, got, tt.want)
			}
		})
	}
}

func TestIsDistroSupported(t *testing.T) {
	tests := []struct {
		distro string
		want   bool
	}{
		{"ubuntu", true},
		{"debian", true},
		{"fedora", true},
		{"archlinux", true},
		{"gentoo", false},
		{"centos", false},
		{"", false},
		{"Ubuntu", false}, // case sensitive
	}

	for _, tt := range tests {
		t.Run(tt.distro, func(t *testing.T) {
			got := IsDistroSupported(tt.distro)
			if got != tt.want {
				t.Errorf("IsDistroSupported(%q) = %v, want %v", tt.distro, got, tt.want)
			}
		})
	}
}

func TestSupportedDistrosCompleteness(t *testing.T) {
	// Ensure DefaultRelease has entries for all supported distros
	for distro := range SupportedDistros {
		if _, ok := DefaultRelease[distro]; !ok {
			t.Errorf("DefaultRelease missing entry for %q", distro)
		}
	}

	// Ensure DefaultRelease only has entries for supported distros
	for distro := range DefaultRelease {
		if _, ok := SupportedDistros[distro]; !ok {
			t.Errorf("DefaultRelease has entry for unsupported distro %q", distro)
		}
	}

	// Ensure default release is always in the supported releases list
	for distro, defaultRel := range DefaultRelease {
		releases := SupportedDistros[distro]
		found := false
		for _, r := range releases {
			if r == defaultRel {
				found = true
				break
			}
		}
		if !found {
			t.Errorf("DefaultRelease[%q] = %q is not in SupportedDistros[%q]", distro, defaultRel, distro)
		}
	}
}
