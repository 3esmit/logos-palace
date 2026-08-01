#!/usr/bin/env node

import { createHash } from "node:crypto";
import { createWriteStream, writeSync } from "node:fs";
import { mkdir, writeFile } from "node:fs/promises";
import { spawn } from "node:child_process";
import { basename, join, resolve } from "node:path";
import { createInterface } from "node:readline";
import { pathToFileURL } from "node:url";
import {
  captureDirectChildIdentity,
  signalDirectChild,
  waitForDirectChildExit,
} from "./basecamp_direct_child.mjs";
import {
  capturePalaceFrameTiming,
  palaceFrameTimingContract,
} from "./basecamp_frame_timing.mjs";
import {
  acceptsLezStartupObservation,
  workerInvocationTimeoutLimit,
} from "./basecamp_lez_startup.mjs";
import {
  newlyOpenedFileDialogId,
  uniqueFileDialogIds,
} from "./basecamp_file_dialogs.mjs";

const [
  basecampArgument,
  userDirArgument,
  artifactsArgument,
  labelArgument,
] = process.argv.slice(2);
if (
  !basecampArgument ||
  !userDirArgument ||
  !artifactsArgument ||
  !labelArgument
) {
  throw new Error(
    "usage: node tests/basecamp_gate3_worker.mjs <Basecamp> <user-dir> <artifacts-dir> <label>",
  );
}

const qtMcpRoot = process.env.LOGOS_QT_MCP;
if (!qtMcpRoot) {
  throw new Error("LOGOS_QT_MCP must point to the pinned logos-qt-mcp output");
}
if (!process.env.QML_INSPECTOR_PORT) {
  throw new Error("QML_INSPECTOR_PORT must be set before importing framework");
}
if (
  process.env.PALACE_BASECAMP_REV
  !== palaceFrameTimingContract.basecampRevision
) {
  throw new Error(
    "frame timing contract requires review for the pinned Basecamp revision",
  );
}

const basecamp = resolve(basecampArgument);
const userDir = resolve(userDirArgument);
const artifactsDir = resolve(artifactsArgument);
const label = labelArgument.toLowerCase();
if (!/^[a-z][a-z0-9-]{0,31}$/.test(label)) {
  throw new Error(`invalid worker label: ${labelArgument}`);
}
await mkdir(artifactsDir, { recursive: true });

const frameworkUrl = pathToFileURL(
  resolve(qtMcpRoot, "test-framework/framework.mjs"),
).href;
const { App, Inspector } = await import(frameworkUrl);

const sleep = (milliseconds) =>
  new Promise((resolveSleep) => setTimeout(resolveSleep, milliseconds));

function childStdioWithoutReleaseLock(baseStdio) {
  if (process.env.PALACE_MVP_LOCK_FD !== undefined) {
    throw new Error("PALACE_MVP_LOCK_FD must not be inherited");
  }
  return baseStdio;
}

function propertyMap(response) {
  if (response.error) {
    throw new Error(`getProperties failed: ${response.error}`);
  }
  return Object.fromEntries(
    (response.properties ?? []).map((property) => [
      property.name,
      property.value,
    ]),
  );
}

let processState;
let inspector;
let app;
let rootObjectId;
let shuttingDown = false;

function launchBasecamp() {
  const child = spawn(
    basecamp,
    ["--user-dir", userDir, "-platform", "offscreen"],
    {
      env: {
        ...process.env,
        QT_QPA_PLATFORM: "offscreen",
      },
      detached: true,
      stdio: childStdioWithoutReleaseLock(["ignore", "pipe", "pipe"]),
    },
  );
  const childIdentity = captureDirectChildIdentity(child);
  const stdoutChunks = [];
  const stderrChunks = [];
  child.stdout.on("data", (chunk) => stdoutChunks.push(chunk));
  child.stderr.on("data", (chunk) => stderrChunks.push(chunk));
  const exited = new Promise((resolveExit) => {
    child.once("exit", (code, signal) => resolveExit({ code, signal }));
  });
  processState = {
    child,
    childIdentity,
    exited,
    stdoutChunks,
    stderrChunks,
  };
  writeSync(
    process.stdout.fd,
    `${JSON.stringify({
      event: "basecamp-started",
      basecampPid: child.pid,
    })}\n`,
  );
}

async function saveLogs(state) {
  await Promise.all([
    writeFile(
      join(artifactsDir, `basecamp-${label}.stdout.log`),
      Buffer.concat(state.stdoutChunks),
    ),
    writeFile(
      join(artifactsDir, `basecamp-${label}.stderr.log`),
      Buffer.concat(state.stderrChunks),
    ),
  ]);
}

async function stopBasecamp() {
  inspector?.disconnect();
  inspector = undefined;
  app = undefined;
  rootObjectId = undefined;
  if (!processState) return;

  const state = processState;
  processState = undefined;
  if (
    state.child.exitCode === null
    && state.child.signalCode === null
  ) {
    await signalDirectChild(state.childIdentity, "SIGTERM");
    await waitForDirectChildExit(
      state.exited,
      15_000,
      `Gate 3 ${label} Basecamp`,
    );
  }
  await saveLogs(state);
}

async function connectInspector() {
  let lastError = new Error("inspector did not start");
  for (let attempt = 0; attempt < 240; attempt += 1) {
    if (!processState || processState.child.exitCode !== null) {
      throw new Error(
        `Basecamp ${label} exited before inspector connection`,
      );
    }
    const candidate = new Inspector();
    try {
      await candidate.connect();
      inspector = candidate;
      app = new App(candidate);
      return;
    } catch (error) {
      lastError = error;
      candidate.disconnect();
      await sleep(500);
    }
  }
  throw lastError;
}

