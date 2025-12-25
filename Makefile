.PHONY: build clean install test run help fmt lint deps bump prep man completions release snapshot release-check

BINARY_NAME=igloo
VERSION?=$(shell git describe --tags --always --dirty 2>/dev/null || echo "dev")
LDFLAGS=-ldflags "-X main.version=$(VERSION) -s -w"

help: ## Display this help message
	@echo "Available targets:"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "  %-15s %s\n", $$1, $$2}'

build: ## Build the binary
	@echo "Building $(BINARY_NAME) version $(VERSION)..."
	@go build $(LDFLAGS) -o $(BINARY_NAME) .
	@echo "Build complete: ./$(BINARY_NAME)"

clean: ## Remove build artifacts
	@echo "Cleaning..."
	@rm -f $(BINARY_NAME)
	@go clean

install: build ## Install to /usr/local/bin
	@echo "Installing $(BINARY_NAME) to /usr/local/bin..."
	@sudo cp $(BINARY_NAME) /usr/local/bin/
	@echo "Installed successfully"

test: ## Run tests
	@echo "Running tests..."
	@go test -v ./...


fmt: ## Format code
	@echo "Formatting code..."
	@go fmt ./...

lint: ## Run linter
	@echo "Running linter..."
	@golangci-lint run || echo "golangci-lint not installed, skipping"

run: build ## Build and run
	@./$(BINARY_NAME)

deps: ## Download dependencies
	@echo "Downloading dependencies..."
	@go mod download
	@go mod tidy

prep: deps fmt lint test build ## Format, lint, test, and build

man: ## Generate man pages
	@./scripts/manpages.sh

completions: ## Generate shell completions
	@./scripts/completions.sh

release: ## Run goreleaser to create a release
	@goreleaser release --clean

snapshot: ## Run goreleaser snapshot (no publish)
	@goreleaser release --snapshot --clean

release-check: ## Validate goreleaser config
	@goreleaser check

bump: ## generate a new version with svu
	@$(MAKE) build
	@$(MAKE) test
	@$(MAKE) fmt
	$(MAKE) lint
	@if [ -n "$$(git status --porcelain)" ]; then \
		echo "Working directory is not clean. Please commit or stash changes before bumping version."; \
		exit 1; \
	fi
	@echo "Creating new tag..."
	@version=$$(svu next); \
		git tag -a $$version -m "Version $$version"; \
		echo "Tagged version $$version"; \
		echo "Pushing tag $$version to origin..."; \
		git push origin $$version

.DEFAULT_GOAL := help
