#!/usr/bin/env node

// Local-only compiled Basecamp user story: one creator publishes a Palace,
// two independent users join through the copied Palace address, catalog, and
// Storage peer endpoint, then exchange live Delivery traffic.

import { createHash } from "node:crypto";
import { mkdir, writeFile } from "node:fs/promises";
import { spawn } from "node:child_process";
import { basename, resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { loadGate3AssetInputs } from "./basecamp_gate3_asset_inputs.mjs";
import { capturePalaceFrameTiming } from "./basecamp_frame_timing.mjs";

const [basecampArgument, creatorDirArgument, bobDirArgument, carolDirArgument, evidenceDirArgument] =
  process.argv.slice(2);
if (!basecampArgument || !creatorDirArgument || !bobDirArgument || !carolDirArgument || !evidenceDirArgument) {
  throw new Error(
    "usage: basecamp_local_mvp_user_flow.mjs <LogosBasecamp> <creator-dir> <bob-dir> <carol-dir> <evidence-dir>",
  );
}

const qtMcpRoot = process.env.LOGOS_QT_MCP;
if (!qtMcpRoot) throw new Error("LOGOS_QT_MCP is required");
const assetInputRoot = process.env.PALACE_E2E_ASSET_INPUT_ROOT;
const assetManifest = process.env.PALACE_E2E_ASSET_MANIFEST;
const assetInputs = await loadGate3AssetInputs({
  inputRoot: assetInputRoot,
  manifestPath: assetManifest,
});

const basecamp = resolve(basecampArgument);
const creatorDir = resolve(creatorDirArgument);
const bobDir = resolve(bobDirArgument);
const carolDir = resolve(carolDirArgument);
const evidenceDir = resolve(evidenceDirArgument);
const roomBackgrounds = assetInputs.fixtures
  .filter((fixture) => fixture.role === "room-background")
  .map((fixture) => {
    const roomId = fixture.assignment?.roomId;
    if (!roomId) throw new Error(`asset manifest background lacks room assignment: ${fixture.assetId}`);
    return {
      roomId,
      label: fixture.title,
      file: assetInputs.selectionPathFor(fixture.assetId),
      assetId: fixture.assetId,
      assignment: `palaceBackgroundAssign${roomId[0].toUpperCase()}${roomId.slice(1)}-`,
    };
  });
if (!roomBackgrounds.some((asset) => asset.roomId === "atrium")
  || !roomBackgrounds.some((asset) => asset.roomId === "lounge")) {
  throw new Error("asset manifest lacks an Atrium or Lounge background");
}
const propFixture = assetInputs.fixtures.find(
  (candidate) => candidate.role === "prop-image" && candidate.assignment?.kind === "prop-image",
);
const propInput = propFixture
  ? {
    file: assetInputs.selectionPathFor(propFixture.assetId),
    assetId: propFixture.assetId,
    assignment: propFixture.assignment,
  }
  : null;
const deliveryClusterId = 4346;
const deliveryNodeKeys = {
  creator: createHash("sha256").update("logos-palace-local-mvp-delivery/creator").digest("hex"),
  bob: createHash("sha256").update("logos-palace-local-mvp-delivery/bob").digest("hex"),
  carol: createHash("sha256").update("logos-palace-local-mvp-delivery/carol").digest("hex"),
};

const frameworkUrl = pathToFileURL(
  resolve(qtMcpRoot, "test-framework/framework.mjs"),
).href;
const sleep = (milliseconds) => new Promise((done) => setTimeout(done, milliseconds));
const storyStartedAt = Date.now();
const timingSamples = {
  actionReceipt: [],
  deliveryStartup: [],
  deliveryReceive: [],
  storageRetention: [],
  restart: [],
  uiScreenshot: [],
  frameTiming: null,
  applicationRoundTrip: null,
  vmTurn: [],
};

function summarizeTimings(samples) {
  const values = samples
    .map((sample) => typeof sample === "number"
      ? sample
      : (sample.elapsedMs ?? sample.roundTripMs))
    .filter((value) => Number.isSafeInteger(value) && value >= 0)
    .sort((left, right) => left - right);
  if (values.length === 0) {
    return { sampleCount: 0, p50Ms: null, p95Ms: null, maxMs: null };
  }
  const nearestRank = (fraction) => values[Math.max(
    0,
    Math.ceil(values.length * fraction) - 1,
  )];
  return {
    sampleCount: values.length,
    p50Ms: nearestRank(0.50),
    p95Ms: nearestRank(0.95),
    maxMs: values.at(-1),
  };
}

function storyTimings(orderedMessaging) {
  return {
    wallClockMs: Date.now() - storyStartedAt,
    delivery: {
      orderedProjectionConvergenceMs: orderedMessaging.elapsedMs,
      startupMs: summarizeTimings(timingSamples.deliveryStartup),
      actionReceiptMs: summarizeTimings(
        timingSamples.actionReceipt.filter((sample) => sample.operation === "sendSpeech"),
      ),
      sendToReceive: {
        status: timingSamples.deliveryReceive.length > 0 ? "measured" : "not-measured",
        samples: summarizeTimings(timingSamples.deliveryReceive),
        sampling: "remote receivers only; every 30th ordered message is awaited before the next send",
        reason: "projection only retains the latest speech per sender; each sampled message is awaited immediately so it cannot be overwritten before measurement",
      },
    },
    storage: {
      retentionMs: summarizeTimings(timingSamples.storageRetention),
    },
    lezAndApplication: {
      actionReceiptMs: summarizeTimings(
        timingSamples.actionReceipt.filter((sample) => sample.operation !== "sendSpeech"),
      ),
    },
    recovery: {
      restartMs: summarizeTimings(timingSamples.restart),
    },
    ui: {
      screenshotMs: summarizeTimings(timingSamples.uiScreenshot),
      frameTiming: timingSamples.frameTiming,
      ipcPayload: timingSamples.applicationRoundTrip,
    },
    vm: {
      turnMetrics: timingSamples.vmTurn,
      peakMemory: { status: "not-measured", reason: "VM module exposes turn duration but not per-turn peak memory" },
    },
    measurementPolicy: "wall-clock samples from the compiled user story; no inferred thresholds",
  };
}

function statusValue(status, name) {
  const prefix = `${name}=`;
  for (const field of String(status ?? "").split(";")) {
    if (field.startsWith(prefix)) return field.slice(prefix.length);
  }
  return "";
}

function isRejected(value) { return String(value ?? "").startsWith("rejected="); }

function statusNumber(status, name) {
  const value = Number(statusValue(status, name));
  return Number.isSafeInteger(value) ? value : 0;
}

function catalogObject(catalogBase64, objectId) {
  const catalog = Buffer.from(String(catalogBase64), "base64url").toString("utf8");
  const line = catalog.split("\n").find((entry) => entry.startsWith(`object=${objectId};`));
  if (!line) throw new Error(`Storage catalog lacks ${objectId}`);
  const fields = line.slice("object=".length).split(";");
  if (fields.length !== 6 || fields[0] !== objectId
    || fields[2] !== "image/png" || !/^[0-9a-f]{64}$/.test(fields[5])) {
    throw new Error(`Storage catalog object ${objectId} is invalid`);
  }
  const byteLength = Number(fields[4]);
  if (!Number.isSafeInteger(byteLength) || byteLength <= 0) {
    throw new Error(`Storage catalog object ${objectId} byte length is invalid`);
  }
  return {
    objectId,
    type: fields[1],
    mediaType: fields[2],
    cid: fields[3],
    byteLength,
    contentSha256: fields[5],
  };
}

function derivedMissingCid(cid, offset) {
  const alphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
  const value = String(cid);
  const index = alphabet.indexOf(value.at(-1));
  if (value.length < 4 || index < 0) {
    throw new Error(`Storage CID cannot derive missing fixture: ${value}`);
  }
  return value.slice(0, -1) + alphabet[(index + offset) % alphabet.length];
}

function propertyMap(response) {
  if (response.error) throw new Error(`properties: ${response.error}`);
  return Object.fromEntries((response.properties ?? []).map((entry) => [entry.name, entry.value]));
}

class BasecampSession {
  constructor(label, userDir, inspectorPort) {
    this.label = label;
    this.userDir = resolve(userDir);
    this.inspectorPort = inspectorPort;
    this.child = null;
    this.inspector = null;
    this.app = null;
    this.stderr = [];
  }

  async start() {
    this.child = spawn(basecamp, ["--user-dir", this.userDir, "-platform", "offscreen"], {
      detached: true,
      stdio: ["ignore", "pipe", "pipe"],
      env: {
        ...process.env,
        QML_INSPECTOR_PORT: String(this.inspectorPort),
        PALACE_LEZ_PROFILE: "local-development",
        QT_QPA_PLATFORM: "offscreen",
        QT_FORCE_STDERR_LOGGING: "1",
      },
    });
    this.child.stderr.on("data", (chunk) => this.stderr.push(chunk));
    this.child.stdout.resume();
    let lastError = new Error(`${this.label} inspector did not start`);
    for (let attempt = 0; attempt < 240; attempt += 1) {
      if (this.child.exitCode !== null) {
        throw new Error(`${this.label} Basecamp exited before inspector: ${this.child.exitCode}`);
      }
      try {
        // Query suffix gives each import its own framework module and port
        // constant; both inspectors can therefore run in one Node process.
        const previousPort = process.env.QML_INSPECTOR_PORT;
        process.env.QML_INSPECTOR_PORT = String(this.inspectorPort);
        const framework = await import(`${frameworkUrl}?port=${this.inspectorPort}`);
        if (previousPort === undefined) delete process.env.QML_INSPECTOR_PORT;
        else process.env.QML_INSPECTOR_PORT = previousPort;
        const candidate = new framework.Inspector();
        await candidate.connect();
        this.inspector = candidate;
        this.app = new framework.App(candidate);
        return;
      } catch (error) {
        lastError = error;
        await sleep(500);
      }
    }
    throw lastError;
  }

  async stop() {
    this.inspector?.disconnect();
    this.inspector = null;
    this.app = null;
    if (this.child && this.child.exitCode === null) {
      try {
        process.kill(-this.child.pid, "SIGTERM");
      } catch (error) {
        // Detached Basecamp may finish during the final assertion. Treat an
        // already-reaped process as an idempotent cleanup result.
        if (error?.code !== "ESRCH") throw error;
      }
      await Promise.race([
        new Promise((done) => this.child.once("exit", done)),
        sleep(20_000),
      ]);
    }
  }

  async properties(objectId) { return propertyMap(await this.app.getProperties(objectId)); }

  async setProperty(objectId, property, value) {
    const result = await this.inspector.send("setProperty", { objectId, property, value });
    if (result.error) throw new Error(`${this.label} set ${property}: ${result.error}`);
  }

  async clickObject(objectId, description) {
    const result = await this.inspector.send("click", { objectId });
    if (result.error || result.clicked !== true) {
      throw new Error(`${this.label} ${description}: click failed: ${result.error ?? "unknown"}`);
    }
  }

  async findOne(property, value, description) {
    let result;
    let chosen;
    await this.app.waitFor(async () => {
      result = await this.app.findByProperty(property, value);
      if (result.error || !Array.isArray(result.matches) || result.matches.length === 0) {
        throw new Error(`${this.label} ${description}: expected match`);
      }
      if (result.matches.length === 1) {
        const properties = await this.properties(result.matches[0].id);
        if (properties.visible === true) {
          chosen = result.matches[0].id;
          return;
        }
        throw new Error(`${this.label} ${description}: match not ready`);
      }
      const candidates = await Promise.all(result.matches.map(async (match) => ({
        id: match.id,
        properties: await this.properties(match.id),
      })));
      const visible = candidates.filter((candidate) => candidate.properties.visible === true);
      if (visible.length === 1) { chosen = visible[0].id; return; }
      throw new Error(`${this.label} ${description}: ambiguous ${JSON.stringify(candidates)}`);
    }, { timeout: 90_000, interval: 300, description: `${this.label} ${description}` });
    return chosen;
  }

  async clickNamed(name, description) {
    const objectId = await this.findOne("objectName", name, description);
    if (name.startsWith("palaceAsset")) {
      const root = await this.findOne("objectName", "palaceRoot", `${this.label} Palace root`);
      const scrolled = await this.callRoot(root, "ensureModerationControlVisible", [name]);
      if (isRejected(scrolled)) throw new Error(`${this.label} ${description}: asset control scroll rejected: ${scrolled}`);
    }
    await this.clickObject(objectId, description);
  }

  async callRoot(root, method, args = []) {
    const result = await this.inspector.send("callMethod", {
      objectId: root,
      method,
      args,
    });
    if (result.error) throw new Error(`${this.label} ${method}: ${result.error}`);
    return result.result;
  }

  async evaluate(root, expression) {
    if (typeof expression !== "string" || expression.length > 70_000) {
      throw new Error(`${this.label} evaluate expression is invalid`);
    }
    const result = await this.inspector.send("evaluate", {
      objectId: root,
      expression,
    });
    if (result.error) throw new Error(`${this.label} evaluate: ${result.error}`);
    return result;
  }

  async frameTiming(root) {
    return capturePalaceFrameTiming({
      evaluate: (expression) => this.evaluate(root, expression),
      rootProperties: () => this.properties(root),
      sleep,
    });
  }

  async waitForProperty(objectId, predicate, description, timeout = 120_000) {
    const deadline = Date.now() + timeout;
    let current = {};
    while (Date.now() < deadline) {
      current = await this.properties(objectId);
      if (predicate(current)) return current;
      await sleep(500);
    }
    throw new Error(`${this.label} ${description}: timed out; state=${JSON.stringify(current)}`);
  }

  async screenshot(name) {
    const startedAt = Date.now();
    const response = await this.app.screenshot();
    if (response.error || typeof response.image !== "string") {
      throw new Error(`${this.label} screenshot ${name}: ${response.error ?? "invalid image"}`);
    }
    const bytes = Buffer.from(response.image, "base64");
    if (!bytes.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]))) {
      throw new Error(`${this.label} screenshot ${name}: not PNG`);
    }
    const path = resolve(evidenceDir, name);
    await writeFile(path, bytes, { mode: 0o600 });
    const elapsedMs = Date.now() - startedAt;
    timingSamples.uiScreenshot.push(elapsedMs);
    return { file: basename(path), width: response.width, height: response.height,
      sha256: createHash("sha256").update(bytes).digest("hex"), elapsedMs };
  }
}

