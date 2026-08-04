#!/usr/bin/env node

import { execFile } from "node:child_process";
import { constants } from "node:fs";
import {
  access,
  lstat,
  readFile,
  readlink,
  readdir,
  realpath,
} from "node:fs/promises";
import { userInfo } from "node:os";
import { basename, dirname, join, resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { promisify } from "node:util";
import {
  releaseProgramId,
  releaseRootId,
  validateStoredLegacyClaim,
  validateStoredV2Claim,
} from "./basecamp_claim_lifecycle.mjs";
import { verifyReleaseLock } from "./basecamp_release_lock.mjs";
import { retireScopeSlice } from "./basecamp_scope.mjs";
const maximumClaimBytes = 64 * 1024;
const maximumProcEntries = 65_536;
const maximumStatusBytes = 64 * 1024;
const maximumCgroupBytes = 64 * 1024;
const maximumEnvironmentBytes = 1024 * 1024;
const maximumProcessIdentityBytes = 4096;
const execFileAsync = promisify(execFile);

function exactAbsoluteExecutable(candidate, expected) {
  return (
    typeof candidate === "string"
    && resolve(candidate) === candidate
    && candidate === expected
  );
}

async function canonicalOwnerDirectory(path, uid, mode) {
  const metadata = await lstat(path);
  if (
    metadata.isSymbolicLink()
    || !metadata.isDirectory()
    || metadata.uid !== uid
    || (metadata.mode & 0o777) !== mode
    || await realpath(path) !== path
  ) {
    throw new Error("active-scope preflight directory is not owner-only state");
  }
}

async function readSecureClaim(path, uid) {
  let metadata;
  try {
    metadata = await lstat(path);
  } catch (error) {
    if (error?.code === "ENOENT") return undefined;
    throw error;
  }
  if (
    metadata.isSymbolicLink()
    || !metadata.isFile()
    || metadata.uid !== uid
    || (metadata.mode & 0o777) !== 0o600
    || metadata.size <= 0
    || metadata.size > maximumClaimBytes
    || await realpath(path) !== path
  ) {
    throw new Error("active-scope preflight claim is not a secure file");
  }
  const bytes = await readFile(path);
  if (bytes.length !== metadata.size) {
    throw new Error("active-scope preflight claim changed while being read");
  }
  try {
    return JSON.parse(bytes);
  } catch {
    throw new Error("active-scope preflight claim is not valid JSON");
  }
}

function effectiveUid(status, pid) {
  if (status.length <= 0 || status.length > maximumStatusBytes) {
    throw new Error(`process ${pid} status exceeds preflight bounds`);
  }
  const value = Number(
    status.toString("utf8").match(
      /^Uid:\s+[0-9]+\s+([0-9]+)\s+[0-9]+\s+[0-9]+$/m,
    )?.[1],
  );
  if (!Number.isSafeInteger(value) || value < 0) {
    throw new Error(`process ${pid} effective UID is invalid`);
  }
  return value;
}

function cgroupPath(bytes, description) {
  if (bytes.length <= 0 || bytes.length > maximumCgroupBytes) {
    throw new Error(`${description} exceeds legacy preflight bounds`);
  }
  const match = bytes.toString("utf8").match(/^0::(\/[^\0\n]*)\n$/);
  if (
    !match
    || match[1].length > 4096
    || resolve(match[1]) !== match[1]
    || match[1].includes("//")
  ) {
    throw new Error(`${description} is not one canonical cgroup-v2 path`);
  }
  return match[1];
}

function inCgroupSubtree(path, root) {
  return root === "/" || path === root || path.startsWith(`${root}/`);
}

function exactStatusField(status, field) {
  const matches = [
    ...status.matchAll(new RegExp(`^${field}:\\s*(.*)$`, "gm")),
  ];
  return matches.length === 1 ? matches[0][1] : undefined;
}

function numericStatusField(status, field) {
  const encoded = exactStatusField(status, field);
  if (!/^(0|[1-9][0-9]*)$/.test(encoded ?? "")) return undefined;
  const value = Number(encoded);
  return Number.isSafeInteger(value) ? value : undefined;
}

function processStatusIdentity(bytes, pid) {
  if (bytes.length <= 0 || bytes.length > maximumStatusBytes) {
    return undefined;
  }
  const status = bytes.toString("utf8");
  const uidValues = exactStatusField(status, "Uid")
    ?.split(/\s+/)
    .map(Number);
  const gidValues = exactStatusField(status, "Gid")
    ?.split(/\s+/)
    .map(Number);
  const identity = {
    name: exactStatusField(status, "Name"),
    tgid: numericStatusField(status, "Tgid"),
    pid: numericStatusField(status, "Pid"),
    parentPid: numericStatusField(status, "PPid"),
    uidValues,
    gidValues,
    threads: numericStatusField(status, "Threads"),
  };
  if (
    identity.pid !== pid
    || identity.tgid !== pid
    || !Number.isSafeInteger(identity.parentPid)
    || identity.parentPid < 0
    || !Array.isArray(uidValues)
    || uidValues.length !== 4
    || uidValues.some(
      (value) => !Number.isSafeInteger(value) || value < 0,
    )
    || !Array.isArray(gidValues)
    || gidValues.length !== 4
    || gidValues.some(
      (value) => !Number.isSafeInteger(value) || value < 0,
    )
    || !Number.isSafeInteger(identity.threads)
    || identity.threads <= 0
    || identity.threads > 1024
  ) {
    return undefined;
  }
  return identity;
}

function processStatIdentity(bytes, pid) {
  if (
    bytes.length <= 0
    || bytes.length > maximumProcessIdentityBytes
  ) {
    return undefined;
  }
  const encoded = bytes.toString("utf8").trim();
  const close = encoded.lastIndexOf(") ");
  if (!encoded.startsWith(`${pid} (`) || close < 3) return undefined;
  const fields = encoded.slice(close + 2).split(" ");
  const identity = {
    name: encoded.slice(encoded.indexOf("(") + 1, close),
    parentPid: Number(fields[1]),
    processGroupId: Number(fields[2]),
    sessionId: Number(fields[3]),
    startTimeTicks: Number(fields[19]),
  };
  if (
    fields.length < 20
    || !Number.isSafeInteger(identity.parentPid)
    || identity.parentPid < 0
    || !Number.isSafeInteger(identity.processGroupId)
    || identity.processGroupId <= 0
    || !Number.isSafeInteger(identity.sessionId)
    || identity.sessionId <= 0
    || !Number.isSafeInteger(identity.startTimeTicks)
    || identity.startTimeTicks <= 0
  ) {
    return undefined;
  }
  return identity;
}

function exactComm(bytes) {
  if (
    bytes.length <= 1
    || bytes.length > 64
    || bytes.at(-1) !== 0x0a
    || bytes.subarray(0, -1).includes(0x0a)
    || bytes.includes(0x00)
  ) {
    return undefined;
  }
  return bytes.subarray(0, -1).toString("utf8");
}

function exactPaddedArgvLabel(bytes) {
  if (
    bytes.length <= 1
    || bytes.length > maximumProcessIdentityBytes
    || bytes.at(-1) !== 0x00
  ) {
    return undefined;
  }
  let end = bytes.length - 1;
  while (end > 0 && bytes[end - 1] === 0x00) end -= 1;
  if (end <= 0 || bytes.subarray(0, end).includes(0x00)) {
    return undefined;
  }
  return bytes.subarray(0, end).toString("utf8");
}

function exactArgv(bytes) {
  if (
    bytes.length <= 1
    || bytes.length > maximumProcessIdentityBytes
    || bytes.at(-1) !== 0x00
  ) {
    return undefined;
  }
  const values = bytes.toString("utf8").split("\0");
  values.pop();
  if (values.length === 0 || values.some((value) => value.length === 0)) {
    return undefined;
  }
  return values;
}

async function processIdentitySnapshot(procRoot, pid) {
  const directory = join(procRoot, String(pid));
  const [
    statusBytes,
    statBytes,
    cgroupBytes,
    commBytes,
    cmdline,
  ] = await Promise.all([
    readFile(join(directory, "status")),
    readFile(join(directory, "stat")),
    readFile(join(directory, "cgroup")),
    readFile(join(directory, "comm")),
    readFile(join(directory, "cmdline")),
  ]);
  const status = processStatusIdentity(statusBytes, pid);
  const stat = processStatIdentity(statBytes, pid);
  const comm = exactComm(commBytes);
  if (!status || !stat || !comm) return undefined;
  return {
    status,
    stat,
    cgroup: cgroupPath(cgroupBytes, `process ${pid} cgroup`),
    comm,
    cmdline,
  };
}

function stableSnapshotToken(snapshot) {
  return JSON.stringify({
    ...snapshot,
    cmdline: snapshot.cmdline.toString("base64"),
  });
}

async function canonicalRootExecutable({
  path,
  pid,
  procRoot,
  expectedBasename,
}) {
  if (
    typeof path !== "string"
    || path.length <= 0
    || path.length > 4096
    || resolve(path) !== path
    || basename(path) !== expectedBasename
    || path.includes("\0")
    || path.includes("//")
  ) {
    return false;
  }
  let metadata;
  try {
    metadata = await lstat(path);
  } catch {
    return false;
  }
  if (
    metadata.isSymbolicLink()
    || !metadata.isFile()
    || metadata.uid !== 0
    || (metadata.mode & 0o022) !== 0
    || (metadata.mode & 0o100) === 0
    || metadata.size <= 0
    || metadata.nlink <= 0
  ) {
    return false;
  }
  try {
    if (await realpath(path) !== path) return false;
  } catch {
    return false;
  }
  try {
    return await readlink(join(procRoot, String(pid), "exe")) === path;
  } catch (error) {
    if (["EACCES", "EPERM"].includes(error?.code)) {
      // These session daemons are normally nondumpable. Exact root-controlled
      // ancestry, service cgroup, process labels, and canonical binary remain.
      return true;
    }
    return false;
  }
}

async function trustedSshSessionSupervisor({
  pid,
  uid,
  ownCgroup,
  observedCgroup,
  procRoot,
  username,
  gid,
  verifyRootExecutable,
}) {
  const sessionPattern = new RegExp(
    `^/user\\.slice/user-${uid}\\.slice/session-`
      + "(?:c[1-9][0-9]*|[1-9][0-9]*)\\.scope$",
  );
  if (
    inCgroupSubtree(observedCgroup, ownCgroup)
    || !sessionPattern.test(observedCgroup)
    || !/^[A-Za-z_][A-Za-z0-9_.-]{0,63}$/.test(username)
  ) {
    return false;
  }

  const candidate = await processIdentitySnapshot(procRoot, pid);
  if (!candidate) return false;
  const parentPid = candidate.status.parentPid;
  const parent = await processIdentitySnapshot(procRoot, parentPid);
  if (!parent) return false;
  const daemonPid = parent.status.parentPid;
  const daemon = await processIdentitySnapshot(procRoot, daemonPid);
  if (!daemon) return false;

  const candidateLabel = exactPaddedArgvLabel(candidate.cmdline);
  const parentLabel = exactPaddedArgvLabel(parent.cmdline);
  const daemonLabel = exactPaddedArgvLabel(daemon.cmdline);
  const daemonMatch = daemonLabel?.match(
    /^sshd: (\/[^ \0]+\/sshd) -D \[listener\] ([0-9]+) of ([0-9]+)-([0-9]+) startups$/,
  );
  const startupCounts = daemonMatch?.slice(2).map(Number);
  if (
    candidate.cgroup !== observedCgroup
    || candidate.status.name !== "sshd-session"
    || candidate.stat.name !== "sshd-session"
    || candidate.comm !== "sshd-session"
    || candidate.status.uidValues.some((value) => value !== uid)
    || candidate.status.gidValues.some((value) => value !== gid)
    || candidate.status.threads !== 1
    || candidate.status.parentPid !== candidate.stat.parentPid
    || candidate.stat.processGroupId !== parentPid
    || candidate.stat.sessionId !== parentPid
    || !new RegExp(
      `^sshd-session: ${username.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`
        + "@(?:notty|pts/[0-9]+)$",
    ).test(candidateLabel ?? "")
    || parent.cgroup !== observedCgroup
    || parent.status.name !== "sshd-session"
    || parent.stat.name !== "sshd-session"
    || parent.comm !== "sshd-session"
    || parent.status.uidValues.some((value) => value !== 0)
    || parent.status.gidValues.some((value) => value !== 0)
    || parent.status.threads !== 1
    || parent.status.parentPid !== parent.stat.parentPid
    || parent.stat.processGroupId !== parentPid
    || parent.stat.sessionId !== parentPid
    || parentLabel !== `sshd-session: ${username} [priv]`
    || (
      daemon.cgroup !== "/system.slice/ssh.service"
      && daemon.cgroup !== "/system.slice/sshd.service"
    )
    || daemon.status.name !== "sshd"
    || daemon.stat.name !== "sshd"
    || daemon.comm !== "sshd"
    || daemon.status.uidValues.some((value) => value !== 0)
    || daemon.status.gidValues.some((value) => value !== 0)
    || daemon.status.threads !== 1
    || daemon.status.parentPid !== 1
    || daemon.stat.parentPid !== 1
    || daemon.stat.processGroupId !== daemonPid
    || daemon.stat.sessionId !== daemonPid
    || !daemonMatch
    || startupCounts.some(
      (value) =>
        !Number.isSafeInteger(value)
        || value < 0
        || value > 1_000_000,
    )
    || startupCounts[0] > startupCounts[2]
    || startupCounts[1] > startupCounts[2]
    || !await verifyRootExecutable({
      path: daemonMatch[1],
      pid: daemonPid,
      procRoot,
      expectedBasename: "sshd",
    })
  ) {
    return false;
  }

  const [daemonStable, parentStable, candidateStable] = await Promise.all([
    processIdentitySnapshot(procRoot, daemonPid),
    processIdentitySnapshot(procRoot, parentPid),
    processIdentitySnapshot(procRoot, pid),
  ]);
  return (
    daemonStable !== undefined
    && parentStable !== undefined
    && candidateStable !== undefined
    && stableSnapshotToken(daemonStable) === stableSnapshotToken(daemon)
    && stableSnapshotToken(parentStable) === stableSnapshotToken(parent)
    && stableSnapshotToken(candidateStable) === stableSnapshotToken(candidate)
  );
}

function exactCredentials(snapshot, uid, gid) {
  return (
    snapshot.status.uidValues.every((value) => value === uid)
    && snapshot.status.gidValues.every((value) => value === gid)
  );
}

function validDeserializeArgument(value) {
  if (!/^--deserialize=(0|[1-9][0-9]*)$/.test(value ?? "")) {
    return false;
  }
  const descriptor = Number(value.slice("--deserialize=".length));
  return Number.isSafeInteger(descriptor) && descriptor >= 0;
}

function systemdManagerExecutable({
  manager,
  managerPid,
  init,
  uid,
  gid,
}) {
  const managerArgv = exactArgv(manager.cmdline);
  const initArgv = exactArgv(init.cmdline);
  const managerCgroup =
    `/user.slice/user-${uid}.slice/user@${uid}.service/init.scope`;
  if (
    manager.cgroup !== managerCgroup
    || manager.status.name !== "systemd"
    || manager.stat.name !== "systemd"
    || manager.comm !== "systemd"
    || !exactCredentials(manager, uid, gid)
    || manager.status.threads !== 1
    || manager.status.parentPid !== 1
    || manager.stat.parentPid !== 1
    || manager.stat.processGroupId !== managerPid
    || manager.stat.sessionId !== managerPid
    || managerArgv?.length !== 3
    || managerArgv[1] !== "--user"
    || !validDeserializeArgument(managerArgv[2])
    || init.cgroup !== "/init.scope"
    || init.status.name !== "systemd"
    || init.stat.name !== "systemd"
    || init.comm !== "systemd"
    || !exactCredentials(init, 0, 0)
    || init.status.threads !== 1
    || init.status.parentPid !== 0
    || init.stat.parentPid !== 0
    || init.stat.processGroupId !== 1
    || init.stat.sessionId !== 1
    || initArgv?.length !== 3
    || initArgv[0] !== managerArgv?.[0]
    || initArgv[1] !== "--system"
    || !validDeserializeArgument(initArgv[2])
    || basename(managerArgv[0]) !== "systemd"
  ) {
    return undefined;
  }
  return managerArgv[0];
}

async function stableProcessSnapshots(procRoot, expected) {
  const unique = new Map(
    expected.map(({ pid, snapshot }) => [pid, snapshot]),
  );
  const observed = await Promise.all(
    [...unique.keys()].map(
      async (pid) => [pid, await processIdentitySnapshot(procRoot, pid)],
    ),
  );
  return observed.every(([pid, snapshot]) =>
    snapshot !== undefined
    && stableSnapshotToken(snapshot)
      === stableSnapshotToken(unique.get(pid))
  );
}

async function trustedToolOutput({
  tool,
  expectedBasename,
  args,
  uid,
}) {
  try {
    if (
      resolve(tool) !== tool
      || basename(tool) !== expectedBasename
    ) {
      return undefined;
    }
    const canonical = await realpath(tool);
    const metadata = await lstat(canonical);
    if (
      !metadata.isFile()
      || metadata.uid !== 0
      || (metadata.mode & 0o022) !== 0
      || (metadata.mode & 0o100) === 0
    ) {
      return undefined;
    }
    const { stdout, stderr } = await execFileAsync(tool, args, {
      encoding: "utf8",
      env: {
        DBUS_SESSION_BUS_ADDRESS: `unix:path=/run/user/${uid}/bus`,
        LC_ALL: "C",
        XDG_RUNTIME_DIR: `/run/user/${uid}`,
      },
      maxBuffer: 128 * 1024,
      timeout: 5000,
    });
    if (
      stderr !== ""
      || stdout.length <= 0
      || stdout.length > 128 * 1024
      || stdout.includes("\0")
    ) {
      return undefined;
    }
    return stdout;
  } catch {
    return undefined;
  }
}

function exactOutputField(output, field) {
  const matches = [
    ...output.matchAll(new RegExp(`^${field}=(.*)$`, "gm")),
  ];
  return matches.length === 1 ? matches[0][1] : undefined;
}

async function verifySystemdManagerPeer({
  manager,
  managerPid,
  uid,
  gid,
  busctl,
}) {
  const argv = exactArgv(manager.cmdline);
  const output = await trustedToolOutput({
    tool: busctl,
    expectedBasename: "busctl",
    args: [
      "--user",
      "status",
      "org.freedesktop.systemd1",
      "--no-pager",
    ],
    uid,
  });
  return (
    output !== undefined
    && exactOutputField(output, "PID") === String(managerPid)
    && exactOutputField(output, "PPID") === "1"
    && exactOutputField(output, "UID") === String(uid)
    && exactOutputField(output, "EUID") === String(uid)
    && exactOutputField(output, "SUID") === String(uid)
    && exactOutputField(output, "FSUID") === String(uid)
    && exactOutputField(output, "OwnerUID") === String(uid)
    && exactOutputField(output, "GID") === String(gid)
    && exactOutputField(output, "EGID") === String(gid)
    && exactOutputField(output, "SGID") === String(gid)
    && exactOutputField(output, "FSGID") === String(gid)
    && exactOutputField(output, "Comm") === "systemd"
    && exactOutputField(output, "CommandLine") === argv?.join(" ")
    && exactOutputField(output, "CGroup") === manager.cgroup
    && exactOutputField(output, "Unit") === `user@${uid}.service`
    && exactOutputField(output, "UserUnit") === "init.scope"
  );
}

async function canonicalRootUnit(path) {
  try {
    if (
      resolve(path) !== path
      || basename(path) !== "gpg-agent.service"
    ) {
      return false;
    }
    const metadata = await lstat(path);
    return (
      !metadata.isSymbolicLink()
      && metadata.isFile()
      && metadata.uid === 0
      && (metadata.mode & 0o022) === 0
      && metadata.size > 0
      && metadata.size <= 1024 * 1024
      && metadata.nlink > 0
      && await realpath(path) === path
    );
  } catch {
    return false;
  }
}

function escapeRegularExpression(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

async function verifyGpgAgentUnit({
  pid,
  uid,
  observedCgroup,
  executable,
  systemctl,
}) {
  const properties = [
    "Id",
    "LoadState",
    "ActiveState",
    "SubState",
    "FragmentPath",
    "DropInPaths",
    "Transient",
    "MainPID",
    "ExecMainPID",
    "ControlPID",
    "ControlGroup",
    "ExecStart",
  ];
  const output = await trustedToolOutput({
    tool: systemctl,
    expectedBasename: "systemctl",
    args: [
      "--user",
      "show",
      "gpg-agent.service",
      "--no-pager",
      ...properties.flatMap((property) => ["--property", property]),
    ],
    uid,
  });
  if (
    output === undefined
    || exactOutputField(output, "Id") !== "gpg-agent.service"
    || exactOutputField(output, "LoadState") !== "loaded"
    || exactOutputField(output, "ActiveState") !== "active"
    || exactOutputField(output, "SubState") !== "running"
    || exactOutputField(output, "DropInPaths") !== ""
    || exactOutputField(output, "Transient") !== "no"
    || exactOutputField(output, "MainPID") !== String(pid)
    || exactOutputField(output, "ExecMainPID") !== String(pid)
    || exactOutputField(output, "ControlPID") !== "0"
    || exactOutputField(output, "ControlGroup") !== observedCgroup
  ) {
    return false;
  }
  const fragment = exactOutputField(output, "FragmentPath");
  const execStart = exactOutputField(output, "ExecStart");
  const escaped = escapeRegularExpression(executable);
  return (
    await canonicalRootUnit(fragment)
    && new RegExp(
      `^\\{ path=${escaped} ; argv\\[\\]=${escaped} --supervised ; `
        + "ignore_errors=no ; start_time=[^;]* ; stop_time=[^;]* ; "
        + "pid=(?:0|[1-9][0-9]*) ; code=\\(null\\) ; status=0/0 \\}$",
    ).test(execStart ?? "")
  );
}

async function trustedUserSessionInfrastructure({
  pid,
  uid,
  gid,
  ownCgroup,
  observedCgroup,
  procRoot,
  busctl,
  verifyRootExecutable,
  verifySessionManager,
}) {
  const initCgroup =
    `/user.slice/user-${uid}.slice/user@${uid}.service/init.scope`;
  if (
    inCgroupSubtree(observedCgroup, ownCgroup)
    || observedCgroup !== initCgroup
  ) {
    return false;
  }
  const candidate = await processIdentitySnapshot(procRoot, pid);
  if (!candidate) return false;
  const isManager = candidate.comm === "systemd";
  const isPam = candidate.comm === "(sd-pam)";
  if (!isManager && !isPam) return false;
  const managerPid = isManager ? pid : candidate.status.parentPid;
  const manager = isManager
    ? candidate
    : await processIdentitySnapshot(procRoot, managerPid);
  const init = await processIdentitySnapshot(procRoot, 1);
  if (!manager || !init) return false;
  const executable = systemdManagerExecutable({
    manager,
    managerPid,
    init,
    uid,
    gid,
  });
  if (
    !executable
    || (
      isPam
      && (
        candidate.status.name !== "(sd-pam)"
        || candidate.stat.name !== "(sd-pam)"
        || !exactCredentials(candidate, uid, gid)
        || candidate.status.threads !== 1
        || candidate.status.parentPid !== managerPid
        || candidate.stat.parentPid !== managerPid
        || candidate.stat.processGroupId !== managerPid
        || candidate.stat.sessionId !== managerPid
        || candidate.cgroup !== initCgroup
        || exactArgv(candidate.cmdline)?.length !== 1
        || exactArgv(candidate.cmdline)?.[0] !== "(sd-pam)"
      )
    )
  ) {
    return false;
  }
  const executablePids = isManager ? [managerPid, 1] : [pid, managerPid, 1];
  const executableProofs = await Promise.all(
    executablePids.map((processPid) =>
      verifyRootExecutable({
        path: executable,
        pid: processPid,
        procRoot,
        expectedBasename: "systemd",
      })
    ),
  );
  return (
    executableProofs.every((value) => value === true)
    && await verifySessionManager({
      manager,
      managerPid,
      uid,
      gid,
      busctl,
    })
    && await stableProcessSnapshots(procRoot, [
      { pid, snapshot: candidate },
      { pid: managerPid, snapshot: manager },
      { pid: 1, snapshot: init },
    ])
  );
}

async function trustedGpgAgent({
  pid,
  uid,
  gid,
  ownCgroup,
  observedCgroup,
  procRoot,
  busctl,
  systemctl,
  verifyRootExecutable,
  verifySessionManager,
  verifyGpgUnit,
}) {
  const expectedCgroup =
    `/user.slice/user-${uid}.slice/user@${uid}.service/`
      + "app.slice/gpg-agent.service";
  if (
    inCgroupSubtree(observedCgroup, ownCgroup)
    || observedCgroup !== expectedCgroup
  ) {
    return false;
  }
  const candidate = await processIdentitySnapshot(procRoot, pid);
  if (!candidate) return false;
  const managerPid = candidate.status.parentPid;
  const manager = await processIdentitySnapshot(procRoot, managerPid);
  const init = await processIdentitySnapshot(procRoot, 1);
  if (!manager || !init) return false;
  const systemdExecutable = systemdManagerExecutable({
    manager,
    managerPid,
    init,
    uid,
    gid,
  });
  const argv = exactArgv(candidate.cmdline);
  const executable = argv?.[0];
  if (
    !systemdExecutable
    || candidate.status.name !== "gpg-agent"
    || candidate.stat.name !== "gpg-agent"
    || candidate.comm !== "gpg-agent"
    || !exactCredentials(candidate, uid, gid)
    || candidate.status.threads > 64
    || candidate.status.parentPid !== managerPid
    || candidate.stat.parentPid !== managerPid
    || candidate.stat.processGroupId !== pid
    || candidate.stat.sessionId !== pid
    || candidate.cgroup !== expectedCgroup
    || argv?.length !== 2
    || argv[1] !== "--supervised"
    || basename(executable ?? "") !== "gpg-agent"
  ) {
    return false;
  }
  const executableProofs = await Promise.all([
    verifyRootExecutable({
      path: executable,
      pid,
      procRoot,
      expectedBasename: "gpg-agent",
    }),
    verifyRootExecutable({
      path: systemdExecutable,
      pid: managerPid,
      procRoot,
      expectedBasename: "systemd",
    }),
    verifyRootExecutable({
      path: systemdExecutable,
      pid: 1,
      procRoot,
      expectedBasename: "systemd",
    }),
  ]);
  return (
    executableProofs.every((value) => value === true)
    && await verifySessionManager({
      manager,
      managerPid,
      uid,
      gid,
      busctl,
    })
    && await verifyGpgUnit({
      pid,
      uid,
      observedCgroup,
      executable,
      systemctl,
    })
    && await stableProcessSnapshots(procRoot, [
      { pid, snapshot: candidate },
      { pid: managerPid, snapshot: manager },
      { pid: 1, snapshot: init },
    ])
  );
}

async function trustedProtectedNonMutator(input) {
  const {
    uid,
    observedCgroup,
  } = input;
  const initCgroup =
    `/user.slice/user-${uid}.slice/user@${uid}.service/init.scope`;
  const gpgCgroup =
    `/user.slice/user-${uid}.slice/user@${uid}.service/`
      + "app.slice/gpg-agent.service";
  if (observedCgroup === initCgroup) {
    return trustedUserSessionInfrastructure(input);
  }
  if (observedCgroup === gpgCgroup) {
    return trustedGpgAgent(input);
  }
  return trustedSshSessionSupervisor(input);
}

export async function legacyClaimBoundProcesses({
  claimPath,
  uid,
  procRoot = "/proc",
  username = userInfo().username,
  gid = userInfo().gid,
  busctl = join(dirname(process.argv0), "busctl"),
  systemctl = join(dirname(process.argv0), "systemctl"),
  verifyRootExecutable = canonicalRootExecutable,
  verifySessionManager = verifySystemdManagerPeer,
  verifyGpgUnit = verifyGpgAgentUnit,
}) {
  if (
    typeof claimPath !== "string"
    || resolve(claimPath) !== claimPath
    || !Number.isSafeInteger(uid)
    || uid < 0
    || resolve(procRoot) !== procRoot
    || typeof username !== "string"
    || !Number.isSafeInteger(gid)
    || gid < 0
    || resolve(busctl) !== busctl
    || basename(busctl) !== "busctl"
    || resolve(systemctl) !== systemctl
    || basename(systemctl) !== "systemctl"
    || typeof verifyRootExecutable !== "function"
    || typeof verifySessionManager !== "function"
    || typeof verifyGpgUnit !== "function"
  ) {
    throw new TypeError("legacy preflight process inputs are invalid");
  }
  const selfCgroupFile = join(procRoot, "self", "cgroup");
  const ownCgroup = cgroupPath(
    await readFile(selfCgroupFile),
    "current process cgroup",
  );
  const entries = await readdir(procRoot, { withFileTypes: true });
  if (entries.length > maximumProcEntries) {
    throw new Error("legacy preflight process inventory exceeds bound");
  }
  const binding = `PALACE_MVP_CLAIM_PATH=${claimPath}`;
  const matches = [];
  for (const entry of entries) {
    if (!entry.isDirectory() || !/^[1-9][0-9]*$/.test(entry.name)) continue;
    const pid = Number(entry.name);
    try {
      const status = await readFile(join(procRoot, entry.name, "status"));
      if (effectiveUid(status, pid) !== uid) continue;
      const processCgroupFile = join(procRoot, entry.name, "cgroup");
      const processCgroup = cgroupPath(
        await readFile(processCgroupFile),
        `process ${pid} cgroup`,
      );
      let environment;
      try {
        environment = await readFile(
          join(procRoot, entry.name, "environ"),
        );
      } catch (error) {
        if (["EACCES", "EPERM"].includes(error?.code)) {
          const stableCgroup = cgroupPath(
            await readFile(processCgroupFile),
            `process ${pid} cgroup`,
          );
          if (stableCgroup !== processCgroup) {
            throw new Error(
              `process ${pid} changed cgroup during legacy preflight`,
            );
          }
          const trustedProcess = await trustedProtectedNonMutator({
            pid,
            uid,
            ownCgroup,
            observedCgroup: processCgroup,
            procRoot,
            username,
            gid,
            busctl,
            systemctl,
            verifyRootExecutable,
            verifySessionManager,
            verifyGpgUnit,
          });
          if (!trustedProcess) {
            throw new Error(
              `same-UID process ${pid} environment is inaccessible and is `
                + "not authenticated non-mutating session infrastructure "
                + "during "
                + "legacy preflight",
            );
          }
          continue;
        }
        throw error;
      }
      if (environment.length > maximumEnvironmentBytes) {
        throw new Error(
          `process ${pid} environment exceeds legacy preflight bounds`,
        );
      }
      if (environment.toString("utf8").split("\0").includes(binding)) {
        matches.push(pid);
      }
    } catch (error) {
      if (error?.code !== "ENOENT") throw error;
    }
  }
  if (
    cgroupPath(
      await readFile(selfCgroupFile),
      "current process cgroup",
    ) !== ownCgroup
  ) {
    throw new Error("current process changed cgroup during legacy preflight");
  }
  return matches;
}

export async function retireClaimScope({
  claim,
  claimPath,
  uid,
  systemctl,
  retireScope = retireScopeSlice,
  scanLegacy = legacyClaimBoundProcesses,
}) {
  if (claim === undefined) return "no-claim";
  if (claim?.version === 1) {
    const legacy = validateStoredLegacyClaim(claim);
    if (legacy.uid !== uid) {
      throw new Error("legacy preflight claim belongs to another UID");
    }
    const matches = await scanLegacy({ claimPath, uid });
    if (!Array.isArray(matches) || matches.length !== 0) {
      throw new Error("legacy preflight claim retains bound processes");
    }
    return "legacy-clean";
  }
  const current = validateStoredV2Claim(claim);
  if (current.uid !== uid) {
    throw new Error("active-scope preflight claim belongs to another UID");
  }
  const retired = await retireScope({
    slice: current.processScopeSlice,
    systemctl,
  });
  if (
    retired === null
    || typeof retired !== "object"
    || typeof retired.residueKilled !== "boolean"
  ) {
    throw new Error("active-scope retirement result is invalid");
  }
  return retired.residueKilled ? "residue-killed" : "retired-clean";
}

async function main(args) {
  const [
    command,
    snapshotRunner,
    runsRoot,
    runDirectory,
    systemctl,
  ] = args;
  const uid = process.getuid?.();
  const toolsDirectory = dirname(process.argv0);
  const expectedSystemctl = join(toolsDirectory, "systemctl");
  const flock = join(toolsDirectory, "flock");
  if (
    command !== "retire-before-release"
    || args.length !== 5
    || !Number.isSafeInteger(uid)
    || uid < 0
    || !exactAbsoluteExecutable(systemctl, expectedSystemctl)
    || resolve(snapshotRunner ?? "") !== snapshotRunner
    || resolve(runsRoot ?? "") !== runsRoot
    || resolve(runDirectory ?? "") !== runDirectory
  ) {
    throw new Error(
      "usage: basecamp_active_scope_preflight.mjs "
        + "retire-before-release <snapshot-runner> <runs-root> "
        + "<run-directory> <systemctl>",
    );
  }
  await Promise.all([
    access(systemctl, constants.X_OK),
    access(flock, constants.X_OK),
  ]);

  const varTmp = "/var/tmp";
  if (
    await realpath(varTmp) !== varTmp
    || !(await lstat(varTmp)).isDirectory()
  ) {
    throw new Error("/var/tmp is not a canonical directory");
  }
  const claimDirectory = `${varTmp}/logos-palace-${uid}`;
  await canonicalOwnerDirectory(claimDirectory, uid, 0o700);
  const claimPath = join(
    claimDirectory,
    `active-${releaseProgramId}-${releaseRootId}.json`,
  );
  const lockPath = join(
    claimDirectory,
    `release-${releaseProgramId}-${releaseRootId}.lock`,
  );
  const supervisorPid = Number(
    process.env.PALACE_MVP_LOCK_SUPERVISOR_PID,
  );
  const supervisorStartTimeTicks = Number(
    process.env.PALACE_MVP_LOCK_SUPERVISOR_START_TIME_TICKS,
  );
  if (
    process.env.PALACE_MVP_LOCK_FD !== undefined
    || process.env.PALACE_MVP_LOCK_SUPERVISED !== "1"
    || process.env.PALACE_MVP_LOCK_PATH !== lockPath
    || !Number.isSafeInteger(supervisorPid)
    || supervisorPid <= 1
    || !Number.isSafeInteger(supervisorStartTimeTicks)
    || supervisorStartTimeTicks <= 0
  ) {
    throw new Error("active-scope preflight lacks exact release lock state");
  }
  await verifyReleaseLock({
    lockPath,
    flock,
    supervisorPid,
    supervisorStartTimeTicks,
    snapshotRunner,
    runsRoot,
    runDirectory,
  });
  const claim = await readSecureClaim(claimPath, uid);
  return retireClaimScope({
    claim,
    claimPath,
    uid,
    systemctl,
  });
}

const invoked = process.argv[1]
  && pathToFileURL(resolve(process.argv[1])).href === import.meta.url;
if (invoked) {
  process.stdout.write(`${await main(process.argv.slice(2))}\n`);
}
