function knownPid(value) {
  return Number.isSafeInteger(value) && value > 0;
}

function failureMessage(reason) {
  return reason instanceof Error ? reason.message : String(reason);
}

export async function stopKnownWorkers(
  workers,
  processExists,
  processGroupExists,
  terminateProcessGroup,
  finalizeOwnedProcesses,
) {
  if (
    !Array.isArray(workers)
    || typeof processExists !== "function"
    || typeof processGroupExists !== "function"
    || typeof terminateProcessGroup !== "function"
    || typeof finalizeOwnedProcesses !== "function"
  ) {
    throw new TypeError("terminal cleanup inputs are invalid");
  }

  const results = await Promise.allSettled(
    workers.map((worker) => worker.stop()),
  );
  const failures = [];
  for (let index = 0; index < workers.length; index += 1) {
    const worker = workers[index];
    const label = String(
      worker.processLabel ?? worker.label ?? `worker-${index + 1}`,
    );
    const result = results[index];
    if (result.status === "rejected") {
      failures.push(
        `${label} cleanup rejected: ${failureMessage(result.reason)}`,
      );
    }

    const workerPid = worker.child?.pid;
    for (const processGroupId of [workerPid, worker.basecampPid]) {
      if (
        knownPid(processGroupId)
        && processGroupExists(processGroupId)
      ) {
        try {
          await terminateProcessGroup(processGroupId);
        } catch (error) {
          failures.push(
            `${label} process group ${processGroupId} forced cleanup rejected: `
            + failureMessage(error),
          );
        }
      }
    }
    if (knownPid(workerPid) && processExists(workerPid)) {
      failures.push(`${label} worker process ${workerPid} survived cleanup`);
    }
    if (knownPid(workerPid) && processGroupExists(workerPid)) {
      failures.push(
        `${label} worker process group ${workerPid} survived cleanup`,
      );
    }
    if (knownPid(worker.basecampPid) && processExists(worker.basecampPid)) {
      failures.push(
        `${label} Basecamp process ${worker.basecampPid} survived cleanup`,
      );
    }
    if (
      knownPid(worker.basecampPid)
      && processGroupExists(worker.basecampPid)
    ) {
      failures.push(
        `${label} Basecamp process group ${worker.basecampPid} survived cleanup`,
      );
    }
  }
  try {
    await finalizeOwnedProcesses();
  } catch (error) {
    failures.push(
      `run-owned process cleanup rejected: ${failureMessage(error)}`,
    );
  }
  return [...new Set(failures)];
}

export function acceptBasecampPidHandoff(currentPid, message) {
  if (message?.event !== "basecamp-started") return currentPid;
  const announcedPid = message.basecampPid;
  if (
    !knownPid(announcedPid)
    || (knownPid(currentPid) && currentPid !== announcedPid)
  ) {
    throw new Error("worker Basecamp PID handoff is invalid");
  }
  return announcedPid;
}
