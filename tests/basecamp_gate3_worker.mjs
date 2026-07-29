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

const renderTimingContract = {
  basecampRevision: "205405858676849f69a02e55385ae18ce6d7df5a",
  qtVersion: "6.9.2",
  renderLoop: "software",
  clock: "Qt Quick QSG_RENDER_TIMING integer milliseconds",
  messagePattern: "%{category}: %{message}",
  lineFormat:
    "qt.scenegraph.time.renderloop: Frame rendered with 'software' renderloop in <total>ms, polish=<polish>, sync=<sync>, render=<render>, swap=<swap>, frameDelta=<frameDelta>",
};
if (process.env.PALACE_BASECAMP_REV !== renderTimingContract.basecampRevision) {
  throw new Error(
    "QSG render timing parser requires review for the pinned Basecamp revision",
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

function nearestRank(values, percentile) {
  const sorted = [...values].sort((left, right) => left - right);
  return sorted[Math.ceil(percentile * sorted.length) - 1];
}

function qsgRenderTimingEvidence(stderrChunks) {
  const encoded = Buffer.concat(stderrChunks).toString("utf8");
  const lines = encoded.split(/\r?\n/);
  const candidateLines = lines.filter((line) =>
    line.includes("Frame rendered with"));
  const pattern =
    /^qt\.scenegraph\.time\.renderloop: Frame rendered with 'software' renderloop in ([0-9]+)ms, polish=([0-9]+), sync=([0-9]+), render=([0-9]+), swap=([0-9]+), frameDelta=(-?[0-9]+)$/;
  const samples = candidateLines.map((line) => {
    const match = line.match(pattern);
    if (!match) {
      throw new Error(`unrecognized pinned QSG render timing line: ${line}`);
    }
    const values = match.slice(1).map(Number);
    if (
      values.some((value) => !Number.isSafeInteger(value))
      || values.slice(0, 5).some((value) => value < 0)
    ) {
      throw new Error(`invalid pinned QSG render timing line: ${line}`);
    }
    return {
      totalMs: values[0],
      polishMs: values[1],
      syncMs: values[2],
      renderMs: values[3],
      swapMs: values[4],
      frameDeltaMs: values[5],
    };
  });
  if (samples.length === 0) {
    throw new Error("pinned QSG render timing emitted no complete samples");
  }
  const fields = [
    "totalMs",
    "polishMs",
    "syncMs",
    "renderMs",
    "swapMs",
    "frameDeltaMs",
  ];
  return {
    parser: renderTimingContract,
    sampleCount: samples.length,
    percentileMethod:
      "nearest-rank: sorted[Math.ceil(percentile * sampleCount) - 1]",
    summaries: Object.fromEntries(
      fields.map((field) => {
        const values = samples.map((sample) => sample[field]);
        return [
          field,
          {
            p50: nearestRank(values, 0.50),
            p95: nearestRank(values, 0.95),
            max: Math.max(...values),
          },
        ];
      }),
    ),
  };
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
        QT_FORCE_STDERR_LOGGING: "1",
        QT_MESSAGE_PATTERN: renderTimingContract.messagePattern,
        QT_QPA_PLATFORM: "offscreen",
        QSG_RENDER_TIMING: "1",
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
  case "renderTimings":
    if (!processState) throw new Error("worker is not initialized");
    return qsgRenderTimingEvidence(processState.stderrChunks);
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
