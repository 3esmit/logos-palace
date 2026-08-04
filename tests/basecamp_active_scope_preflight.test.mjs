#!/usr/bin/env node

import assert from "node:assert/strict";
import {
  chmod,
  mkdir,
  mkdtemp,
  rm,
  writeFile,
} from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import {
  auditedLegacyPreGate3,
  releaseProgramId,
  releaseRootId,
} from "./basecamp_claim_lifecycle.mjs";
import {
  legacyClaimBoundProcesses,
  retireClaimScope,
} from "./basecamp_active_scope_preflight.mjs";

const uid = 1000;
const common = {
  schema: "logos.palace.basecamp-active-run-claim",
  version: 2,
  uid,
  releaseProgramId,
  releaseRootId,
  runDirectory: "/tmp/runs/run.ABCDef12",
  productSnapshot:
    "/nix/store/00000000000000000000000000000000-product",
  gcRootPath:
    "/var/tmp/logos-palace-1000/gc-roots/run.ABCDef12/product-snapshot",
  gcRootTarget:
    "/nix/store/00000000000000000000000000000000-product",
  gitCommit: "1".repeat(40),
  snapshotNarHash: `sha256-${"A".repeat(43)}=`,
  snapshotNarSize: 4096,
  snapshotRunnerSha256: "2".repeat(64),
  runtimeManifestPath:
    "/tmp/runs/run.ABCDef12/runtime-output-manifest.json",
  runtimeManifestSha256: "3".repeat(64),
  processScopeSlice: "logos-palace-run-ABCDef12.slice",
  processScopePrefix: "logos-palace-run-ABCDef12",
};

const runnerCgroup =
  "/user.slice/user-1000.slice/user@1000.service/"
  + "logos-palace-run-ABCDef12.slice/runner.scope";
const claimPath = "/var/tmp/logos-palace-1000/active.json";

function processStatus({
  pid,
  parentPid,
  uid: processUid,
  gid = processUid,
  name,
  threads = 1,
}) {
  return [
    `Name:\t${name}`,
    `Tgid:\t${pid}`,
    `Pid:\t${pid}`,
    `PPid:\t${parentPid}`,
    `Uid:\t${processUid}\t${processUid}\t${processUid}\t${processUid}`,
    `Gid:\t${gid}\t${gid}\t${gid}\t${gid}`,
    `Threads:\t${threads}`,
    "",
  ].join("\n");
}

function processStat({
  pid,
  parentPid,
  processGroupId,
  sessionId,
  startTimeTicks,
  name,
}) {
  return `${pid} (${name}) ${
    [
      "S",
      parentPid,
      processGroupId,
      sessionId,
      ...Array(15).fill(0),
      startTimeTicks,
    ].join(" ")
  }\n`;
}

async function withLegacyProc(processes, callback) {
  const procRoot = await mkdtemp(join(tmpdir(), "palace-legacy-proc-"));
  try {
    await mkdir(join(procRoot, "self"));
    await writeFile(
      join(procRoot, "self", "cgroup"),
      `0::${runnerCgroup}\n`,
    );
    for (const process of processes) {
      const directory = join(procRoot, String(process.pid));
      await mkdir(directory);
      await writeFile(
        join(directory, "status"),
        process.status ?? processStatus({
          pid: process.pid,
          parentPid: process.parentPid ?? 1,
          uid: process.uid ?? uid,
          name: process.name ?? "worker",
        }),
      );
      await writeFile(
        join(directory, "cgroup"),
        `0::${process.cgroup}\n`,
      );
      if (process.stat) {
        await writeFile(join(directory, "stat"), process.stat);
      }
      if (process.comm) {
        await writeFile(join(directory, "comm"), `${process.comm}\n`);
      }
      if (process.cmdline) {
        await writeFile(join(directory, "cmdline"), process.cmdline);
      }
      await writeFile(
        join(directory, "environ"),
        process.environment ?? "",
      );
      if (process.protected) {
        await chmod(join(directory, "environ"), 0o000);
      }
    }
    await callback(procRoot);
  } finally {
    await rm(procRoot, { recursive: true, force: true });
  }
}

