# The make gate (frostyard/core ADR-0043 and ADR-0044;
# docs/adr/0002-adopt-the-repository-baseline.md).
#
#   make verify  the repository surfaces: agent aliases, docs index, risk paths,
#                corrections and the vendored core copies, with Node only.
#                Fast, credential-free, and it writes nothing.
#   make test    builds Igloo with meson in the GNOME 51 SDK and runs its
#                meson tests (testsuite/), as in "Develop inside the build
#                environment" (docs/design/build-and-release.md).
#   make check   the developer gate: verify.
#   make ci      what CI runs: verify, the corrections comparison with BASE,
#                and test.

MANIFEST := org.frostyard.Igloo.json
# flatpak-builder's build directory and meson's; .gitignore lists both.
BUILD_DIR := build-dir
MESON_BUILD_DIR := _build

# On a developer host, flatpak-builder is the org.flatpak.Builder Flatpak. CI's
# container has the command itself, so ci.yml sets FLATPAK_BUILDER=flatpak-builder.
FLATPAK_BUILDER ?= flatpak run --filesystem=home org.flatpak.Builder
FLATPAK ?= flatpak

# The major version of the Node on PATH (mise.toml pins it), checked so the
# template gate needs no package install.
NODE_MAJOR := $(shell node -p "process.versions.node.split('.')[0]" 2>/dev/null)

.PHONY: verify check test corrections-base ci

verify:
	@if ! command -v node >/dev/null 2>&1; then echo "FAIL: make verify requires Node.js 22 or later and make; run: mise install"; exit 1; fi
	@if [ "$(NODE_MAJOR)" = "" ] || [ "$(NODE_MAJOR)" -lt 22 ]; then echo "FAIL: make verify requires Node.js 22 or later and make (found: $$(node --version 2>/dev/null || echo missing)); run: mise install"; exit 1; fi
	node scripts/check-template.mjs
	node --test test/*.test.mjs

check: verify

# The dependencies (fast_float, simdutf, VTE) build once into BUILD_DIR, and
# flatpak-builder's cache keeps them until the manifest changes. Igloo then
# builds with meson inside that build environment, as a debug build, and
# meson test runs testsuite/ and the desktop, schema and AppStream checks.
test:
	$(FLATPAK_BUILDER) --force-clean --disable-rofiles-fuse --stop-at=ptyxis $(BUILD_DIR) $(MANIFEST)
	$(FLATPAK) build --filesystem=$(CURDIR) $(BUILD_DIR) sh -c 'meson setup --reconfigure $(MESON_BUILD_DIR) --prefix=/app && meson compile -C $(MESON_BUILD_DIR) && meson test -C $(MESON_BUILD_DIR) --print-errorlogs'

# The corrections check compares .memory/corrections.jsonl with BASE: lines
# are append-only (only an empty promoted_to may be filled in). The contract
# starts with the gate: a BASE that does not hold
# scripts/check-corrections.mjs had no such rule, so the comparison is skipped
# with a notice. Every BASE after the adoption holds the script.
#
# A BASE that does not resolve to a commit is an error, never an old base: a
# typo or an unfetched ref must not turn the comparison into a silent pass.
corrections-base:
	@if [ -z "$(BASE)" ]; then exit 0; fi; \
	if ! git rev-parse --verify --quiet "$(BASE)^{commit}" >/dev/null; then \
		echo "FAIL: BASE $(BASE) does not resolve to a commit (fetch it first)"; \
		exit 1; \
	fi; \
	if git cat-file -e "$(BASE):scripts/check-corrections.mjs" 2>/dev/null; then \
		node scripts/check-corrections.mjs --base "$(BASE)"; \
	else \
		echo "==> corrections: BASE $(BASE) predates the corrections gate; append-only comparison skipped"; \
	fi

# CI runs these 3 prerequisites as separate jobs of ci.yml (core ADR-0073):
# make test needs the GNOME SDK container, the other 2 need only Node.
ci: verify corrections-base test
