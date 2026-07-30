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
const identityRegistrationAndIdleStorageProfile =
  "identity-registration-and-idle-storage-before-palace-write";

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

// Each entry is an exact recovery audit for one immutable Gate 3 run. Its
// report must prove the run stopped before its first public write.
export const auditedPrePublicWriteGate3Failures = Object.freeze([
  Object.freeze({
    gitCommit: "6866fe090a9e3b77625869606704bb6398d589a2",
    snapshotNarHash:
      "sha256-yVIq0V3ALbozFlZR4uNnYGuWnhXeeZnetsapy0jp9b8=",
    snapshotNarSize: 7_078_968,
    snapshotRunnerSha256:
      "7241b93c58f896958be425736b8918b676b9b6e0f0e68c5c34dbe017a0cd3fa9",
    runtimeManifestSha256:
      "debdf168bdb8e0e5de7b1750207eebd990db262d4ac5ad33836745ac235fad1c",
    compiledReportSha256:
      "c063493c5e101835a437eae85daa5afb29050fdf2d23a70cbe74d08323b398cb",
    gate3ReportSha256:
      "4fac76d54d2a69758b30473fd0fde42287961f4b8103ad964e6d8cef777a14e6",
    gate3Failure: "production LEZ a: rejected=lez-network-fingerprint",
    retirementStatus: "audited-fingerprint-rejection",
  }),
  Object.freeze({
    gitCommit: "39aeee81a23cd183d5cb821cdd0e20dcc836e9ba",
    snapshotNarHash:
      "sha256-u/3D3yuCmDaKoODNtagwaYC+0FGtX5WsvofWef7ZY8o=",
    snapshotNarSize: 7_101_152,
    snapshotRunnerSha256:
      "7241b93c58f896958be425736b8918b676b9b6e0f0e68c5c34dbe017a0cd3fa9",
    runtimeManifestSha256:
      "ecdbf4f480444ca6ced110c197e56c361341f46adef71b054c47c1d3e0b5ea72",
    compiledReportSha256:
      "3420ffd04626d0c6c1a709c5dc15882386ebd494e06634a65af7eaa496ab1a63",
    gate3ReportSha256:
      "91a3c4e96fba72d4c703ae6484b1cee19479af8e82c969fd645f53f0c06aae88",
    gate3Failure:
      "worker a: gate4StartLez receipt timeout: before=\"\" after=\"\" sequence=0->0",
    retirementStatus: "audited-pre-public-write-failure",
  }),
  Object.freeze({
    gitCommit: "2f18a99f9e48b8ce84b81ccdc3864e7a738634fe",
    snapshotNarHash:
      "sha256-4nZ7s5+fwYKqkivxsz4VNUqsZkV8lKdCnfRIyQUlSeg=",
    snapshotNarSize: 7_149_904,
    snapshotRunnerSha256:
      "264ac08a6cb5d5709eb85e90c9947107ba575284100863c96172d20b170cdba0",
    runtimeManifestSha256:
      "5673105501814c2389f03de87c2811cc773d7817ebaa580fcf5b4e3ce114a808",
    compiledReportSha256:
      "7fd09f10e24ba82a52eaefb72e6cf3ef762ab1ea7db4347dc5497b279c2b46fe",
    gate3ReportSha256:
      "4eccb0a1934e2d0c8fc18138155e3f1bf72bac92ff97c15f98c476a3318fa687",
    gate3Failure: "production LEZ a: ",
    retirementStatus: "audited-pre-public-write-failure",
  }),
  Object.freeze({
    gitCommit: "51375a2a136611f5c8b9dc490619ff7f6154babf",
    snapshotNarHash:
      "sha256-NVv+KyANhwtF1wk/oQ4nKBB+VVGVLCelNL30HP5Sbio=",
    snapshotNarSize: 7_152_184,
    snapshotRunnerSha256:
      "264ac08a6cb5d5709eb85e90c9947107ba575284100863c96172d20b170cdba0",
    runtimeManifestSha256:
      "5673105501814c2389f03de87c2811cc773d7817ebaa580fcf5b4e3ce114a808",
    compiledReportSha256:
      "a2a25bcdd6864ef638979f255d88f4784982365945faaf811a97cc27c50bc3a0",
    gate3ReportSha256:
      "18aae314e77ce1408c198b1cb78988399a276c3fdd62924f7d6bdf881df031df",
    gate3Failure:
      "worker a: gate4StartLez receipt timeout: before=\"\" after=\"\" sequence=0->1",
    retirementStatus: "audited-pre-public-write-failure",
  }),
  Object.freeze({
    gitCommit: "bc0fdf742d695d3750e30f7bd76125c0b9ba4b87",
    snapshotNarHash:
      "sha256-XrqEsotCFYBxsruXJUcc6EYWUZ4PweC0pXLmVun+T9E=",
    snapshotNarSize: 7_155_584,
    snapshotRunnerSha256:
      "264ac08a6cb5d5709eb85e90c9947107ba575284100863c96172d20b170cdba0",
    runtimeManifestSha256:
      "5673105501814c2389f03de87c2811cc773d7817ebaa580fcf5b4e3ce114a808",
    compiledReportSha256:
      "50c413d01dd965ad5a5e8cee0612494ec93f1acb1181fd53c87734e0d3db7da0",
    gate3ReportSha256:
      "6ff81a0b4ff1ec18e102051fd113387216aac211d8cf985523fcd422df247631",
    gate3Failure: "worker a: asset picker opened multiple dialogs",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndIdleStorageProfile,
  }),
  Object.freeze({
    gitCommit: "49d4aa58d08611417def4743ef62c0a4befa3f65",
    snapshotNarHash:
      "sha256-Gvf0HlnoEf+RYF6LAQJgE1y9OEzXNOBw/TU9NU8PYNg=",
    snapshotNarSize: 7_170_928,
    snapshotRunnerSha256:
      "264ac08a6cb5d5709eb85e90c9947107ba575284100863c96172d20b170cdba0",
    runtimeManifestSha256:
      "5673105501814c2389f03de87c2811cc773d7817ebaa580fcf5b4e3ce114a808",
    compiledReportSha256:
      "f6ce2bca3b69306f6b87f54b62d9212bbc2cbef279b5edbf7b05dd74c4adb161",
    gate3ReportSha256:
      "0aac75f279b12fcc9b183d9094a08295dae4845dea83dd35b377ec7b1bcd5e8c",
    gate3Failure: "worker a: asset picker import did not complete",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndIdleStorageProfile,
  }),
]);