function deliveryConfig(label, port, entryNodes, entryLabel = "creator") {
  return JSON.stringify({
    mode: label === entryLabel ? "Core" : "Edge",
    relay: true,
    store: false,
    clusterId: deliveryClusterId,
    numShardsInNetwork: 1,
    entryNodes,
    tcpPort: port,
    nat: "extip:127.0.0.1",
    listenAddress: "127.0.0.1",
    nodekey: deliveryNodeKeys[label],
    discv5Discovery: false,
    websocketSupport: false,
    quicSupport: false,
    logLevel: "WARN",
  });
}

function addressStrings(value) {
  if (typeof value === "string") return value.split(",").map((entry) => entry.trim()).filter(Boolean);
  if (Array.isArray(value)) return value.flatMap(addressStrings);
  if (value && typeof value === "object") return Object.values(value).flatMap(addressStrings);
  return [];
}

function peerIdFromEvidence(evidence) {
  const candidates = [];
  if (typeof evidence.peerId === "string") candidates.push(evidence.peerId);
  if (evidence.peerId && typeof evidence.peerId === "object") {
    candidates.push(evidence.peerId.peerId, evidence.peerId.value, evidence.peerId.id);
  }
  const peerId = candidates.find((value) => typeof value === "string" && value.length > 20);
  if (!peerId) throw new Error(`Delivery evidence lacks peer ID: ${JSON.stringify(evidence)}`);
  return peerId;
}

function loopbackEntryNode(evidence, port) {
  const addresses = addressStrings(evidence.multiaddresses);
  const found = addresses.some((address) => {
    const match = address.match(/^\/(?:ip4|ip6)\/[^/]+\/tcp\/([0-9]+)(?:\/|$)/);
    return match && Number(match[1]) === port;
  });
  if (!found) throw new Error(`Delivery evidence lacks TCP ${port}: ${JSON.stringify(addresses)}`);
  return `/ip4/127.0.0.1/tcp/${port}/p2p/${peerIdFromEvidence(evidence)}`;
}

function parseParticipants(value, label) {
  let decoded;
  try { decoded = JSON.parse(String(value || "[]")); } catch (error) {
    throw new Error(`${label} participant projection is not JSON: ${error.message}`);
  }
  if (!Array.isArray(decoded)) throw new Error(`${label} participant projection is not an array`);
  return decoded;
}

