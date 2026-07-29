const labels = Object.freeze(["a", "b", "c"]);
const rejectionCounterNames = Object.freeze([
  "rejected_scope",
  "rejected_expired",
  "rejected_signature",
  "rejected_replay",
  "rejected_payload",
  "rejected_other",
]);
const settlementCounterNames = Object.freeze([
  "received_accepted",
  "received_rejected",
  ...rejectionCounterNames,
  "outbox",
  "correlated",
]);

function nonnegativeInteger(value, description) {
  if (!Number.isSafeInteger(value) || value < 0) {
    throw new Error(`${description} is not a nonnegative integer`);
  }
  return value;
}

function required(value, description) {
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    throw new Error(`${description} is missing`);
  }
  return value;
}

function validateSessionSet(sessions, description) {
  const egressTargets = {};
  for (const label of labels) {
    const session = required(
      sessions?.[label],
      `${description} ${label} session`,
    );
    if (
      typeof session.senderKey !== "string"
      || session.senderKey.length === 0
      || Object.hasOwn(egressTargets, session.senderKey)
    ) {
      throw new Error(
        `${description} ${label} sender identity is missing or duplicated`,
      );
    }
    egressTargets[session.senderKey] = nonnegativeInteger(
      session.egressSequence,
      `${description} ${label} egress sequence`,
    );
  }
  for (const label of labels) {
    const ingress = required(
      sessions[label].ingressSequences,
      `${description} ${label} ingress sequences`,
    );
    for (const [senderKey, target] of Object.entries(egressTargets)) {
      if (
        nonnegativeInteger(
          ingress[senderKey],
          `${description} ${label} ingress ${senderKey}`,
        ) !== target
      ) {
        throw new Error(
          `${description} ${label} ingress ${senderKey} is not settled`,
        );
      }
    }
  }
}

function assertExactSequenceMap(leftValue, rightValue, description) {
  const left = required(leftValue, `${description} first sequence map`);
  const right = required(rightValue, `${description} second sequence map`);
  const leftKeys = Object.keys(left).sort();
  const rightKeys = Object.keys(right).sort();
  if (
    leftKeys.length !== rightKeys.length
    || leftKeys.some((key, index) => key !== rightKeys[index])
  ) {
    throw new Error(`${description} sequence keys changed`);
  }
  for (const key of leftKeys) {
    const before = nonnegativeInteger(
      left[key],
      `${description} first ${key} sequence`,
    );
    const after = nonnegativeInteger(
      right[key],
      `${description} second ${key} sequence`,
    );
    if (before !== after) {
      throw new Error(`${description} ${key} sequence changed`);
    }
  }
}

export function validateGate2SettlementBoundary({
  firstSnapshots,
  firstSessions,
  secondSnapshots,
  secondSessions,
}) {
  validateSessionSet(firstSessions, "first boundary");
  validateSessionSet(secondSessions, "second boundary");
  for (const label of labels) {
    const firstStatus = required(
      firstSnapshots?.[label]?.status,
      `${label} first boundary status`,
    );
    const secondStatus = required(
      secondSnapshots?.[label]?.status,
      `${label} second boundary status`,
    );
    for (const name of settlementCounterNames) {
      const before = nonnegativeInteger(
        firstStatus[name],
        `${label} first boundary ${name}`,
      );
      const after = nonnegativeInteger(
        secondStatus[name],
        `${label} second boundary ${name}`,
      );
      if (before !== after) {
        throw new Error(`${label} boundary ${name} changed`);
      }
    }
    if (
      firstStatus.outbox !== 0
      || firstStatus.correlated !== 0
      || secondStatus.outbox !== 0
      || secondStatus.correlated !== 0
    ) {
      throw new Error(`${label} boundary Delivery state is not settled`);
    }

    const firstSession = required(
      firstSessions?.[label],
      `${label} first boundary session`,
    );
    const secondSession = required(
      secondSessions?.[label],
      `${label} second boundary session`,
    );
    if (
      firstSession.senderKey !== secondSession.senderKey
      || firstSession.egressSequence !== secondSession.egressSequence
    ) {
      throw new Error(`${label} boundary sender sequence changed`);
    }
    assertExactSequenceMap(
      firstSession.ingressSequences,
      secondSession.ingressSequences,
      `${label} boundary ingress`,
    );
  }
}

