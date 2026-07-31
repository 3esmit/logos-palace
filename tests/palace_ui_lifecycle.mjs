/**
 * Pure lifecycle helpers for Logos Palace UI authoring preview counters and
 * import session flags. Main.qml must apply the same transitions; tests drive
 * these shipped helpers directly.
 */

export function clampNonNegative(value) {
  const n = Number(value);
  if (!Number.isFinite(n) || n <= 0) return 0;
  return Math.floor(n);
}

/**
 * Transition ready-image count when a card's preview readiness changes.
 * wasCounted / isReady are booleans; returns { nextCount, nextCounted }.
 */
export function applyPreviewReadyTransition(count, wasCounted, isReady) {
  let next = clampNonNegative(count);
  let counted = Boolean(wasCounted);
  const ready = Boolean(isReady);
  if (ready && !counted) {
    next += 1;
    counted = true;
  } else if (!ready && counted) {
    next = clampNonNegative(next - 1);
    counted = false;
  }
  return { nextCount: next, nextCounted: counted };
}

/**
 * When a card is destroyed, drop its contribution if it was counted.
 */
export function applyPreviewDestroyed(count, wasCounted) {
  if (!wasCounted) {
    return { nextCount: clampNonNegative(count), nextCounted: false };
  }
  return {
    nextCount: clampNonNegative(count - 1),
    nextCounted: false,
  };
}

/**
 * Closing the authoring surface unloads all previews; count must return to 0.
 */
export function resetAuthoringPreviewCount() {
  return 0;
}

/**
 * Import session flags after abandon/reset (capability released separately).
 */
export function idleImportState(previousGeneration = 0) {
  const generation = Number.isSafeInteger(previousGeneration)
    ? previousGeneration + 1
    : 1;
  return {
    running: false,
    phase: "idle",
    requestId: "",
    session: "",
    label: "",
    expectedSequence: 0,
    pendingSequence: -1,
    startedAtUnixMs: 0,
    trace: null,
    generation,
  };
}

export function shouldCancelOrphanStage(session, matchesActiveRequest) {
  return (
    typeof session === "string"
    && /^[0-9a-f]{32}$/.test(session)
    && !matchesActiveRequest
  );
}
