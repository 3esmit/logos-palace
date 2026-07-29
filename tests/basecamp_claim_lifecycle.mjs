import { createHash } from "node:crypto";
import {
  link,
  lstat,
  open,
  readFile,
  readdir,
  realpath,
  rename,
  unlink,
} from "node:fs/promises";
import { basename, dirname, join, resolve } from "node:path";

export const releaseProgramId =
  "e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61";
export const releaseRootId =
  "12ff117a38d756f132cf616cea36fa007653c3c99727c1b475503caa345cbf2a";

const claimSchema = "logos.palace.basecamp-active-run-claim";
const reportSchema = "logos.palace.basecamp-mvp-compiled-report";
const rollForwardSchema =
  "logos.palace.basecamp-active-run-roll-forward";
const completionSchema =
  "logos.palace.basecamp-active-run-completion";
const implementedGates = [
  "gate0",
  "gate1",
  "gate2",
  "gate3",
  "gate4",
  "gate5",
  "gate6",
];
const preGate3Phases = new Set([
  "gate0",
  "gate1",
  "gate2",
  "signal",
  "run-scope",
  "active-run-claim",
  "gate3-claim-transition",
]);
const sha256Pattern = /^[0-9a-f]{64}$/;
const sourceCommitPattern = /^[0-9a-f]{40}$/;
const narHashPattern = /^sha256-[A-Za-z0-9+/]{43}=$/;

export const auditedLegacyPreGate3 = Object.freeze({
  gitCommit: "1a61a457bb13e0c016f838a7fcd9c8820098a68a",
  snapshotNarHash:
    "sha256-KYD79MQnS+uVRZFA2YGung18ABWUV4k5UI5otIPD4uo=",
  snapshotNarSize: 6_270_528,
  snapshotRunnerSha256:
    "e706612c79cdd59a07166ed3ada3fc691505f96969eac1dfed41dcadf167e3ea",
  runtimeManifestSha256:
    "dceba39d0b74dde0319c586439e91e845351c400a8ddb7e27494265875e65eba",
  compiledReportSha256:
    "24d7366ed2aa6784535ffb1ad04f05cea498935eeaa8dbb6740562cad47b78d7",
  gate0ReportSha256:
    "efe27b0d87ab0fac5f0eb96d08189c456be6f5f070b3382af3b8576d415ef41e",
  gate1ReportSha256:
    "d22d60a15a1b7310d40048735abd3f6850ecf3fd9cddddfec588b8348a9027b3",
});

function exactKeys(value, expected) {
  return value !== null
    && typeof value === "object"
    && !Array.isArray(value)
    && JSON.stringify(Object.keys(value).sort())
      === JSON.stringify([...expected].sort());
}

function exactJson(left, right) {
  return JSON.stringify(left) === JSON.stringify(right);
}

