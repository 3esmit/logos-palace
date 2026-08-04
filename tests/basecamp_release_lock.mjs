import { spawnSync } from "node:child_process";
import {
  lstat,
  readFile,
  readdir,
  realpath,
  stat,
} from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const maximumProcBytes = 64 * 1024;

function bounded(bytes, maximum, description) {
  if (bytes.length <= 0 || bytes.length > maximum) {
    throw new Error(`${description} exceeds release-lock bounds`);
  }
  return bytes;
}

function processIdentity(bytes, pid) {
  const encoded = bounded(bytes, 4096, "supervisor stat")
    .toString("utf8")
    .trim();
  const close = encoded.lastIndexOf(")");
  if (!encoded.startsWith(`${pid} (`) || close < 3) {
    throw new Error("release-lock supervisor stat is invalid");
  }
  const fields = encoded.slice(close + 2).split(" ");
  const parentPid = Number(fields[1]);
  const startTimeTicks = Number(fields[19]);
  if (
    !Number.isSafeInteger(parentPid)
    || parentPid < 0
    || !Number.isSafeInteger(startTimeTicks)
    || startTimeTicks <= 0
  ) {
    throw new Error("release-lock supervisor identity is invalid");
  }
  return { parentPid, startTimeTicks };
}

function argvFrom(bytes, length, description) {
  const encoded = bounded(
    bytes,
    maximumProcBytes,
    description,
  ).toString("utf8");
  if (!encoded.endsWith("\0")) {
    throw new Error(`${description} is invalid`);
  }
  const argv = encoded.slice(0, -1).split("\0");
  if (argv.length !== length || argv.some((value) => value.length === 0)) {
    throw new Error(`${description} is not exact`);
  }
  return argv;
}

function effectiveUid(bytes, description) {
  return Number(
    bounded(bytes, maximumProcBytes, description)
      .toString("utf8")
      .match(/^Uid:\s+[0-9]+\s+([0-9]+)\s+[0-9]+\s+[0-9]+$/m)?.[1],
  );
}

async function lockDescriptorCount(pid, lockMetadata) {
  const fdRoot = `/proc/${pid}/fd`;
  const entries = await readdir(fdRoot, { withFileTypes: true });
  if (entries.length > 1024) {
    throw new Error("release-lock descriptor inventory exceeds bound");
  }
  let matches = 0;
  for (const entry of entries) {
    if (!entry.isSymbolicLink() || !/^(?:0|[1-9][0-9]*)$/.test(entry.name)) {
      continue;
    }
    try {
      const metadata = await stat(join(fdRoot, entry.name), { bigint: true });
      if (
        metadata.dev === lockMetadata.dev
        && metadata.ino === lockMetadata.ino
      ) {
        matches += 1;
      }
    } catch (error) {
      if (error?.code !== "ENOENT") throw error;
    }
  }
  return matches;
}

async function assertKernelLock(pid, lockMetadata) {
  const expectedMajor =
    ((lockMetadata.dev >> 8n) & 0xfffn)
    | ((lockMetadata.dev >> 32n) & 0xfffff000n);
  const expectedMinor =
    (lockMetadata.dev & 0xffn)
    | ((lockMetadata.dev >> 12n) & 0xffffff00n);
  const rows = bounded(
    await readFile("/proc/locks"),
    16 * 1024 * 1024,
    "kernel lock inventory",
  ).toString("utf8").split("\n");
  if (rows.pop() !== "") {
    throw new Error("kernel lock inventory is malformed");
  }
  const matching = rows.filter((row) => {
    const match = row.match(
      new RegExp(
        "^[0-9]+: FLOCK\\s+ADVISORY\\s+WRITE\\s+"
          + "([0-9]+)\\s+([0-9a-fA-F]+):([0-9a-fA-F]+):"
          + "([0-9]+) 0 EOF$",
      ),
    );
    return Number(match?.[1]) === pid
      && BigInt(`0x${match?.[2] ?? "0"}`) === expectedMajor
      && BigInt(`0x${match?.[3] ?? "0"}`) === expectedMinor
      && BigInt(match?.[4] ?? "-1") === lockMetadata.ino;
  });
  if (matching.length !== 1) {
    throw new Error("exact kernel FLOCK ownership is absent");
  }
}

