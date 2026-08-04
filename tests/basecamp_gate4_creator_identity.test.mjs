#!/usr/bin/env node

import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import {
  originalCreatorProcessExists,
  processStartTimeTicks,
  validateCreatorProcessIdentity,
} from "./basecamp_gate4_creator_identity.mjs";

const identity = Object.freeze({
  pid: 4242,
  startTimeTicks: 987654,
  observedCgroupPath:
    "/user.slice/user-1000.slice/logos-palace-run-Ab12Cd34.slice/"
      + "logos-palace-run-Ab12Cd34-gate4-Ef56Gh78.scope",
});

function stat(startTimeTicks = identity.startTimeTicks) {
  const fields = [
    "S",
    "1",
    "4242",
    "4242",
    "0",
    "0",
    "0",
    "0",
    "0",
    "0",
    "0",
    "0",
    "0",
    "0",
    "0",
    "0",
    "0",
    "0",
    "0",
    String(startTimeTicks),
  ];
  return Buffer.from(`${identity.pid} (LogosBasecamp) ${fields.join(" ")}\n`);
}

test("validates and parses exact creator identity", () => {
  assert.deepEqual(
    validateCreatorProcessIdentity(identity, identity.pid),
    identity,
  );
  assert.equal(
    processStartTimeTicks(stat(), identity.pid),
    identity.startTimeTicks,
  );
  assert.throws(
    () => validateCreatorProcessIdentity({
      ...identity,
      unverified: true,
    }),
    /creator process identity is invalid/,
  );
});

test("checks the original process in its persisted cgroup", async () => {
  let checked;
  assert.equal(
    await originalCreatorProcessExists(identity, {
      read: async () => stat(),
      identityExists: async (candidate) => {
        checked = candidate;
        return true;
      },
    }),
    true,
  );
  assert.deepEqual(checked, {
    ...identity,
    cgroupPath: identity.observedCgroupPath,
  });
});

test("PID reuse means the original creator is offline", async () => {
  let identityChecks = 0;
  assert.equal(
    await originalCreatorProcessExists(identity, {
      read: async () => stat(identity.startTimeTicks + 1),
      identityExists: async () => {
        identityChecks += 1;
        return true;
      },
    }),
    false,
  );
  assert.equal(identityChecks, 0);
});

test("PID reuse during identity validation is offline", async () => {
  let reads = 0;
  assert.equal(
    await originalCreatorProcessExists(identity, {
      read: async () => {
        reads += 1;
        return stat(
          reads === 1
            ? identity.startTimeTicks
            : identity.startTimeTicks + 1,
        );
      },
      identityExists: async () => {
        throw new Error("process changed identity during cleanup");
      },
    }),
    false,
  );
  assert.equal(reads, 2);
});

test("same creator leaving its attested cgroup fails closed", async () => {
  await assert.rejects(
    originalCreatorProcessExists(identity, {
      read: async () => stat(),
      identityExists: async () => {
        throw new Error("process left cleanup cgroup");
      },
    }),
    /process left cleanup cgroup/,
  );
});

test("Gate 4 treats a persisted creator identity only as a blocker", async () => {
  const source = await readFile(
    new URL("./basecamp_gate4.mjs", import.meta.url),
    "utf8",
  );
  const resumeStart = source.indexOf("const priorCreatorPhase =");
  const resumeEnd = source.indexOf(
    'phase = "initial-start";',
    resumeStart,
  );
  assert.notEqual(resumeStart, -1);
  assert.notEqual(resumeEnd, -1);
  const resume = source.slice(resumeStart, resumeEnd);

  assert.doesNotMatch(
    source,
    /processExists\((?:priorCreatorPid|creatorPid)\)/,
  );
  assert.doesNotMatch(
    source,
    /processGroupExists\(priorCreatorPid\)/,
  );
  assert.match(
    source,
    /processIdentity:\s*creatorIdentity/,
  );
  assert.match(
    source,
    /validateCreatorProcessIdentity\(/,
  );
  assert.match(
    resume,
    /persisted creator remains live after prior scope retirement/,
  );
  assert.doesNotMatch(resume, /process\.kill|terminate[A-Za-z]*Basecamp/);
  assert.match(
    resume,
    /delete report\.gate6\.recoveredCreatorProcess/,
  );
  assert.doesNotMatch(
    source,
    /(?:let|const)\s+recoveredCreatorProcess|recoveredCreatorProcess\s*=/,
  );
  assert.doesNotMatch(source, /async function terminateBasecampIdentity/);
  assert.doesNotMatch(
    source,
    /process\.kill\([^,\n]+,\s*["']SIG(?:TERM|KILL)["']\)/,
  );
  assert.doesNotMatch(source, /\.kill\(["']SIG(?:TERM|KILL)["']\)/);
});
