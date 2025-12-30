package incus

import (
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"strings"
	"time"
)

// Client wraps incus CLI commands
type Client struct{}

// NewClient creates a new incus client
func NewClient() *Client {
	return &Client{}
}

// InstanceExists checks if an instance with the given name exists
func (c *Client) InstanceExists(name string) (bool, error) {
	cmd := exec.Command("incus", "list", "--format=json", name)
	output, err := cmd.Output()
	if err != nil {
		// If incus is not installed or other error
		if exitErr, ok := err.(*exec.ExitError); ok {
			return false, fmt.Errorf("incus command failed: %s", string(exitErr.Stderr))
		}
		return false, err
	}

	var instances []map[string]interface{}
	if err := json.Unmarshal(output, &instances); err != nil {
		return false, fmt.Errorf("failed to parse incus output: %w", err)
	}

	return len(instances) > 0, nil
}

// IsRunning checks if an instance is currently running
func (c *Client) IsRunning(name string) (bool, error) {
	cmd := exec.Command("incus", "list", "--format=json", name)
	output, err := cmd.Output()
	if err != nil {
		return false, err
	}

	var instances []struct {
		Status string `json:"status"`
	}
	if err := json.Unmarshal(output, &instances); err != nil {
		return false, err
	}

	if len(instances) == 0 {
		return false, nil
	}

	return instances[0].Status == "Running", nil
}

