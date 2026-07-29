#!/usr/bin/env node

import { createHash } from "node:crypto";
import {
  createReadStream,
  createWriteStream,
} from "node:fs";
import {
  mkdir,
  readdir,
  readFile,
  realpath,
  stat,
  writeFile,
} from "node:fs/promises";
import { spawn } from "node:child_process";
import net from "node:net";
import { basename, join, resolve } from "node:path";
import { createInterface } from "node:readline";
import {
  acceptBasecampPidHandoff,
  stopKnownWorkers,
} from "./basecamp_terminal_cleanup.mjs";
import {
  claimBoundProcesses,
  discoverOwnedBasecampProcesses,
} from "./basecamp_owned_processes.mjs";
import {
  captureDirectChildIdentity,
  signalDirectChild,
  waitForDirectChildExit,
} from "./basecamp_direct_child.mjs";
import {
  validateGate2SettlementBoundary,
  validateGate2SpeechSettlement,
} from "./basecamp_gate2_settlement.mjs";

const [
  basecampArgument,
  usersDirArgument,
  artifactsArgument,
  lgxDirArgument,
  productionLgxDirArgument,
  acceptanceLgxDirArgument,
  workerArgument,
] = process.argv.slice(2);
if (
  !basecampArgument ||
  !usersDirArgument ||
  !artifactsArgument ||
  !lgxDirArgument ||
  !productionLgxDirArgument ||
  !acceptanceLgxDirArgument ||
  !workerArgument
) {
  throw new Error(
    "usage: node tests/basecamp_gate2.mjs <Basecamp> <users-dir> <artifacts-dir> <lgx-dir> <production-lgx-dir> <acceptance-lgx-dir> <worker>",
  );
}

const qtMcpRoot = process.env.LOGOS_QT_MCP;
if (!qtMcpRoot) {
  throw new Error("LOGOS_QT_MCP must point to the pinned logos-qt-mcp output");
}

const basecamp = resolve(basecampArgument);
const usersDir = resolve(usersDirArgument);
const artifactsDir = resolve(artifactsArgument);
const lgxDir = resolve(lgxDirArgument);
const productionLgxDir = resolve(productionLgxDirArgument);
const acceptanceLgxDir = resolve(acceptanceLgxDirArgument);
const workerProgram = resolve(workerArgument);
await mkdir(artifactsDir, { recursive: true });

const labels = ["a", "b", "c"];
const acceptanceLabel = "acceptance";
const profiles = {
  a: { userId: "alice", displayName: "Alice" },
  b: { userId: "bob", displayName: "Bob" },
  c: { userId: "carol", displayName: "Carol" },
};
const nodeKeys = {
  a: "1111111111111111111111111111111111111111111111111111111111111111",
  b: "2222222222222222222222222222222222222222222222222222222222222222",
  c: "3333333333333333333333333333333333333333333333333333333333333333",
};
const clusterId = 4242;
const speechCount = 300;

const sleep = (milliseconds) =>
  new Promise((resolveSleep) => setTimeout(resolveSleep, milliseconds));

function childStdioWithoutReleaseLock(baseStdio) {
  if (process.env.PALACE_MVP_LOCK_FD !== undefined) {
    throw new Error("PALACE_MVP_LOCK_FD must not be inherited");
  }
  return baseStdio;
}

function stableJson(value) {
  if (Array.isArray(value)) {
    return value.map(stableJson);
  }
  if (value && typeof value === "object") {
    return Object.fromEntries(
      Object.keys(value)
        .sort()
        .map((key) => [key, stableJson(value[key])]),
    );
  }
  return value;
}

function exactJsonEqual(left, right) {
  return JSON.stringify(stableJson(left)) === JSON.stringify(stableJson(right));
}

function nearestRankPercentile(values, percentile) {
  if (
    values.length === 0 ||
    !Number.isFinite(percentile) ||
    percentile <= 0 ||
    percentile > 1
  ) {
    throw new Error("nearest-rank percentile requires samples and 0 < p <= 1");
  }
  const sorted = [...values].sort((left, right) => left - right);
  return sorted[Math.ceil(percentile * sorted.length) - 1];
}

