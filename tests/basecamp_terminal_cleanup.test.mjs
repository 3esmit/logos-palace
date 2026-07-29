#!/usr/bin/env node

import assert from "node:assert/strict";
import test from "node:test";
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
    (pid) => alive.has(pid),
    () => false,
    async () => {},
    async () => {},
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
    () => false,
    () => false,
    async () => {},
    async () => {},
  );
  assert.deepEqual(failures, ["a cleanup rejected: shutdown failed"]);
});

test("terminal cleanup reports every surviving known PID", async () => {
  const failures = await stopKnownWorkers(
    [worker({ label: "b", workerPid: 301, basecampPid: 302 })],
    () => true,
    () => true,
    async () => {},
    async () => {},
  );
  assert.deepEqual(failures, [
    "b worker process 301 survived cleanup",
    "b worker process group 301 survived cleanup",
    "b Basecamp process 302 survived cleanup",
    "b Basecamp process group 302 survived cleanup",
  ]);
});

test("terminal cleanup rejects a surviving detached Basecamp group", async () => {
  const failures = await stopKnownWorkers(
    [worker({ label: "c", workerPid: 401, basecampPid: 402 })],
    () => false,
    (processGroupId) => processGroupId === 402,
    async () => {},
    async () => {},
  );
  assert.deepEqual(failures, [
    "c Basecamp process group 402 survived cleanup",
  ]);
});

test("terminal cleanup can force a detached group before proof", async () => {
  const groups = new Set([502]);
  const failures = await stopKnownWorkers(
    [worker({ label: "d", workerPid: 501, basecampPid: 502 })],
    () => false,
    (processGroupId) => groups.has(processGroupId),
    async (processGroupId) => {
      groups.delete(processGroupId);
    },
    async () => {},
  );
  assert.deepEqual(failures, []);
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
    () => false,
    () => false,
    async () => {},
    async () => {
      throw new Error("alternate process group survived");
    },
  );
  assert.deepEqual(failures, [
    "run-owned process cleanup rejected: alternate process group survived",
  ]);
});
