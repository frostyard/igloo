package ui

import (
	"github.com/charmbracelet/lipgloss"
)

// Styles provides styled output for igloo CLI
type Styles struct {
	success lipgloss.Style
	info    lipgloss.Style
	warning lipgloss.Style
	err     lipgloss.Style
	header  lipgloss.Style
	label   lipgloss.Style
}

// NewStyles creates a new Styles instance with default colors
func NewStyles() *Styles {
	return &Styles{
		success: lipgloss.NewStyle().
			Foreground(lipgloss.Color("10")). // Green
			Bold(true),
		info: lipgloss.NewStyle().
			Foreground(lipgloss.Color("12")). // Blue
			Bold(false),
		warning: lipgloss.NewStyle().
			Foreground(lipgloss.Color("11")). // Yellow
			Bold(true),
		err: lipgloss.NewStyle().
			Foreground(lipgloss.Color("9")). // Red
			Bold(true),
		header: lipgloss.NewStyle().
			Foreground(lipgloss.Color("13")). // Magenta
			Bold(true).
			Underline(true),
		label: lipgloss.NewStyle().
			Foreground(lipgloss.Color("14")). // Cyan
			Bold(true),
	}
}

// Success returns a success-styled string with a checkmark prefix
func (s *Styles) Success(msg string) string {
	return s.success.Render("✓ " + msg)
}

// Info returns an info-styled string with an arrow prefix
func (s *Styles) Info(msg string) string {
	return s.info.Render("→ " + msg)
}

// Warning returns a warning-styled string with a warning prefix
func (s *Styles) Warning(msg string) string {
	return s.warning.Render("⚠ " + msg)
}

// Error returns an error-styled string with an X prefix
func (s *Styles) Error(msg string) string {
	return s.err.Render("✗ " + msg)
}

// Header returns a header-styled string
func (s *Styles) Header(msg string) string {
	return s.header.Render(msg)
}

// Label returns a label-styled string
func (s *Styles) Label(msg string) string {
	return s.label.Render(msg)
}