export function sha256(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

export function validateProcessScopeNames(
  processScopeSlice,
  processScopePrefix,
) {
  const match = /^logos-palace-run-([A-Za-z0-9]{8})\.slice$/.exec(
    processScopeSlice,
  );
  if (
    !match
    || processScopePrefix !== `logos-palace-run-${match[1]}`
  ) {
    throw new Error("active-run process scope names are invalid");
  }
  return { processScopeSlice, processScopePrefix };
}

function validateCommonIdentity(common) {
  if (
    !exactKeys(common, [
      "schema",
      "version",
      "uid",
      "releaseProgramId",
      "releaseRootId",
      "runDirectory",
      "productSnapshot",
      "gcRootPath",
      "gcRootTarget",
      "gitCommit",
      "snapshotNarHash",
      "snapshotNarSize",
      "snapshotRunnerSha256",
      "runtimeManifestPath",
      "runtimeManifestSha256",
      "processScopeSlice",
      "processScopePrefix",
    ])
    || common.schema !== claimSchema
    || common.version !== 2
    || !Number.isSafeInteger(common.uid)
    || common.uid < 0
    || common.releaseProgramId !== releaseProgramId
    || common.releaseRootId !== releaseRootId
    || resolve(common.runDirectory) !== common.runDirectory
    || resolve(common.productSnapshot) !== common.productSnapshot
    || resolve(common.gcRootPath) !== common.gcRootPath
    || common.gcRootTarget !== common.productSnapshot
    || !sourceCommitPattern.test(common.gitCommit)
    || !narHashPattern.test(common.snapshotNarHash)
    || !Number.isSafeInteger(common.snapshotNarSize)
    || common.snapshotNarSize <= 0
    || common.snapshotNarSize > 64 * 1024 * 1024
    || !sha256Pattern.test(common.snapshotRunnerSha256)
    || resolve(common.runtimeManifestPath) !== common.runtimeManifestPath
    || !sha256Pattern.test(common.runtimeManifestSha256)
  ) {
    throw new Error("active-run common identity is invalid");
  }
  validateProcessScopeNames(
    common.processScopeSlice,
    common.processScopePrefix,
  );
  if (
    basename(common.runDirectory)
      !== `run.${common.processScopePrefix.slice(
        "logos-palace-run-".length,
      )}`
  ) {
    throw new Error("active-run directory and process scope identity differ");
  }
  return common;
}

function rollForwardKeys(value) {
  return exactKeys(value, [
    "evidence",
    "evidenceSha256",
    "retiredClaimArchive",
    "retiredClaimArchiveSha256",
    "predecessorClaimSha256",
    "predecessorCompiledReportSha256",
  ])
    && value.evidence === "claim-roll-forward.json"
    && sha256Pattern.test(value.evidenceSha256)
    && value.retiredClaimArchive === "retired-active-claim.json"
    && sha256Pattern.test(value.retiredClaimArchiveSha256)
    && sha256Pattern.test(value.predecessorClaimSha256)
    && sha256Pattern.test(value.predecessorCompiledReportSha256);
}

function commonFromClaim(claim) {
  return Object.fromEntries(
    [
      "schema",
      "version",
      "uid",
      "releaseProgramId",
      "releaseRootId",
      "runDirectory",
      "productSnapshot",
      "gcRootPath",
      "gcRootTarget",
      "gitCommit",
      "snapshotNarHash",
      "snapshotNarSize",
      "snapshotRunnerSha256",
      "runtimeManifestPath",
      "runtimeManifestSha256",
      "processScopeSlice",
      "processScopePrefix",
    ].map((field) => [field, claim?.[field]]),
  );
}

export function validateStoredV2Claim(claim) {
  const common = validateCommonIdentity(commonFromClaim(claim));
  return validateV2Claim(claim, common);
}

export function validateStoredLegacyClaim(
  claim,
  legacyAudit = auditedLegacyPreGate3,
) {
  return validateLegacyClaim(claim, legacyAudit);
}

function matchesCommon(claim, expectedCommon) {
  return Object.entries(expectedCommon).every(
    ([field, value]) => claim?.[field] === value,
  );
}

export function validateV2Claim(claim, expectedCommon) {
  const stateFields = {
    "active-pre-gate3": [],
    "gate3-entered": ["gate3EnteredAtUnixMs"],
    completed: [
      "gate3EnteredAtUnixMs",
      "completedAtUnixMs",
      "compiledReportSha256",
    ],
  }[claim?.status];
  const optionalRollForward =
    Object.hasOwn(claim ?? {}, "rollForward") ? ["rollForward"] : [];
  if (
    !stateFields
    || !exactKeys(claim, [
      ...Object.keys(expectedCommon),
      "status",
      "createdAtUnixMs",
      ...stateFields,
      ...optionalRollForward,
    ])
    || !matchesCommon(claim, expectedCommon)
    || !Number.isSafeInteger(claim.createdAtUnixMs)
    || claim.createdAtUnixMs <= 0
    || (
      Object.hasOwn(claim, "rollForward")
      && !rollForwardKeys(claim.rollForward)
    )
    || (
      claim.status !== "active-pre-gate3"
      && (
        !Number.isSafeInteger(claim.gate3EnteredAtUnixMs)
        || claim.gate3EnteredAtUnixMs < claim.createdAtUnixMs
      )
    )
    || (
      claim.status === "completed"
      && (
        !Number.isSafeInteger(claim.completedAtUnixMs)
        || claim.completedAtUnixMs < claim.gate3EnteredAtUnixMs
        || !sha256Pattern.test(claim.compiledReportSha256)
      )
    )
  ) {
    throw new Error("persistent active-run claim v2 state is invalid");
  }
  return claim;
}

function transitionTimestamp(now, minimum) {
  const value = now();
  if (!Number.isSafeInteger(value) || value <= 0) {
    throw new Error("active-run transition timestamp is invalid");
  }
  return Math.max(value, minimum);
}

function validateLegacyClaim(claim, legacyAudit) {
  if (
    !exactKeys(claim, [
      "schema",
      "version",
      "uid",
      "releaseProgramId",
      "releaseRootId",
      "runDirectory",
      "productSnapshot",
      "gcRootPath",
      "gcRootTarget",
      "gitCommit",
      "snapshotNarHash",
      "snapshotNarSize",
      "snapshotRunnerSha256",
      "runtimeManifestPath",
      "runtimeManifestSha256",
      "status",
      "createdAtUnixMs",
    ])
    || claim.schema !== claimSchema
    || claim.version !== 1
    || claim.releaseProgramId !== releaseProgramId
    || claim.releaseRootId !== releaseRootId
    || claim.status !== "active"
    || !Number.isSafeInteger(claim.uid)
    || claim.uid < 0
    || resolve(claim.runDirectory) !== claim.runDirectory
    || resolve(claim.productSnapshot) !== claim.productSnapshot
    || resolve(claim.gcRootPath) !== claim.gcRootPath
    || claim.gcRootTarget !== claim.productSnapshot
    || resolve(claim.runtimeManifestPath) !== claim.runtimeManifestPath
    || !Number.isSafeInteger(claim.createdAtUnixMs)
    || claim.createdAtUnixMs <= 0
    || claim.gitCommit !== legacyAudit.gitCommit
    || claim.snapshotNarHash !== legacyAudit.snapshotNarHash
    || claim.snapshotNarSize !== legacyAudit.snapshotNarSize
    || claim.snapshotRunnerSha256
      !== legacyAudit.snapshotRunnerSha256
    || claim.runtimeManifestSha256
      !== legacyAudit.runtimeManifestSha256
  ) {
    throw new Error("legacy active-run claim is not the audited predecessor");
  }
  return claim;
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
    throw new Error("claim lifecycle directory is not canonical owner state");
  }
  return path;
}