function latencySummary(values) {
  return {
    sampleCount: values.length,
    p50Ms: nearestRankPercentile(values, 0.50),
    p95Ms: nearestRankPercentile(values, 0.95),
    maxMs: Math.max(...values),
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

async function packageHashes(root) {
  const names = (await readdir(root))
    .filter((name) => name.endsWith(".lgx"))
    .sort();
  return Promise.all(
    names.map(async (name) => ({
      file: name,
      sha256: await sha256File(join(root, name)),
    })),
  );
}

async function checkPortAvailable(port) {
  await new Promise((resolveCheck, rejectCheck) => {
    const server = net.createServer();
    server.unref();
    server.once("error", rejectCheck);
    server.listen({ host: "127.0.0.1", port, exclusive: true }, () => {
      server.close(resolveCheck);
    });
  });
}

async function ephemeralPorts(count) {
  const servers = [];
  const ports = [];
  try {
    for (let index = 0; index < count; index += 1) {
      const server = net.createServer();
      server.unref();
      await new Promise((resolveListen, rejectListen) => {
        server.once("error", rejectListen);
        server.listen(
          { host: "127.0.0.1", port: 0, exclusive: true },
          resolveListen,
        );
      });
      servers.push(server);
      ports.push(server.address().port);
    }
  } finally {
    await Promise.all(
      servers.map(
        (server) =>
          new Promise((resolveClose) => server.close(resolveClose)),
      ),
    );
  }
  return ports;
}

async function configuredPorts(variable, count) {
  const raw = process.env[variable];
  if (!raw) return ephemeralPorts(count);
  const ports = raw.split(",").map((value) => Number(value.trim()));
  if (
    ports.length !== count ||
    new Set(ports).size !== count ||
    ports.some(
      (port) => !Number.isInteger(port) || port < 1024 || port > 65535,
    )
  ) {
    throw new Error(`${variable} must contain ${count} distinct TCP ports`);
  }
  await Promise.all(ports.map(checkPortAvailable));
  return ports;
}

let terminationSignal;
let terminationPromise;

function claimWorkload(candidates) {
  const byPid = new Map(candidates.map((entry) => [entry.pid, entry]));
  const excluded = new Set([process.pid]);
  let ancestor = process.ppid;
  while (Number.isSafeInteger(ancestor) && ancestor > 0) {
    excluded.add(ancestor);
    ancestor = byPid.get(ancestor)?.parentPid ?? 0;
  }
  return candidates.filter(({ pid }) => !excluded.has(pid));
}

async function cleanupClaimBoundProcesses() {
  const claimPath = process.env.PALACE_MVP_CLAIM_PATH;
  const candidates = await claimBoundProcesses({ claimPath });
  const current = candidates.find(({ pid }) => pid === process.pid);
  const workload = claimWorkload(candidates);
  if (
    current
    && workload.some(
      ({ processGroupId }) =>
        processGroupId === current.processGroupId,
    )
  ) {
    throw new Error("run-owned child remained in Gate 2 process group");
  }
  if (workload.length > 0) {
    throw new Error("run-owned processes survived Gate 2 cleanup");
  }
}

async function cleanupOwnedWorkerProcesses(worker) {
  const expectedUserDir = resolve(join(usersDir, worker.label));
  const failures = [];
  const attempt = async (operation) => {
    try {
      await operation();
    } catch (error) {
      failures.push(error instanceof Error ? error.message : String(error));
    }
  };
  await attempt(async () => {
    const remaining = await discoverOwnedBasecampProcesses({
      basecamp,
      userDirs: new Set([expectedUserDir]),
    });
    if (remaining.length > 0) {
      throw new Error(
        `${worker.label} retained owned Basecamp processes after cleanup`,
      );
    }
  });
  await attempt(async () => {
    const sessionIds = new Set([
      worker.child.pid,
      ...worker.basecampPids,
    ]);
    const sessionProcesses = claimWorkload(
      await claimBoundProcesses({
        claimPath: process.env.PALACE_MVP_CLAIM_PATH,
      }),
    ).filter(({ sessionId }) => sessionIds.has(sessionId));
    if (sessionProcesses.length > 0) {
      throw new Error(
        `${worker.label} retained owned session processes after cleanup`,
      );
    }
  });
  if (failures.length > 0) {
    throw new Error([...new Set(failures)].join("; "));
  }
}

class WorkerClient {
  constructor(label, inspectorPort, view) {
    this.label = label;
    this.inspectorPort = inspectorPort;
    this.nextId = 0;
    this.pending = new Map();
    this.basecampPid = undefined;
    this.basecampPids = new Set();
    this.awaitingBasecampStart = false;
    this.protocolFailure = undefined;
    this.spawnFailure = undefined;
    this.stderr = createWriteStream(
      join(artifactsDir, `worker-${label}.stderr.log`),
    );
    this.stderrFinished = new Promise((resolveFinished) => {
      this.stderr.once("close", resolveFinished);
    });
    this.child = spawn(
      process.execPath,
      [
        workerProgram,
        basecamp,
        join(usersDir, label),
        artifactsDir,
        label,
        view,
      ],
      {
        env: {
          ...process.env,
          LOGOS_QT_MCP: qtMcpRoot,
          QML_INSPECTOR_HOST: "127.0.0.1",
          QML_INSPECTOR_PORT: String(inspectorPort),
        },
        detached: true,
        stdio: childStdioWithoutReleaseLock(["pipe", "pipe", "pipe"]),
      },
    );
    this.childIdentity = captureDirectChildIdentity(this.child);
    this.child.stderr.pipe(this.stderr);
    this.exited = new Promise((resolveExit) => {
      this.child.once("error", (error) => {
        this.spawnFailure =
          error instanceof Error ? error : new Error(String(error));
        resolveExit({ code: null, signal: null, error: this.spawnFailure });
        for (const pending of this.pending.values()) {
          clearTimeout(pending.timer);
          pending.reject(this.spawnFailure);
        }
        this.pending.clear();
      });
      this.child.once("exit", (code, signal) => {
        resolveExit({ code, signal });
        for (const pending of this.pending.values()) {
          clearTimeout(pending.timer);
          pending.reject(
            new Error(`worker ${label} exited: code=${code} signal=${signal}`),
          );
        }
        this.pending.clear();
      });
    });
    const lines = createInterface({
      input: this.child.stdout,
      crlfDelay: Infinity,
    });
    lines.on("line", (line) => {
      let message;
      try {
        message = JSON.parse(line);
      } catch {
        return;
      }
      if (message?.event === "basecamp-started") {
        try {
          const currentPid = this.awaitingBasecampStart
            ? undefined
            : this.basecampPid;
          this.basecampPid = acceptBasecampPidHandoff(
            currentPid,
            message,
          );
          this.basecampPids.add(this.basecampPid);
          this.awaitingBasecampStart = false;
        } catch (error) {
          this.protocolFailure =
            error instanceof Error ? error : new Error(String(error));
          for (const pending of this.pending.values()) {
            clearTimeout(pending.timer);
            pending.reject(this.protocolFailure);
          }
          this.pending.clear();
        }
        return;
      }
      const pending = this.pending.get(String(message.id));
      if (!pending) return;
      this.pending.delete(String(message.id));
      clearTimeout(pending.timer);
      if (message.ok) pending.resolve(message.result);
      else pending.reject(new Error(`worker ${label}: ${message.error}`));
    });
  }

  async call(command, params = {}, timeout = 120_000) {
    if (this.protocolFailure) throw this.protocolFailure;
    if (this.spawnFailure) throw this.spawnFailure;
    if (terminationSignal && command !== "shutdown") {
      throw new Error(`Gate 2 termination requested by ${terminationSignal}`);
    }
    if (this.child.exitCode !== null) {
      throw new Error(`worker ${this.label} is not running`);
    }
    const id = String(++this.nextId);
    const payload = `${JSON.stringify({ id, command, params })}\n`;
    return new Promise((resolveCall, rejectCall) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        rejectCall(
          new Error(`worker ${this.label} command timed out: ${command}`),
        );
      }, timeout);
      this.pending.set(id, { resolve: resolveCall, reject: rejectCall, timer });
      this.child.stdin.write(payload);
    });
  }

  async init() {
    this.awaitingBasecampStart = true;
    const result = await this.call("init", {}, 180_000).finally(() => {
      this.awaitingBasecampStart = false;
    });
    this.basecampPid = acceptBasecampPidHandoff(this.basecampPid, {
      event: "basecamp-started",
      basecampPid: result.basecampPid,
    });
    this.basecampPids.add(this.basecampPid);
    return result;
  }

  async restart() {
    const previousPid = this.basecampPid;
    this.awaitingBasecampStart = true;
    const result = await this.call("restart", {}, 180_000).finally(() => {
      this.awaitingBasecampStart = false;
    });
    this.basecampPid = acceptBasecampPidHandoff(this.basecampPid, {
      event: "basecamp-started",
      basecampPid: result.basecampPid,
    });
    this.basecampPids.add(this.basecampPid);
    return { ...result, previousPid };
  }

  async crashRestart() {
    const previousPid = this.basecampPid;
    this.awaitingBasecampStart = true;
    const result = await this.call("crashRestart", {}, 180_000).finally(() => {
      this.awaitingBasecampStart = false;
    });
    this.basecampPid = acceptBasecampPidHandoff(this.basecampPid, {
      event: "basecamp-started",
      basecampPid: result.basecampPid,
    });
    this.basecampPids.add(this.basecampPid);
    if (
      result.crash?.previousPid !== previousPid
      || result.crash.signal !== "SIGKILL"
      || result.crash.exitCode !== null
      || result.crash.graceful !== false
      || result.basecampPid === previousPid
    ) {
      throw new Error(
        `B controlled crash evidence is invalid: ${JSON.stringify(result)}`,
      );
    }
    return result;
  }

  async stop() {
    this.stopPromise ??= (async () => {
      if (this.child.exitCode === null) {
        try {
          await this.call("shutdown", {}, 30_000);
        } catch {
          await signalDirectChild(this.childIdentity, "SIGTERM");
        }
        this.child.stdin.end();
        await waitForDirectChildExit(
          this.exited,
          5_000,
          `Gate 2 ${this.label} worker`,
        );
      }
      if (!this.stderr.writableEnded) this.stderr.end();
      await this.stderrFinished;
      await cleanupOwnedWorkerProcesses(this);
    })();
    return this.stopPromise;
  }
}

function parseStatus(value) {
  const fields = {};
  for (const field of String(value ?? "").split(";")) {
    const separator = field.indexOf("=");
    if (separator <= 0) continue;
    fields[field.slice(0, separator)] = field.slice(separator + 1);
  }
  for (const key of [
    "outbox",
    "participants",
    "correlated",
    "received_accepted",
    "received_rejected",
    "rejected_scope",
    "rejected_expired",
    "rejected_signature",
    "rejected_replay",
    "rejected_payload",
    "rejected_other",
  ]) {
    if (fields[key] !== undefined) fields[key] = Number(fields[key]);
  }
  return fields;
}

function requestCounter(receipt) {
  const match = String(receipt).match(
    /^ok;request=live-[0-9]+-([0-9]+);delivery=/,
  );
  if (!match) {
    throw new Error(`invalid live request receipt: ${JSON.stringify(receipt)}`);
  }
  const counter = Number(match[1]);
  if (!Number.isSafeInteger(counter) || counter <= 0) {
    throw new Error(`invalid live request counter: ${match[1]}`);
  }
  return counter;
}

function parseProjection(value) {
  let parsed;
  try {
    parsed = JSON.parse(String(value ?? ""));
  } catch (error) {
    throw new Error(`invalid participant projection JSON: ${error.message}`);
  }
  if (!Array.isArray(parsed)) {
    throw new Error("participant projection is not an array");
  }
  return parsed;
}

function parseEvidence(value) {
  let parsed;
  try {
    parsed = JSON.parse(String(value ?? ""));
  } catch (error) {
    throw new Error(`invalid Delivery node evidence JSON: ${error.message}`);
  }
  if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
    throw new Error("Delivery node evidence is not an object");
  }
  return parsed;
}

function peerIdFromEvidence(evidence) {
  const value = evidence.peerId;
  if (typeof value === "string" && value.length > 20) return value;
  if (value && typeof value === "object") {
    for (const key of ["peerId", "value", "id"]) {
      if (typeof value[key] === "string" && value[key].length > 20) {
        return value[key];
      }
    }
  }
  throw new Error(`node evidence has no peer ID: ${JSON.stringify(value)}`);
}

function addressStrings(value) {
  if (typeof value === "string") {
    return value.split(",").map((entry) => entry.trim()).filter(Boolean);
  }
  if (Array.isArray(value)) {
    return value.flatMap(addressStrings);
  }
  if (value && typeof value === "object") {
    return Object.values(value).flatMap(addressStrings);
  }
  return [];
}