async function startDelivery(session, root, label, port, entryNodes, waitOnline = true) {
  const startedAt = Date.now();
  await session.callRoot(root, "startDelivery", [deliveryConfig(label, port, entryNodes)]);
  const state = await session.waitForProperty(root, (value) =>
    statusValue(value.deliveryStatus, "callbacks") === "1"
      && statusValue(value.deliveryStatus, "node_running") === "1",
  `${label} Delivery node started`, 180_000);
  const online = waitOnline
    ? await session.waitForProperty(root, (value) =>
      statusValue(value.deliveryStatus, "state") === "online",
    `${label} Delivery online`, 180_000)
    : state;
  let evidence;
  try { evidence = JSON.parse(String(online.deliveryNodeEvidence)); } catch (error) {
    throw new Error(`${label} Delivery evidence is not JSON: ${error.message}`);
  }
  if (evidence.success !== true) throw new Error(`${label} Delivery evidence: ${JSON.stringify(evidence)}`);
  timingSamples.deliveryStartup.push(Date.now() - startedAt);
  return { config: deliveryConfig(label, port, entryNodes), evidence };
}

async function refreshPresence(session, root, label) {
  const result = await session.callRoot(root, "refreshPresence");
  if (isRejected(result)) throw new Error(`${label} presence: ${result}`);
}

async function waitForParticipants(sessions, roots, expectedIds, description) {
  for (const [label, session] of Object.entries(sessions)) {
    await session.waitForProperty(roots[label], (value) => {
      const participants = parseParticipants(value.participantProjection, label);
      const present = new Set(participants.filter((entry) => entry.present === true).map((entry) => entry.userId));
      return expectedIds.every((id) => present.has(id));
    }, `${description} (${label})`, 180_000);
  }
}

function receiptRequestCounter(receipt) {
  const match = String(receipt ?? "").match(/(?:^|;)request=[^;]*-([0-9]+)(?:;|$)/);
  if (!match) throw new Error(`Delivery receipt has no request counter: ${receipt}`);
  const counter = Number(match[1]);
  if (!Number.isSafeInteger(counter) || counter < 1) {
    throw new Error(`Delivery receipt request counter invalid: ${receipt}`);
  }
  return counter;
}

async function sendOrderedSpeech(sessions, roots, identities) {
  const labels = ["creator", "bob", "carol"];
  const perSender = Object.fromEntries(labels.map((label) => [label, 0]));
  const lastRequest = Object.fromEntries(labels.map((label) => [label, 0]));
  const baselineAccepted = {};
  const baselineRejected = {};
  for (const label of labels) {
    const state = await sessions[label].properties(roots[label]);
    baselineAccepted[label] = statusNumber(state.deliveryStatus, "received_accepted");
    baselineRejected[label] = statusNumber(state.deliveryStatus, "received_rejected");
  }
  const receipts = [];
  const startedAt = Date.now();
  for (let ordinal = 1; ordinal <= 300; ordinal += 1) {
    const label = labels[(ordinal - 1) % labels.length];
    const message = `ordered-${String(ordinal).padStart(3, "0")}-${label}-${identities[label]}`;
    const sendStartedAt = Date.now();
    const receipt = await invokeWatchedReceipt(
      sessions[label],
      roots[label],
      "sendSpeech",
      [message],
      `${label} ordered speech ${ordinal}`,
    );
    const requestCounter = receiptRequestCounter(receipt);
    if (requestCounter <= lastRequest[label]) {
      throw new Error(`${label} Delivery request order regressed: ${lastRequest[label]} -> ${requestCounter}`);
    }
    lastRequest[label] = requestCounter;
    perSender[label] += 1;
    receipts.push({ ordinal, sender: label, message, receipt, requestCounter });
    if (ordinal % 30 === 0) {
      for (const receiver of labels) {
        if (receiver === label) continue;
        await sessions[receiver].waitForProperty(roots[receiver], (value) => {
          const participant = participantById(value.participantProjection, identities[label], receiver);
          return participant?.speech === message;
        }, `${receiver} receives ordered speech ${ordinal} from ${label}`, 180_000);
        timingSamples.deliveryReceive.push({
          ordinal,
          sender: label,
          receiver,
          elapsedMs: Date.now() - sendStartedAt,
        });
      }
    }
  }
  const finalSpeech = Object.fromEntries(
    labels.map((label) => [
      identities[label], receipts.filter((entry) => entry.sender === label).at(-1).message,
    ]),
  );
  for (const label of labels) {
    await sessions[label].waitForProperty(roots[label], (value) => {
      const participants = parseParticipants(value.participantProjection, label);
      const byId = Object.fromEntries(participants.map((entry) => [entry.userId, entry]));
      return labels.every((sender) => byId[identities[sender]]?.speech === finalSpeech[identities[sender]])
        && statusNumber(value.deliveryStatus, "received_accepted") >= baselineAccepted[label] + 300
        && statusNumber(value.deliveryStatus, "received_rejected") === baselineRejected[label];
    }, `300 ordered speeches converge (${label})`, 300_000);
  }
  const receiverStates = {};
  for (const label of labels) {
    const state = await sessions[label].properties(roots[label]);
    receiverStates[label] = {
      receivedAccepted: statusNumber(state.deliveryStatus, "received_accepted"),
      receivedRejected: statusNumber(state.deliveryStatus, "received_rejected"),
      rejectedReplay: statusNumber(state.deliveryStatus, "rejected_replay"),
      rejectedExpired: statusNumber(state.deliveryStatus, "rejected_expired"),
      rejectedSignature: statusNumber(state.deliveryStatus, "rejected_signature"),
      rejectedScope: statusNumber(state.deliveryStatus, "rejected_scope"),
      rejectedPayload: statusNumber(state.deliveryStatus, "rejected_payload"),
    };
  }
  return {
    total: receipts.length,
    perSender,
    requestCounters: lastRequest,
    finalSpeech,
    elapsedMs: Date.now() - startedAt,
    receipts,
    receivers: receiverStates,
  };
}

async function invokeWatchedReceipt(session, root, method, args, description) {
  const startedAt = Date.now();
  const before = await session.properties(root);
  const sequence = Number(before.invocationSequence ?? 0);
  const immediate = await session.callRoot(root, method, args);
  if (isRejected(immediate)) throw new Error(`${description}: ${immediate}`);
  const receiptProperty = method === "acceptanceApplicationRoundTrip"
    ? "acceptanceRoundTripResponse"
    : "watchedActionReceipt";
  const state = await session.waitForProperty(root, (value) =>
    Number(value.invocationSequence ?? 0) > sequence
      && !isRejected(value.invocationError)
      && (receiptProperty === "acceptanceRoundTripResponse"
        || String(value.watchedActionReceipt || value.lastActionReceipt || "").length > 0),
  `${description} receipt`, 180_000);
  const receipt = receiptProperty === "acceptanceRoundTripResponse"
    ? String(state.acceptanceRoundTripResponse ?? "")
    : String(state.watchedActionReceipt || state.lastActionReceipt);
  timingSamples.actionReceipt.push({ operation: method, elapsedMs: Date.now() - startedAt });
  return receipt;
}

async function invokeWatchedExpression(session, root, expression, description) {
  const startedAt = Date.now();
  const before = await session.properties(root);
  const sequence = Number(before.invocationSequence ?? 0);
  const immediate = await session.evaluate(root, expression);
  if (isRejected(immediate.result)) throw new Error(`${description}: ${immediate.result}`);
  const state = await session.waitForProperty(root, (value) =>
    Number(value.invocationSequence ?? 0) > sequence
      && !isRejected(value.invocationError)
      && String(value.watchedActionReceipt || value.lastActionReceipt || "").length > 0,
  `${description} receipt`, 180_000);
  const receipt = String(state.watchedActionReceipt || state.lastActionReceipt);
  timingSamples.actionReceipt.push({ operation: expression.split("(", 1)[0], elapsedMs: Date.now() - startedAt });
  return receipt;
}

async function measureVmTurn(session, root, actionId, phase) {
  const receipt = await invokeWatchedReceipt(
    session,
    root,
    "gate5VmTurnMetrics",
    [actionId, phase],
    `${session.label} VM ${phase} metrics`,
  );
  const duration = Number(statusValue(receipt, "duration_ns"));
  if (statusValue(receipt, "status") !== "available"
    || statusValue(receipt, "clock") !== "steady_clock"
    || !Number.isSafeInteger(duration) || duration <= 0) {
    throw new Error(`${session.label} VM ${phase} metrics unavailable: ${receipt}`);
  }
  const sample = { actionId, phase, durationNs: duration, receipt };
  timingSamples.vmTurn.push(sample);
  return sample;
}

