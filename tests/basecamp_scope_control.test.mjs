#!/usr/bin/env node

import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import {
  access,
  chmod,
  link,
  mkdir,
  mkdtemp,
  readFile,
  rm,
  writeFile,
} from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const control = fileURLToPath(
  new URL("./basecamp_scope_control.mjs", import.meta.url),
);
const slice = "logos-palace-run-AB12cd34.slice";
const unit = "logos-palace-run-AB12cd34-gate2-Ef56Gh78.scope";
const sliceControlGroup =
  "/user.slice/user-1000.slice/user@1000.service/"
  + slice;
const controlGroup = `${sliceControlGroup}/${unit}`;

function launch() {
  return {
    schema: "logos.palace.basecamp-process-scope-launch",
    version: 1,
    status: "planned",
    unit,
    slice,
  };
}

function attested() {
  const cgroupPath = `/sys/fs/cgroup${controlGroup}`;
  const sliceCgroupPath = `/sys/fs/cgroup${sliceControlGroup}`;
  return {
    schema: "logos.palace.basecamp-process-scope",
    version: 1,
    status: "attested",
    unit,
    slice,
    controlGroup,
    attestedPid: 1234,
    attestedStartTimeTicks: 5678,
    barrier: "sigstop-before-exec",
    cgroupPath,
    eventsPath: `${cgroupPath}/cgroup.events`,
    killPath: `${cgroupPath}/cgroup.kill`,
    sliceControlGroup,
    sliceCgroupPath,
    sliceEventsPath: `${sliceCgroupPath}/cgroup.events`,
    sliceKillPath: `${sliceCgroupPath}/cgroup.kill`,
  };
}

async function writeEvidence(path, value) {
  await writeFile(path, `${JSON.stringify(value, null, 2)}\n`, {
    mode: 0o600,
  });
  await chmod(path, 0o600);
}

function invoke(...args) {
  return spawnSync(process.execPath, [control, ...args], {
    encoding: "utf8",
    maxBuffer: 64 * 1024,
  });
}

test("finishes only a matching attested launch", async (t) => {
  const gate = await mkdtemp(join(tmpdir(), "palace-scope-control-"));
  t.after(() => rm(gate, { recursive: true, force: true }));
  const launchPath = join(gate, "process-scope-launch.json");
  const evidencePath = join(gate, "process-scope.json");
  await writeEvidence(launchPath, launch());
  await writeEvidence(evidencePath, attested());

  const result = invoke("finish-launch", launchPath, evidencePath);
  assert.equal(result.status, 0, result.stderr);
  assert.equal(result.stdout, `${unit}\n`);
  await assert.rejects(access(launchPath), { code: "ENOENT" });
  assert.deepEqual(
    JSON.parse(await readFile(evidencePath, "utf8")),
    attested(),
  );
});

test("archives interrupted evidence without overwrite", async (t) => {
  const gate = await mkdtemp(join(tmpdir(), "palace-scope-control-"));
  t.after(() => rm(gate, { recursive: true, force: true }));
  const history = join(gate, "process-scope-history");
  const launchPath = join(gate, "process-scope-launch.json");
  const archivePath = join(history, `${unit}.launch.json`);
  await mkdir(history, { mode: 0o700 });
  await chmod(history, 0o700);
  await writeEvidence(launchPath, launch());

  const result = invoke("archive", launchPath, history);
  assert.equal(result.status, 0, result.stderr);
  assert.equal(result.stdout, `${archivePath}\n`);
  await assert.rejects(access(launchPath), { code: "ENOENT" });
  assert.deepEqual(
    JSON.parse(await readFile(archivePath, "utf8")),
    launch(),
  );

  await writeEvidence(launchPath, launch());
  const conflicting = invoke("archive", launchPath, history);
  assert.notEqual(conflicting.status, 0);
  assert.match(conflicting.stderr, /history target already differs/);
  await rm(launchPath);

  await link(archivePath, launchPath);
  const resumed = invoke("archive", launchPath, history);
  assert.equal(resumed.status, 0, resumed.stderr);
  await assert.rejects(access(launchPath), { code: "ENOENT" });
});
