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
  isRetryableLezSyncReceipt,
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

test("LEZ sync retry classifier stays exact across startup, submit, stable-read, and identity registration stages", async () => {
  const accepted = [
    "current-height-failed",
    "last-synced-height-failed",
    "synced-height-ahead",
    "chunk-failed",
    "chunk-progress-mismatch",
    "terminal-height-mismatch",
  ];
  for (const reason of accepted) {
    assert.equal(
      isRetryableLezSyncReceipt(`rejected=lez-sync;reason=${reason}`, "lez-sync"),
      true,
    );
    assert.equal(
      isRetryableLezSyncReceipt(
        `rejected=lez-submit-sync;reason=${reason}`,
        "lez-submit-sync",
      ),
      true,
    );
    assert.equal(
      isRetryableLezSyncReceipt(
        `rejected=lez-stable-account-read;reason=sync-${reason}`,
        "lez-stable-account-read",
        "sync-",
      ),
      true,
    );
    assert.equal(
      isRetryableLezSyncReceipt(
        `rejected=identity-registration-sync;reason=${reason}`,
        "identity-registration-sync",
      ),
      true,
    );
  }

  const rejected = [
    ["rejected=lez-submit-sync;reason=synced-height-ahead", "lez-sync"],
    ["rejected=lez-sync;reason=synced-height-ahead-extra", "lez-sync"],
    ["rejected=lez-submit-sync;reason=current-height-failed;extra=1", "lez-submit-sync"],
    ["rejected=lez-submit-sync;reason=", "lez-submit-sync"],
    ["rejected=lez-sync;reason=unknown", "lez-sync"],
    ["rejected=lez-submit;reason=synced-height-ahead", "lez-submit-sync"],
    [
      "rejected=lez-stable-account-read;reason=sync-synced-height-ahead-extra",
      "lez-stable-account-read",
      "sync-",
    ],
    [
      "rejected=lez-stable-account-read;reason=sync-current-height-failed;extra=1",
      "lez-stable-account-read",
      "sync-",
    ],
    [
      "rejected=lez-stable-account-read;reason=sync-unknown",
      "lez-stable-account-read",
      "sync-",
    ],
    [
      "rejected=lez-stable-account-read;reason=synced-height-ahead",
      "lez-stable-account-read",
      "sync-",
    ],
    [
      "rejected=identity-registration-sync;reason=synced-height-ahead-extra",
      "identity-registration-sync",
    ],
    [
      "rejected=identity-registration-sync;reason=current-height-failed;extra=1",
      "identity-registration-sync",
    ],
    [
      "rejected=identity-registration-sync;reason=unknown",
      "identity-registration-sync",
    ],
    ["ok;reason=synced-height-ahead", "lez-sync"],
    [null, "lez-sync"],
    ["rejected=lez-sync;reason=synced-height-ahead", null],
  ];
  for (const [receipt, stage, reasonPrefix] of rejected) {
    assert.equal(
      isRetryableLezSyncReceipt(receipt, stage, reasonPrefix),
      false,
    );
  }

  const gate4 = await readFile(gate4Path, "utf8");
  const gate3 = await readFile(gate3Path, "utf8");
  assert.match(gate3, /isRetryableLezSyncReceipt\(lastReceipt, "lez-sync"\)/);
  assert.match(
    gate3,
    /isRetryableLezSyncReceipt\(\s*created\.receipt,\s*"identity-registration-sync",\s*\)/,
  );
  assert.match(gate3, /await startProductionLez\(worker\);/);
  assert.match(gate4, /isRetryableLezSyncReceipt\(lastReceipt, "lez-sync"\)/);
  assert.match(gate4, /isRetryableLezSyncReceipt\(receipt, "lez-sync"\)/);
  assert.match(gate4, /isRetryableLezSyncReceipt\(receipt, "lez-submit-sync"\)/);
  assert.match(
    gate4,
    /isRetryableLezSyncReceipt\(\s*receipt,\s*"lez-stable-account-read",\s*"sync-",\s*\)/,
  );
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

test("Gate 3 resume restarts Storage for its fresh provider worker", async () => {
  const gate3 = await readFile(gate3Path, "utf8");
  const providerBootstrapStart = gate3.indexOf(
    "  // After creator publication, bring provider B onto the private mesh so it",
  );
  const providerStart = gate3.indexOf(
    "report.storageStartup.b = await startStorage(",
    providerBootstrapStart,
  );
  const providerEndpoint = gate3.indexOf(
    "const providerEndpoint = await readStoragePeerEndpoint(provider);",
    providerStart,
  );
  assert.notEqual(providerBootstrapStart, -1);
  assert.notEqual(providerStart, -1);
  assert.notEqual(providerEndpoint, -1);
  const providerBootstrap = gate3.slice(
    providerBootstrapStart,
    providerEndpoint,
  );
  assert.match(
    providerBootstrap,
    /report\.storageStartup\.b = await startStorage\(\s*provider,\s*configs\.b,?\s*\);\s*await checkpointReport\(\);/,
  );
  assert.doesNotMatch(providerBootstrap, /if \(!report\.storageStartup\.b\)/);
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