async function secureFile(path, uid, maximum, description) {
  const metadata = await lstat(path);
  if (
    metadata.isSymbolicLink()
    || !metadata.isFile()
    || metadata.uid !== uid
    || (metadata.mode & 0o777) !== 0o600
    || metadata.size <= 0
    || metadata.size > maximum
    || await realpath(path) !== path
  ) {
    throw new Error(`${description} is not a secure regular file`);
  }
  const bytes = await readFile(path);
  if (bytes.length !== metadata.size) {
    throw new Error(`${description} changed while being read`);
  }
  return bytes;
}

async function parseSecureJson(path, uid, maximum, description) {
  const bytes = await secureFile(path, uid, maximum, description);
  let value;
  try {
    value = JSON.parse(bytes);
  } catch {
    throw new Error(`${description} is not valid JSON`);
  }
  return { bytes, value, sha256: sha256(bytes) };
}

async function absent(path, description) {
  try {
    await lstat(path);
  } catch (error) {
    if (error?.code === "ENOENT") return;
    throw error;
  }
  throw new Error(`${description} must be absent`);
}

async function syncDirectory(path) {
  const handle = await open(path, "r");
  try {
    await handle.sync();
  } finally {
    await handle.close();
  }
}

async function writeExclusiveDurable(path, bytes, uid) {
  const directory = dirname(path);
  const temporary = join(
    directory,
    `.${basename(path)}-${process.pid}-${Date.now()}.tmp`,
  );
  let handle;
  try {
    handle = await open(temporary, "wx", 0o600);
    await handle.writeFile(bytes);
    await handle.sync();
    await handle.close();
    handle = undefined;
    await link(temporary, path);
    await unlink(temporary);
    await syncDirectory(directory);
  } catch (error) {
    await handle?.close().catch(() => {});
    await unlink(temporary).catch((unlinkError) => {
      if (unlinkError?.code !== "ENOENT") throw unlinkError;
    });
    if (error?.code !== "EEXIST") throw error;
    const existing = await secureFile(
      path,
      uid,
      128 * 1024,
      "existing claim lifecycle evidence",
    );
    if (!existing.equals(bytes)) {
      throw new Error("existing claim lifecycle evidence differs");
    }
  }
}

async function writeNewClaim(claimDirectory, claimPath, value) {
  const temporary = join(
    claimDirectory,
    `.claim-${process.pid}-${Date.now()}.tmp`,
  );
  let handle;
  try {
    handle = await open(temporary, "wx", 0o600);
    await handle.writeFile(`${JSON.stringify(value, null, 2)}\n`, "utf8");
    await handle.sync();
    await handle.close();
    handle = undefined;
    await link(temporary, claimPath);
    await unlink(temporary);
    await syncDirectory(claimDirectory);
  } catch (error) {
    await handle?.close().catch(() => {});
    await unlink(temporary).catch((unlinkError) => {
      if (unlinkError?.code !== "ENOENT") throw unlinkError;
    });
    throw error;
  }
}

async function durableReplaceClaim(claimDirectory, claimPath, value) {
  const temporary = join(
    claimDirectory,
    `.claim-${process.pid}-${Date.now()}.tmp`,
  );
  let handle;
  try {
    handle = await open(temporary, "wx", 0o600);
    await handle.writeFile(`${JSON.stringify(value, null, 2)}\n`, "utf8");
    await handle.sync();
    await handle.close();
    handle = undefined;
    await rename(temporary, claimPath);
    await syncDirectory(claimDirectory);
  } catch (error) {
    await handle?.close().catch(() => {});
    await unlink(temporary).catch((unlinkError) => {
      if (unlinkError?.code !== "ENOENT") throw unlinkError;
    });
    throw error;
  }
}

