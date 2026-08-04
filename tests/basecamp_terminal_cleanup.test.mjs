#!/usr/bin/env node

import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { fileURLToPath } from "node:url";
import {
  acceptBasecampPidHandoff,
  stopKnownWorkers,
} from "./basecamp_terminal_cleanup.mjs";

function worker({
  label = "a",
  workerPid = 100,
  basecampPid = 200,
  stop = async () => {},
} = {}) {
  return {
    label,
    child: { pid: workerPid },
    basecampPid,
    stop,
  };
}

test("terminal cleanup passes only after all known processes stop", async () => {
  const alive = new Set([100, 200]);
  const failures = await stopKnownWorkers(
    [
      worker({
        stop: async () => {
          alive.clear();
        },
      }),
    ],
    async () => {
      assert.equal(alive.size, 0);
    },
  );
  assert.deepEqual(failures, []);
});

test("terminal cleanup reports a rejected stop", async () => {
  const failures = await stopKnownWorkers(
    [
      worker({
        stop: async () => {
          throw new Error("shutdown failed");
        },
      }),
    ],
    async () => {},
  );
  assert.deepEqual(failures, ["a cleanup rejected: shutdown failed"]);
});

test("terminal cleanup delegates surviving descendants to exact finalizer", async () => {
  const failures = await stopKnownWorkers(
    [worker({ label: "b", workerPid: 301, basecampPid: 302 })],
    async () => {
      throw new Error("run-owned processes survived Gate 2 cleanup");
    },
  );
  assert.deepEqual(failures, [
    "run-owned process cleanup rejected: "
      + "run-owned processes survived Gate 2 cleanup",
  ]);
});

test("terminal cleanup never signals handed-off numeric process IDs", async () => {
  let finalized = false;
  const failures = await stopKnownWorkers(
    [worker({ label: "d", workerPid: 501, basecampPid: 502 })],
    async () => {
      finalized = true;
    },
  );
  assert.deepEqual(failures, []);
  assert.equal(finalized, true);
});

test("early PID handoff survives an init response failure", () => {
  const basecampPid = acceptBasecampPidHandoff(undefined, {
    event: "basecamp-started",
    basecampPid: 601,
  });
  assert.equal(basecampPid, 601);
  assert.throws(
    () => acceptBasecampPidHandoff(basecampPid, {
      event: "basecamp-started",
      basecampPid: 602,
    }),
    /PID handoff is invalid/,
  );
});

test("terminal cleanup fails when run-owned process sweep fails", async () => {
  const failures = await stopKnownWorkers(
    [],
    async () => {
      throw new Error("alternate process group survived");
    },
  );
  assert.deepEqual(failures, [
    "run-owned process cleanup rejected: alternate process group survived",
  ]);
});

test("Gate 4 always runs the exact claim-bound process finalizer", async () => {
  const source = await readFile(
    fileURLToPath(new URL("./basecamp_gate4.mjs", import.meta.url)),
    "utf8",
  );
  const allCalls = source.match(/stopKnownWorkers\s*\(/g) ?? [];
  const exactCalls = source.match(
    /stopKnownWorkers\s*\(\s*\[\.\.\.workers\.values\(\)\]\s*,\s*cleanupClaimBoundProcesses\s*,?\s*\)/g,
  ) ?? [];

  assert.equal(allCalls.length, 2);
  assert.equal(exactCalls.length, allCalls.length);
});