// Create creates a new instance with cloud-init configuration
func (c *Client) Create(name, image, cloudInit string) error {
	args := []string{"init", image, name}

	if cloudInit != "" {
		args = append(args, "--config", "cloud-init.user-data="+cloudInit)
	}

	cmd := exec.Command("incus", args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	return cmd.Run()
}

// Start starts an instance
func (c *Client) Start(name string) error {
	cmd := exec.Command("incus", "start", name)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

// Stop stops an instance
func (c *Client) Stop(name string) error {
	cmd := exec.Command("incus", "stop", name)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

// Delete deletes an instance
func (c *Client) Delete(name string, force bool) error {
	args := []string{"delete", name}
	if force {
		args = append(args, "--force")
	}

	cmd := exec.Command("incus", args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

// AddDiskDevice adds a disk device (mount) to an instance
func (c *Client) AddDiskDevice(name, deviceName, source, path string) error {
	cmd := exec.Command("incus", "config", "device", "add", name, deviceName, "disk",
		"source="+source,
		"path="+path,
		"shift=true",
	)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

// AddProxyDevice adds a proxy device for socket passthrough
func (c *Client) AddProxyDevice(name, deviceName, connect, listen string, uid, gid int) error {
	cmd := exec.Command("incus", "config", "device", "add", name, deviceName, "proxy",
		"connect="+connect,
		"listen="+listen,
		"bind=instance",
		fmt.Sprintf("uid=%d", uid),
		fmt.Sprintf("gid=%d", gid),
		fmt.Sprintf("security.uid=%d", uid),
		fmt.Sprintf("security.gid=%d", gid),
	)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

// AddSimpleProxyDevice adds a proxy device for file-based sockets with proper permissions
func (c *Client) AddSimpleProxyDevice(name, deviceName, connect, listen string, uid, gid int) error {
	cmd := exec.Command("incus", "config", "device", "add", name, deviceName, "proxy",
		"connect="+connect,
		"listen="+listen,
		"bind=instance",
		fmt.Sprintf("uid=%d", uid),
		fmt.Sprintf("gid=%d", gid),
		"mode=0777",
	)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

// AddGPUDevice adds a GPU device to an instance
func (c *Client) AddGPUDevice(name string) error {
	cmd := exec.Command("incus", "config", "device", "add", name, "gpu", "gpu")
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

// RemoveDevice removes a device from an instance
func (c *Client) RemoveDevice(name, deviceName string) error {
	cmd := exec.Command("incus", "config", "device", "remove", name, deviceName)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

// DeviceExists checks if a device exists on an instance
func (c *Client) DeviceExists(name, deviceName string) (bool, error) {
	cmd := exec.Command("incus", "config", "device", "show", name)
	output, err := cmd.Output()
	if err != nil {
		return false, fmt.Errorf("failed to list devices: %w", err)
	}

	// Simple check if device name appears in output
	return strings.Contains(string(output), deviceName+":"), nil
}

// GetDeviceSource gets the source path of a disk device
func (c *Client) GetDeviceSource(name, deviceName string) (string, error) {
	cmd := exec.Command("incus", "config", "device", "get", name, deviceName, "source")
	output, err := cmd.Output()
	if err != nil {
		return "", fmt.Errorf("failed to get device source: %w", err)
	}
	return strings.TrimSpace(string(output)), nil
}

// UpdateXauthority updates the xauthority device mount if the source file has changed
// This is necessary because XWayland can create new Xauthority files when restarted
func (c *Client) UpdateXauthority(name string) error {
	// Get current Xauthority file from environment
	xauthFile := os.Getenv("XAUTHORITY")
	if xauthFile == "" {
		xauthFile = os.Getenv("HOME") + "/.Xauthority"
	}

	// Check if the file exists
	if _, err := os.Stat(xauthFile); os.IsNotExist(err) {
		// No xauthority file available, nothing to update
		return nil
	}

	// Check if xauthority device exists
	deviceExists, err := c.DeviceExists(name, "xauthority")
	if err != nil {
		return fmt.Errorf("failed to check xauthority device: %w", err)
	}

	username := os.Getenv("USER")
	xauthPath := fmt.Sprintf("/home/%s/.Xauthority", username)

	if !deviceExists {
		// Device doesn't exist, add it
		return c.AddDiskDevice(name, "xauthority", xauthFile, xauthPath)
	}

	// Device exists, check if source has changed
	currentSource, err := c.GetDeviceSource(name, "xauthority")
	if err != nil {
		return fmt.Errorf("failed to get current xauthority source: %w", err)
	}

	if currentSource != xauthFile {
		// Source has changed, remove and re-add the device
		if err := c.RemoveDevice(name, "xauthority"); err != nil {
			return fmt.Errorf("failed to remove old xauthority device: %w", err)
		}

		if err := c.AddDiskDevice(name, "xauthority", xauthFile, xauthPath); err != nil {
			return fmt.Errorf("failed to add new xauthority device: %w", err)
		}
	}

	return nil
}

// SetConfig sets a configuration option on an instance
func (c *Client) SetConfig(name, key, value string) error {
	cmd := exec.Command("incus", "config", "set", name, key+"="+value)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

// Exec runs a command in an instance
func (c *Client) Exec(name string, command ...string) error {
	args := append([]string{"exec", name, "--"}, command...)
	cmd := exec.Command("incus", args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

// ExecAsRoot runs a command in an instance as root
func (c *Client) ExecAsRoot(name string, command ...string) error {
	args := append([]string{"exec", name, "--"}, command...)
	cmd := exec.Command("incus", args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

// ExecAsUser runs a command in an instance as a specific user
func (c *Client) ExecAsUser(name, username string, command ...string) error {
	uid := os.Getuid()
	gid := os.Getgid()

	args := []string{
		"exec", name,
		"--user", fmt.Sprintf("%d", uid),
		"--group", fmt.Sprintf("%d", gid),
		"--env", "HOME=/home/" + username,
		"--env", "USER=" + username,
		"--",
	}
	args = append(args, command...)
	cmd := exec.Command("incus", args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

// ExecInteractive runs an interactive shell in an instance
func (c *Client) ExecInteractive(name, username, workDir string) error {
	uid := os.Getuid()
	gid := os.Getgid()

	args := []string{
		"exec", name,
		"--user", fmt.Sprintf("%d", uid),
		"--group", fmt.Sprintf("%d", gid),
		"--cwd", workDir,
		"--env", "HOME=/home/" + username,
		"--env", "USER=" + username,
		"--env", "XAUTHORITY=/home/" + username + "/.Xauthority",
		"--", "/bin/bash", "--login", "-i",
	}

	cmd := exec.Command("incus", args...)
	cmd.Stdin = os.Stdin
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	// incus handles TTY allocation itself, no special SysProcAttr needed
	return cmd.Run()
}

// WaitForCloudInit waits for cloud-init to complete in the instance
func (c *Client) WaitForCloudInit(name string) error {
	// Poll for cloud-init status with timeout
	timeout := time.After(5 * time.Minute)
	ticker := time.NewTicker(2 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-timeout:
			return fmt.Errorf("timeout waiting for cloud-init")
		case <-ticker.C:
			cmd := exec.Command("incus", "exec", name, "--", "cloud-init", "status")
			output, err := cmd.Output()
			if err != nil {
				// cloud-init might not be ready yet
				continue
			}

			status := strings.TrimSpace(string(output))
			if strings.Contains(status, "done") {
				return nil
			}
			if strings.Contains(status, "error") {
				return fmt.Errorf("cloud-init reported error: %s", status)
			}
			// Still running, continue waiting
		}
	}
}