async function waitForView() {
  await app.waitFor(
    async () => {
      await app.click("Logos Palace");
    },
    {
      timeout: 60_000,
      interval: 500,
      description: `Logos Palace launcher ${label}`,
    },
  );
  await app.waitFor(
    async () => {
      try {
        await app.expectTexts(["Atrium", "Door to Lounge"]);
      } catch (error) {
        await app.click("Logos Palace");
        throw error;
      }
    },
    {
      timeout: 60_000,
      interval: 500,
      description: `Logos Palace content ${label}`,
    },
  );
  await app.waitFor(
    async () => {
      const result = await app.findByProperty(
        "objectName",
        "palaceGate2Root",
      );
      if (result.error || !result.matches || result.matches.length !== 1) {
        throw new Error(
          `expected one Palace root, got ${result.matches?.length ?? 0}`,
        );
      }
      rootObjectId = result.matches[0].id;
    },
    {
      timeout: 60_000,
      interval: 500,
      description: `Palace root ${label}`,
    },
  );
}

async function startBasecamp() {
  const startedAt = performance.now();
  launchBasecamp();
  await connectInspector();
  await waitForView();
  return {
    basecampPid: processState.child.pid,
    startupMs: Math.round(performance.now() - startedAt),
  };
}

async function rootProperties() {
  if (!app || !rootObjectId) throw new Error("worker is not initialized");
  return propertyMap(await app.getProperties(rootObjectId));
}

async function evaluate(expression) {
  if (!inspector || !rootObjectId) throw new Error("worker is not initialized");
  if (typeof expression !== "string" || expression.length > 70_000) {
    throw new Error("invalid evaluate expression");
  }
  const response = await inspector.send("evaluate", {
    expression,
    objectId: rootObjectId,
  });
  if (response.error) {
    throw new Error(`evaluate failed: ${response.error}`);
  }
  return {
    result: response.result,
    undefined: response.undefined,
  };
}

async function saveScreenshot(name) {
  if (!app) throw new Error("worker is not initialized");
  if (!/^[a-z0-9][a-z0-9._-]{0,127}\.png$/.test(name)) {
    throw new Error(`invalid screenshot name: ${name}`);
  }
  const response = await app.screenshot();
  if (
    response.error
    || typeof response.image !== "string"
    || response.image.length === 0
    || !Number.isSafeInteger(response.width)
    || response.width <= 0
    || !Number.isSafeInteger(response.height)
    || response.height <= 0
  ) {
    throw new Error(`screenshot failed: ${response.error ?? "invalid image"}`);
  }
  const bytes = Buffer.from(response.image, "base64");
  if (
    bytes.length < 24
    || bytes.length > 64 * 1024 * 1024
    || !bytes.subarray(0, 8).equals(
      Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    )
  ) {
    throw new Error("screenshot is not a bounded PNG");
  }
  const path = join(artifactsDir, name);
  await writeFile(path, bytes, { mode: 0o600 });
  return {
    file: basename(path),
    artifactPath: basename(path),
    width: response.width,
    height: response.height,
    byteLength: bytes.length,
    sha256: createHash("sha256").update(bytes).digest("hex"),
  };
}

function parseAssetAuthoringEvidence(encoded) {
  let evidence;
  try {
    evidence = JSON.parse(String(encoded));
  } catch {
    throw new Error("asset authoring evidence is not JSON");
  }
  const keys = [
    "schema",
    "version",
    "open",
    "cardCount",
    "readyImageCount",
    "publishedCount",
    "atriumAssigned",
    "loungeAssigned",
    "propAssigned",
    "fenceRequest",
    "fenceState",
    "fenceFrame",
    "epoch",
  ];
  if (
    !evidence
    || typeof evidence !== "object"
    || Array.isArray(evidence)
    || Object.keys(evidence).sort().join(",") !== keys.sort().join(",")
    || evidence.schema !== "logos.palace.asset-authoring-render"
    || evidence.version !== 1
    || typeof evidence.open !== "boolean"
    || !Number.isSafeInteger(evidence.cardCount)
    || !Number.isSafeInteger(evidence.readyImageCount)
    || !Number.isSafeInteger(evidence.publishedCount)
    || typeof evidence.atriumAssigned !== "boolean"
    || typeof evidence.loungeAssigned !== "boolean"
    || typeof evidence.propAssigned !== "boolean"
    || !Number.isSafeInteger(evidence.fenceRequest)
    || !["idle", "waiting", "complete"].includes(evidence.fenceState)
    || !Number.isSafeInteger(evidence.fenceFrame)
    || !Number.isSafeInteger(evidence.epoch)
  ) {
    throw new Error("asset authoring evidence is invalid");
  }
  return evidence;
}

async function setAssetAuthoring(
  open,
  expectedCount = 1,
  expectedProp = false,
) {
  if (
    !Number.isSafeInteger(expectedCount)
    || expectedCount < 1
    || expectedCount > 1024
  ) {
    throw new Error("asset authoring expected count is invalid");
  }
  if (open) {
    await ensureModerationPanelOpen();
  } else {
    await ensureModerationPanelClosed();
  }
  if (!open) {
    return parseAssetAuthoringEvidence(
      (await rootProperties()).gate3AssetAuthoringEvidence,
    );
  }

  const readyDeadline = Date.now() + 30_000;
  let evidence;
  while (Date.now() < readyDeadline) {
    evidence = parseAssetAuthoringEvidence(
      (await rootProperties()).gate3AssetAuthoringEvidence,
    );
    if (
      evidence.open
      && evidence.cardCount >= expectedCount
      && evidence.readyImageCount === evidence.cardCount
      && evidence.publishedCount >= expectedCount
      && evidence.atriumAssigned
      && evidence.loungeAssigned
      && evidence.propAssigned === expectedProp
    ) {
      break;
    }
    await sleep(50);
  }
  if (
    !evidence?.open
    || evidence.cardCount < expectedCount
    || evidence.readyImageCount !== evidence.cardCount
    || evidence.publishedCount < expectedCount
    || !evidence.atriumAssigned
    || !evidence.loungeAssigned
    || evidence.propAssigned !== expectedProp
  ) {
    throw new Error("asset authoring previews did not become ready");
  }

  const started = await evaluate("gate3AssetScreenshotFenceStart()");
  const request = Number(started.result);
  if (!Number.isSafeInteger(request) || request <= 0) {
    throw new Error("asset authoring frame fence did not start");
  }
  const fenceDeadline = Date.now() + 5_000;
  while (Date.now() < fenceDeadline) {
    evidence = parseAssetAuthoringEvidence(
      (await rootProperties()).gate3AssetAuthoringEvidence,
    );
    if (
      evidence.fenceRequest === request
      && evidence.fenceState === "complete"
      && evidence.fenceFrame >= 0
    ) {
      return evidence;
    }
    await sleep(20);
  }
  throw new Error("asset authoring frame fence timed out");
}

