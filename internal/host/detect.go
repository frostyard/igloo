package host

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// Known distro families that have Incus cloud images.
var knownDistros = map[string]bool{
	"debian":    true,
	"ubuntu":    true,
	"fedora":    true,
	"archlinux": true,
}

// Distros that use VERSION_ID instead of VERSION_CODENAME.
var usesVersionID = map[string]bool{
	"fedora": true,
}

// Distros that have a fixed version string.
var fixedVersion = map[string]string{
	"archlinux": "current",
}

// OSInfo holds the detected host OS information.
type OSInfo struct {
	ID      string // distro family (debian, ubuntu, etc.)
	Version string // release codename or version number
}

// Image returns the Incus image string for this OS.
func (o OSInfo) Image() string {
	return fmt.Sprintf("images:%s/%s/cloud", o.ID, o.Version)
}

// DetectOS reads /etc/os-release and returns OS info with ID_LIKE fallback.
func DetectOS() OSInfo {
	return detectFromFile("/etc/os-release")
}

// ContainerName derives the igloo container name from a project directory path.
func ContainerName(projectDir string) string {
	return "igloo-" + filepath.Base(projectDir)
}

func detectFromFile(path string) OSInfo {
	fallback := OSInfo{ID: "debian", Version: "trixie"}

	fields := parseOSRelease(path)
	if len(fields) == 0 {
		return fallback
	}

	id := strings.ToLower(fields["ID"])
	if id == "" {
		return fallback
	}

	// Try the primary ID first, then walk ID_LIKE.
	candidates := []string{id}
	if idLike := fields["ID_LIKE"]; idLike != "" {
		candidates = append(candidates, strings.Fields(idLike)...)
	}

	for _, candidate := range candidates {
		if !knownDistros[candidate] {
			continue
		}
		ver := versionFor(candidate, fields)
		if ver == "" {
			continue
		}
		return OSInfo{ID: candidate, Version: ver}
	}

	return fallback
}

// versionFor returns the version string for a known distro, given os-release fields.
func versionFor(distro string, fields map[string]string) string {
	if v, ok := fixedVersion[distro]; ok {
		return v
	}
	if usesVersionID[distro] {
		return fields["VERSION_ID"]
	}
	// For Ubuntu derivatives: prefer UBUNTU_CODENAME (set by Ubuntu derivatives).
	if distro == "ubuntu" {
		if uc := fields["UBUNTU_CODENAME"]; uc != "" {
			return strings.ToLower(uc)
		}
	}
	return strings.ToLower(fields["VERSION_CODENAME"])
}

func parseOSRelease(path string) map[string]string {
	f, err := os.Open(path)
	if err != nil {
		return nil
	}
	defer func() {
		_ = f.Close()
	}()
	fields := make(map[string]string)
	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := scanner.Text()
		k, v, ok := strings.Cut(line, "=")
		if !ok {
			continue
		}
		fields[k] = strings.Trim(v, "\"")
	}
	return fields
}