function loopbackEntryNode(evidence, expectedPort) {
  const peerId = peerIdFromEvidence(evidence);
  const addresses = addressStrings(evidence.multiaddresses);
  const selected = addresses.find((address) => {
    const match = address.match(
      /^\/(?:ip4|ip6)\/[^/]+\/tcp\/([0-9]+)(?:\/|$)/,
    );
    return match && Number(match[1]) === expectedPort;
  });
  if (!selected) {
    throw new Error(
      `node evidence lacks expected TCP ${expectedPort}: ${JSON.stringify(addresses)}`,
    );
  }
  return `/ip4/127.0.0.1/tcp/${expectedPort}/p2p/${peerId}`;
}

function connectedPeerIds(evidence) {
  const value = evidence.connectedPeers;
  if (value && typeof value === "object" && !Array.isArray(value)) {
    return Object.keys(value);
  }
  const serialized = JSON.stringify(value ?? "");
  return [...serialized.matchAll(/1[2-9A-HJ-NP-Za-km-z]{30,}/g)].map(
    (match) => match[0],
  );
}

async function waitFor(check, {
  timeout = 120_000,
  interval = 250,
  description = "condition",
} = {}) {
  const deadline = Date.now() + timeout;
  let lastError = new Error(`${description} not observed`);
  while (Date.now() < deadline) {
    try {
      return await check();
    } catch (error) {
      lastError = error;
      await sleep(interval);
    }
  }
  try {
    return await check();
  } catch (error) {
    throw new Error(`${description}: ${error.message || lastError.message}`);
  }
}

async function snapshot(worker) {
  const properties = await worker.call("properties");
  return {
    status: parseStatus(properties.gate2Status),
    projection: parseProjection(properties.gate2Projection),
    nodeEvidence: parseEvidence(properties.gate2NodeEvidence),
    receipt: String(properties.gate2Receipt ?? ""),
  };
}

async function acceptanceSnapshot(worker) {
  const properties = await worker.call("properties");
  return {
    status: parseStatus(properties.acceptanceStatus),
    nodeEvidence: parseEvidence(properties.acceptanceNodeEvidence),
    receipt: String(properties.acceptanceReceipt ?? ""),
  };
}

async function waitOnline(worker) {
  return waitFor(
    async () => {
      const observed = await snapshot(worker);
      if (observed.status.state !== "online") {
        throw new Error(`state=${observed.status.state ?? "missing"}`);
      }
      return observed;
    },
    {
      timeout: 120_000,
      description: `Delivery ${worker.label} Online`,
    },
  );
}

async function waitAcceptanceOnline(worker) {
  return waitFor(
    async () => {
      const observed = await acceptanceSnapshot(worker);
      if (observed.status.state !== "online") {
        throw new Error(`state=${observed.status.state ?? "missing"}`);
      }
      return observed;
    },
    {
      timeout: 120_000,
      description: "acceptance sender Online",
    },
  );
}

async function waitEvidence(worker, predicate, description) {
  return waitFor(
    async () => {
      const observed = await snapshot(worker);
      if (observed.nodeEvidence.success !== true) {
        throw new Error(
          `evidence unavailable: ${JSON.stringify(observed.nodeEvidence)}`,
        );
      }
      if (!predicate(observed.nodeEvidence)) {
        throw new Error(
          `peer evidence not converged: ${JSON.stringify(observed.nodeEvidence)}`,
        );
      }
      return observed.nodeEvidence;
    },
    { timeout: 120_000, description },
  );
}

async function waitAcceptanceEvidence(worker, predicate, description) {
  return waitFor(
    async () => {
      const observed = await acceptanceSnapshot(worker);
      if (observed.nodeEvidence.success !== true) {
        throw new Error(
          `evidence unavailable: ${JSON.stringify(observed.nodeEvidence)}`,
        );
      }
      if (!predicate(observed.nodeEvidence)) {
        throw new Error(
          `peer evidence not converged: ${JSON.stringify(observed.nodeEvidence)}`,
        );
      }
      return observed.nodeEvidence;
    },
    { timeout: 120_000, description },
  );
}

