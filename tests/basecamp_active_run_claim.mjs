#!/usr/bin/env node

import { createHash } from "node:crypto";
import {
  chmod,
  link,
  lstat,
  mkdir,
  open,
  readFile,
  realpath,
  rename,
  unlink,
} from "node:fs/promises";
import { basename, dirname, join, resolve } from "node:path";

const releaseProgramId =
  "e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61";
const releaseRootId =
  "12ff117a38d756f132cf616cea36fa007653c3c99727c1b475503caa345cbf2a";
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
] =
  process.argv.slice(2);
if (
  !["acquire", "verify", "complete", "state", "completion"].includes(command)
  || !runArgument
  || !snapshotArgument
  || !gcRootArgument
  || !sourceCommitArgument
  || !snapshotNarHashArgument
  || !snapshotNarSizeArgument
  || !snapshotRunnerSha256Argument
  || !runtimeManifestArgument
  || !runtimeManifestSha256Argument
) {
  throw new Error(
    "usage: node tests/basecamp_active_run_claim.mjs <acquire|verify|complete|state|completion> <run-dir> <snapshot> <gc-root> <git-commit> <snapshot-nar-hash> <snapshot-nar-size> <snapshot-runner-sha256> <runtime-manifest> <runtime-manifest-sha256>",
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

const uid = process.getuid?.();
if (!Number.isSafeInteger(uid) || uid < 0) {
  throw new Error("active-run claim requires a numeric Unix UID");
}

async function canonicalDirectory(path, mode) {
  const canonical = await realpath(path);
  const metadata = await lstat(path);
  if (
    canonical !== path
    || metadata.isSymbolicLink()
    || !metadata.isDirectory()
    || metadata.uid !== uid
    || (metadata.mode & 0o777) !== mode
  ) {
    throw new Error(`${path} is not a canonical owner directory`);
  }
  return canonical;
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
await canonicalDirectory(claimDirectory, 0o700);

const runDirectory = resolve(runArgument);
await canonicalDirectory(runDirectory, 0o700);
const runtimeManifestPath = resolve(runtimeManifestArgument);
const runtimeManifestMetadata = await lstat(runtimeManifestPath);
const runtimeManifestBytes = await readFile(runtimeManifestPath);
if (
  dirname(runtimeManifestPath) !== runDirectory
  || basename(runtimeManifestPath) !== "runtime-output-manifest.json"
  || runtimeManifestMetadata.isSymbolicLink()
  || !runtimeManifestMetadata.isFile()
  || runtimeManifestMetadata.uid !== uid
  || (runtimeManifestMetadata.mode & 0o777) !== 0o600
  || runtimeManifestMetadata.size <= 0
  || runtimeManifestMetadata.size > 128 * 1024
  || await realpath(runtimeManifestPath) !== runtimeManifestPath
  || createHash("sha256").update(runtimeManifestBytes).digest("hex")
    !== runtimeManifestSha256Argument
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
const gcRootPath = resolve(gcRootArgument);
const gcRootsParent = join(claimDirectory, "gc-roots");
await canonicalDirectory(gcRootsParent, 0o700);
const runRootsDirectory = join(gcRootsParent, basename(runDirectory));
await canonicalDirectory(runRootsDirectory, 0o700);
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
  version: 1,
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
};

async function syncDirectory(path) {
  const handle = await open(path, "r");
  try {
    await handle.sync();
  } finally {
    await handle.close();
  }
}

async function writeNewClaim(value) {
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

async function readClaim() {
  const metadata = await lstat(claimPath);
  if (
    metadata.isSymbolicLink()
    || !metadata.isFile()
    || metadata.uid !== uid
    || (metadata.mode & 0o777) !== 0o600
    || metadata.size <= 0
    || metadata.size > 64 * 1024
    || await realpath(claimPath) !== claimPath
  ) {
    throw new Error("persistent active-run claim is not a secure regular file");
  }
  let parsed;
  try {
    parsed = JSON.parse(await readFile(claimPath, "utf8"));
  } catch {
    throw new Error("persistent active-run claim is not valid JSON");
  }
  for (const [field, value] of Object.entries(common)) {
    if (parsed?.[field] !== value) {
      if (field === "runDirectory" && typeof parsed?.runDirectory === "string") {
        throw new Error(
          `another active run owns this release; resume exact: ${parsed.runDirectory}`,
        );
      }
      throw new Error(`persistent active-run claim differs at ${field}`);
    }
  }
  if (
    !["active", "completed"].includes(parsed.status)
    || !Number.isSafeInteger(parsed.createdAtUnixMs)
    || parsed.createdAtUnixMs <= 0
    || (
      parsed.status === "active"
      && (
        Object.hasOwn(parsed, "completedAtUnixMs")
        || Object.hasOwn(parsed, "compiledReportSha256")
      )
    )
    || (
      parsed.status === "completed"
      && (
        !Number.isSafeInteger(parsed.completedAtUnixMs)
        || parsed.completedAtUnixMs < parsed.createdAtUnixMs
        || !/^[0-9a-f]{64}$/.test(parsed.compiledReportSha256)
      )
    )
  ) {
    throw new Error("persistent active-run claim state is invalid");
  }
  return parsed;
}

async function durableReplaceClaim(value) {
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

async function compiledReportSha256() {
  const compiledReportPath = join(
    runDirectory,
    "compiled-mvp-report.json",
  );
  const metadata = await lstat(compiledReportPath);
  const bytes = await readFile(compiledReportPath);
  let parsed;
  try {
    parsed = JSON.parse(bytes);
  } catch {
    throw new Error("completed claim report is not valid JSON");
  }
  if (
    metadata.isSymbolicLink()
    || !metadata.isFile()
    || metadata.uid !== uid
    || (metadata.mode & 0o777) !== 0o600
    || metadata.size <= 0
    || metadata.size > 4 * 1024 * 1024
    || await realpath(compiledReportPath) !== compiledReportPath
    || parsed?.schema !== "logos.palace.basecamp-mvp-compiled-report"
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
  return createHash("sha256").update(bytes).digest("hex");
}

async function writeCompletionRecord(claim) {
  const currentCompiledSha256 = await compiledReportSha256();
  if (currentCompiledSha256 !== claim.compiledReportSha256) {
    throw new Error("completed claim report digest changed");
  }
  const claimBytes = await readFile(claimPath);
  const completionPath = join(
    runDirectory,
    "active-claim-completion.json",
  );
  const temporary = join(
    runDirectory,
    `.active-claim-completion-${process.pid}-${Date.now()}.tmp`,
  );
  const record = {
    schema: "logos.palace.basecamp-active-run-completion",
    version: 1,
    status: "completed",
    completedAtUnixMs: claim.completedAtUnixMs,
    activeClaimSha256:
      createHash("sha256").update(claimBytes).digest("hex"),
    compiledReportSha256: claim.compiledReportSha256,
    productSnapshot,
    sourceCommit: sourceCommitArgument,
    productSnapshotNarHash: snapshotNarHashArgument,
    productSnapshotNarSize: snapshotNarSize,
    snapshotRunnerSha256: snapshotRunnerSha256Argument,
    runtimeOutputManifestSha256: runtimeManifestSha256Argument,
  };
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

let claim;
try {
  claim = await readClaim();
} catch (error) {
  if (error?.code !== "ENOENT") throw error;
}

if (command === "acquire") {
  if (!claim) {
    claim = {
      ...common,
      status: "active",
      createdAtUnixMs: Date.now(),
    };
    try {
      await writeNewClaim(claim);
    } catch (error) {
      if (error?.code !== "EEXIST") throw error;
      claim = await readClaim();
    }
  }
} else if (command === "verify" || command === "complete") {
  if (!claim || claim.status !== "active") {
    throw new Error("exact active-run claim is not active");
  }
  if (command === "complete") {
    const reportSha256 = await compiledReportSha256();
    claim = {
      ...claim,
      status: "completed",
      completedAtUnixMs: Date.now(),
      compiledReportSha256: reportSha256,
    };
    await durableReplaceClaim(claim);
  }
} else if (!claim) {
  throw new Error("exact active-run claim does not exist");
}

if (command === "state") {
  process.stdout.write(`${claim.status}\n`);
} else if (command === "completion") {
  if (claim.status !== "completed") {
    throw new Error("exact active-run claim is not completed");
  }
  process.stdout.write(`${await writeCompletionRecord(claim)}\n`);
} else {
  process.stdout.write(`${claimPath}\n`);
}
