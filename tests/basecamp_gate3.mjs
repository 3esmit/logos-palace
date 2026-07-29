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
import { dirname, join, resolve } from "node:path";
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
  ownedProcessGroupMembers,
  requireOwnedProcessGroup,
} from "./basecamp_owned_processes.mjs";

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
const pngAssets = [
  {
    role: "room-background-atrium",
    handle:
      "3bd13dc41f3e27a7eabf45c73188498b95e3e5e475e6fcb967308477afd522be",
    byteLength: 172,
    width: 16,
    height: 9,
  },
  {
    role: "room-background-lounge",
    handle:
      "d2068f9cc4848b29882e532580c2b455ef5d243e7b38f16d590937fbef720486",
    byteLength: 173,
    width: 16,
    height: 9,
  },
];
const objectOrder = [
  "background-atrium",
  "background-lounge",
  "prop-hat-image",
  "prop-hat-metadata",
  "room-atrium-metadata",
  "room-lounge-metadata",
  "script-door",
  "prop-hat",
  "room-atrium",
  "room-lounge",
  "palace-1",
];
const objectTypes = [
  "background_png",
  "background_png",
  "prop_png",
  "prop_metadata",
  "room_metadata",
  "room_metadata",
  "script_bundle",
  "prop_manifest",
  "room_manifest",
  "room_manifest",
  "palace_manifest",
];
const holderProfiles = {
  a: "alice",
  b: "bob",
  c: "carol",
};

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

function signalProcessGroup(child, signal) {
  try {
    process.kill(-child.pid, signal);
  } catch {
    child.kill(signal);
  }
}

