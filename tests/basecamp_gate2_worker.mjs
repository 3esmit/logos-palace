#!/usr/bin/env node

import { createHash } from "node:crypto";
import { createReadStream, writeSync } from "node:fs";
import {
  mkdir,
  writeFile,
} from "node:fs/promises";
import { spawn } from "node:child_process";
import { basename, join, resolve } from "node:path";
import { createInterface } from "node:readline";
import { pathToFileURL } from "node:url";

const [
  basecampArgument,
  userDirArgument,
  artifactsArgument,
  labelArgument,
  viewArgument,
] = process.argv.slice(2);
if (
  !basecampArgument ||
  !userDirArgument ||
  !artifactsArgument ||
  !labelArgument ||
  !viewArgument
) {
  throw new Error(
    "usage: node tests/basecamp_gate2_worker.mjs <Basecamp> <user-dir> <artifacts-dir> <label> <palace|acceptance>",
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
  basecampRevision: "fd13085f7fda6a8b1ada53a959d3064c5747c9d1",
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
const views = {
  palace: {
    launcher: "Logos Palace",
    expectedTexts: ["Atrium", "Door to Lounge"],
    receiptProperty: "gate2Receipt",
    rootObjectName: "palaceGate2Root",
  },
  acceptance: {
    launcher: "Palace Delivery Acceptance",
    expectedTexts: ["Palace Delivery Acceptance"],
    receiptProperty: "acceptanceReceipt",
    rootObjectName: "palaceDeliveryAcceptanceRoot",
  },
};
const view = views[viewArgument];
if (!view) {
  throw new Error(`invalid worker view: ${viewArgument}`);
}
await mkdir(artifactsDir, { recursive: true });

const frameworkUrl = pathToFileURL(
  resolve(qtMcpRoot, "test-framework/framework.mjs"),
).href;
const { App, Inspector } = await import(frameworkUrl);

const sleep = (milliseconds) =>
  new Promise((resolveSleep) => setTimeout(resolveSleep, milliseconds));

function childStdioWithInheritedMvpLock(baseStdio) {
  const encoded = process.env.PALACE_MVP_LOCK_FD;
  if (encoded === undefined) return baseStdio;
  if (!/^(?:[3-9]|[1-9][0-9]{1,2})$/.test(encoded)) {
    throw new Error("PALACE_MVP_LOCK_FD is invalid");
  }
  const lockFd = Number(encoded);
  const stdio = [...baseStdio];
  while (stdio.length <= lockFd) stdio.push("ignore");
  stdio[lockFd] = lockFd;
  return stdio;
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

async function sha256File(path) {
  const digest = createHash("sha256");
  await new Promise((resolveHash, rejectHash) => {
    const input = createReadStream(path);
    input.on("data", (chunk) => digest.update(chunk));
    input.on("error", rejectHash);
    input.on("end", resolveHash);
  });
  return digest.digest("hex");
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

function signalProcessGroup(child, signal) {
  try {
    process.kill(-child.pid, signal);
  } catch {
    child.kill(signal);
  }
}

let launchNumber = 0;
let processState;
let inspector;
let app;
let rootObjectId;
let shuttingDown = false;

function launchBasecamp() {
  launchNumber += 1;
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
      stdio: childStdioWithInheritedMvpLock(["ignore", "pipe", "pipe"]),
    },
  );
  const stdoutChunks = [];
  const stderrChunks = [];
  child.stdout.on("data", (chunk) => stdoutChunks.push(chunk));
  child.stderr.on("data", (chunk) => stderrChunks.push(chunk));
  const state = {
    child,
    exited: undefined,
    launchNumber,
    stdoutChunks,
    stderrChunks,
    spawnFailure: undefined,
  };
  const exited = new Promise((resolveExit) => {
    child.once("error", (error) => {
      state.spawnFailure =
        error instanceof Error ? error : new Error(String(error));
      resolveExit({ code: null, signal: null, error: state.spawnFailure });
    });
    child.once("exit", (code, signal) => resolveExit({ code, signal }));
  });
  state.exited = exited;
  processState = state;
  if (Number.isSafeInteger(child.pid) && child.pid > 0) {
    writeSync(
      process.stdout.fd,
      `${JSON.stringify({
        event: "basecamp-started",
        basecampPid: child.pid,
      })}\n`,
    );
  }
}

async function saveLaunchLogs(state) {
  const prefix = `basecamp-${label}-launch-${state.launchNumber}`;
  await writeFile(
    join(artifactsDir, `${prefix}.stdout.log`),
    Buffer.concat(state.stdoutChunks),
  );
  await writeFile(
    join(artifactsDir, `${prefix}.stderr.log`),
    Buffer.concat(state.stderrChunks),
  );
}

async function stopBasecamp() {
  inspector?.disconnect();
  inspector = undefined;
  app = undefined;
  rootObjectId = undefined;
  if (!processState) return;

  const state = processState;
  processState = undefined;
  if (state.child.exitCode === null && !state.spawnFailure) {
    signalProcessGroup(state.child, "SIGTERM");
    const cleanExit = await Promise.race([
      state.exited.then(() => true),
      sleep(15_000).then(() => false),
    ]);
    if (!cleanExit) {
      signalProcessGroup(state.child, "SIGKILL");
      await state.exited;
    }
  }
  await saveLaunchLogs(state);
}

async function crashBasecamp() {
  inspector?.disconnect();
  inspector = undefined;
  app = undefined;
  rootObjectId = undefined;
  if (!processState) throw new Error("worker is not initialized");

  const state = processState;
  processState = undefined;
  if (state.child.exitCode !== null) {
    throw new Error("Basecamp exited before controlled crash");
  }
  signalProcessGroup(state.child, "SIGKILL");
  const exit = await state.exited;
  await saveLaunchLogs(state);
  if (exit.signal !== "SIGKILL" || exit.code !== null) {
    throw new Error(
      `controlled crash boundary differed: code=${exit.code} signal=${exit.signal}`,
    );
  }
  return {
    previousPid: state.child.pid,
    signal: exit.signal,
    exitCode: exit.code,
    graceful: false,
  };
}

async function connectInspector() {
  let lastError = new Error("inspector did not start");
  for (let attempt = 0; attempt < 240; attempt += 1) {
    if (processState?.spawnFailure) throw processState.spawnFailure;
    if (!processState || processState.child.exitCode !== null) {
      throw new Error(
        `Basecamp ${label} exited before inspector connection: ${processState?.child.exitCode}`,
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
      await app.click(view.launcher);
    },
    {
      timeout: 60_000,
      interval: 500,
      description: `${view.launcher} launcher ${label}`,
    },
  );
  await app.waitFor(
    async () => {
      try {
        await app.expectTexts(view.expectedTexts);
      } catch (error) {
        await app.click(view.launcher);
        throw error;
      }
    },
    {
      timeout: 60_000,
      interval: 500,
      description: `${view.launcher} content ${label}`,
    },
  );
  await app.waitFor(
    async () => {
      const result = await app.findByProperty(
        "objectName",
        view.rootObjectName,
      );
      if (result.error || !result.matches || result.matches.length !== 1) {
        throw new Error(
          `expected one ${view.rootObjectName}, got ${result.matches?.length ?? 0}`,
        );
      }
      rootObjectId = result.matches[0].id;
    },
    {
      timeout: 60_000,
      interval: 500,
      description: `${view.rootObjectName} ${label}`,
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
    launchNumber,
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

const allowedFunctions = viewArgument === "acceptance"
  ? new Set([
      "acceptanceStart",
      "acceptanceInject",
    ])
  : new Set([
      "gate2Start",
      "gate2Say",
      "gate2Move",
      "gate2Wear",
      "gate2Remove",
      "gate2RefreshPresence",
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
    throw new Error(`unsupported Gate 2 invocation: ${name}`);
  }
  const before = String(
    (await rootProperties())[view.receiptProperty] ?? "",
  );
  const expression =
    `${name}(${args.map((argument) => JSON.stringify(argument)).join(",")})`;
  const startedAt = performance.now();
  const evaluated = await evaluate(expression);
  const timeout = Math.min(Math.max(Number(params.timeout ?? 30_000), 1_000), 120_000);
  const deadline = Date.now() + timeout;
  let receipt = "";
  while (Date.now() < deadline) {
    receipt = String(
      (await rootProperties())[view.receiptProperty] ?? "",
    );
    const changed = receipt !== before;
    if (
      receiptMatches(receipt, params.expect) &&
      (params.allowSameReceipt === true || changed)
    ) {
      return {
        evaluated,
        receipt,
        elapsedMs: Math.round(performance.now() - startedAt),
      };
    }
    await sleep(50);
  }
  throw new Error(
    `${name} receipt timeout: before=${JSON.stringify(before)} after=${JSON.stringify(receipt)}`,
  );
}

async function detailedMatches(params) {
  if (!app) throw new Error("worker is not initialized");
  const response = await app.findByProperty(params.property, params.value);
  if (response.error) {
    throw new Error(`findByProperty failed: ${response.error}`);
  }
  const matches = [];
  for (const match of response.matches ?? []) {
    matches.push({
      id: match.id,
      type: match.type,
      properties: propertyMap(await app.getProperties(match.id)),
    });
  }
  return matches;
}

async function saveScreenshot(name) {
  if (!app) throw new Error("worker is not initialized");
  if (!/^[a-z0-9][a-z0-9._-]{0,127}\.png$/.test(name)) {
    throw new Error(`invalid screenshot name: ${name}`);
  }
  const response = await app.screenshot();
  if (response.error || !response.image) {
    throw new Error(`screenshot failed: ${response.error ?? "missing image"}`);
  }
  const path = join(artifactsDir, name);
  await writeFile(path, Buffer.from(response.image, "base64"));
  return {
    file: basename(path),
    width: response.width,
    height: response.height,
    sha256: await sha256File(path),
  };
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
  case "find":
    return detailedMatches(params ?? {});
  case "screenshot":
    return saveScreenshot(params?.name);
  case "renderTimings":
    if (!processState) throw new Error("worker is not initialized");
    return qsgRenderTimingEvidence(processState.stderrChunks);
  case "restart":
    await stopBasecamp();
    return startBasecamp();
  case "crashRestart": {
    const crash = await crashBasecamp();
    return { crash, ...(await startBasecamp()) };
  }
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
        `${JSON.stringify({ id: null, ok: false, error: "invalid JSON command" })}\n`,
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

async function terminate(signal) {
  if (shuttingDown) return;
  shuttingDown = true;
  await stopBasecamp();
  process.exit(signal === "SIGTERM" ? 0 : 1);
}
process.on("SIGTERM", () => {
  void terminate("SIGTERM");
});
process.on("SIGINT", () => {
  void terminate("SIGINT");
});
