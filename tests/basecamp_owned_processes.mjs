import { readdir, readFile } from "node:fs/promises";
import { basename, dirname, join, resolve } from "node:path";

function bounded(bytes, maximum, description) {
  if (bytes.length <= 0 || bytes.length > maximum) {
    throw new Error(`${description} exceeds cleanup scan bounds`);
  }
  return bytes;
}

function procIdentity(stat, pid, { allowUnownedTopology = false } = {}) {
  const encoded = stat.toString("utf8").trim();
  const close = encoded.lastIndexOf(")");
  if (!encoded.startsWith(`${pid} (`) || close < 3) {
    throw new Error(`process ${pid} has invalid stat during cleanup`);
  }
  const fields = encoded.slice(close + 2).split(" ");
  const parentPid = Number(fields[1]);
  const processGroupId = Number(fields[2]);
  const sessionId = Number(fields[3]);
  const startTimeTicks = Number(fields[19]);
  const hasPositiveGroupAndSession =
    Number.isSafeInteger(processGroupId)
    && processGroupId > 0
    && Number.isSafeInteger(sessionId)
    && sessionId > 0;
  if (!hasPositiveGroupAndSession) {
    if (allowUnownedTopology) return undefined;
    throw new Error(`process ${pid} has invalid topology during cleanup`);
  }
  if (
    !Number.isSafeInteger(parentPid)
    || parentPid < 0
    || !Number.isSafeInteger(startTimeTicks)
    || startTimeTicks <= 0
  ) {
    throw new Error(`process ${pid} has invalid topology during cleanup`);
  }
  return { parentPid, processGroupId, sessionId, startTimeTicks };
}

async function procEntries(procRoot) {
  const entries = await readdir(procRoot, { withFileTypes: true });
  if (entries.length > 65_536) {
    throw new Error("cleanup process inventory exceeds bound");
  }
  return entries.filter(
    (entry) =>
      entry.isDirectory() && /^[1-9][0-9]*$/.test(entry.name),
  );
}

function validObservedCgroupPath(path) {
  return typeof path === "string"
    && path.length > 0
    && path.length <= 4096
    && path.startsWith("/")
    && resolve(path) === path
    && !path.includes("\0")
    && !path.includes("//");
}

function validTargetCgroupPath(path) {
  return validObservedCgroupPath(path) && path !== "/";
}

function inCgroupSubtree(path, root) {
  return path === root || path.startsWith(`${root}/`);
}

async function procCgroup(procRoot, pid) {
  const encoded = bounded(
    await readFile(`${procRoot}/${pid}/cgroup`),
    64 * 1024,
    `process ${pid} cgroup`,
  ).toString("utf8");
  const match = encoded.match(/^0::(\/[^\0\n]*)\n$/);
  if (!match || !validObservedCgroupPath(match[1])) {
    throw new Error(`process ${pid} has invalid cgroup during cleanup`);
  }
  return match[1];
}

async function stableIdentity(
  procRoot,
  pid,
  expected,
  { allowUnownedTopology = false } = {},
) {
  const identity = procIdentity(
    bounded(
      await readFile(`${procRoot}/${pid}/stat`),
      4096,
      `process ${pid} stat`,
    ),
    pid,
    { allowUnownedTopology },
  );
  if (!identity) return undefined;
  if (
    expected
    && (
      identity.parentPid !== expected.parentPid
      || identity.processGroupId !== expected.processGroupId
      || identity.sessionId !== expected.sessionId
      || identity.startTimeTicks !== expected.startTimeTicks
    )
  ) {
    throw new Error(`process ${pid} changed identity during cleanup`);
  }
  return identity;
}

async function scanProcessIdentity(procRoot, pid) {
  return stableIdentity(procRoot, pid, undefined, {
    allowUnownedTopology: true,
  });
}

function requireCgroupPath(path, description) {
  if (!validTargetCgroupPath(path)) {
    throw new TypeError(`${description} cgroup is invalid`);
  }
  return path;
}

