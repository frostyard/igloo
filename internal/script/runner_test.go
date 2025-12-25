package script

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/frostyard/igloo/internal/config"
)

func TestNewRunner(t *testing.T) {
	runner := NewRunner(nil, "test-instance", "testuser", "myproject", "/tmp/test")

	if runner.instance != "test-instance" {
		t.Errorf("instance = %q, want %q", runner.instance, "test-instance")
	}
	if runner.username != "testuser" {
		t.Errorf("username = %q, want %q", runner.username, "testuser")
	}
	if runner.projectName != "myproject" {
		t.Errorf("projectName = %q, want %q", runner.projectName, "myproject")
	}
	if runner.projectDir != "/tmp/test" {
		t.Errorf("projectDir = %q, want %q", runner.projectDir, "/tmp/test")
	}
}

func TestGetScripts_NoDirectory(t *testing.T) {
	// Create a temp directory without .igloo/scripts
	tmpDir := t.TempDir()
	runner := NewRunner(nil, "test", "user", "proj", tmpDir)

	scripts, err := runner.GetScripts()
	if err != nil {
		t.Errorf("GetScripts() error = %v, want nil", err)
	}
	if len(scripts) != 0 {
		t.Errorf("GetScripts() = %v, want empty slice", scripts)
	}
}

func TestGetScripts_EmptyDirectory(t *testing.T) {
	// Create a temp directory with empty .igloo/scripts
	tmpDir := t.TempDir()
	scriptsDir := filepath.Join(tmpDir, config.ScriptsPath())
	if err := os.MkdirAll(scriptsDir, 0755); err != nil {
		t.Fatal(err)
	}

	runner := NewRunner(nil, "test", "user", "proj", tmpDir)

	scripts, err := runner.GetScripts()
	if err != nil {
		t.Errorf("GetScripts() error = %v, want nil", err)
	}
	if len(scripts) != 0 {
		t.Errorf("GetScripts() = %v, want empty slice", scripts)
	}
}

func TestGetScripts_WithScripts(t *testing.T) {
	// Create a temp directory with some scripts
	tmpDir := t.TempDir()
	scriptsDir := filepath.Join(tmpDir, config.ScriptsPath())
	if err := os.MkdirAll(scriptsDir, 0755); err != nil {
		t.Fatal(err)
	}

	// Create some script files (out of order to test sorting)
	scriptFiles := []string{"03-third.sh", "01-first.sh", "02-second.sh"}
	for _, name := range scriptFiles {
		f, err := os.Create(filepath.Join(scriptsDir, name))
		if err != nil {
			t.Fatal(err)
		}
		if err := f.Close(); err != nil {
			t.Fatal(err)
		}
	}

	runner := NewRunner(nil, "test", "user", "proj", tmpDir)

	scripts, err := runner.GetScripts()
	if err != nil {
		t.Errorf("GetScripts() error = %v, want nil", err)
	}

	// Should be sorted lexicographically
	expected := []string{"01-first.sh", "02-second.sh", "03-third.sh"}
	if len(scripts) != len(expected) {
		t.Errorf("GetScripts() returned %d scripts, want %d", len(scripts), len(expected))
	}
	for i, s := range scripts {
		if s != expected[i] {
			t.Errorf("scripts[%d] = %q, want %q", i, s, expected[i])
		}
	}
}

func TestGetScripts_SkipsDirectories(t *testing.T) {
	// Create a temp directory with scripts and a subdirectory
	tmpDir := t.TempDir()
	scriptsDir := filepath.Join(tmpDir, config.ScriptsPath())
	if err := os.MkdirAll(scriptsDir, 0755); err != nil {
		t.Fatal(err)
	}

	// Create a script file
	f, err := os.Create(filepath.Join(scriptsDir, "01-init.sh"))
	if err != nil {
		t.Fatal(err)
	}
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}

	// Create a subdirectory (should be skipped)
	if err := os.Mkdir(filepath.Join(scriptsDir, "subdir"), 0755); err != nil {
		t.Fatal(err)
	}

	runner := NewRunner(nil, "test", "user", "proj", tmpDir)

	scripts, err := runner.GetScripts()
	if err != nil {
		t.Errorf("GetScripts() error = %v, want nil", err)
	}
	if len(scripts) != 1 {
		t.Errorf("GetScripts() = %v, want 1 script", scripts)
	}
	if scripts[0] != "01-init.sh" {
		t.Errorf("scripts[0] = %q, want %q", scripts[0], "01-init.sh")
	}
}

func TestScriptPathResolution(t *testing.T) {
	tests := []struct {
		name        string
		scriptPath  string
		username    string
		projectName string
		wantPath    string
	}{
		{
			name:        "relative path",
			scriptPath:  "setup.sh",
			username:    "bjk",
			projectName: "igloo",
			wantPath:    "/home/bjk/workspace/igloo/setup.sh",
		},
		{
			name:        "relative path with subdirectory",
			scriptPath:  "scripts/init.sh",
			username:    "bjk",
			projectName: "igloo",
			wantPath:    "/home/bjk/workspace/igloo/scripts/init.sh",
		},
		{
			name:        "absolute path",
			scriptPath:  "/opt/scripts/setup.sh",
			username:    "bjk",
			projectName: "igloo",
			wantPath:    "/opt/scripts/setup.sh",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			workspacePath := "/home/" + tt.username + "/workspace/" + tt.projectName

			var fullScriptPath string
			if filepath.IsAbs(tt.scriptPath) {
				fullScriptPath = tt.scriptPath
			} else {
				fullScriptPath = filepath.Join(workspacePath, tt.scriptPath)
			}

			if fullScriptPath != tt.wantPath {
				t.Errorf("script path = %q, want %q", fullScriptPath, tt.wantPath)
			}
		})
	}
}

func TestRunnerFields(t *testing.T) {
	runner := &Runner{
		client:      nil,
		instance:    "my-igloo",
		username:    "developer",
		projectName: "myapp",
		projectDir:  "/home/dev/projects/myapp",
	}

	// Verify all fields are set correctly
	if runner.instance != "my-igloo" {
		t.Errorf("instance = %q, want %q", runner.instance, "my-igloo")
	}
	if runner.username != "developer" {
		t.Errorf("username = %q, want %q", runner.username, "developer")
	}
	if runner.projectName != "myapp" {
		t.Errorf("projectName = %q, want %q", runner.projectName, "myapp")
	}
	if runner.projectDir != "/home/dev/projects/myapp" {
		t.Errorf("projectDir = %q, want %q", runner.projectDir, "/home/dev/projects/myapp")
	}
}
