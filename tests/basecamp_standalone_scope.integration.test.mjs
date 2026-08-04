#!/usr/bin/env node

import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import {
  access,
  chmod,
  mkdtemp,
  readFile,
  rm,
} from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";

const live = process.env.PALACE_STANDALONE_LIVE_TEST === "1";
const snapshot = process.env.PALACE_PRODUCT_SNAPSHOT;
const tools = process.env.PALACE_ACCEPTANCE_TOOLS;

async function exists(path) {
  return access(path).then(
    () => true,
    (error) => {
      if (error?.code === "ENOENT") return false;
      throw error;
    },
  );
}

async function waitFor(path) {
  const deadline = Date.now() + 10_000;
  while (Date.now() < deadline) {
    if (await exists(path)) return;
    await new Promise((resolveWait) => setTimeout(resolveWait, 20));
  }
  throw new Error(`timed out waiting for ${path}`);
}

function invocation(artifacts) {
  return [
    join(tools, "bin", "bash"),
    "-p",
    join(snapshot, "scripts", "run-basecamp-standalone-scoped.sh"),
    "gate1",
    snapshot,
    tools,
    artifacts,
    join(snapshot, "tests", "basecamp_standalone_probe.sh"),
  ];
}

test(
  "live standalone scope serializes one artifact directory",
  { skip: !live },
  async (t) => {
    assert.match(
      snapshot ?? "",
      /^\/nix\/store\/[0-9abcdfghijklmnpqrsvwxyz]{32}-/,
    );
    assert.match(
      tools ?? "",
      /^\/nix\/store\/[0-9abcdfghijklmnpqrsvwxyz]{32}-/,
    );
    const artifacts = await mkdtemp(
      join(tmpdir(), "palace-standalone-scope-"),
    );
    await chmod(artifacts, 0o700);
    t.after(() => rm(artifacts, { recursive: true, force: true }));
    const [program, ...args] = invocation(artifacts);
    const first = spawn(program, args, {
      env: {
        ...process.env,
        PALACE_STANDALONE_PROBE_DELAY: "2",
      },
      stdio: ["ignore", "pipe", "pipe"],
    });
    const firstExit = new Promise((resolveExit, rejectExit) => {
      first.once("error", rejectExit);
      first.once("exit", (code, signal) =>
        resolveExit({ code, signal }));
    });
    await waitFor(join(artifacts, "standalone-probe-started"));

    const contender = spawnSync(program, args, {
      env: {
        ...process.env,
        PALACE_STANDALONE_PROBE_DELAY: "0",
      },
      encoding: "utf8",
      timeout: 10_000,
    });
    assert.equal(contender.status, 75, contender.stderr);
    assert.deepEqual(await firstExit, { code: 0, signal: null });
    assert.equal(
      await readFile(
        join(artifacts, "standalone-probe-passed"),
        "utf8",
      ),
      "passed\n",
    );
    const evidence = JSON.parse(
      await readFile(join(artifacts, "process-scope.json"), "utf8"),
    );
    assert.equal(evidence.status, "cleaned");
    assert.equal(evidence.commandExitStatus, 0);
    assert.equal(evidence.cleanup.residueKilled, false);
    assert.equal(evidence.cleanup.sliceResidueKilled, false);
    assert.equal(
      await exists(join(artifacts, "process-scope-launch.json")),
      false,
    );
  },
);