function validPrePublicWriteAudit(audit) {
  const hasProfile = audit !== null
    && typeof audit === "object"
    && Object.hasOwn(audit, "reportProfile");
  return exactKeys(audit, [
      "compiledReportSha256",
      "gate3ReportSha256",
      "gate3Failure",
      "gitCommit",
      ...(hasProfile ? ["reportProfile"] : []),
      "retirementStatus",
      "runtimeManifestSha256",
    "snapshotNarHash",
    "snapshotNarSize",
    "snapshotRunnerSha256",
  ])
    && sourceCommitPattern.test(audit.gitCommit)
    && narHashPattern.test(audit.snapshotNarHash)
    && Number.isSafeInteger(audit.snapshotNarSize)
    && audit.snapshotNarSize > 0
    && sha256Pattern.test(audit.snapshotRunnerSha256)
    && sha256Pattern.test(audit.runtimeManifestSha256)
    && sha256Pattern.test(audit.compiledReportSha256)
    && sha256Pattern.test(audit.gate3ReportSha256)
    && typeof audit.gate3Failure === "string"
    && audit.gate3Failure.length > 0
    && audit.gate3Failure.length <= 1024
    && (!hasProfile
      || audit.reportProfile === identityRegistrationAndIdleStorageProfile)
    && [
      "audited-fingerprint-rejection",
      "audited-pre-public-write-failure",
    ].includes(audit.retirementStatus);
}

function validPrePublicWriteAudits(audits) {
  if (
    !Array.isArray(audits)
    || audits.length === 0
    || audits.length > 8
    || audits.some((audit) => !validPrePublicWriteAudit(audit))
  ) {
    return false;
  }
  const identities = audits.map((audit) => [
    audit.gitCommit,
    audit.snapshotNarHash,
    audit.snapshotNarSize,
    audit.snapshotRunnerSha256,
    audit.runtimeManifestSha256,
  ].join(":"));
  return new Set(identities).size === identities.length;
}

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

