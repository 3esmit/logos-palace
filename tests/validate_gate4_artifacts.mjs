#!/usr/bin/env node

import { createHash } from "node:crypto";
import {
  lstat,
  readFile,
  readdir,
  realpath,
} from "node:fs/promises";
import {
  basename,
  dirname,
  join,
  resolve,
} from "node:path";
import { inflateSync } from "node:zlib";

const [reportArgument, artifactsArgument] = process.argv.slice(2);
if (!reportArgument || !artifactsArgument) {
  throw new Error(
    "usage: node tests/validate_gate4_artifacts.mjs <report> <artifacts-dir>",
  );
}

const reportPath = resolve(reportArgument);
const artifactsDir = resolve(artifactsArgument);
const expected = [
  ["gate4-a-three-user-atrium-converged.png", "gate4-delivery-convergence", "three-user-atrium-with-approved-prop", "a"],
  ["gate4-b-storage-object-degraded.png", "gate4-storage-failure", "missing-storage-object-degraded", "b"],
  ["gate4-b-atrium-after-moderation.png", "gate4-moderation", "atrium-after-human-user-and-prop-bans", "b"],
  ["gate5-a-door-preview.png", "gate5-preview", "door-preview-before-finality", "a"],
  ["gate5-b-door-preview.png", "gate5-preview", "door-preview-before-finality", "b"],
  ["gate5-b-door-pending.png", "gate5-pending", "door-awaiting-lez-observation", "b"],
  ["gate5-b-lounge-finalized.png", "gate5-finality", "lounge-after-door-finality", "b"],
  ["gate6-b-offline-before-reconnect.png", "gate6-offline", "creator-offline-client-before-reconnect", "b"],
  ["gate6-b-lounge-restarted.png", "gate6-restart", "lounge-after-bob-carol-restart", "b"],
  ["gate6-c-lounge-restarted.png", "gate6-restart", "lounge-after-bob-carol-restart", "c"],
].map(([file, stage, state, label]) => ({ file, stage, state, label }));

const crcTable = Array.from({ length: 256 }, (_, index) => {
  let value = index;
  for (let bit = 0; bit < 8; bit += 1) {
    value = (value & 1) !== 0
      ? (value >>> 1) ^ 0xedb88320
      : value >>> 1;
  }
  return value >>> 0;
});

function crc32(bytes) {
  let value = 0xffffffff;
  for (const byte of bytes) {
    value = crcTable[(value ^ byte) & 0xff] ^ (value >>> 8);
  }
  return (value ^ 0xffffffff) >>> 0;
}

function validatePng(bytes, file) {
  if (
    bytes.length < 45
    || !bytes.subarray(0, 8).equals(
      Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    )
  ) {
    throw new Error(`${file} has invalid PNG signature`);
  }
  let offset = 8;
  let chunkCount = 0;
  let width;
  let height;
  const idatChunks = [];
  let sawIend = false;
  let sawPhys = false;
  let idatEnded = false;
  while (offset < bytes.length && chunkCount < 10_000) {
    if (offset + 12 > bytes.length) {
      throw new Error(`${file} has truncated PNG chunk header`);
    }
    const length = bytes.readUInt32BE(offset);
    const chunkEnd = offset + 12 + length;
    if (length > 64 * 1024 * 1024 || chunkEnd > bytes.length) {
      throw new Error(`${file} has truncated or oversized PNG chunk`);
    }
    const typeBytes = bytes.subarray(offset + 4, offset + 8);
    const type = typeBytes.toString("ascii");
    if (!/^[A-Za-z]{4}$/.test(type)) {
      throw new Error(`${file} has invalid PNG chunk type`);
    }
    const data = bytes.subarray(offset + 8, offset + 8 + length);
    const expectedCrc = bytes.readUInt32BE(offset + 8 + length);
    if (
      crc32(Buffer.concat([typeBytes, data])) !== expectedCrc
    ) {
      throw new Error(`${file} has invalid ${type} CRC`);
    }
    if (chunkCount === 0) {
      if (type !== "IHDR" || length !== 13) {
        throw new Error(`${file} does not begin with exact IHDR`);
      }
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
      if (
        data[8] !== 8
        || data[9] !== 2
        || data[10] !== 0
        || data[11] !== 0
        || data[12] !== 0
      ) {
        throw new Error(`${file} does not use exact 8-bit RGB PNG encoding`);
      }
    } else if (type === "IHDR") {
      throw new Error(`${file} has duplicate IHDR`);
    }
    if (!["IHDR", "pHYs", "IDAT", "IEND"].includes(type)) {
      throw new Error(`${file} has disallowed ${type} screenshot metadata`);
    }
    if (type === "pHYs") {
      if (
        sawPhys
        || idatChunks.length > 0
        || length !== 9
        || data.readUInt32BE(0) !== 3780
        || data.readUInt32BE(4) !== 3780
        || data[8] !== 1
      ) {
        throw new Error(`${file} has invalid pHYs screenshot metadata`);
      }
      sawPhys = true;
    }
    if (type === "IDAT") {
      if (idatEnded) {
        throw new Error(`${file} has non-consecutive IDAT chunks`);
      }
      idatChunks.push(data);
    } else if (idatChunks.length > 0) {
      idatEnded = true;
    }
    if (type === "IEND") {
      if (length !== 0 || chunkEnd !== bytes.length) {
        throw new Error(`${file} has invalid IEND/trailing bytes`);
      }
      sawIend = true;
    }
    offset = chunkEnd;
    chunkCount += 1;
    if (sawIend) break;
  }
  if (
    idatChunks.length === 0
    || !sawIend
    || offset !== bytes.length
    || width !== 1600
    || height !== 900
  ) {
    throw new Error(`${file} PNG structure or 1600x900 dimensions differ`);
  }
  const rowBytes = width * 3 + 1;
  const expectedInflatedBytes = rowBytes * height;
  const compressedPixels = Buffer.concat(idatChunks);
  let pixels;
  try {
    const decoded = inflateSync(compressedPixels, {
      info: true,
      maxOutputLength: expectedInflatedBytes,
    });
    if (
      !Buffer.isBuffer(decoded?.buffer)
      || decoded?.engine?.bytesWritten !== compressedPixels.length
    ) {
      throw new Error("IDAT zlib stream did not consume all input");
    }
    pixels = decoded.buffer;
  } catch {
    throw new Error(`${file} has invalid or oversized IDAT zlib data`);
  }
  if (pixels.length !== expectedInflatedBytes) {
    throw new Error(`${file} has invalid decoded RGB byte length`);
  }
  for (let row = 0; row < height; row += 1) {
    if (pixels[row * rowBytes] > 4) {
      throw new Error(`${file} has invalid PNG row filter`);
    }
  }
  return { width, height };
}

