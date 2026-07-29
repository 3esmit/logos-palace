import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import {
  mkdtemp,
  readFile,
  rename,
  rm,
  symlink,
  writeFile,
} from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";
import test from "node:test";
import { deflateSync } from "node:zlib";

const validator = new URL("./validate_gate4_artifacts.mjs", import.meta.url);
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
];

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

function chunk(type, data) {
  const typeBytes = Buffer.from(type, "ascii");
  const output = Buffer.alloc(12 + data.length);
  output.writeUInt32BE(data.length, 0);
  typeBytes.copy(output, 4);
  data.copy(output, 8);
  output.writeUInt32BE(
    crc32(Buffer.concat([typeBytes, data])),
    8 + data.length,
  );
  return output;
}

function png(width = 1600, height = 900) {
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header[8] = 8;
  header[9] = 2;
  const scanlines = Buffer.alloc((width * 3 + 1) * height);
  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk("IHDR", header),
    chunk("IDAT", deflateSync(scanlines)),
    chunk("IEND", Buffer.alloc(0)),
  ]);
}

async function fixture() {
  const directory = await mkdtemp(
    join(tmpdir(), "logos-palace-gate4-artifacts-"),
  );
  const bytes = png();
  const screenshots = [];
  for (const [file, stage, state, label] of expected) {
    await writeFile(join(directory, file), bytes);
    screenshots.push({
      file,
      artifactPath: file,
      stage,
      state,
      label,
      byteLength: bytes.length,
      width: 1600,
      height: 900,
      sha256: createHash("sha256").update(bytes).digest("hex"),
    });
  }
  const reportPath = join(directory, "gate4-report.json");
  const report = {
    schema: "logos.palace.basecamp-gate4-6-report",
    version: 2,
    screenshots,
  };
  await writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`);
  return { bytes, directory, report, reportPath };
}

function validate({ directory, reportPath }) {
  return spawnSync(
    process.execPath,
    [validator.pathname, reportPath, directory],
    { encoding: "utf8" },
  );
}

async function updateReport(input) {
  await writeFile(
    input.reportPath,
    `${JSON.stringify(input.report, null, 2)}\n`,
  );
}

test("reopens exact ten fully decoded PNG artifacts", async (t) => {
  const input = await fixture();
  t.after(() => rm(input.directory, { recursive: true, force: true }));
  const result = validate(input);
  assert.equal(result.status, 0, result.stderr);
  assert.equal(result.stdout, "GATE4_ARTIFACTS=PASS\n");
});

test("rejects corrupt PNG CRC even when report digest matches bytes", async (t) => {
  const input = await fixture();
  t.after(() => rm(input.directory, { recursive: true, force: true }));
  const file = expected[0][0];
  const path = join(input.directory, file);
  const bytes = Buffer.from(await readFile(path));
  const idatOffset = bytes.indexOf(Buffer.from("IDAT", "ascii"));
  bytes[idatOffset + 4] ^= 0x01;
  await writeFile(path, bytes);
  input.report.screenshots[0].sha256 =
    createHash("sha256").update(bytes).digest("hex");
  await updateReport(input);
  const result = validate(input);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /invalid IDAT CRC/);
});

test("rejects CRC-valid PNG with invalid IDAT zlib data", async (t) => {
  const input = await fixture();
  t.after(() => rm(input.directory, { recursive: true, force: true }));
  const header = Buffer.alloc(13);
  header.writeUInt32BE(1600, 0);
  header.writeUInt32BE(900, 4);
  header[8] = 8;
  header[9] = 2;
  const bytes = Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk("IHDR", header),
    chunk("IDAT", Buffer.from("not-zlib-data", "ascii")),
    chunk("IEND", Buffer.alloc(0)),
  ]);
  const file = expected[0][0];
  await writeFile(join(input.directory, file), bytes);
  Object.assign(input.report.screenshots[0], {
    byteLength: bytes.length,
    sha256: createHash("sha256").update(bytes).digest("hex"),
  });
  await updateReport(input);
  const result = validate(input);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /invalid or oversized IDAT zlib data/);
});

test("rejects CRC-valid secret bytes after the IDAT zlib stream", async (t) => {
  const input = await fixture();
  t.after(() => rm(input.directory, { recursive: true, force: true }));
  const header = Buffer.alloc(13);
  header.writeUInt32BE(1600, 0);
  header.writeUInt32BE(900, 4);
  header[8] = 8;
  header[9] = 2;
  const rows = Buffer.alloc(900 * (1 + 1600 * 3));
  const bytes = Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk("IHDR", header),
    chunk(
      "IDAT",
      Buffer.concat([
        deflateSync(rows),
        Buffer.from("hidden-after-zlib-stream", "ascii"),
      ]),
    ),
    chunk("IEND", Buffer.alloc(0)),
  ]);
  const file = expected[0][0];
  await writeFile(join(input.directory, file), bytes);
  Object.assign(input.report.screenshots[0], {
    byteLength: bytes.length,
    sha256: createHash("sha256").update(bytes).digest("hex"),
  });
  await updateReport(input);
  const result = validate(input);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /invalid or oversized IDAT zlib data/);
});

test("rejects CRC-valid private PNG text metadata", async (t) => {
  const input = await fixture();
  t.after(() => rm(input.directory, { recursive: true, force: true }));
  const header = Buffer.alloc(13);
  header.writeUInt32BE(1600, 0);
  header.writeUInt32BE(900, 4);
  header[8] = 8;
  header[9] = 2;
  const rows = Buffer.alloc(900 * (1 + 1600 * 3));
  const bytes = Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk("IHDR", header),
    chunk("tEXt", Buffer.from("Comment\\0private-path", "utf8")),
    chunk("IDAT", deflateSync(rows)),
    chunk("IEND", Buffer.alloc(0)),
  ]);
  const file = expected[0][0];
  await writeFile(join(input.directory, file), bytes);
  Object.assign(input.report.screenshots[0], {
    byteLength: bytes.length,
    sha256: createHash("sha256").update(bytes).digest("hex"),
  });
  await updateReport(input);
  const result = validate(input);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /disallowed tEXt screenshot metadata/);
});

test("rejects valid PNG with dimensions other than 1600x900", async (t) => {
  const input = await fixture();
  t.after(() => rm(input.directory, { recursive: true, force: true }));
  const bytes = png(1599, 900);
  const file = expected[0][0];
  await writeFile(join(input.directory, file), bytes);
  Object.assign(input.report.screenshots[0], {
    byteLength: bytes.length,
    width: 1599,
    sha256: createHash("sha256").update(bytes).digest("hex"),
  });
  await updateReport(input);
  const result = validate(input);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /metadata is invalid|dimensions differ/);
});

test("rejects truncated PNG even when report length and digest match", async (t) => {
  const input = await fixture();
  t.after(() => rm(input.directory, { recursive: true, force: true }));
  const file = expected[0][0];
  const bytes = input.bytes.subarray(0, input.bytes.length - 4);
  await writeFile(join(input.directory, file), bytes);
  Object.assign(input.report.screenshots[0], {
    byteLength: bytes.length,
    sha256: createHash("sha256").update(bytes).digest("hex"),
  });
  await updateReport(input);
  const result = validate(input);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /truncated|IEND|structure/);
});

test("rejects screenshot symlink replacement", async (t) => {
  const input = await fixture();
  t.after(() => rm(input.directory, { recursive: true, force: true }));
  const file = expected[0][0];
  const path = join(input.directory, file);
  const target = join(input.directory, "outside.bin");
  await writeFile(target, input.bytes);
  await rename(path, `${path}.replaced`);
  await symlink(target, path);
  const result = validate(input);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /PNG set is not exact|canonical regular file/);
});

test("rejects extra PNG in artifact directory", async (t) => {
  const input = await fixture();
  t.after(() => rm(input.directory, { recursive: true, force: true }));
  await writeFile(join(input.directory, "extra.png"), input.bytes);
  const result = validate(input);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /PNG set is not exact/);
});