export async function captureOwnedProcessIdentity({
  pid,
  startTimeTicks,
  cgroupPath = process.env.PALACE_MVP_PROCESS_CGROUP,
  procRoot = "/proc",
}) {
  if (
    !Number.isSafeInteger(pid)
    || pid <= 0
    || (
      startTimeTicks !== undefined
      && (
        !Number.isSafeInteger(startTimeTicks)
        || startTimeTicks <= 0
      )
    )
    || !validTargetCgroupPath(cgroupPath)
  ) {
    throw new TypeError("cleanup process identity inputs are invalid");
  }
  try {
    const identity = await stableIdentity(procRoot, pid);
    if (
      startTimeTicks !== undefined
      && identity.startTimeTicks !== startTimeTicks
    ) {
      throw new Error(`process ${pid} was reused during cleanup`);
    }
    const observedCgroupPath = await procCgroup(procRoot, pid);
    if (!inCgroupSubtree(observedCgroupPath, cgroupPath)) {
      throw new Error(`process ${pid} left cleanup cgroup`);
    }
    await stableIdentity(procRoot, pid, identity);
    if (await procCgroup(procRoot, pid) !== observedCgroupPath) {
      throw new Error(`process ${pid} changed cgroup during cleanup`);
    }
    return {
      pid,
      startTimeTicks: identity.startTimeTicks,
      observedCgroupPath,
    };
  } catch (error) {
    if (error?.code === "ENOENT") return undefined;
    throw error;
  }
}

export async function ownedProcessIdentityExists({
  pid,
  startTimeTicks,
  observedCgroupPath,
  cgroupPath = process.env.PALACE_MVP_PROCESS_CGROUP,
  procRoot = "/proc",
}) {
  if (
    !Number.isSafeInteger(startTimeTicks)
    || startTimeTicks <= 0
    || !validObservedCgroupPath(observedCgroupPath)
  ) {
    throw new TypeError("cleanup process identity token is invalid");
  }
  const current = await captureOwnedProcessIdentity({
    pid,
    startTimeTicks,
    cgroupPath,
    procRoot,
  });
  if (!current) return false;
  if (current.observedCgroupPath !== observedCgroupPath) {
    throw new Error(`process ${pid} changed cgroup during cleanup`);
  }
  return true;
}

export function ownedBasecampUserDir(argv, basecamp, userDirs) {
  if (!Array.isArray(argv) || argv.length === 0) {
    return undefined;
  }
  const expectedWrapper = resolve(basecamp);
  const expectedRuntime = join(
    dirname(expectedWrapper),
    ".LogosBasecamp.elf",
  );
  const wrapperInvocation = resolve(argv[0]) === expectedWrapper;
  const directRuntimeInvocation = resolve(argv[0]) === expectedRuntime;
  const loaderInvocation =
    /^ld(?:-[a-z0-9_-]+)?-linux[^/]*\.so(?:\.[0-9]+)*$/i.test(
      basename(argv[0]),
    )
    && argv.length > 1
    && resolve(argv[1]) === expectedRuntime;
  if (!wrapperInvocation && !directRuntimeInvocation && !loaderInvocation) {
    return undefined;
  }
  const indexes = argv.flatMap(
    (value, index) => value === "--user-dir" ? [index] : [],
  );
  if (indexes.length !== 1 || !argv[indexes[0] + 1]) return undefined;
  const userDir = resolve(argv[indexes[0] + 1]);
  return userDirs.has(userDir) ? userDir : undefined;
}

