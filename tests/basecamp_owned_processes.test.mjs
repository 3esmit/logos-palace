#!/usr/bin/env node

import assert from "node:assert/strict";
import {
  chmod,
  mkdir,
  mkdtemp,
  rm,
  writeFile,
} from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";
import test from "node:test";
import {
  captureOwnedProcessIdentity,
  cgroupProcesses,
  claimBoundProcesses,
  discoverOwnedBasecampProcesses,
  ownedProcessIdentityExists,
  ownedProcessGroupMembers,
  ownedBasecampUserDir,
  requireOwnedProcessGroup,
} from "./basecamp_owned_processes.mjs";

const scopeCgroup =
  "/user.slice/user-1000.slice/user@1000.service/"
  + "logos-palace-run-TEST0001.slice/"
  + "logos-palace-run-TEST0001-gate1-A1b2C3d4.scope";

function procStat(pid, parentPid, processGroupId, sessionId, start = 12345) {
  return `${pid} (worker) ${
    [
      "S",
      parentPid,
      processGroupId,
      sessionId,
      ...Array(15).fill(0),
      start,
    ].join(" ")
  }\n`;
}

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
      procStat(701, 1, 701, 701),
    );
    await writeFile(
      join(processDir, "cgroup"),
      `0::${scopeCgroup}\n`,
    );
    assert.deepEqual(
      await discoverOwnedBasecampProcesses({
        basecamp: "/nix/store/example/bin/LogosBasecamp",
        userDirs: new Set(["/var/tmp/run/users/a"]),
        cgroupPath: scopeCgroup,
        uid: 1000,
        procRoot: root,
      }),
      [{
        pid: 701,
        processGroupId: 701,
        startTimeTicks: 12345,
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
      procStat(pid, 1, 801, 801, 12000 + pid),
    );
    await writeFile(
      join(processDir, "cgroup"),
      `0::${scopeCgroup}\n`,
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
      cgroupPath: scopeCgroup,
      uid: 1000,
      procRoot: root,
    });
    assert.deepEqual(members, [
      {
        pid: 801,
        startTimeTicks: 12801,
        observedCgroupPath: scopeCgroup,
        owned: true,
      },
      {
        pid: 802,
        startTimeTicks: 12802,
        observedCgroupPath: scopeCgroup,
        owned: false,
      },
      {
        pid: 803,
        startTimeTicks: 12803,
        observedCgroupPath: scopeCgroup,
        owned: false,
      },
    ]);
    assert.throws(
      () => requireOwnedProcessGroup(members, 801),
      /refusing to signal unverified process group 801/,
    );
    assert.deepEqual(
      requireOwnedProcessGroup([members[0]], 801),
      [members[0]],
    );
    assert.throws(
      () => requireOwnedProcessGroup([], 801),
      /refusing to signal/,
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("process-group scan ignores unrelated zero-topology entries", async () => {
  const root = await mkdtemp(join(tmpdir(), "palace-proc-zero-topology-"));
  const claimPath = "/var/tmp/logos-palace-1000/active-claim.json";
  try {
    const kernelLike = join(root, "2");
    const owned = join(root, "804");
    await mkdir(kernelLike);
    await mkdir(owned);
    await writeFile(join(kernelLike, "stat"), procStat(2, 1, 0, 0));
    await writeFile(
      join(owned, "status"),
      "Name:\tworker\nUid:\t1000\t1000\t1000\t1000\n",
    );
    await writeFile(join(owned, "stat"), procStat(804, 1, 804, 804));
    await writeFile(join(owned, "cgroup"), `0::${scopeCgroup}\n`);
    await writeFile(
      join(owned, "environ"),
      `PALACE_MVP_CLAIM_PATH=${claimPath}\0`,
    );
    assert.deepEqual(
      await ownedProcessGroupMembers({
        processGroupId: 804,
        claimPath,
        cgroupPath: scopeCgroup,
        uid: 1000,
        procRoot: root,
      }),
      [{
        pid: 804,
        startTimeTicks: 12345,
        observedCgroupPath: scopeCgroup,
        owned: true,
      }],
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
      procStat(901, 900, 901, 900),
    );
    await writeFile(
      join(processDir, "environ"),
      `PALACE_MVP_CLAIM_PATH=${claimPath}\0`,
    );
    await writeFile(
      join(processDir, "cgroup"),
      `0::${scopeCgroup}/nested-worker\n`,
    );
    assert.deepEqual(
      await claimBoundProcesses({
        claimPath,
        cgroupPath: scopeCgroup,
        uid: 1000,
        procRoot: root,
      }),
      [{
        pid: 901,
        parentPid: 900,
        processGroupId: 901,
        sessionId: 900,
        startTimeTicks: 12345,
      }],
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("scoped claim scan ignores process outside attested runner cgroup", async () => {
  const root = await mkdtemp(join(tmpdir(), "palace-proc-scope-"));
  const claimPath = "/var/tmp/logos-palace-1000/active-claim.json";
  try {
    const processDir = join(root, "902");
    await mkdir(processDir);
    await writeFile(
      join(processDir, "status"),
      "Name:\tprotected\nUid:\t1000\t1000\t1000\t1000\n",
    );
    await writeFile(
      join(processDir, "stat"),
      procStat(902, 1, 902, 902),
    );
    await writeFile(join(processDir, "cgroup"), "0::/other-session\n");
    await writeFile(
      join(processDir, "environ"),
      `PALACE_MVP_CLAIM_PATH=${claimPath}\0`,
    );
    await chmod(join(processDir, "environ"), 0o000);
    assert.deepEqual(
      await claimBoundProcesses({
        claimPath,
        cgroupPath: scopeCgroup,
        uid: 1000,
        procRoot: root,
      }),
      [],
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("claim scan rejects protected process inside runner cgroup", async () => {
  const root = await mkdtemp(join(tmpdir(), "palace-proc-protected-"));
  const claimPath = "/var/tmp/logos-palace-1000/active-claim.json";
  try {
    const processDir = join(root, "903");
    await mkdir(processDir);
    await writeFile(
      join(processDir, "status"),
      "Name:\tprotected\nUid:\t1000\t1000\t1000\t1000\n",
    );
    await writeFile(
      join(processDir, "stat"),
      procStat(903, 1, 903, 903),
    );
    await writeFile(
      join(processDir, "cgroup"),
      `0::${scopeCgroup}\n`,
    );
    await writeFile(
      join(processDir, "environ"),
      `PALACE_MVP_CLAIM_PATH=${claimPath}\0`,
    );
    await chmod(join(processDir, "environ"), 0o000);
    await assert.rejects(
      claimBoundProcesses({
        claimPath,
        cgroupPath: scopeCgroup,
        uid: 1000,
        procRoot: root,
      }),
      (error) => error?.code === "EACCES",
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("claim scan rejects cgroup v1 and hybrid inventories", async () => {
  const root = await mkdtemp(join(tmpdir(), "palace-proc-cgroup-v1-"));
  const claimPath = "/var/tmp/logos-palace-1000/active-claim.json";
  try {
    const processDir = join(root, "904");
    await mkdir(processDir);
    await writeFile(
      join(processDir, "cgroup"),
      "0::/legacy\n2:cpu:/legacy\n",
    );
    await assert.rejects(
      claimBoundProcesses({
        claimPath,
        cgroupPath: scopeCgroup,
        uid: 1000,
        procRoot: root,
      }),
      /invalid cgroup/,
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("cgroup inventory finds residue without claim environment", async () => {
  const root = await mkdtemp(join(tmpdir(), "palace-proc-residue-"));
  try {
    const inside = join(root, "905");
    const outside = join(root, "906");
    await mkdir(inside);
    await mkdir(outside);
    await writeFile(
      join(inside, "stat"),
      procStat(905, 1, 905, 905),
    );
    await writeFile(
      join(inside, "cgroup"),
      `0::${scopeCgroup}/sanitized-worker\n`,
    );
    await writeFile(
      join(outside, "stat"),
      procStat(906, 1, 906, 906),
    );
    await writeFile(
      join(outside, "cgroup"),
      "0::/other-session\n",
    );
    assert.deepEqual(
      await cgroupProcesses({
        cgroupPath: scopeCgroup,
        procRoot: root,
      }),
      [{
        pid: 905,
        parentPid: 1,
        processGroupId: 905,
        sessionId: 905,
        startTimeTicks: 12345,
      }],
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("cgroup inventory ignores unrelated zero-topology entries", async () => {
  const root = await mkdtemp(join(tmpdir(), "palace-proc-cgroup-zero-"));
  try {
    const kernelLike = join(root, "2");
    const inside = join(root, "909");
    await mkdir(kernelLike);
    await mkdir(inside);
    await writeFile(join(kernelLike, "stat"), procStat(2, 1, 0, 0));
    await writeFile(join(kernelLike, "cgroup"), "0::/init.scope\n");
    await writeFile(join(inside, "stat"), procStat(909, 1, 909, 909));
    await writeFile(join(inside, "cgroup"), `0::${scopeCgroup}\n`);
    assert.deepEqual(
      await cgroupProcesses({ cgroupPath: scopeCgroup, procRoot: root }),
      [{
        pid: 909,
        parentPid: 1,
        processGroupId: 909,
        sessionId: 909,
        startTimeTicks: 12345,
      }],
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("owned identity keeps zero-topology target rejection strict", async () => {
  const root = await mkdtemp(join(tmpdir(), "palace-proc-target-zero-"));
  try {
    const processDir = join(root, "910");
    await mkdir(processDir);
    await writeFile(join(processDir, "stat"), procStat(910, 1, 0, 0));
    await writeFile(join(processDir, "cgroup"), `0::${scopeCgroup}\n`);
    await assert.rejects(
      captureOwnedProcessIdentity({
        pid: 910,
        cgroupPath: scopeCgroup,
        procRoot: root,
      }),
      /process 910 has invalid topology during cleanup/,
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("observed root cgroup is outside a scoped target, not malformed", async () => {
  const root = await mkdtemp(join(tmpdir(), "palace-proc-root-cgroup-"));
  const claimPath = "/var/tmp/logos-palace-1000/active-claim.json";
  try {
    const processDir = join(root, "907");
    await mkdir(processDir);
    await writeFile(
      join(processDir, "stat"),
      procStat(907, 1, 907, 907, 39007),
    );
    await writeFile(join(processDir, "cgroup"), "0::/\n");
    assert.deepEqual(
      await discoverOwnedBasecampProcesses({
        basecamp: "/nix/store/example/bin/LogosBasecamp",
        userDirs: new Set(["/var/tmp/run/users/a"]),
        cgroupPath: scopeCgroup,
        uid: 1000,
        procRoot: root,
      }),
      [],
    );
    assert.deepEqual(
      await claimBoundProcesses({
        claimPath,
        cgroupPath: scopeCgroup,
        uid: 1000,
        procRoot: root,
      }),
      [],
    );
    assert.deepEqual(
      await cgroupProcesses({
        cgroupPath: scopeCgroup,
        procRoot: root,
      }),
      [],
    );
    assert.deepEqual(
      await ownedProcessGroupMembers({
        processGroupId: 907,
        claimPath,
        cgroupPath: scopeCgroup,
        uid: 1000,
        procRoot: root,
      }),
      [{
        pid: 907,
        startTimeTicks: 39007,
        observedCgroupPath: "/",
        owned: false,
      }],
    );
    await assert.rejects(
      cgroupProcesses({
        cgroupPath: "/",
        procRoot: root,
      }),
      /cgroup process inventory input is invalid/,
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("cleanup identity rejects PID reuse and cgroup churn", async () => {
  const root = await mkdtemp(join(tmpdir(), "palace-proc-churn-"));
  try {
    const processDir = join(root, "908");
    await mkdir(processDir);
    await writeFile(
      join(processDir, "stat"),
      procStat(908, 1, 908, 908, 45000),
    );
    await writeFile(
      join(processDir, "cgroup"),
      `0::${scopeCgroup}\n`,
    );
    const token = await captureOwnedProcessIdentity({
      pid: 908,
      cgroupPath: scopeCgroup,
      procRoot: root,
    });
    assert.deepEqual(token, {
      pid: 908,
      startTimeTicks: 45000,
      observedCgroupPath: scopeCgroup,
    });
    assert.equal(
      await ownedProcessIdentityExists({
        ...token,
        cgroupPath: scopeCgroup,
        procRoot: root,
      }),
      true,
    );

    await writeFile(
      join(processDir, "stat"),
      procStat(908, 1, 908, 908, 45001),
    );
    await assert.rejects(
      ownedProcessIdentityExists({
        ...token,
        cgroupPath: scopeCgroup,
        procRoot: root,
      }),
      /process 908 was reused during cleanup/,
    );

    await writeFile(
      join(processDir, "stat"),
      procStat(908, 1, 908, 908, 45000),
    );
    await writeFile(
      join(processDir, "cgroup"),
      `0::${scopeCgroup}/moved\n`,
    );
    await assert.rejects(
      ownedProcessIdentityExists({
        ...token,
        cgroupPath: scopeCgroup,
        procRoot: root,
      }),
      /process 908 changed cgroup during cleanup/,
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});