function validateFailedReport(
  report,
  predecessor,
  allowedFailurePhases,
  description,
) {
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
    || !allowedFailurePhases.has(report.failure.phase)
    || typeof report.failure.message !== "string"
    || report.failure.message.length <= 0
    || report.failure.message.length > 1024
    || !exactKeys(report.gates, implementedGates)
  ) {
    throw new Error(`${description} failed compiled report is invalid`);
  }
  for (const gate of implementedGates) {
    if (
      !exactKeys(report.gates[gate], ["status", "report"])
      || report.gates[gate].status !== "unknown"
      || report.gates[gate].report !== expectedGateReports[gate]
    ) {
      throw new Error(`${description} failed compiled gate state is invalid`);
    }
  }
  return report.failure.phase;
}

function validatePreGate3FailedReport(report, predecessor) {
  return validateFailedReport(
    report,
    predecessor,
    preGate3Phases,
    "pre-Gate 3",
  );
}

function validateAuditedGate3FailedReport(report, predecessor) {
  return validateFailedReport(
    report,
    predecessor,
    new Set(["gate3"]),
    "audited Gate 3",
  );
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
  const failurePhase = validatePreGate3FailedReport(
    compiled.value,
    predecessor,
  );

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

function matchesAuditedPrePublicWriteFailure(predecessor, audit) {
  return predecessor.version === 2
    && predecessor.gitCommit === audit.gitCommit
    && predecessor.snapshotNarHash === audit.snapshotNarHash
    && predecessor.snapshotNarSize === audit.snapshotNarSize
    && predecessor.snapshotRunnerSha256 === audit.snapshotRunnerSha256
    && predecessor.runtimeManifestSha256 === audit.runtimeManifestSha256;
}

function receiptFields(receipt) {
  const fields = Object.create(null);
  for (const field of String(receipt).split(";")) {
    const separator = field.indexOf("=");
    if (separator > 0) {
      fields[field.slice(0, separator)] = field.slice(separator + 1);
    }
  }
  return fields;
}

function validIdentityRegistration(record, display) {
  if (
    !exactKeys(record, [
      "accountId",
      "deliveryKey",
      "display",
      "existing",
      "keyEpoch",
      "receipt",
      "registrationTransaction",
    ])
    || record.display !== display
    || record.existing !== false
    || record.keyEpoch !== "1"
    || !sha256Pattern.test(record.accountId)
    || !sha256Pattern.test(record.deliveryKey)
    || !sha256Pattern.test(record.registrationTransaction)
  ) {
    return false;
  }
  const fields = receiptFields(record.receipt);
  return fields.identity === record.accountId
    && fields.display === display
    && fields.delivery_key === record.deliveryKey
    && fields.key_epoch === "1"
    && fields.registration === "submitted"
    && fields.registration_ready === "1"
    && fields.registration_tx === record.registrationTransaction;
}

function validCurrentNoAuthorityLez(startup) {
  if (
    !exactKeys(startup, ["basecampPid", "lez", "startupMs"])
    || !Number.isSafeInteger(startup.basecampPid)
    || startup.basecampPid <= 1
    || !Number.isSafeInteger(startup.startupMs)
    || startup.startupMs < 0
    || !exactKeys(startup.lez, ["elapsedMs", "lezStateObservation", "receipt"])
    || startup.lez.receipt !== ""
    || !Number.isSafeInteger(startup.lez.elapsedMs)
    || startup.lez.elapsedMs < 0
    || !exactKeys(startup.lez.lezStateObservation, ["receipt", "source"])
    || startup.lez.lezStateObservation.source !== "gate4LezState"
  ) {
    return false;
  }
  const fields = receiptFields(startup.lez.lezStateObservation.receipt);
  return fields.ready === "1"
    && fields.compatible === "1"
    && fields.running === "1"
    && fields.tracked === "0"
    && fields.sync === "current"
    && fields.current_height === fields.synced_height
    && /^[1-9][0-9]*$/.test(fields.current_height ?? "")
    && fields.authority === "missing"
    && fields.vm === "idle"
    && fields.vm_action === "none"
    && fields.program === releaseProgramId;
}

function validIdleStorageStatus(status, expectedState) {
  if (
    !exactKeys(status, ["elapsedMs", "receipt"])
    || !Number.isSafeInteger(status.elapsedMs)
    || status.elapsedMs < 0
  ) {
    return false;
  }
  const fields = receiptFields(status.receipt);
  return fields.storage === expectedState
    && fields.pending === "0"
    && fields.callbacks === "0"
    && fields.callback_registration === "ready"
    && fields.reconciliation_required === "0"
    && fields.catalog === "idle"
    && fields.catalog_verified === "0"
    && fields.retention_round === "0"
    && fields.retained === "0";
}

function validIdentityRegistrationAndIdleStorageAssetAuthoring(authoring) {
  return exactKeys(authoring, [
    "assets",
    "boundary",
    "elapsedMs",
    "graphBindings",
    "inputManifest",
    "phase",
    "propStory",
    "selectedAssetCount",
    "version",
  ])
    && authoring.version === 1
    && authoring.phase === "input-validated"
    && Number.isSafeInteger(authoring.selectedAssetCount)
    && authoring.selectedAssetCount > 0
    && typeof authoring.propStory === "string"
    && typeof authoring.boundary === "string"
    && authoring.elapsedMs === 0
    && exactJson(authoring.assets, [])
    && exactJson(authoring.graphBindings, [])
    && exactKeys(authoring.inputManifest, [
      "assetCount",
      "schema",
      "sha256",
      "version",
    ])
    && authoring.inputManifest.schema === "logos.palace.e2e-asset-inputs"
    && authoring.inputManifest.version === 1
    && Number.isSafeInteger(authoring.inputManifest.assetCount)
    && authoring.inputManifest.assetCount > 0
    && sha256Pattern.test(authoring.inputManifest.sha256);
}

function validIdentityRegistrationAndIdleStorageGate3Report(report) {
  return exactKeys(report.identities, ["a", "b", "c"])
    && validIdentityRegistration(report.identities.a, "Alice")
    && validIdentityRegistration(report.identities.b, "Bob")
    && validIdentityRegistration(report.identities.c, "Carol")
    && exactKeys(report.startup, ["a", "b", "c"])
    && validCurrentNoAuthorityLez(report.startup.a)
    && validCurrentNoAuthorityLez(report.startup.b)
    && validCurrentNoAuthorityLez(report.startup.c)
    && exactKeys(report.storageConfigs, ["a", "b", "c"])
    && Object.values(report.storageConfigs).every(
      (config) => typeof config === "string" && config.length > 0,
    )
    && exactKeys(report.storageStartup, ["a", "b"])
    && ["a", "b"].every((label) =>
      exactKeys(report.storageStartup[label], ["running", "start"])
      && validIdleStorageStatus(report.storageStartup[label].start, "starting")
      && validIdleStorageStatus(report.storageStartup[label].running, "running"),
    )
    && validIdentityRegistrationAndIdleStorageAssetAuthoring(report.assetAuthoring);
}

function validatesAuditedPrePublicWriteGate3Report(
  report,
  predecessor,
  audit,
) {
  const identityRegistrationAndIdleStorage =
    audit.reportProfile === identityRegistrationAndIdleStorageProfile;
  if (
    !exactKeys(report, [
      ...(identityRegistrationAndIdleStorage ? ["assetAuthoring"] : []),
      "basecampBinarySha256",
      "basecampRevision",
      "blockers",
      "cleanup",
      "creatorOffline",
      "failure",
      "fullGate3",
      "identities",
      "installedPackages",
      "packageHashes",
      "pngRecovery",
      "productSnapshot",
      "productSnapshotNarHash",
      "productSnapshotNarSize",
      "productionIdentityMode",
      "providerBRetentionProofs",
      "releasePreflight",
      "runtimeOutputManifestSha256",
      "schema",
      "snapshotRunnerSha256",
      "sourceCommit",
      "startup",
      "status",
      "storageConfigs",
      "storageStartup",
      "version",
    ])
    || report.schema !== "logos.palace.basecamp-gate3-report"
    || report.version !== 1
    || report.status !== "failed"
    || report.fullGate3 !== "failed"
    || report.productSnapshot !== predecessor.productSnapshot
    || report.sourceCommit !== predecessor.gitCommit
    || report.productSnapshotNarHash !== predecessor.snapshotNarHash
    || report.productSnapshotNarSize !== predecessor.snapshotNarSize
    || report.snapshotRunnerSha256 !== predecessor.snapshotRunnerSha256
    || report.runtimeOutputManifestSha256
      !== predecessor.runtimeManifestSha256
    || report.productionIdentityMode !== true
    || report.failure !== audit.gate3Failure
    || !exactKeys(report.cleanup, ["failures", "status"])
    || report.cleanup.status !== "passed"
    || !exactJson(report.cleanup.failures, [])
    || !exactJson(report.blockers, [])
    || (
      identityRegistrationAndIdleStorage
        ? !validIdentityRegistrationAndIdleStorageGate3Report(report)
        : (
          !exactKeys(report.identities, [])
          || !exactKeys(report.storageStartup, [])
          || !exactKeys(report.startup, ["a"])
          || !exactKeys(report.startup.a, ["basecampPid", "startupMs"])
          || !Number.isSafeInteger(report.startup.a.basecampPid)
          || report.startup.a.basecampPid <= 1
          || !Number.isSafeInteger(report.startup.a.startupMs)
          || report.startup.a.startupMs < 0
        )
    )
    || !Array.isArray(report.providerBRetentionProofs)
    || report.providerBRetentionProofs.length !== 0
    || report.creatorOffline !== false
    || report.pngRecovery !== "failed"
    || report.releasePreflight?.status !== "passed"
    || report.releasePreflight?.rootAccountBeforeWrites?.status !== "passed"
    || report.releasePreflight.rootAccountBeforeWrites.state
      !== "uninitialized"
  ) {
    throw new Error("audited Gate 3 pre-public-write report is invalid");
  }
}

function prePublicWriteRetirementRecord({
  predecessor,
  compiledReportSha256,
  gate3ReportSha256,
  retirementStatus,
}) {
  return {
    schema: "logos.palace.basecamp-pre-public-write-retirement",
    version: 1,
    status: retirementStatus,
    predecessor: {
      gitCommit: predecessor.gitCommit,
      snapshotNarHash: predecessor.snapshotNarHash,
      snapshotNarSize: predecessor.snapshotNarSize,
      snapshotRunnerSha256: predecessor.snapshotRunnerSha256,
      runtimeManifestSha256: predecessor.runtimeManifestSha256,
      compiledReportSha256,
      gate3ReportSha256,
    },
  };
}

function validatePrePublicWriteRetirementRecord({
  record,
  predecessor,
  audit,
}) {
  if (
    !exactKeys(record, ["predecessor", "schema", "status", "version"])
    || record.schema !== "logos.palace.basecamp-pre-public-write-retirement"
    || record.version !== 1
    || record.status !== audit.retirementStatus
    || !exactKeys(record.predecessor, [
      "compiledReportSha256",
      "gate3ReportSha256",
      "gitCommit",
      "runtimeManifestSha256",
      "snapshotNarHash",
      "snapshotNarSize",
      "snapshotRunnerSha256",
    ])
    || record.predecessor.gitCommit !== predecessor.gitCommit
    || record.predecessor.snapshotNarHash !== predecessor.snapshotNarHash
    || record.predecessor.snapshotNarSize !== predecessor.snapshotNarSize
    || record.predecessor.snapshotRunnerSha256
      !== predecessor.snapshotRunnerSha256
    || record.predecessor.runtimeManifestSha256
      !== predecessor.runtimeManifestSha256
    || record.predecessor.compiledReportSha256
      !== audit.compiledReportSha256
    || record.predecessor.gate3ReportSha256 !== audit.gate3ReportSha256
  ) {
    throw new Error("pre-public-write retirement certificate is invalid");
  }
  return record;
}

async function validateAuditedPrePublicWriteGate3Artifacts({
  predecessor,
  uid,
  audits,
}) {
  const audit = audits.find((candidate) =>
    matchesAuditedPrePublicWriteFailure(predecessor, candidate));
  if (!audit) {
    throw new Error("Gate 3 predecessor is not the audited pre-write failure");
  }
  const run = predecessor.runDirectory;
  await canonicalOwnerDirectory(join(run, "gate3"), uid, 0o700);
  await absent(join(run, "gate4"), "pre-public-write Gate 4 evidence");
  await absent(
    join(run, "active-claim-completion.json"),
    "pre-public-write claim completion",
  );
  await absent(
    join(run, "public-evidence.json"),
    "pre-public-write public evidence",
  );
  await canonicalOwnerDirectory(join(run, "shared-state"), uid, 0o700);

  const [compiled, gate3] = await Promise.all([
    parseSecureJson(
      join(run, "compiled-mvp-report.json"),
      uid,
      4 * 1024 * 1024,
      "audited Gate 3 compiled report",
    ),
    parseSecureJson(
      join(run, "gate3", "gate3-report.json"),
      uid,
      4 * 1024 * 1024,
      "audited Gate 3 report",
    ),
  ]);
  if (
    compiled.sha256 !== audit.compiledReportSha256
    || gate3.sha256 !== audit.gate3ReportSha256
  ) {
    throw new Error("audited Gate 3 report digest differs");
  }
  validateAuditedGate3FailedReport(compiled.value, predecessor);
  validatesAuditedPrePublicWriteGate3Report(
    gate3.value,
    predecessor,
    audit,
  );
  return {
    compiledReportSha256: compiled.sha256,
    gate3ReportSha256: gate3.sha256,
    audit,
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

function prePublicWriteRollForwardRecord({
  predecessor,
  predecessorClaimSha256,
  retiredClaimArchiveSha256,
  common,
  compiledReportSha256,
  gate3ReportSha256,
  retirementCertificateSha256,
  retirementStatus,
}) {
  return {
    schema: rollForwardSchema,
    version: 2,
    status: "retired-pre-public-write",
    predecessor: {
      claimVersion: predecessor.version,
      claimSha256: predecessorClaimSha256,
      compiledReportSha256,
      gate3ReportSha256,
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
      compiledFailure: "audited-pre-public-write",
      gate3ClaimState: "entered",
      gate3Artifacts: retirementStatus,
      gate4Artifacts: "absent",
      sharedState: "local-only-retained",
      completion: "absent",
      publicEvidence: "absent",
      retirementCertificate: "pre-public-write-gate3-retirement.json",
      retirementCertificateSha256,
      retiredClaimArchiveSha256,
    },
  };
}

function validatePreGate3RollForwardRecord({
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

async function validatePrePublicWriteRollForwardRecord({
  record,
  claim,
  archivedClaim,
  archivedClaimSha256,
  uid,
  audits,
}) {
  const audit = audits.find((candidate) =>
    matchesAuditedPrePublicWriteFailure(archivedClaim, candidate));
  const retirementCertificate = record?.proof?.retirementCertificate;
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
    || record.version !== 2
    || record.status !== "retired-pre-public-write"
    || archivedClaim.version !== 2
    || archivedClaim.status !== "gate3-entered"
    || !audit
    || !matchesAuditedPrePublicWriteFailure(archivedClaim, audit)
    || !exactKeys(record.predecessor, [
      "claimVersion",
      "claimSha256",
      "compiledReportSha256",
      "gate3ReportSha256",
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
    || record.predecessor.compiledReportSha256
      !== audit.compiledReportSha256
    || record.predecessor.gate3ReportSha256 !== audit.gate3ReportSha256
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
      "retirementCertificate",
      "retirementCertificateSha256",
      "retiredClaimArchiveSha256",
    ])
    || record.proof.releaseLock !== "held-exclusive"
    || record.proof.claimBoundProcessCount !== 0
    || record.proof.compiledFailure !== "audited-pre-public-write"
    || record.proof.gate3ClaimState !== "entered"
    || record.proof.gate3Artifacts !== audit.retirementStatus
    || record.proof.gate4Artifacts !== "absent"
    || record.proof.sharedState !== "local-only-retained"
    || record.proof.completion !== "absent"
    || record.proof.publicEvidence !== "absent"
    || retirementCertificate !== "pre-public-write-gate3-retirement.json"
    || !sha256Pattern.test(record.proof.retirementCertificateSha256)
    || record.proof.retiredClaimArchiveSha256 !== archivedClaimSha256
  ) {
    throw new Error("pre-public-write roll-forward evidence is invalid");
  }

  const certificatePath = join(
    archivedClaim.runDirectory,
    retirementCertificate,
  );
  if (dirname(certificatePath) !== archivedClaim.runDirectory) {
    throw new Error("pre-public-write retirement certificate path is invalid");
  }
  const certificate = await parseSecureJson(
    certificatePath,
    uid,
    64 * 1024,
    "pre-public-write retirement certificate",
  );
  if (certificate.sha256 !== record.proof.retirementCertificateSha256) {
    throw new Error("pre-public-write retirement certificate digest differs");
  }
  validatePrePublicWriteRetirementRecord({
    record: certificate.value,
    predecessor: archivedClaim,
    audit,
  });
  return record;
}

async function validateRollForwardRecord({
  record,
  claim,
  archivedClaim,
  archivedClaimSha256,
  uid,
  prePublicWriteAudits,
}) {
  if (record?.version === 1) {
    if (
      archivedClaim.version !== 1
      && archivedClaim.status !== "active-pre-gate3"
    ) {
      throw new Error("archived predecessor claim state is invalid");
    }
    return validatePreGate3RollForwardRecord({
      record,
      claim,
      archivedClaim,
      archivedClaimSha256,
    });
  }
  return validatePrePublicWriteRollForwardRecord({
    record,
    claim,
    archivedClaim,
    archivedClaimSha256,
    uid,
    audits: prePublicWriteAudits,
  });
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
  prePublicWriteAudits = auditedPrePublicWriteGate3Failures,
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
    || !validPrePublicWriteAudits(prePublicWriteAudits)
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
    if (archivedClaim.uid !== uid) {
      throw new Error("archived predecessor claim state is invalid");
    }
    await validateRollForwardRecord({
      record: evidence.value,
      claim,
      archivedClaim,
      archivedClaimSha256: archive.sha256,
      uid,
      prePublicWriteAudits,
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
    let auditedPrePublicWriteRecovery = false;
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
      if (predecessor.status === "gate3-entered") {
        auditedPrePublicWriteRecovery = true;
      } else if (predecessor.status !== "active-pre-gate3") {
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
    const proof = auditedPrePublicWriteRecovery
      ? await validateAuditedPrePublicWriteGate3Artifacts({
          predecessor,
          uid,
          audits: prePublicWriteAudits,
        })
      : await validatePreGate3Artifacts({
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

    let retirementCertificateSha256;
    if (auditedPrePublicWriteRecovery) {
      const retirementCertificate = prePublicWriteRetirementRecord({
        predecessor,
        compiledReportSha256: proof.compiledReportSha256,
        gate3ReportSha256: proof.gate3ReportSha256,
        retirementStatus: proof.audit.retirementStatus,
      });
      const retirementCertificateBytes = Buffer.from(
        `${JSON.stringify(retirementCertificate, null, 2)}\n`,
        "utf8",
      );
      const retirementCertificatePath = join(
        predecessor.runDirectory,
        "pre-public-write-gate3-retirement.json",
      );
      await writeExclusiveDurable(
        retirementCertificatePath,
        retirementCertificateBytes,
        uid,
      );
      retirementCertificateSha256 = sha256(retirementCertificateBytes);
    }

    const archivePath = join(
      common.runDirectory,
      "retired-active-claim.json",
    );
    await writeExclusiveDurable(archivePath, existing.bytes, uid);
    const archiveSha256 = sha256(existing.bytes);
    const record = auditedPrePublicWriteRecovery
      ? prePublicWriteRollForwardRecord({
          predecessor,
          predecessorClaimSha256: existing.sha256,
          retiredClaimArchiveSha256: archiveSha256,
          common,
          compiledReportSha256: proof.compiledReportSha256,
          gate3ReportSha256: proof.gate3ReportSha256,
          retirementCertificateSha256,
          retirementStatus: proof.audit.retirementStatus,
        })
      : rollForwardRecord({
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
