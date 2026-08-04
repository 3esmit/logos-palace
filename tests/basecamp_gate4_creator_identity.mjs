import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import {
  ownedProcessIdentityExists,
} from "./basecamp_owned_processes.mjs";

function exactKeys(value, expected) {
  return (
    value !== null
    && typeof value === "object"
    && !Array.isArray(value)
    && JSON.stringify(Object.keys(value).sort())
      === JSON.stringify([...expected].sort())
  );
}

function validCgroupPath(value) {
  return (
    typeof value === "string"
    && value.length > 1
    && value.length <= 4096
    && value.startsWith("/")
    && resolve(value) === value
    && !value.includes("\0")
    && !value.includes("//")
  );
}

export function validateCreatorProcessIdentity(identity, expectedPid) {
  if (
    !exactKeys(
      identity,
      ["pid", "startTimeTicks", "observedCgroupPath"],
    )
    || !Number.isSafeInteger(identity.pid)
    || identity.pid <= 0
    || (
      expectedPid !== undefined
      && identity.pid !== expectedPid
    )
    || !Number.isSafeInteger(identity.startTimeTicks)
    || identity.startTimeTicks <= 0
    || !validCgroupPath(identity.observedCgroupPath)
  ) {
    throw new Error("creator process identity is invalid");
  }
  return identity;
}

export function processStartTimeTicks(statBytes, pid) {
  const bytes = Buffer.isBuffer(statBytes)
    ? statBytes
    : Buffer.from(statBytes);
  if (
    !Number.isSafeInteger(pid)
    || pid <= 0
    || bytes.length <= 0
    || bytes.length > 4096
  ) {
    throw new Error("creator process stat is invalid");
  }
  const encoded = bytes.toString("utf8").trim();
  const close = encoded.lastIndexOf(")");
  if (!encoded.startsWith(`${pid} (`) || close < 3) {
    throw new Error("creator process stat is invalid");
  }
  const fields = encoded.slice(close + 2).split(" ");
  const startTimeTicks = Number(fields[19]);
  if (
    !Number.isSafeInteger(startTimeTicks)
    || startTimeTicks <= 0
  ) {
    throw new Error("creator process start time is invalid");
  }
  return startTimeTicks;
}

async function currentStartTime(identity, read) {
  try {
    return processStartTimeTicks(
      await read(`/proc/${identity.pid}/stat`),
      identity.pid,
    );
  } catch (error) {
    if (error?.code === "ENOENT") return undefined;
    throw error;
  }
}

export async function originalCreatorProcessExists(
  identityArgument,
  {
    read = readFile,
    identityExists = ownedProcessIdentityExists,
  } = {},
) {
  const identity = validateCreatorProcessIdentity(identityArgument);
  const before = await currentStartTime(identity, read);
  if (
    before === undefined
    || before !== identity.startTimeTicks
  ) {
    return false;
  }
  try {
    return await identityExists({
      ...identity,
      cgroupPath: identity.observedCgroupPath,
    });
  } catch (error) {
    const after = await currentStartTime(identity, read);
    if (
      after === undefined
      || after !== identity.startTimeTicks
    ) {
      return false;
    }
    throw error;
  }
}
