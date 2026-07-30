export const lezStartupTimeoutMs = 8 * 60_000;
export const ordinaryInvocationTimeoutMs = 120_000;

export function workerInvocationTimeoutLimit(name) {
  return name === "gate4StartLez"
    ? lezStartupTimeoutMs
    : ordinaryInvocationTimeoutMs;
}
