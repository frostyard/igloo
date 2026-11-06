// Repository checks that make verify runs with node --test (no package
// install). They hold the baseline promises that the template gate does not
// check by itself (docs/adr/0002-adopt-the-repository-baseline.md):
//   - LICENSE stays a byte-identical copy of upstream's COPYING;
//   - .github/risk-paths.json covers the release workflow, the manifest and
//     the code that runs outside the Flatpak sandbox or removes machines;
//   - ci.yml runs every part of `make ci` (frostyard/core ADR-0073);
//   - make corrections-base fails closed on a BASE it cannot read;
//   - the CI test job gives root the checkout before flatpak build runs.
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import {
  copyFileSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  readdirSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { after, test } from "node:test";
import { fileURLToPath } from "node:url";
import { matchGlob } from "../scripts/lib/risk-paths.mjs";

const repo = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const temporary = [];

after(() => {
  for (const directory of temporary) rmSync(directory, { recursive: true, force: true });
});

function read(relative) {
  return readFileSync(join(repo, relative), "utf8");
}

function files(relative) {
  const found = [];
  for (const entry of readdirSync(join(repo, relative), { withFileTypes: true })) {
    const path = `${relative}/${entry.name}`;
    if (entry.isDirectory()) found.push(...files(path));
    else if (entry.isFile()) found.push(path);
  }
  return found;
}

test("LICENSE is a byte-identical copy of COPYING", () => {
  assert.ok(
    readFileSync(join(repo, "LICENSE")).equals(readFileSync(join(repo, "COPYING"))),
    "LICENSE and COPYING differ: copy COPYING to LICENSE again",
  );
});

test("the risk-path list covers the release, the sandbox boundary and the destructive commands", () => {
  const { paths } = JSON.parse(read(".github/risk-paths.json"));
  const required = [
    ...files(".github/workflows"),
    "org.frostyard.Igloo.json",
    ...files("agent"),
    "src/ptyxis-machines-page.c",
    "src/ptyxis-machine-command-dialog.c",
    "src/ptyxis-igloo-migration.c",
  ];
  const missing = required.filter((path) => !paths.some((entry) => matchGlob(entry.glob, path)));
  assert.deepEqual(missing, [], `no glob in .github/risk-paths.json matches: ${missing.join(", ")}`);

  // flatpak.yml publishes; it needs its own publication entry, not only the
  // governance entry for every workflow.
  const publication = paths.filter((entry) => entry.why.startsWith("publication:"));
  for (const path of [".github/workflows/flatpak.yml", "org.frostyard.Igloo.json"]) {
    assert.ok(
      publication.some((entry) => matchGlob(entry.glob, path)),
      `${path} has no publication entry`,
    );
  }
});

test("flatpak.yml publishes the previous ID end-of-life, rebased to the new ID, after latest moves", () => {
  // docs/adr/0003-rename-the-application-id.md: without this image, installed
  // copies of io.github.frostyard.Igloo never move to org.frostyard.Igloo.
  const workflow = read(".github/workflows/flatpak.yml");
  assert.match(workflow, /^ {2}APP_ID: org\.frostyard\.Igloo$/m);
  assert.match(workflow, /^ {2}PREVIOUS_APP_ID: io\.github\.frostyard\.Igloo$/m);
  assert.match(workflow, /--end-of-life-rebase="\$PREVIOUS_APP_ID=\$APP_ID"/);
  assert.match(workflow, /repo "app\/\$PREVIOUS_APP_ID\/x86_64\/master"/);
  assert.match(workflow, /build-bundle --oci --arch=x86_64 repo oci\/igloo-eol\.oci "\$PREVIOUS_APP_ID"/);
  // An index built between 2 steps must never offer a rebase to an ID that it
  // does not list, so latest (the new ID) moves first.
  // The tag that the index reads moves only after the digest is attested.
  const latest = workflow.indexOf('docker://$IMAGE:latest"');
  const pushEol = workflow.indexOf("oci:oci/igloo-eol.oci");
  const attestEol = workflow.indexOf("subject-digest: ${{ steps.push-eol.outputs.digest }}");
  const tagEol = workflow.indexOf('docker://$IMAGE:$PREVIOUS_APP_ID"');
  assert.ok(latest > 0 && pushEol > latest, "the end-of-life image is pushed only after latest moves");
  assert.ok(attestEol > pushEol, "the end-of-life image is attested after its push");
  assert.ok(tagEol > attestEol, "the old ID's tag moves only after the attestation");
  assert.doesNotMatch(
    workflow.slice(pushEol, attestEol),
    /docker:\/\/\$IMAGE:\$PREVIOUS_APP_ID"/,
    "the first push of the end-of-life image must not use the tag that the index reads",
  );
});

// The prerequisites and recipe commands of the Makefile's ci target.
function ciTarget(makefile) {
  const lines = makefile.split("\n");
  const start = lines.findIndex((line) => /^ci:/.test(line));
  assert.notEqual(start, -1, "the Makefile has no ci target");
  const prerequisites = lines[start].replace(/^ci:/, "").trim().split(/\s+/).filter(Boolean);
  const recipe = [];
  for (const line of lines.slice(start + 1)) {
    if (line.startsWith("\t")) {
      const command = line.slice(1).trim();
      if (command && !command.startsWith("#")) recipe.push(command.replace(/^@/, ""));
    } else if (line.trim() === "" || line.trim().startsWith("#")) {
      continue;
    } else {
      break;
    }
  }
  return { prerequisites, recipe };
}

// The commands of the run: steps of a workflow (single-line runs only; the
// ci.yml steps are single lines).
function runCommands(workflow) {
  return [...workflow.matchAll(/^\s*(?:-\s+)?run:\s*(.+)$/gm)].map((match) => match[1].trim());
}

test("ci.yml runs every prerequisite and recipe command of make ci", () => {
  const { prerequisites, recipe } = ciTarget(read("Makefile"));
  const workflow = read(".github/workflows/ci.yml");
  assert.match(workflow, /^on:\n(?:.*\n)*?\s+pull_request:/m, "ci.yml does not run on pull_request");
  const runs = runCommands(workflow);
  for (const target of prerequisites) {
    const pattern = new RegExp(`^make ${target}(\\s|$)`);
    assert.ok(runs.some((run) => pattern.test(run)), `no ci.yml step runs make ${target}`);
  }
  for (const command of recipe) {
    assert.ok(runs.includes(command), `no ci.yml step runs the ci recipe command: ${command}`);
  }
});

test("ci.yml gives the container's root the checkout before make test", () => {
  // flatpak build drops root's capabilities, so root must own the checkout
  // that the runner's user checked out, or meson cannot create _build.
  const runs = runCommands(read(".github/workflows/ci.yml"));
  const chown = runs.findIndex((run) => /^chown -R "\$\(id -u\):\$\(id -g\)" "\$GITHUB_WORKSPACE"$/.test(run));
  const makeTest = runs.findIndex((run) => /^make test(\s|$)/.test(run));
  assert.notEqual(chown, -1, "ci.yml has no chown step for the checkout");
  assert.ok(chown < makeTest, "the chown step must come before make test");
});

test("the parser above reads prerequisites and recipe commands", () => {
  const parsed = ciTarget("x:\n\ttrue\n\nci: verify test\n\t# note\n\t@echo one\n\n\techo two\nother:\n\tfalse\n");
  assert.deepEqual(parsed, { prerequisites: ["verify", "test"], recipe: ["echo one", "echo two"] });
  assert.deepEqual(runCommands("steps:\n  - run: make verify\n  - name: x\n    run: make test X=1\n"), [
    "make verify",
    "make test X=1",
  ]);
});

// make corrections-base in a scratch repository with this Makefile and the
// corrections checker.
function scratch() {
  const root = mkdtempSync(join(tmpdir(), "igloo-corrections-"));
  temporary.push(root);
  const git = (...args) => {
    const result = spawnSync("git", args, { cwd: root, encoding: "utf8" });
    assert.equal(result.status, 0, `git ${args.join(" ")}: ${result.stderr}`);
    return result.stdout.trim();
  };
  git("init", "-q", "-b", "main");
  git("config", "user.email", "test@example.org");
  git("config", "user.name", "test");
  git("config", "commit.gpgsign", "false");
  copyFileSync(join(repo, "Makefile"), join(root, "Makefile"));
  mkdirSync(join(root, ".memory"));
  writeFileSync(join(root, ".memory", "corrections.jsonl"), "");
  git("add", ".");
  git("commit", "-q", "-m", "before the gate");
  const pregate = git("rev-parse", "HEAD");
  mkdirSync(join(root, "scripts"));
  copyFileSync(join(repo, "scripts", "check-corrections.mjs"), join(root, "scripts", "check-corrections.mjs"));
  const line = (correction, promoted = "") =>
    `${JSON.stringify({ date: "2026-11-06", scope: "test", correction, evidence: "test", promoted_to: promoted })}\n`;
  writeFileSync(join(root, ".memory", "corrections.jsonl"), line("first"));
  git("add", ".");
  git("commit", "-q", "-m", "the gate");
  const gate = git("rev-parse", "HEAD");
  const make = (base) => {
    const args = ["--no-print-directory", "corrections-base"];
    if (base !== undefined) args.push(`BASE=${base}`);
    const result = spawnSync("make", args, { cwd: root, encoding: "utf8" });
    return { status: result.status, output: `${result.stdout}${result.stderr}` };
  };
  const write = (text) => writeFileSync(join(root, ".memory", "corrections.jsonl"), text);
  return { make, write, line, pregate, gate };
}

test("make corrections-base: no BASE, an unknown BASE, a base before the gate", () => {
  const { make, pregate } = scratch();
  assert.equal(make().status, 0);
  const unknown = make("nope");
  assert.notEqual(unknown.status, 0, "BASE=nope must fail");
  assert.match(unknown.output, /BASE nope does not resolve to a commit/);
  const old = make(pregate);
  assert.equal(old.status, 0, old.output);
  assert.match(old.output, /predates the corrections gate/);
});

test("make corrections-base: an appended line passes, a changed or removed line fails", () => {
  const { make, write, line, gate } = scratch();
  write(line("first") + line("second"));
  assert.equal(make(gate).status, 0, "an appended line must pass");
  write(line("first", "AGENTS.md"));
  assert.equal(make(gate).status, 0, "filling an empty promoted_to must pass");
  write(line("changed"));
  assert.notEqual(make(gate).status, 0, "a changed line must fail");
  write("");
  assert.notEqual(make(gate).status, 0, "a removed line must fail");
});
