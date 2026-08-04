import { execFile as execFileCallback } from "node:child_process";
import { constants as fsConstants } from "node:fs";
import {
  access,
  lstat,
  readFile,
  realpath,
  statfs,
  writeFile,
} from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { promisify } from "node:util";

const execFile = promisify(execFileCallback);
const cgroup2Magic = 0x63677270;
const maximumCgroupBytes = 64 * 1024;

function bounded(bytes, maximum, description) {
  if (bytes.length <= 0 || bytes.length > maximum) {
    throw new Error(`${description} exceeds scope bounds`);
  }
  return bytes;
}

export function parseUnifiedCgroup(bytes) {
  const encoded = bounded(
    Buffer.isBuffer(bytes) ? bytes : Buffer.from(bytes),
    maximumCgroupBytes,
    "process cgroup",
  ).toString("utf8");
  const match = encoded.match(/^0::(\/[^\0\n]*)\n$/);
  const path = match?.[1];
  if (
    !path
    || path.length > 4096
    || path === "/"
    || resolve(path) !== path
    || path.includes("//")
  ) {
    throw new Error("process is not in one canonical cgroup-v2 path");
  }
  return path;
}

export function validateScopeIdentity({ unit, slice }) {
  const sliceMatch = validateSliceIdentity(slice);
  const prefix = `logos-palace-run-${sliceMatch[1]}`;
  if (
    !new RegExp(
      `^${prefix}-gate[1-4]-[A-Za-z0-9]{8}\\.scope$`,
    ).test(unit ?? "")
  ) {
    throw new Error("process scope unit name is invalid");
  }
  return { prefix, runId: sliceMatch[1], slice, unit };
}

function validateSliceIdentity(slice) {
  const match = slice?.match(
    /^logos-palace-run-([A-Za-z0-9]{8})\.slice$/,
  );
  if (!match) {
    throw new Error("process scope slice name is invalid");
  }
  return match;
}