function validImportRequest(params) {
  const selectionPath = params?.selectionPath;
  const expected = params?.expected;
  if (
    typeof params?.assetId !== "string"
    || !/^[a-z][a-z0-9_-]{0,63}$/.test(params.assetId)
    || typeof selectionPath !== "string"
    || selectionPath.length === 0
    || selectionPath.length > 16_384
    || selectionPath.includes("\u0000")
    || !expected
    || typeof expected !== "object"
    || Array.isArray(expected)
    || Object.keys(expected).sort().join(",")
      !== ["byteLength", "handle", "height", "width"].join(",")
    || typeof expected.handle !== "string"
    || !/^[0-9a-f]{64}$/.test(expected.handle)
    || !Number.isSafeInteger(expected.width)
    || expected.width <= 0
    || !Number.isSafeInteger(expected.height)
    || expected.height <= 0
    || !Number.isSafeInteger(expected.byteLength)
    || expected.byteLength <= 0
    || expected.byteLength > 10 * 1024 * 1024
  ) {
    throw new Error("asset picker request is invalid");
  }
  return {
    assetId: params.assetId,
    selectionPath,
    expected,
  };
}

function pathFreeEvidence(value) {
  if (value === null) return true;
  if (typeof value === "string") {
    return (
      !value.startsWith("/")
      && !/^[A-Za-z]:[\\/]/.test(value)
      && !value.includes("\\")
    );
  }
  if (
    typeof value === "number"
    || typeof value === "boolean"
  ) {
    return true;
  }
  if (Array.isArray(value)) return value.every(pathFreeEvidence);
  if (!value || typeof value !== "object") return false;
  return Object.entries(value).every(([key, nested]) => (
    !["path", "filepath", "selectionpath", "inputroot"].includes(
      key.toLowerCase(),
    ) && pathFreeEvidence(nested)
  ));
}

function exactObjectKeys(value, keys) {
  return (
    value
    && typeof value === "object"
    && !Array.isArray(value)
    && Object.keys(value).sort().join(",")
      === [...keys].sort().join(",")
  );
}

const moderationControlTimeoutMs = 30_000;
const moderationPublicationTimeoutMs = 180_000;

function validAssetHandle(value) {
  return typeof value === "string" && /^[0-9a-f]{64}$/.test(value);
}

function validPublishedCid(value) {
  return (
    typeof value === "string"
    && /^(b[a-z2-7]+|z[1-9A-HJ-NP-Za-km-z]+)$/.test(value)
  );
}

function parseModerationCatalog(properties) {
  const encoded = String(properties.gate3AssetAuthoringState ?? "");
  if (encoded.length === 0 || encoded.length > 2 * 1024 * 1024) {
    throw new Error("moderation catalog is unavailable");
  }
  let catalog;
  try {
    catalog = JSON.parse(encoded);
  } catch {
    throw new Error("moderation catalog is not JSON");
  }
  if (
    !catalog
    || typeof catalog !== "object"
    || Array.isArray(catalog)
    || catalog.version !== 1
    || !Array.isArray(catalog.assets)
    || !catalog.roomAssignments
    || typeof catalog.roomAssignments !== "object"
    || Array.isArray(catalog.roomAssignments)
    || !pathFreeEvidence(catalog)
  ) {
    throw new Error("moderation catalog is invalid");
  }
  return catalog;
}

function moderationCatalogAsset(catalog, handle) {
  const matches = catalog.assets.filter(
    (asset) => asset && asset.handle === handle,
  );
  if (matches.length !== 1) {
    throw new Error("moderation asset is unavailable");
  }
  const asset = matches[0];
  if (
    !validAssetHandle(asset.handle)
    || typeof asset.reviewState !== "string"
    || typeof asset.publicationState !== "string"
    || typeof asset.cid !== "string"
  ) {
    throw new Error("moderation asset is invalid");
  }
  return asset;
}

function moderationControlName(prefix, handle) {
  if (!validAssetHandle(handle)) {
    throw new Error("moderation asset handle is invalid");
  }
  return `${prefix}${handle}`;
}

function elapsedReceipt(receipt, startedAt) {
  const evidence = {
    receipt,
    elapsedMs: Math.max(0, Math.round(performance.now() - startedAt)),
  };
  if (!pathFreeEvidence(evidence)) {
    throw new Error("moderation evidence is not path-free");
  }
  return evidence;
}

function pathFreeModerationResult(value) {
  if (!pathFreeEvidence(value)) {
    throw new Error("moderation result is not path-free");
  }
  return value;
}

async function waitForModerationControl(
  objectName,
  description,
  timeout = moderationControlTimeoutMs,
) {
  if (
    typeof objectName !== "string"
    || !/^palace[A-Za-z0-9_-]{1,192}$/.test(objectName)
  ) {
    throw new Error("moderation control name is invalid");
  }
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    if (shuttingDown) throw new Error("moderation interaction interrupted");
    let found;
    try {
      found = await app.findByProperty("objectName", objectName);
    } catch {
      throw new Error(`moderation ${description} control discovery failed`);
    }
    if (found?.error) {
      throw new Error(`moderation ${description} control discovery failed`);
    }
    const matches = found?.matches;
    if (!Array.isArray(matches) || matches.length === 0) {
      await sleep(50);
      continue;
    }
    if (matches.length !== 1) {
      throw new Error(`moderation ${description} control is ambiguous`);
    }
    const objectId = String(matches[0]?.id ?? "");
    if (objectId.length === 0 || objectId.length > 512) {
      throw new Error(`moderation ${description} control identity is invalid`);
    }
    let properties;
    try {
      properties = propertyMap(await app.getProperties(objectId));
    } catch {
      throw new Error(`moderation ${description} control properties failed`);
    }
    if (properties.visible === false || properties.enabled !== true) {
      await sleep(50);
      continue;
    }
    return objectId;
  }
  throw new Error(`moderation ${description} control is unavailable`);
}

