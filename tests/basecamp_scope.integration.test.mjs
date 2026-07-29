#!/usr/bin/env node

import assert from "node:assert/strict";
import { randomBytes } from "node:crypto";
import { spawn, spawnSync } from "node:child_process";
import {
  access,
  mkdtemp,
  readFile,
  rm,
  writeFile,
} from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import {
  attestScope,
  cleanupAttestedScope,
  cleanupPlannedScope,
  releaseAttestedScopeBarrier,
  retireScopeSlice,
} from "./basecamp_scope.mjs";
import { retireClaimScope } from "./basecamp_active_scope_preflight.mjs";
import {
  releaseProgramId,
  releaseRootId,
} from "./basecamp_claim_lifecycle.mjs";

const live = process.env.PALACE_SCOPE_LIVE_TEST === "1";
const systemdRun = resolve(
  process.env.PALACE_SYSTEMD_RUN ?? "/usr/bin/systemd-run",
);
const systemctl = resolve(
  process.env.PALACE_SYSTEMCTL ?? "/usr/bin/systemctl",
);
const setsid = resolve(
  process.env.PALACE_SETSID ?? "/usr/bin/setsid",
);
const bash = resolve(
  process.env.PALACE_BASH ?? "/bin/bash",
);
const toolDirectory = dirname(systemdRun);
const nodeBin = resolve(
  process.env.PALACE_NODE ?? process.execPath,
);
const flock = join(toolDirectory, "flock");
const setpriv = join(toolDirectory, "setpriv");
const trueBin = join(toolDirectory, "true");
const sleepBin = join(toolDirectory, "sleep");
const scopeGuardian = fileURLToPath(
  new URL("./basecamp_scope_guardian.mjs", import.meta.url),
);

function exited(child) {
  return new Promise((resolvePromise, reject) => {
    child.once("error", reject);
    child.once("exit", (code, signal) =>
      resolvePromise({ code, signal }));
  });
}

async function waitForCondition(predicate, description) {
  const deadline = Date.now() + 10_000;
  while (Date.now() < deadline) {
    if (await predicate()) return;
    await new Promise((resolveWait) => setTimeout(resolveWait, 20));
  }
  throw new Error(`timed out waiting for ${description}`);
}

async function exists(path) {
  return access(path).then(
    () => true,
    (error) => {
      if (error?.code === "ENOENT") return false;
      throw error;
    },
  );
}

function activeRunClaim({ directory, runId, slice }) {
  return {
    schema: "logos.palace.basecamp-active-run-claim",
    version: 2,
    uid: process.getuid(),
    releaseProgramId,
    releaseRootId,
    runDirectory: join(directory, `run.${runId}`),
    productSnapshot:
      "/nix/store/00000000000000000000000000000000-product",
    gcRootPath: join(directory, "gc-root"),
    gcRootTarget:
      "/nix/store/00000000000000000000000000000000-product",
    gitCommit: "1".repeat(40),
    snapshotNarHash: `sha256-${"A".repeat(43)}=`,
    snapshotNarSize: 4096,
    snapshotRunnerSha256: "2".repeat(64),
    runtimeManifestPath: join(directory, "runtime-output-manifest.json"),
    runtimeManifestSha256: "3".repeat(64),
    processScopeSlice: slice,
    processScopePrefix: `logos-palace-run-${runId}`,
    status: "gate3-entered",
    createdAtUnixMs: 1,
    gate3EnteredAtUnixMs: 2,
  };
}

function startScope({ unit, slice, command, stdio, env }) {
  return spawn(
    systemdRun,
    [
      "--user",
      "--scope",
      "--quiet",
      "--collect",
      "--expand-environment=no",
      `--unit=${unit}`,
      `--slice=${slice}`,
      "--property=KillMode=control-group",
      setpriv,
      "--pdeathsig",
      "TERM",
      bash,
      "-p",
      "-c",
      'expected_parent="$1"; shift; trap - TERM; '
        + 'if [ "$PPID" != "$expected_parent" ]; then exit 125; fi; '
        + 'builtin kill -STOP "$$"; '
        + 'if [ "$PPID" != "$expected_parent" ]; then exit 125; fi; '
        + 'builtin exec "$@"',
      "palace-scope-barrier",
      String(process.pid),
      nodeBin,
      scopeGuardian,
      unit,
      slice,
      setsid,
      "--wait",
      bash,
      "-p",
      "-c",
      command,
    ],
    {
      stdio: stdio ?? ["ignore", "pipe", "pipe"],
      env,
    },
  );
}