async function readClaim(claimPath, uid) {
  try {
    return await parseSecureJson(
      claimPath,
      uid,
      64 * 1024,
      "persistent active-run claim",
    );
  } catch (error) {
    if (error?.code === "ENOENT") return undefined;
    throw error;
  }
}

function validateFailedReport(report, predecessor) {
  const expectedGateReports = {
    gate0: null,
    gate1: "gate1/gate1-report.json",
    gate2: "gate2/gate2-report.json",
    gate3: "gate3/gate3-report.json",
    gate4: "gate4/gate4-report.json",
    gate5: "gate4/gate4-report.json",
    gate6: "gate4/gate4-report.json",
  };
  if (
    !exactKeys(report, [
      "schema",
      "version",
      "status",
      "fullMvp",
      "productSnapshot",
      "scope",
      "failure",
      "gates",
    ])
    || report.schema !== reportSchema
    || report.version !== 1
    || report.status !== "failed"
    || report.fullMvp !== "not-evaluated"
    || report.productSnapshot !== predecessor.productSnapshot
    || !exactKeys(report.scope, ["implementedGates", "pendingGates"])
    || !exactJson(report.scope.implementedGates, implementedGates)
    || !exactJson(report.scope.pendingGates, [])
    || !exactKeys(report.failure, ["phase", "message"])
    || !preGate3Phases.has(report.failure.phase)
    || typeof report.failure.message !== "string"
    || report.failure.message.length <= 0
    || report.failure.message.length > 1024
    || !exactKeys(report.gates, implementedGates)
  ) {
    throw new Error("pre-Gate 3 failed compiled report is invalid");
  }
  for (const gate of implementedGates) {
    if (
      !exactKeys(report.gates[gate], ["status", "report"])
      || report.gates[gate].status !== "unknown"
      || report.gates[gate].report !== expectedGateReports[gate]
    ) {
      throw new Error("pre-Gate 3 failed compiled gate state is invalid");
    }
  }
  return report.failure.phase;
}

async function validatePredecessorIdentity({
  predecessor,
  common,
  uid,
  validateImmutableSnapshot,
}) {
  if (
    predecessor.uid !== uid
    || predecessor.runDirectory === common.runDirectory
    || dirname(predecessor.runDirectory) !== dirname(common.runDirectory)
  ) {
    throw new Error("predecessor run ownership or scope differs");
  }
  await canonicalOwnerDirectory(predecessor.runDirectory, uid, 0o700);
  await validateImmutableSnapshot(predecessor);

  const marker = await secureFile(
    join(predecessor.runDirectory, "product-snapshot"),
    uid,
    4096,
    "predecessor product snapshot marker",
  );
  if (marker.toString("utf8") !== `${predecessor.productSnapshot}\n`) {
    throw new Error("predecessor product snapshot marker differs");
  }

  const identity = await parseSecureJson(
    join(predecessor.runDirectory, "source-identity.json"),
    uid,
    64 * 1024,
    "predecessor source identity",
  );
  if (
    !exactKeys(identity.value, [
      "schema",
      "version",
      "gitCommit",
      "productSnapshot",
      "snapshotNarHash",
      "snapshotNarSize",
      "snapshotRunnerSha256",
      "trackedPathsSha256",
      "snapshotEvidenceSha256",
      "snapshotGcRoot",
    ])
    || identity.value.schema !== "logos.palace.basecamp-source-identity"
    || identity.value?.version !== 1
    || identity.value?.gitCommit !== predecessor.gitCommit
    || identity.value?.productSnapshot !== predecessor.productSnapshot
    || identity.value?.snapshotGcRoot !== predecessor.gcRootPath
    || identity.value?.snapshotNarHash !== predecessor.snapshotNarHash
    || identity.value?.snapshotNarSize !== predecessor.snapshotNarSize
    || identity.value?.snapshotRunnerSha256
      !== predecessor.snapshotRunnerSha256
    || !sha256Pattern.test(identity.value?.trackedPathsSha256)
    || !sha256Pattern.test(identity.value?.snapshotEvidenceSha256)
  ) {
    throw new Error("predecessor source identity differs");
  }

  const runtimeManifest = await secureFile(
    predecessor.runtimeManifestPath,
    uid,
    128 * 1024,
    "predecessor runtime manifest",
  );
  if (
    dirname(predecessor.runtimeManifestPath) !== predecessor.runDirectory
    || basename(predecessor.runtimeManifestPath)
      !== "runtime-output-manifest.json"
    || sha256(runtimeManifest) !== predecessor.runtimeManifestSha256
  ) {
    throw new Error("predecessor runtime manifest differs");
  }

  const gcRoot = await lstat(predecessor.gcRootPath);
  if (
    !gcRoot.isSymbolicLink()
    || await realpath(predecessor.gcRootPath) !== predecessor.productSnapshot
  ) {
    throw new Error("predecessor GC root does not retain its snapshot");
  }
}