function assertContended(flock, lockPath) {
  const trueProgram = join(dirname(flock), "true");
  const result = spawnSync(
    flock,
    [
      "--exclusive",
      "--nonblock",
      "--conflict-exit-code",
      "75",
      "--",
      lockPath,
      trueProgram,
    ],
    {
      stdio: "ignore",
      timeout: 10_000,
    },
  );
  if (result.error || result.signal || result.status !== 75) {
    throw new Error("release lock is not exclusively contended");
  }
}

export async function verifyReleaseLock({
  lockPath,
  flock,
  supervisorPid,
  supervisorStartTimeTicks,
  snapshotRunner,
  runsRoot,
  runDirectory,
  runnerPid,
}) {
  const uid = process.getuid?.();
  if (
    !Number.isSafeInteger(uid)
    || uid < 0
    || !Number.isSafeInteger(supervisorPid)
    || supervisorPid <= 1
    || (
      supervisorStartTimeTicks !== undefined
      && (
        !Number.isSafeInteger(supervisorStartTimeTicks)
        || supervisorStartTimeTicks <= 0
      )
    )
    || resolve(lockPath ?? "") !== lockPath
    || resolve(flock ?? "") !== flock
    || resolve(snapshotRunner ?? "") !== snapshotRunner
    || resolve(runsRoot ?? "") !== runsRoot
    || resolve(runDirectory ?? "") !== runDirectory
    || dirname(runDirectory) !== runsRoot
    || (
      runnerPid !== undefined
      && (
        !Number.isSafeInteger(runnerPid)
        || runnerPid <= 1
        || process.ppid !== runnerPid
      )
    )
  ) {
    throw new TypeError("release-lock verification inputs are invalid");
  }

  const lockMetadata = await stat(lockPath, { bigint: true });
  const lockPathMetadata = await lstat(lockPath);
  const flockCanonical = await realpath(flock);
  const supervisorExe = await realpath(`/proc/${supervisorPid}/exe`);
  if (
    lockPathMetadata.isSymbolicLink()
    || !lockPathMetadata.isFile()
    || lockPathMetadata.uid !== uid
    || (lockPathMetadata.mode & 0o777) !== 0o600
    || await realpath(lockPath) !== lockPath
    || supervisorExe !== flockCanonical
  ) {
    throw new Error("release-lock file or supervisor identity is invalid");
  }

  const [beforeStat, statusBytes, commandBytes] = await Promise.all([
    readFile(`/proc/${supervisorPid}/stat`),
    readFile(`/proc/${supervisorPid}/status`),
    readFile(`/proc/${supervisorPid}/cmdline`),
  ]);
  const before = processIdentity(beforeStat, supervisorPid);
  const supervisorUid = effectiveUid(statusBytes, "supervisor status");
  const argv = argvFrom(
    commandBytes,
    16,
    "release-lock supervisor command line",
  );
  const setpriv = join(dirname(flock), "setpriv");
  const bash = join(dirname(flock), "bash");
  const bashCanonical = await realpath(bash);
  if (
    supervisorUid !== uid
    || (
      supervisorStartTimeTicks !== undefined
      && before.startTimeTicks !== supervisorStartTimeTicks
    )
    || await realpath(argv[0]) !== flockCanonical
    || argv[1] !== "--exclusive"
    || argv[2] !== "--nonblock"
    || argv[3] !== "--conflict-exit-code"
    || argv[4] !== "75"
    || argv[5] !== "--close"
    || argv[6] !== "--"
    || argv[7] !== lockPath
    || await realpath(argv[8]) !== await realpath(setpriv)
    || argv[9] !== "--pdeathsig"
    || argv[10] !== "KILL"
    || await realpath(argv[11]) !== bashCanonical
    || argv[12] !== "-p"
    || argv[13] !== snapshotRunner
    || argv[14] !== runsRoot
    || argv[15] !== runDirectory
  ) {
    throw new Error("release-lock supervisor command is not exact");
  }

  let runnerBefore;
  if (runnerPid !== undefined) {
    const [
      runnerStat,
      runnerStatus,
      runnerCommand,
      runnerExecutable,
    ] = await Promise.all([
      readFile(`/proc/${runnerPid}/stat`),
      readFile(`/proc/${runnerPid}/status`),
      readFile(`/proc/${runnerPid}/cmdline`),
      realpath(`/proc/${runnerPid}/exe`),
    ]);
    runnerBefore = processIdentity(runnerStat, runnerPid);
    const runnerArgv = argvFrom(
      runnerCommand,
      5,
      "release-lock runner command line",
    );
    if (
      runnerBefore.parentPid !== supervisorPid
      || effectiveUid(runnerStatus, "runner status") !== uid
      || runnerExecutable !== bashCanonical
      || await realpath(runnerArgv[0]) !== bashCanonical
      || runnerArgv[1] !== "-p"
      || runnerArgv[2] !== snapshotRunner
      || runnerArgv[3] !== runsRoot
      || runnerArgv[4] !== runDirectory
    ) {
      throw new Error("release-lock runner parent chain is not exact");
    }
  }

  if (
    await lockDescriptorCount(supervisorPid, lockMetadata) !== 1
    || await lockDescriptorCount(process.pid, lockMetadata) !== 0
    || (
      runnerPid !== undefined
      && await lockDescriptorCount(runnerPid, lockMetadata) !== 0
    )
  ) {
    throw new Error("release-lock descriptor ownership is invalid");
  }
  await assertKernelLock(supervisorPid, lockMetadata);
  assertContended(flock, lockPath);

  const after = processIdentity(
    await readFile(`/proc/${supervisorPid}/stat`),
    supervisorPid,
  );
  const [
    afterLockMetadata,
    afterLockPathMetadata,
    afterSupervisorExecutable,
    afterSupervisorCommand,
  ] = await Promise.all([
    stat(lockPath, { bigint: true }),
    lstat(lockPath),
    realpath(`/proc/${supervisorPid}/exe`),
    readFile(`/proc/${supervisorPid}/cmdline`),
  ]);
  if (
    after.startTimeTicks !== before.startTimeTicks
    || afterLockMetadata.dev !== lockMetadata.dev
    || afterLockMetadata.ino !== lockMetadata.ino
    || afterLockPathMetadata.isSymbolicLink()
    || !afterLockPathMetadata.isFile()
    || afterLockPathMetadata.uid !== uid
    || (afterLockPathMetadata.mode & 0o777) !== 0o600
    || afterSupervisorExecutable !== flockCanonical
    || !afterSupervisorCommand.equals(commandBytes)
    || await lockDescriptorCount(supervisorPid, lockMetadata) !== 1
    || await lockDescriptorCount(process.pid, lockMetadata) !== 0
    || (
      runnerPid !== undefined
      && await lockDescriptorCount(runnerPid, lockMetadata) !== 0
    )
  ) {
    throw new Error("release-lock supervisor changed during attestation");
  }
  await assertKernelLock(supervisorPid, lockMetadata);
  if (runnerBefore) {
    const runnerAfter = processIdentity(
      await readFile(`/proc/${runnerPid}/stat`),
      runnerPid,
    );
    if (
      runnerAfter.startTimeTicks !== runnerBefore.startTimeTicks
      || runnerAfter.parentPid !== supervisorPid
    ) {
      throw new Error("release-lock runner changed during attestation");
    }
  }
  return {
    schema: "logos.palace.release-lock-attestation",
    version: 1,
    lockPath,
    supervisorPid,
    supervisorStartTimeTicks: before.startTimeTicks,
    supervisorExecutable: flockCanonical,
  };
}