async function terminateKnownBasecamp(pid, expectedUserDir) {
  if (!Number.isSafeInteger(pid) || pid <= 0 || !processExists(pid)) return;
  let argv;
  try {
    argv = (await readFile(`/proc/${pid}/cmdline`))
      .toString("utf8")
      .split("\0")
      .filter(Boolean);
  } catch (error) {
    if (error?.code === "ENOENT") return;
    throw error;
  }
  const userDirIndex = argv.indexOf("--user-dir");
  if (
    argv.length === 0
    || resolve(argv[0]) !== basecamp
    || (
      expectedUserDir
      && (
        userDirIndex < 0
        || !argv[userDirIndex + 1]
        || resolve(argv[userDirIndex + 1]) !== resolve(expectedUserDir)
      )
    )
  ) {
    throw new Error(`refusing to signal unexpected reused PID ${pid}`);
  }
  const signalGroup = (signal) => {
    try {
      process.kill(-pid, signal);
    } catch (error) {
      if (error?.code !== "ESRCH") process.kill(pid, signal);
    }
  };
  signalGroup("SIGTERM");
  for (let attempt = 0; attempt < 50 && processExists(pid); attempt += 1) {
    await sleep(100);
  }
  if (processExists(pid)) {
    signalGroup("SIGKILL");
    for (let attempt = 0; attempt < 50 && processExists(pid); attempt += 1) {
      await sleep(100);
    }
  }
  if (processExists(pid)) {
    throw new Error(`Basecamp process ${pid} survived cleanup`);
  }
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
        stdio: childStdioWithInheritedMvpLock(["pipe", "pipe", "pipe"]),
      },
    );
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
        signalProcessGroup(this.child, "SIGTERM");
      }
      this.child.stdin.end();
      const stopped = await Promise.race([
        this.exited.then(() => true),
        sleep(5_000).then(() => false),
      ]);
      if (!stopped) {
        signalProcessGroup(this.child, "SIGKILL");
        await this.exited;
      }
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
  return {
    receipt: String(result.receipt),
    elapsedMs: result.elapsedMs,
  };
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
  const deadline = Date.now() + 5 * 60_000;
  let lastReceipt = "";
  while (Date.now() < deadline) {
    const result = await invoke(
      worker,
      "gate4StartLez",
      [password],
      undefined,
      false,
      120_000,
    );
    lastReceipt = result.receipt;
    const fields = statusFields(lastReceipt);
    if (
      lastReceipt.startsWith("ok;")
      && fields.ready === "1"
      && fields.compatible === "1"
      && fields.running === "1"
      && fields.sync === "current"
      && fields.current_height === fields.synced_height
      && fields.program === palaceRelease.programIdHex
    ) {
      return result;
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

function decodeBase32(value) {
  const alphabet = "abcdefghijklmnopqrstuvwxyz234567";
  let accumulator = 0;
  let bitCount = 0;
  const bytes = [];
  for (const character of value) {
    const digit = alphabet.indexOf(character);
    if (digit < 0) throw new Error("CID contains non-base32 character");
    accumulator = (accumulator << 5) | digit;
    bitCount += 5;
    while (bitCount >= 8) {
      bitCount -= 8;
      bytes.push((accumulator >> bitCount) & 0xff);
      accumulator &= bitCount === 0 ? 0 : (1 << bitCount) - 1;
    }
  }
  if (bitCount > 0 && accumulator !== 0) {
    throw new Error("CID has non-canonical base32 tail bits");
  }
  return Buffer.from(bytes);
}

const base58Alphabet =
  "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

function decodeBase58(value) {
  if (!value) throw new Error("CID base58 payload is empty");
  let decoded = 0n;
  for (const character of value) {
    const digit = base58Alphabet.indexOf(character);
    if (digit < 0) throw new Error("CID contains non-base58 character");
    decoded = decoded * 58n + BigInt(digit);
  }
  let hex = decoded.toString(16);
  if (hex.length % 2 !== 0) hex = `0${hex}`;
  const payload =
    decoded === 0n ? Buffer.alloc(0) : Buffer.from(hex, "hex");
  let leadingZeros = 0;
  while (
    leadingZeros < value.length &&
    value[leadingZeros] === base58Alphabet[0]
  ) {
    leadingZeros += 1;
  }
  return Buffer.concat([Buffer.alloc(leadingZeros), payload]);
}

function encodeBase58(bytes) {
  let value = 0n;
  for (const byte of bytes) value = value * 256n + BigInt(byte);
  let encoded = "";
  while (value > 0n) {
    const digit = Number(value % 58n);
    encoded = base58Alphabet[digit] + encoded;
    value /= 58n;
  }
  let leadingZeros = 0;
  while (leadingZeros < bytes.length && bytes[leadingZeros] === 0) {
    encoded = base58Alphabet[0] + encoded;
    leadingZeros += 1;
  }
  return encoded;
}

function readVarint(bytes, cursor) {
  let value = 0;
  let multiplier = 1;
  const start = cursor.offset;
  while (cursor.offset < bytes.length) {
    const byte = bytes[cursor.offset++];
    value += (byte & 0x7f) * multiplier;
    if (!Number.isSafeInteger(value)) throw new Error("CID varint overflow");
    if ((byte & 0x80) === 0) {
      if (cursor.offset - start > 1 && byte === 0) {
        throw new Error("CID varint is not canonical");
      }
      return value;
    }
    multiplier *= 128;
  }
  throw new Error("CID varint is truncated");
}

function cidSha256(cid) {
  let bytes;
  if (/^b[a-z2-7]+$/.test(cid)) {
    bytes = decodeBase32(cid.slice(1));
  } else if (/^z[1-9A-HJ-NP-Za-km-z]+$/.test(cid)) {
    bytes = decodeBase58(cid.slice(1));
    if (`z${encodeBase58(bytes)}` !== cid) {
      throw new Error(`CID is not canonical base58btc: ${cid}`);
    }
  } else {
    throw new Error(`CID is not canonical CIDv1 base32/base58btc: ${cid}`);
  }
  const cursor = { offset: 0 };
  const version = readVarint(bytes, cursor);
  const codec = readVarint(bytes, cursor);
  const multihash = readVarint(bytes, cursor);
  const digestLength = readVarint(bytes, cursor);
  if (
    version !== 1 ||
    codec === 0 ||
    multihash !== 0x12 ||
    digestLength !== 32 ||
    cursor.offset + digestLength !== bytes.length
  ) {
    throw new Error(`CID has unexpected envelope: ${cid}`);
  }
  return bytes.subarray(cursor.offset).toString("hex");
}

function parseMvpCatalog(receipt) {
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
  if (
    lines.length !== 16 ||
    lines[0] !== "logos-palace-mvp-storage-catalog-v1" ||
    lines[1] !== "version=1" ||
    lines[2] !== "root=palace-1" ||
    lines[3] !== "objects=11"
  ) {
    throw new Error("MVP catalog envelope mismatch");
  }
  const checksumPrefix = "checksum=";
  if (!lines[15].startsWith(checksumPrefix)) {
    throw new Error("MVP catalog checksum missing");
  }
  const checksumOffset = catalog.lastIndexOf(checksumPrefix);
  const expectedChecksum = createHash("sha256")
    .update(catalog.slice(0, checksumOffset))
    .digest("hex");
  if (lines[15].slice(checksumPrefix.length) !== expectedChecksum) {
    throw new Error("MVP catalog checksum mismatch");
  }

  const objects = lines.slice(4, 15).map((line, index) => {
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
      type !== objectTypes[index] ||
      !mediaType ||
      !Number.isSafeInteger(byteLength) ||
      byteLength <= 0 ||
      byteLength > 10 * 1024 * 1024 ||
      !/^[0-9a-f]{64}$/.test(contentSha256) ||
      cidSha256(cid) !== contentSha256
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

async function publishBundle(worker) {
  const dispatched = await invoke(
    worker,
    "gate3PublishBundle",
    [],
    { prefix: "ok;" },
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
        fields.published === "11" &&
        fields.verified === "11" &&
        fields.total === "11" &&
        Boolean(fields.catalog)
      );
    },
  });
  return {
    dispatched,
    completed,
    catalog: parseMvpCatalog(completed.receipt),
  };
}

async function ensurePublishedBundle(worker, priorPublication) {
  const current = await invoke(
    worker,
    "gate3BundleStatus",
    [],
    undefined,
    true,
  );
  const fields = statusFields(current.receipt);
  if (fields.state === "verified" && fields.catalog) {
    const catalog = parseMvpCatalog(current.receipt);
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
  return publishBundle(worker);
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
    dispatched = await invoke(
      worker,
      "gate3FetchBundle",
      [catalog.encoded],
      { prefix: "ok;" },
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
        fields.published === "11" &&
        fields.verified === "11" &&
        fields.total === "11" &&
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
        fields.verified === "11" &&
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

function processGroupExists(processGroupId) {
  if (!Number.isSafeInteger(processGroupId) || processGroupId <= 0) {
    return false;
  }
  try {
    process.kill(-processGroupId, 0);
    return true;
  } catch (error) {
    return error?.code === "EPERM";
  }
}

async function terminateOwnedProcessGroup(processGroupId) {
  if (
    !Number.isSafeInteger(processGroupId)
    || processGroupId <= 0
    || !processGroupExists(processGroupId)
  ) {
    return;
  }
  const members = await ownedProcessGroupMembers({
    processGroupId,
    claimPath: process.env.PALACE_MVP_CLAIM_PATH,
  });
  if (members.length === 0 && !processGroupExists(processGroupId)) {
    return;
  }
  requireOwnedProcessGroup(members, processGroupId);
  try {
    process.kill(-processGroupId, "SIGKILL");
  } catch (error) {
    if (error?.code !== "ESRCH") throw error;
  }
  for (
    let attempt = 0;
    attempt < 50 && processGroupExists(processGroupId);
    attempt += 1
  ) {
    await sleep(100);
  }
  if (processGroupExists(processGroupId)) {
    throw new Error(`process group ${processGroupId} survived SIGKILL`);
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
  for (const processGroupId of new Set(
    workload.map(({ processGroupId }) => processGroupId),
  )) {
    await terminateOwnedProcessGroup(processGroupId);
  }
  const remaining = claimWorkload(
    await claimBoundProcesses({ claimPath }),
  );
  if (remaining.length > 0) {
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

  if (Number.isSafeInteger(worker.basecampPid)) {
    await attempt(() =>
      terminateKnownBasecamp(worker.basecampPid, expectedUserDir));
    await attempt(() => terminateOwnedProcessGroup(worker.basecampPid));
  }
  let discovered = [];
  await attempt(async () => {
    discovered = await discoverOwnedBasecampProcesses({
      basecamp,
      userDirs: new Set([expectedUserDir]),
    });
  });
  for (const process of discovered) {
    worker.basecampPid ??= process.pid;
    await attempt(() =>
      terminateKnownBasecamp(process.pid, expectedUserDir));
    await attempt(() =>
      terminateOwnedProcessGroup(process.processGroupId));
  }
  await attempt(() => terminateOwnedProcessGroup(worker.child.pid));
  await attempt(async () => {
    const sessionIds = new Set(
      [worker.child.pid, worker.basecampPid].filter(
        (value) => Number.isSafeInteger(value) && value > 0,
      ),
    );
    const sessionProcesses = claimWorkload(
      await claimBoundProcesses({
        claimPath: process.env.PALACE_MVP_CLAIM_PATH,
      }),
    ).filter(({ sessionId }) => sessionIds.has(sessionId));
    for (const processGroupId of new Set(
      sessionProcesses.map(({ processGroupId }) => processGroupId),
    )) {
      await terminateOwnedProcessGroup(processGroupId);
    }
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
      processExists,
      processGroupExists,
      terminateOwnedProcessGroup,
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
  const published = await ensurePublishedBundle(
    creator,
    report.publication,
  );
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
    ...pngAssets[0],
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
    processExists,
    processGroupExists,
    terminateOwnedProcessGroup,
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
