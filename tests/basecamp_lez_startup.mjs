export const lezStartupTimeoutMs = 8 * 60_000;
export const ordinaryInvocationTimeoutMs = 120_000;
const retryableLezSyncReasons = Object.freeze([
  "current-height-failed",
  "last-synced-height-failed",
  "synced-height-ahead",
  "chunk-failed",
  "chunk-progress-mismatch",
  "terminal-height-mismatch",
]);

export function hasNonEmptyReceipt(receipt) {
  return typeof receipt === "string" && receipt.length > 0;
}

function statusFields(receipt) {
  return Object.fromEntries(
    String(receipt)
      .split(";")
      .filter((field) => field.includes("="))
      .map((field) => {
        const separator = field.indexOf("=");
        return [field.slice(0, separator), field.slice(separator + 1)];
      }),
  );
}

export function isCurrentLezState(receipt, programId) {
  const fields = statusFields(receipt);
  return (
    fields.ready === "1"
    && fields.compatible === "1"
    && fields.running === "1"
    && fields.sync === "current"
    && fields.current_height === fields.synced_height
    && fields.program === String(programId)
  );
}

export function acceptsLezStartupObservation(receipt, state, programId) {
  return hasNonEmptyReceipt(receipt) || isCurrentLezState(state, programId);
}

export function currentLezStateExpectation(programId) {
  return Object.freeze({ currentLezState: String(programId) });
}

export function isStartedStorageState(receipt) {
  const fields = statusFields(receipt);
  return [
    "starting",
    "running",
    "recovering",
    "reconciliation_required",
  ].includes(fields.storage)
    && fields.callback_registration === "ready"
    && ["0", "1"].includes(fields.reconciliation_required);
}

export function acceptsStorageStartupObservation(receipt, state) {
  return hasNonEmptyReceipt(receipt) || isStartedStorageState(state);
}

export function currentStorageStateExpectation() {
  return Object.freeze({ startedStorageState: true });
}

export function isRetryableLezSyncReceipt(receipt, stage, reasonPrefix = "") {
  if (
    typeof receipt !== "string"
    || typeof stage !== "string"
    || typeof reasonPrefix !== "string"
  ) {
    return false;
  }
  const prefix = `rejected=${stage};reason=${reasonPrefix}`;
  if (!receipt.startsWith(prefix)) {
    return false;
  }
  const reason = receipt.slice(prefix.length);
  return retryableLezSyncReasons.includes(reason);
}

export function workerInvocationTimeoutLimit(name) {
  return name === "gate4StartLez"
    ? lezStartupTimeoutMs
    : ordinaryInvocationTimeoutMs;
}
