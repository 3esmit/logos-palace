#!/usr/bin/env node

import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import {
  access,
  chmod,
  mkdir,
  mkdtemp,
  readFile,
  rm,
  writeFile,
} from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const modulePath = fileURLToPath(
  new URL("./basecamp_release_lock.mjs", import.meta.url),
);
const flock = resolve(process.env.PALACE_FLOCK ?? "/usr/bin/flock");
const toolDirectory = dirname(flock);
const setpriv = join(toolDirectory, "setpriv");
const bash = join(toolDirectory, "bash");

async function waitFor(predicate, description) {
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

test("attests exact close-on-exec flock supervisor", async (t) => {
  const root = await mkdtemp(join(tmpdir(), "palace-release-lock-"));
  t.after(() => rm(root, { recursive: true, force: true }));
  const runsRoot = join(root, "runs");
  const runDirectory = join(runsRoot, "run.AB12cd34");
  const lockPath = join(root, "release.lock");
  const snapshotRunner = join(root, "snapshot-runner.sh");
  await mkdir(runDirectory, { recursive: true, mode: 0o700 });
  await writeFile(lockPath, "", { mode: 0o600 });
  await chmod(lockPath, 0o600);
  await writeFile(
    snapshotRunner,
    "#!/usr/bin/env bash\n"
      + `"${process.execPath}" "${modulePath}" attest-parent `
      + `"${lockPath}" "${flock}" "$0" "$1" "$2" "$$"\n`,
    { mode: 0o700 },
  );
  await chmod(snapshotRunner, 0o700);

  const result = spawnSync(
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
      snapshotRunner,
      runsRoot,
      runDirectory,
    ],
    {
      encoding: "utf8",
      timeout: 10_000,
    },
  );
  assert.equal(result.status, 0, result.stderr);
  const attestation = JSON.parse(result.stdout);
  assert.equal(attestation.schema, "logos.palace.release-lock-attestation");
  assert.equal(attestation.version, 1);
  assert.equal(attestation.lockPath, lockPath);
  assert.ok(attestation.supervisorPid > 1);
  assert.ok(attestation.supervisorStartTimeTicks > 0);
});

test("standalone attestation binds exact target lock and argv", async (t) => {
  const root = await mkdtemp(join(tmpdir(), "palace-standalone-lock-"));
  t.after(() => rm(root, { recursive: true, force: true }));
  const lockPath = join(root, "standalone.lock");
  const otherLock = join(root, "other.lock");
  const script = join(root, "standalone.sh");
  const productSnapshot = join(root, "snapshot");
  const acceptanceTools = join(root, "tools");
  const artifactsDirectory = join(root, "artifacts");
  const gateRunner = join(root, "gate-runner.sh");
  await Promise.all([
    mkdir(productSnapshot),
    mkdir(acceptanceTools),
    mkdir(artifactsDirectory),
    writeFile(lockPath, "", { mode: 0o600 }),
    writeFile(otherLock, "", { mode: 0o600 }),
    writeFile(gateRunner, "#!/bin/sh\nexit 0\n", { mode: 0o700 }),
  ]);
  await Promise.all([chmod(lockPath, 0o600), chmod(otherLock, 0o600)]);
  await writeFile(
    script,
    "#!/usr/bin/env bash\n"
      + `"${process.execPath}" "${modulePath}" attest-standalone `
      + `"${lockPath}" "${flock}" "$0" "$1" "$2" "$3" "$4" "$5" "$$"\n`,
    { mode: 0o700 },
  );
  await chmod(script, 0o700);
  const tail = [
    "gate1",
    productSnapshot,
    acceptanceTools,
    artifactsDirectory,
    gateRunner,
  ];
  const positive = spawnSync(
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
      script,
      ...tail,
    ],
    { encoding: "utf8", timeout: 10_000 },
  );
  assert.equal(positive.status, 0, positive.stderr);
  assert.equal(
    JSON.parse(positive.stdout).schema,
    "logos.palace.standalone-lock-attestation",
  );

  const targetHolder = spawn(
    flock,
    [
      "--exclusive",
      "--nonblock",
      "--",
      lockPath,
      join(toolDirectory, "sleep"),
      "30",
    ],
    { stdio: "ignore" },
  );
  t.after(() => {
    if (
      targetHolder.exitCode === null
      && targetHolder.signalCode === null
    ) {
      targetHolder.kill("SIGKILL");
    }
  });
  await waitFor(
    () =>
      spawnSync(
        flock,
        [
          "--exclusive",
          "--nonblock",
          "--conflict-exit-code",
          "75",
          "--",
          lockPath,
          join(toolDirectory, "true"),
        ],
        { stdio: "ignore" },
      ).status === 75,
    "separate target-lock holder",
  );
  const wrongLock = spawnSync(
    flock,
    [
      "--exclusive",
      "--nonblock",
      "--conflict-exit-code",
      "75",
      "--close",
      "--",
      otherLock,
      setpriv,
      "--pdeathsig",
      "KILL",
      bash,
      "-p",
      script,
      ...tail,
    ],
    { encoding: "utf8", timeout: 10_000 },
  );
  assert.notEqual(wrongLock.status, 0);
  assert.match(wrongLock.stderr, /supervisor chain is not exact/);
});

