const evidenceKeys = Object.freeze([
  "elapsedTimeUs",
  "endFrame",
  "failure",
  "request",
  "sampleTarget",
  "sampleUnit",
  "samplesUs",
  "schema",
  "startFrame",
  "state",
  "version",
  "warmupFrames",
]);

export const palaceFrameTimingContract = Object.freeze({
  basecampRevision: "115ffcafd7ccc0555c6f15f93c1562c4025f148e",
  qtVersion: "6.9.2",
  qtSourceTag: "v6.9.2",
  qtSourcePath: "src/quick/util/qquickframeanimation.cpp",
  qtSourceSha256:
    "d7b72073642d8d053908da70536bb11bf084a60d24f67c89bfede3da752ac7f2",
  source: "QtQuick.FrameAnimation.frameTime",
  clock: "QElapsedTimer monotonic nanoseconds exposed by Qt as seconds",
  frameBoundary:
    "Qt Quick animation-frame update interval associated with rendered animation frames",
  conversion: "Math.round(frameTime * 1000000)",
  sampleUnit: "integer microseconds",
  warmupFrames: 2,
  sampleCount: 120,
  captureTimeoutMs: 30_000,
  activation:
    "disabled outside bounded acceptance capture",
  probeCost:
    "forces continuous Qt Quick animation only during bounded capture",
  elapsedAgreement:
    "abs(sum(samplesUs) - elapsedTimeUs) <= ceil(sampleCount / 2) microseconds",
  metric:
    "inter-frame update interval; not CPU render duration or presentation latency",
  percentileMethod:
    "nearest-rank: sorted[Math.ceil(percentile * sampleCount) - 1]",
});

function exactKeys(value, expected) {
  return (
    value !== null
    && typeof value === "object"
    && !Array.isArray(value)
    && JSON.stringify(Object.keys(value).sort())
      === JSON.stringify([...expected].sort())
  );
}

function stable(value) {
  if (Array.isArray(value)) return value.map(stable);
  if (
    value !== null
    && typeof value === "object"
  ) {
    return Object.fromEntries(
      Object.keys(value)
        .sort()
        .map((key) => [key, stable(value[key])]),
    );
  }
  return value;
}

function exactJson(left, right) {
  return JSON.stringify(stable(left)) === JSON.stringify(stable(right));
}

function validSample(value) {
  return (
    Number.isSafeInteger(value)
    && value > 0
    && value <= palaceFrameTimingContract.captureTimeoutMs * 1_000
  );
}

export function parsePalaceFrameTimingEvidence(encoded) {
  if (
    typeof encoded !== "string"
    || encoded.length === 0
    || encoded.length > 4 * 1024
  ) {
    throw new Error("Palace frame timing evidence is not bounded JSON");
  }
  let value;
  try {
    value = JSON.parse(encoded);
  } catch {
    throw new Error("Palace frame timing evidence is not valid JSON");
  }
  if (
    !exactKeys(value, evidenceKeys)
    || value.schema !== "logos.palace.frame-animation-timing"
    || value.version !== 1
    || !Number.isSafeInteger(value.request)
    || value.request < 0
    || !Number.isSafeInteger(value.startFrame)
    || value.startFrame < -1
    || !Number.isSafeInteger(value.endFrame)
    || value.endFrame < -1
    || !Number.isSafeInteger(value.elapsedTimeUs)
    || value.elapsedTimeUs < 0
    || value.elapsedTimeUs
      > palaceFrameTimingContract.captureTimeoutMs * 1_000
    || !["idle", "sampling", "complete", "failed"].includes(value.state)
    || value.sampleUnit !== "microseconds"
    || value.warmupFrames !== palaceFrameTimingContract.warmupFrames
    || value.sampleTarget !== palaceFrameTimingContract.sampleCount
    || !Array.isArray(value.samplesUs)
    || value.samplesUs.length > palaceFrameTimingContract.sampleCount
    || value.samplesUs.some((sample) => !validSample(sample))
    || typeof value.failure !== "string"
    || value.failure.length > 80
  ) {
    throw new Error("Palace frame timing evidence violates its schema");
  }
  if (
    value.state === "idle"
    && (
      value.request !== 0
      || value.samplesUs.length !== 0
      || value.failure !== ""
      || value.startFrame !== -1
      || value.endFrame !== -1
      || value.elapsedTimeUs !== 0
    )
  ) {
    throw new Error("idle Palace frame timing evidence is inconsistent");
  }
  if (
    value.state === "sampling"
    && (
      value.request <= 0
      || value.samplesUs.length >= palaceFrameTimingContract.sampleCount
      || value.failure !== ""
      || (
        value.samplesUs.length > 0
        && value.startFrame < 0
      )
      || value.endFrame !== -1
      || value.elapsedTimeUs !== 0
    )
  ) {
    throw new Error("sampling Palace frame timing evidence is inconsistent");
  }
  if (
    value.state === "complete"
    && (
      value.request <= 0
      || value.samplesUs.length !== palaceFrameTimingContract.sampleCount
      || value.failure !== ""
      || value.startFrame < 0
      || value.endFrame
        !== value.startFrame + palaceFrameTimingContract.sampleCount
      || value.elapsedTimeUs <= 0
      || Math.abs(
        value.samplesUs.reduce((total, sample) => total + sample, 0)
          - value.elapsedTimeUs,
      ) > Math.ceil(palaceFrameTimingContract.sampleCount / 2)
    )
  ) {
    throw new Error("complete Palace frame timing evidence is inconsistent");
  }
  if (
    value.state === "failed"
    && (
      value.request <= 0
      || ![
        "capture-timeout",
        "invalid-frame-interval",
        "invalid-frame-window",
      ].includes(value.failure)
      || value.samplesUs.length >= palaceFrameTimingContract.sampleCount
      || (
        value.failure !== "capture-timeout"
        && value.startFrame < 0
      )
      || value.endFrame !== -1
      || value.elapsedTimeUs !== 0
    )
  ) {
    throw new Error("failed Palace frame timing evidence is inconsistent");
  }
  return value;
}