async function retireSlices(slices) {
  for (const slice of slices) {
    await retireScopeSlice({ slice, systemctl });
  }
}

test(
  "live guardian kills daemonized residue before disarming",
  { skip: !live },
  async () => {
    const directory = await mkdtemp(join(tmpdir(), "palace-guardian-live-"));
    const runId = randomBytes(4).toString("hex");
    const slice = `logos-palace-run-${runId}.slice`;
    const unit =
      `logos-palace-run-${runId}-gate2-${randomBytes(4).toString("hex")}.scope`;
    const daemonPidPath = join(directory, "daemon.pid");
    const mutation = join(directory, "late-mutation");
    const daemonSource = [
      'const { writeFileSync } = require("node:fs");',
      `writeFileSync(${JSON.stringify(daemonPidPath)}, String(process.pid));`,
      'process.stdout.write("ready\\n");',
      `setTimeout(() => writeFileSync(${
        JSON.stringify(mutation)
      }, "late\\n"), 750);`,
    ].join("");
    const leaderSource = [
      'const { spawn } = require("node:child_process");',
      `const child = spawn(${JSON.stringify(nodeBin)}, [`,
      '"-e",',
      `${JSON.stringify(daemonSource)}],`,
      '{ detached: true, stdio: ["ignore", "pipe", "ignore"] });',
      'child.stdout.once("data", () => {',
      "child.stdout.destroy();",
      "child.unref();",
      "});",
    ].join("");
    let child;
    let daemonPid;
    try {
      child = startScope({
        unit,
        slice,
        command:
          `${JSON.stringify(nodeBin)} -e ${JSON.stringify(leaderSource)}`,
      });
      const childExit = exited(child);
      const attested = await attestScope({
        pid: child.pid,
        unit,
        slice,
        systemctl,
      });
      await releaseAttestedScopeBarrier(attested, { systemctl });
      await waitForCondition(
        () => exists(daemonPidPath),
        "daemonized scope descendant",
      );
      daemonPid = Number((await readFile(daemonPidPath, "utf8")).trim());
      assert.ok(Number.isSafeInteger(daemonPid) && daemonPid > 1);
      const result = await childExit;
      assert.notDeepEqual(result, { code: 0, signal: null });
      await waitForCondition(
        async () => !await exists(`/proc/${daemonPid}`),
        "guardian-retired daemon",
      );
      await new Promise((resolveWait) => setTimeout(resolveWait, 900));
      await assert.rejects(access(mutation), { code: "ENOENT" });
      await cleanupAttestedScope(attested, { systemctl });
    } finally {
      if (child?.exitCode === null && child?.signalCode === null) {
        child.kill("SIGKILL");
      }
      await retireSlices([slice]);
      await rm(directory, { recursive: true, force: true });
    }
  },
);

