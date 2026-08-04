#!/usr/bin/env node

import {
  access,
  chmod,
  constants,
  lstat,
  mkdir,
  open,
  readFile,
  readdir,
  realpath,
  rename,
  unlink,
} from "node:fs/promises";
import { basename, dirname, join, resolve } from "node:path";
import {
  completionRecord,
  createClaimLifecycle,
  releaseProgramId,
  releaseRootId,
  sha256,
  validateProcessScopeNames,
} from "./basecamp_claim_lifecycle.mjs";
import {
  legacyClaimBoundProcesses,
} from "./basecamp_active_scope_preflight.mjs";
import { cgroupProcesses } from "./basecamp_owned_processes.mjs";
import { verifyReleaseLock } from "./basecamp_release_lock.mjs";
import { retireScopeSlice } from "./basecamp_scope.mjs";

const [
  command,
  runArgument,
  snapshotArgument,
  gcRootArgument,
  sourceCommitArgument,
  snapshotNarHashArgument,
  snapshotNarSizeArgument,
  snapshotRunnerSha256Argument,
  runtimeManifestArgument,
  runtimeManifestSha256Argument,
  processScopeSliceArgument,
  processScopePrefixArgument,
] = process.argv.slice(2);
const commands = new Set([
  "acquire-or-roll-forward",
  "verify",
  "enter-gate3",
  "complete",
  "state",
  "retired-runs",
  "completion",
]);
if (
  !commands.has(command)
  || !runArgument
  || !snapshotArgument
  || !gcRootArgument
  || !sourceCommitArgument
  || !snapshotNarHashArgument
  || !snapshotNarSizeArgument
  || !snapshotRunnerSha256Argument
  || !runtimeManifestArgument
  || !runtimeManifestSha256Argument
  || !processScopeSliceArgument
  || !processScopePrefixArgument
) {
  throw new Error(
    "usage: node tests/basecamp_active_run_claim.mjs "
    + "<acquire-or-roll-forward|verify|enter-gate3|complete|state|retired-runs|completion> "
    + "<run-dir> <snapshot> <gc-root> <git-commit> <snapshot-nar-hash> "
    + "<snapshot-nar-size> <snapshot-runner-sha256> <runtime-manifest> "
    + "<runtime-manifest-sha256> <process-scope-slice> <process-scope-prefix>",
  );
}

const snapshotNarSize = Number(snapshotNarSizeArgument);
if (
  !/^[0-9a-f]{40}$/.test(sourceCommitArgument)
  || !/^sha256-[A-Za-z0-9+/]{43}=$/.test(snapshotNarHashArgument)
  || !Number.isSafeInteger(snapshotNarSize)
  || snapshotNarSize <= 0
  || snapshotNarSize > 64 * 1024 * 1024
  || !/^[0-9a-f]{64}$/.test(snapshotRunnerSha256Argument)
  || !/^[0-9a-f]{64}$/.test(runtimeManifestSha256Argument)
) {
  throw new Error("active-run immutable source identity is invalid");
}
validateProcessScopeNames(
  processScopeSliceArgument,
  processScopePrefixArgument,
);

const uid = process.getuid?.();
if (!Number.isSafeInteger(uid) || uid < 0) {
  throw new Error("active-run claim requires a numeric Unix UID");
}

async function canonicalOwnerDirectory(path, mode) {
  const metadata = await lstat(path);
  if (
    metadata.isSymbolicLink()
    || !metadata.isDirectory()
    || metadata.uid !== uid
    || (metadata.mode & 0o777) !== mode
    || await realpath(path) !== path
  ) {
    throw new Error(`${path} is not a canonical owner directory`);
  }
  return path;
}

