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
const gate4RunnerPath = fileURLToPath(
  new URL("../scripts/run-basecamp-gate4.sh", import.meta.url),
);
const gate2HarnessPath = fileURLToPath(
  new URL("./basecamp_gate2.mjs", import.meta.url),
);
const gate2WorkerPath = fileURLToPath(
  new URL("./basecamp_gate2_worker.mjs", import.meta.url),
);
const gate1HarnessPath = fileURLToPath(
  new URL("./basecamp_gate1.mjs", import.meta.url),
);
const gate3HarnessPath = fileURLToPath(
  new URL("./basecamp_gate3.mjs", import.meta.url),
);
const gate3WorkerPath = fileURLToPath(
  new URL("./basecamp_gate3_worker.mjs", import.meta.url),
);
const gate4HarnessPath = fileURLToPath(
  new URL("./basecamp_gate4.mjs", import.meta.url),
);
const standaloneScopeRunnerPath = fileURLToPath(
  new URL("../scripts/run-basecamp-standalone-scoped.sh", import.meta.url),
);
const flakePath = fileURLToPath(
  new URL("../flake.nix", import.meta.url),
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

test("resume invalidates public evidence after lock attestation", async () => {
  const source = await readFile(runnerPath, "utf8");
  const invalidation = source.indexOf(
    '"${bootstrap_tools}/bin/unlink" -- "${public_evidence}"',
  );
  const sourceReopen = source.indexOf(
    "MVP resume snapshot marker is missing or insecure",
  );
  const lockAttestation = source.indexOf(
    'lock_attestation="$(',
  );
  assert.notEqual(invalidation, -1);
  assert.notEqual(sourceReopen, -1);
  assert.notEqual(lockAttestation, -1);
  assert.ok(sourceReopen < lockAttestation);
  assert.ok(lockAttestation < invalidation);
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

test("runner attests a unique stopped scope before each gate executes", async () => {
  const source = await readFile(runnerPath, "utf8");
  const start = source.indexOf("run_gate() {");
  const end = source.indexOf("\nactive_gate_pid=", start);
  assert.notEqual(start, -1);
  assert.notEqual(end, -1);
  const runGate = source.slice(start, end);
  ordered(runGate, [
    '"${scope_control}" plan',
    '"${systemd_run}"',
    "--pdeathsig TERM",
    'builtin kill -STOP "$$"',
    '"${scope_guardian}"',
    '"${scope_control}" attest',
    '"${scope_control}" finish-launch',
    '"${scope_control}" release',
    'wait "${active_gate_pid}"',
    '"${scope_control}" cleanup',
  ]);
  assert.doesNotMatch(runGate, /\bkill\s+-CONT\b/);
  assert.match(
    runGate,
    /if \[ "\$PPID" != "\$expected_parent" \]; then[\s\S]*?builtin kill -STOP "\$\$"[\s\S]*?if \[ "\$PPID" != "\$expected_parent" \]; then/,
  );
  assert.match(
    runGate,
    /scope_unit="\$\{process_scope_prefix\}-\$\{gate\}-\$\{scope_attempt_id\}\.scope"/,
  );
  assert.match(
    source,
    /"\$\{scope_control\}" validate-cleaned[\s\S]*?"\$\{evidence\}" "\$\{gate\}" "\$\{expected_slice\}"/,
  );
  assert.match(
    source,
    /\|\| \[ -e "\$\{launch\}" \][\s\S]*?\|\| \[ -L "\$\{launch\}" \]/,
  );
  assert.match(
    runGate,
    /"\$\{scope_control\}" archive[\s\S]*?"\$\{scope_history\}"/,
  );
  const standalone = await readFile(standaloneScopeRunnerPath, "utf8");
  assert.match(standalone, /--pdeathsig TERM/);
  assert.match(standalone, /"\$\{scope_guardian\}"/);
  assert.match(standalone, /"\$\{scope_control\}" release/);
  assert.doesNotMatch(standalone, /\bkill\s+-CONT\b/);
});

test("standalone Gate 1 and Gate 2 runners create exact cleanup claims", async () => {
  for (const path of [gate1RunnerPath, gate2RunnerPath]) {
    const source = await readFile(path, "utf8");
    assert.match(
      source,
      /work_dir="\$\(cd "\$\{work_dir\}" && pwd -P\)"/,
    );
    assert.match(source, /chmod 700 "\$\{work_dir\}"/);
    assert.match(source, /must not inherit the MVP release lock FD/);
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

test("gate descendants never receive the release lock descriptor", async () => {
  for (const path of [
    gate1HarnessPath,
    gate2HarnessPath,
    gate2WorkerPath,
    gate3HarnessPath,
    gate3WorkerPath,
    gate4HarnessPath,
  ]) {
    const source = await readFile(path, "utf8");
    assert.match(source, /PALACE_MVP_LOCK_FD must not be inherited/);
    assert.doesNotMatch(source, /stdio\[lockFd\] = lockFd/);
    assert.doesNotMatch(source, /childStdioWithInheritedMvpLock/);
  }
});

test("render timing parsers match the exact Basecamp source pin", async () => {
  const flake = await readFile(flakePath, "utf8");
  const pinnedRevision = flake.match(
    /basecamp\.url = "github:3esmit\/logos-basecamp\/([0-9a-f]{40})";/,
  )?.[1];
  assert.match(pinnedRevision ?? "", /^[0-9a-f]{40}$/);
  for (const path of [gate2WorkerPath, gate3WorkerPath]) {
    const worker = await readFile(path, "utf8");
    assert.match(
      worker,
      new RegExp(`basecampRevision: "${pinnedRevision}"`),
    );
  }
});

test("runner uses exact close-on-exec kill-coupled lock handoff", async () => {
  const source = await readFile(runnerPath, "utf8");
  ordered(source, [
    'export PALACE_MVP_LOCK_SUPERVISED=1',
    'exec "${bootstrap_flock}"',
    "--close",
    '"${bootstrap_setpriv}" --pdeathsig KILL',
    '"${bootstrap_bash}" -p',
    '"${snapshot_runner}" "${runs_root}" "${run_dir}"',
    'lock_attestation="$(',
  ]);
  assert.match(
    source,
    /if \[ "\$\{PALACE_MVP_LOCK_FD\+x\}" = "x" \]; then/,
  );
  assert.match(
    source,
    /PALACE_MVP_CLAIM_PATH \\\n  PALACE_MVP_PROCESS_CGROUP/,
  );
});

test("runner retires persisted scope immediately after lock attestation", async () => {
  const source = await readFile(runnerPath, "utf8");
  ordered(source, [
    'lock_attestation="$(',
    'export PALACE_MVP_LOCK_SUPERVISOR_PID=',
    'scope_preflight_result="$(',
    '"${active_scope_preflight}"',
    "retire-before-release",
    'if [ "${resuming}" -eq 1 ]; then',
    '"${bootstrap_tools}/bin/unlink" -- "${public_evidence}"',
    'acceptance_tools="$(',
  ]);
});

test("active claim acquisition precedes every scoped gate execution", async () => {
  const source = await readFile(runnerPath, "utf8");
  ordered(source, [
    '"${claim_tool}"',
    "acquire-or-roll-forward",
    'run_or_skip_gate "gate1"',
    'run_or_skip_gate "gate2"',
    'run_or_skip_gate "gate3"',
    'run_or_skip_gate "gate4"',
  ]);
});

test("scope interruption never signals or waits on a numeric job PID", async () => {
  for (const path of [runnerPath, standaloneScopeRunnerPath]) {
    const source = await readFile(path, "utf8");
    assert.doesNotMatch(source, /\$\{!\}/);
    assert.doesNotMatch(source, /kill_active_job_if_owned/);
    assert.doesNotMatch(source, /\bkill\s+-(?:KILL|TERM)\b/);
    const handlerStart = source.indexOf(
      path === runnerPath
        ? "handle_runner_signal() {"
        : "handle_signal() {",
    );
    const handlerEnd = source.indexOf(
      path === runnerPath
        ? "\nprintf 'MVP run directory:"
        : "\ntrap 'handle_signal HUP'",
      handlerStart,
    );
    const handler = source.slice(handlerStart, handlerEnd);
    assert.doesNotMatch(handler, /\bwait\b/);
    assert.match(handler, /cleanup_status=\$\?/);
    assert.match(handler, /residue-killed/);
  }
});

test("scope cleanup failure exits before report work or child wait", async () => {
  const runner = await readFile(runnerPath, "utf8");
  const runnerHandler = runner.slice(
    runner.indexOf("handle_runner_signal() {"),
    runner.indexOf("\nprintf 'MVP run directory:"),
  );
  ordered(runnerHandler, [
    'if [ "${cleanup_status}" -ne 0 ]',
    "exact scope cleanup failed",
    "exit 1",
    "fail_run",
  ]);
  assert.doesNotMatch(runnerHandler, /\bwait\b/);
  for (const message of [
    "scope attestation and exact recovery failed",
    "scope transition and exact cleanup failed",
    "scope release and exact cleanup failed",
    "post-command exact scope cleanup failed",
  ]) {
    assert.match(
      runner,
      new RegExp(`${message.replaceAll(" ", "\\s+")}[^]*?exit 1`),
    );
  }
  const postWait = runner.slice(
    runner.indexOf('wait "${active_gate_pid}"'),
    runner.indexOf("\n}\n\nactive_gate_pid=", runner.indexOf(
      'wait "${active_gate_pid}"',
    )),
  );
  ordered(postWait, [
    'if [ "${cleanup_status}" -ne 0 ]',
    "post-command exact scope cleanup failed",
    "exit 1",
    'active_gate_pid=""',
    'if [ "${gate_status}" -ne 0 ]',
    "fail_run",
  ]);

  const standalone = await readFile(standaloneScopeRunnerPath, "utf8");
  const standaloneHandler = standalone.slice(
    standalone.indexOf("handle_signal() {"),
    standalone.indexOf("\ntrap 'handle_signal HUP'"),
  );
  ordered(standaloneHandler, [
    'if [ "${cleanup_status}" -ne 0 ]',
    "exact scope cleanup failed",
    "exit 1",
  ]);
  assert.doesNotMatch(standaloneHandler, /\bwait\b/);
});

test("gate process signals use pidfd identity, never numeric kill", async () => {
  for (const path of [
    gate1HarnessPath,
    gate2HarnessPath,
    gate2WorkerPath,
    gate3HarnessPath,
    gate3WorkerPath,
  ]) {
    const source = await readFile(path, "utf8");
    assert.match(source, /captureDirectChildIdentity/);
    assert.match(source, /signalDirectChild/);
    assert.doesNotMatch(source, /signalProcessGroup/);
    assert.doesNotMatch(source, /process\.kill\(-/);
    assert.doesNotMatch(
      source,
      /process\.kill\((?:pid|member\.pid|child\.pid),\s*"(?:SIGTERM|SIGKILL)"/,
    );
    assert.doesNotMatch(source, /\.kill\("(?:SIGTERM|SIGKILL)"\)/);
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

test("Gate 2 publication binds accepted traffic to persisted sequences", async () => {
  const source = await readFile(runnerPath, "utf8");
  const validatorStart = source.indexOf("def valid_gate2_core:");
  const validatorEnd = source.indexOf(
    "\n\n      .[0] as $candidate",
    validatorStart,
  );
  assert.notEqual(validatorStart, -1);
  assert.notEqual(validatorEnd, -1);
  const validator = source.slice(validatorStart, validatorEnd);
  for (const marker of [
    ".orderedSpeech.baselineStability",
    '"minimumQuietWindowMs",',
    '"observedQuietWindowMs",',
    ".baselineStability.statusProbe",
    ".baselineStability.sessionProbe",
    "$report.orderedSpeech.sequenceEvidence",
    "$report.orderedSpeech.sessionBefore",
    "$report.orderedSpeech.sessionAfter",
    "$accepted_delta",
    ".ingressSequences[",
    ".correlated == 0",
  ]) {
    assert.match(validator, new RegExp(
      marker.replaceAll(/[.*+?^${}()|[\]\\]/g, "\\$&"),
    ));
  }
  ordered(validator, [
    "$probe.received_accepted,",
    "$probe.received_rejected,",
    "$probe.rejected_scope,",
    "$probe.rejected_expired,",
    "$probe.rejected_signature,",
    "$probe.rejected_replay,",
    "$probe.rejected_payload,",
    "$probe.rejected_other,",
    "$probe.outbox,",
    "$probe.correlated,",
    "$before.received_accepted,",
    "$before.received_rejected,",
    "$before.rejected_scope,",
    "$before.rejected_expired,",
    "$before.rejected_signature,",
    "$before.rejected_replay,",
    "$before.rejected_payload,",
    "$before.rejected_other,",
    "$before.outbox,",
    "$before.correlated,",
    "$after.received_accepted,",
    "$after.received_rejected,",
    "$after.rejected_scope,",
    "$after.rejected_expired,",
    "$after.rejected_signature,",
    "$after.rejected_replay,",
    "$after.rejected_payload,",
    "$after.rejected_other,",
    "$after.outbox,",
    "$after.correlated",
    "valid_nonnegative_integer",
    "$before.outbox == 0",
    "$before.correlated == 0",
  ]);
  assert.match(
    validator,
    /\$probe\.senderKey == \$before\.senderKey[\s\S]*?\$probe\.egressSequence == \$before\.egressSequence[\s\S]*?\$probe\.ingressSequences == \$before\.ingressSequences/,
  );
  assert.equal(
    validator.split(
      "$report.orderedSpeech.sessionBefore[$receiver]",
    ).length - 1,
    1,
  );
  assert.equal(
    validator.split(
      "$report.orderedSpeech.sessionAfter[$receiver]",
    ).length - 1,
    1,
  );
  assert.doesNotMatch(
    validator,
    /received_accepted\s*\+\s*300/,
  );
});

test("Gate 4 provides the exact pidfd helper before worker startup", async () => {
  const source = await readFile(gate4RunnerPath, "utf8");
  ordered(source, [
    'export PALACE_PIDFD_SIGNAL="${acceptance_tools}/bin/palace-pidfd-signal"',
    'if [ ! -x "${PALACE_PIDFD_SIGNAL}" ]; then',
    "Gate 4 pidfd signal helper is unavailable",
    '"${acceptance_tools}/bin/node"',
    '"${product_snapshot}/tests/basecamp_gate4.mjs"',
  ]);
});

test("Gate 4 approves and binds the exact LEZ dependency before work", async () => {
  const [runner, gate4] = await Promise.all([
    readFile(runnerPath, "utf8"),
    readFile(gate4HarnessPath, "utf8"),
  ]);
  const approval = gate4.indexOf(
    "lezModuleRevision !== approvedLezModuleRevision",
  );
  const priorReportRead = gate4.indexOf(
    "const previousReport = await optionalJson(reportPath);",
  );
  assert.notEqual(approval, -1);
  assert.notEqual(priorReportRead, -1);
  assert.ok(approval < priorReportRead);
  assert.match(
    gate4,
    /const approvedLezModuleRevision =\s*"e8d84103660604b1a6a06ddd66d20da7a2fdeb3f";/,
  );
  assert.match(
    gate4,
    /const lezModuleRevision = dependencyRevisions\?\.lez_core\?\.revision;/,
  );
  assert.match(
    gate4,
    /releaseContract = \{[\s\S]*?network: \{[\s\S]*?lezModuleRevision,/,
  );
  assert.match(
    runner,
    /\$gate4\.releaseContract\.network\.lezModuleRevision\s+== \$gate4\.dependencyRevisions\.lez_core\.revision/,
  );
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
