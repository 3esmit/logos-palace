#!/usr/bin/env node

import { createHash } from "node:crypto";
import { createReadStream } from "node:fs";
import {
  mkdir,
  readdir,
  readFile,
  realpath,
  writeFile,
} from "node:fs/promises";
import { spawn } from "node:child_process";
import { basename, join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const [basecampArgument, userDirArgument, artifactsArgument, lgxDirArgument] =
  process.argv.slice(2);
if (
  !basecampArgument ||
  !userDirArgument ||
  !artifactsArgument ||
  !lgxDirArgument
) {
  throw new Error(
    "usage: node tests/basecamp_gate1.mjs <Basecamp> <user-dir> <artifacts-dir> <lgx-dir>",
  );
}

const qtMcpRoot = process.env.LOGOS_QT_MCP;
if (!qtMcpRoot) {
  throw new Error("LOGOS_QT_MCP must point to the pinned logos-qt-mcp output");
}

const basecamp = resolve(basecampArgument);
const userDir = resolve(userDirArgument);
const artifactsDir = resolve(artifactsArgument);
const lgxDir = resolve(lgxDirArgument);
await mkdir(artifactsDir, { recursive: true });

const frameworkUrl = pathToFileURL(
  resolve(qtMcpRoot, "test-framework/framework.mjs"),
).href;
const { App, Inspector } = await import(frameworkUrl);

const sleep = (milliseconds) =>
  new Promise((resolveSleep) => setTimeout(resolveSleep, milliseconds));

const expectedBackgroundHandles = {
  Atrium: "3bd13dc41f3e27a7eabf45c73188498b95e3e5e475e6fcb967308477afd522be",
  Lounge: "d2068f9cc4848b29882e532580c2b455ef5d243e7b38f16d590937fbef720486",
};

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

async function packageHashes() {
  const names = (await readdir(lgxDir))
    .filter((name) => name.endsWith(".lgx"))
    .sort();
  return Promise.all(
    names.map(async (name) => ({
      file: name,
      sha256: await sha256File(join(lgxDir, name)),
    })),
  );
}

function launchBasecamp(label) {
  const child = spawn(
    basecamp,
    ["--user-dir", userDir, "-platform", "offscreen"],
    {
      env: {
        ...process.env,
        QT_FORCE_STDERR_LOGGING: "1",
        QT_QPA_PLATFORM: "offscreen",
      },
      detached: true,
      stdio: ["ignore", "pipe", "pipe"],
    },
  );
  const stdoutChunks = [];
  const stderrChunks = [];
  child.stdout.on("data", (chunk) => stdoutChunks.push(chunk));
  child.stderr.on("data", (chunk) => stderrChunks.push(chunk));
  const exited = new Promise((resolveExit) => {
    child.once("exit", (code, signal) => resolveExit({ code, signal }));
  });

  return {
    child,
    exited,
    async saveLogs() {
      await writeFile(
        join(artifactsDir, `${label}.stdout.log`),
        Buffer.concat(stdoutChunks),
      );
      await writeFile(
        join(artifactsDir, `${label}.stderr.log`),
        Buffer.concat(stderrChunks),
      );
    },
  };
}

function signalProcessGroup(child, signal) {
  try {
    process.kill(-child.pid, signal);
  } catch {
    child.kill(signal);
  }
}

async function stopBasecamp(processState) {
  if (processState.child.exitCode === null) {
    signalProcessGroup(processState.child, "SIGTERM");
    const cleanExit = await Promise.race([
      processState.exited.then(() => true),
      sleep(10_000).then(() => false),
    ]);
    if (!cleanExit) {
      signalProcessGroup(processState.child, "SIGKILL");
      await processState.exited;
    }
  }
  await processState.saveLogs();
}

async function connectInspector(processState) {
  let lastError = new Error("inspector did not start");
  for (let attempt = 0; attempt < 240; attempt += 1) {
    if (processState.child.exitCode !== null) {
      throw new Error(
        `Basecamp exited before inspector connection: ${processState.child.exitCode}`,
      );
    }
    const inspector = new Inspector();
    try {
      await inspector.connect();
      return inspector;
    } catch (error) {
      lastError = error;
      inspector.disconnect();
      await sleep(500);
    }
  }
  throw lastError;
}

async function waitForPalace(app, roomTitle, doorTitle) {
  await app.waitFor(
    async () => {
      await app.click("Logos Palace");
    },
    {
      timeout: 60_000,
      interval: 500,
      description: "Logos Palace launcher",
    },
  );
  await app.waitFor(
    async () => {
      await app.expectTexts([roomTitle, doorTitle]);
    },
    {
      timeout: 60_000,
      interval: 500,
      description: `Palace ${roomTitle} room`,
    },
  );
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

async function objectProperties(app, objectName) {
  const result = await app.findByProperty("objectName", objectName);
  if (result.error || !result.matches || result.matches.length !== 1) {
    throw new Error(
      `expected one ${objectName}, got ${result.matches?.length ?? 0}`,
    );
  }
  return propertyMap(await app.getProperties(result.matches[0].id));
}

async function verifiedBackground(app, roomTitle) {
  let observed;
  await app.waitFor(
    async () => {
      const image = await objectProperties(app, "palaceRoomBackground");
      const placeholder = await objectProperties(
        app,
        "palaceRoomBackgroundPlaceholder",
      );
      const source = String(image.source ?? "");
      const match = source.match(
        /^image:\/\/basecamp-verified\/([0-9a-f]{64})$/,
      );
      const ready = image.status === 1 || String(image.status) === "Ready";
      const placeholderHidden =
        placeholder.visible === false || String(placeholder.visible) === "false";
      if (!match || !ready || !placeholderHidden) {
        throw new Error(
          `background not ready: source=${source} status=${image.status} placeholder=${placeholder.visible}`,
        );
      }
      if (match[1] !== expectedBackgroundHandles[roomTitle]) {
        throw new Error(
          `${roomTitle} background digest mismatch: ${match[1]}`,
        );
      }
      observed = { handle: match[1], source };
    },
    {
      timeout: 60_000,
      interval: 500,
      description: "verified room background",
    },
  );
  return observed;
}

async function findNamedFiles(root, wantedName) {
  const found = [];
  for (const entry of await readdir(root, { withFileTypes: true })) {
    const path = join(root, entry.name);
    if (entry.isDirectory()) {
      found.push(...(await findNamedFiles(path, wantedName)));
    } else if (entry.isFile() && entry.name === wantedName) {
      found.push(path);
    }
  }
  return found;
}

async function verifyPersistedProjection() {
  const projectionFiles = await findNamedFiles(
    join(userDir, "module_data", "palace_core"),
    "projection-v1",
  );
  if (projectionFiles.length !== 1) {
    throw new Error(
      `expected one Palace projection, got ${projectionFiles.length}`,
    );
  }
  const record = await readFile(projectionFiles[0], "utf8");
  const newline = record.indexOf("\n");
  if (newline !== 64) {
    throw new Error("persisted projection has invalid checksum framing");
  }
  const checksum = record.slice(0, newline);
  const state = record.slice(newline + 1);
  const calculated = createHash("sha256").update(state).digest("hex");
  if (checksum !== calculated) {
    throw new Error("persisted projection checksum mismatch");
  }
  if (!state.includes("version=1;room=lounge;")) {
    throw new Error(`persisted projection did not retain Lounge: ${state}`);
  }
  await writeFile(join(artifactsDir, "projection-v1"), record);
  return { checksum, state };
}

async function saveScreenshot(app, name) {
  const response = await app.screenshot();
  if (response.error || !response.image) {
    throw new Error(`screenshot failed: ${response.error ?? "missing image"}`);
  }
  const path = join(artifactsDir, name);
  await writeFile(path, Buffer.from(response.image, "base64"));
  return {
    file: name,
    width: response.width,
    height: response.height,
    sha256: await sha256File(path),
  };
}

const timings = {};
const screenshots = [];
let firstProcess;
let secondProcess;
let firstInspector;
let secondInspector;
let atriumBackground;
let loungeBackground;
let restoredBackground;
let persistedProjection;

try {
  const initialStart = performance.now();
  firstProcess = launchBasecamp("basecamp-initial");
  firstInspector = await connectInspector(firstProcess);
  const firstApp = new App(firstInspector);
  await waitForPalace(firstApp, "Atrium", "Door to Lounge");
  atriumBackground = await verifiedBackground(firstApp, "Atrium");
  timings.initialRenderMs = Math.round(performance.now() - initialStart);
  screenshots.push(await saveScreenshot(firstApp, "atrium.png"));

  const doorStart = performance.now();
  await firstApp.click("Door to Lounge");
  await firstApp.waitFor(
    async () => {
      await firstApp.expectTexts(["Lounge", "Door to Atrium"]);
    },
    {
      timeout: 30_000,
      interval: 300,
      description: "door navigation to Lounge",
    },
  );
  loungeBackground = await verifiedBackground(firstApp, "Lounge");
  if (loungeBackground.handle === atriumBackground.handle) {
    throw new Error("two rooms resolved to the same background handle");
  }
  timings.doorMs = Math.round(performance.now() - doorStart);
  screenshots.push(await saveScreenshot(firstApp, "lounge.png"));

  firstInspector.disconnect();
  firstInspector = undefined;
  await stopBasecamp(firstProcess);
  firstProcess = undefined;
  persistedProjection = await verifyPersistedProjection();

  const restartStart = performance.now();
  secondProcess = launchBasecamp("basecamp-restart");
  secondInspector = await connectInspector(secondProcess);
  const secondApp = new App(secondInspector);
  await waitForPalace(secondApp, "Lounge", "Door to Atrium");
  restoredBackground = await verifiedBackground(secondApp, "Lounge");
  if (restoredBackground.handle !== loungeBackground.handle) {
    throw new Error(
      `restored Lounge handle changed: ${loungeBackground.handle} -> ${restoredBackground.handle}`,
    );
  }
  timings.restartRenderMs = Math.round(performance.now() - restartStart);
  screenshots.push(await saveScreenshot(secondApp, "lounge-restored.png"));

  const installManifestPath = join(artifactsDir, "installed-packages.json");
  const installManifest = JSON.parse(await readFile(installManifestPath, "utf8"));
  const report = {
    schema: "logos-palace-basecamp-gate1-report-v1",
    result: "PASS",
    basecamp: {
      binary: await realpath(basecamp),
      revision: process.env.PALACE_BASECAMP_REV ?? "unknown",
      sha256: await sha256File(basecamp),
    },
    installedPackages: installManifest,
    lgxPackages: await packageHashes(),
    rooms: {
      atriumBackground,
      loungeBackground,
      restoredBackground,
    },
    persistedProjection,
    timings,
    screenshots,
  };
  await writeFile(
    join(artifactsDir, "gate1-report.json"),
    `${JSON.stringify(report, null, 2)}\n`,
  );
  process.stdout.write("BASECAMP_GATE1_RESULT=PASS\n");
} finally {
  firstInspector?.disconnect();
  secondInspector?.disconnect();
  if (firstProcess) await stopBasecamp(firstProcess);
  if (secondProcess) await stopBasecamp(secondProcess);
}
