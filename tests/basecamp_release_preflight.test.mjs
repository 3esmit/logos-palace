import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import test from "node:test";

import {
  assertReleaseRootBindings,
  computeRisc0ImageId,
  loadImmutableReleaseArtifact,
  palaceRelease,
  validateFetchedReleaseValues,
  validateReleaseManifest,
} from "./basecamp_release_preflight.mjs";

const manifest = readFileSync(
  process.env.PALACE_RELEASE_ARTIFACT === undefined
    ? new URL("../program/release/release.json", import.meta.url)
    : `${process.env.PALACE_RELEASE_ARTIFACT}`
      + "/share/logos-palace/release.json",
  "utf8",
);
const fixtureBytecode = Buffer.from(
  "public non-RISC0 fixture for release-boundary tests",
  "utf8",
);
const fixtureRelease = Object.freeze({
  ...palaceRelease,
  programByteLength: fixtureBytecode.length,
  programBytecodeSha256: createHash("sha256")
    .update(fixtureBytecode)
    .digest("hex"),
});

test("release manifest validates exact pinned public metadata", () => {
  assert.deepEqual(
    validateReleaseManifest(manifest),
    {
      programByteLength: palaceRelease.programByteLength,
      programBytecodeSha256:
        palaceRelease.programBytecodeSha256,
      programIdHex: palaceRelease.programIdHex,
    },
  );
  assertReleaseRootBindings();
});

test("fetched release values accept exact bound bytes and image ID", () => {
  const artifact = validateFetchedReleaseValues(
    fixtureBytecode,
    palaceRelease.programIdHex,
    fixtureRelease,
  );
  assert.equal(artifact.byteLength, fixtureBytecode.length);
  assert.equal(
    artifact.digest,
    fixtureRelease.programBytecodeSha256,
  );
  assert.equal(artifact.imageIdHex, palaceRelease.programIdHex);
});

test("fetched release rejects mutation, truncation, and replacement", () => {
  const mutated = Buffer.from(fixtureBytecode);
  mutated[mutated.length - 1] ^= 0x01;
  for (const candidate of [
    mutated,
    fixtureBytecode.subarray(0, fixtureBytecode.length - 1),
    Buffer.alloc(fixtureBytecode.length),
  ]) {
    assert.throws(
      () => validateFetchedReleaseValues(
        candidate,
        palaceRelease.programIdHex,
        fixtureRelease,
      ),
      /fetched release bytecode mismatch/,
    );
  }
  assert.throws(
    () => validateFetchedReleaseValues(
      fixtureBytecode,
      "0".repeat(64),
      fixtureRelease,
    ),
    /computed RISC0 image ID mismatch/,
  );
});

test("release manifest rejects hash, shape, and image drift", () => {
  const changedManifest = manifest.replace(
    palaceRelease.programBytecodeSha256,
    "0".repeat(64),
  );
  assert.throws(
    () => validateReleaseManifest(changedManifest),
    /immutable release manifest mismatch/,
  );
  const extraManifest = JSON.parse(manifest);
  extraManifest.extra = true;
  assert.throws(
    () => validateReleaseManifest(JSON.stringify(extraManifest)),
    /immutable release manifest mismatch/,
  );
  const missingManifest = JSON.parse(manifest);
  delete missingManifest.byteLength;
  assert.throws(
    () => validateReleaseManifest(JSON.stringify(missingManifest)),
    /immutable release manifest mismatch/,
  );
  const imageMismatchManifest = JSON.parse(manifest);
  imageMismatchManifest.imageIdHex = "0".repeat(64);
  assert.throws(
    () => validateReleaseManifest(
      JSON.stringify(imageMismatchManifest),
    ),
    /immutable release manifest mismatch/,
  );
});

test("release root rejects base58 drift before network use", () => {
  assert.throws(
    () => assertReleaseRootBindings({
      ...palaceRelease,
      rootAccountIdBase58:
        `1${palaceRelease.rootAccountIdBase58.slice(1)}`,
    }),
    /release root base58 binding mismatch/,
  );
});

test(
  "pinned stdin verifier rejects empty, truncated, and non-RISC0 bytes",
  {
    skip: process.env.PALACE_RELEASE_ARTIFACT === undefined,
  },
  () => {
    const artifact = loadImmutableReleaseArtifact();
    for (const candidate of [
      Buffer.alloc(0),
      Buffer.from([0x7f, 0x45, 0x4c, 0x46]),
      Buffer.alloc(palaceRelease.programByteLength),
    ]) {
      assert.throws(
        () => computeRisc0ImageId(
          candidate,
          artifact.verifierPath,
        ),
        /RISC0 image verifier rejected fetched bytecode/,
      );
    }
  },
);
