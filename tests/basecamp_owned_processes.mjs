import { readdir, readFile } from "node:fs/promises";
import { resolve } from "node:path";

function bounded(bytes, maximum, description) {
  if (bytes.length <= 0 || bytes.length > maximum) {
    throw new Error(`${description} exceeds cleanup scan bounds`);
  }
  return bytes;
}

function procIdentity(stat, pid) {
  const encoded = stat.toString("utf8").trim();
  const close = encoded.lastIndexOf(")");
  if (!encoded.startsWith(`${pid} (`) || close < 3) {
    throw new Error(`process ${pid} has invalid stat during cleanup`);
  }
  const fields = encoded.slice(close + 2).split(" ");
  const parentPid = Number(fields[1]);
  const processGroupId = Number(fields[2]);
  const sessionId = Number(fields[3]);
  if (
    !Number.isSafeInteger(parentPid)
    || parentPid < 0
    || !Number.isSafeInteger(processGroupId)
    || processGroupId <= 0
    || !Number.isSafeInteger(sessionId)
    || sessionId <= 0
  ) {
    throw new Error(`process ${pid} has invalid topology during cleanup`);
  }
  return { parentPid, processGroupId, sessionId };
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

export function ownedBasecampUserDir(argv, basecamp, userDirs) {
  if (
    !Array.isArray(argv)
    || argv.length === 0
    || resolve(argv[0]) !== resolve(basecamp)
  ) {
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
  uid = process.getuid?.(),
  procRoot = "/proc",
}) {
  if (
    typeof basecamp !== "string"
    || !(userDirs instanceof Set)
    || userDirs.size === 0
    || [...userDirs].some((path) => resolve(path) !== path)
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
      const { processGroupId } = procIdentity(
        bounded(
          await readFile(`${procRoot}/${pid}/stat`),
          4096,
          `process ${pid} stat`,
        ),
        pid,
      );
      matches.push({ pid, processGroupId, userDir });
    } catch (error) {
      if (error?.code !== "ENOENT") throw error;
    }
  }
  return matches.sort((left, right) => left.pid - right.pid);
}

export async function ownedProcessGroupMembers({
  processGroupId,
  claimPath,
  uid = process.getuid?.(),
  procRoot = "/proc",
}) {
  if (
    !Number.isSafeInteger(processGroupId)
    || processGroupId <= 0
    || typeof claimPath !== "string"
    || resolve(claimPath) !== claimPath
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
      const { processGroupId: memberGroupId } = procIdentity(
        bounded(
          await readFile(`${procRoot}/${pid}/stat`),
          4096,
          `process ${pid} stat`,
        ),
        pid,
      );
      if (memberGroupId !== processGroupId) continue;
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
      members.push({
        pid,
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
  uid = process.getuid?.(),
  procRoot = "/proc",
}) {
  if (
    typeof claimPath !== "string"
    || resolve(claimPath) !== claimPath
    || !Number.isSafeInteger(uid)
    || uid < 0
  ) {
    throw new TypeError("claim-bound process inputs are invalid");
  }
  const binding = `PALACE_MVP_CLAIM_PATH=${claimPath}`;
  const processes = [];
  for (const entry of await procEntries(procRoot)) {
    const pid = Number(entry.name);
    try {
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
      processes.push({
        pid,
        ...procIdentity(
          bounded(
            await readFile(`${procRoot}/${pid}/stat`),
            4096,
            `process ${pid} stat`,
          ),
          pid,
        ),
      });
    } catch (error) {
      if (error?.code !== "ENOENT") throw error;
    }
  }
  return processes.sort((left, right) => left.pid - right.pid);
}