async function measureApplicationRoundTrip(session, root) {
  const payloads = [
    { bytes: 0, value: "" },
    { bytes: 256, value: "é".repeat(128) },
    { bytes: 4096, value: "é".repeat(2048) },
  ];
  const samplesPerSize = 4;
  const measurements = {};
  for (const { bytes, value } of payloads) {
    if (Buffer.byteLength(value, "utf8") !== bytes) {
      throw new Error(`application payload size ${bytes} is not exact`);
    }
    const samples = [];
    for (let ordinal = 1; ordinal <= samplesPerSize; ordinal += 1) {
      const receipt = await invokeWatchedReceipt(
        session,
        root,
        "acceptanceApplicationRoundTrip",
        [value],
        `application round trip ${bytes} bytes ${ordinal}`,
      );
      const responseBytes = Buffer.byteLength(receipt, "utf8");
      if (receipt !== value || responseBytes !== bytes) {
        throw new Error(`application round trip payload changed at ${bytes} bytes`);
      }
      const elapsedMs = timingSamples.actionReceipt.at(-1)?.elapsedMs;
      samples.push({ ordinal, requestUtf8Bytes: bytes, responseUtf8Bytes: responseBytes, roundTripMs: elapsedMs });
    }
    measurements[String(bytes)] = {
      payloadUtf8Bytes: bytes,
      requestUtf8Bytes: bytes,
      responseUtf8Bytes: bytes,
      samples,
      latency: summarizeTimings(samples.map((sample) => sample.roundTripMs)),
    };
  }
  return {
    status: "measured",
    clock: "compiled user story wall-clock milliseconds",
    payloadSemantics: "application UTF-8 bytes; not transport wire bytes",
    samplesPerSize,
    measurements,
  };
}

async function verifyStorageRetention(session, root, description) {
  const startedAt = Date.now();
  const receipt = await invokeWatchedReceipt(
    session,
    root,
    "gate3VerifyRetention",
    [],
    `${description} retention start`,
  );
  if (isRejected(receipt)) throw new Error(`${description} retention: ${receipt}`);
  const state = await session.waitForProperty(root, (value) => {
    const storage = String(value.storageStatus || "");
    const catalogVerified = Number(statusValue(storage, "catalog_verified"));
    const retained = Number(statusValue(storage, "retained"));
    return statusValue(storage, "catalog") === "retained"
      && Number.isSafeInteger(catalogVerified)
      && catalogVerified > 0
      && retained === catalogVerified;
  }, `${description} retained catalog`, 300_000);
  timingSamples.storageRetention.push(Date.now() - startedAt);
  return { receipt, status: state.storageStatus };
}

async function proveMissingStorageObject(session, root, catalogBase64) {
  const object = catalogObject(catalogBase64, "background-atrium");
  const missingSourceCid = derivedMissingCid(object.cid, 1);
  const missingDerivativeCid = derivedMissingCid(object.cid, 2);
  const before = await invokeWatchedReceipt(
    session,
    root,
    "gate3AssetStatus",
    [missingDerivativeCid],
    "missing Storage object initial status",
  );
  if (before !== "missing") {
    throw new Error(`missing Storage fixture was not missing: ${before}`);
  }
  const dispatched = await invokeWatchedExpression(
    session,
    root,
    `gate3FetchPng(${[
      missingSourceCid,
      missingDerivativeCid,
      object.byteLength,
      object.contentSha256,
      1600,
      900,
    ].map((argument) => JSON.stringify(argument)).join(",")})`,
    "missing Storage object fetch",
  );
  if (dispatched.startsWith("degraded;reason=")) {
    return {
      status: "passed",
      objectId: object.objectId,
      missingSourceCid,
      derivativeCid: missingDerivativeCid,
      expectedContentSha256: object.contentSha256,
      states: ["missing", "degraded"],
      before,
      dispatched,
      degraded: dispatched,
      recovery: "missing source is explicit; canonical catalog remains verified",
    };
  }
  if (!dispatched.startsWith("ok;asset=fetching;")) {
    throw new Error(`missing Storage fetch entered unexpected state: ${dispatched}`);
  }
  const deadline = Date.now() + 180_000;
  let last = dispatched;
  while (Date.now() < deadline) {
    last = await invokeWatchedReceipt(
      session,
      root,
      "gate3AssetStatus",
      [missingDerivativeCid],
      "missing Storage object status",
    );
    if (last.startsWith("degraded;reason=")) {
      return {
        status: "passed",
        objectId: object.objectId,
        missingSourceCid,
        derivativeCid: missingDerivativeCid,
        expectedContentSha256: object.contentSha256,
        states: ["missing", "fetching", "degraded"],
        before,
        dispatched,
        degraded: last,
        recovery: "missing source is explicit; canonical catalog remains verified",
      };
    }
    if (last !== "fetching") {
      throw new Error(`missing Storage object entered unexpected state: ${last}`);
    }
    await sleep(500);
  }
  throw new Error(`missing Storage object did not degrade: ${last}`);
}

async function waitForLocalAuthorityAction(session, root, action, description) {
  return session.waitForProperty(root, (value) => {
    const checkpoint = Number(statusValue(value.palaceState, "action"));
    return Number.isSafeInteger(checkpoint)
      && checkpoint >= action
      && /(?:state=local-committed|state=finalized)/.test(
        String(value.moderationState || ""),
      );
  }, description, 300_000);
}

function participantById(value, userId, label) {
  const participants = parseParticipants(value, label);
  return participants.find((entry) => entry.userId === userId) || null;
}

async function openView(session, label, rootName) {
  await session.app.waitFor(
    async () => { await session.app.click(label); },
    { timeout: 90_000, interval: 500, description: `${session.label} ${label} launcher` },
  );
  return session.findOne("objectName", rootName, `${label} root`);
}

function storageBootstrapSpr(endpoint) {
  const encoded = String(endpoint ?? "");
  if (!encoded.startsWith("ok;")) throw new Error(`invalid Storage endpoint: ${encoded}`);
  const payload = JSON.parse(encoded.slice(3));
  if (typeof payload.spr !== "string" || payload.spr.length === 0) {
    throw new Error(`Storage endpoint has no signed peer record: ${encoded}`);
  }
  return payload.spr;
}

async function startStorageThroughControl(session, port, discoveryPort, dataDir, bootstrapSpr = "") {
  const controlRoot = await openView(session, "Logos Control", "logosControlRoot");
  await session.waitForProperty(controlRoot, (state) => state.ready === true, "Control ready");
  const configInput = await session.findOne("objectName", "storageConfig", "Storage config");
  const startButton = await session.findOne("objectName", "startStorage", "Storage start");
  const config = {
    "log-level": "INFO", "listen-ip": "127.0.0.1", nat: "none",
    "listen-port": port, "disc-port": discoveryPort, "no-bootstrap-node": true,
    "data-dir": resolve(dataDir),
  };
  if (bootstrapSpr) {
    config["no-bootstrap-node"] = false;
    config["bootstrap-node"] = [bootstrapSpr];
  }
  await session.setProperty(configInput, "text", JSON.stringify(config));
  await session.clickObject(startButton, "Storage start");
  const state = await session.waitForProperty(
    controlRoot,
    (value) => statusValue(value.storageStatus, "state") === "running",
    "Storage running", 180_000,
  );
  return { state: state.storageStatus, screenshot: await session.screenshot(`${session.label}-storage-running.png`) };
}