test("supervisor death kills runner and death-coupled mutator", async (t) => {
  const root = await mkdtemp(join(tmpdir(), "palace-release-lock-death-"));
  t.after(() => rm(root, { recursive: true, force: true }));
  const runsRoot = join(root, "runs");
  const runDirectory = join(runsRoot, "run.AB12cd34");
  const lockPath = join(root, "release.lock");
  const snapshotRunner = join(root, "snapshot-runner.sh");
  const mutator = join(root, "mutator.mjs");
  const runnerPidPath = join(root, "runner.pid");
  const mutatorPidPath = join(root, "mutator.pid");
  const lateMutation = join(root, "late-mutation");
  await mkdir(runDirectory, { recursive: true, mode: 0o700 });
  await writeFile(lockPath, "", { mode: 0o600 });
  await chmod(lockPath, 0o600);
  await writeFile(
    mutator,
    "import { writeFile } from 'node:fs/promises';\n"
      + "await writeFile(process.argv[2], `${process.pid}\\n`);\n"
      + "await new Promise((resolve) => setTimeout(resolve, 10_000));\n"
      + "await writeFile(process.argv[3], 'late\\n');\n",
    { mode: 0o600 },
  );
  await writeFile(
    snapshotRunner,
    "#!/usr/bin/env bash\n"
      + "set -euo pipefail\n"
      + `"${process.execPath}" "${modulePath}" attest-parent `
      + `"${lockPath}" "${flock}" "$0" "$1" "$2" "$$" >/dev/null\n`
      + `printf '%s\\n' "$$" >"${runnerPidPath}"\n`
      + `"${setpriv}" --pdeathsig KILL `
      + `"${bash}" -p -c '`
      + "expected_parent=\"$1\"; shift; "
      + "[ \"$PPID\" = \"$expected_parent\" ] || exit 125; "
      + "exec \"$@\"' palace-death-coupled-node \"$$\" "
      + `"${process.execPath}" "${mutator}" `
      + `"${mutatorPidPath}" "${lateMutation}"\n`,
    { mode: 0o700 },
  );
  await chmod(snapshotRunner, 0o700);

  const supervisor = spawn(
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
      snapshotRunner,
      runsRoot,
      runDirectory,
    ],
    { stdio: "ignore" },
  );
  t.after(() => {
    if (supervisor.exitCode === null && supervisor.signalCode === null) {
      supervisor.kill("SIGKILL");
    }
  });
  const supervisorExit = new Promise((resolveExit, rejectExit) => {
    supervisor.once("error", rejectExit);
    supervisor.once("exit", (code, signal) =>
      resolveExit({ code, signal }));
  });
  await waitFor(() => exists(mutatorPidPath), "death-coupled mutator");
  const runnerPid = Number((await readFile(runnerPidPath, "utf8")).trim());
  const mutatorPid = Number((await readFile(mutatorPidPath, "utf8")).trim());
  assert.ok(Number.isSafeInteger(runnerPid) && runnerPid > 1);
  assert.ok(Number.isSafeInteger(mutatorPid) && mutatorPid > 1);
  assert.notEqual(runnerPid, mutatorPid);

  supervisor.kill("SIGKILL");
  assert.deepEqual(await supervisorExit, { code: null, signal: "SIGKILL" });
  await waitFor(
    async () => !(await exists(`/proc/${runnerPid}`)),
    "runner death",
  );
  await waitFor(
    async () => !(await exists(`/proc/${mutatorPid}`)),
    "mutator death",
  );
  await new Promise((resolveWait) => setTimeout(resolveWait, 100));
  await assert.rejects(access(lateMutation), { code: "ENOENT" });

  const contender = spawnSync(
    flock,
    [
      "--exclusive",
      "--nonblock",
      "--conflict-exit-code",
      "75",
      "--",
      lockPath,
      join(toolDirectory, "true"),
    ],
    { stdio: "ignore", timeout: 10_000 },
  );
  assert.equal(contender.status, 0);
});
