import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { test } from "node:test";
import {
  applyPreviewDestroyed,
  applyPreviewReadyTransition,
  clampNonNegative,
  idleImportState,
  resetAuthoringPreviewCount,
  shouldCancelOrphanStage,
} from "./palace_ui_lifecycle.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const mainQml = readFileSync(
  join(root, "packages/logos_palace_ui/src/qml/Main.qml"),
  "utf8",
);
const backendCpp = readFileSync(
  join(root, "packages/logos_palace_ui/src/logos_palace_ui_backend.cpp"),
  "utf8",
);
const backendH = readFileSync(
  join(root, "packages/logos_palace_ui/src/logos_palace_ui_backend.h"),
  "utf8",
);

test("preview ready transitions never go negative and balance destroy", () => {
  let count = 0;
  let a = false;
  let b = false;
  ({ nextCount: count, nextCounted: a } = applyPreviewReadyTransition(
    count,
    a,
    true,
  ));
  ({ nextCount: count, nextCounted: b } = applyPreviewReadyTransition(
    count,
    b,
    true,
  ));
  assert.equal(count, 2);
  ({ nextCount: count, nextCounted: a } = applyPreviewDestroyed(count, a));
  assert.equal(count, 1);
  assert.equal(a, false);
  ({ nextCount: count, nextCounted: b } = applyPreviewDestroyed(count, b));
  assert.equal(count, 0);
  // Double-destroy is a no-op
  ({ nextCount: count, nextCounted: b } = applyPreviewDestroyed(count, b));
  assert.equal(count, 0);
  assert.equal(clampNonNegative(-3), 0);
});

test("authoring close resets preview count to zero", () => {
  assert.equal(resetAuthoringPreviewCount(), 0);
});

test("import abandon yields idle flags and bumps generation", () => {
  const idle = idleImportState(4);
  assert.equal(idle.running, false);
  assert.equal(idle.phase, "idle");
  assert.equal(idle.session, "");
  assert.equal(idle.generation, 5);
  assert.equal(idle.trace, null);
});

test("orphan stage sessions are cancelled only when unmatched", () => {
  assert.equal(
    shouldCancelOrphanStage("0123456789abcdef0123456789abcdef", false),
    true,
  );
  assert.equal(
    shouldCancelOrphanStage("0123456789abcdef0123456789abcdef", true),
    false,
  );
  assert.equal(shouldCancelOrphanStage("not-a-session", false), false);
});

test("Main.qml wires destroy/reset cleanup on real paths", () => {
  assert.match(mainQml, /function noteAuthoringPreviewReady/);
  assert.match(mainQml, /function noteAuthoringPreviewLost/);
  assert.match(mainQml, /function resetAuthoringPreviewState/);
  assert.match(mainQml, /Component\.onDestruction:\s*root\.abandonAssetImport\(\)/);
  assert.match(mainQml, /resetAuthoringPreviewState\(\)/);
  assert.match(mainQml, /active:\s*root\.backgroundModerationOpen/);
  assert.match(mainQml, /objectName:\s*"palaceToolbox"/);
  assert.match(mainQml, /objectName:\s*"palaceStatusStrip"/);
  assert.match(mainQml, /objectName:\s*"palaceInputStrip"/);
});

test("backend stops poll timers on teardown", () => {
  assert.match(backendH, /~LogosPalaceUiBackend\(\)/);
  assert.match(backendCpp, /stopPollingTimers/);
  assert.match(backendCpp, /m_deliveryPollTimer->stop\(\)/);
  assert.match(backendCpp, /m_nodeEvidencePollTimer->stop\(\)/);
});