test(
  "live transient scopes preserve status and isolate recursive cleanup",
  { skip: !live },
  async () => {
    const directory = await mkdtemp(join(tmpdir(), "palace-scope-live-"));
    const runId = randomBytes(4).toString("hex");
    const sentinelRunId = randomBytes(4).toString("hex");
    const slice = `logos-palace-run-${runId}.slice`;
    const sentinelSlice = `logos-palace-run-${sentinelRunId}.slice`;
    const shortUnit =
      `logos-palace-run-${runId}-gate1-${randomBytes(4).toString("hex")}.scope`;
    const residueUnit =
      `logos-palace-run-${runId}-gate2-${randomBytes(4).toString("hex")}.scope`;
    const sentinelUnit =
      `logos-palace-run-${sentinelRunId}-gate3-${randomBytes(4).toString("hex")}.scope`;
    const children = [];
    const slices = new Set([slice, sentinelSlice]);
    try {
      const short = startScope({
        unit: shortUnit,
        slice,
        command: "printf 'scoped-output\\n'; sleep 1; exit 7",
      });
      children.push(short);
      const shortExit = exited(short);
      const shortEvidence = await attestScope({
        pid: short.pid,
        unit: shortUnit,
        slice,
        systemctl,
      });
      await releaseAttestedScopeBarrier(shortEvidence, { systemctl });
      let shortOutput = "";
      short.stdout.setEncoding("utf8");
      short.stdout.on("data", (chunk) => {
        shortOutput += chunk;
      });
      assert.deepEqual(await shortExit, { code: 7, signal: null });
      assert.equal(shortOutput, "scoped-output\n");
      const shortCleaned = await cleanupAttestedScope(shortEvidence, {
        systemctl,
        commandExitStatus: 7,
      });
      assert.equal(shortCleaned.cleanup.residueKilled, false);

      const sentinel = startScope({
        unit: sentinelUnit,
        slice: sentinelSlice,
        command: "sleep 30",
      });
      children.push(sentinel);
      const sentinelExit = exited(sentinel);
      const sentinelEvidence = await attestScope({
        pid: sentinel.pid,
        unit: sentinelUnit,
        slice: sentinelSlice,
        systemctl,
      });
      await releaseAttestedScopeBarrier(sentinelEvidence, { systemctl });

      const residue = startScope({
        unit: residueUnit,
        slice,
        command:
          "setsid sleep 30 >/dev/null 2>&1 & "
          + "setsid sh -c 'sleep 30' >/dev/null 2>&1 & sleep 1",
      });
      children.push(residue);
      const residueExit = exited(residue);
      const residueEvidence = await attestScope({
        pid: residue.pid,
        unit: residueUnit,
        slice,
        systemctl,
      });
      await releaseAttestedScopeBarrier(residueEvidence, { systemctl });
      assert.deepEqual(await residueExit, {
        code: null,
        signal: "SIGKILL",
      });
      const residueCleaned = await cleanupAttestedScope(residueEvidence, {
        systemctl,
        commandExitStatus: 137,
      });
      assert.equal(residueCleaned.cleanup.residueKilled, false);

      assert.equal(sentinel.exitCode, null);
      const sentinelCleaned = await cleanupAttestedScope(sentinelEvidence, {
        systemctl,
        commandExitStatus: 137,
      });
      assert.equal(sentinelCleaned.cleanup.residueKilled, true);
      const sentinelResult = await sentinelExit;
      assert.equal(sentinelResult.code, null);
      assert.equal(sentinelResult.signal, "SIGKILL");

      const plannedUnit =
        `logos-palace-run-${runId}-gate4-${randomBytes(4).toString("hex")}.scope`;
      const planned = startScope({
        unit: plannedUnit,
        slice,
        command: "sleep 30",
      });
      children.push(planned);
      const plannedExit = exited(planned);
      await attestScope({
        pid: planned.pid,
        unit: plannedUnit,
        slice,
        systemctl,
      });
      const recovered = await cleanupPlannedScope({
        unit: plannedUnit,
        slice,
        systemctl,
      });
      assert.equal(recovered.residueKilled, true);
      assert.deepEqual(await plannedExit, {
        code: null,
        signal: "SIGKILL",
      });
    } finally {
      for (const child of children) {
        if (child.exitCode === null && child.signalCode === null) {
          child.kill("SIGKILL");
        }
      }
      await retireSlices(slices);
      await rm(directory, { recursive: true, force: true });
    }
  },
);

