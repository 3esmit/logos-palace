import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { EventEmitter } from "node:events";
import { readFileSync } from "node:fs";
import test from "node:test";

import {
  assertReleaseRootBindings,
  computeRisc0ImageId,
  loadImmutableReleaseArtifact,
  palaceRelease,
  postExplorerReadOnly,
  probePalaceRootAccount,
  validateFetchedReleaseValues,
  validateReleaseManifest,
  verifyPalaceProgramDeployment,
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

function transportError(code) {
  const error = new Error(`fixture transport error: ${code}`);
  error.code = code;
  return error;
}

function explorerRequestSequence(outcomes) {
  const calls = [];
  const request = (options, receiveResponse) => {
    const outcome = outcomes.shift();
    if (!outcome) throw new Error("explorer request sequence exhausted");
    const call = { options, body: undefined };
    calls.push(call);
    const client = new EventEmitter();
    client.destroy = (error) => {
      queueMicrotask(() => client.emit("error", error));
    };
    client.end = (body) => {
      call.body = body;
      queueMicrotask(() => {
        if (outcome.type === "error") {
          client.emit("error", outcome.error);
          return;
        }
        if (outcome.type === "timeout") {
          client.emit("timeout");
          return;
        }
        const response = new EventEmitter();
        response.statusCode = outcome.statusCode ?? 200;
        response.headers = {
          "content-type": "application/json",
          "content-length": String(Buffer.byteLength(outcome.body)),
          ...outcome.headers,
        };
        response.complete = outcome.complete ?? true;
        response.resume = () => {};
        response.destroy = (error) => {
          if (error) queueMicrotask(() => response.emit("error", error));
        };
        receiveResponse(response);
        queueMicrotask(() => {
          response.emit("data", Buffer.from(outcome.body));
          response.emit("end");
        });
      });
    };
    return client;
  };
  return { calls, request };
}

function returningExplorerBody(body) {
  let calls = 0;
  return {
    calls: () => calls,
    post: async () => {
      calls += 1;
      return body;
    },
  };
}

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

test("read-only explorer POST retries transient transport failures", async () => {
  const sequence = explorerRequestSequence([
    { type: "error", error: transportError("ECONNRESET") },
    { type: "timeout" },
    { type: "response", body: "{}" },
  ]);
  const path = `/api/get_account${palaceRelease.explorerSuffix}`;
  const formBody = `account_id=${palaceRelease.rootAccountIdBase58}`;

  assert.equal(
    await postExplorerReadOnly(path, formBody, 1024, sequence.request),
    "{}",
  );
  assert.equal(sequence.calls.length, 3);
  for (const call of sequence.calls) {
    assert.equal(call.options.method, "POST");
    assert.equal(call.options.path, path);
    assert.equal(call.body, formBody);
  }
});

test("read-only explorer POST bounds transient transport retries", async () => {
  const finalError = transportError("ECONNRESET");
  const sequence = explorerRequestSequence([
    { type: "error", error: transportError("ETIMEDOUT") },
    { type: "error", error: transportError("EPIPE") },
    { type: "error", error: finalError },
  ]);

  await assert.rejects(
    () => postExplorerReadOnly(
      `/api/get_account${palaceRelease.explorerSuffix}`,
      `account_id=${palaceRelease.rootAccountIdBase58}`,
      1024,
      sequence.request,
    ),
    (error) => error === finalError,
  );
  assert.equal(sequence.calls.length, 3);
});

test("explorer response validation failures are not retried", async () => {
  const sequence = explorerRequestSequence([
    { type: "response", statusCode: 503, body: "{}" },
    { type: "response", body: "{}" },
  ]);

  await assert.rejects(
    () => postExplorerReadOnly(
      `/api/get_account${palaceRelease.explorerSuffix}`,
      `account_id=${palaceRelease.rootAccountIdBase58}`,
      1024,
      sequence.request,
    ),
    /explorer response rejected: status=503 content-type=application\/json/,
  );
  assert.equal(sequence.calls.length, 1);
});

test("explorer schema, identity, and content failures are not retried", async () => {
  const schema = returningExplorerBody(JSON.stringify({
    program_owner: palaceRelease.systemProgramBase58,
    balance: 0,
    data: "",
    nonce: "0",
  }));
  await assert.rejects(
    () => probePalaceRootAccount(schema.post),
    /root account schema invalid/,
  );
  assert.equal(schema.calls(), 1);

  const identity = returningExplorerBody(JSON.stringify([{
    header: {
      block_id: palaceRelease.deploymentBlockId,
      prev_block_hash: "0".repeat(64),
      hash: "1".repeat(64),
      timestamp: 0,
      signature: "2".repeat(128),
    },
    body: { transactions: [] },
    bedrock_status: "Finalized",
  }]));
  await assert.rejects(
    () => verifyPalaceProgramDeployment(null, identity.post),
    /release deployment block identity mismatch/,
  );
  assert.equal(identity.calls(), 1);

  const content = returningExplorerBody(JSON.stringify({
    program_owner: palaceRelease.systemProgramBase58,
    balance: 0,
    data: "not-base64",
    nonce: 0,
  }));
  await assert.rejects(
    () => probePalaceRootAccount(content.post),
    /root account data is not canonical base64/,
  );
  assert.equal(content.calls(), 1);
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