async function validatePreGate3Artifacts({
  predecessor,
  claimVersion,
  uid,
  legacyAudit,
}) {
  const run = predecessor.runDirectory;
  await absent(join(run, "gate3"), "predecessor Gate 3 evidence");
  await absent(join(run, "gate4"), "predecessor Gate 4 evidence");
  await absent(
    join(run, "active-claim-completion.json"),
    "predecessor claim completion",
  );
  await absent(
    join(run, "public-evidence.json"),
    "predecessor public evidence",
  );
  const sharedState = join(run, "shared-state");
  await canonicalOwnerDirectory(sharedState, uid, 0o700);
  if ((await readdir(sharedState)).length !== 0) {
    throw new Error("predecessor shared state is not empty");
  }

  const compiled = await parseSecureJson(
    join(run, "compiled-mvp-report.json"),
    uid,
    4 * 1024 * 1024,
    "predecessor compiled report",
  );
  const failurePhase = validateFailedReport(compiled.value, predecessor);

  if (claimVersion === 1) {
    if (
      failurePhase !== "gate1"
      || compiled.sha256 !== legacyAudit.compiledReportSha256
    ) {
      throw new Error("legacy failed report is not the audited report");
    }
    const gate0 = await secureFile(
      join(run, "gate0", "gate0-report.json"),
      uid,
      4 * 1024 * 1024,
      "legacy Gate 0 report",
    );
    const gate1 = await secureFile(
      join(run, "gate1", "gate1-report.json"),
      uid,
      4 * 1024 * 1024,
      "legacy Gate 1 report",
    );
    if (
      sha256(gate0) !== legacyAudit.gate0ReportSha256
      || sha256(gate1) !== legacyAudit.gate1ReportSha256
    ) {
      throw new Error("legacy gate reports are not the audited reports");
    }
    await absent(join(run, "gate2"), "legacy Gate 2 evidence");
  } else {
    const laterGates = {
      gate0: ["gate1", "gate2"],
      gate1: ["gate2"],
      gate2: [],
    }[failurePhase] ?? [];
    for (const gate of laterGates) {
      await absent(join(run, gate), `predecessor ${gate} evidence`);
    }
  }
  return {
    compiledReportSha256: compiled.sha256,
    failurePhase,
  };
}

function rollForwardRecord({
  predecessor,
  predecessorClaimSha256,
  predecessorCompiledReportSha256,
  retiredClaimArchiveSha256,
  common,
  failurePhase,
}) {
  return {
    schema: rollForwardSchema,
    version: 1,
    status: "retired-before-gate3",
    predecessor: {
      claimVersion: predecessor.version,
      claimSha256: predecessorClaimSha256,
      compiledReportSha256: predecessorCompiledReportSha256,
      failurePhase,
      gitCommit: predecessor.gitCommit,
      snapshotNarHash: predecessor.snapshotNarHash,
      snapshotNarSize: predecessor.snapshotNarSize,
      snapshotRunnerSha256: predecessor.snapshotRunnerSha256,
      runtimeManifestSha256: predecessor.runtimeManifestSha256,
    },
    successor: {
      gitCommit: common.gitCommit,
      snapshotNarHash: common.snapshotNarHash,
      snapshotNarSize: common.snapshotNarSize,
      snapshotRunnerSha256: common.snapshotRunnerSha256,
      runtimeManifestSha256: common.runtimeManifestSha256,
      processScopeSlice: common.processScopeSlice,
      processScopePrefix: common.processScopePrefix,
    },
    proof: {
      releaseLock: "held-exclusive",
      claimBoundProcessCount: 0,
      compiledFailure: "verified",
      gate3ClaimState: "never-entered",
      gate3Artifacts: "absent",
      gate4Artifacts: "absent",
      sharedState: "empty",
      completion: "absent",
      publicEvidence: "absent",
      retiredClaimArchiveSha256,
    },
  };
}