function trustedSshProcesses({ includeMutator = false } = {}) {
  const sessionCgroup =
    "/user.slice/user-1000.slice/session-c99.scope";
  const candidatePid = 44;
  const parentPid = 51;
  const daemonPid = 61;
  return [
    {
      pid: candidatePid,
      parentPid,
      name: "sshd-session",
      cgroup: sessionCgroup,
      status: processStatus({
        pid: candidatePid,
        parentPid,
        uid,
        name: "sshd-session",
      }),
      stat: processStat({
        pid: candidatePid,
        parentPid,
        processGroupId: parentPid,
        sessionId: parentPid,
        startTimeTicks: 4400,
        name: "sshd-session",
      }),
      comm: "sshd-session",
      cmdline: "sshd-session: palace@notty\0\0\0",
      environment: `PALACE_MVP_CLAIM_PATH=${claimPath}\0`,
      protected: true,
    },
    {
      pid: parentPid,
      parentPid: daemonPid,
      uid: 0,
      name: "sshd-session",
      cgroup: sessionCgroup,
      status: processStatus({
        pid: parentPid,
        parentPid: daemonPid,
        uid: 0,
        name: "sshd-session",
      }),
      stat: processStat({
        pid: parentPid,
        parentPid: daemonPid,
        processGroupId: parentPid,
        sessionId: parentPid,
        startTimeTicks: 5100,
        name: "sshd-session",
      }),
      comm: "sshd-session",
      cmdline: "sshd-session: palace [priv]\0\0",
    },
    {
      pid: daemonPid,
      parentPid: 1,
      uid: 0,
      name: "sshd",
      cgroup: "/system.slice/ssh.service",
      status: processStatus({
        pid: daemonPid,
        parentPid: 1,
        uid: 0,
        name: "sshd",
      }),
      stat: processStat({
        pid: daemonPid,
        parentPid: 1,
        processGroupId: daemonPid,
        sessionId: daemonPid,
        startTimeTicks: 6100,
        name: "sshd",
      }),
      comm: "sshd",
      cmdline:
        "sshd: /usr/sbin/sshd -D [listener] 0 of 10-100 startups\0",
    },
    ...(includeMutator
      ? [{
          pid: 45,
          parentPid: candidatePid,
          name: "node",
          cgroup: sessionCgroup,
          environment: `PALACE_MVP_CLAIM_PATH=${claimPath}\0`,
        }]
      : []),
  ];
}

function trustedUserInfrastructureProcesses({
  includeMutator = false,
} = {}) {
  const systemdPath = "/usr/lib/systemd/systemd";
  const initCgroup =
    "/user.slice/user-1000.slice/user@1000.service/init.scope";
  const managerPid = 52;
  return [
    {
      pid: 1,
      parentPid: 0,
      uid: 0,
      name: "systemd",
      cgroup: "/init.scope",
      status: processStatus({
        pid: 1,
        parentPid: 0,
        uid: 0,
        name: "systemd",
      }),
      stat: processStat({
        pid: 1,
        parentPid: 0,
        processGroupId: 1,
        sessionId: 1,
        startTimeTicks: 100,
        name: "systemd",
      }),
      comm: "systemd",
      cmdline: `${systemdPath}\0--system\0--deserialize=75\0`,
    },
    {
      pid: managerPid,
      parentPid: 1,
      name: "systemd",
      cgroup: initCgroup,
      status: processStatus({
        pid: managerPid,
        parentPid: 1,
        uid,
        name: "systemd",
      }),
      stat: processStat({
        pid: managerPid,
        parentPid: 1,
        processGroupId: managerPid,
        sessionId: managerPid,
        startTimeTicks: 5200,
        name: "systemd",
      }),
      comm: "systemd",
      cmdline: `${systemdPath}\0--user\0--deserialize=12\0`,
      protected: true,
    },
    {
      pid: 53,
      parentPid: managerPid,
      name: "(sd-pam)",
      cgroup: initCgroup,
      status: processStatus({
        pid: 53,
        parentPid: managerPid,
        uid,
        name: "(sd-pam)",
      }),
      stat: processStat({
        pid: 53,
        parentPid: managerPid,
        processGroupId: managerPid,
        sessionId: managerPid,
        startTimeTicks: 5300,
        name: "(sd-pam)",
      }),
      comm: "(sd-pam)",
      cmdline: "(sd-pam)\0",
      protected: true,
    },
    {
      pid: 54,
      parentPid: managerPid,
      name: "gpg-agent",
      cgroup:
        "/user.slice/user-1000.slice/user@1000.service/app.slice/"
        + "gpg-agent.service",
      status: processStatus({
        pid: 54,
        parentPid: managerPid,
        uid,
        name: "gpg-agent",
        threads: 2,
      }),
      stat: processStat({
        pid: 54,
        parentPid: managerPid,
        processGroupId: 54,
        sessionId: 54,
        startTimeTicks: 5400,
        name: "gpg-agent",
      }),
      comm: "gpg-agent",
      cmdline: "/usr/bin/gpg-agent\0--supervised\0",
      protected: true,
    },
    ...(includeMutator
      ? [{
          pid: 55,
          parentPid: 54,
          name: "node",
          cgroup:
            "/user.slice/user-1000.slice/user@1000.service/app.slice/"
            + "gpg-agent.service",
          environment: `PALACE_MVP_CLAIM_PATH=${claimPath}\0`,
        }]
      : []),
  ];
}

