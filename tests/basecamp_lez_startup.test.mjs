import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { fileURLToPath } from "node:url";

import {
  hasNonEmptyReceipt,
  lezStartupReceiptExpectation,
  lezStartupTimeoutMs,
  ordinaryInvocationTimeoutMs,
  workerInvocationTimeoutLimit,
} from "./basecamp_lez_startup.mjs";

const workerPath = fileURLToPath(
  new URL("./basecamp_gate3_worker.mjs", import.meta.url),
);
const gate3Path = fileURLToPath(
  new URL("./basecamp_gate3.mjs", import.meta.url),
);
const gate4Path = fileURLToPath(
  new URL("./basecamp_gate4.mjs", import.meta.url),
);

test("permits an extended timeout only for production LEZ startup", () => {
  assert.equal(lezStartupTimeoutMs, 8 * 60_000);
  assert.equal(workerInvocationTimeoutLimit("gate4StartLez"), lezStartupTimeoutMs);
  assert.equal(workerInvocationTimeoutLimit("gate3StartStorage"), ordinaryInvocationTimeoutMs);
  assert.equal(workerInvocationTimeoutLimit("unknown"), ordinaryInvocationTimeoutMs);
});

test("LEZ startup waits for a material terminal receipt", async () => {
  assert.deepEqual(lezStartupReceiptExpectation, { nonEmpty: true });
  assert.equal(hasNonEmptyReceipt(""), false);
  assert.equal(hasNonEmptyReceipt("ok;ready=1"), true);
  assert.equal(hasNonEmptyReceipt("rejected=lez-sync;reason=chunk-failed"), true);

  const [worker, gate3, gate4] = await Promise.all([
    readFile(workerPath, "utf8"),
    readFile(gate3Path, "utf8"),
    readFile(gate4Path, "utf8"),
  ]);
  assert.match(worker, /expected\.nonEmpty === true/);
  assert.match(worker, /hasNonEmptyReceipt\(receipt\)/);
  assert.match(gate3, /"gate4StartLez",[\s\S]{0,180}lezStartupReceiptExpectation/);
  assert.match(gate4, /"gate4StartLez",[\s\S]{0,180}lezStartupReceiptExpectation/);
});
