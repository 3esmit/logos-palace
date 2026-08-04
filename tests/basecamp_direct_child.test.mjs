#!/usr/bin/env node

import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import test from "node:test";
import {
  captureDirectChildIdentity,
  procStartTimeTicks,
  signalDirectChild,
  waitForDirectChildExit,
} from "./basecamp_direct_child.mjs";

function stat(pid, startTimeTicks) {
  return `${pid} (fixture) S 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 `
    + `${startTimeTicks} 0 0\n`;
}

test("captures immutable direct-child PID and start time synchronously", () => {
  const child = {
    pid: 7123,
    exitCode: null,
    signalCode: null,
  };
  assert.deepEqual(
    captureDirectChildIdentity(child, {
      read: (path) => {
        assert.equal(path, "/proc/7123/stat");
        return stat(7123, 99881);
      },
    }),
    { pid: 7123, startTimeTicks: 99881 },
  );
  assert.equal(procStartTimeTicks(stat(7123, 99881), 7123), 99881);
});

test("PID reuse is rejected by pidfd helper identity arguments", async () => {
  const calls = [];
  await assert.rejects(
    signalDirectChild(
      { pid: 7124, startTimeTicks: 99882 },
      "SIGKILL",
      {
        helper: "/nix/store/palace-pidfd-signal",
        execute: (...args) => {
          calls.push(args.slice(0, 2));
          const callback = args.at(-1);
          callback(
            Object.assign(new Error("identity mismatch"), { code: 4 }),
            "",
            "pidfd target identity mismatch\n",
          );
        },
      },
    ),
    /pidfd target identity mismatch/,
  );
  assert.equal(calls.length, 1);
});

test("pidfd helper receives exact identity and signal", async () => {
  const calls = [];
  await signalDirectChild(
    { pid: 7125, startTimeTicks: 99883 },
    "SIGTERM",
    {
      helper: "/nix/store/palace-pidfd-signal",
      execute: (helper, args, options, callback) => {
        calls.push({ helper, args, options });
        callback(null, "", "");
      },
    },
  );
  assert.deepEqual(calls[0].args, ["7125", "99883", "SIGTERM"]);
  assert.deepEqual(calls[0].options.env, {});
});

test("direct-child wait is bounded on cleanup failure", async () => {
  let scheduled;
  await assert.rejects(
    waitForDirectChildExit(
      new Promise(() => {}),
      5000,
      "test child",
      {
        schedule: (callback) => {
          scheduled = callback;
          queueMicrotask(callback);
          return 19;
        },
        cancel: () => {},
      },
    ),
    /did not exit within 5000ms; exact process-scope cleanup is required/,
  );
  assert.equal(typeof scheduled, "function");
});

test("direct-child wait returns exact exit result", async () => {
  const exit = { code: null, signal: "SIGKILL" };
  assert.deepEqual(
    await waitForDirectChildExit(
      Promise.resolve(exit),
      5000,
      "test child",
    ),
    exit,
  );
});

test(
  "live pidfd signal rejects wrong identity then signals exact child",
  { skip: !process.env.PALACE_PIDFD_SIGNAL },
  async () => {
    const child = spawn(
      process.execPath,
      ["-e", "setInterval(() => {}, 1000)"],
      { stdio: "ignore" },
    );
    const exited = new Promise((resolveExit, rejectExit) => {
      child.once("error", rejectExit);
      child.once("exit", (code, signal) => resolveExit({ code, signal }));
    });
    const identity = captureDirectChildIdentity(child);
    let exactSignalSent = false;
    try {
      await assert.rejects(
        signalDirectChild(
          {
            pid: identity.pid,
            startTimeTicks: identity.startTimeTicks + 1,
          },
          "SIGTERM",
        ),
        /pidfd target identity mismatch/,
      );
      assert.equal(child.exitCode, null);
      assert.equal(child.signalCode, null);
      await signalDirectChild(identity, "SIGTERM");
      exactSignalSent = true;
      assert.deepEqual(
        await waitForDirectChildExit(exited, 5000, "pidfd test child"),
        { code: null, signal: "SIGTERM" },
      );
    } finally {
      if (!exactSignalSent && child.exitCode === null) {
        await signalDirectChild(identity, "SIGKILL").catch(() => {});
        await waitForDirectChildExit(
          exited,
          5000,
          "pidfd test cleanup child",
        ).catch(() => {});
      }
    }
  },
);
