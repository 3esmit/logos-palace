import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { fileURLToPath } from "node:url";

import {
  acceptsLezStartupObservation,
  currentLezStateExpectation,
  hasNonEmptyReceipt,
  isCurrentLezState,
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

test("LEZ startup accepts an action receipt or current visible state", async () => {
  assert.equal(hasNonEmptyReceipt(""), false);
  assert.equal(hasNonEmptyReceipt("ok;ready=1"), true);
  assert.equal(hasNonEmptyReceipt("rejected=lez-sync;reason=chunk-failed"), true);
  const programId = "a".repeat(64);
  const current =
    `wallet=created;ready=1;compatible=1;running=1;sync=current;`
    + `current_height=42;synced_height=42;program=${programId}`;
  assert.equal(isCurrentLezState(current, programId), true);
  assert.equal(isCurrentLezState(current, "b".repeat(64)), false);
  assert.equal(acceptsLezStartupObservation("", current, programId), true);
  assert.equal(
    acceptsLezStartupObservation("", "wallet=closed;ready=0", programId),
    false,
  );
  assert.equal(
    acceptsLezStartupObservation(
      "rejected=lez-sync;reason=chunk-failed",
      "",
      programId,
    ),
    true,
  );
  assert.equal(
    isCurrentLezState(
      current.replace("sync=current", "sync=catching-up"),
      programId,
    ),
    false,
  );
  assert.deepEqual(currentLezStateExpectation(programId), {
    currentLezState: programId,
  });

  const [worker, gate3, gate4] = await Promise.all([
    readFile(workerPath, "utf8"),
    readFile(gate3Path, "utf8"),
    readFile(gate4Path, "utf8"),
  ]);
  assert.match(worker, /expected\.currentLezState !== undefined/);
  assert.match(worker, /acceptsLezStartupObservation\(/);
  assert.match(gate3, /"gate4StartLez",[\s\S]{0,180}currentLezStateExpectation/);
  assert.match(gate4, /"gate4StartLez",[\s\S]{0,180}currentLezStateExpectation/);
});