async function clickModerationControl(objectName, description) {
  const objectId = await waitForModerationControl(objectName, description);
  // Clipped authoring cards keep objectName-findable controls whose scene
  // centers sit outside the Flickable viewport. Inspector click then hits
  // panel chrome and never fires onClicked — scroll first for assign rows.
  if (
    objectName.startsWith("palaceBackgroundAssign")
    || objectName.startsWith("palaceAssetAssignProp-")
    || objectName.startsWith("palaceAssetApprove-")
  ) {
    try {
      const scrolled = await evaluate(
        `ensureModerationControlVisible(${JSON.stringify(objectName)})`,
      );
      if (scrolled?.result !== "ok" && scrolled?.result !== undefined) {
        // Best-effort: still attempt the click so missing helpers fail closed
        // via the existing control/timeout paths rather than a soft miss.
      }
      await sleep(50);
    } catch {
      // Visibility assist is optional; the click path remains authoritative.
    }
  }
  let clicked;
  try {
    clicked = await inspector.send("click", { objectId });
  } catch {
    throw new Error(`moderation ${description} click failed`);
  }
  if (clicked?.error || clicked?.clicked !== true) {
    throw new Error(`moderation ${description} click failed`);
  }
}

async function waitForModerationPanel(open, description) {
  const deadline = Date.now() + moderationControlTimeoutMs;
  while (Date.now() < deadline) {
    if (shuttingDown) throw new Error("moderation interaction interrupted");
    if ((await rootProperties()).backgroundModerationOpen === open) return;
    await sleep(50);
  }
  throw new Error(`moderation panel ${description} timed out`);
}

async function ensureModerationPanelOpen() {
  if ((await rootProperties()).backgroundModerationOpen === true) return;
  await clickModerationControl(
    "palaceBackgroundModerationButton",
    "panel open",
  );
  await waitForModerationPanel(true, "open");
}

async function ensureModerationPanelClosed() {
  if ((await rootProperties()).backgroundModerationOpen !== true) return;
  await clickModerationControl(
    "palaceBackgroundModerationClose",
    "panel close",
  );
  await waitForModerationPanel(false, "close");
}

async function moderationSnapshot() {
  const properties = await rootProperties();
  const invocationSequence = Number(properties.invocationSequence ?? -1);
  if (!Number.isSafeInteger(invocationSequence) || invocationSequence < 0) {
    throw new Error("moderation invocation sequence is unavailable");
  }
  return {
    properties,
    catalog: parseModerationCatalog(properties),
    invocationSequence,
  };
}

async function waitForModerationCatalog({
  description,
  handle,
  startedAt,
  beforeSequence,
  timeout = moderationPublicationTimeoutMs,
  accept,
}) {
  const deadline = Date.now() + timeout;
  let lastPublicationState = "";
  while (Date.now() < deadline) {
    if (shuttingDown) throw new Error("moderation interaction interrupted");
    const snapshot = await moderationSnapshot();
    const failure = String(
      snapshot.properties.invocationError
        || (
          String(snapshot.properties.gate3Receipt ?? "").startsWith(
            "rejected=",
          )
            ? snapshot.properties.gate3Receipt
            : ""
        )
        || "",
    );
    if (
      snapshot.invocationSequence > beforeSequence
      && failure.startsWith("rejected=")
    ) {
      throw new Error(`moderation ${description} failed: ${failure}`);
    }
    const accepted = accept(snapshot);
    if (accepted) return { ...snapshot, accepted };
    lastPublicationState = moderationCatalogAsset(
      snapshot.catalog,
      handle,
    ).publicationState;
    // The backend refreshes the catalog from its 500 ms delivery poll. Avoid
    // a synthetic action here: its receipt sequence must remain attributable
    // to the visible moderation control that started this transition.
    await sleep(100);
  }
  throw new Error(
    `moderation ${description} timed out: publication=${lastPublicationState}`,
  );
}

function validModerationAssetRequest(params) {
  if (!exactObjectKeys(params, ["handle"]) || !validAssetHandle(params.handle)) {
    throw new Error("moderation asset request is invalid");
  }
  return { handle: params.handle };
}

async function approveAndPublishAsset(params) {
  if (!processState) throw new Error("worker is not initialized");
  if (shuttingDown) throw new Error("moderation interaction interrupted");
  const { handle } = validModerationAssetRequest(params);
  await ensureModerationPanelOpen();
  const before = await moderationSnapshot();
  const existing = moderationCatalogAsset(before.catalog, handle);
  if (existing.publicationState === "published") {
    throw new Error("moderation asset is already published");
  }
  if (existing.publicationState === "publishing") {
    throw new Error("moderation asset publication is already in progress");
  }
  const startedAt = performance.now();
  await clickModerationControl(
    moderationControlName("palaceAssetApprove-", handle),
    "approval and upload",
  );

  let review;
  let dispatched;
  const completed = await waitForModerationCatalog({
    description: "approval and upload",
    handle,
    startedAt,
    beforeSequence: before.invocationSequence,
    accept(snapshot) {
      const asset = moderationCatalogAsset(snapshot.catalog, handle);
      if (asset.publicationState.startsWith("publish-failed")) {
        throw new Error("moderation asset upload failed");
      }
      if (!review && asset.reviewState === "approved") {
        review = elapsedReceipt(
          `ok;handle=${handle};review=approved`,
          startedAt,
        );
      }
      if (
        !dispatched
        && ["publishing", "published"].includes(asset.publicationState)
      ) {
        dispatched = elapsedReceipt("ok;asset=publishing", startedAt);
      }
      if (
        review
        && dispatched
        && asset.publicationState === "published"
        && validPublishedCid(asset.cid)
      ) {
        return asset;
      }
      return undefined;
    },
  });
  const result = {
    review,
    publication: {
      dispatched,
      completed: elapsedReceipt(
        `published;cid=${completed.accepted.cid}`,
        startedAt,
      ),
    },
    cid: completed.accepted.cid,
  };
  return pathFreeModerationResult(result);
}

