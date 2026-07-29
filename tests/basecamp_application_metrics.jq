def palace_application_exact_object_keys($expected):
  type == "object"
    and ((keys | sort) == ($expected | sort));

def palace_application_nonnegative_integer:
  type == "number"
    and . >= 0
    and . <= 9007199254740991
    and floor == .;

def palace_valid_application_measurement($bytes):
  try (
    . as $measurement
    | (
        $measurement.samples
        | map(.roundTripMs)
        | sort
      ) as $durations
    | (
        $measurement
        | palace_application_exact_object_keys([
            "payloadUtf8Bytes",
            "requestUtf8Bytes",
            "responseUtf8Bytes",
            "latency",
            "samples"
          ])
      )
      and $measurement.payloadUtf8Bytes == $bytes
      and $measurement.requestUtf8Bytes == $bytes
      and $measurement.responseUtf8Bytes == $bytes
      and ($measurement.samples | type) == "array"
      and ($measurement.samples | length) == 20
      and all(
        range(0; 20);
        . as $index
        | $measurement.samples[$index] as $sample
        | (
            $sample
            | palace_application_exact_object_keys([
                "ordinal",
                "requestUtf8Bytes",
                "responseUtf8Bytes",
                "roundTripMs"
              ])
          )
          and $sample.ordinal == ($index + 1)
          and $sample.requestUtf8Bytes == $bytes
          and $sample.responseUtf8Bytes == $bytes
          and (
            $sample.roundTripMs
            | palace_application_nonnegative_integer
          )
      )
      and $measurement.latency == {
        sampleCount: 20,
        p50Ms: $durations[9],
        p95Ms: $durations[18],
        maxMs: $durations[19]
      }
  ) catch false;

def palace_valid_application_round_trip:
  try (
    . as $metric
    | (
        $metric
        | palace_application_exact_object_keys([
            "status",
            "clock",
            "startBoundary",
            "endBoundary",
            "payloadSemantics",
            "samplesPerSize",
            "measurements",
            "rejectedUnsupportedSize"
          ])
      )
      and $metric.status == "passed"
      and $metric.clock
        == "worker performance.now monotonic milliseconds"
      and $metric.startBoundary
        == "immediately before inspector invokes the QML UI-backend call"
      and $metric.endBoundary
        == "invocationSequence advanced and exact raw echo property was observed"
      and $metric.payloadSemantics
        == "application UTF-8 bytes; not transport wire bytes"
      and $metric.samplesPerSize == 20
      and (
        $metric.measurements
        | palace_application_exact_object_keys(["0", "256", "4096"])
      )
      and (
        $metric.measurements["0"]
        | palace_valid_application_measurement(0)
      )
      and (
        $metric.measurements["256"]
        | palace_valid_application_measurement(256)
      )
      and (
        $metric.measurements["4096"]
        | palace_valid_application_measurement(4096)
      )
      and $metric.rejectedUnsupportedSize
        == "rejected=application-round-trip-size"
  ) catch false;