export async function discoverOwnedBasecampProcesses({
  basecamp,
  userDirs,
  cgroupPath = process.env.PALACE_MVP_PROCESS_CGROUP,
  uid = process.getuid?.(),
  procRoot = "/proc",
}) {
  if (
    typeof basecamp !== "string"
    || !(userDirs instanceof Set)
    || userDirs.size === 0
    || [...userDirs].some((path) => resolve(path) !== path)
    || !validTargetCgroupPath(cgroupPath)
    || !Number.isSafeInteger(uid)
    || uid < 0
  ) {
    throw new TypeError("owned Basecamp discovery inputs are invalid");
  }
  const entries = await procEntries(procRoot);
  const matches = [];
  for (const entry of entries) {
    const pid = Number(entry.name);
    try {
      const processCgroup = await procCgroup(procRoot, pid);
      if (!inCgroupSubtree(processCgroup, cgroupPath)) continue;
      const status = bounded(
        await readFile(`${procRoot}/${pid}/status`),
        64 * 1024,
        `process ${pid} status`,
      ).toString("utf8");
      const effectiveUid = Number(
        status.match(
          /^Uid:\s+[0-9]+\s+([0-9]+)\s+[0-9]+\s+[0-9]+$/m,
        )?.[1],
      );
      if (effectiveUid !== uid) continue;
      const cmdlineBytes = await readFile(`${procRoot}/${pid}/cmdline`);
      if (cmdlineBytes.length === 0) continue;
      const cmdline = bounded(
        cmdlineBytes,
        64 * 1024,
        `process ${pid} command line`,
      ).toString("utf8").split("\0").filter(Boolean);
      const userDir = ownedBasecampUserDir(
        cmdline,
        basecamp,
        userDirs,
      );
      if (!userDir) continue;
      const identity = await stableIdentity(procRoot, pid);
      if (await procCgroup(procRoot, pid) !== processCgroup) {
        throw new Error(`process ${pid} changed cgroup during cleanup`);
      }
      await stableIdentity(procRoot, pid, identity);
      matches.push({
        pid,
        processGroupId: identity.processGroupId,
        startTimeTicks: identity.startTimeTicks,
        userDir,
      });
    } catch (error) {
      if (error?.code !== "ENOENT") throw error;
    }
  }
  return matches.sort((left, right) => left.pid - right.pid);
}

export async function ownedProcessGroupMembers({
  processGroupId,
  claimPath,
  cgroupPath = process.env.PALACE_MVP_PROCESS_CGROUP,
  uid = process.getuid?.(),
  procRoot = "/proc",
}) {
  if (
    !Number.isSafeInteger(processGroupId)
    || processGroupId <= 0
    || typeof claimPath !== "string"
    || resolve(claimPath) !== claimPath
    || !validTargetCgroupPath(cgroupPath)
    || !Number.isSafeInteger(uid)
    || uid < 0
  ) {
    throw new TypeError("owned process-group inputs are invalid");
  }
  const binding = `PALACE_MVP_CLAIM_PATH=${claimPath}`;
  const members = [];
  for (const entry of await procEntries(procRoot)) {
    const pid = Number(entry.name);
    try {
      const identity = await scanProcessIdentity(procRoot, pid);
      if (!identity) continue;
      if (identity.processGroupId !== processGroupId) continue;
      const memberCgroup = await procCgroup(procRoot, pid);
      if (!inCgroupSubtree(memberCgroup, cgroupPath)) {
        members.push({
          pid,
          startTimeTicks: identity.startTimeTicks,
          observedCgroupPath: memberCgroup,
          owned: false,
        });
        continue;
      }
      const status = bounded(
        await readFile(`${procRoot}/${pid}/status`),
        64 * 1024,
        `process ${pid} status`,
      ).toString("utf8");
      const effectiveUid = Number(
        status.match(
          /^Uid:\s+[0-9]+\s+([0-9]+)\s+[0-9]+\s+[0-9]+$/m,
        )?.[1],
      );
      const environment = await readFile(`${procRoot}/${pid}/environ`);
      const hasClaim = environment.length > 0
        && environment.length <= 1024 * 1024
        && environment.toString("utf8").split("\0").includes(binding);
      await stableIdentity(procRoot, pid, identity);
      if (await procCgroup(procRoot, pid) !== memberCgroup) {
        throw new Error(`process ${pid} changed cgroup during cleanup`);
      }
      members.push({
        pid,
        startTimeTicks: identity.startTimeTicks,
        observedCgroupPath: memberCgroup,
        owned: effectiveUid === uid && hasClaim,
      });
    } catch (error) {
      if (error?.code !== "ENOENT") throw error;
    }
  }
  return members.sort((left, right) => left.pid - right.pid);
}

