import { createHash } from "node:crypto";
import { constants } from "node:fs";
import {
  lstat,
  open,
  readdir,
} from "node:fs/promises";
import { join } from "node:path";

const safeEntryName = /^[A-Za-z0-9][A-Za-z0-9._-]{0,255}$/;

function boundedInteger(value, name) {
  if (!Number.isSafeInteger(value) || value <= 0) {
    throw new Error(`${name} must be a positive safe integer`);
  }
  return value;
}

function linuxDeviceNumber(device) {
  const major =
    ((device & 0x00000000000fff00n) >> 8n)
    | ((device & 0xfffff00000000000n) >> 32n);
  const minor =
    (device & 0x00000000000000ffn)
    | ((device & 0x00000ffffff00000n) >> 12n);
  return `${major.toString(16).padStart(2, "0")}:${
    minor.toString(16).padStart(2, "0")
  }`;
}

export async function fingerprintBoundedOpenRegularFile(
  handle,
  maximumBytes,
) {
  const byteLimit = boundedInteger(maximumBytes, "maximumBytes");
  const metadata = await handle.stat({ bigint: true });
  if (
    !metadata.isFile()
    || metadata.size < 0n
    || metadata.size > BigInt(byteLimit)
    || metadata.ino <= 0n
  ) {
    throw new Error("cold-rebuild file exceeds evidence bound");
  }
  const byteLength = Number(metadata.size);
  const digest = createHash("sha256");
  const buffer = Buffer.allocUnsafe(64 * 1024);
  let offset = 0;
  while (offset < byteLength) {
    const length = Math.min(buffer.length, byteLength - offset);
    const { bytesRead } = await handle.read(
      buffer,
      0,
      length,
      offset,
    );
    if (bytesRead <= 0) {
      throw new Error("cold-rebuild file changed during fingerprint");
    }
    digest.update(buffer.subarray(0, bytesRead));
    offset += bytesRead;
  }
  const trailing = Buffer.allocUnsafe(1);
  const { bytesRead: trailingBytes } = await handle.read(
    trailing,
    0,
    1,
    offset,
  );
  if (trailingBytes !== 0) {
    throw new Error("cold-rebuild file grew during fingerprint");
  }
  const finalMetadata = await handle.stat({ bigint: true });
  if (
    finalMetadata.size !== metadata.size
    || finalMetadata.dev !== metadata.dev
    || finalMetadata.ino !== metadata.ino
  ) {
    throw new Error("cold-rebuild file changed during fingerprint");
  }
  return {
    byteLength,
    sha256: digest.digest("hex"),
    device: linuxDeviceNumber(metadata.dev),
    inode: metadata.ino.toString(10),
  };
}

export async function fingerprintBoundedRegularFileIdentity(
  path,
  maximumBytes,
) {
  const handle = await open(
    path,
    constants.O_RDONLY | constants.O_CLOEXEC | constants.O_NOFOLLOW,
  );
  try {
    return await fingerprintBoundedOpenRegularFile(
      handle,
      maximumBytes,
    );
  } finally {
    await handle.close();
  }
}

export async function fingerprintBoundedRegularFile(
  path,
  maximumBytes,
) {
  const {
    byteLength,
    sha256,
  } = await fingerprintBoundedRegularFileIdentity(path, maximumBytes);
  return { byteLength, sha256 };
}

export async function boundedDirectoryFingerprint(
  path,
  {
    maximumFiles = 4_096,
    maximumDirectories = 4_096,
    maximumDepth = 32,
    maximumTotalBytes = 512 * 1024 * 1024,
  } = {},
) {
  const fileLimit = boundedInteger(maximumFiles, "maximumFiles");
  const directoryLimit = boundedInteger(
    maximumDirectories,
    "maximumDirectories",
  );
  const depthLimit = boundedInteger(maximumDepth, "maximumDepth");
  const byteLimit = boundedInteger(
    maximumTotalBytes,
    "maximumTotalBytes",
  );
  const rootMetadata = await lstat(path);
  if (rootMetadata.isSymbolicLink() || !rootMetadata.isDirectory()) {
    throw new Error("cold-rebuild root is not a safe directory");
  }

  const records = [];
  let directoryCount = 1;
  let totalBytes = 0;
  const visit = async (directory, relativeRoot = "", depth = 0) => {
    if (depth > depthLimit) {
      throw new Error("cold-rebuild directory exceeds depth bound");
    }
    const entries = await readdir(directory, { withFileTypes: true });
    entries.sort((left, right) => left.name.localeCompare(right.name));
    for (const entry of entries) {
      if (!safeEntryName.test(entry.name)) {
        throw new Error(`unsafe cold-rebuild entry ${entry.name}`);
      }
      const absolute = join(directory, entry.name);
      const relative = relativeRoot
        ? `${relativeRoot}/${entry.name}`
        : entry.name;
      const metadata = await lstat(absolute);
      if (metadata.isSymbolicLink()) {
        throw new Error(`unsafe cold-rebuild entry type ${relative}`);
      }
      if (metadata.isDirectory()) {
        directoryCount += 1;
        if (directoryCount > directoryLimit) {
          throw new Error(
            "cold-rebuild directory exceeds directory bound",
          );
        }
        await visit(absolute, relative, depth + 1);
        continue;
      }
      if (!metadata.isFile()) {
        throw new Error(`unsafe cold-rebuild entry type ${relative}`);
      }
      if (records.length >= fileLimit) {
        throw new Error("cold-rebuild directory exceeds file bound");
      }
      const remainingBytes = byteLimit - totalBytes;
      if (metadata.size < 0 || metadata.size > remainingBytes) {
        throw new Error("cold-rebuild directory exceeds byte bound");
      }
      const fingerprint = await fingerprintBoundedRegularFile(
        absolute,
        Math.max(1, remainingBytes),
      );
      totalBytes += fingerprint.byteLength;
      records.push({
        file: relative,
        ...fingerprint,
      });
    }
  };
  await visit(path);
  return {
    fileCount: records.length,
    directoryCount,
    totalBytes,
    sha256: createHash("sha256")
      .update(JSON.stringify(records))
      .digest("hex"),
  };
}