async function restartExistingClient(
  session,
  password,
  palaceId,
  storagePort,
  storageDiscoveryPort,
  storageDir,
  catalogBase64,
  deliveryPort,
  entryNodes,
  waitForDeliveryOnline = true,
) {
  const startedAt = Date.now();
  await session.stop();
  await session.start();
  const root = await openView(session, "Logos Palace", "palaceRoot");
  await session.waitForProperty(root, (value) => value.ready === true, "restarted Palace ready");
  await session.callRoot(root, "gate4StartLez", [password]);
  await session.waitForProperty(root, (value) => {
    if (isRejected(value.invocationError)) throw new Error(`${session.label} LEZ restart: ${value.invocationError}`);
    return statusValue(value.lezState, "ready") === "1"
      && statusValue(value.lezState, "profile_state") === "bound";
  }, "restarted LEZ ready", 180_000);
  await session.waitForProperty(root, (value) => {
    if (isRejected(value.invocationError)) throw new Error(`${session.label} automatic Palace reopen: ${value.invocationError}`);
    return statusValue(value.palaceState, "palace") === "open"
      && statusValue(value.palaceState, "authority") === "local-committed";
  }, "automatic Palace reopen from durable Delivery session", 180_000);
  await startStorageThroughControl(
    session,
    storagePort,
    storageDiscoveryPort,
    storageDir,
  );
  await session.callRoot(root, "connectStorage", []);
  await session.callRoot(root, "gate3FetchBundle", [catalogBase64]);
  await session.waitForProperty(root, (value) =>
    statusValue(value.storageStatus, "storage") === "running"
      && statusValue(value.storageStatus, "catalog") === "verified",
  "restarted Storage ready", 180_000);
  await session.callRoot(root, "gate4OpenPalace", [`palace://${palaceId}`]);
  await session.waitForProperty(root, (value) => {
    if (isRejected(value.invocationError)) throw new Error(`${session.label} Palace restart: ${value.invocationError}`);
    return statusValue(value.palaceState, "palace") === "open"
      && Number(statusValue(value.palaceState, "action")) >= 8
      && JSON.parse(String(value.activePropAssetState || "{}")).available === false;
  }, "restarted Palace authority", 180_000);
  await session.callRoot(root, "startDelivery", [deliveryConfig(session.label, deliveryPort, entryNodes)]);
  await session.waitForProperty(root, (value) =>
    statusValue(value.deliveryStatus, "node_running") === "1",
  "restarted Delivery node", 180_000);
  if (waitForDeliveryOnline) {
    await session.waitForProperty(root, (value) =>
      statusValue(value.deliveryStatus, "state") === "online",
    "restarted Delivery online", 180_000);
  }
  // Storage control is a separate launcher view. Re-activate Palace so
  // recovery screenshots show the user-facing room, not node controls.
  const restartedRoot = await openView(session, "Logos Palace", "palaceRoot");
  timingSamples.restart.push({ session: session.label, elapsedMs: Date.now() - startedAt });
  return restartedRoot;
}

function collectPickerControlIds(node, matches, predicate) {
  if (!node || typeof node !== "object" || Array.isArray(node)) throw new Error("picker tree invalid");
  if (predicate(node)) matches.push(String(node.id ?? ""));
  if (node.children !== undefined) {
    if (!Array.isArray(node.children)) throw new Error("picker children invalid");
    for (const child of node.children) collectPickerControlIds(child, matches, predicate);
  }
}

async function pickerTree(session, dialogId) {
  const result = await session.app.getTree({ objectId: dialogId, depth: 16 });
  if (result.error || !result.tree) throw new Error(`${session.label} picker tree unavailable`);
  return result.tree;
}

function singlePickerControl(tree, predicate, description) {
  const matches = [];
  collectPickerControlIds(tree, matches, predicate);
  const distinct = [...new Set(matches.filter(Boolean))];
  if (distinct.length !== 1) throw new Error(`picker ${description} ambiguous: ${JSON.stringify(distinct)}`);
  return distinct[0];
}

async function importAsset(session, root, file) {
  const before = await session.properties(root);
  const generation = Number(before.assetImportGeneration ?? -1);
  const existing = new Set((await session.app.listFileDialogs()).dialogs?.map((dialog) => dialog.id) ?? []);
  await session.clickNamed("palaceAssetSelectFile", "Add PNG");
  let dialogId;
  await session.app.waitFor(async () => {
    dialogId = ((await session.app.listFileDialogs()).dialogs ?? [])
      .find((dialog) => !existing.has(dialog.id))?.id;
    if (!dialogId) throw new Error("file picker not visible");
  }, { timeout: 30_000, interval: 100, description: `${session.label} asset picker` });
  const tree = await pickerTree(session, dialogId);
  const inputId = singlePickerControl(tree, (node) => node.type === "QLineEdit"
    && node.objectName === "fileNameEdit" && node.visible === true && node.enabled === true, "file name");
  await session.clickObject(inputId, "picker filename");
  const sent = await session.inspector.send("sendKeys", { text: file });
  if (sent.error) throw new Error(`picker filename: ${sent.error}`);
  await session.waitForProperty(inputId, (state) => String(state.text ?? "") === file, "picker filename", 30_000);
  const submitted = await session.inspector.send("callMethod", { objectId: inputId, method: "returnPressed", args: [] });
  if (submitted.error) throw new Error(`picker submit: ${submitted.error}`);
  let asset;
  await session.waitForProperty(root, (state) => {
    if (Number(state.assetImportGeneration ?? -1) <= generation || state.assetImportRunning !== false) return false;
    if (isRejected(state.invocationError)) throw new Error(`asset import rejected: ${state.invocationError}`);
    let catalog;
    try { catalog = JSON.parse(String(state.assetAuthoringState)); } catch { return false; }
    asset = catalog?.assets?.find((candidate) => candidate?.label === basename(file));
    return Boolean(asset?.handle && /^[0-9a-f]{64}$/.test(asset.handle));
  }, `asset import ${basename(file)}`, 180_000);
  return asset;
}

async function approveAndPublish(session, root, asset) {
  await session.clickNamed(`palaceAssetApprove-${asset.handle}`, `approve ${asset.label}`);
  await session.waitForProperty(root, (state) => {
    if (isRejected(state.invocationError)) throw new Error(`asset publication rejected: ${state.invocationError}`);
    let catalog;
    try { catalog = JSON.parse(String(state.assetAuthoringState)); } catch { return false; }
    const current = catalog?.assets?.find((candidate) => candidate?.handle === asset.handle);
    return current?.publicationState === "published" && String(current.cid ?? "").length > 0;
  }, `publish ${asset.label}`, 180_000);
}

async function approveAndAssign(session, root, asset, assignment) {
  await approveAndPublish(session, root, asset);
  await session.clickNamed(`${assignment}${asset.handle}`, `assign ${asset.label}`);
  await session.waitForProperty(root, (state) => {
    let catalog;
    try { catalog = JSON.parse(String(state.assetAuthoringState)); } catch { return false; }
    const current = catalog?.assets?.find((candidate) => candidate?.handle === asset.handle);
    return Array.isArray(current?.roomAssignments) && current.roomAssignments.length > 0;
  }, `assign ${asset.label}`);
}