function validateRollForwardRecord({
  record,
  claim,
  archivedClaim,
  archivedClaimSha256,
}) {
  if (
    !exactKeys(record, [
      "schema",
      "version",
      "status",
      "predecessor",
      "successor",
      "proof",
    ])
    || record.schema !== rollForwardSchema
    || record.version !== 1
    || record.status !== "retired-before-gate3"
    || !exactKeys(record.predecessor, [
      "claimVersion",
      "claimSha256",
      "compiledReportSha256",
      "failurePhase",
      "gitCommit",
      "snapshotNarHash",
      "snapshotNarSize",
      "snapshotRunnerSha256",
      "runtimeManifestSha256",
    ])
    || record.predecessor.claimVersion !== archivedClaim.version
    || record.predecessor.claimSha256 !== archivedClaimSha256
    || record.predecessor.compiledReportSha256
      !== claim.rollForward.predecessorCompiledReportSha256
    || (
      archivedClaim.version === 1
        ? record.predecessor.failurePhase !== "gate1"
        : !preGate3Phases.has(record.predecessor.failurePhase)
    )
    || record.predecessor.gitCommit !== archivedClaim.gitCommit
    || record.predecessor.snapshotNarHash !== archivedClaim.snapshotNarHash
    || record.predecessor.snapshotNarSize !== archivedClaim.snapshotNarSize
    || record.predecessor.snapshotRunnerSha256
      !== archivedClaim.snapshotRunnerSha256
    || record.predecessor.runtimeManifestSha256
      !== archivedClaim.runtimeManifestSha256
    || !exactKeys(record.successor, [
      "gitCommit",
      "snapshotNarHash",
      "snapshotNarSize",
      "snapshotRunnerSha256",
      "runtimeManifestSha256",
      "processScopeSlice",
      "processScopePrefix",
    ])
    || record.successor.gitCommit !== claim.gitCommit
    || record.successor.snapshotNarHash !== claim.snapshotNarHash
    || record.successor.snapshotNarSize !== claim.snapshotNarSize
    || record.successor.snapshotRunnerSha256 !== claim.snapshotRunnerSha256
    || record.successor.runtimeManifestSha256
      !== claim.runtimeManifestSha256
    || record.successor.processScopeSlice !== claim.processScopeSlice
    || record.successor.processScopePrefix !== claim.processScopePrefix
    || !exactKeys(record.proof, [
      "releaseLock",
      "claimBoundProcessCount",
      "compiledFailure",
      "gate3ClaimState",
      "gate3Artifacts",
      "gate4Artifacts",
      "sharedState",
      "completion",
      "publicEvidence",
      "retiredClaimArchiveSha256",
    ])
    || record.proof.releaseLock !== "held-exclusive"
    || record.proof.claimBoundProcessCount !== 0
    || record.proof.compiledFailure !== "verified"
    || record.proof.gate3ClaimState !== "never-entered"
    || record.proof.gate3Artifacts !== "absent"
    || record.proof.gate4Artifacts !== "absent"
    || record.proof.sharedState !== "empty"
    || record.proof.completion !== "absent"
    || record.proof.publicEvidence !== "absent"
    || record.proof.retiredClaimArchiveSha256 !== archivedClaimSha256
  ) {
    throw new Error("active-run roll-forward evidence is invalid");
  }
  return record;
}

async function assertNoClaimProcesses(scanClaimBoundProcesses, input) {
  const processes = await scanClaimBoundProcesses(input);
  if (!Array.isArray(processes) || processes.length !== 0) {
    throw new Error("predecessor retains claim-bound processes");
  }
}

function newClaim(common, now, rollForward) {
  return validateV2Claim({
    ...common,
    status: "active-pre-gate3",
    createdAtUnixMs: now,
    ...(rollForward ? { rollForward } : {}),
  }, common);
}

