#!/usr/bin/env node

import assert from "node:assert/strict";
import test from "node:test";
import {
  buildGate2PostRestartProjection,
  validateGate2SettlementBoundary,
  validateGate2SpeechSettlement,
} from "./basecamp_gate2_settlement.mjs";

const rejectionCounters = {
  received_rejected: 5,
  rejected_scope: 1,
  rejected_expired: 1,
  rejected_signature: 1,
  rejected_replay: 1,
  rejected_payload: 1,
  rejected_other: 0,
};

function fixture() {
  const senderKeys = {
    a: "alice@1",
    b: "bob@2",
    c: "carol@3",
  };
  const acceptedBefore = { a: 8, b: 6, c: 7 };
  const beforeSequences = { a: 10, b: 20, c: 30 };
  const afterSequences = { a: 114, b: 124, c: 134 };
  const ingressSequences = Object.fromEntries(
    Object.entries(senderKeys).map(([label, senderKey]) => [
      senderKey,
      afterSequences[label],
    ]),
  );
  return {
    perSenderCount: { a: 100, b: 100, c: 100 },
    statusBefore: Object.fromEntries(
      ["a", "b", "c"].map((label) => [
        label,
        {
          outbox: 0,
          correlated: 0,
          received_accepted: acceptedBefore[label],
          ...rejectionCounters,
        },
      ]),
    ),
    snapshots: Object.fromEntries(
      ["a", "b", "c"].map((label) => [
        label,
        {
          status: {
            outbox: 0,
            correlated: 0,
            received_accepted: acceptedBefore[label] + 312,
            ...rejectionCounters,
          },
        },
      ]),
    ),
    sessionBefore: Object.fromEntries(
      ["a", "b", "c"].map((label) => [
        label,
        {
          senderKey: senderKeys[label],
          egressSequence: beforeSequences[label],
        },
      ]),
    ),
    sessionAfter: Object.fromEntries(
      ["a", "b", "c"].map((label) => [
        label,
        {
          senderKey: senderKeys[label],
          egressSequence: afterSequences[label],
          ingressSequences: { ...ingressSequences },
        },
      ]),
    ),
  };
}

function boundaryFixture() {
  const candidate = fixture();
  const ingressSequences = Object.fromEntries(
    Object.values(candidate.sessionBefore).map((session) => [
      session.senderKey,
      session.egressSequence,
    ]),
  );
  const sessions = Object.fromEntries(
    Object.entries(candidate.sessionBefore).map(([label, session]) => [
      label,
      {
        ...session,
        ingressSequences: { ...ingressSequences },
      },
    ]),
  );
  const snapshots = Object.fromEntries(
    Object.entries(candidate.statusBefore).map(([label, status]) => [
      label,
      { status: { ...status } },
    ]),
  );
  return {
    firstSnapshots: structuredClone(snapshots),
    firstSessions: structuredClone(sessions),
    secondSnapshots: structuredClone(snapshots),
    secondSessions: structuredClone(sessions),
  };
}

test("accepts a stable caught-up pre-speech boundary", () => {
  validateGate2SettlementBoundary(boundaryFixture());
});

test("rejects pre-speech counter or sequence movement", () => {
  for (const mutate of [
    (candidate) => {
      candidate.secondSnapshots.b.status.received_accepted += 1;
    },
    (candidate) => {
      candidate.secondSessions.a.egressSequence += 1;
    },
    (candidate) => {
      candidate.secondSessions.c.ingressSequences["bob@2"] -= 1;
    },
  ]) {
    const candidate = boundaryFixture();
    mutate(candidate);
    assert.throws(
      () => validateGate2SettlementBoundary(candidate),
      /changed|not settled/,
    );
  }
});

test("accepts speech plus exact periodic presence sequence deltas", () => {
  const candidate = fixture();
  const result = validateGate2SpeechSettlement(candidate);
  assert.deepEqual(
    Object.fromEntries(
      Object.entries(candidate.snapshots).map(([label, snapshot]) => [
        label,
        snapshot.status.received_accepted,
      ]),
    ),
    { a: 320, b: 318, c: 319 },
  );
  assert.equal(result.totalAcceptedDelta, 312);
  assert.deepEqual(result.sequenceEvidence, {
    a: { delta: 104, speechCount: 100, interleavedPresenceCount: 4 },
    b: { delta: 104, speechCount: 100, interleavedPresenceCount: 4 },
    c: { delta: 104, speechCount: 100, interleavedPresenceCount: 4 },
  });
});

test("rejects an accepted message not bound to persisted egress", () => {
  const candidate = fixture();
  candidate.snapshots.a.status.received_accepted += 1;
  assert.throws(
    () => validateGate2SpeechSettlement(candidate),
    /accepted delta=313, exact persisted egress delta=312/,
  );
});

test("rejects receiver ingress behind exact sender egress", () => {
  const candidate = fixture();
  candidate.sessionAfter.b.ingressSequences["alice@1"] -= 1;
  assert.throws(
    () => validateGate2SpeechSettlement(candidate),
    /b ingress alice@1=113, exact egress=114/,
  );
});

test("rejects sender sequence delta below its speech count", () => {
  const candidate = fixture();
  candidate.sessionAfter.c.egressSequence =
    candidate.sessionBefore.c.egressSequence + 99;
  candidate.sessionAfter.c.ingressSequences["carol@3"] =
    candidate.sessionAfter.c.egressSequence;
  assert.throws(
    () => validateGate2SpeechSettlement(candidate),
    /c egress delta=99, minimum speech count=100/,
  );
});

test("rejects unsettled correlation or rejection drift", () => {
  for (const mutate of [
    (candidate) => {
      candidate.snapshots.c.status.correlated = 1;
    },
    (candidate) => {
      candidate.statusBefore.c.outbox = 1;
    },
    (candidate) => {
      candidate.snapshots.c.status.rejected_other = 1;
    },
  ]) {
    const candidate = fixture();
    mutate(candidate);
    assert.throws(
      () => validateGate2SpeechSettlement(candidate),
      /not settled|rejected_other changed/,
    );
  }
});

test("rebuilds post-restart motion from the restart actions", () => {
  const result = buildGate2PostRestartProjection({
    beforeRestart: {
      alice: { displayName: "Alice", speech: "before", x: 5000, y: 6306, props: [] },
      bob: { displayName: "Bob", speech: "before", x: 3400, y: 4500, props: [] },
      carol: { displayName: "Carol", speech: "before", x: 5600, y: 6700, props: [] },
    },
    restartSpeechByUser: {
      alice: "restart-a",
      bob: "restart-b",
      carol: "restart-c",
    },
    rebuiltMotionByUser: {
      alice: { x: 1200, y: 2300 },
      bob: { x: 3400, y: 4500 },
      carol: { x: 5600, y: 6700 },
    },
  });
  assert.deepEqual(result.alice, {
    displayName: "Alice",
    speech: "restart-a",
    x: 1200,
    y: 2300,
    props: [],
  });
});

test("rejects a restart projection without every rebuilt motion", () => {
  assert.throws(
    () => buildGate2PostRestartProjection({
      beforeRestart: { alice: { displayName: "Alice" } },
      restartSpeechByUser: { alice: "restart-a" },
      rebuiltMotionByUser: {},
    }),
    /participant sets differ/,
  );
});