async function boundedRegularFile(path, maximumBytes) {
  const metadata = await lstat(path);
  if (
    metadata.isSymbolicLink()
    || !metadata.isFile()
    || metadata.size <= 0
    || metadata.size > maximumBytes
    || await realpath(path) !== path
  ) {
    throw new Error(`${basename(path)} is not a bounded canonical regular file`);
  }
  const bytes = await readFile(path);
  if (bytes.length !== metadata.size) {
    throw new Error(`${basename(path)} changed while reading`);
  }
  return bytes;
}

const reportBytes = await boundedRegularFile(reportPath, 16 * 1024 * 1024);
let report;
try {
  report = JSON.parse(reportBytes.toString("utf8"));
} catch {
  throw new Error("Gate 4 report is not valid JSON");
}
if (
  report?.schema !== "logos.palace.basecamp-gate4-6-report"
  || report.version !== 2
  || !Array.isArray(report.screenshots)
  || report.screenshots.length !== expected.length
  || dirname(reportPath) !== artifactsDir
  || await realpath(artifactsDir) !== artifactsDir
) {
  throw new Error("Gate 4 report/artifact envelope is invalid");
}

const byFile = new Map(
  report.screenshots.map((entry) => [entry?.file, entry]),
);
if (byFile.size !== expected.length) {
  throw new Error("Gate 4 screenshot filenames are not unique");
}
const pngFiles = (await readdir(artifactsDir, { withFileTypes: true }))
  .filter((entry) => entry.isFile() && entry.name.endsWith(".png"))
  .map((entry) => entry.name)
  .sort();
if (
  JSON.stringify(pngFiles)
  !== JSON.stringify(expected.map(({ file }) => file).sort())
) {
  throw new Error("Gate 4 artifact directory PNG set is not exact");
}
for (const spec of expected) {
  const entry = byFile.get(spec.file);
  if (
    !entry
    || Object.keys(entry).sort().join(",")
      !== [
        "artifactPath",
        "byteLength",
        "file",
        "height",
        "label",
        "sha256",
        "stage",
        "state",
        "width",
      ].sort().join(",")
    || entry.file !== spec.file
    || entry.artifactPath !== spec.file
    || entry.stage !== spec.stage
    || entry.state !== spec.state
    || entry.label !== spec.label
    || !Number.isSafeInteger(entry.byteLength)
    || !Number.isSafeInteger(entry.width)
    || !Number.isSafeInteger(entry.height)
    || entry.byteLength < 24
    || entry.byteLength > 64 * 1024 * 1024
    || entry.width !== 1600
    || entry.height !== 900
    || typeof entry.sha256 !== "string"
    || !/^[0-9a-f]{64}$/.test(entry.sha256)
  ) {
    throw new Error(`${spec.file} report metadata is invalid`);
  }
  const path = join(artifactsDir, spec.file);
  if (dirname(path) !== artifactsDir) {
    throw new Error(`${spec.file} escaped the artifact directory`);
  }
  const bytes = await boundedRegularFile(path, 64 * 1024 * 1024);
  const dimensions = validatePng(bytes, spec.file);
  if (
    bytes.length !== entry.byteLength
    || dimensions.width !== entry.width
    || dimensions.height !== entry.height
    || createHash("sha256").update(bytes).digest("hex") !== entry.sha256
  ) {
    throw new Error(`${spec.file} bytes differ from report evidence`);
  }
}

process.stdout.write(
  "GATE4_ARTIFACTS=PASS\n"
    + `GATE4_REPORT_SHA256=${
      createHash("sha256").update(reportBytes).digest("hex")
    }\n`,
);