export async function verifyStandaloneLock({
  lockPath,
  flock,
  supervisorPid,
  script,
  gate,
  productSnapshot,
  acceptanceTools,
  artifactsDirectory,
  gateRunner,
  runnerPid,
}) {
  const uid = process.getuid?.();
  const absoluteInputs = [
    lockPath,
    flock,
    script,
    productSnapshot,
    acceptanceTools,
    artifactsDirectory,
    gateRunner,
  ];
  if (
    !Number.isSafeInteger(uid)
    || uid < 0
    || !Number.isSafeInteger(supervisorPid)
    || supervisorPid <= 1
    || !Number.isSafeInteger(runnerPid)
    || runnerPid <= 1
    || process.ppid !== runnerPid
    || !["gate1", "gate2"].includes(gate)
    || absoluteInputs.some((value) => resolve(value ?? "") !== value)
  ) {
    throw new TypeError("standalone-lock verification inputs are invalid");
  }
  const lockMetadata = await stat(lockPath, { bigint: true });
  const lockPathMetadata = await lstat(lockPath);
  const flockCanonical = await realpath(flock);
  const bash = join(dirname(flock), "bash");
  const setpriv = join(dirname(flock), "setpriv");
  const bashCanonical = await realpath(bash);
  const [
    supervisorExecutable,
    supervisorStat,
    supervisorStatus,
    supervisorCommand,
    runnerStat,
    runnerStatus,
    runnerCommand,
    runnerExecutable,
  ] = await Promise.all([
    realpath(`/proc/${supervisorPid}/exe`),
    readFile(`/proc/${supervisorPid}/stat`),
    readFile(`/proc/${supervisorPid}/status`),
    readFile(`/proc/${supervisorPid}/cmdline`),
    readFile(`/proc/${runnerPid}/stat`),
    readFile(`/proc/${runnerPid}/status`),
    readFile(`/proc/${runnerPid}/cmdline`),
    realpath(`/proc/${runnerPid}/exe`),
  ]);
  const supervisorBefore = processIdentity(supervisorStat, supervisorPid);
  const runnerBefore = processIdentity(runnerStat, runnerPid);
  const supervisorArgv = argvFrom(
    supervisorCommand,
    19,
    "standalone-lock supervisor command line",
  );
  const runnerArgv = argvFrom(
    runnerCommand,
    8,
    "standalone-lock runner command line",
  );
  const exactTail = [
    script,
    gate,
    productSnapshot,
    acceptanceTools,
    artifactsDirectory,
    gateRunner,
  ];
  if (
    lockPathMetadata.isSymbolicLink()
    || !lockPathMetadata.isFile()
    || lockPathMetadata.uid !== uid
    || (lockPathMetadata.mode & 0o777) !== 0o600
    || await realpath(lockPath) !== lockPath
    || supervisorExecutable !== flockCanonical
    || effectiveUid(supervisorStatus, "standalone supervisor status") !== uid
    || await realpath(supervisorArgv[0]) !== flockCanonical
    || supervisorArgv.slice(1, 8).join("\0")
      !== [
        "--exclusive",
        "--nonblock",
        "--conflict-exit-code",
        "75",
        "--close",
        "--",
        lockPath,
      ].join("\0")
    || await realpath(supervisorArgv[8]) !== await realpath(setpriv)
    || supervisorArgv[9] !== "--pdeathsig"
    || supervisorArgv[10] !== "KILL"
    || await realpath(supervisorArgv[11]) !== bashCanonical
    || supervisorArgv[12] !== "-p"
    || supervisorArgv.slice(13).join("\0") !== exactTail.join("\0")
    || runnerBefore.parentPid !== supervisorPid
    || effectiveUid(runnerStatus, "standalone runner status") !== uid
    || runnerExecutable !== bashCanonical
    || await realpath(runnerArgv[0]) !== bashCanonical
    || runnerArgv[1] !== "-p"
    || runnerArgv.slice(2).join("\0") !== exactTail.join("\0")
  ) {
    throw new Error("standalone-lock supervisor chain is not exact");
  }
  if (
    await lockDescriptorCount(supervisorPid, lockMetadata) !== 1
    || await lockDescriptorCount(runnerPid, lockMetadata) !== 0
    || await lockDescriptorCount(process.pid, lockMetadata) !== 0
  ) {
    throw new Error("standalone-lock descriptor ownership is invalid");
  }
  await assertKernelLock(supervisorPid, lockMetadata);
  assertContended(flock, lockPath);
  const [
    supervisorAfterStat,
    supervisorAfterCommand,
    runnerAfterStat,
    runnerAfterCommand,
    afterLockMetadata,
  ] = await Promise.all([
    readFile(`/proc/${supervisorPid}/stat`),
    readFile(`/proc/${supervisorPid}/cmdline`),
    readFile(`/proc/${runnerPid}/stat`),
    readFile(`/proc/${runnerPid}/cmdline`),
    stat(lockPath, { bigint: true }),
  ]);
  const supervisorAfter = processIdentity(
    supervisorAfterStat,
    supervisorPid,
  );
  const runnerAfter = processIdentity(runnerAfterStat, runnerPid);
  if (
    supervisorAfter.startTimeTicks !== supervisorBefore.startTimeTicks
    || runnerAfter.startTimeTicks !== runnerBefore.startTimeTicks
    || runnerAfter.parentPid !== supervisorPid
    || !supervisorAfterCommand.equals(supervisorCommand)
    || !runnerAfterCommand.equals(runnerCommand)
    || afterLockMetadata.dev !== lockMetadata.dev
    || afterLockMetadata.ino !== lockMetadata.ino
    || await lockDescriptorCount(supervisorPid, lockMetadata) !== 1
    || await lockDescriptorCount(runnerPid, lockMetadata) !== 0
    || await lockDescriptorCount(process.pid, lockMetadata) !== 0
  ) {
    throw new Error("standalone-lock supervision changed during attestation");
  }
  await assertKernelLock(supervisorPid, lockMetadata);
  return {
    schema: "logos.palace.standalone-lock-attestation",
    version: 1,
    lockPath,
    supervisorPid,
    supervisorStartTimeTicks: supervisorBefore.startTimeTicks,
  };
}