test(
  "live barrier ignores ambient Bash startup hooks",
  { skip: !live },
  async () => {
    const directory = await mkdtemp(join(tmpdir(), "palace-scope-env-"));
    const hook = join(directory, "hostile-bash-env");
    const marker = join(directory, "hook-ran");
    const runId = randomBytes(4).toString("hex");
    const slice = `logos-palace-run-${runId}.slice`;
    const unit =
      `logos-palace-run-${runId}-gate1-${randomBytes(4).toString("hex")}.scope`;
    let child;
    try {
      await writeFile(
        hook,
        `printf hostile >"${marker}"\nkill() { :; }\n`,
        { mode: 0o600 },
      );
      child = startScope({
        unit,
        slice,
        command: "exit 0",
        env: {
          ...process.env,
          BASH_ENV: hook,
          "BASH_FUNC_kill%%": "() { :; }",
        },
      });
      const childExit = exited(child);
      const attested = await attestScope({
        pid: child.pid,
        unit,
        slice,
        systemctl,
      });
      await assert.rejects(access(marker), { code: "ENOENT" });
      await releaseAttestedScopeBarrier(attested, { systemctl });
      assert.deepEqual(await childExit, { code: 0, signal: null });
      await assert.rejects(access(marker), { code: "ENOENT" });
      const cleaned = await cleanupAttestedScope(attested, {
        systemctl,
        commandExitStatus: 0,
      });
      assert.equal(cleaned.cleanup.residueKilled, false);
    } finally {
      if (child?.exitCode === null && child?.signalCode === null) {
        child.kill("SIGKILL");
      }
      await retireSlices([slice]);
      await rm(directory, { recursive: true, force: true });
    }
  },
);

test(
  "live planned recovery retires populated slice after unit disappeared",
  { skip: !live },
  async () => {
    const runId = randomBytes(4).toString("hex");
    const slice = `logos-palace-run-${runId}.slice`;
    const residentUnit =
      `logos-palace-run-${runId}-gate1-${randomBytes(4).toString("hex")}.scope`;
    const absentUnit =
      `logos-palace-run-${runId}-gate4-${randomBytes(4).toString("hex")}.scope`;
    let child;
    try {
      child = startScope({
        unit: residentUnit,
        slice,
        command: "sleep 30",
      });
      const childExit = exited(child);
      const attested = await attestScope({
        pid: child.pid,
        unit: residentUnit,
        slice,
        systemctl,
      });
      assert.equal(attested.slice, slice);
      await releaseAttestedScopeBarrier(attested, { systemctl });

      const recovered = await cleanupPlannedScope({
        unit: absentUnit,
        slice,
        systemctl,
      });
      assert.deepEqual(recovered, { residueKilled: true });
      assert.deepEqual(await childExit, {
        code: null,
        signal: "SIGKILL",
      });
    } finally {
      if (child?.exitCode === null && child?.signalCode === null) {
        child.kill("SIGKILL");
      }
      await retireSlices([slice]);
    }
  },
);

test(
  "live active-claim preflight retires stale slice before next mutation",
  { skip: !live },
  async () => {
    const directory = await mkdtemp(join(tmpdir(), "palace-preflight-live-"));
    const runId = randomBytes(4).toString("hex");
    const slice = `logos-palace-run-${runId}.slice`;
    const unit =
      `logos-palace-run-${runId}-gate3-${randomBytes(4).toString("hex")}.scope`;
    const mutation = join(directory, "next-mutation");
    let child;
    try {
      child = startScope({
        unit,
        slice,
        command: "sleep 30",
      });
      const childExit = exited(child);
      const attested = await attestScope({
        pid: child.pid,
        unit,
        slice,
        systemctl,
      });
      await releaseAttestedScopeBarrier(attested, { systemctl });
      const claim = activeRunClaim({ directory, runId, slice });
      const result = await retireClaimScope({
        claim,
        claimPath: join(directory, "active.json"),
        uid: process.getuid(),
        systemctl,
      });
      assert.equal(result, "residue-killed");
      assert.deepEqual(await childExit, {
        code: null,
        signal: "SIGKILL",
      });
      await assert.rejects(access(`/proc/${child.pid}`), { code: "ENOENT" });
      await writeFile(mutation, "after-retirement\n", {
        flag: "wx",
        mode: 0o600,
      });
      assert.equal(
        await access(mutation).then(() => true),
        true,
      );
    } finally {
      if (child?.exitCode === null && child?.signalCode === null) {
        child.kill("SIGKILL");
      }
      await retireSlices([slice]);
      await rm(directory, { recursive: true, force: true });
    }
  },
);