async function secureFile(path, maximum, description) {
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

async function syncDirectory(path) {
  const handle = await open(path, "r");
  try {
    await handle.sync();
  } finally {
    await handle.close();
  }
}

const varTmp = "/var/tmp";
if (await realpath(varTmp) !== varTmp || !(await lstat(varTmp)).isDirectory()) {
  throw new Error("/var/tmp is not a canonical directory");
}
const claimDirectory = `${varTmp}/logos-palace-${uid}`;
let claimDirectoryCreated = false;
try {
  await mkdir(claimDirectory, { mode: 0o700 });
  claimDirectoryCreated = true;
} catch (error) {
  if (error?.code !== "EEXIST") throw error;
}
if (claimDirectoryCreated) await chmod(claimDirectory, 0o700);
await canonicalOwnerDirectory(claimDirectory, 0o700);

const runDirectory = resolve(runArgument);
await canonicalOwnerDirectory(runDirectory, 0o700);
const runtimeManifestPath = resolve(runtimeManifestArgument);
const runtimeManifestBytes = await secureFile(
  runtimeManifestPath,
  128 * 1024,
  "active-run runtime manifest",
);
if (
  dirname(runtimeManifestPath) !== runDirectory
  || basename(runtimeManifestPath) !== "runtime-output-manifest.json"
  || sha256(runtimeManifestBytes) !== runtimeManifestSha256Argument
) {
  throw new Error("active-run runtime manifest is invalid");
}

const productSnapshot = resolve(snapshotArgument);
const snapshotMetadata = await lstat(productSnapshot);
if (
  snapshotMetadata.isSymbolicLink()
  || !snapshotMetadata.isDirectory()
  || await realpath(productSnapshot) !== productSnapshot
  || !/^\/nix\/store\/[0-9abcdfghijklmnpqrsvwxyz]{32}-[^/]+$/.test(
    productSnapshot,
  )
) {
  throw new Error("active-run product snapshot is not one immutable store path");
}
const snapshotRunner = join(productSnapshot, "scripts/run-basecamp-mvp.sh");
const snapshotRunnerMetadata = await lstat(snapshotRunner);
const snapshotRunnerBytes = await readFile(snapshotRunner);
if (
  snapshotRunnerMetadata.isSymbolicLink()
  || !snapshotRunnerMetadata.isFile()
  || snapshotRunnerMetadata.size <= 0
  || snapshotRunnerMetadata.size > 1024 * 1024
  || sha256(snapshotRunnerBytes) !== snapshotRunnerSha256Argument
) {
  throw new Error("active-run immutable runner identity is invalid");
}

const gcRootPath = resolve(gcRootArgument);
const gcRootsParent = join(claimDirectory, "gc-roots");
await canonicalOwnerDirectory(gcRootsParent, 0o700);
const runRootsDirectory = join(gcRootsParent, basename(runDirectory));
await canonicalOwnerDirectory(runRootsDirectory, 0o700);
const gcRootMetadata = await lstat(gcRootPath);
if (
  dirname(gcRootPath) !== runRootsDirectory
  || basename(gcRootPath) !== "product-snapshot"
  || !gcRootMetadata.isSymbolicLink()
  || await realpath(gcRootPath) !== productSnapshot
) {
  throw new Error("active-run GC root does not retain the exact snapshot");
}

const claimPath = join(
  claimDirectory,
  `active-${releaseProgramId}-${releaseRootId}.json`,
);
const common = {
  schema: "logos.palace.basecamp-active-run-claim",
  version: 2,
  uid,
  releaseProgramId,
  releaseRootId,
  runDirectory,
  productSnapshot,
  gcRootPath,
  gcRootTarget: productSnapshot,
  gitCommit: sourceCommitArgument,
  snapshotNarHash: snapshotNarHashArgument,
  snapshotNarSize,
  snapshotRunnerSha256: snapshotRunnerSha256Argument,
  runtimeManifestPath,
  runtimeManifestSha256: runtimeManifestSha256Argument,
  processScopeSlice: processScopeSliceArgument,
  processScopePrefix: processScopePrefixArgument,
};

async function assertReleaseLockHeld() {
  const expected = join(
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
    || process.env.PALACE_MVP_LOCK_PATH !== expected
    || !Number.isSafeInteger(supervisorPid)
    || supervisorPid <= 1
    || !Number.isSafeInteger(supervisorStartTimeTicks)
    || supervisorStartTimeTicks <= 0
  ) {
    throw new Error("claim roll-forward requires exact release lock state");
  }

  const flock = join(dirname(process.argv0), "flock");
  await access(flock, constants.X_OK);
  await verifyReleaseLock({
    lockPath: expected,
    flock,
    supervisorPid,
    supervisorStartTimeTicks,
    snapshotRunner,
    runsRoot: dirname(runDirectory),
    runDirectory,
  });
}

async function findCgroupSlice(slice) {
  const root = "/sys/fs/cgroup";
  const queue = [{ path: root, depth: 0 }];
  const matches = [];
  let visited = 0;
  while (queue.length > 0) {
    const current = queue.shift();
    if (current.depth > 16) {
      throw new Error("process cgroup hierarchy exceeds depth bound");
    }
    const entries = await readdir(current.path, { withFileTypes: true });
    visited += entries.length;
    if (visited > 65_536) {
      throw new Error("process cgroup hierarchy exceeds entry bound");
    }
    for (const entry of entries) {
      if (!entry.isDirectory()) continue;
      const child = join(current.path, entry.name);
      if (entry.name === slice) {
        const controlGroup = child.slice(root.length);
        if (!controlGroup.startsWith("/")) {
          throw new Error("recorded process slice escaped cgroup root");
        }
        matches.push(controlGroup);
      }
      queue.push({ path: child, depth: current.depth + 1 });
    }
  }
  if (matches.length > 1) {
    throw new Error("recorded process slice is ambiguous");
  }
  return matches[0];
}

async function scanClaimBoundProcesses(input) {
  if (input.legacy) {
    return legacyClaimBoundProcesses({
      claimPath: input.claimPath,
      uid,
    });
  }
  const cgroupPath = await findCgroupSlice(input.processScopeSlice);
  if (!cgroupPath) return [];
  return cgroupProcesses({
    cgroupPath,
  });
}

async function retirePredecessorScope(input) {
  if (input.legacy) return;
  const systemctl = join(dirname(process.argv0), "systemctl");
  await access(systemctl, constants.X_OK);
  await retireScopeSlice({
    slice: input.processScopeSlice,
    systemctl,
  });
}

async function validateImmutableSnapshot(predecessor) {
  const snapshot = predecessor.productSnapshot;
  const metadata = await lstat(snapshot);
  if (
    metadata.isSymbolicLink()
    || !metadata.isDirectory()
    || await realpath(snapshot) !== snapshot
    || !/^\/nix\/store\/[0-9abcdfghijklmnpqrsvwxyz]{32}-[^/]+$/.test(
      snapshot,
    )
  ) {
    throw new Error("predecessor snapshot is not immutable");
  }
  const runner = join(snapshot, "scripts/run-basecamp-mvp.sh");
  const runnerMetadata = await lstat(runner);
  const runnerBytes = await readFile(runner);
  if (
    runnerMetadata.isSymbolicLink()
    || !runnerMetadata.isFile()
    || runnerMetadata.size <= 0
    || runnerMetadata.size > 1024 * 1024
    || sha256(runnerBytes) !== predecessor.snapshotRunnerSha256
  ) {
    throw new Error("predecessor immutable runner differs");
  }
}

async function completedReportSha256() {
  const compiledReportPath = join(runDirectory, "compiled-mvp-report.json");
  const bytes = await secureFile(
    compiledReportPath,
    4 * 1024 * 1024,
    "completed claim report",
  );
  let parsed;
  try {
    parsed = JSON.parse(bytes);
  } catch {
    throw new Error("completed claim report is not valid JSON");
  }
  if (
    parsed?.schema !== "logos.palace.basecamp-mvp-compiled-report"
    || parsed?.version !== 1
    || parsed?.status !== "passed"
    || parsed?.fullMvp !== "passed"
    || parsed?.productSnapshot !== productSnapshot
    || parsed?.sourceCommit !== sourceCommitArgument
    || parsed?.productSnapshotNarHash !== snapshotNarHashArgument
    || parsed?.productSnapshotNarSize !== snapshotNarSize
    || parsed?.snapshotRunnerSha256 !== snapshotRunnerSha256Argument
    || parsed?.runtimeOutputs?.manifest !== "runtime-output-manifest.json"
    || parsed?.runtimeOutputs?.manifestSha256
      !== runtimeManifestSha256Argument
    || parsed?.sharedState?.activeRunClaim !== claimPath
    || parsed?.sharedState?.productSnapshotGcRoot !== gcRootPath
  ) {
    throw new Error("completed claim report identity is invalid");
  }
  return sha256(bytes);
}

async function writeCompletionRecord(claim) {
  const currentCompiledSha256 = await completedReportSha256();
  if (currentCompiledSha256 !== claim.compiledReportSha256) {
    throw new Error("completed claim report digest changed");
  }
  const claimBytes = await secureFile(
    claimPath,
    64 * 1024,
    "completed active-run claim",
  );
  const completionPath = join(
    runDirectory,
    "active-claim-completion.json",
  );
  const temporary = join(
    runDirectory,
    `.active-claim-completion-${process.pid}-${Date.now()}.tmp`,
  );
  const record = completionRecord({
    claim,
    claimBytes,
    compiledReportSha256: currentCompiledSha256,
    common,
    completedAtUnixMs: claim.completedAtUnixMs,
  });
  let handle;
  try {
    handle = await open(temporary, "wx", 0o600);
    await handle.writeFile(`${JSON.stringify(record, null, 2)}\n`, "utf8");
    await handle.sync();
    await handle.close();
    handle = undefined;
    await rename(temporary, completionPath);
    await syncDirectory(runDirectory);
  } catch (error) {
    await handle?.close().catch(() => {});
    await unlink(temporary).catch((unlinkError) => {
      if (unlinkError?.code !== "ENOENT") throw unlinkError;
    });
    throw error;
  }
  return completionPath;
}

const lifecycle = createClaimLifecycle({
  uid,
  claimDirectory,
  claimPath,
  common,
  assertReleaseLockHeld,
  scanClaimBoundProcesses,
  retirePredecessorScope,
  validateImmutableSnapshot,
  completedReportSha256,
  writeCompletionRecord,
});
const result = await lifecycle.execute(command);
process.stdout.write(`${result.output}\n`);
