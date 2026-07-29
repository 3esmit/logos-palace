#!/usr/bin/env node

import assert from "node:assert/strict";
import {
  mkdir,
  mkdtemp,
  rm,
  writeFile,
} from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";
import test from "node:test";
import {
  claimBoundProcesses,
  discoverOwnedBasecampProcesses,
  ownedProcessGroupMembers,
  ownedBasecampUserDir,
  requireOwnedProcessGroup,
} from "./basecamp_owned_processes.mjs";

test("matches only exact Basecamp and run-owned user directory", () => {
  const basecamp = "/nix/store/example/bin/LogosBasecamp";
  const userDir = "/var/tmp/run/users/a";
  const owned = new Set([userDir]);
  assert.equal(
    ownedBasecampUserDir(
      [basecamp, "--user-dir", userDir, "-platform", "offscreen"],
      basecamp,
      owned,
    ),
    userDir,
  );
  assert.equal(
    ownedBasecampUserDir(
      [basecamp, "--user-dir", "/var/tmp/other/users/a"],
      basecamp,
      owned,
    ),
    undefined,
  );
});

test("discovers exact same-UID Basecamp PID and detached group", async () => {
  const root = await mkdtemp(join(tmpdir(), "palace-proc-"));
  try {
    const processDir = join(root, "701");
    await mkdir(processDir);
    await writeFile(
      join(processDir, "status"),
      "Name:\tLogosBasecamp\nUid:\t1000\t1000\t1000\t1000\n",
    );
    await writeFile(
      join(processDir, "cmdline"),
      "/nix/store/example/bin/LogosBasecamp\0--user-dir\0"
      + "/var/tmp/run/users/a\0",
    );
    await writeFile(
      join(processDir, "stat"),
      "701 (LogosBasecamp) S 1 701 701 0 0\n",
    );
    assert.deepEqual(
      await discoverOwnedBasecampProcesses({
        basecamp: "/nix/store/example/bin/LogosBasecamp",
        userDirs: new Set(["/var/tmp/run/users/a"]),
        uid: 1000,
        procRoot: root,
      }),
      [{
        pid: 701,
        processGroupId: 701,
        userDir: "/var/tmp/run/users/a",
      }],
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("process-group authorization rejects mixed claim ownership", async () => {
  const root = await mkdtemp(join(tmpdir(), "palace-proc-group-"));
  const claimPath = "/var/tmp/logos-palace-1000/active-claim.json";
  const writeProcess = async (pid, uid, claim) => {
    const processDir = join(root, String(pid));
    await mkdir(processDir);
    await writeFile(
      join(processDir, "status"),
      `Name:\tworker\nUid:\t${uid}\t${uid}\t${uid}\t${uid}\n`,
    );
    await writeFile(
      join(processDir, "stat"),
      `${pid} (worker) S 1 801 801 0 0\n`,
    );
    await writeFile(
      join(processDir, "environ"),
      `PALACE_MVP_CLAIM_PATH=${claim}\0`,
    );
  };
  try {
    await writeProcess(801, 1000, claimPath);
    await writeProcess(802, 1000, "/var/tmp/other-claim.json");
    await writeProcess(803, 1001, claimPath);
    const members = await ownedProcessGroupMembers({
      processGroupId: 801,
      claimPath,
      uid: 1000,
      procRoot: root,
    });
    assert.deepEqual(members, [
      { pid: 801, owned: true },
      { pid: 802, owned: false },
      { pid: 803, owned: false },
    ]);
    assert.throws(
      () => requireOwnedProcessGroup(members, 801),
      /refusing to signal unverified process group 801/,
    );
    assert.deepEqual(
      requireOwnedProcessGroup([{ pid: 801, owned: true }], 801),
      [{ pid: 801, owned: true }],
    );
    assert.throws(
      () => requireOwnedProcessGroup([], 801),
      /refusing to signal/,
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("claim scan finds module host in alternate process group", async () => {
  const root = await mkdtemp(join(tmpdir(), "palace-proc-claim-"));
  const claimPath = "/var/tmp/logos-palace-1000/active-claim.json";
  try {
    const processDir = join(root, "901");
    await mkdir(processDir);
    await writeFile(
      join(processDir, "status"),
      "Name:\tlogos_host\nUid:\t1000\t1000\t1000\t1000\n",
    );
    await writeFile(
      join(processDir, "stat"),
      "901 (logos_host) S 900 901 900 0 0\n",
    );
    await writeFile(
      join(processDir, "environ"),
      `PALACE_MVP_CLAIM_PATH=${claimPath}\0`,
    );
    assert.deepEqual(
      await claimBoundProcesses({
        claimPath,
        uid: 1000,
        procRoot: root,
      }),
      [{
        pid: 901,
        parentPid: 900,
        processGroupId: 901,
        sessionId: 900,
      }],
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});