function nearestRank(values, percentile) {
  const sorted = [...values].sort((left, right) => left - right);
  return sorted[Math.ceil(percentile * sorted.length) - 1];
}

export function summarizePalaceFrameIntervals(samplesUs) {
  if (
    !Array.isArray(samplesUs)
    || samplesUs.length !== palaceFrameTimingContract.sampleCount
    || samplesUs.some((sample) => !validSample(sample))
  ) {
    throw new Error("Palace frame timing samples are invalid");
  }
  return {
    p50: nearestRank(samplesUs, 0.50),
    p95: nearestRank(samplesUs, 0.95),
    max: Math.max(...samplesUs),
  };
}

export function validatePalaceFrameTimingMeasurement(value) {
  if (
    !exactKeys(
      value,
      [
        "frameWindow",
        "measurementContract",
        "sampleCount",
        "samplesUs",
        "summaries",
      ],
    )
    || !exactJson(
      value.measurementContract,
      palaceFrameTimingContract,
    )
    || value.sampleCount !== palaceFrameTimingContract.sampleCount
    || !exactKeys(
      value.frameWindow,
      ["startFrame", "endFrame", "elapsedTimeUs"],
    )
    || !Number.isSafeInteger(value.frameWindow.startFrame)
    || value.frameWindow.startFrame < 0
    || value.frameWindow.endFrame
      !== value.frameWindow.startFrame + palaceFrameTimingContract.sampleCount
    || !Number.isSafeInteger(value.frameWindow.elapsedTimeUs)
    || value.frameWindow.elapsedTimeUs <= 0
    || value.frameWindow.elapsedTimeUs
      > palaceFrameTimingContract.captureTimeoutMs * 1_000
    || !exactKeys(value.summaries, ["frameIntervalUs"])
  ) {
    throw new Error("Palace frame timing measurement is invalid");
  }
  const expected = summarizePalaceFrameIntervals(value.samplesUs);
  if (
    !exactKeys(value.summaries.frameIntervalUs, ["p50", "p95", "max"])
    || !exactJson(value.summaries.frameIntervalUs, expected)
    || Math.abs(
      value.samplesUs.reduce((total, sample) => total + sample, 0)
        - value.frameWindow.elapsedTimeUs,
    ) > Math.ceil(palaceFrameTimingContract.sampleCount / 2)
  ) {
    throw new Error("Palace frame timing summary differs from raw samples");
  }
  return value;
}

export async function capturePalaceFrameTiming({
  evaluate,
  rootProperties,
  sleep,
  timeoutMs = palaceFrameTimingContract.captureTimeoutMs,
}) {
  if (
    typeof evaluate !== "function"
    || typeof rootProperties !== "function"
    || typeof sleep !== "function"
    || !Number.isSafeInteger(timeoutMs)
    || timeoutMs < 1_000
    || timeoutMs > palaceFrameTimingContract.captureTimeoutMs
  ) {
    throw new Error("Palace frame timing capture interface is invalid");
  }

  const before = await rootProperties();
  const beforeEncoded = String(before.gateFrameTimingEvidence ?? "");
  parsePalaceFrameTimingEvidence(beforeEncoded);
  const rejected = await evaluate(
    `gateFrameTimingStart(${palaceFrameTimingContract.sampleCount - 1})`,
  );
  const afterRejected = String(
    (await rootProperties()).gateFrameTimingEvidence ?? "",
  );
  if (
    rejected.result !== -1
    || rejected.undefined === true
    || afterRejected !== beforeEncoded
  ) {
    throw new Error("Palace frame timing accepted an invalid sample target");
  }

  const started = await evaluate(
    `gateFrameTimingStart(${palaceFrameTimingContract.sampleCount})`,
  );
  if (
    !Number.isSafeInteger(started.result)
    || started.result <= 0
    || started.undefined === true
  ) {
    throw new Error("Palace frame timing did not return a request identifier");
  }
  const request = started.result;
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const encoded = String(
      (await rootProperties()).gateFrameTimingEvidence ?? "",
    );
    const evidence = parsePalaceFrameTimingEvidence(encoded);
    if (evidence.request !== request) {
      throw new Error("Palace frame timing request changed during capture");
    }
    if (evidence.state === "failed") {
      throw new Error(`Palace frame timing failed: ${evidence.failure}`);
    }
    if (evidence.state === "complete") {
      return validatePalaceFrameTimingMeasurement({
        measurementContract: palaceFrameTimingContract,
        frameWindow: {
          startFrame: evidence.startFrame,
          endFrame: evidence.endFrame,
          elapsedTimeUs: evidence.elapsedTimeUs,
        },
        sampleCount: evidence.samplesUs.length,
        samplesUs: evidence.samplesUs,
        summaries: {
          frameIntervalUs:
            summarizePalaceFrameIntervals(evidence.samplesUs),
        },
      });
    }
    await sleep(25);
  }
  throw new Error("Palace frame timing capture timed out");
}
