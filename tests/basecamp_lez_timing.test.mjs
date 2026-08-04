import assert from "node:assert/strict";
import test from "node:test";

import {
  completeTimingBoundary,
  recoverPersistedTimingEvidence,
  startTimingBoundary,
  validateObservationTimingEnvelope,
} from "./basecamp_lez_timing.mjs";

function exactFinalizedTiming() {
  return {
    timings: {
      submitMs: 1,
      observeMs: 2,
      finalityMs: 3,
      totalMs: 6,
    },
    timingMeasurement: {
      submitMs: "measured",
      observeMs: "measured",
      finalityMs: "measured",
      totalMs: "measured",
    },
    timingBoundaries: {
      submitStartedAtUnixMs: 1_000,
      submitCompletedAtUnixMs: 1_001,
      observeStartedAtUnixMs: 1_001,
      observeCompletedAtUnixMs: 1_003,
      finalityStartedAtUnixMs: 1_003,
      finalityCompletedAtUnixMs: 1_006,
      totalStartedAtUnixMs: 1_000,
      totalCompletedAtUnixMs: 1_006,
    },
  };
}

test("accepts exact persisted boundaries for finalized resume", () => {
  const record = exactFinalizedTiming();
  assert.deepEqual(
    recoverPersistedTimingEvidence(record, "finalized"),
    [],
  );
  assert.deepEqual(record, exactFinalizedTiming());
});

test("blocks durable submitted resume without exact completion", () => {
  const record = {
    timings: {},
    timingMeasurement: {},
    timingBoundaries: {
      submitStartedAtUnixMs: 1_000,
      totalStartedAtUnixMs: 1_000,
    },
  };
  const unavailable = recoverPersistedTimingEvidence(
    record,
    "submitted_to_lez",
  );
  assert.deepEqual(unavailable, ["submitMs"]);
  assert.equal(record.timings.submitMs, null);
  assert.equal(
    record.timingMeasurement.submitMs,
    "recovered-unmeasured",
  );
});

test("blocks forged finalized boundary continuity", () => {
  const record = exactFinalizedTiming();
  record.timingBoundaries.observeStartedAtUnixMs = 1_002;
  record.timingBoundaries.observeCompletedAtUnixMs = 1_004;
  const unavailable = recoverPersistedTimingEvidence(
    record,
    "finalized",
  );
  assert.deepEqual(
    [...unavailable].sort(),
    ["finalityMs", "observeMs", "submitMs"].sort(),
  );
  for (const field of unavailable) {
    assert.equal(record.timings[field], null);
    assert.equal(
      record.timingMeasurement[field],
      "recovered-unmeasured",
    );
  }
});

test("queued retry preserves first worker invocation start", () => {
  const record = {
    timings: {},
    timingMeasurement: {},
    timingBoundaries: {},
  };
  assert.equal(startTimingBoundary(record, "submitMs", 1_000), 1_000);
  assert.equal(startTimingBoundary(record, "totalMs", 1_000), 1_000);
  assert.equal(startTimingBoundary(record, "submitMs", 2_000), 1_000);
  assert.equal(startTimingBoundary(record, "totalMs", 2_000), 1_000);
  assert.deepEqual(
    recoverPersistedTimingEvidence(record, "queued"),
    [],
  );
  completeTimingBoundary(record, "submitMs", 2_050);
  assert.equal(record.timings.submitMs, 1_050);
  assert.equal(record.timingBoundaries.submitStartedAtUnixMs, 1_000);
  assert.equal(record.timingBoundaries.submitCompletedAtUnixMs, 2_050);
});

test("worker timing envelope rejects reversed observation order", () => {
  assert.deepEqual(
    validateObservationTimingEnvelope(
      {
        startedAtUnixMs: 1_000,
        completedAtUnixMs: 1_001,
        elapsedMs: 1,
      },
      "test invocation",
    ),
    {
      startedAtUnixMs: 1_000,
      completedAtUnixMs: 1_001,
      elapsedMs: 1,
    },
  );
  assert.throws(
    () => validateObservationTimingEnvelope(
      {
        startedAtUnixMs: 1_001,
        completedAtUnixMs: 1_000,
        elapsedMs: 1,
      },
      "test invocation",
    ),
    /omitted exact observation timing/,
  );
});
