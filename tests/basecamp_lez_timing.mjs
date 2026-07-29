export const lezStageTimingFields = Object.freeze([
  "submitMs",
  "observeMs",
  "finalityMs",
  "totalMs",
]);

export const lezMeasurementBoundaries = Object.freeze({
  submitMs:
    "worker Date.now at first submit-capable client invocation start to worker Date.now at first response proving durable submitted_to_lez acceptance or a later durable stage",
  observeMs:
    "durable submission acceptance response to first client response proving observed or finalized, using worker Date.now timestamps",
  finalityMs:
    "first client response proving observed to first client response proving finalized, using worker Date.now timestamps; measured-coalesced means one response first proved both and duration is exactly zero",
  totalMs:
    "first submit-capable client invocation start to first client response proving finalized, using worker Date.now timestamps",
});

export function validateObservationTimingEnvelope(result, description) {
  const startedAtUnixMs = Number(result?.startedAtUnixMs);
  const completedAtUnixMs = Number(result?.completedAtUnixMs);
  const elapsedMs = Number(result?.elapsedMs);
  if (
    !Number.isSafeInteger(startedAtUnixMs)
    || startedAtUnixMs <= 0
    || !Number.isSafeInteger(completedAtUnixMs)
    || completedAtUnixMs < startedAtUnixMs
    || !Number.isSafeInteger(elapsedMs)
    || elapsedMs < 0
  ) {
    throw new Error(
      `${description} omitted exact observation timing`,
    );
  }
  return Object.freeze({
    startedAtUnixMs,
    completedAtUnixMs,
    elapsedMs,
  });
}

export function isCompleteTimingMeasurement(field, status, value) {
  return (
    status === "measured"
    && Number.isSafeInteger(value)
    && value >= 0
  ) || (
    field === "finalityMs"
    && status === "measured-coalesced"
    && value === 0
  );
}

export function timingBoundaryNames(field) {
  if (!lezStageTimingFields.includes(field)) {
    throw new Error(`unsupported LEZ timing field ${field}`);
  }
  const phase = field.replace(/Ms$/, "");
  return {
    started: `${phase}StartedAtUnixMs`,
    completed: `${phase}CompletedAtUnixMs`,
  };
}

export function resumableTimings(priorRecord) {
  const timings = {};
  const measurement = {};
  for (const field of lezStageTimingFields) {
    const value = priorRecord?.timings?.[field];
    const status = priorRecord?.timingMeasurement?.[field];
    const names = timingBoundaryNames(field);
    const started = priorRecord?.timingBoundaries?.[names.started];
    const completed = priorRecord?.timingBoundaries?.[names.completed];
    if (
      isCompleteTimingMeasurement(field, status, value)
      && Number.isSafeInteger(started)
      && started > 0
      && Number.isSafeInteger(completed)
      && completed >= started
      && completed - started === value
    ) {
      timings[field] = value;
      measurement[field] = status;
    }
  }
  return { timings, measurement };
}

export function setMeasuredTiming(record, field, value) {
  if (!lezStageTimingFields.includes(field)) {
    throw new Error(`unsupported LEZ timing field ${field}`);
  }
  if (!Number.isSafeInteger(value) || value < 0) {
    throw new Error(`invalid LEZ timing ${field}=${value}`);
  }
  record.timings ??= {};
  record.timingMeasurement ??= {};
  record.timings[field] = value;
  record.timingMeasurement[field] = "measured";
}

export function setCoalescedFinalityTiming(record, observedAtUnixMs) {
  const field = "finalityMs";
  const names = timingBoundaryNames(field);
  if (!Number.isSafeInteger(observedAtUnixMs) || observedAtUnixMs <= 0) {
    throw new Error("invalid coalesced finality observation timestamp");
  }
  record.timings ??= {};
  record.timingMeasurement ??= {};
  record.timingBoundaries ??= {};
  record.timingBoundaries[names.started] = observedAtUnixMs;
  record.timingBoundaries[names.completed] = observedAtUnixMs;
  record.timings[field] = 0;
  record.timingMeasurement[field] = "measured-coalesced";
}

export function startTimingBoundary(record, field, observedAtUnixMs) {
  const names = timingBoundaryNames(field);
  record.timingBoundaries ??= {};
  const prior = record.timingBoundaries[names.started];
  if (prior !== undefined) {
    if (!Number.isSafeInteger(prior) || prior <= 0) {
      throw new Error(`invalid persisted ${field} start boundary`);
    }
    return prior;
  }
  if (!Number.isSafeInteger(observedAtUnixMs) || observedAtUnixMs <= 0) {
    throw new Error(`invalid ${field} start observation timestamp`);
  }
  record.timingBoundaries[names.started] = observedAtUnixMs;
  return observedAtUnixMs;
}

export function completeTimingBoundary(record, field, observedAtUnixMs) {
  const names = timingBoundaryNames(field);
  const started = record.timingBoundaries?.[names.started];
  if (
    !Number.isSafeInteger(started)
    || started <= 0
    || !Number.isSafeInteger(observedAtUnixMs)
    || observedAtUnixMs < started
  ) {
    throw new Error(`${field} wall-clock boundary moved backwards`);
  }
  record.timingBoundaries[names.completed] = observedAtUnixMs;
  setMeasuredTiming(record, field, observedAtUnixMs - started);
}

export function markRecoveredTimingUnmeasured(record, field) {
  record.timings ??= {};
  record.timingMeasurement ??= {};
  record.timings[field] = null;
  record.timingMeasurement[field] = "recovered-unmeasured";
}