async function createCreatorPalace(session) {
  const root = await openView(session, "Logos Palace", "palaceRoot");
  await session.waitForProperty(root, (state) => state.ready === true, "Palace ready");
  const onboarding = await session.screenshot("creator-onboarding.png");
  let state = await session.properties(root);
  if (state.onboardingPhase === "creating-identity") {
    state = await session.waitForProperty(root, (value) => value.onboardingPhase !== "creating-identity", "creator identity", 180_000);
  }
  if (state.onboardingPhase === "details") {
    await session.setProperty(root, "onboardingPassword", "creator-e2e-password");
    await session.setProperty(root, "onboardingDisplayName", "Creator Admin");
    await session.setProperty(root, "onboardingPalaceTitle", "Provider Palace");
    await session.clickNamed("palaceOnboardingOpenButton", "creator room setup");
    await session.waitForProperty(root, (value) => {
      if (value.onboardingPhase === "error") throw new Error(`creator onboarding: ${value.onboardingError} (${value.onboardingReceipt})`);
      return value.onboardingPhase === "authoring-rooms" && value.backgroundModerationOpen === true;
    }, "creator room authoring", 180_000);
  } else if (state.onboardingPhase !== "authoring-rooms") {
    throw new Error(`creator unexpected onboarding phase: ${state.onboardingPhase}`);
  }
  await session.inspector.send("callMethod", { objectId: root, method: "connectStorage", args: [] });
  await session.waitForProperty(root, (value) => statusValue(value.storageStatus, "storage") === "running", "creator Storage connection", 120_000);
  const imported = [];
  for (const background of roomBackgrounds) {
    const asset = await importAsset(session, root, background.file);
    await approveAndAssign(session, root, asset, background.assignment);
    imported.push({ ...background, label: background.label, handle: asset.handle });
  }
  const activeBackgrounds = ["atrium", "lounge"].map((roomId) =>
    [...imported].reverse().find((asset) => asset.roomId === roomId));
  let propId = null;
  let propAsset = null;
  if (propInput) {
    propAsset = await importAsset(session, root, propInput.file);
    await approveAndPublish(session, root, propAsset);
    propId = propInput.assignment.propId;
    await session.setProperty(root, "propDraftId", propId);
    await session.setProperty(root, "propDraftAnchorX", String(propInput.assignment.anchorX));
    await session.setProperty(root, "propDraftAnchorY", String(propInput.assignment.anchorY));
    await session.setProperty(root, "propDraftLayer", propInput.assignment.layer);
    await session.clickNamed(`palaceAssetAssignProp-${propAsset.handle}`, `assign ${propId} prop`);
    await session.waitForProperty(root, (value) => {
      let catalog;
      try { catalog = JSON.parse(String(value.assetAuthoringState)); } catch { return false; }
      return catalog?.propAssignment?.propId === propId
        && catalog?.propAssignment?.handle === propAsset.handle;
    }, `${propId} prop assignment`, 120_000);
  }
  await session.waitForProperty(root, (value) => Number(value.backgroundReadyImageCount) >= imported.length, "creator background previews", 120_000);
  const authoring = await session.screenshot("creator-assets-assigned.png");
  await session.clickNamed("palacePublishRoomSetup", "publish room setup");
  const opened = await session.waitForProperty(root, (value) => {
    if (value.onboardingPhase === "error") throw new Error(`creator publish: ${value.onboardingError} (${value.onboardingReceipt}); ${value.invocationError}`);
    return statusValue(value.palaceState, "palace") === "open"
      && statusValue(value.palaceState, "authority") === "local-committed"
      && statusValue(value.palaceState, "entry_state") === "ready"
      && value.roomUsable === true;
  }, "creator Palace open", 300_000);
  const palaceId = statusValue(opened.palaceState, "id");
  const palaceUri = `palace://${palaceId}`;
  const catalog = String(opened.sharedStorageCatalog || "").trim();
  const peerEndpoint = String(opened.sharedStoragePeerEndpoint || "").trim();
  if (!/^[0-9a-f]{64}$/.test(palaceId) || catalog.length === 0 || peerEndpoint.length === 0) {
    throw new Error(`creator did not publish join inputs: ${JSON.stringify({ palaceId, catalogLength: catalog.length, peerEndpointLength: peerEndpoint.length })}`);
  }
  const background = await session.findOne("objectName", "palaceRoomBackground", "creator background");
  await session.waitForProperty(background, (value) => (Number(value.status) === 1 || String(value.status).toLowerCase() === "ready")
    && String(value.source).includes(activeBackgrounds.find((asset) => asset.roomId === "atrium").handle), "creator Atrium background", 120_000);
  const room = await session.screenshot("creator-palace-open.png");
  await session.clickNamed("palaceUserListToggle", "creator user list");
  await session.waitForProperty(root, (value) => value.userListOpen === true,
    "creator user list open");
  await sleep(1_000);
  await session.clickNamed("palaceRoomLockButton", "lock Atrium");
  const locked = await session.waitForProperty(root, (value) => {
    const state = String(value.roomLockState || "");
    return statusValue(state, "locked") === "1"
      && statusValue(String(value.moderationState || ""), "kind") === "room-lock";
  }, "creator room lock", 240_000);
  const lockedRoom = await session.screenshot("creator-admin-room-locked.png");
  await session.clickNamed("palaceRoomLockButton", "unlock Atrium");
  await session.waitForProperty(root, (value) =>
    statusValue(String(value.roomLockState || ""), "locked") === "0",
  "creator room unlock", 240_000);
  const unlockedRoom = await session.screenshot("creator-admin-room-unlocked.png");
  await session.waitForProperty(root, (value) =>
    Number(statusValue(String(value.palaceState || ""), "action")) >= 3,
  "creator unlock authority", 240_000);
  const prop = propId
    ? await session.waitForProperty(root, (value) =>
      String(value.availablePropId) === propId, "creator prop materialization", 180_000)
    : null;
  return { root, onboarding, authoring, room, lockedRoom, unlockedRoom, imported, activeBackgrounds, palaceId, palaceUri, catalog, peerEndpoint,
    propId, palaceState: opened.palaceState, lezState: opened.lezState,
    propState: prop?.activePropAssetState ?? null };
}

async function joinAsIndependentUser(session, creator, displayName, password, exerciseDoor = true) {
  const root = await openView(session, "Logos Palace", "palaceRoot");
  await session.waitForProperty(root, (state) => state.ready === true, "joiner Palace ready");
  const onboarding = await session.screenshot("joiner-onboarding-empty.png");
  let state = await session.waitForProperty(root, (value) => value.onboardingPhase !== "creating-identity", "joiner initial state", 180_000);
  if (state.onboardingPhase !== "details") throw new Error(`joiner unexpected onboarding phase: ${state.onboardingPhase}`);
  await session.setProperty(root, "onboardingPassword", password);
  await session.setProperty(root, "onboardingDisplayName", displayName);
  await session.setProperty(root, "onboardingPalaceAddress", creator.palaceUri);
  await session.setProperty(root, "onboardingStorageCatalog", creator.catalog);
  await session.setProperty(root, "onboardingStoragePeerEndpoint", creator.peerEndpoint);
  const entered = await session.screenshot("joiner-onboarding-handoff.png");
  await session.clickNamed("palaceOnboardingOpenButton", "join existing Palace");
  const opened = await session.waitForProperty(root, (value) => {
    if (value.onboardingPhase === "error") {
      throw new Error(`joiner onboarding: ${value.onboardingError} (${value.onboardingReceipt}); ${value.invocationError}; ${value.palaceState}`);
    }
    return statusValue(value.palaceState, "palace") === "open"
      && statusValue(value.palaceState, "authority") === "local-committed"
      && statusValue(value.palaceState, "entry_state") === "ready"
      && value.roomUsable === true;
  }, "joiner Palace open", 360_000);
  if (String(opened.roomTitle) !== "Atrium") throw new Error(`joiner entered ${opened.roomTitle}, expected Atrium`);
  const background = await session.findOne("objectName", "palaceRoomBackground", "joiner Atrium background");
  await session.waitForProperty(background, (value) => (Number(value.status) === 1 || String(value.status).toLowerCase() === "ready")
    && String(value.source).includes(creator.activeBackgrounds.find((asset) => asset.roomId === "atrium").handle), "joiner Atrium background", 180_000);
  const catalogStatus = String(opened.onboardingBundleStatus || "");
  if (!/(state=verified|state=retained)/.test(catalogStatus)) {
    throw new Error(`joiner catalog was not verified/retained: ${catalogStatus}`);
  }
  const atrium = await session.screenshot("joiner-atrium-open.png");
  let lounge = null;
  let loungeState = null;
  if (exerciseDoor) {
    await session.clickNamed("palaceRoomDoor", "joiner Atrium door");
    loungeState = await session.waitForProperty(root, (value) => {
      if (isRejected(value.invocationError)) throw new Error(`joiner door: ${value.invocationError}`);
      return String(value.roomTitle) === "Lounge"
        && statusValue(value.spotState, "vm") === "promoted"
        && statusValue(value.spotState, "navigation") === "1";
    }, "joiner Lounge door", 300_000);
    const loungeBackground = await session.findOne("objectName", "palaceRoomBackground", "joiner Lounge background");
    await session.waitForProperty(loungeBackground, (value) => (Number(value.status) === 1 || String(value.status).toLowerCase() === "ready")
      && String(value.source).includes(creator.activeBackgrounds.find((asset) => asset.roomId === "lounge").handle), "joiner Lounge background", 180_000);
    lounge = await session.screenshot("joiner-lounge-open.png");
    await session.callRoot(root, "gate1EnterRoom", ["atrium"]);
    await session.waitForProperty(root, (value) => String(value.roomTitle) === "Atrium",
      `${displayName} return to Atrium`, 120_000);
  }
  if (creator.propId) {
    await session.waitForProperty(root, (value) => String(value.availablePropId) === creator.propId,
      `${displayName} prop materialization`, 180_000);
  }
  const current = await session.properties(root);
  return { root, onboarding, entered, atrium, lounge, palaceState: loungeState?.palaceState || current.palaceState,
    lezState: loungeState?.lezState || current.lezState, spotState: loungeState?.spotState || current.spotState,
    catalogStatus, storageStatus: loungeState?.storageStatus || current.storageStatus };
}

