#!/usr/bin/env node

import { spawn as spawnChild } from "node:child_process";
import {
  accessSync,
  closeSync,
  constants as fsConstants,
  fstatSync,
  lstatSync,
  openSync,
  readFileSync,
  readSync,
  realpathSync,
  statfsSync,
  writeSync,
} from "node:fs";
import { constants as osConstants } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import {
  parseUnifiedCgroup,
  validateControlGroups,
  validateScopeIdentity,
} from "./basecamp_scope.mjs";

const cgroup2Magic = 0x63677270;

export function guardianScopePaths({
  cgroupBytes,
  unit,
  slice,
  cgroupRoot = "/sys/fs/cgroup",
}) {
  validateScopeIdentity({ unit, slice });
  if (typeof cgroupRoot !== "string" || resolve(cgroupRoot) !== cgroupRoot) {
    throw new Error("guardian cgroup root is not absolute");
  }
  const controlGroup = parseUnifiedCgroup(cgroupBytes);
  const sliceControlGroup = dirname(controlGroup);
  validateControlGroups({
    pidControlGroup: controlGroup,
    unitControlGroup: controlGroup,
    sliceControlGroup,
    unit,
    slice,
  });
  const cgroupPath = join(cgroupRoot, controlGroup);
  return {
    controlGroup,
    sliceControlGroup,
    cgroupPath,
    killPath: join(cgroupPath, "cgroup.kill"),
    procsPath: join(cgroupPath, "cgroup.procs"),
  };
}

export function openGuardianKill({
  unit,
  slice,
  procRoot = "/proc",
  cgroupRoot = "/sys/fs/cgroup",
  pid = process.pid,
  read = readFileSync,
  canonical = realpathSync,
  metadata = lstatSync,
  filesystem = statfsSync,
  checkAccess = accessSync,
  open = openSync,
  close = closeSync,
  fileMetadata = fstatSync,
}) {
  if (!Number.isSafeInteger(pid) || pid <= 0) {
    throw new TypeError("guardian PID is invalid");
  }
  if (
    canonical(cgroupRoot) !== cgroupRoot
    || Number(filesystem(cgroupRoot).type) !== cgroup2Magic
  ) {
    throw new Error("guardian cgroup root is not canonical cgroup v2");
  }
  const before = guardianScopePaths({
    cgroupBytes: read(`${procRoot}/${pid}/cgroup`),
    unit,
    slice,
    cgroupRoot,
  });
  if (
    canonical(before.cgroupPath) !== before.cgroupPath
    || !metadata(before.cgroupPath).isDirectory()
    || canonical(before.killPath) !== before.killPath
    || !metadata(before.killPath).isFile()
    || canonical(before.procsPath) !== before.procsPath
    || !metadata(before.procsPath).isFile()
  ) {
    throw new Error("guardian cgroup control path is not canonical");
  }
  checkAccess(before.killPath, fsConstants.W_OK);
  const killFd = open(
    before.killPath,
    fsConstants.O_WRONLY | (fsConstants.O_CLOEXEC ?? 0),
  );
  let procsFd;
  try {
    if (!fileMetadata(killFd).isFile()) {
      throw new Error("guardian cgroup.kill descriptor is not a file");
    }
    procsFd = open(
      before.procsPath,
      fsConstants.O_RDONLY | (fsConstants.O_CLOEXEC ?? 0),
    );
    if (!fileMetadata(procsFd).isFile()) {
      throw new Error("guardian cgroup.procs descriptor is not a file");
    }
    const after = guardianScopePaths({
      cgroupBytes: read(`${procRoot}/${pid}/cgroup`),
      unit,
      slice,
      cgroupRoot,
    });
    if (
      after.controlGroup !== before.controlGroup
      || after.cgroupPath !== before.cgroupPath
      || after.killPath !== before.killPath
    ) {
      throw new Error("guardian changed cgroup while opening cgroup.kill");
    }
    return { ...before, killFd, procsFd };
  } catch (error) {
    if (Number.isSafeInteger(procsFd)) close(procsFd);
    close(killFd);
    throw error;
  }
}