export function completedTimingTimestamp(record, field) {
  const completed =
    record.timingBoundaries?.[timingBoundaryNames(field).completed];
  if (!Number.isSafeInteger(completed) || completed <= 0) {
    throw new Error(`${field} has no exact completion observation timestamp`);
  }
  return completed;
}

export function unusablePersistedTimingFields(priorRecord, record) {
  return lezStageTimingFields.filter((field) => {
    const names = timingBoundaryNames(field);
    const priorHasCompletionEvidence = (
      priorRecord?.timings?.[field] !== undefined
      || priorRecord?.timingMeasurement?.[field] !== undefined
      || priorRecord?.timingBoundaries?.[names.completed] !== undefined
    );
    return (
      priorHasCompletionEvidence
      && !isCompleteTimingMeasurement(
        field,
        record.timingMeasurement?.[field],
        record.timings?.[field],
      )
    );
  });
}

export function normalizePersistedTimingMeasurements(record) {
  const prior = {
    timings: { ...(record.timings ?? {}) },
    timingMeasurement: { ...(record.timingMeasurement ?? {}) },
    timingBoundaries: { ...(record.timingBoundaries ?? {}) },
  };
  const normalized = resumableTimings(prior);
  record.timings = normalized.timings;
  record.timingMeasurement = normalized.measurement;
  const unavailable = unusablePersistedTimingFields(prior, record);
  for (const field of unavailable) {
    markRecoveredTimingUnmeasured(record, field);
  }
  return unavailable;
}

export function missingTimingEvidenceForDurableStage(record, durableStage) {
  const requiredCompleted = durableStage === "finalized"
    ? lezStageTimingFields
    : durableStage === "observed"
      ? ["submitMs", "observeMs"]
      : durableStage === "submitted_to_lez"
        ? ["submitMs"]
        : [];
  const missing = requiredCompleted.filter(
    (field) =>
      !isCompleteTimingMeasurement(
        field,
        record.timingMeasurement?.[field],
        record.timings?.[field],
      ),
  );
  const totalStarted =
    record.timingBoundaries?.[
      timingBoundaryNames("totalMs").started
    ];
  const submitStarted =
    record.timingBoundaries?.[
      timingBoundaryNames("submitMs").started
    ];
  if (durableStage === "queued") {
    if (!Number.isSafeInteger(submitStarted) || submitStarted <= 0) {
      missing.push("submitMs");
    }
    if (!Number.isSafeInteger(totalStarted) || totalStarted <= 0) {
      missing.push("totalMs");
    }
  }
  if (
    ["submitted_to_lez", "observed"].includes(durableStage)
    && (
      !Number.isSafeInteger(totalStarted)
      || totalStarted <= 0
    )
  ) {
    missing.push("totalMs");
  }
  return [...new Set(missing)];
}

export function crossBoundaryMismatchFields(record) {
  const unavailable = new Set();
  const boundaries = record.timingBoundaries ?? {};
  const exact = (field) =>
    isCompleteTimingMeasurement(
      field,
      record.timingMeasurement?.[field],
      record.timings?.[field],
    );
  const compare = (leftField, leftName, rightField, rightName) => {
    const left = boundaries[timingBoundaryNames(leftField)[leftName]];
    const right = boundaries[timingBoundaryNames(rightField)[rightName]];
    if (
      Number.isSafeInteger(left)
      && Number.isSafeInteger(right)
      && left !== right
    ) {
      unavailable.add(leftField);
      unavailable.add(rightField);
    }
  };

  compare("submitMs", "started", "totalMs", "started");
  if (exact("submitMs")) {
    compare("submitMs", "completed", "observeMs", "started");
  }
  if (exact("observeMs")) {
    compare("observeMs", "completed", "finalityMs", "started");
  }
  if (exact("finalityMs") && exact("totalMs")) {
    compare("finalityMs", "completed", "totalMs", "completed");
  }
  return [...unavailable];
}

export function invalidPersistedTimingBoundaryFields(record) {
  const boundaries = record.timingBoundaries ?? {};
  return lezStageTimingFields.filter((field) => {
    const names = timingBoundaryNames(field);
    const started = boundaries[names.started];
    const completed = boundaries[names.completed];
    return (
      (
        started !== undefined
        && (!Number.isSafeInteger(started) || started <= 0)
      )
      || (
        completed !== undefined
        && (
          !Number.isSafeInteger(completed)
          || !Number.isSafeInteger(started)
          || started <= 0
          || completed < started
        )
      )
    );
  });
}

export function recoverPersistedTimingEvidence(
  record,
  durableStage,
  priorRecord = record,
) {
  let unavailablePersisted;
  if (priorRecord === record) {
    unavailablePersisted = normalizePersistedTimingMeasurements(record);
  } else {
    const normalized = resumableTimings(priorRecord);
    record.timings = normalized.timings;
    record.timingMeasurement = normalized.measurement;
    unavailablePersisted = unusablePersistedTimingFields(
      priorRecord,
      record,
    );
  }
  const unavailable = [
    ...new Set([
      ...unavailablePersisted,
      ...missingTimingEvidenceForDurableStage(record, durableStage),
      ...invalidPersistedTimingBoundaryFields(record),
      ...crossBoundaryMismatchFields(record),
    ]),
  ];
  for (const field of unavailable) {
    markRecoveredTimingUnmeasured(record, field);
  }
  return unavailable;
}

export function finalizeLezTimingEvidence(record) {
  for (const field of lezStageTimingFields) {
    if (
      !isCompleteTimingMeasurement(
        field,
        record.timingMeasurement?.[field],
        record.timings?.[field],
      )
    ) {
      markRecoveredTimingUnmeasured(record, field);
    }
  }
}