async function waitForPublishedAsset(params) {
  if (!processState) throw new Error("worker is not initialized");
  if (shuttingDown) throw new Error("moderation interaction interrupted");
  const { handle } = validModerationAssetRequest(params);
  const startedAt = performance.now();
  const before = await moderationSnapshot();
  if (moderationCatalogAsset(before.catalog, handle).publicationState !== "publishing") {
    throw new Error("moderation asset is not publishing");
  }
  const completed = await waitForModerationCatalog({
    description: "publication completion",
    handle,
    startedAt,
    beforeSequence: before.invocationSequence,
    accept(snapshot) {
      const asset = moderationCatalogAsset(snapshot.catalog, handle);
      if (asset.publicationState.startsWith("publish-failed")) {
        throw new Error("moderation asset upload failed");
      }
      return asset.publicationState === "published" && validPublishedCid(asset.cid)
        ? asset
        : undefined;
    },
  });
  return pathFreeModerationResult({
    cid: completed.accepted.cid,
    completed: elapsedReceipt(
      `published;cid=${completed.accepted.cid}`,
      startedAt,
    ),
  });
}

function validRoomAssignmentRequest(params) {
  if (
    !exactObjectKeys(params, ["handle", "roomId"])
    || !validAssetHandle(params.handle)
    || !["atrium", "lounge"].includes(params.roomId)
  ) {
    throw new Error("moderation room assignment request is invalid");
  }
  return { handle: params.handle, roomId: params.roomId };
}

async function assignRoomBackgroundFromModeration(params) {
  if (!processState) throw new Error("worker is not initialized");
  if (shuttingDown) throw new Error("moderation interaction interrupted");
  const { handle, roomId } = validRoomAssignmentRequest(params);
  await ensureModerationPanelOpen();
  const before = await moderationSnapshot();
  const asset = moderationCatalogAsset(before.catalog, handle);
  if (asset.publicationState !== "published" || !validPublishedCid(asset.cid)) {
    throw new Error("moderation room assignment asset is not published");
  }
  if (before.catalog.roomAssignments[roomId] === handle) {
    throw new Error("moderation room is already assigned");
  }
  const startedAt = performance.now();
  const prefix = roomId === "atrium"
    ? "palaceBackgroundAssignAtrium-"
    : "palaceBackgroundAssignLounge-";
  await clickModerationControl(
    moderationControlName(prefix, handle),
    `${roomId} assignment`,
  );
  await waitForModerationCatalog({
    description: `${roomId} assignment`,
    handle,
    startedAt,
    beforeSequence: before.invocationSequence,
    accept(snapshot) {
      const current = moderationCatalogAsset(snapshot.catalog, handle);
      return (
        snapshot.catalog.roomAssignments[roomId] === handle
        && Array.isArray(current.roomAssignments)
        && current.roomAssignments.includes(roomId)
      );
    },
  });
  return pathFreeModerationResult(
    elapsedReceipt(`ok;room=${roomId};handle=${handle}`, startedAt),
  );
}

function validPropAssignmentRequest(params) {
  if (
    !exactObjectKeys(params, [
      "anchorX",
      "anchorY",
      "handle",
      "layer",
      "propId",
    ])
    || !validAssetHandle(params.handle)
    || typeof params.propId !== "string"
    || !/^[a-z][a-z0-9_-]{0,63}$/.test(params.propId)
    || !Number.isSafeInteger(params.anchorX)
    || params.anchorX < 0
    || !Number.isSafeInteger(params.anchorY)
    || params.anchorY < 0
    || !["head", "body", "hand", "back"].includes(params.layer)
  ) {
    throw new Error("moderation prop assignment request is invalid");
  }
  return params;
}

async function typeModerationField(objectName, rootProperty, value) {
  const before = await rootProperties();
  if (String(before[rootProperty] ?? "") === value) return;
  if (String(before[rootProperty] ?? "").length !== 0) {
    throw new Error(`moderation ${rootProperty} field is not empty`);
  }
  await clickModerationControl(objectName, rootProperty);
  let sent;
  try {
    sent = await inspector.send("sendKeys", { text: value });
  } catch {
    throw new Error(`moderation ${rootProperty} input failed`);
  }
  if (sent?.error) throw new Error(`moderation ${rootProperty} input failed`);
  const deadline = Date.now() + moderationControlTimeoutMs;
  while (Date.now() < deadline) {
    if (shuttingDown) throw new Error("moderation interaction interrupted");
    if (String((await rootProperties())[rootProperty] ?? "") === value) return;
    await sleep(50);
  }
  throw new Error(`moderation ${rootProperty} input timed out`);
}

