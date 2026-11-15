// Repository checks that make verify runs with node --test (no package
// install). They hold the baseline promises that the template gate does not
// check by itself (docs/adr/0002-adopt-the-repository-baseline.md):
//   - LICENSE stays a byte-identical copy of upstream's COPYING;
//   - the README's documentation line links the user guide on frostyard.org,
//     Igloo's documentation home (row B9, frostyard/core ADR-0084);
//   - .github/risk-paths.json covers the release workflow, the manifest and
//     the code that runs outside the Flatpak sandbox or removes machines, at
//     L1 or L2 (the levels that get risk:high);
//   - ci.yml runs every part of `make ci` (frostyard/core ADR-0073);
//   - make corrections-base fails closed on a BASE it cannot read;
//   - the CI test job gives root the checkout before flatpak build runs;
//   - flatpak.yml keeps the Flatpak installation on a named volume, off the
//     image's overlayfs.
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
import { LEVELS, matchGlob } from "../scripts/lib/risk-paths.mjs";

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

// The jobs of a workflow text: each line under the top-level jobs: key splits
// at a job id line, and a line at column 0 ends the block.
function jobs(workflow) {
  const lines = workflow.split("\n");
  const start = lines.findIndex((line) => /^jobs:\s*$/.test(line));
  if (start === -1) return [];
  const found = [];
  let current = null;
  for (const line of lines.slice(start + 1)) {
    const heading = /^ {2}([A-Za-z0-9_-]+):\s*$/.exec(line);
    if (heading) {
      current = { id: heading[1], text: "" };
      found.push(current);
    } else if (/^#/.test(line)) {
      // A comment at column 0 belongs to no job and ends nothing.
      continue;
    } else if (/^\S/.test(line)) {
      break;
    } else if (current) {
      current.text += `${line}\n`;
    }
  }
  return found;
}

