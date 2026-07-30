import assert from "node:assert/strict";
import test from "node:test";

import {
  lezStartupTimeoutMs,
  ordinaryInvocationTimeoutMs,
  workerInvocationTimeoutLimit,
} from "./basecamp_lez_startup.mjs";

test("permits an extended timeout only for production LEZ startup", () => {
  assert.equal(lezStartupTimeoutMs, 8 * 60_000);
  assert.equal(workerInvocationTimeoutLimit("gate4StartLez"), lezStartupTimeoutMs);
  assert.equal(workerInvocationTimeoutLimit("gate3StartStorage"), ordinaryInvocationTimeoutMs);
  assert.equal(workerInvocationTimeoutLimit("unknown"), ordinaryInvocationTimeoutMs);
});