async function assignPropAssetFromModeration(params) {
  if (!processState) throw new Error("worker is not initialized");
  if (shuttingDown) throw new Error("moderation interaction interrupted");
  const request = validPropAssignmentRequest(params);
  await ensureModerationPanelOpen();
  const before = await moderationSnapshot();
  const asset = moderationCatalogAsset(before.catalog, request.handle);
  if (asset.publicationState !== "published" || !validPublishedCid(asset.cid)) {
    throw new Error("moderation prop assignment asset is not published");
  }
  await typeModerationField(
    "palaceAssetPropId",
    "propDraftId",
    request.propId,
  );
  await typeModerationField(
    "palaceAssetPropAnchorX",
    "propDraftAnchorX",
    String(request.anchorX),
  );
  await typeModerationField(
    "palaceAssetPropAnchorY",
    "propDraftAnchorY",
    String(request.anchorY),
  );
  await typeModerationField(
    "palaceAssetPropLayer",
    "propDraftLayer",
    request.layer,
  );
  const startedAt = performance.now();
  await clickModerationControl(
    moderationControlName("palaceAssetAssignProp-", request.handle),
    "prop assignment",
  );
  await waitForModerationCatalog({
    description: "prop assignment",
    handle: request.handle,
    startedAt,
    beforeSequence: before.invocationSequence,
    accept(snapshot) {
      const assigned = snapshot.catalog.propAssignment;
      const current = moderationCatalogAsset(snapshot.catalog, request.handle);
      return (
        assigned?.propId === request.propId
        && assigned?.handle === request.handle
        && assigned?.anchorX === request.anchorX
        && assigned?.anchorY === request.anchorY
        && assigned?.layer === request.layer
        && Array.isArray(current.propAssignments)
        && current.propAssignments.includes(request.propId)
      );
    },
  });
  return pathFreeModerationResult(
    elapsedReceipt(
      `ok;propId=${request.propId};handle=${request.handle};`
      + `anchorX=${request.anchorX};anchorY=${request.anchorY};`
      + `layer=${request.layer}`,
      startedAt,
    ),
  );
}

function validElapsedTrace(value) {
  return (
    exactObjectKeys(value, ["receipt", "elapsedMs"])
    && typeof value.receipt === "string"
    && value.receipt.length > 0
    && !value.receipt.startsWith("rejected=")
    && Number.isSafeInteger(value.elapsedMs)
    && value.elapsedMs >= 0
  );
}

function validImportTrace(trace, expected, generation) {
  if (
    !exactObjectKeys(trace, [
      "schema",
      "version",
      "generation",
      "handle",
      "width",
      "height",
      "byteLength",
      "chunkBytes",
      "chunkCount",
      "begin",
      "appends",
      "commit",
    ])
    || trace.schema !== "logos.palace.user-file-import"
    || trace.version !== 1
    || trace.generation !== generation
    || trace.handle !== expected.handle
    || trace.width !== expected.width
    || trace.height !== expected.height
    || trace.byteLength !== expected.byteLength
    || trace.chunkBytes !== 32 * 1024
    || !Number.isSafeInteger(trace.chunkCount)
    || trace.chunkCount <= 0
    || !validElapsedTrace(trace.begin)
    || !Array.isArray(trace.appends)
    || trace.appends.length !== trace.chunkCount
    || !validElapsedTrace(trace.commit)
  ) {
    return false;
  }
  let totalBytes = 0;
  return trace.appends.every((append, sequence) => {
    totalBytes += append?.byteLength ?? 0;
    return (
      exactObjectKeys(append, [
        "sequence",
        "byteLength",
        "receipt",
        "elapsedMs",
      ])
      && append.sequence === sequence
      && Number.isSafeInteger(append.byteLength)
      && append.byteLength > 0
      && append.byteLength <= trace.chunkBytes
      && validElapsedTrace({
        receipt: append.receipt,
        elapsedMs: append.elapsedMs,
      })
      && totalBytes <= trace.byteLength
    );
  }) && totalBytes === trace.byteLength;
}

function parsedImportEvidence(value, expected, generation) {
  if (
    typeof value !== "string"
    || value.length === 0
    || value.length > 2 * 1024 * 1024
  ) {
    return undefined;
  }
  try {
    const parsed = JSON.parse(value);
    if (
      !parsed
      || typeof parsed !== "object"
      || Array.isArray(parsed)
      || !pathFreeEvidence(parsed)
      || !validImportTrace(parsed, expected, generation)
    ) {
      return undefined;
    }
    return parsed;
  } catch {
    return undefined;
  }
}

function importedAssetMatches(properties, expected) {
  let state;
  try {
    state = JSON.parse(String(properties.gate3AssetAuthoringState ?? ""));
  } catch {
    return false;
  }
  if (!Array.isArray(state?.assets)) return false;
  return state.assets.some((asset) => (
    asset
    && asset.handle === expected.handle
    && asset.width === expected.width
    && asset.height === expected.height
    && asset.byteLength === expected.byteLength
  ));
}

async function listedFileDialogIds() {
  let listed;
  try {
    listed = await app.listFileDialogs();
  } catch {
    throw new Error("asset picker dialog discovery failed");
  }
  if (listed?.error) {
    throw new Error("asset picker dialog discovery failed");
  }
  const dialogIds = uniqueFileDialogIds(listed?.dialogs);
  if (!dialogIds) {
    throw new Error("asset picker dialog discovery failed");
  }
  return dialogIds;
}

async function waitForAssetDialog(priorDialogIds) {
  const deadline = Date.now() + 30_000;
  while (Date.now() < deadline) {
    if (shuttingDown) throw new Error("asset picker interrupted");
    const objectId = newlyOpenedFileDialogId(
      await listedFileDialogIds(),
      priorDialogIds,
    );
    if (objectId) return objectId;
    await sleep(50);
  }
  throw new Error("asset picker dialog did not open");
}

function pickerControlId(node, matches, predicate) {
  if (!node || typeof node !== "object" || Array.isArray(node)) {
    throw new Error("asset picker control tree is invalid");
  }
  if (predicate(node)) {
    const objectId = String(node.id ?? "");
    if (objectId.length === 0 || objectId.length > 512) {
      throw new Error("asset picker control identity is invalid");
    }
    matches.push(objectId);
  }
  if (node.children === undefined) return;
  if (!Array.isArray(node.children)) {
    throw new Error("asset picker control tree is invalid");
  }
  for (const child of node.children) {
    pickerControlId(child, matches, predicate);
  }
}

function singleVisiblePickerControl(tree, predicate, description) {
  const matches = [];
  pickerControlId(tree, matches, predicate);
  const distinct = [...new Set(matches)];
  if (distinct.length !== 1) {
    throw new Error(`asset picker ${description} control is ambiguous`);
  }
  return distinct[0];
}

