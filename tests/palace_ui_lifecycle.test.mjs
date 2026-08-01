import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { test } from "node:test";
import {
  applyPreviewDestroyed,
  applyPreviewReadyTransition,
  canvasPixelsToProtocol,
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

test("canvas clicks map to bounded Palace protocol coordinates", () => {
  assert.deepEqual(canvasPixelsToProtocol(90, 110, 1000, 800), {
    x: 0,
    y: 0,
  });
  assert.deepEqual(canvasPixelsToProtocol(910, 580, 1000, 800), {
    x: 10000,
    y: 10000,
  });
  assert.deepEqual(canvasPixelsToProtocol(-100, 10000, 1000, 800), {
    x: 0,
    y: 10000,
  });
  assert.deepEqual(canvasPixelsToProtocol(Number.NaN, Number.NaN, 1000, 800), {
    x: 0,
    y: 0,
  });
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
  assert.match(mainQml, /objectName:\s*"palacePropBag"/);
  assert.match(mainQml, /objectName:\s*"palacePropTrash"/);
  assert.match(mainQml, /objectName:\s*"palacePropBagPanel"/);
  assert.match(mainQml, /objectName:\s*"palaceUserListToggle"/);
  assert.match(mainQml, /Users:\s*"\s*\+\s*root\.participants\.length/);
});

test("Main.qml maps canvas clicks below actors and exposes fixed rooms", () => {
  assert.match(
    mainQml,
    /function canvasPixelToProtocol\s*\(\s*pixel,\s*inset,\s*usableSpan\s*\)/,
  );
  assert.match(
    mainQml,
    /function canvasPixelsToProtocol\s*\(\s*pixelX,\s*pixelY\s*\)/,
  );
  assert.match(mainQml, /objectName:\s*"palaceRoomMoveSurface"/);
  assert.match(
    mainQml,
    /objectName:\s*"palaceRoomMoveSurface"[\s\S]{0,220}z:\s*2/,
  );
  assert.match(
    mainQml,
    /var coordinate\s*=\s*root\.canvasPixelsToProtocol\(\s*mouse\.x,\s*mouse\.y\s*\)[\s\S]{0,120}root\.gate2Move\(coordinate\.x,\s*coordinate\.y\)/,
  );

  const moveSurfaceOffset = mainQml.indexOf('objectName: "palaceRoomMoveSurface"');
  const participantOffset = mainQml.indexOf('objectName: "palaceParticipants"');
  const doorOffset = mainQml.indexOf('objectName: "palaceRoomDoor"');
  assert.ok(moveSurfaceOffset >= 0 && moveSurfaceOffset < participantOffset);
  assert.ok(moveSurfaceOffset >= 0 && moveSurfaceOffset < doorOffset);

  assert.match(mainQml, /property bool roomListOpen:\s*false/);
  assert.match(mainQml, /objectName:\s*"palaceRoomListPanel"/);
  assert.match(mainQml, /objectName:\s*"palaceRoomListAtrium"/);
  assert.match(mainQml, /objectName:\s*"palaceRoomListLounge"/);
  assert.match(
    mainQml,
    /objectName:\s*"palaceRoomListAtrium"[\s\S]{0,420}root\.selectFixedRoom\("atrium"\)/,
  );
  assert.match(
    mainQml,
    /objectName:\s*"palaceRoomListLounge"[\s\S]{0,520}root\.selectFixedRoom\("lounge"\)/,
  );
  assert.match(mainQml, /function selectFixedRoom\s*\(\s*roomId\s*\)/);
  assert.match(
    mainQml,
    /if \(selectedRoom === currentRoom\)[\s\S]{0,360}watchedActionReceipt\s*=\s*"ok=room-current;[\s\S]{0,180}return watchedActionReceipt/,
  );
  assert.match(
    mainQml,
    /selectedRoom === "lounge"[\s\S]{0,180}root\.gate5UseDoor\(\)/,
  );
  assert.match(
    mainQml,
    /selectedRoom === "atrium"[\s\S]{0,220}backend\.enterRoom\("atrium"\)/,
  );
  assert.match(
    mainQml,
    /objectName:\s*"palaceToolboxRooms"[\s\S]{0,420}root\.roomListOpen\s*=\s*!root\.roomListOpen/,
  );
});

test("backend stops poll timers on teardown", () => {
  assert.match(backendH, /~LogosPalaceUiBackend\(\)/);
  assert.match(backendCpp, /stopPollingTimers/);
  assert.match(backendCpp, /m_deliveryPollTimer->stop\(\)/);
  assert.match(backendCpp, /m_nodeEvidencePollTimer->stop\(\)/);
});