async function invoke(worker, name, args, expect, timeout = 30_000) {
  const {
    allowSameReceipt = false,
    ...receiptExpectation
  } = expect ?? {};
  return worker.call(
    "invoke",
    {
      name,
      args,
      expect: receiptExpectation,
      allowSameReceipt,
      timeout,
    },
    timeout + 10_000,
  );
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

function hexDecode(value) {
  if (!/^(?:[0-9a-f]{2})*$/i.test(value)) {
    throw new Error(`invalid canonical-state hex: ${value}`);
  }
  return Buffer.from(value, "hex").toString("utf8");
}

async function sessionEvidence(label) {
  const paths = await findNamedFiles(
    join(usersDir, label, "module_data", "palace_core"),
    "delivery-session-v1",
  );
  if (paths.length !== 1) {
    throw new Error(
      `expected one Delivery session for ${label}, got ${paths.length}`,
    );
  }
  let record;
  for (let attempt = 0; attempt < 20; attempt += 1) {
    record = await readFile(paths[0], "utf8");
    const newline = record.indexOf("\n");
    if (newline === 64) {
      const checksum = record.slice(0, newline);
      const stateValue = record.slice(newline + 1);
      const calculated = createHash("sha256").update(stateValue).digest("hex");
      if (checksum === calculated) break;
    }
    record = undefined;
    await sleep(50);
  }
  if (!record) {
    throw new Error(`Delivery session checksum did not stabilize for ${label}`);
  }
  const newline = record.indexOf("\n");
  const checksum = record.slice(0, newline);
  const stateValue = record.slice(newline + 1);
  const ingressSequences = {};
  const egressSequences = {};
  for (const line of stateValue.split("\n")) {
    const fields = line.split(";");
    if (
      (fields[0] !== "ingress" && fields[0] !== "egress") ||
      fields.length !== 3
    ) {
      continue;
    }
    const sequences =
      fields[0] === "ingress" ? ingressSequences : egressSequences;
    sequences[hexDecode(fields[1])] = Number(fields[2]);
  }
  const senderKey = `${profiles[label].userId}@${label === "a" ? 1 : label === "b" ? 2 : 3}`;
  const egressSequence = egressSequences[senderKey];
  if (!Number.isSafeInteger(egressSequence) || egressSequence <= 0) {
    throw new Error(`missing egress sequence ${senderKey} for ${label}`);
  }
  return {
    file: basename(paths[0]),
    checksum,
    sha256: createHash("sha256").update(record).digest("hex"),
    senderKey,
    egressSequence,
    ingressSequences,
  };
}

async function waitIngressSequenceSet(
  worker,
  label,
  expectedSequences,
  description,
) {
  return waitFor(
    async () => {
      await snapshot(worker);
      const evidence = await sessionEvidence(label);
      for (const [senderKey, expected] of Object.entries(expectedSequences)) {
        const actual = evidence.ingressSequences[senderKey];
        if (actual !== expected) {
          throw new Error(
            `${senderKey} ingress sequence=${actual ?? "missing"} expected=${expected}`,
          );
        }
      }
      return evidence;
    },
    { timeout: 120_000, description: `${label.toUpperCase()} ${description}` },
  );
}

function expectedUsers() {
  return labels.map((label) => profiles[label].userId).sort();
}

function assertExactProjection(projection, expectedByUser) {
  const ids = projection.map((participant) => participant.userId).sort();
  if (JSON.stringify(ids) !== JSON.stringify(expectedUsers())) {
    throw new Error(`participant IDs differ: ${JSON.stringify(ids)}`);
  }
  for (const participant of projection) {
    const expected = expectedByUser[participant.userId];
    if (!expected) {
      throw new Error(`unexpected participant: ${participant.userId}`);
    }
    if (
      participant.present !== true ||
      participant.displayName !== expected.displayName
    ) {
      throw new Error(
        `invalid presence for ${participant.userId}: ${JSON.stringify(participant)}`,
      );
    }
    if (
      expected.speech !== undefined &&
      participant.speech !== expected.speech
    ) {
      throw new Error(
        `speech mismatch for ${participant.userId}: ${JSON.stringify(participant.speech)}`,
      );
    }
    if (
      expected.x !== undefined &&
      (participant.x !== expected.x || participant.y !== expected.y)
    ) {
      throw new Error(
        `motion mismatch for ${participant.userId}: ${participant.x},${participant.y}`,
      );
    }
    if (expected.props !== undefined) {
      const props = [...(participant.props ?? [])].sort();
      if (JSON.stringify(props) !== JSON.stringify([...expected.props].sort())) {
        throw new Error(
          `props mismatch for ${participant.userId}: ${JSON.stringify(props)}`,
        );
      }
    }
  }
}

async function waitProjection(worker, expectedByUser, description) {
  return waitFor(
    async () => {
      const observed = await snapshot(worker);
      assertExactProjection(observed.projection, expectedByUser);
      return observed;
    },
    { timeout: 120_000, description: `${worker.label} ${description}` },
  );
}

const rejectionCounterNames = [
  "rejected_scope",
  "rejected_expired",
  "rejected_signature",
  "rejected_replay",
  "rejected_payload",
  "rejected_other",
];

function assertNumericCounters(status) {
  for (const name of [
    "outbox",
    "correlated",
    "received_accepted",
    "received_rejected",
    ...rejectionCounterNames,
  ]) {
    if (!Number.isSafeInteger(status[name]) || status[name] < 0) {
      throw new Error(`invalid ${name} counter: ${JSON.stringify(status[name])}`);
    }
  }
}

function assertRawCounters(
  status,
  baseline,
  acceptedDelta,
  rejectedDeltas,
) {
  assertNumericCounters(status);
  const rejectedDelta = Object.values(rejectedDeltas).reduce(
    (sum, value) => sum + value,
    0,
  );
  if (
    status.received_accepted !==
      baseline.received_accepted + acceptedDelta ||
    status.received_rejected !==
      baseline.received_rejected + rejectedDelta ||
    status.outbox !== baseline.outbox ||
    status.correlated !== baseline.correlated
  ) {
    throw new Error(
      `raw aggregate counters differ: baseline=${JSON.stringify(baseline)} `
        + `expected_accepted=${baseline.received_accepted + acceptedDelta} `
        + `expected_rejected=${baseline.received_rejected + rejectedDelta} `
        + `observed=${JSON.stringify(status)}`,
    );
  }
  for (const name of rejectionCounterNames) {
    if (status[name] !== baseline[name] + (rejectedDeltas[name] ?? 0)) {
      throw new Error(
        `raw ${name} delta differs: ${baseline[name]} -> ${status[name]}`,
      );
    }
  }
}

async function waitQuiescent(worker) {
  return waitFor(
    async () => {
      const before = await snapshot(worker);
      assertNumericCounters(before.status);
      if (before.status.outbox !== 0 || before.status.correlated !== 0) {
        throw new Error(`outbox not quiet: ${JSON.stringify(before.status)}`);
      }
      await sleep(2000);
      const after = await snapshot(worker);
      assertNumericCounters(after.status);
      const counterNames = [
        "received_accepted",
        "received_rejected",
        ...rejectionCounterNames,
      ];
      if (
        counterNames.some(
          (name) => before.status[name] !== after.status[name],
        ) ||
        JSON.stringify(before.projection) !== JSON.stringify(after.projection)
      ) {
        throw new Error("status or projection changed during quiet window");
      }
      return after;
    },
    {
      timeout: 120_000,
      interval: 250,
      description: `${worker.label} quiescent raw baseline`,
    },
  );
}

async function captureSpeechSettlementBoundary() {
  return waitFor(
    async () => {
      const firstSnapshots = Object.fromEntries(
        await Promise.all(
          labels.map(async (label) => [
            label,
            await snapshot(workers[label]),
          ]),
        ),
      );
      const firstSessions = Object.fromEntries(
        await Promise.all(
          labels.map(async (label) => [
            label,
            await sessionEvidence(label),
          ]),
        ),
      );
      const quietWindowStarted = performance.now();
      await sleep(2000);
      const secondSnapshots = Object.fromEntries(
        await Promise.all(
          labels.map(async (label) => [
            label,
            await snapshot(workers[label]),
          ]),
        ),
      );
      const secondSessions = Object.fromEntries(
        await Promise.all(
          labels.map(async (label) => [
            label,
            await sessionEvidence(label),
          ]),
        ),
      );
      const observedQuietWindowMs = Math.round(
        performance.now() - quietWindowStarted,
      );
      validateGate2SettlementBoundary({
        firstSnapshots,
        firstSessions,
        secondSnapshots,
        secondSessions,
      });
      return {
        firstSnapshots,
        firstSessions,
        secondSnapshots,
        secondSessions,
        minimumQuietWindowMs: 2000,
        observedQuietWindowMs,
      };
    },
    {
      timeout: 120_000,
      interval: 250,
      description: "stable caught-up pre-speech Delivery boundary",
    },
  );
}

async function waitRawState(
  worker,
  baseline,
  acceptedDelta,
  rejectedDeltas,
  expectedProjection,
  description,
) {
  return waitFor(
    async () => {
      const observed = await snapshot(worker);
      assertRawCounters(
        observed.status,
        baseline.status,
        acceptedDelta,
        rejectedDeltas,
      );
      if (!exactJsonEqual(observed.projection, expectedProjection)) {
        throw new Error(
          `projection differs: expected=${JSON.stringify(expectedProjection)} `
            + `observed=${JSON.stringify(observed.projection)}`,
        );
      }
      return observed;
    },
    { timeout: 120_000, description },
  );
}

async function waitIngressSequence(label, senderKey, expected, description) {
  return waitFor(
    async () => {
      const evidence = await sessionEvidence(label);
      const actual = evidence.ingressSequences[senderKey];
      if (actual !== expected) {
        throw new Error(`${senderKey} ingress sequence=${actual ?? "missing"}`);
      }
      return evidence;
    },
    { timeout: 120_000, description },
  );
}

async function assertExactUi(worker, expectedByUser) {
  const isVisible = (value) => value !== false && String(value) !== "false";
  const participants = await worker.call("find", {
    property: "objectName",
    value: "palaceParticipant",
  });
  if (participants.length !== 3) {
    throw new Error(
      `${worker.label} UI participant count=${participants.length}`,
    );
  }
  const participantIds = participants
    .map((match) => String(match.properties.participantUserId ?? ""))
    .sort();
  if (JSON.stringify(participantIds) !== JSON.stringify(expectedUsers())) {
    throw new Error(
      `${worker.label} UI participant IDs=${JSON.stringify(participantIds)}`,
    );
  }
  const participantByUser = Object.fromEntries(
    participants.map((match) => [
      String(match.properties.participantUserId ?? ""),
      match.properties,
    ]),
  );
  for (const [userId, expected] of Object.entries(expectedByUser)) {
    if (
      expected.x !== undefined &&
      (Number(participantByUser[userId]?.participantMotionX) !== expected.x ||
        Number(participantByUser[userId]?.participantMotionY) !== expected.y)
    ) {
      throw new Error(
        `${worker.label} UI motion ${userId}=${participantByUser[userId]?.participantMotionX},${participantByUser[userId]?.participantMotionY}`,
      );
    }
  }

  const bubbles = await worker.call("find", {
    property: "objectName",
    value: "palaceSpeechBubble",
  });
  const bubbleByUser = Object.fromEntries(
    bubbles
      .filter((match) => isVisible(match.properties.visible))
      .map((match) => [
        String(match.properties.participantUserId ?? ""),
        String(match.properties.text ?? ""),
      ]),
  );
  for (const [userId, expected] of Object.entries(expectedByUser)) {
    if (
      expected.speech !== undefined &&
      bubbleByUser[userId] !== expected.speech
    ) {
      throw new Error(
        `${worker.label} UI speech ${userId}=${JSON.stringify(bubbleByUser[userId])}`,
      );
    }
  }

  const wornProps = await worker.call("find", {
    property: "objectName",
    value: "palaceWornProp",
  });
  const actualProps = wornProps
    .filter((match) => isVisible(match.properties.visible))
    .map((match) => ({
      userId: String(match.properties.participantUserId ?? ""),
      propId: String(match.properties.propId ?? ""),
    }))
    .sort((left, right) =>
      `${left.userId}:${left.propId}`.localeCompare(
        `${right.userId}:${right.propId}`,
      ),
    );
  const expectedProps = Object.entries(expectedByUser)
    .flatMap(([userId, expected]) =>
      [...(expected.props ?? [])].map((propId) => ({ userId, propId })),
    )
    .sort((left, right) =>
      `${left.userId}:${left.propId}`.localeCompare(
        `${right.userId}:${right.propId}`,
      ),
    );
  if (JSON.stringify(actualProps) !== JSON.stringify(expectedProps)) {
    const participantProps = Object.fromEntries(
      Object.entries(participantByUser).map(([userId, properties]) => [
        userId,
        properties.participantProps,
      ]),
    );
    const wornPropState = wornProps.map((match) => ({
      userId: String(match.properties.participantUserId ?? ""),
      propId: String(match.properties.propId ?? ""),
      visible: match.properties.visible,
    }));
    throw new Error(
      `${worker.label} UI props=${JSON.stringify(actualProps)} `
        + `participantProps=${JSON.stringify(participantProps)} `
        + `wornPropState=${JSON.stringify(wornPropState)}`,
    );
  }
  return {
    participantIds,
    participantMotion: Object.fromEntries(
      Object.entries(participantByUser).map(([userId, properties]) => [
        userId,
        {
          x: Number(properties.participantMotionX),
          y: Number(properties.participantMotionY),
        },
      ]),
    ),
    bubbleByUser,
    wornProps: actualProps,
  };
}

async function waitExactUi(worker, expectedByUser, description) {
  return waitFor(
    () => assertExactUi(worker, expectedByUser),
    {
      timeout: 30_000,
      interval: 100,
      description: `${worker.label} ${description}`,
    },
  );
}

function makeConfig(label, tcpPort, entryNodes) {
  return JSON.stringify({
    palaceAcceptanceProfile: profiles[label].userId,
    mode: label === "a" ? "Core" : "Edge",
    relay: true,
    store: false,
    clusterId,
    numShardsInNetwork: 1,
    entryNodes,
    tcpPort,
    nat: "extip:127.0.0.1",
    listenAddress: "127.0.0.1",
    nodekey: nodeKeys[label],
    discv5Discovery: false,
    websocketSupport: false,
    quicSupport: false,
    logLevel: "WARN",
  });
}

async function logEvidence() {
  const names = (await readdir(artifactsDir))
    .filter((name) => name.endsWith(".log"))
    .sort();
  return Promise.all(
    names.map(async (name) => ({
      file: name,
      bytes: (await stat(join(artifactsDir, name))).size,
      sha256: await sha256File(join(artifactsDir, name)),
    })),
  );
}

const inspectorPorts = await configuredPorts(
  "PALACE_GATE2_INSPECTOR_PORTS",
  4,
);
let deliveryPorts;
for (let attempt = 0; attempt < 20; attempt += 1) {
  deliveryPorts = await configuredPorts("PALACE_GATE2_DELIVERY_PORTS", 4);
  const collision = deliveryPorts.some((port) => inspectorPorts.includes(port));
  if (!collision) break;
  if (process.env.PALACE_GATE2_DELIVERY_PORTS) {
    throw new Error(
      "PALACE_GATE2_INSPECTOR_PORTS and PALACE_GATE2_DELIVERY_PORTS must be disjoint",
    );
  }
  deliveryPorts = undefined;
}
if (!deliveryPorts) {
  throw new Error("could not select disjoint inspector and Delivery ports");
}
const allWorkerLabels = [...labels, acceptanceLabel];
const workers = {};
let failure;
const timings = {};
const screenshots = [];
let reportData;
let cleanup = { status: "pending", failures: [] };

function requestTermination(signal) {
  if (terminationPromise) return;
  terminationSignal = signal;
  failure ??= new Error(`Gate 2 termination requested by ${signal}`);
  terminationPromise = (async () => {
    const failures = await stopKnownWorkers(
      Object.values(workers),
      cleanupClaimBoundProcesses,
    );
    if (failures.length > 0) {
      process.stderr.write(
        `Gate 2 signal cleanup failed: ${failures.join("; ")}\n`,
      );
    }
    process.removeAllListeners("SIGHUP");
    process.removeAllListeners("SIGINT");
    process.removeAllListeners("SIGTERM");
    process.kill(process.pid, signal);
  })().catch((error) => {
    process.stderr.write(
      `Gate 2 signal cleanup crashed: ${
        error instanceof Error ? error.message : String(error)
      }\n`,
    );
    process.exitCode = 1;
  });
}

process.on("SIGHUP", () => requestTermination("SIGHUP"));
process.on("SIGINT", () => requestTermination("SIGINT"));
process.on("SIGTERM", () => requestTermination("SIGTERM"));

for (const [index, label] of allWorkerLabels.entries()) {
  try {
    workers[label] = new WorkerClient(
      label,
      inspectorPorts[index],
      label === acceptanceLabel ? "acceptance" : "palace",
    );
  } catch (error) {
    failure = error instanceof Error ? error : new Error(String(error));
    break;
  }
}

try {
  if (failure) throw failure;
  const launchStarted = performance.now();
  const launches = Object.fromEntries(
    await Promise.all(
      allWorkerLabels.map(async (label) => [
        label,
        await workers[label].init(),
      ]),
    ),
  );
  timings.basecampLaunchAllMs = Math.round(performance.now() - launchStarted);

  const negativeSeams = {};
  negativeSeams.beforeStart = await invoke(
    workers.a,
    "gate2Say",
    ["before-online"],
    { exact: "rejected=delivery-session-not-configured" },
  );

  const aConfig = makeConfig("a", deliveryPorts[0], []);
  const aStarted = performance.now();
  const aStartReceipt = await invoke(
    workers.a,
    "gate2Start",
    [aConfig],
    { prefix: "ok;state=" },
    60_000,
  );
  const aEvidenceBeforePeers = await waitEvidence(
    workers.a,
    (evidence) => {
      try {
        loopbackEntryNode(evidence, deliveryPorts[0]);
        return true;
      } catch {
        return false;
      }
    },
    "A listen evidence",
  );
  const entryNode = loopbackEntryNode(
    aEvidenceBeforePeers,
    deliveryPorts[0],
  );
  timings.deliveryAListenMs = Math.round(performance.now() - aStarted);

  const configs = {
    a: aConfig,
    b: makeConfig("b", deliveryPorts[1], [entryNode]),
    c: makeConfig("c", deliveryPorts[2], [entryNode]),
  };
  const peersStarted = performance.now();
  const [peerStartReceipts, acceptanceStartReceipt] = await Promise.all([
    Promise.all(
      ["b", "c"].map((label) =>
        invoke(
          workers[label],
          "gate2Start",
          [configs[label]],
          { prefix: "ok;state=" },
          60_000,
        ),
      ),
    ),
    invoke(
      workers.acceptance,
      "acceptanceStart",
      [entryNode, deliveryPorts[3]],
      { prefix: "ok;state=starting" },
      60_000,
    ),
  ]);
  const online = await Promise.all(labels.map((label) => waitOnline(workers[label])));
  const acceptanceOnline = await waitAcceptanceOnline(workers.acceptance);
  timings.deliveryAllOnlineMs = Math.round(performance.now() - peersStarted);

  const peerIds = Object.fromEntries(
    labels.map((label, index) => [
      label,
      peerIdFromEvidence(online[index].nodeEvidence),
    ]),
  );
  const acceptancePeerId = peerIdFromEvidence(
    acceptanceOnline.nodeEvidence,
  );
  const topologyEvidence = {
    a: await waitEvidence(
      workers.a,
      (evidence) => {
        const connected = connectedPeerIds(evidence);
        return connected.includes(peerIds.b)
          && connected.includes(peerIds.c)
          && connected.includes(acceptancePeerId);
      },
      "A connected to B, C, and acceptance sender",
    ),
    b: await waitEvidence(
      workers.b,
      (evidence) => connectedPeerIds(evidence).includes(peerIds.a),
      "B connected to A",
    ),
    c: await waitEvidence(
      workers.c,
      (evidence) => connectedPeerIds(evidence).includes(peerIds.a),
      "C connected to A",
    ),
    acceptance: await waitAcceptanceEvidence(
      workers.acceptance,
      (evidence) => connectedPeerIds(evidence).includes(peerIds.a),
      "acceptance sender connected to A",
    ),
  };

  await Promise.all(
    labels.map((label) =>
      invoke(
        workers[label],
        "gate2RefreshPresence",
        [],
        { prefix: "ok;request=" },
      ),
    ),
  );
  const presenceExpected = Object.fromEntries(
    labels.map((label) => [
      profiles[label].userId,
      { displayName: profiles[label].displayName },
    ]),
  );
  await Promise.all(
    labels.map((label) =>
      waitProjection(workers[label], presenceExpected, "presence convergence"),
    ),
  );

  const rawStarted = performance.now();
  const palaceSequenceTargets = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => {
        await snapshot(workers[label]);
        const evidence = await sessionEvidence(label);
        return [evidence.senderKey, evidence.egressSequence];
      }),
    ),
  );
  const palaceSequencesBeforeRaw = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => [
        label,
        await waitIngressSequenceSet(
          workers[label],
          label,
          palaceSequenceTargets,
          "persisted Palace ingress convergence before raw traffic",
        ),
      ]),
    ),
  );
  const rawBaselines = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => [
        label,
        await waitQuiescent(workers[label]),
      ]),
    ),
  );
  const rawProjections = Object.fromEntries(
    labels.map((label) => [label, rawBaselines[label].projection]),
  );
  const rawHelloProjections = Object.fromEntries(
    labels.map((label) => [
      label,
      [
        ...rawProjections[label],
        {
          userId: "acceptance-injector",
          displayName: "Acceptance Injector",
          present: true,
          x: null,
          y: null,
          speech: "",
          props: [],
        },
      ].sort((left, right) =>
        left.userId < right.userId
          ? -1
          : left.userId > right.userId
            ? 1
            : 0,
      ),
    ]),
  );
  const rawReceipts = {};
  const zeroRejectedDeltas = Object.fromEntries(
    rejectionCounterNames.map((name) => [name, 0]),
  );
  rawReceipts.presenceHello = await invoke(
    workers.acceptance,
    "acceptanceInject",
    ["presence-hello"],
    { prefix: "ok;scenario=presence-hello;" },
  );
  const rawHelloAccepted = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => [
        label,
        await waitRawState(
          workers[label],
          rawBaselines[label],
          1,
          zeroRejectedDeltas,
          rawHelloProjections[label],
          `${label.toUpperCase()} accepted raw PresenceHello`,
        ),
      ]),
    ),
  );
  const acceptanceSenderKey = "acceptance-injector@4";
  const rawSessionAfterHello = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => [
        label,
        await waitIngressSequence(
          label,
          acceptanceSenderKey,
          1,
          `${label.toUpperCase()} persisted raw PresenceHello sequence`,
        ),
      ]),
    ),
  );

  const invalidScenarios = [
    ["replay", "rejected_replay"],
    ["expired", "rejected_expired"],
    ["bad-signature", "rejected_signature"],
    ["wrong-epoch", "rejected_scope"],
    ["out-of-bounds-motion", "rejected_payload"],
  ];
  const rejectedDeltas = { ...zeroRejectedDeltas };
  const rawInvalidSnapshots = {};
  for (const [scenario, counter] of invalidScenarios) {
    rawReceipts[scenario] = await invoke(
      workers.acceptance,
      "acceptanceInject",
      [scenario],
      { prefix: `ok;scenario=${scenario};` },
    );
    rejectedDeltas[counter] += 1;
    rawInvalidSnapshots[scenario] = Object.fromEntries(
      await Promise.all(
        labels.map(async (label) => [
          label,
          await waitRawState(
            workers[label],
            rawBaselines[label],
            1,
            rejectedDeltas,
            rawHelloProjections[label],
            `${label.toUpperCase()} rejected raw ${scenario}`,
          ),
        ]),
      ),
    );
  }
  const rawSessionAfterInvalid = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => [
        label,
        await waitIngressSequence(
          label,
          acceptanceSenderKey,
          1,
          `${label.toUpperCase()} retained raw PresenceHello sequence after invalid inputs`,
        ),
      ]),
    ),
  );

  rawReceipts.presenceByeCleanup = await invoke(
    workers.acceptance,
    "acceptanceInject",
    ["presence-bye-cleanup"],
    { prefix: "ok;scenario=presence-bye-cleanup;" },
  );
  const rawCleanupAccepted = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => [
        label,
        await waitRawState(
          workers[label],
          rawBaselines[label],
          2,
          rejectedDeltas,
          rawProjections[label],
          `${label.toUpperCase()} accepted raw PresenceBye cleanup`,
        ),
      ]),
    ),
  );
  const rawSessionAfterCleanup = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => [
        label,
        await waitIngressSequence(
          label,
          acceptanceSenderKey,
          2,
          `${label.toUpperCase()} persisted raw PresenceBye cleanup sequence`,
        ),
      ]),
    ),
  );
  const acceptanceScreenshot = await workers.acceptance.call(
    "screenshot",
    { name: "gate2-acceptance-sender.png" },
  );
  screenshots.push(acceptanceScreenshot);
  const settledAfterRaw = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => [
        label,
        await waitQuiescent(workers[label]),
      ]),
    ),
  );
  timings.rawAcceptanceMs = Math.round(performance.now() - rawStarted);

  negativeSeams.emptySpeech = await invoke(
    workers.b,
    "gate2Say",
    [""],
    { exact: "rejected=delivery-publish;preflight=invalid-or-banned-payload" },
  );
  negativeSeams.outOfBoundsMotion = await invoke(
    workers.b,
    "gate2Move",
    [-1, 20],
    {
      exact: "rejected=delivery-publish;preflight=invalid-or-banned-payload",
      allowSameReceipt: true,
    },
  );
  negativeSeams.unapprovedProp = await invoke(
    workers.c,
    "gate2Wear",
    ["forbidden"],
    { exact: "rejected=delivery-publish;preflight=invalid-or-banned-payload" },
  );
  negativeSeams.invalidRemove = await invoke(
    workers.a,
    "gate2Remove",
    ["bad prop!"],
    { exact: "rejected=prop-id-invalid" },
  );

  const speechBaseline = await captureSpeechSettlementBoundary();
  const statusBeforeSpeech = Object.fromEntries(
    labels.map((label) => [
      label,
      speechBaseline.secondSnapshots[label].status,
    ]),
  );
  const sessionBeforeSpeech = speechBaseline.secondSessions;

  const speechStarted = performance.now();
  const speechReceipts = [];
  const speechDeliveryObservations = [];
  const finalSpeech = {};
  const perSenderCount = { a: 0, b: 0, c: 0 };
  const lastSpeechRequestCounter = { a: 0, b: 0, c: 0 };
  for (let ordinal = 1; ordinal <= speechCount; ordinal += 1) {
    const label = labels[(ordinal - 1) % labels.length];
    const message =
      `gate2-${String(ordinal).padStart(3, "0")}-${profiles[label].userId}`;
    const deliveryStarted = performance.now();
    const receipt = await invoke(
      workers[label],
      "gate2Say",
      [message],
      { prefix: "ok;request=" },
    );
    const counter = requestCounter(receipt.receipt);
    if (counter <= lastSpeechRequestCounter[label]) {
      throw new Error(
        `${label} live request order regressed: ${lastSpeechRequestCounter[label]} -> ${counter}`,
      );
    }
    lastSpeechRequestCounter[label] = counter;
    speechReceipts.push({
      ordinal,
      sender: profiles[label].userId,
      message,
      receipt: receipt.receipt,
      requestCounter: counter,
    });
    finalSpeech[profiles[label].userId] = message;
    perSenderCount[label] += 1;
    const ordinalExpected = Object.fromEntries(
      labels.map((expectedLabel) => {
        const userId = profiles[expectedLabel].userId;
        const expected = {
          displayName: profiles[expectedLabel].displayName,
        };
        if (finalSpeech[userId] !== undefined) {
          expected.speech = finalSpeech[userId];
        }
        return [userId, expected];
      }),
    );
    const receivers = Object.fromEntries(
      await Promise.all(
        labels.map(async (receiverLabel) => {
          const observed = await waitProjection(
            workers[receiverLabel],
            ordinalExpected,
            `speech ordinal ${ordinal}`,
          );
          return [
            receiverLabel,
            {
              latencyMs: Math.round(performance.now() - deliveryStarted),
              receivedAccepted: observed.status.received_accepted,
            },
          ];
        }),
      ),
    );
    speechDeliveryObservations.push({
      ordinal,
      sender: profiles[label].userId,
      message,
      allNodesLatencyMs: Math.max(
        ...Object.values(receivers).map((entry) => entry.latencyMs),
      ),
      receivers,
    });
  }
  timings.orderedSpeechSendMs = Math.round(performance.now() - speechStarted);

  const speechExpected = Object.fromEntries(
    labels.map((label) => [
      profiles[label].userId,
      {
        displayName: profiles[label].displayName,
        speech: finalSpeech[profiles[label].userId],
      },
    ]),
  );
  const speechConvergenceStarted = performance.now();
  await Promise.all(
    labels.map((label) =>
      waitProjection(
        workers[label],
        speechExpected,
        "speech convergence",
      ),
    ),
  );
  const speechSettlement = await waitFor(
    async () => {
      const snapshots = Object.fromEntries(
        await Promise.all(
          labels.map(async (label) => [
            label,
            await snapshot(workers[label]),
          ]),
        ),
      );
      const sessionAfter = Object.fromEntries(
        await Promise.all(
          labels.map(async (label) => [
            label,
            await sessionEvidence(label),
          ]),
        ),
      );
      const validated = validateGate2SpeechSettlement({
        perSenderCount,
        statusBefore: statusBeforeSpeech,
        snapshots,
        sessionBefore: sessionBeforeSpeech,
        sessionAfter,
      });
      return { snapshots, sessionAfter, ...validated };
    },
    {
      timeout: 120_000,
      description: "speech, presence, counters, and persisted sequences",
    },
  );
  timings.orderedSpeechConvergenceMs = Math.round(
    performance.now() - speechConvergenceStarted,
  );

  const speechSnapshots = speechSettlement.snapshots;
  const sessionAfterSpeech = speechSettlement.sessionAfter;
  const speechSequenceEvidence = speechSettlement.sequenceEvidence;
  const speechAllNodesLatencies = speechDeliveryObservations.map(
    (observation) => observation.allNodesLatencyMs,
  );
  const speechReceiverLatency = Object.fromEntries(
    labels.map((label) => [
      label,
      latencySummary(
        speechDeliveryObservations.map(
          (observation) => observation.receivers[label].latencyMs,
        ),
      ),
    ]),
  );
  const speechDeliveryLatency = {
    clock: "performance.now monotonic milliseconds",
    startBoundary: "immediately before sender gate2Say invocation",
    receiveBoundary:
      "exact sender speech observed in every receiver projection",
    messageAggregation: "maximum of A, B, and C receiver observations",
    percentileMethod:
      "nearest-rank: sorted[Math.ceil(percentile * sampleCount) - 1]",
    allNodes: latencySummary(speechAllNodesLatencies),
    perReceiver: speechReceiverLatency,
    observations: speechDeliveryObservations,
  };

  for (const label of labels) {
    if (perSenderCount[label] < 100) {
      throw new Error(
        `${label} ordered speech count ${perSenderCount[label]} is below 100`,
      );
    }
  }

  const renderProbeMessage = "gate2-render-probe-alice";
  const renderProbeExpected = {
    ...speechExpected,
    alice: {
      ...speechExpected.alice,
      speech: renderProbeMessage,
    },
  };
  const renderProbeStarted = performance.now();
  const renderProbeReceipt = await invoke(
    workers.a,
    "gate2Say",
    [renderProbeMessage],
    { prefix: "ok;request=" },
  );
  const renderCaptures = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => {
        const exactUi = await waitExactUi(
          workers[label],
          renderProbeExpected,
          "marked render probe",
        );
        const screenshot = await workers[label].call("screenshot", {
          name: `gate2-render-probe-${label}.png`,
        });
        return [
          label,
          {
            location: label === "a" ? "local" : "remote",
            actionToFramebufferCaptureMs: Math.round(
              performance.now() - renderProbeStarted,
            ),
            exactUi,
            screenshot,
          },
        ];
      }),
    ),
  );
  screenshots.push(...labels.map((label) => renderCaptures[label].screenshot));
  const renderProbe = {
    clock: "performance.now monotonic milliseconds",
    sender: "a",
    senderUserId: profiles.a.userId,
    message: renderProbeMessage,
    receipt: renderProbeReceipt.receipt,
    startBoundary: "immediately before sender gate2Say invocation",
    endBoundary:
      "exact QML delegate state followed by completed inspector PNG capture",
    captureCostIncluded: true,
    local: renderCaptures.a,
    remote: {
      b: renderCaptures.b,
      c: renderCaptures.c,
    },
  };

  const liveActions = [];
  const actionCalls = [
    ["a", "gate2Move", [1200, 2300]],
    ["b", "gate2Move", [3400, 4500]],
    ["c", "gate2Move", [5600, 6700]],
  ];
  for (const [label, name, args] of actionCalls) {
    const accepted = await invoke(
      workers[label],
      name,
      args,
      { prefix: "ok;request=" },
    );
    liveActions.push({ label, name, args, receipt: accepted.receipt });
  }

  const actionExpected = {
    alice: {
      displayName: "Alice",
      speech: renderProbeMessage,
      x: 1200,
      y: 2300,
      props: [],
    },
    bob: {
      displayName: "Bob",
      speech: finalSpeech.bob,
      x: 3400,
      y: 4500,
      props: [],
    },
    carol: {
      displayName: "Carol",
      speech: finalSpeech.carol,
      x: 5600,
      y: 6700,
      props: [],
    },
  };
  const actionSnapshots = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => [
        label,
        await waitProjection(
          workers[label],
          actionExpected,
          "motion and prop convergence",
        ),
      ]),
    ),
  );
  const uiBeforeRestart = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => [
        label,
        await waitExactUi(
          workers[label],
          actionExpected,
          "exact UI convergence",
        ),
      ]),
    ),
  );
  for (const label of labels) {
    screenshots.push(
      await workers[label].call("screenshot", {
        name: `gate2-${label}-converged.png`,
      }),
    );
  }
  const frameTimingBeforeRestart = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => [
        label,
        await workers[label].call("frameTimings"),
      ]),
    ),
  );

  const sessionBeforeRestart = await sessionEvidence("b");
  const restartStarted = performance.now();
  const restartLaunch = await workers.b.crashRestart();
  const restartInitial = await snapshot(workers.b);
  if (
    restartInitial.status.state !== "recovering" &&
    restartInitial.status.state !== "offline"
  ) {
    throw new Error(
      `B restart did not restore recoverable session: ${JSON.stringify(restartInitial.status)}`,
    );
  }
  const restartReceipt = await invoke(
    workers.b,
    "gate2Start",
    [configs.b],
    { prefix: "ok;state=" },
    60_000,
  );
  await waitOnline(workers.b);
  await waitEvidence(
    workers.b,
    (evidence) => connectedPeerIds(evidence).includes(peerIds.a),
    "B reconnected to A",
  );
  await Promise.all(
    labels.map((label) =>
      invoke(
        workers[label],
        "gate2RefreshPresence",
        [],
        { prefix: "ok;request=" },
      ),
    ),
  );
  await Promise.all(
    labels.map((label) =>
      waitProjection(
        workers[label],
        presenceExpected,
        "post-restart presence",
      ),
    ),
  );

  const restartMessages = {
    alice: "gate2-restart-alice",
    bob: "gate2-restart-bob",
    carol: "gate2-restart-carol",
  };
  const restartSends = {};
  for (const label of labels) {
    restartSends[label] = await invoke(
      workers[label],
      "gate2Say",
      [restartMessages[profiles[label].userId]],
      { prefix: "ok;request=" },
    );
  }
  const rebuildCalls = [
    ["a", "gate2Move", [1200, 2300]],
    ["b", "gate2Move", [3400, 4500]],
    ["c", "gate2Move", [5600, 6700]],
  ];
  const rebuildReceipts = [];
  for (const [label, name, args] of rebuildCalls) {
    const rebuilt = await invoke(
      workers[label],
      name,
      args,
      { prefix: "ok;request=" },
    );
    rebuildReceipts.push({ label, name, args, receipt: rebuilt.receipt });
  }
  const postRestartExpected = {
    alice: {
      ...actionExpected.alice,
      speech: restartMessages.alice,
    },
    bob: {
      displayName: "Bob",
      speech: restartMessages.bob,
      x: 3400,
      y: 4500,
      props: [],
    },
    carol: {
      ...actionExpected.carol,
      speech: restartMessages.carol,
    },
  };
  const postRestartSnapshots = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => [
        label,
        await waitProjection(
          workers[label],
          postRestartExpected,
          "post-restart message convergence",
        ),
      ]),
    ),
  );
  const uiAfterRestart = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => [
        label,
        await waitExactUi(
          workers[label],
          postRestartExpected,
          "post-restart exact UI convergence",
        ),
      ]),
    ),
  );
  const sessionAfterRestart = await sessionEvidence("b");
  if (
    sessionAfterRestart.egressSequence <=
    sessionBeforeRestart.egressSequence
  ) {
    throw new Error(
      `B egress sequence did not advance across restart: ${sessionBeforeRestart.egressSequence} -> ${sessionAfterRestart.egressSequence}`,
    );
  }
  timings.restartRecoveryMs = Math.round(performance.now() - restartStarted);
  screenshots.push(
    await workers.b.call("screenshot", {
      name: "gate2-b-restored.png",
    }),
  );
  const frameTimingAfterRestart = {
    b: await workers.b.call("frameTimings"),
  };

  reportData = {
    propStory: "not-requested",
    launches,
    ports: {
      inspectors: Object.fromEntries(
        allWorkerLabels.map(
          (label, index) => [label, inspectorPorts[index]],
        ),
      ),
      deliveryTcp: Object.fromEntries(
        allWorkerLabels.map(
          (label, index) => [label, deliveryPorts[index]],
        ),
      ),
    },
    topology: {
      clusterId,
      entryNode,
      peerIds: {
        ...peerIds,
        acceptance: acceptancePeerId,
      },
      evidenceBeforePeers: aEvidenceBeforePeers,
      connectedEvidence: topologyEvidence,
    },
    startReceipts: {
      a: aStartReceipt,
      b: peerStartReceipts[0],
      c: peerStartReceipts[1],
      acceptance: acceptanceStartReceipt,
    },
    negativeSeams,
    negativeCoverage: {
      coreContractSeams: "executed",
      rawNetworkPackets: "executed-compiled-canned-sender",
    },
    rawAcceptance: {
      sender: "acceptance-injector",
      senderKeyEpoch: 4,
      scenarioOrder: [
        "presence-hello",
        ...invalidScenarios.map(([scenario]) => scenario),
        "presence-bye-cleanup",
      ],
      receipts: rawReceipts,
      statusBefore: Object.fromEntries(
        labels.map((label) => [label, rawBaselines[label].status]),
      ),
      projectionBefore: rawProjections,
      palaceSequenceTargets,
      palaceSequencesBeforeRaw,
      projectionAfterHello: rawHelloProjections,
      helloAccepted: rawHelloAccepted,
      invalidSnapshots: rawInvalidSnapshots,
      cleanupAccepted: rawCleanupAccepted,
      sessionAfterHello: rawSessionAfterHello,
      sessionAfterInvalid: rawSessionAfterInvalid,
      sessionAfterCleanup: rawSessionAfterCleanup,
      settledAllNodes: settledAfterRaw,
      screenshot: acceptanceScreenshot,
    },
    orderedSpeech: {
      count: speechCount,
      perSenderCount,
      lastRequestCounter: lastSpeechRequestCounter,
      receipts: speechReceipts,
      finalSpeech,
      statusBefore: statusBeforeSpeech,
      snapshots: speechSnapshots,
      sessionBefore: sessionBeforeSpeech,
      sessionAfter: sessionAfterSpeech,
      baselineStability: {
        minimumQuietWindowMs: speechBaseline.minimumQuietWindowMs,
        observedQuietWindowMs: speechBaseline.observedQuietWindowMs,
        statusProbe: Object.fromEntries(
          labels.map((label) => [
            label,
            speechBaseline.firstSnapshots[label].status,
          ]),
        ),
        sessionProbe: speechBaseline.firstSessions,
      },
      sequenceEvidence: speechSequenceEvidence,
      sendToReceiveLatency: speechDeliveryLatency,
    },
    renderProbe,
    frameTiming: {
      beforeRestart: frameTimingBeforeRestart,
      afterRestart: frameTimingAfterRestart,
    },
    liveActions,
    actionSnapshots,
    exactUiBeforeRestart: uiBeforeRestart,
    restartRecovery: {
      launch: restartLaunch,
      initialStatus: restartInitial.status,
      startReceipt: restartReceipt,
      messageReceipts: restartSends,
      rebuildReceipts,
      sessionBefore: sessionBeforeRestart,
      sessionAfter: sessionAfterRestart,
      snapshots: postRestartSnapshots,
      exactUi: uiAfterRestart,
    },
  };
} catch (error) {
  failure = error;
} finally {
  if (terminationPromise) await terminationPromise;
  const cleanupFailures = await stopKnownWorkers(
    Object.values(workers),
    cleanupClaimBoundProcesses,
  );
  cleanup = {
    status: cleanupFailures.length === 0 ? "passed" : "failed",
    failures: cleanupFailures,
  };
  if (cleanupFailures.length > 0 && !failure) {
    failure = new Error(
      `Gate 2 terminal cleanup failed: ${cleanupFailures.join("; ")}`,
    );
  }
}

