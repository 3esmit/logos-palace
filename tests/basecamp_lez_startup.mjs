export const lezStartupTimeoutMs = 8 * 60_000;
export const ordinaryInvocationTimeoutMs = 120_000;
export const lezStartupReceiptExpectation = Object.freeze({ nonEmpty: true });

export function hasNonEmptyReceipt(receipt) {
  return typeof receipt === "string" && receipt.length > 0;
}

export function workerInvocationTimeoutLimit(name) {
  return name === "gate4StartLez"
    ? lezStartupTimeoutMs
    : ordinaryInvocationTimeoutMs;
}