async function pickerDialogTree(dialogId) {
  let result;
  try {
    result = await app.getTree({ objectId: dialogId, depth: 16 });
  } catch {
    throw new Error("asset picker control discovery failed");
  }
  if (result?.error || !result?.tree) {
    throw new Error("asset picker control discovery failed");
  }
  return result.tree;
}

async function pickerFileNameInput(dialogId) {
  return singleVisiblePickerControl(
    await pickerDialogTree(dialogId),
    (node) => (
      node.type === "QLineEdit"
      && node.objectName === "fileNameEdit"
      && node.visible === true
      && node.enabled === true
    ),
    "file name",
  );
}

async function pickerOpenButton(dialogId) {
  return singleVisiblePickerControl(
    await pickerDialogTree(dialogId),
    (node) => (
      node.type === "QPushButton"
      && node.text === "&Open"
      && node.visible === true
      && node.enabled === true
    ),
    "Open",
  );
}

async function clickPickerControl(objectId, description) {
  let clicked;
  try {
    clicked = await inspector.send("click", { objectId });
  } catch {
    throw new Error(`asset picker ${description} click failed`);
  }
  if (clicked?.error || clicked?.clicked !== true) {
    throw new Error(`asset picker ${description} click failed`);
  }
}

async function typePickerFileName(dialogId, selectionPath) {
  const inputId = await pickerFileNameInput(dialogId);
  await clickPickerControl(inputId, "file name");
  let sent;
  try {
    sent = await inspector.send("sendKeys", { text: selectionPath });
  } catch {
    throw new Error("asset picker file name input failed");
  }
  if (sent?.error) {
    throw new Error("asset picker file name input failed");
  }

  const deadline = Date.now() + 30_000;
  while (Date.now() < deadline) {
    if (shuttingDown) throw new Error("asset picker interrupted");
    let properties;
    try {
      properties = propertyMap(await app.getProperties(inputId));
    } catch {
      throw new Error("asset picker file name properties failed");
    }
    if (String(properties.text ?? "") === selectionPath) {
      return;
    }
    await sleep(50);
  }
  throw new Error("asset picker file name input timed out");
}

async function acceptPickerFile(dialogId, selectionPath) {
  await typePickerFileName(dialogId, selectionPath);
  const openId = await pickerOpenButton(dialogId);
  await clickPickerControl(openId, "Open");
}

async function fileDialogAction(objectId, action) {
  let result;
  try {
    result = await app.fileDialogAction(objectId, action);
  } catch {
    throw new Error(`asset picker ${action} action failed`);
  }
  if (result?.error) {
    throw new Error(`asset picker ${action} action failed`);
  }
}

async function importSelectedAsset(params) {
  if (!processState) throw new Error("worker is not initialized");
  if (shuttingDown) throw new Error("asset picker interrupted");
  const request = validImportRequest(params);
  const before = await rootProperties();
  const beforeGeneration = Number(before.assetImportGeneration ?? -1);
  if (!Number.isSafeInteger(beforeGeneration) || beforeGeneration < 0) {
    throw new Error("asset picker generation is unavailable");
  }

  if (before.backgroundModerationOpen !== true) {
    await ensureModerationPanelOpen();
  }

  // The inspector walks each top-level Qt root independently. A parented
  // QFileDialog can therefore be reported through both its parent and itself;
  // identify the one new host picker by its stable object ID.
  const priorDialogIds = await listedFileDialogIds();
  await clickModerationControl("palaceAssetSelectFile", "file picker");
  const dialogId = await waitForAssetDialog(priorDialogIds);
  await acceptPickerFile(dialogId, request.selectionPath);

  const deadline = Date.now() + 180_000;
  while (Date.now() < deadline) {
    if (shuttingDown) throw new Error("asset picker interrupted");
    const properties = await rootProperties();
    const generation = Number(properties.assetImportGeneration ?? -1);
    const trace = parsedImportEvidence(
      properties.gate3AssetImportEvidence,
      request.expected,
      beforeGeneration + 1,
    );
    if (
      generation > beforeGeneration
      && properties.assetImportRunning === false
      && importedAssetMatches(properties, request.expected)
      && trace
    ) {
      return {
        assetId: request.assetId,
        trace,
      };
    }
    if (
      generation > beforeGeneration
      && properties.assetImportRunning === false
      && String(properties.gate3AssetImportEvidence ?? "").length > 0
      && !trace
    ) {
      throw new Error("asset picker import trace is invalid");
    }
    if (
      generation > beforeGeneration
      && properties.assetImportRunning === false
      && String(properties.invocationError ?? "").startsWith("rejected=")
    ) {
      throw new Error("asset picker import was rejected");
    }
    if (
      generation > beforeGeneration
      && properties.assetImportRunning === false
      && !trace
    ) {
      throw new Error("asset picker selection did not complete");
    }
    await sleep(50);
  }
  throw new Error("asset picker import did not complete");
}

const allowedFunctions = new Set([
  "gate1EnterRoom",
  "gate2Start",
  "gate2Say",
  "gate2Move",
  "gate2Wear",
  "gate2Remove",
  "gate2RefreshPresence",
  "gate3StartStorage",
  "gate3FetchPng",
  "gate3AssetStatus",
  "gate3PublishPng",
  "gate3PublicationStatus",
  "publishAsset",
  "gate3PublishBundle",
  "gate3BundleStatus",
  "gate3FetchBundle",
  "gate3VerifyRetention",
  "gate3ObjectStatus",
  "gate3StorageStatus",
  "gate3StoragePeerEndpoint",
  "gate3ConnectStoragePeer",
  "gate3MarkStorageMaterialized",
  "gate4StartLez",
  "gate4CreateIdentity",
  "gate4OpenPalace",
  "gate4PalaceStatus",
  "gate4RefreshLez",
  "gate4RefreshIdentity",
  "gate4BanUser",
  "gate4BanProp",
  "gate4RefreshModeration",
  "gate4Submit",
  "gate4Observe",
  "gate4Reconcile",
  "gate4ActionStatus",
  "gate5PreviewDoor",
  "gate5UseDoor",
  "gate5ActionStatus",
  "gate5Reconcile",
  "acceptanceApplicationRoundTrip",
  "gate5VmTurnMetrics",
]);

