#!/usr/bin/env node

import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { fileURLToPath } from "node:url";

const runnerPath = fileURLToPath(
  new URL("../scripts/run-basecamp-mvp.sh", import.meta.url),
);
const gate1RunnerPath = fileURLToPath(
  new URL("../scripts/run-basecamp-gate1.sh", import.meta.url),
);
const gate2RunnerPath = fileURLToPath(
  new URL("../scripts/run-basecamp-gate2.sh", import.meta.url),
);
const gate2HarnessPath = fileURLToPath(
  new URL("./basecamp_gate2.mjs", import.meta.url),
);
const gate1HarnessPath = fileURLToPath(
  new URL("./basecamp_gate1.mjs", import.meta.url),
);
const gate3HarnessPath = fileURLToPath(
  new URL("./basecamp_gate3.mjs", import.meta.url),
);
const gate4HarnessPath = fileURLToPath(
  new URL("./basecamp_gate4.mjs", import.meta.url),
);

function ordered(source, markers) {
  let offset = 0;
  for (const marker of markers) {
    const index = source.indexOf(marker, offset);
    assert.notEqual(index, -1, `missing runner marker: ${marker}`);
    offset = index + marker.length;
  }
}

test("runner invalidates stale public evidence at claim-state boundary", async () => {
  const source = await readFile(runnerPath, "utf8");
  assert.match(
    source,
    /if \[ "\$\{claim_state_status\}" -ne 0 \]; then[\s\S]*?\nfi\ninvalidate_public_evidence\ncase "\$\{claim_state\}" in/,
  );
});

test("resume invalidates public evidence before source reopening", async () => {
  const source = await readFile(runnerPath, "utf8");
  const earlyInvalidation = source.indexOf(
    'unlink -- "${public_evidence}"',
  );
  const sourceReopen = source.indexOf(
    "MVP resume snapshot marker is missing or insecure",
  );
  assert.notEqual(earlyInvalidation, -1);
  assert.notEqual(sourceReopen, -1);
  assert.ok(earlyInvalidation < sourceReopen);
});

test("runner publishes only through completed-claim finalization", async () => {
  const source = await readFile(runnerPath, "utf8");
  assert.equal(
    source.split("build_public_evidence.mjs").length - 1,
    1,
  );
  assert.equal(source.split("cleanup == {").length - 1, 12);

  const finalizerStart = source.indexOf("finalize_completed_run() {");
  const finalizerEnd = source.indexOf(
    '\nif [ "${claim_completed}" -eq 1 ]; then',
    finalizerStart,
  );
  assert.notEqual(finalizerStart, -1);
  assert.notEqual(finalizerEnd, -1);
  const finalizer = source.slice(finalizerStart, finalizerEnd);
  ordered(finalizer, [
    "invalidate_public_evidence",
    'prior_run_complete "${run_dir}"',
    '"${claim_tool}" completion',
    "build_public_evidence.mjs",
    'Public MVP evidence: %s\\n',
  ]);

  const terminalStart = source.lastIndexOf(
    'if ! prior_run_complete "${run_dir}"; then',
  );
  const terminal = source.slice(terminalStart);
  ordered(terminal, [
    'prior_run_complete "${run_dir}"',
    "invalidate_public_evidence",
    "claim_completed=1",
    '"${claim_tool}" complete',
    "finalize_completed_run",
  ]);
});

test("standalone Gate 1 and Gate 2 runners create exact cleanup claims", async () => {
  for (const path of [gate1RunnerPath, gate2RunnerPath]) {
    const source = await readFile(path, "utf8");
    assert.match(
      source,
      /work_dir="\$\(cd "\$\{work_dir\}" && pwd -P\)"/,
    );
    assert.match(source, /chmod 700 "\$\{work_dir\}"/);
    assert.match(
      source,
      /PALACE_MVP_CLAIM_PATH and PALACE_MVP_LOCK_FD must be provided together/,
    );
    assert.match(
      source,
      /PALACE_MVP_CLAIM_PATH="\$\{work_dir\}\/standalone-cleanup-claim"/,
    );
    assert.match(source, /chmod 600 "\$\{PALACE_MVP_CLAIM_PATH\}"/);
    assert.equal(
      source.split("export PALACE_MVP_CLAIM_PATH").length - 1,
      1,
    );
  }
});

test("Gate 2 signal handlers precede first worker spawn", async () => {
  const source = await readFile(gate2HarnessPath, "utf8");
  const signalHandler = source.indexOf(
    'process.on("SIGTERM", () => requestTermination("SIGTERM"));',
  );
  const workerLoop = source.indexOf(
    "for (const [index, label] of allWorkerLabels.entries())",
  );
  assert.notEqual(signalHandler, -1);
  assert.notEqual(workerLoop, -1);
  assert.ok(signalHandler < workerLoop);
});

test("all gate signals synchronously block PASS", async () => {
  for (const [path, gate] of [
    [gate1HarnessPath, 1],
    [gate2HarnessPath, 2],
    [gate3HarnessPath, 3],
    [gate4HarnessPath, 4],
  ]) {
    const source = await readFile(path, "utf8");
    const handlerStart = source.indexOf("function requestTermination(signal) {");
    const cleanupStart = source.indexOf(
      "terminationPromise = (async () => {",
      handlerStart,
    );
    const failureMarker = source.indexOf(
      `failure ??= new Error(\`Gate ${gate} termination requested by \${signal}\`);`,
      handlerStart,
    );
    assert.notEqual(handlerStart, -1);
    assert.notEqual(cleanupStart, -1);
    assert.notEqual(failureMarker, -1);
    assert.ok(failureMarker < cleanupStart);
    assert.match(
      source,
      new RegExp(
        `run-owned child remained in Gate ${gate} process group`,
      ),
    );
    if (gate <= 2) {
      assert.match(
        source,
        /if \(terminationPromise\) await terminationPromise;/,
      );
    }
  }
});