export function createClaimLifecycle({
  uid,
  claimDirectory,
  claimPath,
  common,
  now = () => Date.now(),
  assertReleaseLockHeld,
  scanClaimBoundProcesses,
  retirePredecessorScope,
  validateImmutableSnapshot,
  completedReportSha256,
  writeCompletionRecord,
  legacyAudit = auditedLegacyPreGate3,
}) {
  validateCommonIdentity(common);
  if (
    uid !== common.uid
    || resolve(claimDirectory) !== claimDirectory
    || dirname(claimPath) !== claimDirectory
    || typeof assertReleaseLockHeld !== "function"
    || typeof scanClaimBoundProcesses !== "function"
    || typeof retirePredecessorScope !== "function"
    || typeof validateImmutableSnapshot !== "function"
    || typeof completedReportSha256 !== "function"
    || typeof writeCompletionRecord !== "function"
    || !exactKeys(legacyAudit, [
      "gitCommit",
      "snapshotNarHash",
      "snapshotNarSize",
      "snapshotRunnerSha256",
      "runtimeManifestSha256",
      "compiledReportSha256",
      "gate0ReportSha256",
      "gate1ReportSha256",
    ])
    || !sourceCommitPattern.test(legacyAudit.gitCommit)
    || !narHashPattern.test(legacyAudit.snapshotNarHash)
    || !Number.isSafeInteger(legacyAudit.snapshotNarSize)
    || legacyAudit.snapshotNarSize <= 0
    || !sha256Pattern.test(legacyAudit.snapshotRunnerSha256)
    || !sha256Pattern.test(legacyAudit.runtimeManifestSha256)
    || !sha256Pattern.test(legacyAudit.compiledReportSha256)
    || !sha256Pattern.test(legacyAudit.gate0ReportSha256)
    || !sha256Pattern.test(legacyAudit.gate1ReportSha256)
  ) {
    throw new Error("claim lifecycle dependencies are invalid");
  }

  async function validateRollForwardChain(claim, depth = 0, seen = new Set()) {
    if (!Object.hasOwn(claim, "rollForward")) return claim;
    if (depth >= 16) {
      throw new Error("active-run roll-forward chain exceeds depth bound");
    }
    const rollForward = claim.rollForward;
    const evidencePath = join(claim.runDirectory, rollForward.evidence);
    const archivePath = join(
      claim.runDirectory,
      rollForward.retiredClaimArchive,
    );
    if (
      dirname(evidencePath) !== claim.runDirectory
      || basename(evidencePath) !== "claim-roll-forward.json"
      || dirname(archivePath) !== claim.runDirectory
      || basename(archivePath) !== "retired-active-claim.json"
      || rollForward.retiredClaimArchiveSha256
        !== rollForward.predecessorClaimSha256
      || seen.has(rollForward.predecessorClaimSha256)
    ) {
      throw new Error("active-run roll-forward chain identity is invalid");
    }

    const [evidence, archive] = await Promise.all([
      parseSecureJson(
        evidencePath,
        uid,
        128 * 1024,
        "active-run roll-forward evidence",
      ),
      parseSecureJson(
        archivePath,
        uid,
        64 * 1024,
        "retired active-run claim archive",
      ),
    ]);
    if (
      evidence.sha256 !== rollForward.evidenceSha256
      || archive.sha256 !== rollForward.retiredClaimArchiveSha256
      || archive.sha256 !== rollForward.predecessorClaimSha256
    ) {
      throw new Error("active-run roll-forward evidence digest differs");
    }

    let archivedClaim;
    if (archive.value?.version === 1) {
      archivedClaim = validateLegacyClaim(archive.value, legacyAudit);
    } else {
      const archivedCommon = validateCommonIdentity(
        commonFromClaim(archive.value),
      );
      archivedClaim = validateV2Claim(archive.value, archivedCommon);
    }
    if (
      archivedClaim.uid !== uid
      || (
        archivedClaim.version === 2
        && archivedClaim.status !== "active-pre-gate3"
      )
    ) {
      throw new Error("archived predecessor claim state is invalid");
    }
    validateRollForwardRecord({
      record: evidence.value,
      claim,
      archivedClaim,
      archivedClaimSha256: archive.sha256,
    });

    const nextSeen = new Set(seen);
    nextSeen.add(archive.sha256);
    if (archivedClaim.version === 2) {
      await validateRollForwardChain(archivedClaim, depth + 1, nextSeen);
    }
    return claim;
  }

  async function acquireOrRollForward() {
    await canonicalOwnerDirectory(claimDirectory, uid, 0o700);
    await assertReleaseLockHeld();
    const existing = await readClaim(claimPath, uid);
    if (!existing) {
      const claim = validateV2Claim(
        newClaim(common, transitionTimestamp(now, 1)),
        common,
      );
      try {
        await writeNewClaim(claimDirectory, claimPath, claim);
        return claim;
      } catch (error) {
        if (error?.code !== "EEXIST") throw error;
        const raced = await readClaim(claimPath, uid);
        const racedClaim = validateV2Claim(raced.value, common);
        return validateRollForwardChain(racedClaim);
      }
    }

    if (
      existing.value?.version === 2
      && matchesCommon(existing.value, common)
    ) {
      const current = validateV2Claim(existing.value, common);
      return validateRollForwardChain(current);
    }

    let predecessor;
    let processScopeSlice;
    let processScopePrefix;
    if (existing.value?.version === 1) {
      predecessor = validateLegacyClaim(existing.value, legacyAudit);
      processScopeSlice = common.processScopeSlice;
      processScopePrefix = common.processScopePrefix;
    } else {
      const predecessorCommon = validateCommonIdentity(
        commonFromClaim(existing.value),
      );
      predecessor = validateV2Claim(existing.value, predecessorCommon);
      await validateRollForwardChain(predecessor);
      if (predecessor.status !== "active-pre-gate3") {
        throw new Error("predecessor claim already crossed Gate 3");
      }
      processScopeSlice = predecessor.processScopeSlice;
      processScopePrefix = predecessor.processScopePrefix;
    }

    await validatePredecessorIdentity({
      predecessor,
      common,
      uid,
      validateImmutableSnapshot,
    });
    const proof = await validatePreGate3Artifacts({
      predecessor,
      claimVersion: predecessor.version,
      uid,
      legacyAudit,
    });
    const scanInput = {
      claimPath,
      processScopeSlice,
      processScopePrefix,
      legacy: predecessor.version === 1,
      createdAtUnixMs: predecessor.createdAtUnixMs,
    };
    if (predecessor.version === 2) {
      await retirePredecessorScope(scanInput);
    }
    await assertNoClaimProcesses(scanClaimBoundProcesses, scanInput);

    const archivePath = join(
      common.runDirectory,
      "retired-active-claim.json",
    );
    await writeExclusiveDurable(archivePath, existing.bytes, uid);
    const archiveSha256 = sha256(existing.bytes);
    const record = rollForwardRecord({
      predecessor,
      predecessorClaimSha256: existing.sha256,
      predecessorCompiledReportSha256: proof.compiledReportSha256,
      retiredClaimArchiveSha256: archiveSha256,
      common,
      failurePhase: proof.failurePhase,
    });
    const recordBytes = Buffer.from(
      `${JSON.stringify(record, null, 2)}\n`,
      "utf8",
    );
    const evidencePath = join(common.runDirectory, "claim-roll-forward.json");
    await writeExclusiveDurable(evidencePath, recordBytes, uid);

    const successor = validateV2Claim(
      newClaim(common, transitionTimestamp(now, 1), {
        evidence: basename(evidencePath),
        evidenceSha256: sha256(recordBytes),
        retiredClaimArchive: basename(archivePath),
        retiredClaimArchiveSha256: archiveSha256,
        predecessorClaimSha256: existing.sha256,
        predecessorCompiledReportSha256: proof.compiledReportSha256,
      }),
      common,
    );
    await validateRollForwardChain(successor);
    if (predecessor.version === 2) {
      await retirePredecessorScope(scanInput);
    }
    await assertNoClaimProcesses(scanClaimBoundProcesses, scanInput);
    await assertReleaseLockHeld();

    const unchanged = await secureFile(
      claimPath,
      uid,
      64 * 1024,
      "predecessor active-run claim",
    );
    if (sha256(unchanged) !== existing.sha256) {
      throw new Error("predecessor claim changed before replacement");
    }
    await durableReplaceClaim(claimDirectory, claimPath, successor);
    return successor;
  }

  async function exactCurrentClaim() {
    const existing = await readClaim(claimPath, uid);
    if (!existing) throw new Error("exact active-run claim does not exist");
    const claim = validateV2Claim(existing.value, common);
    return validateRollForwardChain(claim);
  }

  return {
    async execute(command) {
      if (command === "acquire-or-roll-forward") {
        const claim = await acquireOrRollForward();
        return { claim, output: claimPath };
      }

      let claim = await exactCurrentClaim();
      if (command === "state") {
        return { claim, output: claim.status };
      }
      if (command === "enter-gate3") {
        await assertReleaseLockHeld();
        if (claim.status === "active-pre-gate3") {
          claim = {
            ...claim,
            status: "gate3-entered",
            gate3EnteredAtUnixMs: transitionTimestamp(
              now,
              claim.createdAtUnixMs,
            ),
          };
          validateV2Claim(claim, common);
          await durableReplaceClaim(claimDirectory, claimPath, claim);
        } else if (claim.status !== "gate3-entered") {
          throw new Error("exact active-run claim cannot enter Gate 3");
        }
        return { claim, output: claimPath };
      }
      if (command === "verify") {
        await assertReleaseLockHeld();
        if (claim.status !== "gate3-entered") {
          throw new Error("exact active-run claim has not entered Gate 3");
        }
        return { claim, output: claimPath };
      }
      if (command === "complete") {
        await assertReleaseLockHeld();
        if (claim.status !== "gate3-entered") {
          throw new Error("exact active-run claim cannot complete");
        }
        const reportSha256 = await completedReportSha256();
        if (!sha256Pattern.test(reportSha256)) {
          throw new Error("completed claim report digest is invalid");
        }
        claim = {
          ...claim,
          status: "completed",
          completedAtUnixMs: transitionTimestamp(
            now,
            claim.gate3EnteredAtUnixMs,
          ),
          compiledReportSha256: reportSha256,
        };
        validateV2Claim(claim, common);
        await durableReplaceClaim(claimDirectory, claimPath, claim);
        return { claim, output: claimPath };
      }
      if (command === "completion") {
        await assertReleaseLockHeld();
        if (claim.status !== "completed") {
          throw new Error("exact active-run claim is not completed");
        }
        return {
          claim,
          output: await writeCompletionRecord(claim),
        };
      }
      throw new Error("active-run claim command is invalid");
    },
  };
}

export function completionRecord({
  claim,
  claimBytes,
  compiledReportSha256,
  common,
  completedAtUnixMs,
}) {
  return {
    schema: completionSchema,
    version: 1,
    status: "completed",
    completedAtUnixMs,
    activeClaimSha256: sha256(claimBytes),
    compiledReportSha256,
    productSnapshot: common.productSnapshot,
    sourceCommit: common.gitCommit,
    productSnapshotNarHash: common.snapshotNarHash,
    productSnapshotNarSize: common.snapshotNarSize,
    snapshotRunnerSha256: common.snapshotRunnerSha256,
    runtimeOutputManifestSha256: common.runtimeManifestSha256,
  };
}