const installedPackages = Object.fromEntries(
  await Promise.all(
    allWorkerLabels.map(async (label) => [
      label,
      JSON.parse(
        await readFile(
          join(artifactsDir, `installed-packages-${label}.json`),
          "utf8",
        ),
      ),
    ]),
  ),
);
const lgxPackages = await packageHashes(lgxDir);
const productionLgxPackages =
  await packageHashes(productionLgxDir);
const palaceCorePackageFile =
  "logos-palace_core-module-lib.lgx";
const installedPalaceCore = lgxPackages.find(
  ({ file }) => file === palaceCorePackageFile,
);
const productionPalaceCore = productionLgxPackages.find(
  ({ file }) => file === palaceCorePackageFile,
);
const installedNonCore = lgxPackages.filter(
  ({ file }) => file !== palaceCorePackageFile,
);
const productionNonCore = productionLgxPackages.filter(
  ({ file }) => file !== palaceCorePackageFile,
);
if (
  !installedPalaceCore
  || !productionPalaceCore
  || installedPalaceCore.sha256 === productionPalaceCore.sha256
  || JSON.stringify(installedNonCore)
    !== JSON.stringify(productionNonCore)
) {
  failure ??= new Error(
    "Gate 2 package set must replace only production Palace Core",
  );
}
const commonReport = {
  schema: "logos-palace-basecamp-gate2-report-v1",
  result: failure ? "FAIL" : "PASS",
  cleanup,
  acceptanceContract: {
    deliveryEnvelope: "PalaceDeliveryEnvelopeV1",
    deliveryNetworkId: "logos.test",
  },
  runtimeVariants: {
    palaceCore: {
      kind: "test-only-acceptance-fixtures",
      file: palaceCorePackageFile,
      runtimeOutput: "palace-core-acceptance-lgx",
      productionSha256: productionPalaceCore?.sha256 ?? "missing",
      installedSha256: installedPalaceCore?.sha256 ?? "missing",
    },
  },
  productSnapshot: process.env.PALACE_PRODUCT_SNAPSHOT ?? "unknown",
  sourceCommit: process.env.PALACE_SOURCE_COMMIT ?? "unknown",
  productSnapshotNarHash:
    process.env.PALACE_PRODUCT_SNAPSHOT_NAR_HASH ?? "unknown",
  productSnapshotNarSize: Number(
    process.env.PALACE_PRODUCT_SNAPSHOT_NAR_SIZE ?? Number.NaN,
  ),
  snapshotRunnerSha256:
    process.env.PALACE_MVP_RUNNER_SHA256 ?? "unknown",
  runtimeOutputManifestSha256:
    process.env.PALACE_RUNTIME_OUTPUT_MANIFEST_SHA256 ?? "unknown",
  basecamp: {
    binary: await realpath(basecamp),
    revision: process.env.PALACE_BASECAMP_REV ?? "unknown",
    sha256: await sha256File(basecamp),
  },
  installedPackages,
  lgxPackages,
  productionLgxPackages,
  acceptanceLgxPackages: await packageHashes(acceptanceLgxDir),
  ports: {
    inspectors: Object.fromEntries(
      allWorkerLabels.map(
        (label, index) => [label, inspectorPorts[index]],
      ),
    ),
    deliveryTcp: Object.fromEntries(
      allWorkerLabels.map(
        (label, index) => [label, deliveryPorts[index]],
      ),
    ),
  },
  timings,
  screenshots,
  logs: await logEvidence(),
};
const report = failure
  ? {
      ...commonReport,
      failure: failure instanceof Error ? failure.message : String(failure),
    }
  : {
      ...commonReport,
      ...reportData,
    };
await writeFile(
  join(artifactsDir, "gate2-report.json"),
  `${JSON.stringify(report, null, 2)}\n`,
);

if (failure) throw failure;
process.stdout.write("BASECAMP_GATE2_RESULT=PASS\n");