export function guardianCgroupMembers({
  procsFd,
  read = readSync,
}) {
  if (
    !Number.isSafeInteger(procsFd)
    || procsFd < 0
    || typeof read !== "function"
  ) {
    throw new TypeError("guardian cgroup.procs inputs are invalid");
  }
  const buffer = Buffer.alloc(64 * 1024 + 1);
  const size = read(procsFd, buffer, 0, buffer.length, 0);
  if (
    !Number.isSafeInteger(size)
    || size <= 0
    || size > 64 * 1024
  ) {
    throw new Error("guardian cgroup.procs exceeds bounds");
  }
  const encoded = buffer.subarray(0, size).toString("utf8");
  if (!/^(?:[1-9][0-9]*\n)+$/.test(encoded)) {
    throw new Error("guardian cgroup.procs is invalid");
  }
  const members = encoded.trimEnd().split("\n").map(Number);
  if (
    members.some(
      (member) => !Number.isSafeInteger(member) || member <= 0,
    )
    || new Set(members).size !== members.length
  ) {
    throw new Error("guardian cgroup.procs members are invalid");
  }
  return members.sort((left, right) => left - right);
}

export function retireGuardianResidue({
  killFd,
  procsFd,
  pid = process.pid,
  members = guardianCgroupMembers,
  write = writeSync,
  terminate = (status) => process.exit(status),
}) {
  if (
    !Number.isSafeInteger(killFd)
    || killFd < 0
    || !Number.isSafeInteger(pid)
    || pid <= 0
    || typeof members !== "function"
    || typeof write !== "function"
    || typeof terminate !== "function"
  ) {
    throw new TypeError("guardian residue inputs are invalid");
  }
  const first = members({ procsFd });
  const second = members({ procsFd });
  if (
    first.length === 1
    && first[0] === pid
    && second.length === 1
    && second[0] === pid
  ) {
    return false;
  }
  write(killFd, "1\n");
  terminate(125);
  return true;
}

export function createParentDeathHandler({
  killFd,
  write = writeSync,
  terminate = (status) => process.exit(status),
}) {
  if (!Number.isSafeInteger(killFd) || killFd < 0) {
    throw new TypeError("guardian cgroup.kill descriptor is invalid");
  }
  let triggered = false;
  return () => {
    if (triggered) return;
    triggered = true;
    try {
      write(killFd, "1\n");
    } finally {
      terminate(125);
    }
  };
}

function childStatus(code, signal) {
  if (Number.isSafeInteger(code) && code >= 0 && code <= 255) return code;
  const signalNumber = osConstants.signals[signal];
  if (
    typeof signal === "string"
    && Number.isSafeInteger(signalNumber)
    && signalNumber > 0
  ) {
    return Math.min(255, 128 + signalNumber);
  }
  return 125;
}

export async function runScopeGuardian({
  unit,
  slice,
  command,
  openKill = openGuardianKill,
  spawn = spawnChild,
  signalTarget = process,
  close = closeSync,
  write = writeSync,
  terminate = (status) => process.exit(status),
  retireResidue = retireGuardianResidue,
}) {
  if (
    !Array.isArray(command)
    || command.length === 0
    || typeof command[0] !== "string"
    || resolve(command[0]) !== command[0]
  ) {
    throw new TypeError("guardian command is invalid");
  }
  const { killFd, procsFd } = openKill({ unit, slice });
  const parentDeath = createParentDeathHandler({
    killFd,
    write,
    terminate,
  });
  signalTarget.on("SIGTERM", parentDeath);
  try {
    const child = spawn(command[0], command.slice(1), {
      stdio: "inherit",
      env: process.env,
    });
    const status = await new Promise((resolvePromise, reject) => {
      child.once("error", reject);
      child.once("exit", (code, signal) => {
        resolvePromise(childStatus(code, signal));
      });
    });
    if (retireResidue({
      killFd,
      procsFd,
      write,
      terminate,
    })) {
      throw new Error("guardian exact cgroup residue was killed");
    }
    return status;
  } finally {
    signalTarget.off("SIGTERM", parentDeath);
    close(procsFd);
    close(killFd);
  }
}

async function main() {
  const [unit, slice, ...command] = process.argv.slice(2);
  if (command.length === 0) {
    throw new Error(
      "usage: basecamp_scope_guardian.mjs <unit> <slice> <command> [args...]",
    );
  }
  process.exitCode = await runScopeGuardian({ unit, slice, command });
}

if (
  process.argv[1]
  && resolve(process.argv[1]) === fileURLToPath(import.meta.url)
) {
  await main();
}
