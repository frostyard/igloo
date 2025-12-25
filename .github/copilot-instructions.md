# Copilot Instructions for Igloo

## Build/Test/Run Workflow

Always run `make lint` as part of every build, test, or run cycle before considering the task complete.
Fix any linting issues that arise to maintain code quality.

Always add unit tests for new features or bug fixes.

## Project Overview

Igloo is a Go-based CLI tool for managing Incus containers as development environments.

## Code Style

- Follow standard Go conventions and idioms
- Use the existing project structure (cmd/ for CLI commands, internal/ for internal packages)
- Error messages should be user-friendly and actionable
