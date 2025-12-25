package script

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"

	"github.com/frostyard/igloo/internal/config"
	"github.com/frostyard/igloo/internal/incus"
)

// Runner handles script execution in incus instances
type Runner struct {
	client      *incus.Client
	instance    string
	username    string
	projectName string
	projectDir  string
}

// NewRunner creates a new script runner
func NewRunner(client *incus.Client, instance, username, projectName, projectDir string) *Runner {
	return &Runner{
		client:      client,
		instance:    instance,
		username:    username,
		projectName: projectName,
		projectDir:  projectDir,
	}
}

// RunScripts executes all scripts in the .igloo/scripts directory in lexicographical order
func (r *Runner) RunScripts() error {
	scriptsPath := filepath.Join(r.projectDir, config.ScriptsPath())

	// Check if scripts directory exists on the host
	entries, err := os.ReadDir(scriptsPath)
	if err != nil {
		if os.IsNotExist(err) {
			// No scripts directory - nothing to do
			return nil
		}
		return fmt.Errorf("failed to read scripts directory: %w", err)
	}

	// Filter and sort script files
	var scripts []string
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		name := entry.Name()
		// Skip hidden files and non-executable looking files
		if len(name) > 0 && name[0] != '.' {
			scripts = append(scripts, name)
		}
	}

	if len(scripts) == 0 {
		return nil
	}

	// Sort lexicographically
	sort.Strings(scripts)

	// The project directory is mounted at /home/$USER/workspace/$projectName
	workspacePath := fmt.Sprintf("/home/%s/workspace/%s", r.username, r.projectName)
	containerScriptsDir := filepath.Join(workspacePath, config.ScriptsPath())

	// Execute each script in order
	for _, scriptName := range scripts {
		fullScriptPath := filepath.Join(containerScriptsDir, scriptName)

		// Make the script executable
		if err := r.client.ExecAsRoot(r.instance, "chmod", "+x", fullScriptPath); err != nil {
			return fmt.Errorf("failed to make script %s executable: %w", scriptName, err)
		}

		// Execute the script as root
		if err := r.client.ExecAsRoot(r.instance, "/bin/sh", "-c", fullScriptPath); err != nil {
			return fmt.Errorf("script %s failed: %w", scriptName, err)
		}
	}

	return nil
}

// GetScripts returns the list of scripts that would be run, in order
func (r *Runner) GetScripts() ([]string, error) {
	scriptsPath := filepath.Join(r.projectDir, config.ScriptsPath())

	entries, err := os.ReadDir(scriptsPath)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil
		}
		return nil, err
	}

	var scripts []string
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		name := entry.Name()
		if len(name) > 0 && name[0] != '.' {
			scripts = append(scripts, name)
		}
	}

	sort.Strings(scripts)
	return scripts, nil
}
