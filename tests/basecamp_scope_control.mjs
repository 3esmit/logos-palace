#!/usr/bin/env node

import {
  chmod,
  link,
  lstat,
  open,
  readFile,
  realpath,
  rename,
  unlink,
} from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import {
  attestScope,
  cleanupAttestedScope,
  cleanupDisposition,
  cleanupPlannedScope,
  parseUnifiedCgroup,
  releaseAttestedScopeBarrier,
  validateCleanedScopeEvidence,
  validateControlGroups,
  validateScopeIdentity,
} from "./basecamp_scope.mjs";

const [command, ...args] = process.argv.slice(2);
const uid = process.getuid?.();
if (!Number.isSafeInteger(uid) || uid < 0) {
  throw new Error("process scope control requires a numeric Unix UID");
}

function exactKeys(value, expected) {
  return (
    value !== null
    && typeof value === "object"
    && !Array.isArray(value)
    && JSON.stringify(Object.keys(value).sort())
      === JSON.stringify([...expected].sort())
  );
}

async function syncDirectory(path) {
  const handle = await open(path, "r");
  try {
    await handle.sync();
  } finally {
    await handle.close();
  }
}

async function writeEvidence(path, value) {
  const target = resolve(path);
  const parent = dirname(target);
  if (await realpath(parent) !== parent) {
    throw new Error("process scope evidence parent is not canonical");
  }
  const temporary = `${target}.${process.pid}.tmp`;
  let handle;
  try {
    handle = await open(temporary, "wx", 0o600);
    await handle.writeFile(`${JSON.stringify(value, null, 2)}\n`, "utf8");
    await handle.sync();
    await handle.close();
    handle = undefined;
    await rename(temporary, target);
    await chmod(target, 0o600);
    await syncDirectory(parent);
  } catch (error) {
    await handle?.close().catch(() => {});
    await unlink(temporary).catch((unlinkError) => {
      if (unlinkError?.code !== "ENOENT") throw unlinkError;
    });
    throw error;
  }
}

async function readEvidence(path) {
  const target = resolve(path);
  const metadata = await lstat(target);
  if (
    metadata.isSymbolicLink()
    || !metadata.isFile()
    || metadata.uid !== uid
    || (metadata.mode & 0o777) !== 0o600
    || metadata.size <= 0
    || metadata.size > 64 * 1024
    || await realpath(target) !== target
  ) {
    throw new Error("process scope evidence is not a secure regular file");
  }
  return JSON.parse(await readFile(target, "utf8"));
}

function validateLaunchEvidence(value) {
  if (
    !exactKeys(value, ["schema", "version", "status", "unit", "slice"])
    || value.schema !== "logos.palace.basecamp-process-scope-launch"
    || value.version !== 1
    || value.status !== "planned"
  ) {
    throw new Error("process scope launch evidence is invalid");
  }
  validateScopeIdentity(value);
  return value;
}

async function archiveEvidence(evidencePath, historyArgument) {
  const source = resolve(evidencePath);
  const sourceParent = dirname(source);
  const history = resolve(historyArgument);
  if (history !== join(sourceParent, "process-scope-history")) {
    throw new Error("process scope history path is not exact");
  }
  const historyMetadata = await lstat(history);
  if (
    historyMetadata.isSymbolicLink()
    || !historyMetadata.isDirectory()
    || historyMetadata.uid !== uid
    || (historyMetadata.mode & 0o777) !== 0o700
    || await realpath(history) !== history
  ) {
    throw new Error("process scope history is not an owner-only directory");
  }
  const value = await readEvidence(source);
  let suffix;
  if (value.schema === "logos.palace.basecamp-process-scope-launch") {
    validateLaunchEvidence(value);
    suffix = ".launch.json";
  } else if (
    value.schema === "logos.palace.basecamp-process-scope"
    && value.version === 1
    && ["attested", "cleaned"].includes(value.status)
  ) {
    validateScopeIdentity(value);
    suffix = ".json";
  } else {
    throw new Error("process scope evidence cannot be archived");
  }
  const target = join(history, `${value.unit}${suffix}`);
  const sourceMetadata = await lstat(source);
  try {
    await link(source, target);
  } catch (error) {
    if (error?.code !== "EEXIST") throw error;
    const targetMetadata = await lstat(target);
    if (
      targetMetadata.isSymbolicLink()
      || !targetMetadata.isFile()
      || targetMetadata.uid !== uid
      || (targetMetadata.mode & 0o777) !== 0o600
      || targetMetadata.dev !== sourceMetadata.dev
      || targetMetadata.ino !== sourceMetadata.ino
    ) {
      throw new Error("process scope history target already differs");
    }
  }
  await syncDirectory(history);
  await unlink(source);
  await syncDirectory(sourceParent);
  return target;
}

