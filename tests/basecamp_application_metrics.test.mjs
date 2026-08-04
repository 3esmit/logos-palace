#!/usr/bin/env node

import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { dirname, resolve } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const jq = resolve(process.env.PALACE_JQ ?? "/usr/bin/jq");
const filterDirectory = dirname(fileURLToPath(import.meta.url));
const filter =
  'include "basecamp_application_metrics"; '
  + "palace_valid_application_round_trip";

function summary(durations) {
  const sorted = [...durations].sort((left, right) => left - right);
  return {
    sampleCount: 20,
    p50Ms: sorted[9],
    p95Ms: sorted[18],
    maxMs: sorted[19],
  };
}

function measurement(bytes) {
  const durations = Array.from(
    { length: 20 },
    (_, index) => (19 - index) * 2 + bytes / 256,
  );
  return {
    payloadUtf8Bytes: bytes,
    requestUtf8Bytes: bytes,
    responseUtf8Bytes: bytes,
    latency: summary(durations),
    samples: durations.map((roundTripMs, index) => ({
      ordinal: index + 1,
      requestUtf8Bytes: bytes,
      responseUtf8Bytes: bytes,
      roundTripMs,
    })),
  };
}

function fixture() {
  return {
    status: "passed",
    clock: "worker performance.now monotonic milliseconds",
    startBoundary:
      "immediately before inspector invokes the QML UI-backend call",
    endBoundary:
      "invocationSequence advanced and exact raw echo property was observed",
    payloadSemantics: "application UTF-8 bytes; not transport wire bytes",
    samplesPerSize: 20,
    measurements: {
      0: measurement(0),
      256: measurement(256),
      4096: measurement(4096),
    },
    rejectedUnsupportedSize: "rejected=application-round-trip-size",
  };
}

function accepts(value) {
  const result = spawnSync(
    jq,
    ["-L", filterDirectory, "-e", filter],
    {
      input: `${JSON.stringify(value)}\n`,
      encoding: "utf8",
      maxBuffer: 1024 * 1024,
    },
  );
  if (result.error) throw result.error;
  return result.status === 0;
}

test("accepts exact raw application sample evidence", () => {
  assert.equal(accepts(fixture()), true);
});

test("rejects raw application envelope drift", () => {
  for (const mutate of [
    (value) => {
      value.extra = true;
    },
    (value) => {
      value.clock = "Date.now";
    },
    (value) => {
      value.samplesPerSize = 19;
    },
    (value) => {
      delete value.measurements["256"];
    },
    (value) => {
      value.measurements["64"] = measurement(64);
    },
  ]) {
    const value = fixture();
    mutate(value);
    assert.equal(accepts(value), false);
  }
});

test("rejects missing, extra, reordered, or malformed raw samples", () => {
  for (const mutate of [
    (samples) => {
      samples.pop();
    },
    (samples) => {
      samples.push(structuredClone(samples.at(-1)));
    },
    (samples) => {
      samples[2].ordinal = 4;
    },
    (samples) => {
      samples[3].requestUtf8Bytes = 255;
    },
    (samples) => {
      samples[4].roundTripMs = -1;
    },
    (samples) => {
      samples[5].extra = true;
    },
  ]) {
    const value = fixture();
    mutate(value.measurements["256"].samples);
    assert.equal(accepts(value), false);
  }
});

test("rejects latency aggregates not recomputed from raw samples", () => {
  for (const field of ["sampleCount", "p50Ms", "p95Ms", "maxMs"]) {
    const value = fixture();
    value.measurements["4096"].latency[field] += 1;
    assert.equal(accepts(value), false);
  }
});