test("legacy scan exempts only authenticated SSH session supervisor", async () => {
  await withLegacyProc(
    trustedSshProcesses({ includeMutator: true }),
    async (procRoot) => {
      let executableChecks = 0;
      assert.deepEqual(
        await legacyClaimBoundProcesses({
          claimPath,
          uid,
          procRoot,
          username: "palace",
          gid: uid,
          verifyRootExecutable: async (input) => {
            executableChecks += 1;
            assert.deepEqual(input, {
              path: "/usr/sbin/sshd",
              pid: 61,
              procRoot,
              expectedBasename: "sshd",
            });
            return true;
          },
        }),
        [45],
      );
      assert.equal(executableChecks, 1);
    },
  );
});

test("legacy scan authenticates exact protected user-session infrastructure", async () => {
  await withLegacyProc(
    trustedUserInfrastructureProcesses({ includeMutator: true }),
    async (procRoot) => {
      const executableChecks = [];
      assert.deepEqual(
        await legacyClaimBoundProcesses({
          claimPath,
          uid,
          procRoot,
          username: "palace",
          gid: uid,
          verifyRootExecutable: async (input) => {
            executableChecks.push(input);
            assert.equal(input.procRoot, procRoot);
            if (input.expectedBasename === "systemd") {
              assert.equal(input.path, "/usr/lib/systemd/systemd");
              assert.ok([1, 52, 53].includes(input.pid));
            } else {
              assert.equal(input.expectedBasename, "gpg-agent");
              assert.equal(input.path, "/usr/bin/gpg-agent");
              assert.equal(input.pid, 54);
            }
            return true;
          },
          verifySessionManager: async (input) => {
            assert.equal(input.managerPid, 52);
            assert.equal(input.uid, uid);
            assert.equal(input.gid, uid);
            return true;
          },
          verifyGpgUnit: async (input) => {
            assert.equal(input.pid, 54);
            assert.equal(input.executable, "/usr/bin/gpg-agent");
            return true;
          },
        }),
        [55],
      );
      assert.ok(
        executableChecks.some(
          (input) =>
            input.pid === 53
            && input.expectedBasename === "systemd",
        ),
      );
      assert.ok(
        executableChecks.some(
          (input) =>
            input.pid === 54
            && input.expectedBasename === "gpg-agent",
        ),
      );
    },
  );
});

test("legacy scan rejects unknown protected process outside runner cgroup", async () => {
  await withLegacyProc([{
    pid: 41,
    cgroup:
      "/user.slice/user-1000.slice/user@1000.service/app.slice/"
      + "unknown.service",
    environment: `PALACE_MVP_CLAIM_PATH=${claimPath}\0`,
    protected: true,
  }], async (procRoot) => {
    await assert.rejects(
      legacyClaimBoundProcesses({ claimPath, uid, procRoot }),
      /not authenticated non-mutating session infrastructure/,
    );
  });
});

test("legacy scan rejects protected process inside runner cgroup", async () => {
  await withLegacyProc([{
    pid: 42,
    cgroup: `${runnerCgroup}/worker`,
    environment: `PALACE_MVP_CLAIM_PATH=${claimPath}\0`,
    protected: true,
  }], async (procRoot) => {
    await assert.rejects(
      legacyClaimBoundProcesses({ claimPath, uid, procRoot }),
      /not authenticated non-mutating session infrastructure/,
    );
  });
});

