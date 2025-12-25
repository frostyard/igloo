package config

import (
	"bufio"
	"os"
	"strings"
)

// DetectHostOS reads /etc/os-release and returns the distro and release
// Falls back to ubuntu/questing if detection fails or OS is not supported
func DetectHostOS() (distro, release string) {
	// Default fallback
	defaultDistro := "ubuntu"
	defaultRelease := "questing"

	file, err := os.Open("/etc/os-release")
	if err != nil {
		return defaultDistro, defaultRelease
	}
	defer func() { _ = file.Close() }()

	osInfo := make(map[string]string)
	scanner := bufio.NewScanner(file)

	for scanner.Scan() {
		line := scanner.Text()
		parts := strings.SplitN(line, "=", 2)
		if len(parts) == 2 {
			key := parts[0]
			value := strings.Trim(parts[1], "\"")
			osInfo[key] = value
		}
	}

	// Get distribution ID
	distro = strings.ToLower(osInfo["ID"])

	// Check if it's a supported distro
	if !IsDistroSupported(distro) {
		return defaultDistro, defaultRelease
	}

	// Get version/release
	// Fedora uses VERSION_ID (numeric), others use VERSION_CODENAME
	switch distro {
	case "fedora":
		release = osInfo["VERSION_ID"]
	case "archlinux":
		release = "current"
	default:
		release = strings.ToLower(osInfo["VERSION_CODENAME"])
	}

	// Validate the release is supported
	if err := ValidateDistro(distro, release); err != nil {
		// Distro is supported but release isn't - use default release for this distro
		release = GetDefaultRelease(distro)
	}

	return distro, release
}
