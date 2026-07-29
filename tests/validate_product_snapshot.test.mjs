import assert from "node:assert/strict";
import {
  chmod,
  mkdtemp,
  mkdir,
  symlink,
  writeFile,
} from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { spawnSync } from "node:child_process";
import test from "node:test";

const validator = new URL(
  "./validate_product_snapshot.mjs",
  import.meta.url,
);

async function fixture(entries, trackedPaths) {
  const fixtureDirectory = await mkdtemp(
    join(tmpdir(), "logos-palace-snapshot-fixture-"),
  );
  const source = join(fixtureDirectory, "source");
  await mkdir(source);
  for (const [path, value] of Object.entries(entries)) {
    const absolute = join(source, path);
    await mkdir(dirname(absolute), { recursive: true });
    if (value?.symlink !== undefined) {
      await symlink(value.symlink, absolute);
    } else {
      await writeFile(absolute, value);
    }
  }
  const added = spawnSync(
    "nix",
    ["store", "add-path", "--name", "snapshot-validator-fixture", source],
    { encoding: "utf8" },
  );
  assert.equal(added.status, 0, added.stderr);
  const snapshot = added.stdout.trim();
  const tracked = join(fixtureDirectory, "tracked-paths.nul");
  await writeFile(
    tracked,
    Buffer.from(`${trackedPaths.join("\0")}\0`, "utf8"),
  );
  await chmod(tracked, 0o600);
  return { snapshot, tracked };
}

function validate({ snapshot, tracked }) {
  return spawnSync(
    process.execPath,
    [validator.pathname, snapshot, tracked],
    { encoding: "utf8" },
  );
}

test("accepts exact regular tracked-file set", async () => {
  const input = await fixture(
    {
      "README.md": "fixture\n",
      "scripts/run.sh": "#!/bin/sh\nexit 0\n",
    },
    ["README.md", "scripts/run.sh"],
  );
  const result = validate(input);
  assert.equal(result.status, 0, result.stderr);
  const evidence = JSON.parse(result.stdout);
  assert.equal(evidence.fileCount, 2);
  assert.equal(evidence.exactTrackedFileSet, true);
  assert.match(evidence.treeSha256, /^[0-9a-f]{64}$/);
});

test("rejects generated .git directory", async () => {
  const result = validate(
    await fixture(
      { "README.md": "fixture\n", ".git/config": "unsafe\n" },
      ["README.md"],
    ),
  );
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /forbidden path/);
});

test("rejects generated target directory", async () => {
  const result = validate(
    await fixture(
      { "README.md": "fixture\n", "program/target/out": "unsafe\n" },
      ["README.md"],
    ),
  );
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /forbidden path/);
});

test("rejects absolute or other symlink", async () => {
  const result = validate(
    await fixture(
      { "README.md": "fixture\n", result: { symlink: "/nix/store" } },
      ["README.md", "result"],
    ),
  );
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /forbidden path|symlink is forbidden/);
});

test("rejects file-set mismatch", async () => {
  const result = validate(
    await fixture(
      { "README.md": "fixture\n", "extra.txt": "unexpected\n" },
      ["README.md"],
    ),
  );
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /differ from exact Git tracked-file set/);
});
