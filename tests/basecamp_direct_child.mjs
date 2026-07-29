import { execFile as executeFile } from "node:child_process";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

const destructiveSignals = new Set(["SIGTERM", "SIGKILL"]);

export function procStartTimeTicks(stat, pid) {
  const encoded = String(stat).trim();
  const close = encoded.lastIndexOf(")");
  if (!encoded.startsWith(`${pid} (`) || close < 3) {
    throw new Error(`spawned process ${pid} has invalid stat`);
  }
  const startTimeTicks = Number(encoded.slice(close + 2).split(" ")[19]);
  if (!Number.isSafeInteger(startTimeTicks) || startTimeTicks <= 0) {
    throw new Error(`spawned process ${pid} has invalid start time`);
  }
  return startTimeTicks;
}

export function captureDirectChildIdentity(
  child,
  {
    procRoot = "/proc",
    read = readFileSync,
  } = {},
) {
  if (
    !child
    || !Number.isSafeInteger(child.pid)
    || child.pid <= 0
    || child.exitCode !== null
    || child.signalCode !== null
    || typeof procRoot !== "string"
    || resolve(procRoot) !== procRoot
    || typeof read !== "function"
  ) {
    throw new TypeError("direct child identity inputs are invalid");
  }
  const pid = child.pid;
  const startTimeTicks = procStartTimeTicks(
    read(`${procRoot}/${pid}/stat`, "utf8"),
    pid,
  );
  if (
    child.pid !== pid
    || child.exitCode !== null
    || child.signalCode !== null
  ) {
    throw new Error("direct child exited during identity capture");
  }
  return Object.freeze({ pid, startTimeTicks });
}

function executePidfd(helper, args, execute) {
  return new Promise((resolveExecution, rejectExecution) => {
    execute(
      helper,
      args,
      {
        env: {},
        stdio: ["ignore", "ignore", "pipe"],
      },
      (error, _stdout, stderr) => {
        if (error) {
          rejectExecution(
            new Error(
              `pidfd signal failed: ${
                Buffer.from(stderr ?? "").toString("utf8").trim()
                  || error.message
              }`,
            ),
          );
          return;
        }
        resolveExecution(true);
      },
    );
  });
}

export async function signalDirectChild(
  identity,
  signal,
  {
    helper = process.env.PALACE_PIDFD_SIGNAL,
    execute = executeFile,
  } = {},
) {
  if (
    !identity
    || !Number.isSafeInteger(identity.pid)
    || identity.pid <= 0
    || !Number.isSafeInteger(identity.startTimeTicks)
    || identity.startTimeTicks <= 0
    || !destructiveSignals.has(signal)
    || typeof helper !== "string"
    || resolve(helper) !== helper
    || typeof execute !== "function"
  ) {
    throw new TypeError("pidfd signal inputs are invalid");
  }
  return executePidfd(
    helper,
    [String(identity.pid), String(identity.startTimeTicks), signal],
    execute,
  );
}

export async function waitForDirectChildExit(
  exited,
  timeoutMs,
  description,
  {
    schedule = setTimeout,
    cancel = clearTimeout,
  } = {},
) {
  if (
    !exited
    || typeof exited.then !== "function"
    || !Number.isSafeInteger(timeoutMs)
    || timeoutMs <= 0
    || typeof description !== "string"
    || description.length === 0
    || typeof schedule !== "function"
    || typeof cancel !== "function"
  ) {
    throw new TypeError("direct child wait inputs are invalid");
  }
  let timer;
  const timedOut = new Promise((resolveTimeout) => {
    timer = schedule(() => resolveTimeout({ timedOut: true }), timeoutMs);
  });
  try {
    const result = await Promise.race([
      Promise.resolve(exited).then((exit) => ({ timedOut: false, exit })),
      timedOut,
    ]);
    if (result.timedOut) {
      throw new Error(
        `${description} did not exit within ${timeoutMs}ms; `
        + "exact process-scope cleanup is required",
      );
    }
    return result.exit;
  } finally {
    cancel(timer);
  }
}