const creator = new BasecampSession("creator", creatorDir, 45141);
const bob = new BasecampSession("bob", bobDir, 45142);
const carol = new BasecampSession("carol", carolDir, 45143);
const sessions = { creator, bob, carol };
await mkdir(evidenceDir, { recursive: true, mode: 0o700 });
let result;
try {
  await creator.start();
  const creatorStorage = await startStorageThroughControl(
    creator, 40181, 40182, resolve(creatorDir, "storage-data"));
  const creatorPalace = await createCreatorPalace(creator);
  const creatorDelivery = await startDelivery(
    creator, creatorPalace.root, "creator", 40381, [], false);
  const entryNode = loopbackEntryNode(creatorDelivery.evidence, 40381);

  await bob.start();
  const bobStorage = await startStorageThroughControl(
    bob, 40281, 40282, resolve(bobDir, "storage-data"),
    storageBootstrapSpr(creatorPalace.peerEndpoint));
  const bobPalace = await joinAsIndependentUser(
    bob, creatorPalace, "Bob Moderator", "bob-e2e-password", false);
  const bobRetention = await verifyStorageRetention(
    bob,
    bobPalace.root,
    "Bob initial Storage",
  );

  await carol.start();
  const carolStorage = await startStorageThroughControl(
    carol, 40481, 40482, resolve(carolDir, "storage-data"),
    storageBootstrapSpr(creatorPalace.peerEndpoint));
  const carolPalace = await joinAsIndependentUser(
    carol, creatorPalace, "Carol Visitor", "carol-e2e-password", false);
  const carolRetention = await verifyStorageRetention(
    carol,
    carolPalace.root,
    "Carol initial Storage",
  );

  const bobDelivery = await startDelivery(
    bob, bobPalace.root, "bob", 40581, [entryNode]);
  const carolDelivery = await startDelivery(
    carol, carolPalace.root, "carol", 40681, [entryNode]);
  await creator.waitForProperty(creatorPalace.root, (value) =>
    statusValue(value.deliveryStatus, "state") === "online",
  "creator Delivery online after peers", 180_000);
  const identities = {
    creator: statusValue((await creator.properties(creatorPalace.root)).identityState, "identity"),
    bob: statusValue((await bob.properties(bobPalace.root)).identityState, "identity"),
    carol: statusValue((await carol.properties(carolPalace.root)).identityState, "identity"),
  };
  if (!Object.values(identities).every((value) => /^[0-9a-f]{64}$/.test(value))) {
    throw new Error(`invalid compiled identities: ${JSON.stringify(identities)}`);
  }
  await Promise.all([
    refreshPresence(creator, creatorPalace.root, "creator"),
    refreshPresence(bob, bobPalace.root, "bob"),
    refreshPresence(carol, carolPalace.root, "carol"),
  ]);
  await waitForParticipants(
    sessions,
    { creator: creatorPalace.root, bob: bobPalace.root, carol: carolPalace.root },
    Object.values(identities),
    "three-user presence",
  );
  const move = await creator.callRoot(creatorPalace.root, "moveAvatar", [2400, 3600]);
  const speech = await bob.callRoot(bobPalace.root, "sendSpeech", ["Hello from the Palace"]);
  const wear = creatorPalace.propId
    ? await carol.callRoot(carolPalace.root, "wearProp", [creatorPalace.propId])
    : null;
  if ([move, speech, wear].filter((value) => value !== null).some(isRejected)) {
    throw new Error(`live Delivery action rejected: ${JSON.stringify({ move, speech, wear })}`);
  }
  await waitForParticipants(
    sessions,
    { creator: creatorPalace.root, bob: bobPalace.root, carol: carolPalace.root },
    Object.values(identities),
    "three-user live projection",
  );
  const liveProjection = {};
  for (const [label, session] of Object.entries(sessions)) {
    const root = { creator: creatorPalace.root, bob: bobPalace.root, carol: carolPalace.root }[label];
    await session.waitForProperty(root, (value) => {
      const participants = parseParticipants(value.participantProjection, label);
      const byId = Object.fromEntries(participants.map((entry) => [entry.userId, entry]));
      return Number(byId[identities.creator]?.x) === 2400
        && Number(byId[identities.creator]?.y) === 3600
        && byId[identities.bob]?.speech === "Hello from the Palace"
        && (!creatorPalace.propId
          || (Array.isArray(byId[identities.carol]?.props)
            && byId[identities.carol].props.includes(creatorPalace.propId)));
    }, `three-user motion/speech/prop projection (${label})`, 180_000);
    liveProjection[label] = await session.properties(root);
  }
  const liveCreator = await creator.screenshot("creator-three-user-live.png");
  const liveBob = await bob.screenshot("bob-three-user-live.png");
  timingSamples.frameTiming = await creator.frameTiming(creatorPalace.root);
  timingSamples.applicationRoundTrip = await measureApplicationRoundTrip(
    creator,
    creatorPalace.root,
  );

  const orderedMessaging = await sendOrderedSpeech(
    sessions,
    { creator: creatorPalace.root, bob: bobPalace.root, carol: carolPalace.root },
    identities,
  );

  const bobDoorPreview = await invokeWatchedReceipt(
    bob,
    bobPalace.root,
    "gate5PreviewDoor",
    [],
    "Bob door preview",
  );
  const carolDoorPreview = await invokeWatchedReceipt(
    carol,
    carolPalace.root,
    "gate5PreviewDoor",
    [],
    "Carol door preview",
  );
  if (bobDoorPreview !== carolDoorPreview) {
    throw new Error(`two-client door preview mismatch: ${bobDoorPreview} != ${carolDoorPreview}`);
  }
  const bobDoorUse = await bob.callRoot(bobPalace.root, "gate5UseDoor", []);
  if (isRejected(bobDoorUse)) throw new Error(`Bob door use rejected: ${bobDoorUse}`);
  const bobLoungeState = await bob.waitForProperty(bobPalace.root, (value) => {
    if (isRejected(value.invocationError)) throw new Error(`Bob door: ${value.invocationError}`);
    return String(value.roomTitle) === "Lounge"
      && statusValue(value.spotState, "vm") === "promoted"
      && statusValue(value.spotState, "navigation") === "1";
  }, "Bob finalized Lounge door", 300_000);
  const bobLounge = await bob.screenshot("bob-lounge-door-finalized.png");
  const doorActionId = statusValue(bobLoungeState.spotState, "action");
  if (!/^[1-9][0-9]*$/.test(doorActionId)) {
    throw new Error(`Bob finalized door action is invalid: ${bobLoungeState.spotState}`);
  }
  await measureVmTurn(bob, bobPalace.root, doorActionId, "provisional");
  await measureVmTurn(bob, bobPalace.root, doorActionId, "finalized");
  await bob.callRoot(bobPalace.root, "gate1EnterRoom", ["atrium"]);
  await bob.waitForProperty(bobPalace.root, (value) => String(value.roomTitle) === "Atrium",
    "Bob returns to Atrium", 120_000);
  const door = {
    preview: bobDoorPreview,
    previewMatchesAcrossClients: true,
    use: bobDoorUse,
    finalizedState: bobLoungeState.spotState,
    screenshot: bobLounge,
  };

  const beforeDelegate = await creator.properties(creatorPalace.root);
  const delegateAction = Number(statusValue(beforeDelegate.palaceState, "action")) + 1;
  const delegate = await creator.callRoot(
    creatorPalace.root,
    "delegateModerator",
    [identities.bob],
  );
  if (isRejected(delegate)) throw new Error(`moderator delegation rejected: ${delegate}`);
  await waitForLocalAuthorityAction(
    creator,
    creatorPalace.root,
    delegateAction,
    "creator moderator delegation",
  );
  await bob.waitForProperty(bobPalace.root, (value) =>
    statusValue(value.moderationCapabilityState, "can_ban_user") === "1"
      && Number(statusValue(value.moderationCapabilityState, "checkpoint")) >= delegateAction,
  "Bob moderator capability", 300_000);

  const beforeBanUser = await bob.properties(bobPalace.root);
  const banUserAction = Number(statusValue(beforeBanUser.palaceState, "action")) + 1;
  const banUser = await bob.callRoot(
    bobPalace.root,
    "gate4BanUser",
    [identities.carol],
  );
  if (isRejected(banUser)) throw new Error(`user ban rejected: ${banUser}`);
  await Promise.all([
    waitForLocalAuthorityAction(bob, bobPalace.root, banUserAction, "Bob user ban"),
    creator.waitForProperty(creatorPalace.root, (value) =>
      Number(statusValue(value.palaceState, "action")) >= banUserAction
        && Number(statusValue(value.moderationCapabilityState, "checkpoint")) >= banUserAction,
    "creator user-ban authority", 300_000),
    carol.waitForProperty(carolPalace.root, (value) =>
      Number(statusValue(value.palaceState, "action")) >= banUserAction,
    "Carol user-ban authority", 300_000),
  ]);
  await carol.callRoot(
    carolPalace.root,
    "sendSpeech",
    ["Banned user raw Delivery message"],
  );
  const rawBannedSpeech = await carol.waitForProperty(
    carolPalace.root,
    (value) => String(value.invocationError || "").includes("sender-banned"),
    "Carol rejects banned user's valid Delivery traffic",
    180_000,
  );
  const rawDeliveryRejected = true;
  await Promise.all([
    creator.waitForProperty(creatorPalace.root, (value) =>
      participantById(value.participantProjection, identities.carol, "creator") === null,
    "creator removes banned user from projection", 180_000),
    bob.waitForProperty(bobPalace.root, (value) =>
      participantById(value.participantProjection, identities.carol, "bob") === null,
    "Bob removes banned user from projection", 180_000),
  ]);
  let banProp = null;
  let banPropAction = null;
  if (creatorPalace.propId) {
    const beforeBanProp = await bob.properties(bobPalace.root);
    banPropAction = Number(statusValue(beforeBanProp.palaceState, "action")) + 1;
    banProp = await bob.callRoot(
      bobPalace.root,
      "gate4BanProp",
      [creatorPalace.propId],
    );
    if (isRejected(banProp)) throw new Error(`prop ban rejected: ${banProp}`);
    await Promise.all([
      waitForLocalAuthorityAction(bob, bobPalace.root, banPropAction, "Bob prop ban"),
      creator.waitForProperty(creatorPalace.root, (value) =>
        Number(statusValue(value.palaceState, "action")) >= banPropAction
          && JSON.parse(String(value.activePropAssetState || "{}")).available === false,
      "creator removes banned prop", 300_000),
      carol.waitForProperty(carolPalace.root, (value) =>
        Number(statusValue(value.palaceState, "action")) >= banPropAction
          && JSON.parse(String(value.activePropAssetState || "{}")).available === false,
      "Carol removes banned prop", 300_000),
    ]);
  }
  const moderation = {
    delegate,
    delegateAction,
    banUser,
    banUserAction,
    rawBannedSpeech: rawBannedSpeech.invocationError,
    rawDeliveryRejected,
    banProp,
    banPropAction,
    creator: await creator.properties(creatorPalace.root),
    bob: await bob.properties(bobPalace.root),
    carol: await carol.properties(carolPalace.root),
  };
  const moderationScreenshot = await creator.screenshot("creator-moderation-banned.png");
  const remove = creatorPalace.propId
    ? await carol.callRoot(carolPalace.root, "removeProp", [creatorPalace.propId])
    : null;
  if (isRejected(remove)) throw new Error(`prop removal rejected: ${remove}`);

  const palaceId = statusValue(moderation.bob.palaceState, "id");
  if (!/^[0-9a-f]{64}$/.test(palaceId)) throw new Error(`invalid recovery Palace id: ${palaceId}`);
  await creator.stop();
  const bobRecoveryRoot = await restartExistingClient(
    bob,
    "bob-e2e-password",
    palaceId,
    40281,
    40282,
    resolve(bobDir, "storage-data"),
    creatorPalace.catalog,
    40581,
    [],
    false,
  );
  const bobRecoveryEvidence = await bob.screenshot("bob-provider-offline-restarted.png");
  const bobRecoveryEntry = loopbackEntryNode(
    JSON.parse(String((await bob.properties(bobRecoveryRoot)).deliveryNodeEvidence)),
    40581,
  );
  const carolRecoveryRoot = await restartExistingClient(
    carol,
    "carol-e2e-password",
    palaceId,
    40481,
    40482,
    resolve(carolDir, "storage-data"),
    creatorPalace.catalog,
    40681,
    [bobRecoveryEntry],
  );
  const carolRecoveryEvidence = await carol.screenshot("carol-provider-offline-restarted.png");
  await Promise.all([
    refreshPresence(bob, bobRecoveryRoot, "restarted Bob"),
    refreshPresence(carol, carolRecoveryRoot, "restarted Carol"),
  ]);
  await bob.waitForProperty(bobRecoveryRoot, (value) =>
    statusValue(value.storageStatus, "catalog") === "verified"
      && statusValue(value.deliveryStatus, "state") === "online",
  "restarted Bob retained catalog", 180_000);
  await carol.waitForProperty(carolRecoveryRoot, (value) =>
    statusValue(value.storageStatus, "catalog") === "verified"
      && statusValue(value.deliveryStatus, "state") === "online",
  "restarted Carol retained catalog", 180_000);
  const bobRecoveryRetention = await verifyStorageRetention(
    bob,
    bobRecoveryRoot,
    "restarted Bob Storage",
  );
  const missingStorageObject = await proveMissingStorageObject(
    bob,
    bobRecoveryRoot,
    creatorPalace.catalog,
  );
  const carolRecoveryRetention = await verifyStorageRetention(
    carol,
    carolRecoveryRoot,
    "restarted Carol Storage",
  );
  const bobRecoveryBeforeSpeech = await bob.properties(bobRecoveryRoot);
  const bobRecoveryAcceptedBefore = statusNumber(
    bobRecoveryBeforeSpeech.deliveryStatus,
    "received_accepted",
  );
  const recoveredSpeech = await invokeWatchedReceipt(
    bob,
    bobRecoveryRoot,
    "sendSpeech",
    ["Provider A is offline; Bob recovered"],
    "restarted Bob speech",
  );
  await bob.waitForProperty(bobRecoveryRoot, (value) =>
    statusNumber(value.deliveryStatus, "received_accepted") > bobRecoveryAcceptedBefore,
  "restarted Bob accepts local speech", 180_000);
  const bannedRecoverySend = await carol.callRoot(
    carolRecoveryRoot,
    "sendSpeech",
    ["Banned Carol post-restart Delivery message"],
  );
  if (isRejected(bannedRecoverySend)) {
    throw new Error(`restarted banned Carol dispatch failed: ${bannedRecoverySend}`);
  }
  const carolBannedRecovery = await carol.waitForProperty(
    carolRecoveryRoot,
    (value) => String(value.invocationError || "").includes("sender-banned"),
    "restarted banned Carol rejects fresh Delivery traffic",
    180_000,
  );
  const recovery = {
    providerAOffline: true,
    palaceId,
    missingStorageObject,
    bob: { screenshot: bobRecoveryEvidence, recoveredSpeech, retention: bobRecoveryRetention, state: await bob.properties(bobRecoveryRoot) },
    carol: { screenshot: carolRecoveryEvidence, bannedEnforced: true, bannedRecoverySend, retention: carolRecoveryRetention, state: carolBannedRecovery },
  };
  result = {
    schema: "logos-palace-local-mvp-provider-live-user-flow-v3",
    identities,
    delivery: { creator: creatorDelivery, bob: bobDelivery, carol: carolDelivery, entryNode },
    live: { move, speech, wear, remove, creator: liveCreator, bob: liveBob, projection: liveProjection },
    orderedMessaging,
    timings: storyTimings(orderedMessaging),
    door,
    moderation: { ...moderation, screenshot: moderationScreenshot },
    recovery,
    creator: { storage: creatorStorage, palace: creatorPalace },
    bob: { storage: bobStorage, retention: bobRetention, palace: bobPalace },
    carol: { storage: carolStorage, retention: carolRetention, palace: carolPalace },
  };
  await writeFile(resolve(evidenceDir, "provider-join-result.json"), `${JSON.stringify(result, null, 2)}\n`, { mode: 0o600 });
  process.stdout.write(`${JSON.stringify(result)}\n`);
} catch (error) {
  const diagnostics = [
    String(error.stack ?? error),
    "", "CREATOR STDERR", Buffer.concat(creator.stderr).toString("utf8"),
    "", "BOB STDERR", Buffer.concat(bob.stderr).toString("utf8"),
    "", "CAROL STDERR", Buffer.concat(carol.stderr).toString("utf8"),
  ].join("\n");
  await writeFile(resolve(evidenceDir, "provider-join-failure.log"), diagnostics, { mode: 0o600 });
  throw error;
} finally {
  await carol.stop();
  await bob.stop();
  await creator.stop();
}
