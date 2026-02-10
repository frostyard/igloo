package host

import (
	"os"
	"path/filepath"
	"testing"
)

func TestDetectOS(t *testing.T) {
	tests := []struct {
		name      string
		content   string
		wantID    string
		wantVer   string
		wantImage string
	}{
		{
			name: "debian trixie",
			content: `ID=debian
VERSION_CODENAME=trixie
`,
			wantID:    "debian",
			wantVer:   "trixie",
			wantImage: "images:debian/trixie/cloud",
		},
		{
			name: "ubuntu noble",
			content: `ID=ubuntu
VERSION_CODENAME=noble
`,
			wantID:    "ubuntu",
			wantVer:   "noble",
			wantImage: "images:ubuntu/noble/cloud",
		},
		{
			name: "pop os falls back via ID_LIKE to ubuntu",
			content: `ID=pop
VERSION_CODENAME=noble
ID_LIKE="ubuntu debian"
`,
			wantID:    "ubuntu",
			wantVer:   "noble",
			wantImage: "images:ubuntu/noble/cloud",
		},
		{
			name: "linuxmint falls back via ID_LIKE to ubuntu",
			content: `ID=linuxmint
VERSION_CODENAME=wilma
ID_LIKE="ubuntu debian"
UBUNTU_CODENAME=noble
`,
			wantID:    "ubuntu",
			wantVer:   "noble",
			wantImage: "images:ubuntu/noble/cloud",
		},
		{
			name: "unknown distro with debian ID_LIKE",
			content: `ID=mxlinux
VERSION_CODENAME=libretto
ID_LIKE="debian"
`,
			wantID:    "debian",
			wantVer:   "libretto",
			wantImage: "images:debian/libretto/cloud",
		},
		{
			name: "completely unknown falls back to debian trixie",
			content: `ID=gentoo
`,
			wantID:    "debian",
			wantVer:   "trixie",
			wantImage: "images:debian/trixie/cloud",
		},
		{
			name:      "missing file falls back to debian trixie",
			content:   "",
			wantID:    "debian",
			wantVer:   "trixie",
			wantImage: "images:debian/trixie/cloud",
		},
		{
			name: "fedora uses VERSION_ID",
			content: `ID=fedora
VERSION_ID=43
`,
			wantID:    "fedora",
			wantVer:   "43",
			wantImage: "images:fedora/43/cloud",
		},
		{
			name: "arch uses current",
			content: `ID=archlinux
`,
			wantID:    "archlinux",
			wantVer:   "current",
			wantImage: "images:archlinux/current/cloud",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			var path string
			if tt.content == "" {
				path = "/nonexistent/os-release"
			} else {
				tmpDir := t.TempDir()
				path = filepath.Join(tmpDir, "os-release")
				if err := os.WriteFile(path, []byte(tt.content), 0644); err != nil {
					t.Fatalf("failed to write os-release: %v", err)
				}
			}

			info := detectFromFile(path)

			if info.ID != tt.wantID {
				t.Errorf("ID = %q, want %q", info.ID, tt.wantID)
			}
			if info.Version != tt.wantVer {
				t.Errorf("Version = %q, want %q", info.Version, tt.wantVer)
			}
			if info.Image() != tt.wantImage {
				t.Errorf("Image() = %q, want %q", info.Image(), tt.wantImage)
			}
		})
	}
}

func TestContainerName(t *testing.T) {
	name := ContainerName("/home/bjk/projects/my-gtk-app")
	if name != "igloo-my-gtk-app" {
		t.Errorf("ContainerName() = %q, want %q", name, "igloo-my-gtk-app")
	}
}