test("legacy scan rejects SSH-looking process without verified daemon", async () => {
  await withLegacyProc(trustedSshProcesses(), async (procRoot) => {
    await assert.rejects(
      legacyClaimBoundProcesses({
        claimPath,
        uid,
        procRoot,
        username: "palace",
        gid: uid,
        verifyRootExecutable: async () => false,
      }),
      /not authenticated non-mutating session infrastructure/,
    );
  });
});

test("legacy scan counts readable binding outside runner cgroup", async () => {
  await withLegacyProc([{
    pid: 43,
    cgroup: "/user.slice/sshd-session.scope",
    environment: `OTHER=value\0PALACE_MVP_CLAIM_PATH=${claimPath}\0`,
  }], async (procRoot) => {
    assert.deepEqual(
      await legacyClaimBoundProcesses({ claimPath, uid, procRoot }),
      [43],
    );
  });
});

test("retires exact v2 claim slice before release work", async () => {
  const calls = [];
  const result = await retireClaimScope({
    claim: {
      ...common,
      status: "active-pre-gate3",
      createdAtUnixMs: 1,
    },
    claimPath: "/var/tmp/logos-palace-1000/active.json",
    uid,
    systemctl: "/tools/systemctl",
    retireScope: async (input) => {
      calls.push(input);
      return { residueKilled: true };
    },
  });
  assert.equal(result, "residue-killed");
  assert.deepEqual(calls, [{
    slice: common.processScopeSlice,
    systemctl: "/tools/systemctl",
  }]);
});

test("rejects malformed or cross-UID v2 claim before retirement", async () => {
  for (const claim of [
    {
      ...common,
      status: "active-pre-gate3",
      createdAtUnixMs: 1,
      extra: true,
    },
    {
      ...common,
      uid: uid + 1,
      status: "active-pre-gate3",
      createdAtUnixMs: 1,
    },
    {
      ...common,
      runDirectory: "/tmp/runs/run.Other123",
      status: "active-pre-gate3",
      createdAtUnixMs: 1,
    },
  ]) {
    let retired = false;
    await assert.rejects(
      retireClaimScope({
        claim,
        claimPath: "/var/tmp/logos-palace-1000/active.json",
        uid,
        systemctl: "/tools/systemctl",
        retireScope: async () => {
          retired = true;
          return { residueKilled: false };
        },
      }),
    );
    assert.equal(retired, false);
  }
});

test("legacy claim must have no claim-bound processes", async () => {
  const legacy = {
    schema: "logos.palace.basecamp-active-run-claim",
    version: 1,
    uid,
    releaseProgramId,
    releaseRootId,
    runDirectory: "/tmp/runs/run.ABCDef12",
    productSnapshot:
      "/nix/store/00000000000000000000000000000000-product",
    gcRootPath:
      "/var/tmp/logos-palace-1000/gc-roots/run.ABCDef12/product-snapshot",
    gcRootTarget:
      "/nix/store/00000000000000000000000000000000-product",
    gitCommit: auditedLegacyPreGate3.gitCommit,
    snapshotNarHash: auditedLegacyPreGate3.snapshotNarHash,
    snapshotNarSize: auditedLegacyPreGate3.snapshotNarSize,
    snapshotRunnerSha256: auditedLegacyPreGate3.snapshotRunnerSha256,
    runtimeManifestPath:
      "/tmp/runs/run.ABCDef12/runtime-output-manifest.json",
    runtimeManifestSha256:
      auditedLegacyPreGate3.runtimeManifestSha256,
    status: "active",
    createdAtUnixMs: 1,
  };
  assert.equal(
    await retireClaimScope({
      claim: legacy,
      claimPath: "/var/tmp/logos-palace-1000/active.json",
      uid,
      systemctl: "/tools/systemctl",
      scanLegacy: async () => [],
    }),
    "legacy-clean",
  );
  await assert.rejects(
    retireClaimScope({
      claim: legacy,
      claimPath: "/var/tmp/logos-palace-1000/active.json",
      uid,
      systemctl: "/tools/systemctl",
      scanLegacy: async () => [42],
    }),
    /retains bound processes/,
  );
});

test("absent claim performs no retirement", async () => {
  assert.equal(
    await retireClaimScope({
      claim: undefined,
      claimPath: "/var/tmp/logos-palace-1000/active.json",
      uid,
      systemctl: "/tools/systemctl",
    }),
    "no-claim",
  );
});