if (command === "preflight") {
  if (args.length !== 0) {
    throw new Error("usage: basecamp_scope_control.mjs preflight");
  }
  parseUnifiedCgroup(await readFile("/proc/self/cgroup"));
  const filesystem = await import("node:fs/promises").then(
    ({ statfs }) => statfs("/sys/fs/cgroup"),
  );
  if (Number(filesystem.type) !== 0x63677270) {
    throw new Error("compiled MVP requires cgroup v2");
  }
  process.stdout.write("cgroup-v2\n");
} else if (command === "plan") {
  const [unit, slice, evidencePath] = args;
  if (args.length !== 3) {
    throw new Error(
      "usage: basecamp_scope_control.mjs plan <unit> <slice> <evidence>",
    );
  }
  validateScopeIdentity({ unit, slice });
  await writeEvidence(evidencePath, {
    schema: "logos.palace.basecamp-process-scope-launch",
    version: 1,
    status: "planned",
    unit,
    slice,
  });
  process.stdout.write(`${unit}\n`);
} else if (command === "recover") {
  const [evidencePath, systemctl] = args;
  if (
    args.length !== 2
    || resolve(systemctl ?? "") !== systemctl
  ) {
    throw new Error(
      "usage: basecamp_scope_control.mjs recover <evidence> <systemctl>",
    );
  }
  const launch = validateLaunchEvidence(await readEvidence(evidencePath));
  const cleaned = await cleanupPlannedScope({
    unit: launch.unit,
    slice: launch.slice,
    systemctl,
  });
  process.stdout.write(
    cleaned.residueKilled ? "residue-killed\n" : "clean\n",
  );
} else if (command === "finish-launch") {
  const [launchPath, evidencePath] = args;
  if (args.length !== 2) {
    throw new Error(
      "usage: basecamp_scope_control.mjs finish-launch "
        + "<launch-evidence> <scope-evidence>",
    );
  }
  const launch = validateLaunchEvidence(await readEvidence(launchPath));
  const evidence = await readEvidence(evidencePath);
  if (
    evidence.schema !== "logos.palace.basecamp-process-scope"
    || evidence.version !== 1
    || evidence.status !== "attested"
    || evidence.unit !== launch.unit
    || evidence.slice !== launch.slice
  ) {
    throw new Error("attested scope does not finish exact launch plan");
  }
  validateScopeIdentity(evidence);
  await unlink(resolve(launchPath));
  await syncDirectory(dirname(resolve(launchPath)));
  process.stdout.write(`${launch.unit}\n`);
} else if (command === "release") {
  const [evidencePath, systemctl] = args;
  if (
    args.length !== 2
    || resolve(systemctl ?? "") !== systemctl
  ) {
    throw new Error(
      "usage: basecamp_scope_control.mjs release <evidence> <systemctl>",
    );
  }
  const evidence = await readEvidence(evidencePath);
  process.stdout.write(
    `${await releaseAttestedScopeBarrier(evidence, { systemctl })}\n`,
  );
} else if (command === "archive") {
  const [evidencePath, history] = args;
  if (args.length !== 2) {
    throw new Error(
      "usage: basecamp_scope_control.mjs archive <evidence> <history>",
    );
  }
  process.stdout.write(`${await archiveEvidence(evidencePath, history)}\n`);
} else if (command === "current") {
  const [unit, slice] = args;
  if (args.length !== 2) {
    throw new Error(
      "usage: basecamp_scope_control.mjs current <unit> <slice>",
    );
  }
  const controlGroup = parseUnifiedCgroup(
    await readFile("/proc/self/cgroup"),
  );
  validateControlGroups({
    pidControlGroup: controlGroup,
    unitControlGroup: controlGroup,
    sliceControlGroup: dirname(controlGroup),
    unit,
    slice,
  });
  process.stdout.write(`${controlGroup}\n`);
} else if (command === "attest") {
  const [pidArgument, unit, slice, evidencePath, systemctl] = args;
  const pid = Number(pidArgument);
  if (
    args.length !== 5
    || !Number.isSafeInteger(pid)
    || pid <= 0
    || resolve(systemctl ?? "") !== systemctl
  ) {
    throw new Error(
      "usage: basecamp_scope_control.mjs attest <pid> <unit> <slice> <evidence> <systemctl>",
    );
  }
  validateScopeIdentity({ unit, slice });
  const evidence = await attestScope({ pid, unit, slice, systemctl });
  await writeEvidence(evidencePath, evidence);
  process.stdout.write(`${evidence.controlGroup}\n`);
} else if (command === "cleanup") {
  const [evidencePath, exitStatusArgument] = args;
  const commandExitStatus = exitStatusArgument === undefined
    ? undefined
    : Number(exitStatusArgument);
  if (
    ![1, 2].includes(args.length)
    || (
      exitStatusArgument !== undefined
      && (
        !/^(?:0|[1-9][0-9]{0,2})$/.test(exitStatusArgument)
        || !Number.isSafeInteger(commandExitStatus)
        || commandExitStatus > 255
      )
    )
  ) {
    throw new Error(
      "usage: basecamp_scope_control.mjs cleanup "
        + "<evidence> [command-exit-status]",
    );
  }
  const evidence = await readEvidence(evidencePath);
  const absoluteInvocation =
    typeof process.argv0 === "string"
    && resolve(process.argv0) === process.argv0
    && await realpath(process.argv0) === process.execPath
      ? process.argv0
      : process.execPath;
  const systemctl = resolve(dirname(absoluteInvocation), "systemctl");
  const cleaned = await cleanupAttestedScope(evidence, {
    systemctl,
    commandExitStatus,
  });
  await writeEvidence(evidencePath, cleaned);
  process.stdout.write(`${cleanupDisposition(cleaned)}\n`);
} else if (command === "validate-cleaned") {
  const [evidencePath, gate, slice] = args;
  if (args.length !== 3) {
    throw new Error(
      "usage: basecamp_scope_control.mjs validate-cleaned "
        + "<evidence> <gate> <slice>",
    );
  }
  const evidence = await readEvidence(evidencePath);
  validateCleanedScopeEvidence(evidence, { gate, slice });
  process.stdout.write(`${evidence.unit}\n`);
} else {
  throw new Error(
    "usage: basecamp_scope_control.mjs "
      + "<preflight|plan|recover|finish-launch|archive|current|attest"
      + "|release|cleanup|validate-cleaned> ...",
  );
}