function receiptMatches(receipt, expected, properties) {
  if (!expected) return true;
  if (
    expected.currentLezState !== undefined
    && !acceptsLezStartupObservation(
      receipt,
      String(properties.gate4LezState ?? ""),
      expected.currentLezState,
    )
  ) {
    return false;
  }
  if (
    expected.exact !== undefined &&
    receipt !== String(expected.exact)
  ) {
    return false;
  }
  if (
    expected.prefix !== undefined &&
    !receipt.startsWith(String(expected.prefix))
  ) {
    return false;
  }
  if (
    expected.includes !== undefined &&
    !receipt.includes(String(expected.includes))
  ) {
    return false;
  }
  return true;
}

async function invoke(params) {
  const name = params?.name;
  const args = params?.args ?? [];
  if (!allowedFunctions.has(name) || !Array.isArray(args)) {
    throw new Error(`unsupported Palace invocation: ${name}`);
  }
  const receiptProperty = name === "acceptanceApplicationRoundTrip"
    ? "acceptanceRoundTripResponse"
    : name.startsWith("gate5")
      ? "gate5Receipt"
      : "gate3Receipt";
  const beforeProperties = await rootProperties();
  const before = String(beforeProperties[receiptProperty] ?? "");
  const beforeSequence = Number(beforeProperties.invocationSequence ?? -1);
  if (!Number.isSafeInteger(beforeSequence) || beforeSequence < 0) {
    throw new Error("Palace invocation sequence is unavailable");
  }
  const expression =
    `${name}(${args.map((argument) => JSON.stringify(argument)).join(",")})`;
  const startedAtUnixMs = Date.now();
  const startedAt = performance.now();
  const evaluated = await evaluate(expression);
  if (shuttingDown) {
    throw new Error(`${name} interrupted before receipt observation`);
  }
  const timeout = Math.min(
    Math.max(Number(params.timeout ?? 30_000), 1_000),
    workerInvocationTimeoutLimit(name),
  );
  const deadline = Date.now() + timeout;
  let receipt = "";
  let lezState = "";
  let sequence = beforeSequence;
  while (Date.now() < deadline) {
    if (shuttingDown) {
      throw new Error(`${name} interrupted while awaiting receipt`);
    }
    const properties = await rootProperties();
    receipt = String(properties[receiptProperty] ?? "");
    lezState = String(properties.gate4LezState ?? "");
    sequence = Number(properties.invocationSequence ?? -1);
    if (
      receiptMatches(receipt, params.expect, properties) &&
      Number.isSafeInteger(sequence) &&
      sequence > beforeSequence
    ) {
      const completedAtUnixMs = Date.now();
      return {
        evaluated,
        receipt,
        lezState,
        invocationSequence: sequence,
        elapsedMs: Math.round(performance.now() - startedAt),
        startedAtUnixMs,
        completedAtUnixMs,
      };
    }
    await sleep(50);
  }
  throw new Error(
    `${name} receipt timeout: before=${JSON.stringify(before)} after=${JSON.stringify(receipt)} state=${JSON.stringify(lezState)} sequence=${beforeSequence}->${sequence}`,
  );
}

async function dispatch(command, params) {
  switch (command) {
  case "init":
    if (processState) throw new Error("worker already initialized");
    return startBasecamp();
  case "properties":
    return rootProperties();
  case "invoke":
    return invoke(params);
  case "screenshot":
    return saveScreenshot(params?.name);
  case "frameTimings":
    if (!processState) throw new Error("worker is not initialized");
    return capturePalaceFrameTiming({
      evaluate,
      rootProperties,
      sleep,
    });
  case "assetAuthoring":
    if (!processState) throw new Error("worker is not initialized");
    return setAssetAuthoring(
      params?.open === true,
      Number(params?.expectedCount ?? 1),
      params?.expectedProp === true,
    );
  case "importSelectedAsset":
    return importSelectedAsset(params);
  case "approveAndPublishAsset":
    return approveAndPublishAsset(params);
  case "waitForPublishedAsset":
    return waitForPublishedAsset(params);
  case "assignRoomBackgroundFromModeration":
    return assignRoomBackgroundFromModeration(params);
  case "assignPropAssetFromModeration":
    return assignPropAssetFromModeration(params);
  case "shutdown":
    shuttingDown = true;
    await stopBasecamp();
    return { stopped: true };
  default:
    throw new Error(`unknown worker command: ${command}`);
  }
}

async function reply(message) {
  const id = message?.id;
  try {
    const result = await dispatch(message?.command, message?.params);
    process.stdout.write(`${JSON.stringify({ id, ok: true, result })}\n`);
  } catch (error) {
    process.stdout.write(
      `${JSON.stringify({
        id,
        ok: false,
        error: error instanceof Error ? error.message : String(error),
      })}\n`,
    );
  }
  if (shuttingDown) process.exitCode = 0;
}

let queue = Promise.resolve();
const input = createInterface({
  input: process.stdin,
  crlfDelay: Infinity,
});
input.on("line", (line) => {
  queue = queue.then(async () => {
    let message;
    try {
      message = JSON.parse(line);
    } catch {
      process.stdout.write(
        `${JSON.stringify({
          id: null,
          ok: false,
          error: "invalid JSON command",
        })}\n`,
      );
      return;
    }
    await reply(message);
    if (shuttingDown) input.close();
  });
});
input.on("close", () => {
  queue = queue.finally(async () => {
    await stopBasecamp();
  });
});

process.on("SIGTERM", () => {
  shuttingDown = true;
  input.close();
});
process.on("SIGINT", () => {
  shuttingDown = true;
  input.close();
});