const invokedPath = process.argv[1]
  ? await realpath(resolve(process.argv[1])).catch(() => undefined)
  : undefined;
if (
  invokedPath
  && pathToFileURL(invokedPath).href === import.meta.url
) {
  const [
    command,
    lockPath,
    flock,
    snapshotRunner,
    runsRoot,
    runDirectory,
    runnerPidArgument,
  ] = process.argv.slice(2);
  if (command === "attest-standalone") {
    const [
      lockPath,
      flock,
      script,
      gate,
      productSnapshot,
      acceptanceTools,
      artifactsDirectory,
      gateRunner,
      runnerPidArgument,
    ] = process.argv.slice(3);
    if (
      process.argv.slice(3).length !== 9
      || !lockPath
      || !flock
      || !script
      || !gate
      || !productSnapshot
      || !acceptanceTools
      || !artifactsDirectory
      || !gateRunner
    ) {
      throw new Error(
        "usage: basecamp_release_lock.mjs attest-standalone "
          + "<lock> <flock> <script> <gate> <snapshot> <tools> "
          + "<artifacts> <gate-runner> <runner-pid>",
      );
    }
    const runnerPid = Number(runnerPidArgument);
    const supervisorPid = processIdentity(
      await readFile(`/proc/${runnerPid}/stat`),
      runnerPid,
    ).parentPid;
    const result = await verifyStandaloneLock({
      lockPath,
      flock,
      supervisorPid,
      script,
      gate,
      productSnapshot,
      acceptanceTools,
      artifactsDirectory,
      gateRunner,
      runnerPid,
    });
    process.stdout.write(`${JSON.stringify(result)}\n`);
    process.exit(0);
  }
  const fromEnvironment = command === "verify-environment";
  if (
    !["attest-parent", "verify-environment"].includes(command)
    || !lockPath
    || !flock
    || !snapshotRunner
    || !runsRoot
    || !runDirectory
    || (
      fromEnvironment
        ? process.argv.slice(2).length !== 6
        : process.argv.slice(2).length !== 7
    )
  ) {
    throw new Error(
      "usage: basecamp_release_lock.mjs "
        + "<attest-parent|verify-environment> <lock> <flock> "
        + "<snapshot-runner> <runs-root> <run-dir> [runner-pid]",
    );
  }
  const runnerPid = fromEnvironment ? undefined : Number(runnerPidArgument);
  const supervisorPid = fromEnvironment
    ? Number(process.env.PALACE_MVP_LOCK_SUPERVISOR_PID)
    : processIdentity(
      await readFile(`/proc/${runnerPid}/stat`),
      runnerPid,
    ).parentPid;
  const supervisorStartTimeTicks = fromEnvironment
    ? Number(process.env.PALACE_MVP_LOCK_SUPERVISOR_START_TIME_TICKS)
    : undefined;
  if (
    fromEnvironment
    && (
      process.env.PALACE_MVP_LOCK_PATH !== lockPath
      || process.env.PALACE_MVP_LOCK_SUPERVISED !== "1"
    )
  ) {
    throw new Error("release-lock environment binding is invalid");
  }
  const result = await verifyReleaseLock({
    lockPath,
    flock,
    supervisorPid,
    supervisorStartTimeTicks,
    snapshotRunner,
    runsRoot,
    runDirectory,
    runnerPid,
  });
  process.stdout.write(`${JSON.stringify(result)}\n`);
}