test(
  "live supervisor death at STOP barrier stays non-mutating until preflight",
  { skip: !live },
  async () => {
    const directory = await mkdtemp(join(tmpdir(), "palace-stop-live-"));
    const runId = randomBytes(4).toString("hex");
    const slice = `logos-palace-run-${runId}.slice`;
    const unit =
      `logos-palace-run-${runId}-gate3-${randomBytes(4).toString("hex")}.scope`;
    const lockPath = join(directory, "release.lock");
    const runnerPath = join(directory, "runner.sh");
    const barrierPath = join(directory, "barrier.sh");
    const gatePath = join(directory, "gate.sh");
    const runnerPidPath = join(directory, "runner.pid");
    const childPidPath = join(directory, "scope.pid");
    const mutation = join(directory, "forbidden-mutation");
    let supervisor;
    let childPid;
    try {
      await writeFile(lockPath, "", { mode: 0o600 });
      await writeFile(
        barrierPath,
        "#!/usr/bin/env bash\n"
          + "set -euo pipefail\n"
          + 'expected_parent="$1"\n'
          + "shift\n"
          + "trap - TERM\n"
          + 'if [ "$PPID" != "$expected_parent" ]; then exit 125; fi\n'
          + 'builtin kill -STOP "$$"\n'
          + 'if [ "$PPID" != "$expected_parent" ]; then exit 125; fi\n'
          + 'builtin exec "$@"\n',
        { mode: 0o700 },
      );
      await writeFile(
        gatePath,
        "#!/usr/bin/env bash\n"
          + "set -euo pipefail\n"
          + `printf 'forbidden\\n' >${JSON.stringify(mutation)}\n`,
        { mode: 0o700 },
      );
      await writeFile(
        runnerPath,
        "#!/usr/bin/env bash\n"
          + "set -euo pipefail\n"
          + `printf '%s\\n' "$$" >${JSON.stringify(runnerPidPath)}\n`
          + `${JSON.stringify(systemdRun)} --user --scope --quiet `
          + "--collect --expand-environment=no "
          + `--unit=${JSON.stringify(unit)} `
          + `--slice=${JSON.stringify(slice)} `
          + "--property=KillMode=control-group "
          + `${JSON.stringify(setpriv)} --pdeathsig TERM `
          + `${JSON.stringify(bash)} -p ${JSON.stringify(barrierPath)} `
          + '"$$" '
          + `${JSON.stringify(nodeBin)} ${JSON.stringify(scopeGuardian)} `
          + `${JSON.stringify(unit)} ${JSON.stringify(slice)} `
          + `${JSON.stringify(setsid)} --wait `
          + `${JSON.stringify(bash)} -p ${JSON.stringify(gatePath)} &\n`
          + "scope_pid=$!\n"
          + `printf '%s\\n' "$scope_pid" >${JSON.stringify(childPidPath)}\n`
          + 'wait "$scope_pid"\n',
        { mode: 0o700 },
      );
      supervisor = spawn(
        flock,
        [
          "--exclusive",
          "--nonblock",
          "--conflict-exit-code",
          "75",
          "--close",
          "--",
          lockPath,
          setpriv,
          "--pdeathsig",
          "KILL",
          bash,
          "-p",
          runnerPath,
        ],
        { stdio: "ignore" },
      );
      const supervisorExit = exited(supervisor);
      await waitForCondition(
        () => exists(childPidPath),
        "stopped scope PID",
      );
      const runnerPid = Number(
        (await readFile(runnerPidPath, "utf8")).trim(),
      );
      childPid = Number((await readFile(childPidPath, "utf8")).trim());
      assert.ok(Number.isSafeInteger(runnerPid) && runnerPid > 1);
      assert.ok(Number.isSafeInteger(childPid) && childPid > 1);
      await attestScope({
        pid: childPid,
        unit,
        slice,
        systemctl,
      });

      supervisor.kill("SIGKILL");
      assert.deepEqual(await supervisorExit, {
        code: null,
        signal: "SIGKILL",
      });
      await waitForCondition(
        async () => !await exists(`/proc/${runnerPid}`),
        "death-coupled runner exit",
      );
      assert.equal(await exists(`/proc/${childPid}`), true);
      assert.match(
        await readFile(`/proc/${childPid}/status`, "utf8"),
        /^State:\s+T\b/m,
      );
      await new Promise((resolveWait) => setTimeout(resolveWait, 250));
      await assert.rejects(access(mutation), { code: "ENOENT" });

      assert.equal(
        await retireClaimScope({
          claim: activeRunClaim({ directory, runId, slice }),
          claimPath: join(directory, "active.json"),
          uid: process.getuid(),
          systemctl,
        }),
        "residue-killed",
      );
      await waitForCondition(
        async () => !await exists(`/proc/${childPid}`),
        "preflight-retired stopped scope",
      );
      await assert.rejects(access(mutation), { code: "ENOENT" });
    } finally {
      if (
        supervisor?.exitCode === null
        && supervisor?.signalCode === null
      ) {
        supervisor.kill("SIGKILL");
      }
      await retireSlices([slice]);
      await rm(directory, { recursive: true, force: true });
    }
  },
);