export function requireOwnedProcessGroup(members, processGroupId) {
  if (
    !Array.isArray(members)
    || members.length === 0
    || members.some(
      (member) =>
        !Number.isSafeInteger(member?.pid)
        || member.pid <= 0
        || !Number.isSafeInteger(member.startTimeTicks)
        || member.startTimeTicks <= 0
        || !validObservedCgroupPath(member.observedCgroupPath)
        || member.owned !== true,
    )
  ) {
    throw new Error(
      `refusing to signal unverified process group ${processGroupId}`,
    );
  }
  return members;
}

export async function claimBoundProcesses({
  claimPath,
  cgroupPath = process.env.PALACE_MVP_PROCESS_CGROUP,
  uid = process.getuid?.(),
  procRoot = "/proc",
}) {
  if (
    typeof claimPath !== "string"
    || resolve(claimPath) !== claimPath
    || !validTargetCgroupPath(cgroupPath)
    || !Number.isSafeInteger(uid)
    || uid < 0
  ) {
    throw new TypeError("claim-bound process inputs are invalid");
  }
  const binding = `PALACE_MVP_CLAIM_PATH=${claimPath}`;
  requireCgroupPath(cgroupPath, "claim-bound process");
  const processes = [];
  for (const entry of await procEntries(procRoot)) {
    const pid = Number(entry.name);
    try {
      const processCgroup = await procCgroup(procRoot, pid);
      if (!inCgroupSubtree(processCgroup, cgroupPath)) {
        continue;
      }
      const status = bounded(
        await readFile(`${procRoot}/${pid}/status`),
        64 * 1024,
        `process ${pid} status`,
      ).toString("utf8");
      const effectiveUid = Number(
        status.match(
          /^Uid:\s+[0-9]+\s+([0-9]+)\s+[0-9]+\s+[0-9]+$/m,
        )?.[1],
      );
      if (effectiveUid !== uid) {
        continue;
      }
      const environment = await readFile(`${procRoot}/${pid}/environ`);
      if (
        environment.length === 0
        || environment.length > 1024 * 1024
        || !environment.toString("utf8").split("\0").includes(binding)
      ) {
        continue;
      }
      const identity = await stableIdentity(procRoot, pid);
      if (await procCgroup(procRoot, pid) !== processCgroup) {
        throw new Error(`process ${pid} changed cgroup during cleanup`);
      }
      await stableIdentity(procRoot, pid, identity);
      processes.push({
        pid,
        parentPid: identity.parentPid,
        processGroupId: identity.processGroupId,
        sessionId: identity.sessionId,
        startTimeTicks: identity.startTimeTicks,
      });
    } catch (error) {
      if (error?.code !== "ENOENT") throw error;
    }
  }
  return processes.sort((left, right) => left.pid - right.pid);
}

export async function cgroupProcesses({
  cgroupPath,
  procRoot = "/proc",
}) {
  if (!validTargetCgroupPath(cgroupPath)) {
    throw new TypeError("cgroup process inventory input is invalid");
  }
  const processes = [];
  for (const entry of await procEntries(procRoot)) {
    const pid = Number(entry.name);
    try {
      const processCgroup = await procCgroup(procRoot, pid);
      if (!inCgroupSubtree(processCgroup, cgroupPath)) {
        continue;
      }
      const identity = await stableIdentity(procRoot, pid);
      await stableIdentity(procRoot, pid, identity);
      if (await procCgroup(procRoot, pid) !== processCgroup) {
        throw new Error(`process ${pid} changed cgroup during cleanup`);
      }
      processes.push({
        pid,
        parentPid: identity.parentPid,
        processGroupId: identity.processGroupId,
        sessionId: identity.sessionId,
        startTimeTicks: identity.startTimeTicks,
      });
    } catch (error) {
      if (error?.code !== "ENOENT") throw error;
    }
  }
  return processes.sort((left, right) => left.pid - right.pid);
}
