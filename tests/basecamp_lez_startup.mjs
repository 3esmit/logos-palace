export const lezStartupTimeoutMs = 8 * 60_000;
export const ordinaryInvocationTimeoutMs = 120_000;

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

export function workerInvocationTimeoutLimit(name) {
  return name === "gate4StartLez"
    ? lezStartupTimeoutMs
    : ordinaryInvocationTimeoutMs;
}
