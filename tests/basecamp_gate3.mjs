#!/usr/bin/env node

import { createHash } from "node:crypto";
import dgram from "node:dgram";
import {
  createReadStream,
  createWriteStream,
} from "node:fs";
import {
  lstat,
  mkdir,
  open,
  readdir,
  readFile,
  rename,
  unlink,
  writeFile,
} from "node:fs/promises";
import { spawn } from "node:child_process";
import net from "node:net";
import { basename, dirname, join, resolve } from "node:path";
import { createInterface } from "node:readline";
import {
  palaceRelease,
  runPalaceReleasePreflight,
} from "./basecamp_release_preflight.mjs";
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
  canonicalStorageCidSha256 as cidSha256,
} from "./basecamp_storage_cid.mjs";
import {
  loadGate3AssetInputs,
} from "./basecamp_gate3_asset_inputs.mjs";
import {
  currentLezStateExpectation,
  isCurrentLezState,
  lezStartupTimeoutMs,
} from "./basecamp_lez_startup.mjs";

const [
  basecampArgument,
  usersDirArgument,
  artifactsArgument,
  lgxDirArgument,
  workerArgument,
] = process.argv.slice(2);
if (
  !basecampArgument ||
  !usersDirArgument ||
  !artifactsArgument ||
  !lgxDirArgument ||
  !workerArgument
) {
  throw new Error(
    "usage: node tests/basecamp_gate3.mjs <Basecamp> <users-dir> <artifacts-dir> <lgx-dir> <worker>",
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
const workerProgram = resolve(workerArgument);
const gate3ReportPath = join(artifactsDir, "gate3-report.json");
await mkdir(artifactsDir, { recursive: true });

const labels = ["a", "b", "c"];
const displayNames = { a: "Alice", b: "Bob", c: "Carol" };
const productionIdentityMode =
  process.env.PALACE_GATE3_PRODUCTION_IDENTITIES === "1";
const assetInputs = await loadGate3AssetInputs({
  manifestPath: process.env.PALACE_E2E_ASSET_MANIFEST,
  inputRoot: process.env.PALACE_E2E_ASSET_INPUT_ROOT,
});
const assetFixtures = assetInputs.fixtures;
function graphObjectContract(propId) {
  if (
    propId !== null
    && !/^[a-z][a-z0-9_-]{0,63}$/.test(propId)
  ) {
    throw new Error("active prop ID cannot form graph object IDs");
  }
  return [
    ["background-atrium", "background_png"],
    ["background-lounge", "background_png"],
    ...(propId === null
      ? []
      : [
          [`prop-${propId}-image`, "prop_png"],
          [`prop-${propId}-metadata`, "prop_metadata"],
        ]),
    ["room-atrium-metadata", "room_metadata"],
    ["room-lounge-metadata", "room_metadata"],
    ["script-door", "script_bundle"],
    ...(propId === null
      ? []
      : [[`prop-${propId}`, "prop_manifest"]]),
    ["room-atrium", "room_manifest"],
    ["room-lounge", "room_manifest"],
    ["palace-1", "palace_manifest"],
  ];
}

function graphObjectOrder(propId) {
  return graphObjectContract(propId).map(([objectId]) => objectId);
}
const holderProfiles = {
  a: "alice",
  b: "bob",
  c: "carol",
};

const sleep = (milliseconds) =>
  new Promise((resolveSleep) => setTimeout(resolveSleep, milliseconds));

function childStdioWithoutReleaseLock(baseStdio) {
  if (process.env.PALACE_MVP_LOCK_FD !== undefined) {
    throw new Error("PALACE_MVP_LOCK_FD must not be inherited");
  }
  return baseStdio;
}

async function durableReplace(path, contents) {
  const temporary = `${path}.${process.pid}.${Date.now()}.tmp`;
  let handle;
  try {
    handle = await open(temporary, "wx", 0o600);
    await handle.writeFile(contents, "utf8");
    await handle.sync();
    await handle.close();
    handle = undefined;
    await rename(temporary, path);
    const directory = await open(dirname(path), "r");
    try {
      await directory.sync();
    } finally {
      await directory.close();
    }
  } catch (error) {
    await handle?.close().catch(() => {});
    await unlink(temporary).catch((unlinkError) => {
      if (unlinkError?.code !== "ENOENT") throw unlinkError;
    });
    throw error;
  }
}

function sha256(value) {
  return createHash("sha256").update(value).digest("hex");
}

function validStorageCid(value) {
  try {
    cidSha256(value);
    return true;
  } catch {
    return false;
  }
}

function stableId(label) {
  return sha256(`logos-palace-basecamp-gate4-v1\n${label}\n`);
}

function isHex64(value) {
  return typeof value === "string" && /^[0-9a-f]{64}$/.test(value);
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

function canonicalHashes(values) {
  if (!Array.isArray(values)) return [];
  return values
    .map(({ file, sha256: digest }) => ({
      file: String(file),
      sha256: String(digest),
    }))
    .sort((left, right) => left.file.localeCompare(right.file));
}

async function optionalJson(path) {
  let bytes;
  try {
    const metadata = await lstat(path);
    if (!metadata.isFile() || metadata.isSymbolicLink()) {
      throw new Error("prior Gate 3 report is not a regular file");
    }
    bytes = await readFile(path);
  } catch (error) {
    if (error?.code === "ENOENT") return undefined;
    throw error;
  }
  if (bytes.length === 0 || bytes.length > 16 * 1024 * 1024) {
    throw new Error("prior Gate 3 report has invalid size");
  }
  try {
    return JSON.parse(bytes.toString("utf8"));
  } catch {
    throw new Error("prior Gate 3 report is not valid JSON");
  }
}

async function ephemeralTcpPorts(count, excluded = new Set()) {
  const servers = [];
  try {
    let attempts = 0;
    while (servers.length < count && attempts < count * 20) {
      attempts += 1;
      const server = net.createServer();
      await new Promise((resolveListen, rejectListen) => {
        server.once("error", rejectListen);
        server.listen(
          { host: "127.0.0.1", port: 0, exclusive: true },
          resolveListen,
        );
      });
      if (excluded.has(server.address().port)) {
        await new Promise((resolveClose) => server.close(resolveClose));
        continue;
      }
      servers.push(server);
    }
    if (servers.length !== count) {
      throw new Error("could not allocate disjoint TCP ports");
    }
    return servers.map((server) => server.address().port);
  } finally {
    await Promise.all(
      servers.map(
        (server) =>
          new Promise((resolveClose) => server.close(resolveClose)),
      ),
    );
  }
}

async function ephemeralUdpPorts(count) {
  const sockets = [];
  try {
    for (let index = 0; index < count; index += 1) {
      const socket = dgram.createSocket("udp4");
      await new Promise((resolveBind, rejectBind) => {
        socket.once("error", rejectBind);
        socket.bind({ address: "127.0.0.1", port: 0, exclusive: true }, resolveBind);
      });
      sockets.push(socket);
    }
    return sockets.map((socket) => socket.address().port);
  } finally {
    await Promise.all(
      sockets.map(
        (socket) =>
          new Promise((resolveClose) => socket.close(resolveClose)),
      ),
    );
  }
}

function parseStorageBaseConfig() {
  const raw = process.env.PALACE_GATE3_STORAGE_CONFIG_BASE;
  if (productionIdentityMode && raw !== undefined) {
    throw new Error(
      "production Gate 3 forbids PALACE_GATE3_STORAGE_CONFIG_BASE",
    );
  }
  if (!raw) {
    return {
      "log-level": "INFO",
      "listen-ip": "0.0.0.0",
      "nat": "any",
      "network": "logos.test",
    };
  }
  if (Buffer.byteLength(raw, "utf8") > 64 * 1024) {
    throw new Error("PALACE_GATE3_STORAGE_CONFIG_BASE exceeds 64 KiB");
  }
  let parsed;
  try {
    parsed = JSON.parse(raw);
  } catch {
    throw new Error("PALACE_GATE3_STORAGE_CONFIG_BASE is not valid JSON");
  }
  if (
    !parsed ||
    Array.isArray(parsed) ||
    typeof parsed !== "object"
  ) {
    throw new Error("PALACE_GATE3_STORAGE_CONFIG_BASE must be an object");
  }
  return parsed;
}

function exactProductionStorageConfig(config) {
  return (
    config
    && !Array.isArray(config)
    && typeof config === "object"
    && Object.keys(config).sort().join(",")
      === [
        "disc-port",
        "listen-ip",
        "listen-port",
        "log-level",
        "nat",
        "network",
      ].join(",")
    && config["log-level"] === "INFO"
    && config["listen-ip"] === "0.0.0.0"
    && config.nat === "any"
    && config.network === "logos.test"
    && Number.isInteger(config["listen-port"])
    && config["listen-port"] >= 1024
    && config["listen-port"] <= 65535
    && Number.isInteger(config["disc-port"])
    && config["disc-port"] >= 1024
    && config["disc-port"] <= 65535
  );
}

function storageConfig(base, tcpPort, udpPort, label) {
  const config = {
    ...base,
    "listen-port": tcpPort,
    "disc-port": udpPort,
  };
  delete config["data-dir"];
  delete config["log-file"];
  if (!productionIdentityMode) {
    config.palaceAcceptanceHolderProfile = holderProfiles[label];
  } else {
    delete config.palaceAcceptanceHolderProfile;
  }
  return JSON.stringify(config);
}

class WorkerClient {
  constructor(label, inspectorPort) {
    this.label = label;
    this.nextId = 0;
    this.pending = new Map();
    this.basecampPid = undefined;
    this.protocolFailure = undefined;
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
      this.child.once("exit", (code, signal) => {
        resolveExit({ code, signal });
        for (const pending of this.pending.values()) {
          clearTimeout(pending.timer);
          pending.reject(
            new Error(
              `worker ${label} exited: code=${code} signal=${signal}`,
            ),
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
          this.basecampPid = acceptBasecampPidHandoff(
            this.basecampPid,
            message,
          );
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
    if (terminationSignal && command !== "shutdown") {
      throw new Error(`Gate 3 termination requested by ${terminationSignal}`);
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
    const initialized = await this.call("init", {}, 180_000);
    this.basecampPid = acceptBasecampPidHandoff(
      this.basecampPid,
      {
        event: "basecamp-started",
        basecampPid: initialized.basecampPid,
      },
    );
    return initialized;
  }

  async stop() {
    this.stopPromise ??= this.stopImpl();
    return this.stopPromise;
  }

  async stopImpl() {
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
        `Gate 3 ${this.label} worker`,
      );
    }
    if (!this.stderr.writableEnded) this.stderr.end();
    await this.stderrFinished;
    await cleanupOwnedWorkerProcesses(this);
  }
}

async function invoke(
  worker,
  name,
  args,
  expect,
  allowSameReceipt = false,
  timeout = 15_000,
) {
  const result = await worker.call(
    "invoke",
    {
      name,
      args,
      expect,
      allowSameReceipt,
      timeout,
    },
    timeout + 15_000,
  );
  const evidence = {
    receipt: String(result.receipt),
    elapsedMs: result.elapsedMs,
  };
  Object.defineProperty(evidence, "lezState", {
    value: String(result.lezState ?? ""),
    enumerable: false,
  });
  return evidence;
}

function identityFields(receipt) {
  const fields = statusFields(receipt);
  if (
    !isHex64(fields.identity)
    || !isHex64(fields.delivery_key)
    || fields.key_epoch !== "1"
  ) {
    throw new Error(`invalid production identity: ${receipt}`);
  }
  return {
    accountId: fields.identity,
    display: fields.display,
    deliveryKey: fields.delivery_key,
    keyEpoch: fields.key_epoch,
    registrationTransaction: isHex64(fields.registration_tx)
      ? fields.registration_tx
      : undefined,
  };
}

async function startProductionLez(worker) {
  const password = stableId(`wallet-password/${worker.label}`);
  const deadline = Date.now() + lezStartupTimeoutMs;
  let lastReceipt = "";
  while (Date.now() < deadline) {
    const result = await invoke(
      worker,
      "gate4StartLez",
      [password],
      currentLezStateExpectation(palaceRelease.programIdHex),
      false,
      lezStartupTimeoutMs,
    );
    lastReceipt = result.receipt;
    const { lezState, ...actionResult } = result;
    if (
      lastReceipt.startsWith("ok;")
      && isCurrentLezState(lastReceipt, palaceRelease.programIdHex)
    ) {
      return actionResult;
    }
    if (
      lastReceipt.length === 0
      && isCurrentLezState(lezState, palaceRelease.programIdHex)
    ) {
      return {
        ...actionResult,
        lezStateObservation: {
          source: "gate4LezState",
          receipt: lezState,
        },
      };
    }
    if (
      !/^rejected=lez-sync;reason=(current-height-failed|last-synced-height-failed|chunk-failed|chunk-progress-mismatch|terminal-height-mismatch)$/.test(
        lastReceipt,
      )
    ) {
      throw new Error(`production LEZ ${worker.label}: ${lastReceipt}`);
    }
    await sleep(1_000);
  }
  throw new Error(`production LEZ ${worker.label} timed out: ${lastReceipt}`);
}

async function ensureProductionIdentity(worker) {
  const refreshed = await invoke(
    worker,
    "gate4RefreshIdentity",
    [],
    undefined,
    false,
    30_000,
  );
  if (statusFields(refreshed.receipt).identity !== "none") {
    let receipt = refreshed.receipt;
    let identity = identityFields(receipt);
    if (identity.display !== displayNames[worker.label]) {
      throw new Error(`production identity display mismatch ${worker.label}`);
    }
    const refreshedFields = statusFields(receipt);
    if (
      refreshedFields.registration !== "submitted"
      || !identity.registrationTransaction
    ) {
      const ensured = await invoke(
        worker,
        "gate4CreateIdentity",
        [displayNames[worker.label]],
        { prefix: "ok;existing=1;" },
        false,
        120_000,
      );
      receipt = ensured.receipt;
      identity = identityFields(receipt);
    }
    const fields = statusFields(receipt);
    if (
      fields.registration !== "submitted"
      || !identity.registrationTransaction
    ) {
      throw new Error(
        `production identity registration ${worker.label}: ${receipt}`,
      );
    }
    return { ...identity, existing: true, receipt };
  }
  const created = await invoke(
    worker,
    "gate4CreateIdentity",
    [displayNames[worker.label]],
    { prefix: "ok;" },
    false,
    120_000,
  );
  const fields = statusFields(created.receipt);
  const identity = identityFields(created.receipt);
  if (
    fields.existing !== "0"
    || fields.registration !== "submitted"
    || !identity.registrationTransaction
  ) {
    throw new Error(
      `production identity registration ${worker.label}: ${created.receipt}`,
    );
  }
  return { ...identity, existing: false, receipt: created.receipt };
}

function failOnRejected(receipt, operation) {
  if (receipt.startsWith("rejected=")) {
    throw new Error(`${operation} rejected: ${receipt}`);
  }
}

async function pollReceipt({
  worker,
  name,
  args,
  accept,
  description,
  timeout = 180_000,
}) {
  const startedAt = performance.now();
  const deadline = Date.now() + timeout;
  let lastReceipt = "";
  while (Date.now() < deadline) {
    const result = await invoke(worker, name, args, undefined, true);
    lastReceipt = result.receipt;
    failOnRejected(lastReceipt, description);
    if (accept(lastReceipt)) {
      return {
        receipt: lastReceipt,
        elapsedMs: Math.round(performance.now() - startedAt),
      };
    }
    await sleep(250);
  }
  throw new Error(
    `${description} timed out after ${timeout} ms: ${lastReceipt}`,
  );
}

async function startStorage(worker, config) {
  const start = await invoke(
    worker,
    "gate3StartStorage",
    [config],
    { prefix: "ok;" },
  );
  const running = await pollReceipt({
    worker,
    name: "gate3StorageStatus",
    args: [],
    description: `storage start ${worker.label}`,
    accept: (receipt) =>
      receipt.includes("storage=running") &&
      receipt.includes("callback_registration=ready") &&
      receipt.includes("reconciliation_required=0"),
  });
  return { start, running };
}

function statusFields(receipt) {
  return Object.fromEntries(
    String(receipt)
      .split(";")
      .filter((field) => field.includes("="))
      .map((field) => {
        const separator = field.indexOf("=");
        return [field.slice(0, separator), field.slice(separator + 1)];
      }),
  );
}

function exactObjectKeys(value, keys) {
  return (
    value
    && typeof value === "object"
    && !Array.isArray(value)
    && Object.keys(value).sort().join(",") === [...keys].sort().join(",")
  );
}

function parseAssetAuthoringCatalog(encoded) {
  let catalog;
  try {
    catalog = JSON.parse(String(encoded));
  } catch {
    throw new Error("asset authoring catalog is not JSON");
  }
  if (
    !exactObjectKeys(
      catalog,
      [
        "version",
        "count",
        "sessionCount",
        "bundleLocked",
        "roomAssignments",
        "propAssignment",
        "assets",
      ],
    )
    || catalog.version !== 1
    || !Number.isSafeInteger(catalog.count)
    || catalog.count < 0
    || catalog.count > 1024
    || !Number.isSafeInteger(catalog.sessionCount)
    || catalog.sessionCount < 0
    || catalog.sessionCount > 4
    || typeof catalog.bundleLocked !== "boolean"
    || !exactObjectKeys(catalog.roomAssignments, ["atrium", "lounge"])
    || Object.values(catalog.roomAssignments).some(
      (handle) => handle !== "" && !isHex64(handle),
    )
    || (
      catalog.propAssignment !== null
      && (
        !exactObjectKeys(
          catalog.propAssignment,
          ["propId", "handle", "anchorX", "anchorY", "layer"],
        )
        || !/^[a-z][a-z0-9_-]{0,63}$/.test(
          catalog.propAssignment.propId,
        )
        || !isHex64(catalog.propAssignment.handle)
        || !Number.isSafeInteger(catalog.propAssignment.anchorX)
        || catalog.propAssignment.anchorX < 0
        || !Number.isSafeInteger(catalog.propAssignment.anchorY)
        || catalog.propAssignment.anchorY < 0
        || !["head", "body", "hand", "back"].includes(
          catalog.propAssignment.layer,
        )
      )
    )
    || !Array.isArray(catalog.assets)
    || catalog.assets.length !== catalog.count
  ) {
    throw new Error("asset authoring catalog envelope mismatch");
  }
  const entryKeys = [
    "handle",
    "label",
    "width",
    "height",
    "byteLength",
    "reviewState",
    "publicationState",
    "cid",
    "roles",
    "roomAssignments",
    "propAssignments",
  ];
  for (const actual of catalog.assets) {
    if (
      !exactObjectKeys(actual, entryKeys)
      || !isHex64(actual.handle)
      || typeof actual.label !== "string"
      || actual.label.length < 1
      || actual.label.length > 128
      || !Number.isSafeInteger(actual.width)
      || actual.width <= 0
      || !Number.isSafeInteger(actual.height)
      || actual.height <= 0
      || !Number.isSafeInteger(actual.byteLength)
      || actual.byteLength <= 0
      || actual.byteLength > 10 * 1024 * 1024
      || !["pending", "approved", "rejected"].includes(
        actual.reviewState,
      )
      || (
        !["not-uploaded", "publishing", "published"].includes(
          actual.publicationState,
        )
        && !actual.publicationState.startsWith("publish-failed")
      )
      || (
        actual.publicationState === "published"
          ? !validStorageCid(actual.cid)
          : actual.cid !== ""
      )
      || !Array.isArray(actual.roles)
      || actual.roles.some(
        (role) => !["room-background", "prop-image"].includes(role),
      )
      || new Set(actual.roles).size !== actual.roles.length
      || !Array.isArray(actual.roomAssignments)
      || actual.roomAssignments.some(
        (roomId) => !["atrium", "lounge"].includes(roomId),
      )
      || new Set(actual.roomAssignments).size
        !== actual.roomAssignments.length
      || !Array.isArray(actual.propAssignments)
      || actual.propAssignments.some(
        (propId) => !/^[a-z][a-z0-9_-]{0,63}$/.test(propId),
      )
      || new Set(actual.propAssignments).size
        !== actual.propAssignments.length
      || (
        actual.roomAssignments.length > 0
        && !actual.roles.includes("room-background")
      )
      || (
        actual.propAssignments.length > 0
        && !actual.roles.includes("prop-image")
      )
    ) {
      throw new Error("asset authoring catalog entry mismatch");
    }
  }
  if (
    new Set(catalog.assets.map(({ handle }) => handle)).size
      !== catalog.assets.length
  ) {
    throw new Error("asset authoring catalog is not deduplicated");
  }
  const byHandle = Object.fromEntries(
    catalog.assets.map((asset) => [asset.handle, asset]),
  );
  for (const roomId of ["atrium", "lounge"]) {
    const handle = catalog.roomAssignments[roomId];
    if (
      handle !== ""
      && (
        !byHandle[handle]
        || !byHandle[handle].roomAssignments.includes(roomId)
      )
    ) {
      throw new Error("background asset assignment is inconsistent");
    }
    if (
      catalog.assets.some(
        (asset) =>
          asset.roomAssignments.includes(roomId)
          && asset.handle !== handle,
      )
    ) {
      throw new Error("room asset reverse assignment is inconsistent");
    }
  }
  if (catalog.propAssignment !== null) {
    const asset = byHandle[catalog.propAssignment.handle];
    if (
      !asset
      || !asset.propAssignments.includes(catalog.propAssignment.propId)
    ) {
      throw new Error("prop asset assignment is inconsistent");
    }
  }
  if (
    catalog.assets.some(
      (asset) =>
        asset.propAssignments.some(
          (propId) =>
            catalog.propAssignment?.propId !== propId
            || catalog.propAssignment?.handle !== asset.handle,
        ),
    )
  ) {
    throw new Error("prop asset reverse assignment is inconsistent");
  }
  return catalog;
}

async function readAssetAuthoringCatalog(worker) {
  const properties = await worker.call("properties", {}, 30_000);
  return parseAssetAuthoringCatalog(
    properties.gate3AssetAuthoringState,
  );
}

function parseActivePropAsset(
  value,
  description,
  expectedAvailable,
) {
  let projection;
  try {
    projection = JSON.parse(String(value));
  } catch {
    throw new Error(`${description} is not JSON`);
  }
  if (!expectedAvailable) {
    if (
      !exactObjectKeys(projection, ["version", "available"])
      || projection.version !== 1
      || projection.available !== false
    ) {
      throw new Error(`${description} is invalid`);
    }
    return projection;
  }
  if (
    !exactObjectKeys(
      projection,
      [
        "version",
        "available",
        "propId",
        "handle",
        "contentSha256",
        "width",
        "height",
        "anchorX",
        "anchorY",
        "layer",
      ],
    )
    || projection.version !== 1
    || projection.available !== true
    || !/^[a-z][a-z0-9_-]{0,63}$/.test(projection.propId)
    || !/^[0-9a-f]{64}$/.test(projection.handle)
    || projection.contentSha256 !== projection.handle
    || !Number.isSafeInteger(projection.width)
    || projection.width <= 0
    || !Number.isSafeInteger(projection.height)
    || projection.height <= 0
    || !Number.isSafeInteger(projection.anchorX)
    || projection.anchorX < 0
    || !Number.isSafeInteger(projection.anchorY)
    || projection.anchorY < 0
    || !["head", "body", "hand", "back"].includes(projection.layer)
  ) {
    throw new Error(`${description} is invalid`);
  }
  return projection;
}

function validatedPriorAssetGuard(previousEvidence) {
  const guarded = previousEvidence?.guardedBeforeApproval;
  if (
    !exactObjectKeys(guarded, ["receipt", "elapsedMs"])
    || guarded.receipt !== "rejected=asset-not-approved"
    || !Number.isSafeInteger(guarded.elapsedMs)
    || guarded.elapsedMs < 0
  ) {
    throw new Error(
      "approved asset resume lacks prior guard proof",
    );
  }
  return {
    receipt: guarded.receipt,
    elapsedMs: guarded.elapsedMs,
  };
}

function matchingFixtureAsset(catalog, fixture) {
  const asset = catalog.assets.find(
    ({ handle }) => handle === fixture.handle,
  );
  if (
    asset
    && (
      asset.label !== expectedStageLabel(fixture)
      || asset.width !== fixture.width
      || asset.height !== fixture.height
      || asset.byteLength !== fixture.byteLength
    )
  ) {
    throw new Error(`asset metadata mismatch: ${fixture.assetId}`);
  }
  return asset;
}

function selectedFileName(fixture) {
  const name = basename(String(fixture.file ?? ""));
  if (!/^[a-z0-9][a-z0-9._-]{0,127}\.png$/.test(name)) {
    throw new Error(`asset input file name is invalid: ${fixture.assetId}`);
  }
  return name;
}

function expectedStageLabel(fixture) {
  const name = selectedFileName(fixture);
  return name.length > 32 ? "selected-image.png" : name;
}

function validatePriorStage(prior, fixture) {
  if (
    !prior
    || prior.assetId !== fixture.assetId
    || prior.label !== expectedStageLabel(fixture)
    || prior.file !== selectedFileName(fixture)
    || prior.handle !== fixture.handle
    || prior.width !== fixture.width
    || prior.height !== fixture.height
    || prior.byteLength !== fixture.byteLength
    || prior.role !== fixture.role
    || JSON.stringify(prior.target ?? null)
      !== JSON.stringify(fixture.assignment ?? null)
    || prior.chunkBytes !== 32 * 1024
    || !Number.isSafeInteger(prior.chunkCount)
    || prior.chunkCount <= 0
    || !Array.isArray(prior.appends)
    || prior.appends.length !== prior.chunkCount
    || prior.commit?.receipt
      !== `ok;handle=${fixture.handle};width=${fixture.width};`
        + `height=${fixture.height};bytes=${fixture.byteLength}`
  ) {
    throw new Error(`prior asset stage is invalid: ${fixture.assetId}`);
  }
  return prior;
}

function validElapsedStageEvidence(value) {
  return (
    exactObjectKeys(value, ["receipt", "elapsedMs"])
    && typeof value.receipt === "string"
    && value.receipt.length > 0
    && Number.isSafeInteger(value.elapsedMs)
    && value.elapsedMs >= 0
  );
}

function approvedAndPublishedAssetEvidence(result, fixture) {
  if (
    !exactObjectKeys(result, ["review", "publication", "cid"])
    || !validElapsedStageEvidence(result.review)
    || result.review.receipt
      !== `ok;handle=${fixture.handle};review=approved`
    || !exactObjectKeys(result.publication, ["dispatched", "completed"])
    || !validElapsedStageEvidence(result.publication.dispatched)
    || result.publication.dispatched.receipt !== "ok;asset=publishing"
    || !validElapsedStageEvidence(result.publication.completed)
    || typeof result.cid !== "string"
    || result.publication.completed.receipt !== `published;cid=${result.cid}`
    || !validStorageCid(result.cid)
  ) {
    throw new Error(`moderation publication evidence is invalid: ${fixture.assetId}`);
  }
  return result;
}

function completedPublicationEvidence(result, fixture) {
  if (
    !exactObjectKeys(result, ["cid", "completed"])
    || !validElapsedStageEvidence(result.completed)
    || typeof result.cid !== "string"
    || result.completed.receipt !== `published;cid=${result.cid}`
    || !validStorageCid(result.cid)
  ) {
    throw new Error(`moderation completion evidence is invalid: ${fixture.assetId}`);
  }
  return result;
}

function completedAssetImportStage(imported, fixture) {
  if (
    !exactObjectKeys(imported, ["assetId", "trace"])
    || imported.assetId !== fixture.assetId
    || !exactObjectKeys(imported.trace, [
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
    || imported.trace.schema !== "logos.palace.user-file-import"
    || imported.trace.version !== 1
    || !Number.isSafeInteger(imported.trace.generation)
    || imported.trace.generation <= 0
    || imported.trace.handle !== fixture.handle
    || imported.trace.width !== fixture.width
    || imported.trace.height !== fixture.height
    || imported.trace.byteLength !== fixture.byteLength
    || imported.trace.chunkBytes !== 32 * 1024
    || !Number.isSafeInteger(imported.trace.chunkCount)
    || imported.trace.chunkCount <= 0
    || !validElapsedStageEvidence(imported.trace.begin)
    || !Array.isArray(imported.trace.appends)
    || imported.trace.appends.length !== imported.trace.chunkCount
    || !validElapsedStageEvidence(imported.trace.commit)
  ) {
    throw new Error(`asset picker trace is invalid: ${fixture.assetId}`);
  }
  const beginFields = statusFields(imported.trace.begin.receipt);
  if (
    !imported.trace.begin.receipt.startsWith("ok;")
    ||
    !/^[0-9a-f]{32}$/.test(beginFields.session)
    || beginFields.next !== "0"
    || beginFields.maxChunkBytes !== String(32 * 1024)
    || beginFields.maxTotalBytes !== String(10 * 1024 * 1024)
  ) {
    throw new Error(`asset picker begin is invalid: ${fixture.assetId}`);
  }

  let totalBytes = 0;
  for (
    let sequence = 0;
    sequence < imported.trace.appends.length;
    sequence += 1
  ) {
    const append = imported.trace.appends[sequence];
    totalBytes += append?.byteLength ?? 0;
    const fields = statusFields(append?.receipt);
    if (
      !exactObjectKeys(append, [
        "sequence",
        "byteLength",
        "receipt",
        "elapsedMs",
      ])
      || append.sequence !== sequence
      || !Number.isSafeInteger(append.byteLength)
      || append.byteLength <= 0
      || append.byteLength > imported.trace.chunkBytes
      || !validElapsedStageEvidence({
        receipt: append.receipt,
        elapsedMs: append.elapsedMs,
      })
      || append.receipt
        !== `ok;session=${beginFields.session};next=${sequence + 1};`
          + `bytes=${totalBytes}`
    ) {
      throw new Error(
        `asset picker append is invalid: ${fixture.assetId}/${sequence}`,
      );
    }
  }
  if (
    totalBytes !== fixture.byteLength
    || imported.trace.commit.receipt
      !== `ok;handle=${fixture.handle};width=${fixture.width};`
        + `height=${fixture.height};bytes=${fixture.byteLength}`
  ) {
    throw new Error(`asset picker commit is invalid: ${fixture.assetId}`);
  }
  return {
    assetId: fixture.assetId,
    label: expectedStageLabel(fixture),
    file: selectedFileName(fixture),
    handle: fixture.handle,
    width: fixture.width,
    height: fixture.height,
    byteLength: fixture.byteLength,
    role: fixture.role,
    target: fixture.assignment,
    chunkBytes: imported.trace.chunkBytes,
    chunkCount: imported.trace.chunkCount,
    begin: imported.trace.begin,
    appends: imported.trace.appends,
    commit: imported.trace.commit,
  };
}

async function stageAssetFixture(worker, fixture, prior) {
  const existing = matchingFixtureAsset(
    await readAssetAuthoringCatalog(worker),
    fixture,
  );
  if (existing) return validatePriorStage(prior, fixture);

  const imported = await worker.call(
    "importSelectedAsset",
    {
      assetId: fixture.assetId,
      selectionPath: assetInputs.selectionPathFor(fixture.assetId),
      expected: {
        handle: fixture.handle,
        width: fixture.width,
        height: fixture.height,
        byteLength: fixture.byteLength,
      },
    },
    240_000,
  );
  const stage = completedAssetImportStage(imported, fixture);
  matchingFixtureAsset(
    await readAssetAuthoringCatalog(worker),
    fixture,
  );
  return stage;
}

function priorAssetEvidence(previousEvidence, fixture) {
  return previousEvidence?.assets?.find(
    ({ assetId }) => assetId === fixture.assetId,
  );
}

async function authorAssetFixtures(
  worker,
  previousEvidence,
  checkpoint,
) {
  const startedAt = performance.now();
  const evidence = {
    version: 1,
    phase: "input-validated",
    inputManifest: assetInputs.manifest,
    selectedAssetCount: assetFixtures.length,
    propStory: assetFixtures.some(({ role }) => role === "prop-image")
      ? "requested"
      : "not-requested",
    boundary:
      "operator-selected bounded PNG bytes -> verified handle -> approval"
      + " -> local-byte-verified Storage CID -> manifest assignment",
    guardedBeforeApproval: previousEvidence?.guardedBeforeApproval,
    assets: [],
    graphBindings: previousEvidence?.graphBindings ?? [],
    elapsedMs: 0,
  };
  await checkpoint(evidence);

  for (const fixture of assetFixtures) {
    const previousAsset = priorAssetEvidence(previousEvidence, fixture);
    const staged = await stageAssetFixture(
      worker,
      fixture,
      previousAsset,
    );
    evidence.assets.push({
      ...staged,
      review: previousAsset?.review,
      publication: previousAsset?.publication,
      assignment: previousAsset?.assignment,
    });
    evidence.phase = "staging";
    evidence.elapsedMs = Math.round(performance.now() - startedAt);
    await checkpoint(evidence);
  }
  evidence.phase = "staged";
  await checkpoint(evidence);

  const beforeGuard = await readAssetAuthoringCatalog(worker);
  const guardFixture = assetFixtures.find(
    (fixture) =>
      matchingFixtureAsset(beforeGuard, fixture)?.reviewState !== "approved",
  );
  evidence.guardedBeforeApproval = guardFixture
    ? await invoke(
        worker,
        "publishAsset",
        [guardFixture.handle],
        { exact: "rejected=asset-not-approved" },
      )
    : validatedPriorAssetGuard(previousEvidence);
  const afterGuard = await readAssetAuthoringCatalog(worker);
  if (JSON.stringify(afterGuard) !== JSON.stringify(beforeGuard)) {
    throw new Error("rejected asset upload mutated catalog");
  }
  evidence.phase = "approval-guarded";
  evidence.elapsedMs = Math.round(performance.now() - startedAt);
  await checkpoint(evidence);

  for (const fixture of assetFixtures) {
    const entry = matchingFixtureAsset(
      await readAssetAuthoringCatalog(worker),
      fixture,
    );
    const assetEvidence = evidence.assets.find(
      ({ assetId }) => assetId === fixture.assetId,
    );
    if (entry.publicationState === "published") {
      if (
        !assetEvidence.review
        || !assetEvidence.publication?.dispatched
        || !assetEvidence.publication?.completed
        || assetEvidence.cid !== entry.cid
        || !validStorageCid(entry.cid)
      ) {
        throw new Error(
          `published asset lacks prior moderation evidence: ${fixture.assetId}`,
        );
      }
      continue;
    }
    if (entry.reviewState === "approved" && !assetEvidence.review) {
      throw new Error(
        `approved asset lacks prior review: ${fixture.assetId}`,
      );
    }
    if (entry.publicationState === "publishing") {
      if (!assetEvidence.publication?.dispatched) {
        throw new Error(
          `publishing asset lacks prior dispatch: ${fixture.assetId}`,
        );
      }
      const completed = completedPublicationEvidence(
        await worker.call(
          "waitForPublishedAsset",
          { handle: fixture.handle },
          240_000,
        ),
        fixture,
      );
      assetEvidence.publication.completed = completed.completed;
      assetEvidence.cid = completed.cid;
      continue;
    }
    const published = approvedAndPublishedAssetEvidence(
      await worker.call(
        "approveAndPublishAsset",
        { handle: fixture.handle },
        240_000,
      ),
      fixture,
    );
    if (assetEvidence.review) {
      if (
        assetEvidence.review.receipt
          !== published.review.receipt
      ) {
        throw new Error(
          `approved asset review changed: ${fixture.assetId}`,
        );
      }
    } else {
      assetEvidence.review = published.review;
    }
    assetEvidence.publication = published.publication;
    assetEvidence.cid = published.cid;
    evidence.phase = "approved";
    evidence.elapsedMs = Math.round(performance.now() - startedAt);
    await checkpoint(evidence);
  }
  evidence.phase = "approved";
  evidence.elapsedMs = Math.round(performance.now() - startedAt);
  await checkpoint(evidence);
  evidence.phase = "published";
  evidence.elapsedMs = Math.round(performance.now() - startedAt);
  await checkpoint(evidence);

  // Final catalog validation uses last-wins room ownership. Drive the admin
  // Set Atrium/Lounge controls only for those final owners so intermediate
  // reassignment thrash is not required for a complete Gate 3 authoring pass.
  const finalRoomHandles = {};
  for (const fixture of assetFixtures) {
    if (fixture.assignment?.kind === "room-background") {
      finalRoomHandles[fixture.assignment.roomId] = fixture.handle;
    }
  }

  for (const fixture of assetFixtures.filter(
    ({ assignment }) => assignment !== undefined,
  )) {
    if (
      fixture.assignment.kind === "room-background"
      && finalRoomHandles[fixture.assignment.roomId] !== fixture.handle
    ) {
      continue;
    }
    const assetEvidence = evidence.assets.find(
      ({ assetId }) => assetId === fixture.assetId,
    );
    const current = await readAssetAuthoringCatalog(worker);
    const matches = fixture.assignment.kind === "room-background"
      ? current.roomAssignments[fixture.assignment.roomId] === fixture.handle
      : (
          current.propAssignment?.propId === fixture.assignment.propId
          && current.propAssignment?.handle === fixture.handle
          && current.propAssignment?.anchorX === fixture.assignment.anchorX
          && current.propAssignment?.anchorY === fixture.assignment.anchorY
          && current.propAssignment?.layer === fixture.assignment.layer
        );
    if (matches) {
      if (!assetEvidence.assignment) {
        throw new Error(
          `assigned asset lacks prior receipt: ${fixture.assetId}`,
        );
      }
    } else if (fixture.assignment.kind === "room-background") {
      assetEvidence.assignment = await worker.call(
        "assignRoomBackgroundFromModeration",
        {
          roomId: fixture.assignment.roomId,
          handle: fixture.handle,
        },
        180_000,
      );
    } else {
      assetEvidence.assignment = await worker.call(
        "assignPropAssetFromModeration",
        {
          handle: fixture.handle,
          propId: fixture.assignment.propId,
          anchorX: fixture.assignment.anchorX,
          anchorY: fixture.assignment.anchorY,
          layer: fixture.assignment.layer,
        },
        60_000,
      );
    }
  }
  evidence.phase = "assigned";
  evidence.elapsedMs = Math.round(performance.now() - startedAt);
  await checkpoint(evidence);

  const completed = await readAssetAuthoringCatalog(worker);
  // finalRoomHandles already computed above for the assignment pass.
  for (const fixture of assetFixtures) {
    const entry = matchingFixtureAsset(completed, fixture);
    const assignmentMatches = fixture.assignment === undefined
      || (
        fixture.assignment.kind === "room-background"
          ? (
              finalRoomHandles[fixture.assignment.roomId] === fixture.handle
                ? (
                    completed.roomAssignments[fixture.assignment.roomId]
                      === fixture.handle
                    && entry.roles.includes("room-background")
                    && entry.roomAssignments.includes(
                      fixture.assignment.roomId,
                    )
                  )
                : !entry.roomAssignments.includes(fixture.assignment.roomId)
            )
          : (
              completed.propAssignment?.propId === fixture.assignment.propId
              && completed.propAssignment?.handle === fixture.handle
              && completed.propAssignment?.anchorX
                === fixture.assignment.anchorX
              && completed.propAssignment?.anchorY
                === fixture.assignment.anchorY
              && completed.propAssignment?.layer === fixture.assignment.layer
              && entry.roles.includes("prop-image")
              && entry.propAssignments.includes(fixture.assignment.propId)
            )
      );
    if (
      entry.reviewState !== "approved"
      || entry.publicationState !== "published"
      || !validStorageCid(entry.cid)
      || !assignmentMatches
    ) {
      throw new Error(`asset authoring incomplete: ${fixture.assetId}`);
    }
  }
  evidence.phase = "complete";
  evidence.catalogCount = completed.count;
  evidence.assignments = {
    rooms: completed.roomAssignments,
    prop: completed.propAssignment,
  };
  evidence.elapsedMs = Math.round(performance.now() - startedAt);
  await checkpoint(evidence);
  return evidence;
}

function parseMvpCatalog(receipt, propId) {
  const objectContract = graphObjectContract(propId);
  const objectOrder = objectContract.map(([objectId]) => objectId);
  const encoded = statusFields(receipt).catalog;
  if (!encoded) throw new Error("MVP bundle status omitted catalog");
  const decoded = Buffer.from(encoded, "base64url");
  if (decoded.toString("base64url") !== encoded) {
    throw new Error("MVP catalog is not canonical base64url");
  }
  const catalog = decoded.toString("utf8");
  if (!catalog.endsWith("\n")) {
    throw new Error("MVP catalog lacks terminal newline");
  }
  const lines = catalog.slice(0, -1).split("\n");
  const objectCount = Number(lines[3]?.slice("objects=".length));
  if (
    lines.length !== objectOrder.length + 5 ||
    lines[0] !== "logos-palace-mvp-storage-catalog-v1" ||
    lines[1] !== "version=1" ||
    lines[2] !== "root=palace-1" ||
    !lines[3].startsWith("objects=")
    || objectCount !== objectOrder.length
  ) {
    throw new Error("MVP catalog envelope mismatch");
  }
  const checksumPrefix = "checksum=";
  const checksumLine = lines[4 + objectCount];
  if (!checksumLine.startsWith(checksumPrefix)) {
    throw new Error("MVP catalog checksum missing");
  }
  const checksumOffset = catalog.lastIndexOf(checksumPrefix);
  const expectedChecksum = createHash("sha256")
    .update(catalog.slice(0, checksumOffset))
    .digest("hex");
  if (checksumLine.slice(checksumPrefix.length) !== expectedChecksum) {
    throw new Error("MVP catalog checksum mismatch");
  }

  const objects = lines.slice(4, 4 + objectCount).map((line, index) => {
    if (!line.startsWith("object=")) {
      throw new Error(`MVP catalog object ${index} missing prefix`);
    }
    const fields = line.slice("object=".length).split(";");
    if (fields.length !== 6) {
      throw new Error(`MVP catalog object ${index} field mismatch`);
    }
    const [objectId, type, mediaType, cid, lengthText, contentSha256] =
      fields;
    const byteLength = Number(lengthText);
    if (
      objectId !== objectOrder[index] ||
      type !== objectContract[index][1] ||
      !mediaType ||
      !Number.isSafeInteger(byteLength) ||
      byteLength <= 0 ||
      byteLength > 10 * 1024 * 1024 ||
      !/^[0-9a-f]{64}$/.test(contentSha256) ||
      !validStorageCid(cid)
    ) {
      throw new Error(`MVP catalog object ${objectId} is not exact`);
    }
    return {
      objectId,
      type,
      mediaType,
      cid,
      byteLength,
      contentSha256,
    };
  });
  if (new Set(objects.map(({ cid }) => cid)).size !== objects.length) {
    throw new Error("MVP catalog contains duplicate CIDs");
  }
  return { encoded, catalog, checksum: expectedChecksum, objects };
}

async function publishBundle(worker, propId) {
  const expectedObjectCount = graphObjectOrder(propId).length;
  // Bundle dispatch may stage multiple leaves and complete local publication
  // verification before returning ok;…. Keep this under the ordinary worker
  // cap (120s) but well above the default 15s invoke budget used for lighter
  // Gate 3 actions.
  const dispatched = await invoke(
    worker,
    "gate3PublishBundle",
    [],
    { prefix: "ok;" },
    false,
    120_000,
  );
  const completed = await pollReceipt({
    worker,
    name: "gate3BundleStatus",
    args: [],
    description: "publish exact MVP storage bundle",
    accept: (receipt) => {
      const fields = statusFields(receipt);
      return (
        fields.state === "verified" &&
        fields.published === String(expectedObjectCount) &&
        fields.verified === String(expectedObjectCount) &&
        fields.total === String(expectedObjectCount) &&
        Boolean(fields.catalog)
      );
    },
  });
  return {
    dispatched,
    completed,
    catalog: parseMvpCatalog(completed.receipt, propId),
  };
}

async function ensurePublishedBundle(
  worker,
  priorPublication,
  propId,
) {
  const current = await invoke(
    worker,
    "gate3BundleStatus",
    [],
    undefined,
    true,
  );
  const fields = statusFields(current.receipt);
  if (fields.state === "verified" && fields.catalog) {
    const catalog = parseMvpCatalog(current.receipt, propId);
    if (
      priorPublication
      && (
        priorPublication.checksum !== catalog.checksum
        || JSON.stringify(priorPublication.objects)
          !== JSON.stringify(catalog.objects)
      )
    ) {
      throw new Error("persisted publication differs from prior report");
    }
    return {
      dispatched: {
        receipt: current.receipt,
        elapsedMs: 0,
        reusedVerifiedPublication: true,
      },
      completed: current,
      catalog,
    };
  }
  if (fields.state !== "missing") {
    throw new Error(
      `publication is not resumable: ${current.receipt}`,
    );
  }
  return publishBundle(worker, propId);
}

async function fetchBundle(worker, catalog) {
  const startedAt = performance.now();
  const before = [];
  let missing = 0;
  let verifiedCount = 0;
  for (const object of catalog.objects) {
    const status = await invoke(
      worker,
      "gate3ObjectStatus",
      [object.objectId],
      undefined,
      true,
    );
    const fields = statusFields(status.receipt);
    if (status.receipt === "state=missing") {
      missing += 1;
    } else if (
      fields.state === "verified"
      && fields.cid === object.cid
    ) {
      verifiedCount += 1;
    } else {
      throw new Error(
        `${worker.label}/${object.objectId} is not resumable: ${status.receipt}`,
      );
    }
    before.push({ objectId: object.objectId, ...status });
  }
  let dispatched;
  let mode;
  if (verifiedCount === catalog.objects.length) {
    mode = "cache";
    dispatched = {
      receipt: `ok;state=verified;catalog=${catalog.encoded}`,
      elapsedMs: 0,
      reusedVerifiedCatalog: true,
    };
  } else {
    mode = "network";
    if (missing + verifiedCount !== catalog.objects.length) {
      throw new Error(`${worker.label} catalog state count mismatch`);
    }
    // Fetch dispatch starts network (or cache) downloads for every catalog
    // object. downloadToUrlV2 acknowledgements can take multi-second provider
    // lookups per object; keep this under the ordinary worker cap.
    dispatched = await invoke(
      worker,
      "gate3FetchBundle",
      [catalog.encoded],
      { prefix: "ok;" },
      false,
      120_000,
    );
    if (
      !dispatched.receipt.includes("state=fetching")
      && !dispatched.receipt.includes("state=verified")
    ) {
      throw new Error(
        `${worker.label} did not expose fetching state: ${dispatched.receipt}`,
      );
    }
  }
  const completed = await pollReceipt({
    worker,
    name: "gate3BundleStatus",
    args: [],
    description: `fetch exact MVP bundle on ${worker.label}`,
    accept: (receipt) => {
      const fields = statusFields(receipt);
      return (
        fields.state === "verified" &&
        fields.published === String(catalog.objects.length) &&
        fields.verified === String(catalog.objects.length) &&
        fields.total === String(catalog.objects.length) &&
        fields.catalog === catalog.encoded
      );
    },
  });
  const verified = [];
  for (const object of catalog.objects) {
    const status = await invoke(
      worker,
      "gate3ObjectStatus",
      [object.objectId],
      undefined,
      true,
    );
    const fields = statusFields(status.receipt);
    if (fields.state !== "verified" || fields.cid !== object.cid) {
      throw new Error(
        `${worker.label}/${object.objectId} was not verified: ${status.receipt}`,
      );
    }
    verified.push({ objectId: object.objectId, cid: object.cid, ...status });
  }
  return {
    mode,
    clock: "performance.now monotonic milliseconds",
    startBoundary: "immediately before first local object status read",
    endBoundary:
      "all exact catalog objects re-read as CID-verified after completion",
    before,
    dispatched,
    completed,
    verified,
    endToEndMs: Math.round(performance.now() - startedAt),
  };
}

async function verifyRetention(worker, catalog, round) {
  const dispatched = await invoke(
    worker,
    "gate3VerifyRetention",
    [],
    { prefix: "ok;" },
  );
  if (
    !dispatched.receipt.includes("retention=fetching") &&
    !dispatched.receipt.includes("retention=verified")
  ) {
    throw new Error(
      `${worker.label} did not expose retention verification: ${dispatched.receipt}`,
    );
  }
  const completed = await pollReceipt({
    worker,
    name: "gate3BundleStatus",
    args: [],
    description: `local retention round ${round} on ${worker.label}`,
    accept: (receipt) => {
      const fields = statusFields(receipt);
      return (
        fields.state === "verified" &&
        fields.retention === "verified" &&
        fields.retention_round === String(round) &&
        fields.verified === String(catalog.objects.length) &&
        fields.catalog === catalog.encoded
      );
    },
  });
  const retained = [];
  for (const object of catalog.objects) {
    const status = await invoke(
      worker,
      "gate3ObjectStatus",
      [object.objectId],
      undefined,
      true,
    );
    const fields = statusFields(status.receipt);
    if (
      fields.state !== "verified" ||
      fields.retention !== "verified" ||
      fields.cid !== object.cid
    ) {
      throw new Error(
        `${worker.label}/${object.objectId} retention failed: ${status.receipt}`,
      );
    }
    retained.push({ objectId: object.objectId, cid: object.cid, ...status });
  }
  return {
    round,
    proof:
      "native exists(cid)=true for every CID, then local-only Storage V2 retrieval with exact length, SHA-256, and bytes",
    dispatched,
    completed,
    retained,
  };
}

async function ensureRetention(worker, catalog, round) {
  const current = await invoke(
    worker,
    "gate3BundleStatus",
    [],
    undefined,
    true,
  );
  const fields = statusFields(current.receipt);
  const currentRound = Number(fields.retention_round ?? -1);
  if (
    fields.state === "verified"
    && fields.retention === "verified"
    && fields.catalog === catalog.encoded
    && Number.isSafeInteger(currentRound)
    && currentRound >= round
  ) {
    const retained = [];
    for (const object of catalog.objects) {
      const status = await invoke(
        worker,
        "gate3ObjectStatus",
        [object.objectId],
        undefined,
        true,
      );
      const objectFields = statusFields(status.receipt);
      if (
        objectFields.state !== "verified"
        || objectFields.retention !== "verified"
        || objectFields.cid !== object.cid
      ) {
        throw new Error(
          `${worker.label}/${object.objectId} retained resume mismatch`,
        );
      }
      retained.push({
        objectId: object.objectId,
        cid: object.cid,
        ...status,
      });
    }
    return {
      round,
      observedRound: currentRound,
      reusedVerifiedRetention: true,
      proof:
        "native exists(cid)=true for every CID, then local-only Storage V2 retrieval with exact length, SHA-256, and bytes",
      dispatched: { receipt: current.receipt, elapsedMs: 0 },
      completed: current,
      retained,
    };
  }
  return verifyRetention(worker, catalog, round);
}

async function fetchPng(worker, asset) {
  const before = await invoke(
    worker,
    "gate3AssetStatus",
    [asset.cid],
    undefined,
    true,
  );
  if (
    before.receipt !== "missing"
    && !before.receipt.startsWith("degraded;reason=")
  ) {
    throw new Error(
      `fetch ${worker.label}/${asset.role} is not retryable: ${before.receipt}`,
    );
  }
  const dispatched = await invoke(
    worker,
    "gate3FetchPng",
    [
      asset.cid,
      asset.cid,
      asset.byteLength,
      asset.handle,
      asset.width,
      asset.height,
    ],
    { prefix: "ok;asset=fetching;" },
  );
  const completed = await pollReceipt({
    worker,
    name: "gate3AssetStatus",
    args: [asset.cid],
    description: `fetch ${worker.label}/${asset.role}`,
    accept: (receipt) => receipt === `verified;handle=${asset.handle}`,
  });
  return { role: asset.role, cid: asset.cid, before, dispatched, completed };
}

async function degradePng(worker, asset) {
  const dispatched = await invoke(
    worker,
    "gate3FetchPng",
    [
      asset.cid,
      asset.cid,
      asset.byteLength,
      "0".repeat(64),
      asset.width,
      asset.height,
    ],
    undefined,
    true,
  );
  failOnRejected(dispatched.receipt, `degraded state ${asset.role}`);
  const completed = await pollReceipt({
    worker,
    name: "gate3AssetStatus",
    args: [asset.cid],
    description: `degraded state ${worker.label}/${asset.role}`,
    accept: (receipt) => receipt.startsWith("degraded;reason="),
  });
  return { role: asset.role, cid: asset.cid, dispatched, completed };
}

async function ensureAssetStateProof(worker, asset, priorProof) {
  let current = await invoke(
    worker,
    "gate3AssetStatus",
    [asset.cid],
    undefined,
    true,
  );
  if (current.receipt.startsWith("fetching")) {
    current = await pollReceipt({
      worker,
      name: "gate3AssetStatus",
      args: [asset.cid],
      description: `resume asset ${worker.label}/${asset.role}`,
      accept: (receipt) =>
        receipt === `verified;handle=${asset.handle}`
        || receipt.startsWith("degraded;reason="),
    });
  }
  if (
    current.receipt.startsWith("degraded;reason=")
    && priorProof?.verifiedAsset?.cid === asset.cid
    && priorProof?.degradedAsset?.cid === asset.cid
  ) {
    return {
      ...priorProof,
      resumedVerifiedDegradedState: true,
      current,
    };
  }
  let verifiedAsset;
  if (current.receipt === `verified;handle=${asset.handle}`) {
    verifiedAsset = {
      role: asset.role,
      cid: asset.cid,
      before: current,
      dispatched: {
        receipt: current.receipt,
        elapsedMs: 0,
        reusedVerifiedAsset: true,
      },
      completed: current,
    };
  } else {
    verifiedAsset = await fetchPng(worker, asset);
  }
  const degradedAsset = await degradePng(worker, asset);
  return {
    states: ["missing", "fetching", "verified", "degraded"],
    verifiedAsset,
    degradedAsset,
  };
}

function processExists(pid) {
  if (!Number.isInteger(pid) || pid <= 0) return false;
  try {
    process.kill(pid, 0);
    return true;
  } catch (error) {
    return error?.code === "EPERM";
  }
}

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
    throw new Error("run-owned child remained in Gate 3 process group");
  }
  if (workload.length > 0) {
    throw new Error("run-owned processes survived Gate 3 cleanup");
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
    const sessionIds = new Set(
      [worker.child.pid, worker.basecampPid].filter(
        (value) => Number.isSafeInteger(value) && value > 0,
      ),
    );
    const remaining = claimWorkload(
      await claimBoundProcesses({
        claimPath: process.env.PALACE_MVP_CLAIM_PATH,
      }),
    ).filter(({ sessionId }) => sessionIds.has(sessionId));
    if (remaining.length > 0) {
      throw new Error(
        `${worker.label} retained owned session processes after cleanup`,
      );
    }
  });
  if (failures.length > 0) {
    throw new Error([...new Set(failures)].join("; "));
  }
}

const currentPackageHashes = await packageHashes(lgxDir);
const currentBasecampDigest = await sha256File(basecamp);
const previousReport = await optionalJson(gate3ReportPath);
if (
  previousReport
  && (
    previousReport.schema !== "logos.palace.basecamp-gate3-report"
    || previousReport.version !== 1
    || previousReport.productSnapshot
      !== (process.env.PALACE_PRODUCT_SNAPSHOT ?? "unknown")
    || previousReport.sourceCommit
      !== (process.env.PALACE_SOURCE_COMMIT ?? "unknown")
    || previousReport.productSnapshotNarHash
      !== (process.env.PALACE_PRODUCT_SNAPSHOT_NAR_HASH ?? "unknown")
    || previousReport.productSnapshotNarSize
      !== Number(process.env.PALACE_PRODUCT_SNAPSHOT_NAR_SIZE ?? Number.NaN)
    || previousReport.snapshotRunnerSha256
      !== (process.env.PALACE_MVP_RUNNER_SHA256 ?? "unknown")
    || previousReport.runtimeOutputManifestSha256
      !== (process.env.PALACE_RUNTIME_OUTPUT_MANIFEST_SHA256 ?? "unknown")
    || previousReport.basecampRevision
      !== (process.env.PALACE_BASECAMP_REV ?? "unknown")
    || previousReport.basecampBinarySha256 !== currentBasecampDigest
    || JSON.stringify(canonicalHashes(previousReport.packageHashes))
      !== JSON.stringify(canonicalHashes(currentPackageHashes))
    || previousReport.productionIdentityMode !== productionIdentityMode
    || (
      previousReport.assetAuthoring?.inputManifest
      && JSON.stringify(previousReport.assetAuthoring.inputManifest)
        !== JSON.stringify(assetInputs.manifest)
    )
  )
) {
  throw new Error(
    "prior Gate 3 report does not match the immutable run inputs",
  );
}

const report = {
  schema: "logos.palace.basecamp-gate3-report",
  version: 1,
  status: "running",
  fullGate3: "running",
  cleanup: { status: "pending", failures: [] },
  blockers: [],
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
  basecampRevision: process.env.PALACE_BASECAMP_REV ?? "unknown",
  packageHashes: currentPackageHashes,
  basecampBinarySha256: currentBasecampDigest,
  installedPackages: { ...(previousReport?.installedPackages ?? {}) },
  productionIdentityMode,
  releasePreflight: previousReport?.releasePreflight,
  identities: { ...(previousReport?.identities ?? {}) },
  storageConfigs: { ...(previousReport?.storageConfigs ?? {}) },
  startup: { ...(previousReport?.startup ?? {}) },
  storageStartup: { ...(previousReport?.storageStartup ?? {}) },
  assetAuthoring:
    previousReport?.assetAuthoring,
  assetAuthoringScreenshot:
    previousReport?.assetAuthoringScreenshot,
  publication: previousReport?.publication,
  providerBFetch: previousReport?.providerBFetch,
  providerBCachedFetch: previousReport?.providerBCachedFetch,
  providerBRetentionProofs: [
    ...(previousReport?.providerBRetentionProofs ?? []),
  ],
  coldCFetch: previousReport?.coldCFetch,
  coldCCachedFetch: previousReport?.coldCCachedFetch,
  assetStateProof: previousReport?.assetStateProof,
  creatorOffline: previousReport?.creatorOffline === true,
};
for (const label of labels) {
  report.installedPackages[label] = JSON.parse(
    await readFile(
      join(artifactsDir, `installed-packages-${label}.json`),
      "utf8",
    ),
  );
}

let reportWrite = Promise.resolve();
function checkpointReport() {
  reportWrite = reportWrite.then(async () => {
    await durableReplace(
      gate3ReportPath,
      `${JSON.stringify(report, null, 2)}\n`,
    );
  });
  return reportWrite;
}

const workers = new Map();
let failure;
let terminationSignal;
let terminationPromise;

function requestTermination(signal) {
  if (terminationPromise) return;
  terminationSignal = signal;
  failure ??= new Error(`Gate 3 termination requested by ${signal}`);
  terminationPromise = (async () => {
    report.status = "failed";
    report.fullGate3 = "failed";
    report.failure = `terminated by ${signal}`;
    const cleanupFailures = await stopKnownWorkers(
      [...workers.values()],
      cleanupClaimBoundProcesses,
    );
    report.cleanup = {
      status: cleanupFailures.length === 0 ? "passed" : "failed",
      failures: cleanupFailures,
    };
    await checkpointReport();
    await reportWrite;
    process.removeAllListeners("SIGINT");
    process.removeAllListeners("SIGTERM");
    process.kill(process.pid, signal);
  })();
}

process.on("SIGINT", () => requestTermination("SIGINT"));
process.on("SIGTERM", () => requestTermination("SIGTERM"));

try {
  if (productionIdentityMode) {
    report.releasePreflight = await runPalaceReleasePreflight();
    if (
      report.releasePreflight.rootAccountBeforeWrites.state
      !== "uninitialized"
    ) {
      throw new Error(
        "Gate 3 production writes require an uninitialized Palace root",
      );
    }
    await checkpointReport();
  }
  let configs;
  const priorConfigs = previousReport?.storageConfigs;
  const storageTcpPortSet = new Set();
  const storageUdpPortSet = new Set();
  if (
    priorConfigs
    && labels.every((label) => typeof priorConfigs[label] === "string")
  ) {
    configs = Object.fromEntries(
      labels.map((label) => {
        let parsed;
        try {
          parsed = JSON.parse(priorConfigs[label]);
        } catch {
          throw new Error(`prior Storage config ${label} is invalid`);
        }
        if (
          (
            productionIdentityMode
              ? !exactProductionStorageConfig(parsed)
              : (
                  !Number.isInteger(parsed?.["listen-port"])
                  || parsed["listen-port"] < 1024
                  || parsed["listen-port"] > 65535
                  || !Number.isInteger(parsed?.["disc-port"])
                  || parsed["disc-port"] < 1024
                  || parsed["disc-port"] > 65535
                  || Object.hasOwn(parsed, "data-dir")
                  || Object.hasOwn(parsed, "log-file")
                )
          )
          || storageTcpPortSet.has(parsed["listen-port"])
          || storageUdpPortSet.has(parsed["disc-port"])
        ) {
          throw new Error(`prior Storage config ${label} is unsafe`);
        }
        storageTcpPortSet.add(parsed["listen-port"]);
        storageUdpPortSet.add(parsed["disc-port"]);
        return [label, priorConfigs[label]];
      }),
    );
  } else {
    const storageTcpPorts = await ephemeralTcpPorts(3);
    const storageUdpPorts = await ephemeralUdpPorts(3);
    const storageBase = parseStorageBaseConfig();
    configs = Object.fromEntries(
      labels.map((label, index) => [
        label,
        storageConfig(
          storageBase,
          storageTcpPorts[index],
          storageUdpPorts[index],
          label,
        ),
      ]),
    );
    for (const port of storageTcpPorts) storageTcpPortSet.add(port);
  }
  const inspectorPorts = await ephemeralTcpPorts(
    3,
    storageTcpPortSet,
  );
  report.storageConfigs = configs;
  await checkpointReport();

  const initialLabels = productionIdentityMode ? labels : ["a", "b"];
  for (const label of initialLabels) {
    const index = labels.indexOf(label);
    const worker = new WorkerClient(label, inspectorPorts[index]);
    workers.set(label, worker);
    report.startup[label] = await worker.init();
    if (productionIdentityMode) {
      report.startup[label].lez =
        await startProductionLez(worker);
      await checkpointReport();
      const identity = await ensureProductionIdentity(worker);
      const priorIdentity = previousReport?.identities?.[label];
      if (
        priorIdentity
        && (
          priorIdentity.accountId !== identity.accountId
          || priorIdentity.deliveryKey !== identity.deliveryKey
          || priorIdentity.display !== identity.display
          || priorIdentity.registrationTransaction
            !== identity.registrationTransaction
        )
      ) {
        throw new Error(
          `production identity ${label} changed during resume`,
        );
      }
      report.identities[label] = identity;
      await checkpointReport();
    }
  }
  if (productionIdentityMode) {
    for (const field of [
      "accountId",
      "deliveryKey",
      "registrationTransaction",
    ]) {
      if (
        new Set(labels.map((label) => report.identities[label]?.[field])).size
        !== labels.length
      ) {
        throw new Error(`production identities reuse ${field}`);
      }
    }
  }
  for (const label of ["a", "b"]) {
    report.storageStartup[label] = await startStorage(
      workers.get(label),
      configs[label],
    );
    await checkpointReport();
  }

  const creator = workers.get("a");
  const provider = workers.get("b");
  report.assetAuthoring =
    await authorAssetFixtures(
      creator,
      previousReport?.assetAuthoring,
      async (evidence) => {
        report.assetAuthoring = evidence;
        await checkpointReport();
      },
    );
  const assetAuthoringRender = await creator.call(
    "assetAuthoring",
    {
      open: true,
      expectedCount: assetFixtures.length,
      expectedProp:
        report.assetAuthoring.assignments.prop !== null,
    },
    30_000,
  );
  report.assetAuthoringScreenshot = {
    ...(await creator.call(
      "screenshot",
      { name: "gate3-admin-assets-published.png" },
      60_000,
    )),
    stage: "gate3-admin-asset-authoring",
    state: "admin-selected-assets-approved-published-assigned",
    label: "a",
    renderEvidence: assetAuthoringRender,
  };
  await creator.call(
    "assetAuthoring", { open: false }, 30_000,
  );
  await checkpointReport();
  const published = await ensurePublishedBundle(
    creator,
    report.publication,
    report.assetAuthoring.assignments.prop?.propId ?? null,
  );
  const authoredAssets = Object.fromEntries(
    report.assetAuthoring.assets.map(
      (asset) => [asset.assetId, asset],
    ),
  );
  const propAssignment = report.assetAuthoring.assignments.prop;
  const graphBindings = [
    {
      kind: "room-background",
      targetId: "atrium",
      objectId: "background-atrium",
    },
    {
      kind: "room-background",
      targetId: "lounge",
      objectId: "background-lounge",
    },
    ...(propAssignment === null
      ? []
      : [{
          kind: "prop-image",
          objectId: `prop-${propAssignment.propId}-image`,
        }]),
  ].map((binding) => {
    const fixture = assetFixtures.reduce(
      (selected, candidate) =>
        candidate.assignment?.kind === binding.kind
        && (
          binding.kind !== "room-background"
          || candidate.assignment.roomId === binding.targetId
        )
          ? candidate
          : selected,
      undefined,
    );
    if (!fixture) {
      throw new Error(`assigned asset fixture is missing: ${binding.kind}`);
    }
    const assetId = fixture.assetId;
    const authored = authoredAssets[assetId];
    const object = published.catalog.objects.find(
      ({ objectId }) => objectId === binding.objectId,
    );
    if (
      !authored
      || !object
      || object.cid !== authored.cid
      || object.contentSha256 !== authored.handle
    ) {
      throw new Error(
        `authored asset is not active graph leaf: ${binding.kind}`,
      );
    }
    return {
      ...binding,
      assetId,
      assignment: fixture.assignment,
      cid: authored.cid,
      contentSha256: authored.handle,
    };
  });
  const propBinding = graphBindings.find(
    ({ kind }) => kind === "prop-image",
  );
  const propAsset = authoredAssets[propBinding?.assetId];
  const activePropProjection = parseActivePropAsset(
    (await creator.call("properties", {}, 30_000)).gate3ActivePropAsset,
    "Gate 3 active prop projection",
    propAssignment !== null,
  );
  if (propAssignment === null ? propBinding !== undefined : (
    !propBinding
      || !propAsset
      || activePropProjection.propId !== propBinding.assignment.propId
      || activePropProjection.handle !== propBinding.contentSha256
      || activePropProjection.contentSha256 !== propBinding.contentSha256
      || activePropProjection.width !== propAsset.width
      || activePropProjection.height !== propAsset.height
      || activePropProjection.anchorX !== propBinding.assignment.anchorX
      || activePropProjection.anchorY !== propBinding.assignment.anchorY
      || activePropProjection.layer !== propBinding.assignment.layer
  )) {
    throw new Error(
      "Gate 3 active prop projection differs from verified graph leaf",
    );
  }
  report.assetAuthoring.graphBindings =
    graphBindings;
  report.assetAuthoring.activePropProjection =
    activePropProjection;
  report.publication = {
    dispatched: published.dispatched,
    completed: published.completed,
    checksum: published.catalog.checksum,
    objects: published.catalog.objects,
  };
  await checkpointReport();
  const providerBFetch = await fetchBundle(provider, published.catalog);
  if (providerBFetch.mode === "network") {
    report.providerBFetch = providerBFetch;
  } else if (report.providerBFetch?.mode !== "network") {
    throw new Error(
      "first provider fetch was already cached without measured network evidence",
    );
  }
  report.providerBCachedFetch = await fetchBundle(
    provider,
    published.catalog,
  );
  if (report.providerBCachedFetch.mode !== "cache") {
    throw new Error("second provider fetch did not use verified local cache");
  }
  await checkpointReport();
  const retentionOne = await ensureRetention(
    provider,
    published.catalog,
    1,
  );
  report.providerBRetentionProofs =
    report.providerBRetentionProofs.filter(({ round }) => round !== 1);
  report.providerBRetentionProofs.push(retentionOne);
  report.providerBRetentionProofs.sort(
    (left, right) => left.round - right.round,
  );
  await checkpointReport();

  const creatorPid = creator.basecampPid;
  report.creatorOffline = false;
  report.creatorStopIntent = { pid: creatorPid, checkpointed: true };
  await checkpointReport();
  await creator.stop();
  workers.delete("a");
  await sleep(1_000);
  if (processExists(creatorPid)) {
    throw new Error(`creator Basecamp process ${creatorPid} remained alive`);
  }
  report.creatorOffline = true;
  await checkpointReport();
  const retentionTwo = await ensureRetention(
    provider,
    published.catalog,
    2,
  );
  report.providerBRetentionProofs =
    report.providerBRetentionProofs.filter(({ round }) => round !== 2);
  report.providerBRetentionProofs.push(retentionTwo);
  report.providerBRetentionProofs.sort(
    (left, right) => left.round - right.round,
  );
  await checkpointReport();

  const atriumObject = published.catalog.objects.find(
    ({ objectId }) => objectId === "background-atrium",
  );
  const atriumAsset = {
    role: "room-background-atrium",
    ...assetFixtures.reduce(
      (selected, candidate) =>
        candidate.assignment?.kind === "room-background"
        && candidate.assignment.roomId === "atrium"
          ? candidate
          : selected,
      undefined,
    ),
    cid: atriumObject.cid,
    byteLength: atriumObject.byteLength,
    handle: atriumObject.contentSha256,
  };
  report.assetStateProof = await ensureAssetStateProof(
    provider,
    atriumAsset,
    report.assetStateProof,
  );
  await checkpointReport();

  let coldClient = workers.get("c");
  if (!coldClient) {
    coldClient = new WorkerClient("c", inspectorPorts[2]);
    workers.set("c", coldClient);
    report.startup.c = await coldClient.init();
  }
  report.storageStartup.c = await startStorage(coldClient, configs.c);
  await checkpointReport();
  const coldCFetch = await fetchBundle(coldClient, published.catalog);
  if (coldCFetch.mode === "network") {
    report.coldCFetch = coldCFetch;
  } else if (report.coldCFetch?.mode !== "network") {
    throw new Error(
      "cold client was already cached without measured network evidence",
    );
  }
  report.coldCCachedFetch = await fetchBundle(
    coldClient,
    published.catalog,
  );
  if (report.coldCCachedFetch.mode !== "cache") {
    throw new Error("second cold-client fetch did not use verified local cache");
  }
  await checkpointReport();

  report.metrics = {
    storage: {
      clock: "performance.now monotonic milliseconds",
      firstNetworkFetch: {
        providerB: {
          mode: report.providerBFetch.mode,
          endToEndMs: report.providerBFetch.endToEndMs,
        },
        coldC: {
          mode: report.coldCFetch.mode,
          endToEndMs: report.coldCFetch.endToEndMs,
        },
      },
      cachedFetch: {
        providerB: {
          mode: report.providerBCachedFetch.mode,
          endToEndMs: report.providerBCachedFetch.endToEndMs,
        },
        coldC: {
          mode: report.coldCCachedFetch.mode,
          endToEndMs: report.coldCCachedFetch.endToEndMs,
        },
      },
    },
  };
} catch (error) {
  failure = error instanceof Error ? error : new Error(String(error));
  report.pngRecovery = "failed";
  report.fullGate3 = "failed";
  report.status = "failed";
  report.failure = failure.message;
} finally {
  const cleanupFailures = await stopKnownWorkers(
    [...workers.values()],
    cleanupClaimBoundProcesses,
  );
  report.cleanup = {
    status: cleanupFailures.length === 0 ? "passed" : "failed",
    failures: cleanupFailures,
  };
  if (cleanupFailures.length > 0) {
    report.pngRecovery = "failed";
    report.fullGate3 = "failed";
    report.status = "failed";
    if (!failure) {
      failure = new Error(
        `Gate 3 terminal cleanup failed: ${cleanupFailures.join("; ")}`,
      );
      report.failure = failure.message;
    }
  } else if (!failure) {
    report.pngRecovery = "passed";
    report.fullGate3 = "passed";
    report.status = "passed";
  }
  await checkpointReport();
  await reportWrite;
}

if (failure) throw failure;
