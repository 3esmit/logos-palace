#!/usr/bin/env node

import assert from "node:assert/strict";
import { EventEmitter } from "node:events";
import test from "node:test";
import {
  createParentDeathHandler,
  guardianCgroupMembers,
  guardianScopePaths,
  openGuardianKill,
  retireGuardianResidue,
  runScopeGuardian,
} from "./basecamp_scope_guardian.mjs";

const slice = "logos-palace-run-AB12cd34.slice";
const unit = "logos-palace-run-AB12cd34-gate2-Ef56Gh78.scope";
const sliceControlGroup =
  "/user.slice/user-1000.slice/user@1000.service/"
  + slice;
const controlGroup = `${sliceControlGroup}/${unit}`;

test("binds guardian kill control to exact own unit and slice", () => {
  assert.deepEqual(
    guardianScopePaths({
      cgroupBytes: Buffer.from(`0::${controlGroup}\n`),
      unit,
      slice,
    }),
    {
      controlGroup,
      sliceControlGroup,
      cgroupPath: `/sys/fs/cgroup${controlGroup}`,
      killPath: `/sys/fs/cgroup${controlGroup}/cgroup.kill`,
      procsPath: `/sys/fs/cgroup${controlGroup}/cgroup.procs`,
    },
  );
  for (const candidate of [
    `0::${controlGroup}/nested\n`,
    `0::${sliceControlGroup}/other.scope\n`,
    "0::/\n",
  ]) {
    assert.throws(
      () =>
        guardianScopePaths({
          cgroupBytes: Buffer.from(candidate),
          unit,
          slice,
        }),
      /cgroup|ControlGroup/,
    );
  }
});

test("opens only stable writable cgroup.kill identity", () => {
  const reads = [];
  const opens = [];
  const result = openGuardianKill({
    unit,
    slice,
    pid: 4321,
    read: (path) => {
      reads.push(path);
      return Buffer.from(`0::${controlGroup}\n`);
    },
    canonical: (path) => path,
    metadata: (path) => ({
      isDirectory: () => path.endsWith(unit),
      isFile: () =>
        path.endsWith("cgroup.kill") || path.endsWith("cgroup.procs"),
    }),
    filesystem: () => ({ type: 0x63677270 }),
    checkAccess: (path, mode) => {
      assert.equal(path, `/sys/fs/cgroup${controlGroup}/cgroup.kill`);
      assert.ok(Number.isSafeInteger(mode));
    },
    open: (...args) => {
      opens.push(args);
      return opens.length === 1 ? 19 : 20;
    },
    fileMetadata: () => ({ isFile: () => true }),
  });
  assert.equal(result.killFd, 19);
  assert.equal(result.procsFd, 20);
  assert.deepEqual(reads, [
    "/proc/4321/cgroup",
    "/proc/4321/cgroup",
  ]);
  assert.equal(opens.length, 2);
  assert.equal(opens[0][0], `/sys/fs/cgroup${controlGroup}/cgroup.kill`);
  assert.equal(opens[1][0], `/sys/fs/cgroup${controlGroup}/cgroup.procs`);
});

test("closes kill descriptor when guardian identity changes", () => {
  let reads = 0;
  const closed = [];
  assert.throws(
    () =>
      openGuardianKill({
        unit,
        slice,
        pid: 4321,
        read: () => {
          reads += 1;
          return Buffer.from(
            reads === 1
              ? `0::${controlGroup}\n`
              : `0::${sliceControlGroup}/other.scope\n`,
          );
        },
        canonical: (path) => path,
        metadata: (path) => ({
          isDirectory: () => path.endsWith(unit),
          isFile: () =>
            path.endsWith("cgroup.kill") || path.endsWith("cgroup.procs"),
        }),
        filesystem: () => ({ type: 0x63677270 }),
        checkAccess: () => {},
        open: (path) => path.endsWith("cgroup.kill") ? 21 : 22,
        close: (fd) => closed.push(fd),
        fileMetadata: () => ({ isFile: () => true }),
      }),
    /ControlGroup identities differ/,
  );
  assert.deepEqual(closed, [22, 21]);
});

test("reads bounded exact cgroup membership", () => {
  const reads = [];
  assert.deepEqual(
    guardianCgroupMembers({
      procsFd: 24,
      read: (fd, buffer, offset, length, position) => {
        reads.push({ fd, offset, length, position });
        return buffer.write("19\n23\n", offset);
      },
    }),
    [19, 23],
  );
  assert.deepEqual(reads, [{
    fd: 24,
    offset: 0,
    length: 64 * 1024 + 1,
    position: 0,
  }]);
});

test("kills exact cgroup before disarming when descendant remains", () => {
  const operations = [];
  const snapshots = [[31, 32], [31, 32]];
  assert.equal(
    retireGuardianResidue({
      killFd: 25,
      procsFd: 26,
      pid: 31,
      members: () => snapshots.shift(),
      write: (...args) => operations.push(["write", ...args]),
      terminate: (status) => operations.push(["terminate", status]),
    }),
    true,
  );
  assert.deepEqual(operations, [
    ["write", 25, "1\n"],
    ["terminate", 125],
  ]);
});

test("disarms only after two sole-guardian membership reads", () => {
  let reads = 0;
  assert.equal(
    retireGuardianResidue({
      killFd: 27,
      procsFd: 28,
      pid: 31,
      members: () => {
        reads += 1;
        return [31];
      },
      write: () => assert.fail("must not kill sole guardian"),
      terminate: () => assert.fail("must not terminate sole guardian"),
    }),
    false,
  );
  assert.equal(reads, 2);
});

test("parent death requests exact cgroup kill once before terminating", () => {
  const operations = [];
  const handler = createParentDeathHandler({
    killFd: 23,
    write: (...args) => operations.push(["write", ...args]),
    terminate: (status) => operations.push(["terminate", status]),
  });
  handler();
  handler();
  assert.deepEqual(operations, [
    ["write", 23, "1\n"],
    ["terminate", 125],
  ]);
});

test("registers parent-death handler before spawning gate command", async () => {
  const signalTarget = new EventEmitter();
  const child = new EventEmitter();
  const operations = [];
  const status = runScopeGuardian({
    unit,
    slice,
    command: ["/usr/bin/setsid", "--wait", "/bin/true"],
    openKill: () => ({ killFd: 29, procsFd: 30 }),
    signalTarget,
    spawn: (command, args, options) => {
      operations.push([
        "spawn",
        signalTarget.listenerCount("SIGTERM"),
        command,
        args,
        options.stdio,
      ]);
      queueMicrotask(() => child.emit("exit", 7, null));
      return child;
    },
    close: (fd) => operations.push(["close", fd]),
    retireResidue: () => false,
  });
  assert.equal(await status, 7);
  assert.deepEqual(operations, [
    [
      "spawn",
      1,
      "/usr/bin/setsid",
      ["--wait", "/bin/true"],
      "inherit",
    ],
    ["close", 30],
    ["close", 29],
  ]);
  assert.equal(signalTarget.listenerCount("SIGTERM"), 0);
});

test("guardian rejects relative command before opening cgroup control", async () => {
  let opened = false;
  await assert.rejects(
    runScopeGuardian({
      unit,
      slice,
      command: ["setsid", "--wait", "/bin/true"],
      openKill: () => {
        opened = true;
        return { killFd: 31 };
      },
    }),
    /guardian command is invalid/,
  );
  assert.equal(opened, false);
});
