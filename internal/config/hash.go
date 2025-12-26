package config

import (
	"crypto/sha256"
	"encoding/hex"
	"io"
	"os"
	"path/filepath"
	"sort"
)

// GetDataDir returns the XDG data directory for igloo
// Uses $XDG_DATA_HOME/igloo or ~/.local/share/igloo
func GetDataDir() string {
	dataHome := os.Getenv("XDG_DATA_HOME")
	if dataHome == "" {
		home := os.Getenv("HOME")
		dataHome = filepath.Join(home, ".local", "share")
	}
	return filepath.Join(dataHome, "igloo")
}

// HashConfigDir computes a SHA256 hash of all files in the .igloo directory
func HashConfigDir() (string, error) {
	return hashDir(ConfigDir)
}

// hashDir computes a SHA256 hash of all files in a directory
func hashDir(dir string) (string, error) {
	h := sha256.New()

	// Walk the config directory and hash all file contents
	err := filepath.Walk(dir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}

		// Hash the relative path for structure
		relPath, err := filepath.Rel(dir, path)
		if err != nil {
			return err
		}

		// Skip directories, just hash their names for structure
		if info.IsDir() {
			h.Write([]byte("dir:" + relPath + "\n"))
			return nil
		}

		// Hash the relative path and file contents
		h.Write([]byte("file:" + relPath + "\n"))

		// Read and hash file contents
		f, err := os.Open(path)
		if err != nil {
			return err
		}

		if _, err := io.Copy(h, f); err != nil {
			if closeErr := f.Close(); closeErr != nil {
				return closeErr
			}
			return err
		}

		if err := f.Close(); err != nil {
			return err
		}

		return nil
	})

	if err != nil {
		return "", err
	}

	return hex.EncodeToString(h.Sum(nil)), nil
}

// GetStoredHash retrieves the stored hash for a container
func GetStoredHash(containerName string) (string, error) {
	hashFile := filepath.Join(GetDataDir(), containerName+".hash")
	data, err := os.ReadFile(hashFile)
	if err != nil {
		if os.IsNotExist(err) {
			return "", nil // No stored hash yet
		}
		return "", err
	}
	return string(data), nil
}

// StoreHash saves the hash for a container
func StoreHash(containerName, hash string) error {
	dataDir := GetDataDir()
	if err := os.MkdirAll(dataDir, 0755); err != nil {
		return err
	}

	hashFile := filepath.Join(dataDir, containerName+".hash")
	return os.WriteFile(hashFile, []byte(hash), 0644)
}

// RemoveStoredHash deletes the stored hash for a container
func RemoveStoredHash(containerName string) error {
	hashFile := filepath.Join(GetDataDir(), containerName+".hash")
	err := os.Remove(hashFile)
	if os.IsNotExist(err) {
		return nil // Already gone
	}
	return err
}

// ConfigChanged checks if the .igloo directory has changed since last provision
// Returns (changed, currentHash, error)
func ConfigChanged(containerName string) (bool, string, error) {
	currentHash, err := HashConfigDir()
	if err != nil {
		return false, "", err
	}

	storedHash, err := GetStoredHash(containerName)
	if err != nil {
		return false, currentHash, err
	}

	// If no stored hash, this is first run - not "changed"
	if storedHash == "" {
		return false, currentHash, nil
	}

	return currentHash != storedHash, currentHash, nil
}

// ListStoredHashes returns all container names that have stored hashes
func ListStoredHashes() ([]string, error) {
	dataDir := GetDataDir()
	entries, err := os.ReadDir(dataDir)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil
		}
		return nil, err
	}

	var containers []string
	for _, entry := range entries {
		if !entry.IsDir() && filepath.Ext(entry.Name()) == ".hash" {
			name := entry.Name()
			containers = append(containers, name[:len(name)-5]) // Remove .hash suffix
		}
	}

	sort.Strings(containers)
	return containers, nil
}
