import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { fileURLToPath } from "node:url";

import {
  acceptsStorageStartupObservation,
  acceptsLezStartupObservation,
  currentStorageStateExpectation,
  currentLezStateExpectation,
  hasNonEmptyReceipt,
  isStartedStorageState,
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

test("storage startup accepts an action receipt or live storage state", async () => {
  const starting =
    "storage=starting;pending=0;callbacks=0;callback_registration=ready;"
    + "reconciliation_required=0;catalog=idle;catalog_verified=0;"
    + "retention_round=0;retained=0";
  const running =
    "storage=running;pending=0;callbacks=0;callback_registration=ready;"
    + "reconciliation_required=0;catalog=idle;catalog_verified=0;"
    + "retention_round=0;retained=0";
  assert.equal(isStartedStorageState(starting), true);
  assert.equal(isStartedStorageState(running), true);
  assert.equal(
    isStartedStorageState(
      starting.replace("callback_registration=ready", "callback_registration=failed"),
    ),
    false,
  );
  assert.equal(
    acceptsStorageStartupObservation("", starting),
    true,
  );
  assert.equal(
    acceptsStorageStartupObservation("", "storage=offline;callback_registration=ready"),
    false,
  );
  assert.equal(
    acceptsStorageStartupObservation("rejected=storage-session-config", ""),
    true,
  );
  assert.deepEqual(currentStorageStateExpectation(), {
    startedStorageState: true,
  });

  const [worker, gate3] = await Promise.all([
    readFile(workerPath, "utf8"),
    readFile(gate3Path, "utf8"),
  ]);
  assert.match(worker, /expected\.startedStorageState !== undefined/);
  assert.match(worker, /acceptsStorageStartupObservation\(/);
  assert.match(gate3, /"gate3StartStorage",[\s\S]{0,180}currentStorageStateExpectation/);
});

test("Gate 4 invocations are authorized by the Palace worker", async () => {
  const [worker, gate4] = await Promise.all([
    readFile(workerPath, "utf8"),
    readFile(gate4Path, "utf8"),
  ]);
  const allowlistStart = worker.indexOf("const allowedFunctions = new Set([");
  const allowlistEnd = worker.indexOf("]);", allowlistStart);
  assert.notEqual(allowlistStart, -1);
  assert.notEqual(allowlistEnd, -1);
  const allowlist = new Set(
    [...worker.slice(allowlistStart, allowlistEnd).matchAll(/"([^"]+)"/g)]
      .map((match) => match[1]),
  );
  const gate4Invocations = [
    "gate4StartLez",
    "gate4RefreshIdentity",
    "gate4PalaceStatus",
    "gate4OpenPalace",
    "gate4ActionStatus",
    "gate4Submit",
    "gate4BanUser",
    "gate4BanProp",
    "gate4Observe",
    "gate4Reconcile",
    "gate4RefreshModeration",
  ];

  for (const name of gate4Invocations) {
    assert.match(gate4, new RegExp(`"${name}"`));
    assert.ok(allowlist.has(name), `${name} must be worker-authorized`);
  }
});
