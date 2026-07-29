import assert from "node:assert/strict";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import {
  mkdir,
  mkdtemp,
  rm,
  symlink,
  truncate,
  writeFile,
} from "node:fs/promises";
import {
  boundedDirectoryFingerprint,
  fingerprintBoundedRegularFile,
  fingerprintBoundedRegularFileIdentity,
} from "./basecamp_gate4_cold_state.mjs";

async function withTemporaryDirectory(run) {
  const directory = await mkdtemp(join(tmpdir(), "palace-cold-state-"));
  try {
    await run(directory);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
}

test("cold state fingerprint is deterministic and counts directories", async () => {
  await withTemporaryDirectory(async (directory) => {
    await mkdir(join(directory, "nested"));
    await writeFile(join(directory, "a"), "alpha");
    await writeFile(join(directory, "nested", "b"), "beta");

    const first = await boundedDirectoryFingerprint(directory);
    const second = await boundedDirectoryFingerprint(directory);
    assert.deepEqual(second, first);
    assert.equal(first.fileCount, 2);
    assert.equal(first.directoryCount, 2);
    assert.equal(first.totalBytes, 9);
    assert.match(first.sha256, /^[0-9a-f]{64}$/);
  });
});

test("cold state fingerprint rejects oversized sparse file before reading", async () => {
  await withTemporaryDirectory(async (directory) => {
    const path = join(directory, "large");
    await writeFile(path, "");
    await truncate(path, 1_025);
    await assert.rejects(
      boundedDirectoryFingerprint(directory, {
        maximumTotalBytes: 1_024,
      }),
      /exceeds byte bound/,
    );
    await assert.rejects(
      fingerprintBoundedRegularFile(path, 1_024),
      /exceeds evidence bound/,
    );
  });
});

test("cold state fingerprint bounds directories and depth", async () => {
  await withTemporaryDirectory(async (directory) => {
    await mkdir(join(directory, "one", "two"), { recursive: true });
    await assert.rejects(
      boundedDirectoryFingerprint(directory, {
        maximumDirectories: 2,
      }),
      /exceeds directory bound/,
    );
    await assert.rejects(
      boundedDirectoryFingerprint(directory, {
        maximumDepth: 1,
      }),
      /exceeds depth bound/,
    );
  });
});

test("cold state fingerprint rejects symbolic links", async () => {
  await withTemporaryDirectory(async (directory) => {
    await writeFile(join(directory, "target"), "value");
    await symlink("target", join(directory, "alias"));
    await assert.rejects(
      boundedDirectoryFingerprint(directory),
      /unsafe cold-rebuild entry type alias/,
    );
  });
});

test("identity fingerprint preserves legacy shape and binds opened inode", async () => {
  await withTemporaryDirectory(async (directory) => {
    const path = join(directory, "identity");
    await writeFile(path, "exact bytes");
    const legacy = await fingerprintBoundedRegularFile(path, 1024);
    const identity = await fingerprintBoundedRegularFileIdentity(
      path,
      1024,
    );
    assert.deepEqual(Object.keys(legacy).sort(), [
      "byteLength",
      "sha256",
    ]);
    assert.equal(identity.byteLength, legacy.byteLength);
    assert.equal(identity.sha256, legacy.sha256);
    assert.match(identity.device, /^[0-9a-f]+:[0-9a-f]+$/);
    assert.match(identity.inode, /^[1-9][0-9]*$/);
  });
});