export function validateControlGroups({
  pidControlGroup,
  unitControlGroup,
  sliceControlGroup,
  unit,
  slice,
}) {
  validateScopeIdentity({ unit, slice });
  for (const [description, path] of [
    ["PID", pidControlGroup],
    ["scope", unitControlGroup],
    ["slice", sliceControlGroup],
  ]) {
    if (
      typeof path !== "string"
      || path.length <= 1
      || path.length > 4096
      || !path.startsWith("/")
      || resolve(path) !== path
      || path.includes("//")
    ) {
      throw new Error(`${description} ControlGroup is invalid`);
    }
  }
  if (
    pidControlGroup !== unitControlGroup
    || dirname(unitControlGroup) !== sliceControlGroup
    || unitControlGroup.split("/").at(-1) !== unit
    || sliceControlGroup.split("/").at(-1) !== slice
  ) {
    throw new Error("PID, scope, and slice ControlGroup identities differ");
  }
  return unitControlGroup;
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

function validateSliceControlGroup({ sliceControlGroup, slice }) {
  if (
    typeof sliceControlGroup !== "string"
    || sliceControlGroup.length <= 1
    || sliceControlGroup.length > 4096
    || !sliceControlGroup.startsWith("/")
    || resolve(sliceControlGroup) !== sliceControlGroup
    || sliceControlGroup.includes("//")
    || sliceControlGroup.split("/").at(-1) !== slice
  ) {
    throw new Error("slice ControlGroup identity differs");
  }
  return sliceControlGroup;
}

export function validateCleanedScopeEvidence(
  evidence,
  { gate, slice },
) {
  if (
    !/^gate[1-4]$/.test(gate ?? "")
    || !exactKeys(evidence, [
      "schema",
      "version",
      "status",
      "unit",
      "slice",
      "controlGroup",
      "attestedPid",
      "attestedStartTimeTicks",
      "barrier",
      "cgroupPath",
      "eventsPath",
      "killPath",
      "sliceControlGroup",
      "sliceCgroupPath",
      "sliceEventsPath",
      "sliceKillPath",
      "commandExitStatus",
      "cleanup",
    ])
    || evidence.schema !== "logos.palace.basecamp-process-scope"
    || evidence.version !== 1
    || evidence.status !== "cleaned"
    || evidence.slice !== slice
    || !evidence.unit.startsWith(
      `${slice.slice(0, -".slice".length)}-${gate}-`,
    )
    || !Number.isSafeInteger(evidence.attestedPid)
    || evidence.attestedPid <= 0
    || !Number.isSafeInteger(evidence.attestedStartTimeTicks)
    || evidence.attestedStartTimeTicks <= 0
    || evidence.barrier !== "sigstop-before-exec"
    || evidence.commandExitStatus !== 0
    || !exactKeys(evidence.cleanup, [
      "status",
      "initiallyPopulated",
      "residueKilled",
      "finalPopulated",
      "sliceInitiallyPopulated",
      "sliceResidueKilled",
      "sliceFinalPopulated",
    ])
    || evidence.cleanup.status !== "passed"
    || evidence.cleanup.initiallyPopulated !== false
    || evidence.cleanup.residueKilled !== false
    || evidence.cleanup.finalPopulated !== false
    || evidence.cleanup.sliceInitiallyPopulated !== false
    || evidence.cleanup.sliceResidueKilled !== false
    || evidence.cleanup.sliceFinalPopulated !== false
  ) {
    throw new Error("cleaned process scope evidence is invalid");
  }
  validateScopeIdentity(evidence);
  validateControlGroups({
    pidControlGroup: evidence.controlGroup,
    unitControlGroup: evidence.controlGroup,
    sliceControlGroup: evidence.sliceControlGroup,
    unit: evidence.unit,
    slice: evidence.slice,
  });
  if (
    evidence.cgroupPath
      !== join("/sys/fs/cgroup", evidence.controlGroup)
    || evidence.eventsPath !== join(evidence.cgroupPath, "cgroup.events")
    || evidence.killPath !== join(evidence.cgroupPath, "cgroup.kill")
    || evidence.sliceCgroupPath
      !== join("/sys/fs/cgroup", evidence.sliceControlGroup)
    || evidence.sliceEventsPath
      !== join(evidence.sliceCgroupPath, "cgroup.events")
    || evidence.sliceKillPath
      !== join(evidence.sliceCgroupPath, "cgroup.kill")
  ) {
    throw new Error("cleaned process scope evidence paths differ");
  }
  return evidence;
}

export function cleanupDisposition(evidence) {
  const cleanup = evidence?.cleanup;
  if (
    evidence?.status !== "cleaned"
    || !exactKeys(cleanup, [
      "status",
      "initiallyPopulated",
      "residueKilled",
      "finalPopulated",
      "sliceInitiallyPopulated",
      "sliceResidueKilled",
      "sliceFinalPopulated",
    ])
    || cleanup.status !== "passed"
    || cleanup.finalPopulated !== false
    || cleanup.sliceFinalPopulated !== false
    || typeof cleanup.initiallyPopulated !== "boolean"
    || typeof cleanup.residueKilled !== "boolean"
    || typeof cleanup.sliceInitiallyPopulated !== "boolean"
    || typeof cleanup.sliceResidueKilled !== "boolean"
  ) {
    throw new Error("process scope cleanup result is invalid");
  }
  return cleanup.residueKilled || cleanup.sliceResidueKilled
    ? "residue-killed"
    : "clean";
}

async function defaultControlGroupFor(systemctl, unit) {
  const { stdout } = await execFile(
    systemctl,
    ["--user", "show", "--property=ControlGroup", "--value", unit],
    {
      encoding: "utf8",
      maxBuffer: maximumCgroupBytes,
      timeout: 10_000,
    },
  );
  if (!stdout.endsWith("\n") || stdout.slice(0, -1).includes("\n")) {
    throw new Error(`systemd returned invalid ControlGroup for ${unit}`);
  }
  const path = stdout.slice(0, -1);
  if (!path) {
    throw new Error(`systemd returned empty ControlGroup for ${unit}`);
  }
  return path;
}

async function optionalControlGroupFor(systemctl, unit) {
  try {
    return await defaultControlGroupFor(systemctl, unit);
  } catch (error) {
    if (
      error instanceof Error
      && error.message
        === `systemd returned empty ControlGroup for ${unit}`
    ) {
      return undefined;
    }
    throw error;
  }
}

async function defaultActiveStateFor(systemctl, unit) {
  const { stdout } = await execFile(
    systemctl,
    [
      "--user",
      "show",
      "--property=ActiveState",
      "--value",
      "--",
      unit,
    ],
    {
      encoding: "utf8",
      maxBuffer: maximumCgroupBytes,
      timeout: 10_000,
    },
  );
  if (!stdout.endsWith("\n") || stdout.slice(0, -1).includes("\n")) {
    throw new Error(`systemd returned invalid ActiveState for ${unit}`);
  }
  return stdout.slice(0, -1);
}

async function defaultLoadedStateFor(systemctl, unit) {
  const { stdout } = await execFile(
    systemctl,
    [
      "--user",
      "list-units",
      "--all",
      "--plain",
      "--no-legend",
      "--no-pager",
      "--",
      unit,
    ],
    {
      encoding: "utf8",
      maxBuffer: maximumCgroupBytes,
      timeout: 10_000,
    },
  );
  if (stdout === "") return false;
  const lines = stdout.trimEnd().split("\n");
  if (
    lines.length !== 1
    || lines[0].trimStart().split(/\s+/, 1)[0] !== unit
  ) {
    throw new Error(`systemd returned invalid loaded state for ${unit}`);
  }
  return true;
}

export function scopeBarrierSignalArguments(unit, slice) {
  validateScopeIdentity({ unit, slice });
  return [
    "--user",
    "kill",
    "--kill-whom=all",
    "--signal=CONT",
    "--",
    unit,
  ];
}

async function defaultSignalScope(systemctl, unit, slice) {
  await execFile(
    systemctl,
    scopeBarrierSignalArguments(unit, slice),
    {
      encoding: "utf8",
      maxBuffer: maximumCgroupBytes,
      timeout: 10_000,
    },
  );
}

async function defaultStopSlice(systemctl, slice) {
  await execFile(
    systemctl,
    ["--user", "stop", "--", slice],
    {
      encoding: "utf8",
      maxBuffer: maximumCgroupBytes,
      timeout: 10_000,
    },
  );
}

async function validateCgroupMount(cgroupRoot) {
  if (
    cgroupRoot !== resolve(cgroupRoot)
    || await realpath(cgroupRoot) !== cgroupRoot
  ) {
    throw new Error("cgroup root is not canonical");
  }
  const filesystem = await statfs(cgroupRoot);
  if (Number(filesystem.type) !== cgroup2Magic) {
    throw new Error("cgroup root is not cgroup v2");
  }
}

async function validateControlFiles(cgroupRoot, controlGroup) {
  const path = join(cgroupRoot, controlGroup);
  const canonical = await realpath(path);
  if (
    canonical !== path
    || !canonical.startsWith(`${cgroupRoot}/`)
    || !(await lstat(path)).isDirectory()
  ) {
    throw new Error("scope cgroup path escaped cgroup root");
  }
  const events = join(path, "cgroup.events");
  const kill = join(path, "cgroup.kill");
  if (!(await lstat(events)).isFile() || !(await lstat(kill)).isFile()) {
    throw new Error("scope cgroup control files are missing");
  }
  await access(events, fsConstants.R_OK);
  await access(kill, fsConstants.W_OK);
  return { cgroupPath: path, eventsPath: events, killPath: kill };
}

function stoppedProcessIdentity(statBytes, statusBytes, pid) {
  const stat = bounded(statBytes, 4096, "process stat")
    .toString("utf8")
    .trim();
  const close = stat.lastIndexOf(")");
  if (!stat.startsWith(`${pid} (`) || close < 3) {
    throw new Error("attested process stat is invalid");
  }
  const fields = stat.slice(close + 2).split(" ");
  const startTimeTicks = Number(fields[19]);
  const state = bounded(statusBytes, maximumCgroupBytes, "process status")
    .toString("utf8")
    .match(/^State:\s+([A-Z])\b/m)?.[1];
  if (
    !Number.isSafeInteger(startTimeTicks)
    || startTimeTicks <= 0
    || state !== "T"
  ) {
    throw new Error("attested process did not stop at the pre-exec barrier");
  }
  return { startTimeTicks };
}

function validateAttestedScopeEvidence(evidence, cgroupRoot) {
  if (
    !exactKeys(evidence, [
      "schema",
      "version",
      "status",
      "unit",
      "slice",
      "controlGroup",
      "attestedPid",
      "attestedStartTimeTicks",
      "barrier",
      "cgroupPath",
      "eventsPath",
      "killPath",
      "sliceControlGroup",
      "sliceCgroupPath",
      "sliceEventsPath",
      "sliceKillPath",
    ])
    || evidence?.schema !== "logos.palace.basecamp-process-scope"
    || evidence?.version !== 1
    || evidence?.status !== "attested"
    || evidence?.barrier !== "sigstop-before-exec"
    || !Number.isSafeInteger(evidence?.attestedPid)
    || evidence.attestedPid <= 0
    || !Number.isSafeInteger(evidence?.attestedStartTimeTicks)
    || evidence.attestedStartTimeTicks <= 0
  ) {
    throw new Error("attested process scope evidence is invalid");
  }
  validateScopeIdentity(evidence);
  validateControlGroups({
    pidControlGroup: evidence.controlGroup,
    unitControlGroup: evidence.controlGroup,
    sliceControlGroup: evidence.sliceControlGroup,
    unit: evidence.unit,
    slice: evidence.slice,
  });
  if (
    evidence.cgroupPath
      !== join(cgroupRoot, evidence.controlGroup)
    || evidence.eventsPath !== join(evidence.cgroupPath, "cgroup.events")
    || evidence.killPath !== join(evidence.cgroupPath, "cgroup.kill")
    || evidence.sliceControlGroup !== dirname(evidence.controlGroup)
    || evidence.sliceCgroupPath
      !== join(cgroupRoot, evidence.sliceControlGroup)
    || evidence.sliceEventsPath
      !== join(evidence.sliceCgroupPath, "cgroup.events")
    || evidence.sliceKillPath
      !== join(evidence.sliceCgroupPath, "cgroup.kill")
  ) {
    throw new Error("attested process scope evidence paths differ");
  }
  return evidence;
}

export async function releaseAttestedScopeBarrier(
  evidence,
  {
    systemctl,
    procRoot = "/proc",
    cgroupRoot = "/sys/fs/cgroup",
    read = readFile,
    canonical = realpath,
    metadata = lstat,
    controlGroupFor = defaultControlGroupFor,
    signalScope = defaultSignalScope,
  } = {},
) {
  validateAttestedScopeEvidence(evidence, cgroupRoot);
  if (typeof systemctl !== "string" || resolve(systemctl) !== systemctl) {
    throw new TypeError("process scope barrier release inputs are invalid");
  }

  const reopenIdentity = async () => {
    const [statBytes, statusBytes, cgroupBytes] = await Promise.all([
      read(`${procRoot}/${evidence.attestedPid}/stat`),
      read(`${procRoot}/${evidence.attestedPid}/status`),
      read(`${procRoot}/${evidence.attestedPid}/cgroup`),
    ]);
    const identity = stoppedProcessIdentity(
      statBytes,
      statusBytes,
      evidence.attestedPid,
    );
    if (
      identity.startTimeTicks !== evidence.attestedStartTimeTicks
      || parseUnifiedCgroup(cgroupBytes) !== evidence.controlGroup
    ) {
      throw new Error("attested scope leader changed before barrier release");
    }
  };

  await reopenIdentity();
  const [unitControlGroup, sliceControlGroup] = await Promise.all([
    controlGroupFor(systemctl, evidence.unit),
    controlGroupFor(systemctl, evidence.slice),
  ]);
  validateControlGroups({
    pidControlGroup: evidence.controlGroup,
    unitControlGroup,
    sliceControlGroup,
    unit: evidence.unit,
    slice: evidence.slice,
  });
  const processesPath = join(evidence.cgroupPath, "cgroup.procs");
  if (
    await canonical(processesPath) !== processesPath
    || !(await metadata(processesPath)).isFile()
  ) {
    throw new Error("scope cgroup.procs is not one canonical file");
  }
  const verifySoleLeader = async () => {
    const bytes = bounded(
      await read(processesPath),
      maximumCgroupBytes,
      "scope cgroup.procs",
    ).toString("utf8");
    if (
      bytes !== `${evidence.attestedPid}\n`
      || !/^[1-9][0-9]*\n$/.test(bytes)
    ) {
      throw new Error(
        "scope cgroup does not contain only attested stopped leader",
      );
    }
  };
  await verifySoleLeader();
  await reopenIdentity();
  await verifySoleLeader();
  await signalScope(systemctl, evidence.unit, evidence.slice);
  return evidence.unit;
}

export async function attestScope({
  pid,
  unit,
  slice,
  systemctl,
  procRoot = "/proc",
  cgroupRoot = "/sys/fs/cgroup",
  controlGroupFor = defaultControlGroupFor,
}) {
  if (
    !Number.isSafeInteger(pid)
    || pid <= 0
    || typeof systemctl !== "string"
    || resolve(systemctl) !== systemctl
  ) {
    throw new TypeError("scope attestation inputs are invalid");
  }
  validateScopeIdentity({ unit, slice });
  await validateCgroupMount(cgroupRoot);

  let lastError;
  for (let attempt = 0; attempt < 100; attempt += 1) {
    try {
      const [beforeStat, beforeStatus, beforeCgroup] = await Promise.all([
        readFile(`${procRoot}/${pid}/stat`),
        readFile(`${procRoot}/${pid}/status`),
        readFile(`${procRoot}/${pid}/cgroup`),
      ]);
      const beforeIdentity = stoppedProcessIdentity(
        beforeStat,
        beforeStatus,
        pid,
      );
      const pidControlGroup = parseUnifiedCgroup(beforeCgroup);
      const [unitControlGroup, sliceControlGroup] = await Promise.all([
        controlGroupFor(systemctl, unit),
        controlGroupFor(systemctl, slice),
      ]);
      const controlGroup = validateControlGroups({
        pidControlGroup,
        unitControlGroup,
        sliceControlGroup,
        unit,
        slice,
      });
      const controls = await validateControlFiles(cgroupRoot, controlGroup);
      const sliceControls = await validateControlFiles(
        cgroupRoot,
        sliceControlGroup,
      );
      const [afterStat, afterStatus, afterCgroup] = await Promise.all([
        readFile(`${procRoot}/${pid}/stat`),
        readFile(`${procRoot}/${pid}/status`),
        readFile(`${procRoot}/${pid}/cgroup`),
      ]);
      const afterIdentity = stoppedProcessIdentity(
        afterStat,
        afterStatus,
        pid,
      );
      if (
        afterIdentity.startTimeTicks !== beforeIdentity.startTimeTicks
        || parseUnifiedCgroup(afterCgroup) !== controlGroup
      ) {
        throw new Error("attested process changed identity before release");
      }
      return {
        schema: "logos.palace.basecamp-process-scope",
        version: 1,
        status: "attested",
        unit,
        slice,
        controlGroup,
        attestedPid: pid,
        attestedStartTimeTicks: beforeIdentity.startTimeTicks,
        barrier: "sigstop-before-exec",
        ...controls,
        sliceControlGroup,
        sliceCgroupPath: sliceControls.cgroupPath,
        sliceEventsPath: sliceControls.eventsPath,
        sliceKillPath: sliceControls.killPath,
      };
    } catch (error) {
      lastError = error;
      if (attempt === 99) break;
      await new Promise((resolvePromise) => setTimeout(resolvePromise, 50));
    }
  }
  throw new Error(
    `process scope attestation failed: ${
      lastError instanceof Error ? lastError.message : String(lastError)
    }`,
  );
}

export function parseCgroupEvents(bytes) {
  const encoded = bounded(
    Buffer.isBuffer(bytes) ? bytes : Buffer.from(bytes),
    4096,
    "cgroup events",
  ).toString("utf8");
  const lines = encoded.split("\n");
  if (lines.pop() !== "" || lines.length === 0) {
    throw new Error("cgroup events are malformed");
  }
  const fields = new Map();
  for (const line of lines) {
    const match = line.match(/^([a-z_]+) ([0-9]+)$/);
    if (!match || fields.has(match[1])) {
      throw new Error("cgroup events are malformed");
    }
    fields.set(match[1], match[2]);
  }
  if (!["0", "1"].includes(fields.get("populated"))) {
    throw new Error("cgroup populated state is invalid");
  }
  return { populated: fields.get("populated") === "1" };
}

async function controlsOrAbsent(validate, cgroupRoot, controlGroup) {
  if (!controlGroup) return undefined;
  try {
    return await validate(cgroupRoot, controlGroup);
  } catch (error) {
    if (error?.code === "ENOENT") return undefined;
    throw error;
  }
}

async function populatedOrAbsent(read, path) {
  if (!path) return false;
  try {
    return parseCgroupEvents(await read(path)).populated;
  } catch (error) {
    if (error?.code === "ENOENT") return false;
    throw error;
  }
}

async function killCgroupAndWait({
  controls,
  description,
  read,
  write,
  wait,
}) {
  const initial = await populatedOrAbsent(read, controls?.eventsPath);
  if (!initial) return false;
  await write(controls.killPath, "1\n", { flag: "w" });
  for (let attempt = 0; attempt < 200; attempt += 1) {
    if (!await populatedOrAbsent(read, controls.eventsPath)) return true;
    await wait(50);
  }
  throw new Error(`${description} remained populated after cgroup.kill`);
}

export async function retireScopeSlice({
  slice,
  systemctl,
  expectedControlGroup,
  cgroupRoot = "/sys/fs/cgroup",
  controlGroupFor = optionalControlGroupFor,
  read = readFile,
  write = writeFile,
  wait = (milliseconds) =>
    new Promise((resolvePromise) => setTimeout(resolvePromise, milliseconds)),
  validateMount = validateCgroupMount,
  validateControls = validateControlFiles,
  stopSlice = defaultStopSlice,
  activeStateFor = defaultActiveStateFor,
  loadedStateFor = defaultLoadedStateFor,
}) {
  validateSliceIdentity(slice);
  if (typeof systemctl !== "string" || resolve(systemctl) !== systemctl) {
    throw new TypeError("process slice retirement inputs are invalid");
  }
  await validateMount(cgroupRoot);

  const [
    reportedControlGroup,
    initialActiveState,
    initiallyLoaded,
  ] = await Promise.all([
    controlGroupFor(systemctl, slice),
    activeStateFor(systemctl, slice),
    loadedStateFor(systemctl, slice),
  ]);
  if (reportedControlGroup) {
    validateSliceControlGroup({
      sliceControlGroup: reportedControlGroup,
      slice,
    });
  }
  if (expectedControlGroup !== undefined) {
    validateSliceControlGroup({
      sliceControlGroup: expectedControlGroup,
      slice,
    });
    if (
      reportedControlGroup
      && reportedControlGroup !== expectedControlGroup
    ) {
      throw new Error("process slice ControlGroup changed before retirement");
    }
  }
  if (
    !reportedControlGroup
    && expectedControlGroup === undefined
    && initialActiveState === "inactive"
    && !initiallyLoaded
  ) {
    return { residueKilled: false };
  }
  const controlGroup = expectedControlGroup ?? reportedControlGroup;
  const controls = await controlsOrAbsent(
    validateControls,
    cgroupRoot,
    controlGroup,
  );
  const residueKilled = await killCgroupAndWait({
    controls,
    description: "process slice",
    read,
    write,
    wait,
  });

  await stopSlice(systemctl, slice);
  let lastState = "unknown";
  for (let attempt = 0; attempt < 200; attempt += 1) {
    const [remainingControlGroup, activeState, loaded] = await Promise.all([
      controlGroupFor(systemctl, slice),
      activeStateFor(systemctl, slice),
      loadedStateFor(systemctl, slice),
    ]);
    if (remainingControlGroup) {
      validateSliceControlGroup({
        sliceControlGroup: remainingControlGroup,
        slice,
      });
    }
    lastState =
      `ControlGroup=${remainingControlGroup ?? ""},`
      + ` ActiveState=${activeState}, loaded=${loaded}`;
    if (!remainingControlGroup && activeState === "inactive" && !loaded) {
      return { residueKilled };
    }
    if (attempt !== 199) await wait(50);
  }
  throw new Error(`process slice did not unload after stop: ${lastState}`);
}

export async function cleanupAttestedScope(
  evidence,
  {
    systemctl,
    cgroupRoot = "/sys/fs/cgroup",
    read = readFile,
    write = writeFile,
    wait = (milliseconds) =>
      new Promise((resolvePromise) => setTimeout(resolvePromise, milliseconds)),
    validateUnitControls = async (root, controlGroup) => {
      await validateCgroupMount(root);
      return validateControlFiles(root, controlGroup);
    },
    validateSliceControls = async (root, controlGroup) =>
      validateControlFiles(root, controlGroup),
    controlGroupFor = optionalControlGroupFor,
    validateMount = validateCgroupMount,
    stopSlice = defaultStopSlice,
    activeStateFor = defaultActiveStateFor,
    loadedStateFor = defaultLoadedStateFor,
    commandExitStatus,
  } = {},
) {
  const alreadyCleaned = evidence?.status === "cleaned";
  const recordedExitStatus = alreadyCleaned
    ? evidence?.commandExitStatus
    : (commandExitStatus ?? 255);
  if (
    evidence?.schema !== "logos.palace.basecamp-process-scope"
    || evidence?.version !== 1
    || !["attested", "cleaned"].includes(evidence?.status)
    || evidence?.barrier !== "sigstop-before-exec"
    || !Number.isSafeInteger(evidence?.attestedPid)
    || evidence.attestedPid <= 0
    || !Number.isSafeInteger(evidence?.attestedStartTimeTicks)
    || evidence.attestedStartTimeTicks <= 0
    || !Number.isSafeInteger(recordedExitStatus)
    || recordedExitStatus < 0
    || recordedExitStatus > 255
    || (
      alreadyCleaned
      && commandExitStatus !== undefined
      && commandExitStatus !== recordedExitStatus
    )
    || (
      alreadyCleaned
      && (
        evidence?.cleanup?.status !== "passed"
        || evidence.cleanup.finalPopulated !== false
        || evidence.cleanup.sliceFinalPopulated !== false
        || typeof evidence.cleanup.initiallyPopulated !== "boolean"
        || typeof evidence.cleanup.residueKilled !== "boolean"
        || typeof evidence.cleanup.sliceInitiallyPopulated !== "boolean"
        || typeof evidence.cleanup.sliceResidueKilled !== "boolean"
      )
    )
  ) {
    throw new Error("process scope evidence is invalid");
  }
  if (typeof systemctl !== "string" || resolve(systemctl) !== systemctl) {
    throw new TypeError("process scope cleanup inputs are invalid");
  }
  validateScopeIdentity(evidence);
  validateControlGroups({
    pidControlGroup: evidence.controlGroup,
    unitControlGroup: evidence.controlGroup,
    sliceControlGroup: dirname(evidence.controlGroup),
    unit: evidence.unit,
    slice: evidence.slice,
  });
  if (
    evidence.cgroupPath
      !== join(cgroupRoot, evidence.controlGroup)
    || evidence.eventsPath !== join(evidence.cgroupPath, "cgroup.events")
    || evidence.killPath !== join(evidence.cgroupPath, "cgroup.kill")
    || evidence.sliceControlGroup !== dirname(evidence.controlGroup)
    || evidence.sliceCgroupPath
      !== join(cgroupRoot, evidence.sliceControlGroup)
    || evidence.sliceEventsPath
      !== join(evidence.sliceCgroupPath, "cgroup.events")
    || evidence.sliceKillPath
      !== join(evidence.sliceCgroupPath, "cgroup.kill")
  ) {
    throw new Error("process scope evidence paths differ");
  }

  const unitControls = await controlsOrAbsent(
    validateUnitControls,
    cgroupRoot,
    evidence.controlGroup,
  );
  if (
    unitControls
    && (
      unitControls.cgroupPath !== evidence.cgroupPath
      || unitControls.eventsPath !== evidence.eventsPath
      || unitControls.killPath !== evidence.killPath
    )
  ) {
    throw new Error("process scope control files changed after attestation");
  }
  const sliceControls = await controlsOrAbsent(
    validateSliceControls,
    cgroupRoot,
    evidence.sliceControlGroup,
  );
  if (
    sliceControls
    && (
      sliceControls.cgroupPath !== evidence.sliceCgroupPath
      || sliceControls.eventsPath !== evidence.sliceEventsPath
      || sliceControls.killPath !== evidence.sliceKillPath
    )
  ) {
    throw new Error("process slice control files changed after attestation");
  }

  const unitResidue = await killCgroupAndWait({
    controls: unitControls,
    description: "process scope",
    read,
    write,
    wait,
  });
  const retiredSlice = await retireScopeSlice({
    slice: evidence.slice,
    systemctl,
    expectedControlGroup: evidence.sliceControlGroup,
    cgroupRoot,
    controlGroupFor,
    read,
    write,
    wait,
    validateMount,
    validateControls: validateSliceControls,
    stopSlice,
    activeStateFor,
    loadedStateFor,
  });
  const sliceResidue = retiredSlice.residueKilled;
  const prior = alreadyCleaned ? evidence.cleanup : undefined;
  return {
    ...evidence,
    status: "cleaned",
    commandExitStatus: recordedExitStatus,
    cleanup: {
      status: "passed",
      initiallyPopulated:
        (prior?.initiallyPopulated ?? false) || unitResidue,
      residueKilled: (prior?.residueKilled ?? false) || unitResidue,
      finalPopulated: false,
      sliceInitiallyPopulated:
        (prior?.sliceInitiallyPopulated ?? false) || sliceResidue,
      sliceResidueKilled:
        (prior?.sliceResidueKilled ?? false) || sliceResidue,
      sliceFinalPopulated: false,
    },
  };
}

export async function cleanupPlannedScope({
  unit,
  slice,
  systemctl,
  cgroupRoot = "/sys/fs/cgroup",
  controlGroupFor = optionalControlGroupFor,
  read = readFile,
  write = writeFile,
  wait = (milliseconds) =>
    new Promise((resolvePromise) => setTimeout(resolvePromise, milliseconds)),
  validateMount = validateCgroupMount,
  validateControls = validateControlFiles,
  stopSlice = defaultStopSlice,
  activeStateFor = defaultActiveStateFor,
  loadedStateFor = defaultLoadedStateFor,
}) {
  validateScopeIdentity({ unit, slice });
  if (typeof systemctl !== "string" || resolve(systemctl) !== systemctl) {
    throw new TypeError("planned scope cleanup inputs are invalid");
  }
  await validateMount(cgroupRoot);
  const unitControlGroup = await controlGroupFor(systemctl, unit);
  let expectedSliceControlGroup;
  if (unitControlGroup) {
    expectedSliceControlGroup = dirname(unitControlGroup);
    validateControlGroups({
      pidControlGroup: unitControlGroup,
      unitControlGroup,
      sliceControlGroup: expectedSliceControlGroup,
      unit,
      slice,
    });
  }
  const unitControls = await controlsOrAbsent(
    validateControls,
    cgroupRoot,
    unitControlGroup,
  );
  const unitResidue = unitControls
    ? await killCgroupAndWait({
      controls: unitControls,
      description: "planned process scope",
      read,
      write,
      wait,
    })
    : false;
  const retiredSlice = await retireScopeSlice({
    slice,
    systemctl,
    expectedControlGroup: expectedSliceControlGroup,
    cgroupRoot,
    controlGroupFor,
    read,
    write,
    wait,
    validateMount,
    validateControls,
    stopSlice,
    activeStateFor,
    loadedStateFor,
  });
  return {
    residueKilled: unitResidue || retiredSlice.residueKilled,
  };
}