export function validateGate2SpeechSettlement({
  perSenderCount,
  statusBefore,
  snapshots,
  sessionBefore,
  sessionAfter,
}) {
  const sequenceEvidence = {};
  const egressTargets = {};
  let totalAcceptedDelta = 0;

  for (const label of labels) {
    const sent = nonnegativeInteger(
      perSenderCount?.[label],
      `${label} speech count`,
    );
    const before = required(
      sessionBefore?.[label],
      `${label} pre-speech session`,
    );
    const after = required(
      sessionAfter?.[label],
      `${label} post-speech session`,
    );
    if (
      typeof before.senderKey !== "string"
      || before.senderKey.length === 0
      || after.senderKey !== before.senderKey
      || Object.hasOwn(egressTargets, after.senderKey)
    ) {
      throw new Error(`${label} sender identity changed or is duplicated`);
    }
    const beforeSequence = nonnegativeInteger(
      before.egressSequence,
      `${label} pre-speech egress sequence`,
    );
    const afterSequence = nonnegativeInteger(
      after.egressSequence,
      `${label} post-speech egress sequence`,
    );
    const delta = afterSequence - beforeSequence;
    if (!Number.isSafeInteger(delta) || delta < sent) {
      throw new Error(
        `${label} egress delta=${delta}, minimum speech count=${sent}`,
      );
    }
    totalAcceptedDelta += delta;
    if (!Number.isSafeInteger(totalAcceptedDelta)) {
      throw new Error("total accepted Delivery delta is unsafe");
    }
    egressTargets[after.senderKey] = afterSequence;
    sequenceEvidence[label] = {
      delta,
      speechCount: sent,
      interleavedPresenceCount: delta - sent,
    };
  }

  for (const label of labels) {
    const baseline = required(
      statusBefore?.[label],
      `${label} pre-speech status`,
    );
    const status = required(
      snapshots?.[label]?.status,
      `${label} post-speech status`,
    );
    const baselineAccepted = nonnegativeInteger(
      baseline.received_accepted,
      `${label} pre-speech accepted count`,
    );
    const accepted = nonnegativeInteger(
      status.received_accepted,
      `${label} post-speech accepted count`,
    );
    if (
      nonnegativeInteger(
        baseline.outbox,
        `${label} pre-speech outbox`,
      ) !== 0
      || nonnegativeInteger(
        baseline.correlated,
        `${label} pre-speech correlation`,
      ) !== 0
    ) {
      throw new Error(`${label} pre-speech Delivery state is not settled`);
    }
    if (accepted - baselineAccepted !== totalAcceptedDelta) {
      throw new Error(
        `${label} accepted delta=${accepted - baselineAccepted}, `
          + `exact persisted egress delta=${totalAcceptedDelta}`,
      );
    }
    if (
      nonnegativeInteger(status.outbox, `${label} outbox`) !== 0
      || nonnegativeInteger(status.correlated, `${label} correlation`) !== 0
    ) {
      throw new Error(`${label} Delivery sends are not settled`);
    }
    for (const name of ["received_rejected", ...rejectionCounterNames]) {
      const before = nonnegativeInteger(
        baseline[name],
        `${label} pre-speech ${name}`,
      );
      const after = nonnegativeInteger(
        status[name],
        `${label} post-speech ${name}`,
      );
      if (after !== before) {
        throw new Error(`${label} ${name} changed: ${before} -> ${after}`);
      }
    }

    const ingress = required(
      sessionAfter?.[label]?.ingressSequences,
      `${label} post-speech ingress sequences`,
    );
    for (const [senderKey, target] of Object.entries(egressTargets)) {
      if (ingress[senderKey] !== target) {
        throw new Error(
          `${label} ingress ${senderKey}=${ingress[senderKey] ?? "missing"}, `
            + `exact egress=${target}`,
        );
      }
    }
  }

  return {
    totalAcceptedDelta,
    sequenceEvidence,
  };
}
