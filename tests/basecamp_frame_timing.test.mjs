#!/usr/bin/env node

import assert from "node:assert/strict";
import test from "node:test";
import {
  capturePalaceFrameTiming,
  palaceFrameTimingContract,
  parsePalaceFrameTimingEvidence,
  summarizePalaceFrameIntervals,
  validatePalaceFrameTimingMeasurement,
} from "./basecamp_frame_timing.mjs";

const samplesUs = Array.from(
  { length: palaceFrameTimingContract.sampleCount },
  (_, index) => (index + 1) * 1_000,
);

function encodedEvidence({
  elapsedTimeUs,
  endFrame,
  failure = "",
  request = 1,
  samples = samplesUs,
  startFrame,
  state = "complete",
} = {}) {
  const resolvedStartFrame = startFrame ?? (state === "idle" ? -1 : 2);
  const resolvedEndFrame = endFrame ?? (
    state === "complete"
      ? resolvedStartFrame + palaceFrameTimingContract.sampleCount
      : -1
  );
  const resolvedElapsedTimeUs = elapsedTimeUs ?? (
    state === "complete"
      ? samples.reduce((total, sample) => total + sample, 0)
      : 0
  );
  return JSON.stringify({
    schema: "logos.palace.frame-animation-timing",
    version: 1,
    request,
    state,
    startFrame: resolvedStartFrame,
    endFrame: resolvedEndFrame,
    elapsedTimeUs: resolvedElapsedTimeUs,
    sampleUnit: "microseconds",
    warmupFrames: 2,
    sampleTarget: palaceFrameTimingContract.sampleCount,
    samplesUs: samples,
    failure,
  });
}

function measurement(samples = samplesUs) {
  return {
    measurementContract: palaceFrameTimingContract,
    frameWindow: {
      startFrame: 2,
      endFrame: 2 + palaceFrameTimingContract.sampleCount,
      elapsedTimeUs:
        samples.reduce((total, sample) => total + sample, 0),
    },
    sampleCount: samples.length,
    samplesUs: samples,
    summaries: {
      frameIntervalUs: summarizePalaceFrameIntervals(samples),
    },
  };
}

test("summarizes exact FrameAnimation intervals with nearest rank", () => {
  assert.deepEqual(summarizePalaceFrameIntervals(samplesUs), {
    p50: 60_000,
    p95: 114_000,
    max: 120_000,
  });
  assert.deepEqual(
    validatePalaceFrameTimingMeasurement(measurement()),
    measurement(),
  );
});

test("accepts bounded idle, sampling, complete, and failed evidence", () => {
  assert.equal(
    parsePalaceFrameTimingEvidence(encodedEvidence({
      request: 0,
      samples: [],
      state: "idle",
    })).state,
    "idle",
  );
  assert.equal(
    parsePalaceFrameTimingEvidence(encodedEvidence({
      samples: samplesUs.slice(0, 5),
      state: "sampling",
    })).state,
    "sampling",
  );
  assert.equal(
    parsePalaceFrameTimingEvidence(encodedEvidence()).state,
    "complete",
  );
  assert.equal(
    parsePalaceFrameTimingEvidence(encodedEvidence({
      failure: "invalid-frame-interval",
      samples: samplesUs.slice(0, 5),
      state: "failed",
    })).state,
    "failed",
  );
  assert.equal(
    parsePalaceFrameTimingEvidence(encodedEvidence({
      failure: "capture-timeout",
      samples: [],
      startFrame: -1,
      state: "failed",
    })).state,
    "failed",
  );
});

test("rejects malformed, incomplete, or out-of-range frame evidence", () => {
  for (const encoded of [
    "",
    "not-json",
    encodedEvidence({ samples: samplesUs.slice(1) }),
    encodedEvidence({ samples: [...samplesUs.slice(0, -1), 0] }),
    encodedEvidence({ samples: [...samplesUs.slice(0, -1), -1] }),
    encodedEvidence({ samples: [...samplesUs.slice(0, -1), 30_000_001] }),
    encodedEvidence({ request: 0 }),
    encodedEvidence({ failure: "unexpected", state: "failed" }),
    encodedEvidence({ endFrame: 121 }),
    encodedEvidence({ elapsedTimeUs: 1 }),
  ]) {
    assert.throws(
      () => parsePalaceFrameTimingEvidence(encoded),
      /frame timing/,
    );
  }
  for (const samples of [
    samplesUs.slice(1),
    [...samplesUs, 121_000],
    [...samplesUs.slice(0, -1), 1.5],
  ]) {
    assert.throws(
      () => summarizePalaceFrameIntervals(samples),
      /samples are invalid/,
    );
  }
});

test("rejects a summary or contract differing from raw frame samples", () => {
  const wrongSummary = structuredClone(measurement());
  wrongSummary.summaries.frameIntervalUs.p95 += 1;
  assert.throws(
    () => validatePalaceFrameTimingMeasurement(wrongSummary),
    /summary differs/,
  );

  const wrongContract = structuredClone(measurement());
  wrongContract.measurementContract.clock = "wall clock";
  assert.throws(
    () => validatePalaceFrameTimingMeasurement(wrongContract),
    /measurement is invalid/,
  );

  const wrongFrameWindow = structuredClone(measurement());
  wrongFrameWindow.frameWindow.endFrame += 1;
  assert.throws(
    () => validatePalaceFrameTimingMeasurement(wrongFrameWindow),
    /measurement is invalid/,
  );

  const wrongElapsed = structuredClone(measurement());
  wrongElapsed.frameWindow.elapsedTimeUs +=
    Math.ceil(palaceFrameTimingContract.sampleCount / 2) + 1;
  assert.throws(
    () => validatePalaceFrameTimingMeasurement(wrongElapsed),
    /summary differs/,
  );
});

test("capture rejects an invalid target without mutating evidence", async () => {
  let encoded = encodedEvidence({
    request: 0,
    samples: [],
    state: "idle",
  });
  const expressions = [];
  const captured = await capturePalaceFrameTiming({
    evaluate: async (expression) => {
      expressions.push(expression);
      if (expression === "gateFrameTimingStart(119)") {
        return { result: -1, undefined: false };
      }
      encoded = encodedEvidence();
      return { result: 1, undefined: false };
    },
    rootProperties: async () => ({ gateFrameTimingEvidence: encoded }),
    sleep: async () => {},
  });
  assert.deepEqual(expressions, [
    "gateFrameTimingStart(119)",
    "gateFrameTimingStart(120)",
  ]);
  assert.deepEqual(captured, measurement());
});

test("capture fails if invalid-target rejection mutates state", async () => {
  let encoded = encodedEvidence({
    request: 0,
    samples: [],
    state: "idle",
  });
  await assert.rejects(
    capturePalaceFrameTiming({
      evaluate: async (expression) => {
        if (expression === "gateFrameTimingStart(119)") {
          encoded = encodedEvidence({
            request: 1,
            samples: [],
            state: "sampling",
          });
          return { result: -1, undefined: false };
        }
        return { result: 1, undefined: false };
      },
      rootProperties: async () => ({ gateFrameTimingEvidence: encoded }),
      sleep: async () => {},
    }),
    /accepted an invalid sample target/,
  );
});