test(
  "live supervisor death kills released scope before delayed mutation",
  { skip: !live },
  async () => {
    const directory = await mkdtemp(join(tmpdir(), "palace-takeover-live-"));
    const runId = randomBytes(4).toString("hex");
    const slice = `logos-palace-run-${runId}.slice`;
    const unit =
      `logos-palace-run-${runId}-gate4-${randomBytes(4).toString("hex")}.scope`;
    const lockPath = join(directory, "release.lock");
    const runnerPath = join(directory, "runner.sh");
    const barrierPath = join(directory, "barrier.sh");
    const gatePath = join(directory, "gate.sh");
    const runnerPidPath = join(directory, "runner.pid");
    const childPidPath = join(directory, "scope.pid");
    const gateStarted = join(directory, "gate.started");
    const takeoverReady = join(directory, "takeover.ready");
    const mutation = join(directory, "next-mutation");
    let supervisor;
    let takeover;
    let takeoverExit;
    let childPid;
    try {
      await writeFile(lockPath, "", { mode: 0o600 });
      await writeFile(
        barrierPath,
        "#!/usr/bin/env bash\n"
          + "set -euo pipefail\n"
          + 'expected_parent="$1"\n'
          + "shift\n"
          + "trap - TERM\n"
          + 'if [ "$PPID" != "$expected_parent" ]; then exit 125; fi\n'
          + 'builtin kill -STOP "$$"\n'
          + 'if [ "$PPID" != "$expected_parent" ]; then exit 125; fi\n'
          + 'builtin exec "$@"\n',
        { mode: 0o700 },
      );
      await writeFile(
        gatePath,
        "#!/usr/bin/env bash\n"
          + "set -euo pipefail\n"
          + `printf 'started\\n' >${JSON.stringify(gateStarted)}\n`
          + `${JSON.stringify(sleepBin)} 1\n`
          + `printf 'late\\n' >${JSON.stringify(mutation)}\n`,
        { mode: 0o700 },
      );
      await writeFile(
        runnerPath,
        "#!/usr/bin/env bash\n"
          + "set -euo pipefail\n"
          + `printf '%s\\n' "$$" >${JSON.stringify(runnerPidPath)}\n`
          + `${JSON.stringify(systemdRun)} --user --scope --quiet `
          + "--collect --expand-environment=no "
          + `--unit=${JSON.stringify(unit)} `
          + `--slice=${JSON.stringify(slice)} `
          + "--property=KillMode=control-group "
          + `${JSON.stringify(setpriv)} --pdeathsig TERM `
          + `${JSON.stringify(bash)} -p ${JSON.stringify(barrierPath)} `
          + '"$$" '
          + `${JSON.stringify(nodeBin)} ${JSON.stringify(scopeGuardian)} `
          + `${JSON.stringify(unit)} ${JSON.stringify(slice)} `
          + `${JSON.stringify(setsid)} --wait `
          + `${JSON.stringify(bash)} -p ${JSON.stringify(gatePath)} &\n`
          + "scope_pid=$!\n"
          + `printf '%s\\n' "$scope_pid" >${JSON.stringify(childPidPath)}\n`
          + 'wait "$scope_pid"\n',
        { mode: 0o700 },
      );
      supervisor = spawn(
        flock,
        [
          "--exclusive",
          "--nonblock",
          "--conflict-exit-code",
          "75",
          "--close",
          "--",
          lockPath,
          setpriv,
          "--pdeathsig",
          "KILL",
          bash,
          "-p",
          runnerPath,
        ],
        { stdio: "ignore" },
      );
      const supervisorExit = exited(supervisor);
      await waitForCondition(
        () => exists(childPidPath),
        "orphanable scope PID",
      );
      const runnerPid = Number(
        (await readFile(runnerPidPath, "utf8")).trim(),
      );
      childPid = Number((await readFile(childPidPath, "utf8")).trim());
      assert.ok(Number.isSafeInteger(runnerPid) && runnerPid > 1);
      assert.ok(Number.isSafeInteger(childPid) && childPid > 1);
      const attested = await attestScope({
        pid: childPid,
        unit,
        slice,
        systemctl,
      });
      await releaseAttestedScopeBarrier(attested, { systemctl });
      await waitForCondition(
        () => exists(gateStarted),
        "released gate command",
      );

      supervisor.kill("SIGKILL");
      assert.deepEqual(await supervisorExit, {
        code: null,
        signal: "SIGKILL",
      });
      await waitForCondition(
        async () => !await exists(`/proc/${runnerPid}`),
        "death-coupled runner exit",
      );
      await waitForCondition(
        async () => !await exists(`/proc/${childPid}`),
        "guardian cgroup exit",
      );
      await new Promise((resolveWait) => setTimeout(resolveWait, 1_250));
      await assert.rejects(access(mutation), { code: "ENOENT" });

      takeover = spawn(
        flock,
        [
          "--exclusive",
          "--nonblock",
          "--conflict-exit-code",
          "75",
          "--close",
          "--",
          lockPath,
          setpriv,
          "--pdeathsig",
          "KILL",
          bash,
          "-p",
          "-c",
          'printf "ready\\n" >"$1"; exec "$2" 30',
          "palace-lock-takeover",
          takeoverReady,
          sleepBin,
        ],
        { stdio: "ignore" },
      );
      takeoverExit = exited(takeover);
      await waitForCondition(
        () => exists(takeoverReady),
        "release lock takeover",
      );
      const blockedContender = spawnSync(
        flock,
        [
          "--exclusive",
          "--nonblock",
          "--conflict-exit-code",
          "75",
          "--",
          lockPath,
          trueBin,
        ],
        { stdio: "ignore", timeout: 10_000 },
      );
      assert.equal(blockedContender.status, 75);

      const claim = activeRunClaim({ directory, runId, slice });
      assert.match(
        await retireClaimScope({
          claim,
          claimPath: join(directory, "active.json"),
          uid: process.getuid(),
          systemctl,
        }),
        /^(?:retired-clean|residue-killed)$/,
      );
      await writeFile(mutation, "after-retirement\n", {
        flag: "wx",
        mode: 0o600,
      });
      takeover.kill("SIGKILL");
      assert.deepEqual(await takeoverExit, {
        code: null,
        signal: "SIGKILL",
      });
    } finally {
      if (
        supervisor?.exitCode === null
        && supervisor?.signalCode === null
      ) {
        supervisor.kill("SIGKILL");
      }
      if (takeover?.exitCode === null && takeover?.signalCode === null) {
        takeover.kill("SIGKILL");
        await takeoverExit?.catch(() => {});
      }
      await retireSlices([slice]);
      await rm(directory, { recursive: true, force: true });
    }
  },
);
