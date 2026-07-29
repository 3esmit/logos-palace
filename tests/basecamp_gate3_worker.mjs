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
  await evaluate(`backgroundModerationOpen = ${open ? "true" : "false"}`);
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
  "beginAssetStage",
  "appendAssetStageChunk",
  "commitAssetStage",
  "cancelAssetStage",
  "reviewAsset",
  "publishAsset",
  "assignRoomBackground",
  "assignPropAsset",
  "refreshAssetAuthoring",
  "gate3PublishBundle",
  "gate3BundleStatus",
  "gate3FetchBundle",
  "gate3VerifyRetention",
  "gate3ObjectStatus",
  "gate3StorageStatus",
  "gate4StartLez",
  "gate4CreateIdentity",
  "gate4OpenPalace",
  "gate4PalaceStatus",
  "gate4RefreshLez",
  "gate4RefreshIdentity",
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

function receiptMatches(receipt, expected) {
  if (!expected) return true;
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
  const timeout = Math.min(
    Math.max(Number(params.timeout ?? 30_000), 1_000),
    120_000,
  );
  const deadline = Date.now() + timeout;
  let receipt = "";
  let sequence = beforeSequence;
  while (Date.now() < deadline) {
    const properties = await rootProperties();
    receipt = String(properties[receiptProperty] ?? "");
    sequence = Number(properties.invocationSequence ?? -1);
    if (
      receiptMatches(receipt, params.expect) &&
      Number.isSafeInteger(sequence) &&
      sequence > beforeSequence
    ) {
      const completedAtUnixMs = Date.now();
      return {
        evaluated,
        receipt,
        invocationSequence: sequence,
        elapsedMs: Math.round(performance.now() - startedAt),
        startedAtUnixMs,
        completedAtUnixMs,
      };
    }
    await sleep(50);
  }
  throw new Error(
    `${name} receipt timeout: before=${JSON.stringify(before)} after=${JSON.stringify(receipt)} sequence=${beforeSequence}->${sequence}`,
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
