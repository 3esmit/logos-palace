#!/usr/bin/env node

import { createHash } from "node:crypto";
import {
  lstat,
  open,
  readFile,
  readdir,
  realpath,
} from "node:fs/promises";
import { createReadStream } from "node:fs";
import { join, relative, resolve, sep } from "node:path";
import { TextDecoder } from "node:util";

const [snapshotArgument, trackedPathsArgument] = process.argv.slice(2);
if (!snapshotArgument || !trackedPathsArgument) {
  throw new Error(
    "usage: node tests/validate_product_snapshot.mjs <snapshot> <tracked-paths-nul>",
  );
}

const maxTrackedManifestBytes = 4 * 1024 * 1024;
const maxFiles = 4096;
const maxTreeBytes = 64 * 1024 * 1024;
const forbiddenComponents = new Set([".git", ".artifacts", "target"]);
const decoder = new TextDecoder("utf-8", { fatal: true });

function compareUtf8(left, right) {
  return Buffer.compare(Buffer.from(left, "utf8"), Buffer.from(right, "utf8"));
}

function validateRelativePath(path) {
  const components = path.split("/");
  if (
    path.length === 0
    || path.startsWith("/")
    || path.includes("\0")
    || components.some(
      (component) =>
        component.length === 0
        || component === "."
        || component === ".."
        || forbiddenComponents.has(component)
        || /^result(?:-[0-9]+)?$/.test(component),
    )
  ) {
    throw new Error(`snapshot contains forbidden path: ${path}`);
  }
}

async function canonicalRegularFile(path, maxBytes) {
  const metadata = await lstat(path);
  if (
    metadata.isSymbolicLink()
    || !metadata.isFile()
    || metadata.size <= 0
    || metadata.size > maxBytes
    || await realpath(path) !== path
  ) {
    throw new Error(`${path} is not one bounded canonical regular file`);
  }
  return metadata;
}

const snapshot = resolve(snapshotArgument);
const snapshotMetadata = await lstat(snapshot);
if (
  snapshotMetadata.isSymbolicLink()
  || !snapshotMetadata.isDirectory()
  || await realpath(snapshot) !== snapshot
  || !/^\/nix\/store\/[0-9abcdfghijklmnpqrsvwxyz]{32}-[^/]+$/.test(
    snapshot,
  )
) {
  throw new Error("product snapshot is not one canonical Nix store directory");
}

const trackedPathsFile = resolve(trackedPathsArgument);
await canonicalRegularFile(trackedPathsFile, maxTrackedManifestBytes);
const encodedTrackedPaths = await readFile(trackedPathsFile);
if (encodedTrackedPaths.at(-1) !== 0) {
  throw new Error("tracked-path manifest is not NUL terminated");
}
const expectedPaths = encodedTrackedPaths
  .subarray(0, encodedTrackedPaths.length - 1)
  .toString("binary")
  .split("\0")
  .map((encoded) => decoder.decode(Buffer.from(encoded, "binary")));
if (
  expectedPaths.length === 0
  || expectedPaths.length > maxFiles
  || new Set(expectedPaths).size !== expectedPaths.length
) {
  throw new Error("tracked-path manifest count or uniqueness is invalid");
}
for (const path of expectedPaths) validateRelativePath(path);
expectedPaths.sort(compareUtf8);

const actualFiles = [];
async function walk(directory) {
  const entries = await readdir(directory, { withFileTypes: true });
  entries.sort((left, right) => compareUtf8(left.name, right.name));
  for (const entry of entries) {
    const absolute = join(directory, entry.name);
    const path = relative(snapshot, absolute).split(sep).join("/");
    validateRelativePath(path);
    const metadata = await lstat(absolute);
    if (metadata.isSymbolicLink()) {
      throw new Error(`snapshot symlink is forbidden: ${path}`);
    }
    if (metadata.isDirectory()) {
      await walk(absolute);
      continue;
    }
    if (!metadata.isFile()) {
      throw new Error(`snapshot special entry is forbidden: ${path}`);
    }
    actualFiles.push({ absolute, metadata, path });
    if (actualFiles.length > maxFiles) {
      throw new Error("snapshot file count exceeds bound");
    }
  }
}
await walk(snapshot);
actualFiles.sort((left, right) => compareUtf8(left.path, right.path));

if (
  actualFiles.length !== expectedPaths.length
  || actualFiles.some((file, index) => file.path !== expectedPaths[index])
) {
  throw new Error("snapshot files differ from exact Git tracked-file set");
}

let totalBytes = 0;
const treeHash = createHash("sha256");
for (const file of actualFiles) {
  if (!Number.isSafeInteger(file.metadata.size) || file.metadata.size < 0) {
    throw new Error(`snapshot file size is invalid: ${file.path}`);
  }
  totalBytes += file.metadata.size;
  if (!Number.isSafeInteger(totalBytes) || totalBytes > maxTreeBytes) {
    throw new Error("snapshot tracked bytes exceed bound");
  }

  const fileHash = createHash("sha256");
  for await (const chunk of createReadStream(file.absolute)) {
    fileHash.update(chunk);
  }
  const digest = fileHash.digest();
  const pathBytes = Buffer.from(file.path, "utf8");
  const header = Buffer.alloc(13);
  header.writeUInt32BE(pathBytes.length, 0);
  header.writeBigUInt64BE(BigInt(file.metadata.size), 4);
  header[12] = (file.metadata.mode & 0o111) === 0 ? 0 : 1;
  treeHash.update(header);
  treeHash.update(pathBytes);
  treeHash.update(digest);
}

const evidence = {
  schema: "logos.palace.product-snapshot-evidence",
  version: 1,
  productSnapshot: snapshot,
  fileCount: actualFiles.length,
  totalBytes,
  treeSha256: treeHash.digest("hex"),
  exactTrackedFileSet: true,
  forbiddenEntriesAbsent: true,
  symlinksAbsent: true,
};

process.stdout.write(`${JSON.stringify(evidence, null, 2)}\n`);