// True when the job's container: block mounts a named volume at target: an
// entry SOURCE:TARGET or SOURCE:TARGET:OPTIONS, where SOURCE is a volume name
// and not a host path.
function namedVolumeAt(jobText, target) {
  const lines = jobText.split("\n");
  const key = lines.findIndex((line) => /^\s*container:\s*$/.test(line));
  if (key === -1) return false;
  const keyIndent = lines[key].match(/^\s*/)[0].length;
  const container = [];
  for (const line of lines.slice(key + 1)) {
    if (line.trim() === "") continue;
    if (line.match(/^\s*/)[0].length <= keyIndent) break;
    container.push(line);
  }
  const volumes = container.findIndex((line) => /^\s*volumes:\s*$/.test(line));
  if (volumes === -1) return false;
  const volumesIndent = container[volumes].match(/^\s*/)[0].length;
  for (const line of container.slice(volumes + 1)) {
    if (line.trim() === "") continue;
    if (line.match(/^\s*/)[0].length <= volumesIndent) break;
    const item = /^\s*-\s*(.+?)\s*$/.exec(line);
    if (!item) continue;
    const parts = item[1].replace(/^["']|["']$/g, "").split(":");
    if (parts.length < 2 || parts[1] !== target) continue;
    const source = parts[0];
    if (source === "" || /^[/.~$]/.test(source)) continue;
    return true;
  }
  return false;
}

// A path gets risk:high only from a glob at L1 or L2, so the coverage checks
// below require such a match. Only an entry without a level is L1 (a version 1
// list); an explicit null or an unknown level fails, and the strictest matching
// entry wins, as in scripts/lib/risk-paths.mjs: a path that only an L3 or L4
// glob matches gets no risk:high.
function riskHighCoverage(entries, path) {
  const levels = entries
    .filter((entry) => matchGlob(entry.glob, path))
    .map((entry) => ("level" in entry ? entry.level : "L1"));
  if (levels.length === 0 || levels.some((level) => !LEVELS.includes(level))) return false;
  return levels.some((level) => level === "L1" || level === "L2");
}

test("LICENSE is a byte-identical copy of COPYING", () => {
  assert.ok(
    readFileSync(join(repo, "LICENSE")).equals(readFileSync(join(repo, "COPYING"))),
    "LICENSE and COPYING differ: copy COPYING to LICENSE again",
  );
});

test("the README's documentation line links the frostyard.org user guide", () => {
  // Row B9 of the baseline (frostyard/core ADR-0084): a user-facing tool has 1
  // user documentation home, and README.md links it on its documentation line.
  // Igloo's home is its full guide on frostyard.org, not docs/README.md.
  const guide = "https://frostyard.org/docs/tools/igloo/";
  const lines = read("README.md")
    .split("\n")
    .filter((line) => line.startsWith("**Documentation:**"));
  assert.equal(lines.length, 1, "README.md needs exactly 1 **Documentation:** line");
  const targets = [...lines[0].matchAll(/\]\(([^)\s]+)\)|<(https?:\/\/[^>]+)>/g)].map(
    (match) => match[1] ?? match[2],
  );
  assert.ok(
    targets.includes(guide),
    `the documentation line links ${JSON.stringify(targets)}, not the user guide ${guide}`,
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
  const missing = required.filter((path) => !riskHighCoverage(paths, path));
  assert.deepEqual(
    missing,
    [],
    `no L1 or L2 glob in .github/risk-paths.json matches: ${missing.join(", ")}`,
  );

  // flatpak.yml publishes; it needs its own publication entry, not only the
  // governance entry for every workflow.
  const publication = paths.filter((entry) => entry.why.startsWith("publication:"));
  for (const path of [".github/workflows/flatpak.yml", "org.frostyard.Igloo.json"]) {
    assert.ok(riskHighCoverage(publication, path), `${path} has no publication entry at L1 or L2`);
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
  // latest (the new ID) moves first, so an index that reads the tags after
  // the release job always lists the rebase target.
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

test("the coverage helper requires an L1 or L2 glob", () => {
  const entry = (glob, level) =>
    level === undefined ? { glob, why: "test" } : { glob, level, why: "test" };
  assert.equal(
    riskHighCoverage([entry("src/**", "L3")], "src/a.c"),
    false,
    "an L3 glob alone must not cover a file",
  );
  assert.equal(
    riskHighCoverage([entry("src/**", "L4")], "src/a.c"),
    false,
    "an L4 glob alone must not cover a file",
  );
  assert.equal(riskHighCoverage([entry("src/**", "L1")], "src/a.c"), true, "an L1 glob covers a file");
  assert.equal(riskHighCoverage([entry("src/**", "L2")], "src/a.c"), true, "an L2 glob covers a file");
  assert.equal(riskHighCoverage([entry("src/**")], "src/a.c"), true, "an entry with no level is L1");
  assert.equal(
    riskHighCoverage([entry("src/**", null)], "src/a.c"),
    false,
    "an explicit null level must not cover a file",
  );
  assert.equal(
    riskHighCoverage([entry("src/**", "L5")], "src/a.c"),
    false,
    "an unknown level must not cover a file",
  );
  assert.equal(
    riskHighCoverage([entry("src/**", "L3"), entry("src/a.c", "L1")], "src/a.c"),
    true,
    "the strictest matching level wins, so an overlapping L1 glob passes",
  );
  assert.equal(
    riskHighCoverage([entry("src/**", "L3")], "docs/a.md"),
    false,
    "no matching glob is no coverage",
  );
});

// flatpak-builder's --install-deps-from=flathub updates the runtime and SDK
// that the runner image keeps in /var/lib/flatpak, and an update renames a
// directory of an image layer into .removed/. On an overlayfs with redirect_dir
// off, that rename fails with EXDEV, "Invalid cross-device link"
// (https://docs.kernel.org/filesystems/overlayfs.html#renaming-directories),
// and the build died there. A named volume holds a copy of that installation on
// the host's own file system, where the rename works, so every job that runs
// flatpak-builder mounts one.
test("every flatpak-builder job keeps the Flatpak installation off overlayfs", (t) => {
  const action = "uses: flatpak/flatpak-github-actions/flatpak-builder@";
  let found = 0;
  for (const path of files(".github/workflows").sort()) {
    if (!/\.ya?ml$/.test(path)) continue;
    for (const job of jobs(read(path))) {
      if (!job.text.includes(action)) continue;
      found += 1;
      assert.ok(
        namedVolumeAt(job.text, "/var/lib/flatpak"),
        `${path} job ${job.id}: flatpak-builder updates the runner image's Flatpak installation, and that update fails with EXDEV on an overlayfs; mount a named volume at /var/lib/flatpak in the job's container`,
      );
    }
  }
  t.diagnostic(`${found} flatpak-builder job(s) checked`);
  assert.ok(found > 0, `no job in .github/workflows runs ${action.replace(/^uses: /, "")}`);
});

test("the job and volume helpers read a workflow's jobs, container and volumes", () => {
  const workflow = (container) =>
    `name: x\njobs:\n  build:\n    runs-on: ubuntu-latest\n${container}    steps:\n      - uses: flatpak/flatpak-github-actions/flatpak-builder@abc # v6\n  publish:\n    runs-on: ubuntu-latest\n    steps:\n      - run: true\n`;
  const job = (volumes) =>
    workflow(`    container:\n      image: x\n      options: --privileged\n${volumes}    `);
  const build = (volumes) => jobs(job(volumes)).find((entry) => entry.id === "build").text;
  assert.equal(jobs(workflow("")).length, 2, "the jobs helper must split 2 jobs");
  assert.equal(jobs(job("")).map((entry) => entry.id).join(","), "build,publish");
  assert.equal(
    jobs("jobs:\n  build:\n    runs-on: ubuntu-latest\n# note\n  publish:\n    runs-on: ubuntu-latest\n")
      .map((entry) => entry.id)
      .join(","),
    "build,publish",
    "a comment line at column 0 must not end the jobs block",
  );
  for (const volumes of [
    "      volumes:\n        - flatpak-installation:/var/lib/flatpak\n",
    "      volumes:\n        - flatpak-installation:/var/lib/flatpak:rw\n",
    '      volumes:\n        - "flatpak-installation:/var/lib/flatpak"\n',
  ]) {
    assert.equal(namedVolumeAt(build(volumes), "/var/lib/flatpak"), true, `must accept ${volumes}`);
  }
  for (const volumes of [
    "      volumes:\n        - /srv/flatpak:/var/lib/flatpak\n",
    "      volumes:\n        - ./flatpak:/var/lib/flatpak\n",
    "      volumes:\n        - /var/lib/flatpak\n",
    "      volumes:\n        - flatpak-installation:/var/lib/flatpak2\n",
    "      volumes:\n        - flatpak-installation:/tmp\n",
    "",
  ]) {
    assert.equal(namedVolumeAt(build(volumes), "/var/lib/flatpak"), false, `must refuse ${volumes}`);
  }
  // A volume in another job's container does not count for this one.
  const other = workflow("    container:\n      volumes:\n        - flatpak-installation:/var/lib/flatpak\n    ");
  const publish = jobs(other).find((entry) => entry.id === "publish").text;
  assert.equal(namedVolumeAt(publish, "/var/lib/flatpak"), false);
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
    // `make ci BASE=<ref>` runs this test with BASE in MAKEFLAGS; a child make
    // would take it, so the scratch make gets an environment without it.
    const env = { ...process.env };
    for (const name of ["MAKEFLAGS", "MFLAGS", "MAKELEVEL", "BASE"]) delete env[name];
    const result = spawnSync("make", args, { cwd: root, encoding: "utf8", env });
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
