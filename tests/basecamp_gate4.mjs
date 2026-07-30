#!/usr/bin/env node

import { createHash } from "node:crypto";
import {
  constants as fsConstants,
  createReadStream,
  createWriteStream,
} from "node:fs";
import {
  access,
  lstat,
  mkdir,
  open,
  readFile,
  readlink,
  realpath,
  readdir,
  rename,
  rm,
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
  captureOwnedProcessIdentity,
  claimBoundProcesses,
  discoverOwnedBasecampProcesses,
  ownedProcessIdentityExists,
  ownedProcessGroupMembers,
  requireOwnedProcessGroup,
} from "./basecamp_owned_processes.mjs";
import {
  originalCreatorProcessExists,
  validateCreatorProcessIdentity,
} from "./basecamp_gate4_creator_identity.mjs";
import {
  exactProcessInventoryContract,
  findStandalonePalaceServerMatches,
  parseTcpListenTable,
  validateExactProcessInventory,
  validateExpectedExecutableMapping,
  validateExactTcpListenerOwnership,
} from "./basecamp_process_model.mjs";
import {
  boundedDirectoryFingerprint,
  fingerprintBoundedOpenRegularFile,
  fingerprintBoundedRegularFile,
  fingerprintBoundedRegularFileIdentity,
} from "./basecamp_gate4_cold_state.mjs";
import {
  completeTimingBoundary,
  completedTimingTimestamp,
  finalizeLezTimingEvidence,
  isCompleteTimingMeasurement,
  lezMeasurementBoundaries,
  lezStageTimingFields,
  markRecoveredTimingUnmeasured,
  recoverPersistedTimingEvidence,
  resumableTimings,
  setCoalescedFinalityTiming,
  startTimingBoundary,
  timingBoundaryNames,
  validateObservationTimingEnvelope,
} from "./basecamp_lez_timing.mjs";
import {
  validatePalaceFrameTimingMeasurement,
} from "./basecamp_frame_timing.mjs";
import {
  canonicalStorageCidSha256 as cidSha256,
} from "./basecamp_storage_cid.mjs";
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
  gate3ReportArgument,
] = process.argv.slice(2);
if (
  !basecampArgument ||
  !usersDirArgument ||
  !artifactsArgument ||
  !lgxDirArgument ||
  !workerArgument ||
  !gate3ReportArgument
) {
  throw new Error(
    "usage: node tests/basecamp_gate4.mjs <Basecamp> <users-dir> <artifacts-dir> <lgx-dir> <worker> <gate3-report>",
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
const gate3ReportPath = resolve(gate3ReportArgument);
const reportPath = join(artifactsDir, "gate4-report.json");
const labels = ["a", "b", "c"];
const displayNames = { a: "Alice", b: "Bob", c: "Carol" };
const processProgramSpecs = Object.freeze([
  {
    role: "basecamp-main",
    program: "LogosBasecamp",
    argument: ".LogosBasecamp.elf",
    relativePath: "bin/.LogosBasecamp.elf",
  },
  {
    role: "core-module-host",
    program: "logos_host",
    argument: ".logos_host.elf",
    relativePath: "bin/.logos_host.elf",
  },
  {
    role: "ui-module-host",
    program: "ui-host",
    argument: ".ui-host.elf",
    relativePath: "bin/.ui-host.elf",
  },
]);
const bundledProcessModuleSpecs = Object.freeze([
  "capability_module",
  "package_downloader",
  "package_manager",
].map((moduleName) => ({
  moduleName,
  relativePath: `modules/${moduleName}/${moduleName}_plugin.so`,
})));
const installedProcessModuleSpecs = Object.freeze([
  {
    moduleName: "delivery_module",
    packageFile: "logos-delivery_module-module-lib.lgx",
    mainFile: "delivery_module_plugin.so",
    type: "core",
  },
  {
    moduleName: "lez_core",
    packageFile: "logos-lez_core-module-lib.lgx",
    mainFile: "lez_core_plugin.so",
    type: "core",
  },
  {
    moduleName: "logos_palace_ui",
    packageFile: "logos-logos_palace_ui-module.lgx",
    mainFile: "logos_palace_ui_plugin.so",
    type: "ui_qml",
  },
  {
    moduleName: "palace_core",
    packageFile: "logos-palace_core-module-lib.lgx",
    mainFile: "palace_core_plugin.so",
    type: "core",
  },
  {
    moduleName: "palace_vm",
    packageFile: "logos-palace_vm-module-lib.lgx",
    mainFile: "palace_vm_plugin.so",
    type: "core",
  },
  {
    moduleName: "storage_module",
    packageFile: "logos-storage_module-module-lib.lgx",
    mainFile: "storage_module_plugin.so",
    type: "core",
  },
]);
const processArtifactMaximumBytes = 256 * 1024 * 1024;
const basecampWrapperInterpreterName = "ld-linux-x86-64.so.2";
const basecampWrapperDirectLoaderPath =
  `/lib/${basecampWrapperInterpreterName}`;
const basecampWrapperFallbackLoaderPaths = Object.freeze([
  `/lib64/${basecampWrapperInterpreterName}`,
  `/lib/${basecampWrapperInterpreterName}`,
  `/lib/x86_64-linux-gnu/${basecampWrapperInterpreterName}`,
  `/lib/aarch64-linux-gnu/${basecampWrapperInterpreterName}`,
  `/usr/lib64/${basecampWrapperInterpreterName}`,
  `/usr/lib/${basecampWrapperInterpreterName}`,
]);
const releaseProgramId = palaceRelease.programIdHex;
const releaseRootId = palaceRelease.rootAccountIdHex;
const approvedLezModuleRevision =
  "e8d84103660604b1a6a06ddd66d20da7a2fdeb3f";
function expectedGraphObjectContract(propId) {
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

function expectedAssetGraphTargets(propId) {
  return [
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
    ...(propId === null
      ? []
      : [{
          kind: "prop-image",
          objectId: `prop-${propId}-image`,
        }]),
  ];
}
const deliveryNodeKeys = {
  a: stableId("delivery-node-key/a"),
  b: stableId("delivery-node-key/b"),
  c: stableId("delivery-node-key/c"),
};
const deliveryClusterId = 4346;
const missingStorageFixtureCid =
  "zb2rhWeJ5rsKhbfi31kikfdS3hE2pvWzhW27Es4Jmc863vFdx";
const screenshotSpecs = {
  gate4Convergence: {
    file: "gate4-a-three-user-atrium-converged.png",
    stage: "gate4-delivery-convergence",
    state: "three-user-atrium-converged",
    label: "a",
  },
  gate4StorageDegraded: {
    file: "gate4-b-storage-object-degraded.png",
    stage: "gate4-storage-failure",
    state: "missing-storage-object-degraded",
    label: "b",
  },
  gate4Moderation: {
    file: "gate4-b-atrium-after-moderation.png",
    stage: "gate4-moderation",
    state: "atrium-after-human-moderation",
    label: "b",
  },
  gate5PreviewA: {
    file: "gate5-a-door-preview.png",
    stage: "gate5-preview",
    state: "door-preview-before-finality",
    label: "a",
  },
  gate5PreviewB: {
    file: "gate5-b-door-preview.png",
    stage: "gate5-preview",
    state: "door-preview-before-finality",
    label: "b",
  },
  gate5PendingB: {
    file: "gate5-b-door-pending.png",
    stage: "gate5-pending",
    state: "door-awaiting-lez-observation",
    label: "b",
  },
  gate5FinalB: {
    file: "gate5-b-lounge-finalized.png",
    stage: "gate5-finality",
    state: "lounge-after-door-finality",
    label: "b",
  },
  gate6RestartB: {
    file: "gate6-b-lounge-restarted.png",
    stage: "gate6-restart",
    state: "lounge-after-bob-carol-restart",
    label: "b",
  },
  gate6RestartC: {
    file: "gate6-c-lounge-restarted.png",
    stage: "gate6-restart",
    state: "lounge-after-bob-carol-restart",
    label: "c",
  },
  gate6OfflineB: {
    file: "gate6-b-offline-before-reconnect.png",
    stage: "gate6-offline",
    state: "creator-offline-client-before-reconnect",
    label: "b",
  },
};

await mkdir(artifactsDir, { recursive: true });

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

function lengthEncoded(value) {
  const text = String(value);
  return `${Buffer.byteLength(text, "utf8")}:${text}`;
}

function humanModerationBanId(kind, actionId, issuer, target) {
  return sha256(
    "logos-palace-human-ban-v1;"
      + `kind=${lengthEncoded(kind)}`
      + `;action=${lengthEncoded(actionId)}`
      + `;program=${lengthEncoded(releaseProgramId)}`
      + `;root=${lengthEncoded(releaseRootId)}`
      + `;issuer=${lengthEncoded(issuer)}`
      + `;target=${lengthEncoded(target)}`
      + `;scope=${lengthEncoded("palace")}`,
  );
}

function isHex64(value) {
  return typeof value === "string" && /^[0-9a-f]{64}$/.test(value);
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

async function exactRegularFileArtifact(path, description) {
  const metadata = await lstat(path);
  const canonical = await realpath(path);
  if (
    !metadata.isFile()
    || metadata.isSymbolicLink()
    || canonical !== path
  ) {
    throw new Error(`${description} is not an exact canonical regular file`);
  }
  const fingerprint = await fingerprintBoundedRegularFileIdentity(
    path,
    processArtifactMaximumBytes,
  );
  if (fingerprint.byteLength <= 0) {
    throw new Error(`${description} is empty`);
  }
  return {
    path,
    ...fingerprint,
  };
}

function processArtifactIdentity(role, moduleName) {
  return `${role}:${moduleName ?? ""}`;
}

async function wrapperPathIsExecutable(path) {
  try {
    await access(path, fsConstants.X_OK);
    return true;
  } catch (error) {
    if (
      ["EACCES", "ENOENT", "ENOTDIR"].includes(error?.code)
    ) {
      return false;
    }
    throw error;
  }
}

async function selectBasecampWrapperExecution() {
  if (await wrapperPathIsExecutable(basecampWrapperDirectLoaderPath)) {
    const canonicalPath = await realpath(
      basecampWrapperDirectLoaderPath,
    );
    const artifact = await fingerprintBoundedRegularFileIdentity(
      canonicalPath,
      processArtifactMaximumBytes,
    );
    const executableBasename = basename(canonicalPath);
    return {
      raw: {
        mode: "direct",
        fallbackIndex: null,
        candidatePath: basecampWrapperDirectLoaderPath,
        canonicalPath,
        argumentBasename: null,
        executableBasename,
        sha256: artifact.sha256,
        device: artifact.device,
        inode: artifact.inode,
      },
      publicEvidence: {
        mode: "direct",
        fallbackIndex: null,
        argumentBasename: null,
        executableBasename,
        sha256: artifact.sha256,
      },
      expected: {
        executionMode: "direct",
        loaderPath: canonicalPath,
        loaderArgument: null,
        loaderExecutable: executableBasename,
        loaderSha256: artifact.sha256,
        loaderDevice: artifact.device,
        loaderInode: artifact.inode,
      },
    };
  }
  for (
    let fallbackIndex = 0;
    fallbackIndex < basecampWrapperFallbackLoaderPaths.length;
    fallbackIndex += 1
  ) {
    const candidatePath =
      basecampWrapperFallbackLoaderPaths[fallbackIndex];
    if (!(await wrapperPathIsExecutable(candidatePath))) continue;
    const canonicalPath = await realpath(candidatePath);
    const artifact = await fingerprintBoundedRegularFileIdentity(
      canonicalPath,
      processArtifactMaximumBytes,
    );
    const argumentBasename = basename(candidatePath);
    const executableBasename = basename(canonicalPath);
    return {
      raw: {
        mode: "fallback",
        fallbackIndex,
        candidatePath,
        canonicalPath,
        argumentBasename,
        executableBasename,
        sha256: artifact.sha256,
        device: artifact.device,
        inode: artifact.inode,
      },
      publicEvidence: {
        mode: "fallback",
        fallbackIndex,
        argumentBasename,
        executableBasename,
        sha256: artifact.sha256,
      },
      expected: {
        executionMode: "fallback",
        loaderPath: canonicalPath,
        loaderArgument: argumentBasename,
        loaderExecutable: executableBasename,
        loaderSha256: artifact.sha256,
        loaderDevice: artifact.device,
        loaderInode: artifact.inode,
      },
    };
  }
  throw new Error("Basecamp wrapper has no executable loader selection");
}

async function buildProcessRuntimeArtifacts(
  installedPackages,
  installedRoots,
  sourceLgxHashes,
) {
  const bundleRoot = dirname(dirname(basecamp));
  const canonicalBasecamp = await realpath(basecamp);
  if (
    canonicalBasecamp !== basecamp
    || basecamp !== join(bundleRoot, "bin", "LogosBasecamp")
  ) {
    throw new Error("Basecamp executable is not from exact bundle root");
  }
  const wrapperExecution = await selectBasecampWrapperExecution();
  const programs = [];
  const programLiveByRole = new Map();
  for (const spec of processProgramSpecs) {
    const artifact = await exactRegularFileArtifact(
      join(bundleRoot, spec.relativePath),
      `Basecamp ${spec.program} process artifact`,
    );
    programs.push({
      role: spec.role,
      program: spec.program,
      argument: spec.argument,
      relativePath: spec.relativePath,
      sha256: artifact.sha256,
    });
    programLiveByRole.set(spec.role, {
      programArgument: spec.argument,
      programPath: artifact.path,
      programSha256: artifact.sha256,
      programDevice: artifact.device,
      programInode: artifact.inode,
    });
  }
  const bundledModules = [];
  const moduleLiveByName = new Map();
  for (const spec of bundledProcessModuleSpecs) {
    const artifact = await exactRegularFileArtifact(
      join(bundleRoot, spec.relativePath),
      `bundled ${spec.moduleName} module artifact`,
    );
    bundledModules.push({
      moduleName: spec.moduleName,
      relativePath: spec.relativePath,
      sha256: artifact.sha256,
    });
    moduleLiveByName.set(spec.moduleName, artifact);
  }
  const expectedSourceLgx = canonicalHashes(sourceLgxHashes);
  const sourceLgxByFile = new Map(
    expectedSourceLgx.map((entry) => [entry.file, entry]),
  );
  if (
    expectedSourceLgx.length !== installedProcessModuleSpecs.length
    || installedProcessModuleSpecs.some(
      ({ packageFile }) => !sourceLgxByFile.has(packageFile),
    )
  ) {
    throw new Error("process artifact LGX source set is not exact");
  }
  const expectedArtifactsByLabel = new Map();
  let installedModules;
  for (const label of labels) {
    const packageList = installedPackages[label];
    const roots = installedRoots[label];
    if (
      !Array.isArray(packageList)
      || packageList.length !== installedProcessModuleSpecs.length
      || !roots
      || roots.stagedThenSwappedWithRecovery !== true
      || !/^sha256-[A-Za-z0-9+/]{43}=$/.test(
        roots.modulesNarHash ?? "",
      )
      || !/^sha256-[A-Za-z0-9+/]{43}=$/.test(
        roots.pluginsNarHash ?? "",
      )
      || !exactJson(
        canonicalHashes(roots.sourceLgxSet),
        expectedSourceLgx,
      )
    ) {
      throw new Error(
        `installed process artifact roots are not exact for ${label}`,
      );
    }
    const packageByName = new Map(
      packageList.map((entry) => [entry?.name, entry]),
    );
    if (packageByName.size !== installedProcessModuleSpecs.length) {
      throw new Error(
        `installed process package set is not unique for ${label}`,
      );
    }
    const labelModules = [];
    const labelModuleLiveByName = new Map(moduleLiveByName);
    for (const spec of installedProcessModuleSpecs) {
      const entry = packageByName.get(spec.moduleName);
      const installClass =
        spec.type === "ui_qml" ? "plugins" : "modules";
      const expectedInstallDir = join(
        usersDir,
        label,
        installClass,
        spec.moduleName,
      );
      const expectedMainFilePath = join(
        expectedInstallDir,
        spec.mainFile,
      );
      if (
        entry?.name !== spec.moduleName
        || entry.type !== spec.type
        || entry.installDir !== expectedInstallDir
        || entry.mainFilePath !== expectedMainFilePath
        || !isHex64(entry.hashes?.root)
      ) {
        throw new Error(
          `installed ${spec.moduleName} artifact is not exact for ${label}`,
        );
      }
      const artifact = await exactRegularFileArtifact(
        expectedMainFilePath,
        `installed ${spec.moduleName} module artifact`,
      );
      const source = sourceLgxByFile.get(spec.packageFile);
      labelModules.push({
        moduleName: spec.moduleName,
        packageFile: spec.packageFile,
        packageSha256: source.sha256,
        installedRootSha256: entry.hashes.root,
        mainFile: spec.mainFile,
        mainFileSha256: artifact.sha256,
      });
      labelModuleLiveByName.set(spec.moduleName, artifact);
    }
    labelModules.sort(
      (left, right) => left.moduleName.localeCompare(right.moduleName),
    );
    if (installedModules && !exactJson(installedModules, labelModules)) {
      throw new Error(
        "installed process artifacts differ between Basecamp clients",
      );
    }
    installedModules ??= labelModules;
    const expected = {};
    const addExpected = (role, moduleName) => {
      const program = programLiveByRole.get(role);
      const module = moduleName
        ? labelModuleLiveByName.get(moduleName)
        : undefined;
      if (!program || (moduleName && !module)) {
        throw new Error("process runtime artifact map is incomplete");
      }
      expected[processArtifactIdentity(role, moduleName)] = {
        ...program,
        ...wrapperExecution.expected,
        modulePath: module?.path ?? null,
        moduleSha256: module?.sha256 ?? null,
        moduleDevice: module?.device ?? null,
        moduleInode: module?.inode ?? null,
      };
    };
    addExpected("basecamp-main", null);
    for (const moduleName of exactProcessInventoryContract.coreModuleHosts) {
      addExpected("core-module-host", moduleName);
    }
    addExpected("ui-module-host", "logos_palace_ui");
    expectedArtifactsByLabel.set(label, expected);
  }
  return {
    publicEvidence: {
      basecampBundlePrograms: programs,
      basecampBundleModules: bundledModules,
      installedLgxModules: installedModules,
      wrapperExecution: wrapperExecution.publicEvidence,
    },
    loaderSelection: wrapperExecution.raw,
    expectedArtifactsByLabel,
  };
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

function exactJson(left, right) {
  return JSON.stringify(left) === JSON.stringify(right);
}

function stableJson(value) {
  if (Array.isArray(value)) return value.map(stableJson);
  if (value && typeof value === "object") {
    return Object.fromEntries(
      Object.keys(value)
        .sort()
        .map((key) => [key, stableJson(value[key])]),
    );
  }
  return value;
}

function exactStableJson(left, right) {
  return JSON.stringify(stableJson(left)) === JSON.stringify(stableJson(right));
}

async function readJson(path, maximumBytes = 8 * 1024 * 1024) {
  const metadata = await lstat(path);
  if (
    !metadata.isFile()
    || metadata.isSymbolicLink()
    || metadata.size <= 0
    || metadata.size > maximumBytes
  ) {
    throw new Error(`${basename(path)} is not a bounded regular file`);
  }
  const bytes = await readFile(path);
  if (bytes.length === 0 || bytes.length > maximumBytes) {
    throw new Error(`${basename(path)} has invalid size`);
  }
  try {
    return JSON.parse(bytes.toString("utf8"));
  } catch {
    throw new Error(`${basename(path)} is not valid JSON`);
  }
}

async function optionalJson(path) {
  try {
    await access(path);
  } catch {
    return undefined;
  }
  return readJson(path);
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

class WorkerClient {
  constructor(label, inspectorPort, generation) {
    this.label = label;
    this.inspectorPort = inspectorPort;
    this.processLabel = `${label}-${generation}`;
    this.nextId = 0;
    this.pending = new Map();
    this.basecampPid = undefined;
    this.protocolFailure = undefined;
    this.stderr = createWriteStream(
      join(artifactsDir, `worker-${this.processLabel}.stderr.log`),
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
        this.processLabel,
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
    this.child.stderr.pipe(this.stderr);
    this.exited = new Promise((resolveExit) => {
      this.child.once("exit", (code, signal) => {
        resolveExit({ code, signal });
        for (const pending of this.pending.values()) {
          clearTimeout(pending.timer);
          pending.reject(
            new Error(
              `worker ${this.processLabel} exited: code=${code} signal=${signal}`,
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
      else {
        pending.reject(
          new Error(`worker ${this.processLabel}: ${message.error}`),
        );
      }
    });
  }

  async call(command, params = {}, timeout = 180_000) {
    if (this.protocolFailure) throw this.protocolFailure;
    if (terminationSignal && command !== "shutdown") {
      throw new Error(`Gate 4 termination requested by ${terminationSignal}`);
    }
    if (this.child.exitCode !== null) {
      throw new Error(`worker ${this.processLabel} is not running`);
    }
    const id = String(++this.nextId);
    return new Promise((resolveCall, rejectCall) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        rejectCall(
          new Error(
            `worker ${this.processLabel} command timed out: ${command}`,
          ),
        );
      }, timeout);
      this.pending.set(id, { resolve: resolveCall, reject: rejectCall, timer });
      this.child.stdin.write(
        `${JSON.stringify({ id, command, params })}\n`,
      );
    });
  }

  async init() {
    const initialized = await this.call("init", {}, 180_000);
    this.basecampPid = acceptBasecampPidHandoff(
      this.basecampPid,
      {
        event: "basecamp-started",
        basecampPid: Number(initialized.basecampPid),
      },
    );
    return initialized;
  }

  async stop() {
    this.stopPromise ??= this.stopImpl();
    return this.stopPromise;
  }

  async stopImpl() {
    let stopFailure;
    if (this.child.exitCode === null) {
      try {
        await this.call("shutdown", {}, 30_000);
      } catch (error) {
        stopFailure = error instanceof Error
          ? error
          : new Error(String(error));
      }
      this.child.stdin.end();
      const stopped = await Promise.race([
        this.exited.then(() => true),
        sleep(5_000).then(() => false),
      ]);
      if (!stopped) {
        throw new Error(
          `worker ${this.processLabel} did not exit after graceful shutdown`,
        );
      }
    }
    if (!this.stderr.writableEnded) this.stderr.end();
    await this.stderrFinished;
    await cleanupOwnedWorkerProcesses(this);
    if (stopFailure) throw stopFailure;
  }
}

async function invoke(
  worker,
  name,
  args = [],
  expect = undefined,
  timeout = 120_000,
) {
  const result = await worker.call(
    "invoke",
    {
      name,
      args,
      expect,
      timeout,
    },
    timeout + 30_000,
  );
  const timing = validateObservationTimingEnvelope(
    result,
    `Palace invocation ${name}`,
  );
  const evidence = {
    receipt: String(result.receipt),
    elapsedMs: timing.elapsedMs,
  };
  Object.defineProperty(evidence, "lezState", {
    value: String(result.lezState ?? ""),
    enumerable: false,
  });
  Object.defineProperty(evidence, "observationTiming", {
    value: Object.freeze({
      startedAtUnixMs: timing.startedAtUnixMs,
      completedAtUnixMs: timing.completedAtUnixMs,
    }),
    enumerable: false,
  });
  return evidence;
}

function parseRevisions() {
  const encoded = process.env.PALACE_GATE4_DEPENDENCY_REVISIONS;
  if (!encoded) return {};
  try {
    return JSON.parse(encoded);
  } catch {
    throw new Error("PALACE_GATE4_DEPENDENCY_REVISIONS is invalid");
  }
}

function validateGate3ReleasePreflight(evidence, live) {
  const now = Date.now();
  if (
    evidence?.schema !== "logos.palace.release-preflight"
    || evidence.version !== 1
    || evidence.status !== "passed"
    || !Number.isSafeInteger(evidence.startedAtUnixMs)
    || !Number.isSafeInteger(evidence.completedAtUnixMs)
    || evidence.startedAtUnixMs <= 0
    || evidence.completedAtUnixMs < evidence.startedAtUnixMs
    || evidence.completedAtUnixMs > now + 5 * 60_000
    || evidence.rootAccountBeforeWrites?.state !== "uninitialized"
    || !exactStableJson(evidence.release, {
      programIdHex: palaceRelease.programIdHex,
      programBytecodeSha256: palaceRelease.programBytecodeSha256,
      programByteLength: palaceRelease.programByteLength,
      deploymentTransactionHash:
        palaceRelease.deploymentTransactionHash,
      rootAccountIdHex: palaceRelease.rootAccountIdHex,
      rootAccountIdBase58: palaceRelease.rootAccountIdBase58,
    })
  ) {
    throw new Error("Gate 3 release preflight is invalid");
  }
  const deploymentFields = [
    "explorerOrigin",
    "path",
    "blockId",
    "blockHash",
    "transactionHash",
    "byteLength",
    "bytecodeSha256",
    "sha256",
    "risc0ImageIdHex",
    "programIdHex",
    "bedrockStatus",
  ];
  const rootFields = [
    "state",
    "explorerOrigin",
    "path",
    "accountIdBase58",
    "programOwner",
    "balance",
    "nonce",
    "dataBytes",
    "dataSha256",
    "responseSha256",
  ];
  const select = (value, fields) => Object.fromEntries(
    fields.map((field) => [field, value?.[field]]),
  );
  const exactRootAccountMatch = exactStableJson(
    select(evidence.rootAccountBeforeWrites, rootFields),
    select(live.rootAccountBeforeWrites, rootFields),
  );
  const rootAdvancedByGate4 =
    evidence.rootAccountBeforeWrites?.state === "uninitialized"
    && live.rootAccountBeforeWrites?.state === "initialized"
    && evidence.rootAccountBeforeWrites?.accountIdBase58
      === live.rootAccountBeforeWrites?.accountIdBase58;
  if (
    !exactStableJson(
      select(evidence.programDeployment, deploymentFields),
      select(live.programDeployment, deploymentFields),
    )
    || (!exactRootAccountMatch && !rootAdvancedByGate4)
    || live.completedAtUnixMs < evidence.completedAtUnixMs
  ) {
    throw new Error(
      "Gate 4 release revalidation differs from pre-write Gate 3 evidence",
    );
  }
  return {
    status: "passed",
    gate3CompletedAtUnixMs: evidence.completedAtUnixMs,
    gate4CompletedAtUnixMs: live.completedAtUnixMs,
    gate3AgeAtRevalidationMs:
      live.completedAtUnixMs - evidence.completedAtUnixMs,
    exactDeploymentMatch: true,
    exactRootAccountMatch,
    rootAdvancedByGate4,
  };
}

function exactObjectKeys(value, keys) {
  return (
    value !== null
    && typeof value === "object"
    && !Array.isArray(value)
    && Object.keys(value).sort().join(",") === [...keys].sort().join(",")
  );
}

function validElapsedEvidence(value, expectedReceipt) {
  return (
    exactObjectKeys(value, ["receipt", "elapsedMs"])
    && expectedReceipt(value.receipt)
    && Number.isSafeInteger(value.elapsedMs)
    && value.elapsedMs >= 0
    && value.elapsedMs <= 180_000
  );
}

function parseActivePropProjection(
  value,
  description,
  expectedAvailable = true,
) {
  let projection;
  try {
    projection = typeof value === "string"
      ? JSON.parse(value)
      : value;
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
    || !isHex64(projection.handle)
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

function validateGate3AssetAuthoring(gate3, catalogById) {
  const authoring = gate3?.assetAuthoring;
  if (
    !exactObjectKeys(
      authoring,
      [
        "version",
        "phase",
        "inputManifest",
        "selectedAssetCount",
        "propStory",
        "boundary",
        "guardedBeforeApproval",
        "assets",
        "graphBindings",
        "activePropProjection",
        "elapsedMs",
        "catalogCount",
        "assignments",
      ],
    )
    || authoring.version !== 1
    || authoring.phase !== "complete"
    || !exactObjectKeys(
      authoring.inputManifest,
      ["schema", "version", "sha256", "assetCount"],
    )
    || authoring.inputManifest.schema
      !== "logos.palace.e2e-asset-inputs"
    || authoring.inputManifest.version !== 1
    || !isHex64(authoring.inputManifest.sha256)
    || !Number.isSafeInteger(authoring.inputManifest.assetCount)
    || authoring.inputManifest.assetCount < 2
    || authoring.inputManifest.assetCount > 128
    || authoring.selectedAssetCount !== authoring.inputManifest.assetCount
    || !["requested", "not-requested"].includes(authoring.propStory)
    || authoring.boundary
      !== "operator-selected bounded PNG bytes -> verified handle -> approval"
        + " -> local-byte-verified Storage CID -> manifest assignment"
    || !validElapsedEvidence(
      authoring.guardedBeforeApproval,
      (receipt) => receipt === "rejected=asset-not-approved",
    )
    || !Number.isSafeInteger(authoring.elapsedMs)
    || authoring.elapsedMs < 0
    || authoring.elapsedMs > 30 * 60_000
    || !Number.isSafeInteger(authoring.catalogCount)
    || authoring.catalogCount < authoring.selectedAssetCount
    || !exactObjectKeys(authoring.assignments, ["rooms", "prop"])
    || !exactObjectKeys(authoring.assignments.rooms, ["atrium", "lounge"])
    || Object.values(authoring.assignments.rooms).some(
      (handle) => !isHex64(handle),
    )
    || (
      authoring.assignments.prop !== null
      && (
        !exactObjectKeys(
          authoring.assignments.prop,
          ["propId", "handle", "anchorX", "anchorY", "layer"],
        )
        || !isHex64(authoring.assignments.prop.handle)
        || !/^[a-z][a-z0-9_-]{0,63}$/.test(
          authoring.assignments.prop.propId,
        )
        || !Number.isSafeInteger(authoring.assignments.prop.anchorX)
        || authoring.assignments.prop.anchorX < 0
        || !Number.isSafeInteger(authoring.assignments.prop.anchorY)
        || authoring.assignments.prop.anchorY < 0
        || !["head", "body", "hand", "back"].includes(
          authoring.assignments.prop.layer,
        )
      )
    )
    || !Array.isArray(authoring.assets)
    || authoring.assets.length !== authoring.selectedAssetCount
  ) {
    throw new Error("Gate 3 asset authoring envelope is invalid");
  }
  const assetGraphTargets = expectedAssetGraphTargets(
    authoring.assignments.prop?.propId ?? null,
  );

  const byAssetId = {};
  for (const actual of authoring.assets) {
    const baseKeys = [
      "assetId",
      "label",
      "file",
      "handle",
      "width",
      "height",
      "byteLength",
      "role",
      "chunkBytes",
      "chunkCount",
      "begin",
      "appends",
      "commit",
      "review",
      "publication",
      "cid",
    ];
    const expectedKeys = !Object.hasOwn(actual, "target")
      ? baseKeys
      : [...baseKeys, "target", "assignment"];
    const beginFields = statusFields(actual.begin?.receipt);
    const commitReceipt =
      `ok;handle=${actual.handle};width=${actual.width};`
      + `height=${actual.height};bytes=${actual.byteLength}`;
    if (
      !exactObjectKeys(actual, expectedKeys)
      || typeof actual.assetId !== "string"
      || !/^[a-z][a-z0-9_-]{0,63}$/.test(actual.assetId)
      || typeof actual.file !== "string"
      || !/^[a-z0-9][a-z0-9._-]{0,127}\.png$/.test(actual.file)
      || actual.label !== (
        actual.file.length > 32 ? "selected-image.png" : actual.file
      )
      || !isHex64(actual.handle)
      || !Number.isSafeInteger(actual.width)
      || actual.width <= 0
      || !Number.isSafeInteger(actual.height)
      || actual.height <= 0
      || !Number.isSafeInteger(actual.byteLength)
      || actual.byteLength <= 0
      || actual.byteLength > 10 * 1024 * 1024
      || !["room-background", "prop-image"].includes(actual.role)
      || (
        Object.hasOwn(actual, "target")
        && (
          !actual.target
          || actual.target.kind !== actual.role
          || (
            actual.role === "room-background"
            && (
              !exactObjectKeys(actual.target, ["kind", "roomId"])
              || !["atrium", "lounge"].includes(actual.target.roomId)
            )
          )
          || (
            actual.role === "prop-image"
            && (
              !exactObjectKeys(
                actual.target,
                ["kind", "propId", "anchorX", "anchorY", "layer"],
              )
              || !/^[a-z][a-z0-9_-]{0,63}$/.test(actual.target.propId)
              || !Number.isSafeInteger(actual.target.anchorX)
              || actual.target.anchorX < 0
              || !Number.isSafeInteger(actual.target.anchorY)
              || actual.target.anchorY < 0
              || !["head", "body", "hand", "back"].includes(
                actual.target.layer,
              )
            )
          )
        )
      )
      || actual.chunkBytes !== 32 * 1024
      || !Number.isSafeInteger(actual.chunkCount)
      || actual.chunkCount <= 0
      || !validElapsedEvidence(
        actual.begin,
        (receipt) => receipt.startsWith("ok;session="),
      )
      || !/^[0-9a-f]{32}$/.test(beginFields.session)
      || beginFields.next !== "0"
      || beginFields.maxChunkBytes !== String(32 * 1024)
      || beginFields.maxTotalBytes !== String(10 * 1024 * 1024)
      || !Array.isArray(actual.appends)
      || actual.appends.length !== actual.chunkCount
      || !validElapsedEvidence(
        actual.commit,
        (receipt) => receipt === commitReceipt,
      )
      || !validElapsedEvidence(
        actual.review,
        (receipt) =>
          receipt === `ok;handle=${actual.handle};review=approved`,
      )
      || !exactObjectKeys(
        actual.publication,
        ["dispatched", "completed"],
      )
      || !validElapsedEvidence(
        actual.publication.dispatched,
        (receipt) => receipt === "ok;asset=publishing",
      )
      || !validElapsedEvidence(
        actual.publication.completed,
        (receipt) => receipt === `published;cid=${actual.cid}`,
      )
      || !validStorageCid(actual.cid)
    ) {
      throw new Error(
        `Gate 3 asset authoring fixture is invalid: ${actual?.assetId}`,
      );
    }
    let appendedBytes = 0;
    for (let index = 0; index < actual.appends.length; index += 1) {
      const append = actual.appends[index];
      appendedBytes += append?.byteLength ?? 0;
      if (
        !exactObjectKeys(
          append,
          ["sequence", "byteLength", "receipt", "elapsedMs"],
        )
        || append.sequence !== index
        || !Number.isSafeInteger(append.byteLength)
        || append.byteLength <= 0
        || append.byteLength > actual.chunkBytes
        || !validElapsedEvidence(
          { receipt: append.receipt, elapsedMs: append.elapsedMs },
          (receipt) =>
            receipt
              === `ok;session=${beginFields.session};next=${index + 1};`
                + `bytes=${appendedBytes}`,
        )
      ) {
        throw new Error(
          `Gate 3 asset append is invalid: ${actual.assetId}/${index}`,
        );
      }
    }
    if (appendedBytes !== actual.byteLength) {
      throw new Error(
        `Gate 3 asset byte total is invalid: ${actual.assetId}`,
      );
    }
    if (
      !Object.hasOwn(actual, "target")
        ? Object.hasOwn(actual, "assignment")
        : (
            actual.target.kind === "room-background"
              ? !validElapsedEvidence(
                  actual.assignment,
                  (receipt) =>
                    receipt
                      === `ok;room=${actual.target.roomId};`
                        + `handle=${actual.handle}`,
                )
              : !validElapsedEvidence(
                  actual.assignment,
                  (receipt) =>
                    receipt
                      === `ok;propId=${actual.target.propId};`
                        + `handle=${actual.handle};`
                        + `anchorX=${actual.target.anchorX};`
                        + `anchorY=${actual.target.anchorY};`
                        + `layer=${actual.target.layer}`,
                )
          )
    ) {
      throw new Error(
        `Gate 3 asset assignment is invalid: ${actual.assetId}`,
      );
    }
    byAssetId[actual.assetId] = actual;
  }
  if (
    Object.keys(byAssetId).length !== authoring.selectedAssetCount
    || new Set(authoring.assets.map(({ handle }) => handle)).size
      !== authoring.selectedAssetCount
    || new Set(authoring.assets.map(({ cid }) => cid)).size
      !== authoring.selectedAssetCount
  ) {
    throw new Error("Gate 3 asset fixtures are not unique");
  }

  const assignedFixtures = authoring.assets.filter(
    (asset) => Object.hasOwn(asset, "target"),
  );
  const assignedRooms = assignedFixtures.filter(
    ({ target }) => target.kind === "room-background",
  );
  const assignedProps = assignedFixtures.filter(
    ({ target }) => target.kind === "prop-image",
  );
  const propRequested = authoring.assignments.prop !== null;
  const activePropProjection = parseActivePropProjection(
    authoring.activePropProjection,
    "Gate 3 active prop projection",
    propRequested,
  );
  const finalRoomAssignments = { atrium: "", lounge: "" };
  for (const asset of assignedRooms) {
    finalRoomAssignments[asset.target.roomId] = asset.handle;
  }
  if (
    assignedFixtures.length !== authoring.assets.length
    || authoring.propStory
      !== (propRequested ? "requested" : "not-requested")
    || assignedRooms.length < 2
    || assignedProps.length !== (propRequested ? 1 : 0)
    || new Set(assignedRooms.map(({ target }) => target.roomId)).size !== 2
    || !exactJson(authoring.assignments.rooms, finalRoomAssignments)
    || (
      propRequested
      && (
        authoring.assignments.prop.propId
          !== assignedProps[0].target.propId
        || authoring.assignments.prop.handle !== assignedProps[0].handle
        || authoring.assignments.prop.anchorX
          !== assignedProps[0].target.anchorX
        || authoring.assignments.prop.anchorY
          !== assignedProps[0].target.anchorY
        || authoring.assignments.prop.layer !== assignedProps[0].target.layer
        || activePropProjection.propId !== assignedProps[0].target.propId
        || activePropProjection.handle !== assignedProps[0].handle
        || activePropProjection.width !== assignedProps[0].width
        || activePropProjection.height !== assignedProps[0].height
        || activePropProjection.anchorX !== assignedProps[0].target.anchorX
        || activePropProjection.anchorY !== assignedProps[0].target.anchorY
        || activePropProjection.layer !== assignedProps[0].target.layer
        || catalogById[
          `prop-${authoring.assignments.prop.propId}-image`
        ]?.contentSha256 !== activePropProjection.contentSha256
      )
    )
  ) {
    throw new Error("Gate 3 asset manifest assignments are invalid");
  }

  if (
    !Array.isArray(authoring.graphBindings)
    || authoring.graphBindings.length
      !== assetGraphTargets.length
  ) {
    throw new Error("Gate 3 asset graph bindings are missing");
  }
  for (
    let index = 0;
    index < assetGraphTargets.length;
    ++index
  ) {
    const expected = assetGraphTargets[index];
    const actual = authoring.graphBindings[index];
    const publishedObject = catalogById[expected.objectId];
    const authoredAsset = authoring.assets.reduce(
      (selected, candidate) =>
        candidate.target?.kind === expected.kind
        && (
          expected.kind !== "room-background"
          || candidate.target.roomId === expected.targetId
        )
          ? candidate
          : selected,
      undefined,
    );
    const expectedKeys = expected.kind === "room-background"
      ? [
          "kind",
          "targetId",
          "objectId",
          "assetId",
          "assignment",
          "cid",
          "contentSha256",
        ]
      : [
          "kind",
          "objectId",
          "assetId",
          "assignment",
          "cid",
          "contentSha256",
        ];
    if (
      !exactObjectKeys(actual, expectedKeys)
      || actual.kind !== expected.kind
      || (
        expected.kind === "room-background"
        && actual.targetId !== expected.targetId
      )
      || actual.assetId !== authoredAsset?.assetId
      || !exactJson(actual.assignment, authoredAsset?.target)
      || actual.objectId !== expected.objectId
      || actual.cid !== authoredAsset?.cid
      || actual.contentSha256 !== authoredAsset?.handle
      || publishedObject?.cid !== actual.cid
      || publishedObject?.contentSha256 !== actual.contentSha256
      || !validStorageCid(actual.cid)
    ) {
      throw new Error(
        `Gate 3 active asset graph binding is invalid: ${expected.kind}`,
      );
    }
  }

  const screenshot = gate3?.assetAuthoringScreenshot;
  const render = screenshot?.renderEvidence;
  const screenshotFile = "gate3-admin-assets-published.png";
  if (
    !exactObjectKeys(
      screenshot,
      [
        "file",
        "artifactPath",
        "width",
        "height",
        "byteLength",
        "sha256",
        "stage",
        "state",
        "label",
        "renderEvidence",
      ],
    )
    || screenshot.file !== screenshotFile
    || screenshot.artifactPath !== screenshotFile
    || screenshot.width !== 1600
    || screenshot.height !== 900
    || !Number.isSafeInteger(screenshot.byteLength)
    || screenshot.byteLength < 24
    || screenshot.byteLength > 64 * 1024 * 1024
    || !isHex64(screenshot.sha256)
    || screenshot.stage !== "gate3-admin-asset-authoring"
    || screenshot.state
      !== "admin-selected-assets-approved-published-assigned"
    || screenshot.label !== "a"
    || !exactObjectKeys(
      render,
      [
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
      ],
    )
    || render.schema !== "logos.palace.asset-authoring-render"
    || render.version !== 1
    || render.open !== true
    || render.cardCount < authoring.selectedAssetCount
    || render.readyImageCount !== render.cardCount
    || render.publishedCount < authoring.selectedAssetCount
    || render.atriumAssigned !== true
    || render.loungeAssigned !== true
    || render.propAssigned !== propRequested
    || !Number.isSafeInteger(render.fenceRequest)
    || render.fenceRequest <= 0
    || render.fenceState !== "complete"
    || !Number.isSafeInteger(render.fenceFrame)
    || render.fenceFrame < 0
    || !Number.isSafeInteger(render.epoch)
    || render.epoch < authoring.selectedAssetCount
  ) {
    throw new Error(
      "Gate 3 asset authoring screenshot or render fence is invalid",
    );
  }

  const assetBindings = authoring.assets.map(
    ({ role, handle, cid, target }) => ({
      role,
      handle,
      cid,
      assigned: target !== undefined,
    }),
  );
  return {
    status: "passed",
    version: 1,
    selectedAssetCount: authoring.selectedAssetCount,
    propStory: authoring.propStory,
    manifestSha256: authoring.inputManifest.sha256,
    approvalGuardReceipt: authoring.guardedBeforeApproval.receipt,
    assetBindingsSha256:
      sha256(JSON.stringify(stableJson(assetBindings))),
    activeGraphBindings: authoring.graphBindings,
    activePropProjection,
    screenshot: {
      file: screenshot.file,
      width: screenshot.width,
      height: screenshot.height,
      byteLength: screenshot.byteLength,
      sha256: screenshot.sha256,
      renderEvidence: render,
    },
    evidenceSha256: sha256(
      JSON.stringify(stableJson({
        assetAuthoring: authoring,
        assetAuthoringScreenshot: screenshot,
      })),
    ),
  };
}

function validateGate3(gate3, currentHashes, basecampDigest) {
  if (
    gate3?.schema !== "logos.palace.basecamp-gate3-report" ||
    gate3.version !== 1 ||
    gate3.status !== "passed" ||
    gate3.fullGate3 !== "passed"
  ) {
    throw new Error("Gate 3 report is not a passing full Gate 3 report");
  }
  for (const [field, mode] of [
    ["providerBFetch", "network"],
    ["coldCFetch", "network"],
    ["providerBCachedFetch", "cache"],
    ["coldCCachedFetch", "cache"],
  ]) {
    if (
      gate3[field]?.mode !== mode
      || !Number.isSafeInteger(gate3[field]?.endToEndMs)
      || gate3[field].endToEndMs < 0
    ) {
      throw new Error(`Gate 3 ${field} lacks exact ${mode} timing evidence`);
    }
  }
  if (
    gate3.productSnapshot !== process.env.PALACE_PRODUCT_SNAPSHOT ||
    !String(gate3.productSnapshot).startsWith("/nix/store/") ||
    gate3.sourceCommit !== process.env.PALACE_SOURCE_COMMIT ||
    gate3.productSnapshotNarHash
      !== process.env.PALACE_PRODUCT_SNAPSHOT_NAR_HASH
    || gate3.productSnapshotNarSize
      !== Number(process.env.PALACE_PRODUCT_SNAPSHOT_NAR_SIZE ?? Number.NaN)
    || gate3.snapshotRunnerSha256
      !== process.env.PALACE_MVP_RUNNER_SHA256
    || gate3.runtimeOutputManifestSha256
      !== process.env.PALACE_RUNTIME_OUTPUT_MANIFEST_SHA256
  ) {
    throw new Error("Gate 3 product snapshot does not match Gate 4");
  }
  if (
    gate3.basecampRevision !== process.env.PALACE_BASECAMP_REV ||
    gate3.basecampBinarySha256 !== basecampDigest
  ) {
    throw new Error("Gate 3 Basecamp revision or binary does not match");
  }
  if (
    !exactJson(
      canonicalHashes(gate3.packageHashes),
      canonicalHashes(currentHashes),
    )
  ) {
    throw new Error("Gate 3 package hashes do not match Gate 4");
  }
  const propId =
    gate3?.assetAuthoring?.assignments?.prop?.propId ?? null;
  const objectContract = expectedGraphObjectContract(propId);
  const expectedObjectIds =
    objectContract.map(([objectId]) => objectId);
  const objects = gate3?.publication?.objects;
  if (!Array.isArray(objects) || objects.length !== expectedObjectIds.length) {
    throw new Error("Gate 3 report does not contain the exact MVP catalog");
  }
  const byId = Object.fromEntries(
    objects.map((object) => [object?.objectId, object]),
  );
  if (
    Object.keys(byId).length !== expectedObjectIds.length ||
    expectedObjectIds.some((objectId, index) => {
      const object = byId[objectId];
      return (
        !object ||
        object.type !== objectContract[index][1] ||
        typeof object.mediaType !== "string" ||
        object.mediaType.length === 0 ||
        typeof object.cid !== "string" ||
        object.cid.length < 4 ||
        object.cid.length > 128 ||
        !Number.isSafeInteger(object.byteLength) ||
        object.byteLength <= 0 ||
        object.byteLength > 10 * 1024 * 1024 ||
        !isHex64(object.contentSha256)
      );
    })
  ) {
    throw new Error("Gate 3 catalog object IDs or fields are invalid");
  }
  const ordered = expectedObjectIds.map((objectId) => byId[objectId]);
  const prefix =
    "logos-palace-mvp-storage-catalog-v1\n"
    + "version=1\n"
    + "root=palace-1\n"
    + `objects=${ordered.length}\n`
    + ordered
      .map(
        (object) =>
          `object=${object.objectId};${object.type};${object.mediaType};`
          + `${object.cid};${object.byteLength};${object.contentSha256}\n`,
      )
      .join("");
  const checksum = sha256(prefix);
  if (gate3?.publication?.checksum !== checksum) {
    throw new Error("Gate 3 catalog checksum cannot be reconstructed exactly");
  }
  const canonical = `${prefix}checksum=${checksum}\n`;
  const encoded = Buffer.from(canonical, "utf8").toString("base64url");
  if (
    Buffer.from(encoded, "base64url").toString("utf8") !== canonical
    || encoded.includes("=")
  ) {
    throw new Error("Gate 3 catalog base64url reconstruction failed");
  }
  const assetAuthoringEvidence =
    validateGate3AssetAuthoring(gate3, byId);
  return {
    byId,
    ordered,
    checksum,
    canonical,
    encoded,
    propId,
    propObjectIds: propId === null
      ? null
      : {
          image: `prop-${propId}-image`,
          metadata: `prop-${propId}-metadata`,
          manifest: `prop-${propId}`,
        },
    assetAuthoringEvidence,
  };
}

function profile(identity) {
  return {
    display_name: identity.display,
    delivery_key_hex: identity.deliveryKey,
    key_epoch: "1",
    avatar_manifest_cid: null,
  };
}

function buildPlan(identities, catalog, propObjectId) {
  const propRequested = propObjectId !== null;
  const propId = propRequested
    ? propObjectId.slice("prop-".length)
    : null;
  if (propRequested && !catalog[propObjectId]) {
    throw new Error("requested prop graph object is missing");
  }
  const doorActionId = propRequested ? "10" : "9";
  const ids = {
    palace: stableId("palace"),
    ownerGrant: stableId("grant/alice-owner"),
    atrium: stableId("room/atrium"),
    lounge: stableId("room/lounge"),
    bobGrant: stableId("grant/bob/capabilities-15"),
    door: stableId("shared/atrium/door-open"),
    carolBan: humanModerationBanId(
      "user",
      "8",
      identities.b.accountId,
      identities.c.accountId,
    ),
    ...(propRequested
      ? {
          propBan: humanModerationBanId(
            "asset",
            "9",
            identities.b.accountId,
            catalog[propObjectId].cid,
          ),
        }
      : {}),
  };
  const doorValueHex = Buffer.from("0", "utf8").toString("hex");
  const doorStateRoot = sha256("door_open=0");
  const openedDoorValueHex = Buffer.from("1", "utf8").toString("hex");
  const openedDoorStateRoot = sha256("door_open=1");
  const room = (title, manifest) => ({
    title,
    manifest_cid: catalog[manifest].cid,
    script_bundle_cid: catalog["script-door"].cid,
    vm_profile: "iptscrae_mvp_v1",
  });
  const definitions = [
    {
      actionId: "0",
      caller: "a",
      kind: "initialize",
      transition: {
        kind: "initialize",
        palace_id_hex: ids.palace,
        title: "Logos Palace Gate 4",
        active_manifest_cid: catalog["palace-1"].cid,
        owner_profile: profile(identities.a),
        owner_grant_id_hex: ids.ownerGrant,
        entry_room_id_hex: ids.atrium,
        entry_room: room("Atrium", "room-atrium"),
        secondary_room_id_hex: ids.lounge,
        secondary_room: room("Lounge", "room-lounge"),
      },
    },
    {
      actionId: "1",
      caller: "b",
      kind: "register_user",
      transition: { kind: "register_user", profile: profile(identities.b) },
    },
    {
      actionId: "2",
      caller: "c",
      kind: "register_user",
      transition: { kind: "register_user", profile: profile(identities.c) },
    },
    {
      actionId: "3",
      caller: "a",
      kind: "grant_capability",
      transition: {
        kind: "grant_capability",
        grant_id_hex: ids.bobGrant,
        subject_user_id_hex: identities.b.accountId,
        scope: { kind: "palace" },
        capabilities: "15",
        delegable: false,
        valid_through_action_id: "100",
      },
    },
    {
      actionId: "4",
      caller: "a",
      kind: "create_shared_state",
      transition: {
        kind: "create_shared_state",
        grant_id_hex: ids.ownerGrant,
        shared_state_id_hex: ids.door,
        room_id_hex: ids.atrium,
        key: "door_open",
        value_hex: doorValueHex,
        state_root_hex: doorStateRoot,
      },
    },
    {
      actionId: "5",
      caller: "b",
      kind: "set_room_locked",
      transition: {
        kind: "set_room_locked",
        grant_id_hex: ids.bobGrant,
        room_id_hex: ids.lounge,
        locked: true,
      },
    },
    {
      actionId: "6",
      caller: "b",
      kind: "set_room_locked",
      transition: {
        kind: "set_room_locked",
        grant_id_hex: ids.bobGrant,
        room_id_hex: ids.lounge,
        locked: false,
      },
    },
    {
      actionId: "7",
      caller: "b",
      kind: "update_shared_state",
      transition: {
        kind: "update_shared_state",
        grant_id_hex: ids.bobGrant,
        shared_state_id_hex: ids.door,
        room_id_hex: ids.atrium,
        state_revision: "2",
        value_hex: doorValueHex,
        state_root_hex: doorStateRoot,
      },
    },
    {
      actionId: "8",
      caller: "b",
      kind: "create_user_ban",
      transition: {
        kind: "create_user_ban",
        grant_id_hex: ids.bobGrant,
        ban_id_hex: ids.carolBan,
        subject_user_id_hex: identities.c.accountId,
        scope: { kind: "palace" },
      },
    },
    ...(propRequested
      ? [{
          actionId: "9",
          caller: "b",
          kind: "create_asset_ban",
          transition: {
            kind: "create_asset_ban",
            grant_id_hex: ids.bobGrant,
            ban_id_hex: ids.propBan,
            cid: catalog[propObjectId].cid,
            scope: { kind: "palace" },
          },
        }]
      : []),
    {
      actionId: doorActionId,
      caller: "b",
      kind: "update_shared_state",
      transition: {
        kind: "update_shared_state",
        grant_id_hex: ids.bobGrant,
        shared_state_id_hex: ids.door,
        room_id_hex: ids.atrium,
        state_revision: "3",
        value_hex: openedDoorValueHex,
        state_root_hex: openedDoorStateRoot,
      },
    },
  ];
  const actions = definitions.map((definition) => {
    const transitionJson = JSON.stringify(definition.transition);
    return {
      ...definition,
      transitionJson,
      transitionSha256: sha256(transitionJson),
    };
  });
  const fingerprintFor = (selected) => sha256(
    JSON.stringify({
      version: 2,
      programId: releaseProgramId,
      rootAccountId: releaseRootId,
      palaceId: ids.palace,
      actions: selected.map(
        ({ actionId, caller, transitionSha256 }) => ({
          actionId,
          caller,
          transitionSha256,
        }),
      ),
    }),
  );
  const fingerprint = fingerprintFor(actions);
  const gate4Fingerprint = fingerprintFor(actions.slice(0, -1));
  const legacyGate4Fingerprint = sha256(
    JSON.stringify({
      version: 1,
      programId: releaseProgramId,
      rootAccountId: releaseRootId,
      palaceId: ids.palace,
      actions: actions.slice(0, -1).map(
        ({ actionId, caller, transitionSha256 }) => ({
          actionId,
          caller,
          transitionSha256,
        }),
      ),
    }),
  );
  return {
    version: 2,
    propStory: propRequested ? "requested" : "not-requested",
    propId,
    doorActionId,
    fingerprint,
    gate4Fingerprint,
    legacyGate4Fingerprint,
    ids,
    palaceUri: `palace://${ids.palace}`,
    programId: releaseProgramId,
    rootAccountId: releaseRootId,
    doorState: {
      key: "door_open",
      valueHex: doorValueHex,
      stateRootHex: doorStateRoot,
      openedValueHex: openedDoorValueHex,
      openedStateRootHex: openedDoorStateRoot,
      openedRevision: "3",
    },
    actions,
  };
}

function publicPlan(plan) {
  return {
    version: plan.version,
    propStory: plan.propStory,
    propId: plan.propId,
    doorActionId: plan.doorActionId,
    fingerprint: plan.fingerprint,
    gate4Fingerprint: plan.gate4Fingerprint,
    legacyGate4Fingerprint: plan.legacyGate4Fingerprint,
    ids: plan.ids,
    palaceUri: plan.palaceUri,
    programId: plan.programId,
    rootAccountId: plan.rootAccountId,
    doorState: plan.doorState,
    actions: plan.actions.map(
      ({ actionId, caller, kind, transitionSha256 }) => ({
        actionId,
        caller,
        kind,
        transitionSha256,
      }),
    ),
  };
}

function validatePreviousReportEnvelope(
  previous,
  currentHashes,
  basecampDigest,
  gate3Digest,
  dependencyRevisions,
) {
  const schemaAccepted =
    (
      previous?.schema === "logos.palace.basecamp-gate4-6-report"
      && previous.version === 2
    )
    || (
      previous?.schema === "logos.palace.basecamp-gate4-report"
      && previous.version === 1
    );
  if (
    !schemaAccepted
    || previous.productSnapshot !== process.env.PALACE_PRODUCT_SNAPSHOT
    || previous.sourceCommit !== process.env.PALACE_SOURCE_COMMIT
    || previous.productSnapshotNarHash
      !== process.env.PALACE_PRODUCT_SNAPSHOT_NAR_HASH
    || previous.productSnapshotNarSize
      !== Number(process.env.PALACE_PRODUCT_SNAPSHOT_NAR_SIZE ?? Number.NaN)
    || previous.snapshotRunnerSha256
      !== process.env.PALACE_MVP_RUNNER_SHA256
    || previous.runtimeOutputManifestSha256
      !== process.env.PALACE_RUNTIME_OUTPUT_MANIFEST_SHA256
    || !exactJson(previous.dependencyRevisions, dependencyRevisions)
    || previous.basecampRevision !== process.env.PALACE_BASECAMP_REV
    || previous.basecampBinarySha256 !== basecampDigest
    || !exactJson(
      canonicalHashes(previous.packageHashes),
      canonicalHashes(currentHashes),
    )
    || previous?.gate3?.reportSha256 !== gate3Digest
  ) {
    throw new Error(
      "prior Gate 4 report does not match the immutable run inputs",
    );
  }
  if (
    previous.status === "passed"
    || previous.fullGate4 === "passed"
    || previous.fullGate6 === "passed"
  ) {
    throw new Error(
      "passing prior Gate 4 report must be validated and skipped by the immutable runner",
    );
  }
  if (
    !Array.isArray(previous.actions)
    || (
      Array.isArray(previous.plan?.actions)
      && previous.actions.length > previous.plan.actions.length
    )
    || new Set(previous.actions.map(({ actionId }) => actionId)).size
      !== previous.actions.length
    || previous.actions.some(
      (action) =>
        !action
        || !/^(?:0|[1-9][0-9]*)$/.test(String(action.actionId))
        || !["a", "b", "c"].includes(action.caller)
        || !isHex64(action.transitionSha256),
    )
    || (
      previous.actions.length > 0
      && (
        !previous.plan
        || !isHex64(previous.plan.fingerprint)
      )
    )
  ) {
    throw new Error("prior Gate 4 action evidence is structurally invalid");
  }
  if (
    previous.identities
    && (
      Array.isArray(previous.identities)
      || typeof previous.identities !== "object"
      || Object.entries(previous.identities).some(
        ([label, identity]) =>
          !labels.includes(label)
          || !isHex64(identity?.accountId)
          || !isHex64(identity?.deliveryKey)
          || identity?.display !== displayNames[label],
      )
    )
  ) {
    throw new Error("prior Gate 4 identity evidence is structurally invalid");
  }
  if (
    previous.screenshots !== undefined
    && (
      !Array.isArray(previous.screenshots)
      || previous.screenshots.length > 16
      || previous.screenshots.some(
        (entry) =>
          !entry
          || basename(String(entry.file ?? "")) !== entry.file
          || !/^[a-z0-9][a-z0-9._-]{0,127}\.png$/.test(entry.file)
          || !isHex64(entry.sha256)
          || !labels.includes(entry.label)
          || typeof entry.stage !== "string"
          || typeof entry.state !== "string",
      )
    )
  ) {
    throw new Error("prior Gate 4 screenshot evidence is structurally invalid");
  }
}

function validatePriorCreatorOfflineEvidence(previous, plan, creatorPid) {
  const lifecycle = previous?.gate6?.lifecycle;
  const creator = previous?.gate6?.creator;
  const convergence = previous?.gate5?.convergence;
  const authorityBundles = convergence?.authorityBundles;
  const doorCheckpoint = Number(plan.doorActionId);
  const checkpointReceipt =
    previous?.checkpoints?.[`action${plan.doorActionId}A`]
      ?.status?.receipt;
  if (
    !["creator-stop-pending", "creator-offline"].includes(lifecycle?.phase)
    || lifecycle.creatorPid !== creatorPid
    || lifecycle.actionCheckpoint !== doorCheckpoint
    || creator?.label !== "a"
    || creator.pid !== creatorPid
    || (
      lifecycle.phase === "creator-offline"
      && creator.offline !== true
    )
    || convergence?.status !== "passed"
    || convergence.checkpoint !== doorCheckpoint
    || convergence.sharedRevision !== plan.doorState.openedRevision
    || convergence.sharedStateRoot !== plan.doorState.openedStateRootHex
    || !isHex64(convergence.authorityProjectionDigest)
    || !authorityBundles
    || Object.keys(authorityBundles).sort().join(",") !== "a,b,c"
    || Object.values(authorityBundles).some(
      (bundle) =>
        bundle?.sha256 !== convergence.authorityProjectionDigest,
    )
    || typeof checkpointReceipt !== "string"
    || !checkpointReceipt.includes(`action=${plan.doorActionId}`)
  ) {
    throw new Error("creator-offline resume evidence is not exact");
  }
}

const currentPackageHashes = await packageHashes(lgxDir);
const currentBasecampDigest = await sha256File(basecamp);
const gate3ReportDigest = await sha256File(gate3ReportPath);
const dependencyRevisions = parseRevisions();
const lezModuleRevision = dependencyRevisions?.lez_core?.revision;
if (
  typeof lezModuleRevision !== "string"
  || !/^[0-9a-f]{40}$/.test(lezModuleRevision)
  || lezModuleRevision !== approvedLezModuleRevision
) {
  throw new Error("Gate 4 LEZ module revision is not approved");
}
const previousReport = await optionalJson(reportPath);
if (previousReport) {
  validatePreviousReportEnvelope(
    previousReport,
    currentPackageHashes,
    currentBasecampDigest,
    gate3ReportDigest,
    dependencyRevisions,
  );
}
const runStartedAt = performance.now();
const report = {
  schema: "logos.palace.basecamp-gate4-6-report",
  version: 2,
  status: "running",
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
  dependencyRevisions,
  basecampRevision: process.env.PALACE_BASECAMP_REV ?? "unknown",
  basecampBinarySha256: currentBasecampDigest,
  packageHashes: currentPackageHashes,
  gate3: {
    ...(previousReport?.gate3 ?? {}),
    reportSha256: gate3ReportDigest,
    reportPath: gate3ReportPath,
    productSnapshot: previousReport?.gate3?.productSnapshot,
    catalogChecksum: previousReport?.gate3?.catalogChecksum,
  },
  releaseContract: previousReport?.releaseContract,
  release: {
    programDeployment: undefined,
    rootAccountBeforeWrites: undefined,
  },
  catalog: previousReport?.catalog,
  installedPackages: { ...(previousReport?.installedPackages ?? {}) },
  installedRoots: { ...(previousReport?.installedRoots ?? {}) },
  inspectorPorts: {},
  startup: { ...(previousReport?.startup ?? {}) },
  identities: { ...(previousReport?.identities ?? {}) },
  detectedFinalizedPrefix: previousReport?.detectedFinalizedPrefix,
  plan: previousReport?.plan,
  actions: [...(previousReport?.actions ?? [])],
  actionJournals: previousReport?.actionJournals ?? {},
  checkpoints: { ...(previousReport?.checkpoints ?? {}) },
  storage: previousReport?.storage ?? {},
  delivery: previousReport?.delivery ?? {},
  moderation: previousReport?.moderation ?? {},
  applicationRoundTrip: previousReport?.applicationRoundTrip,
  gate5: previousReport?.gate5 ?? {},
  gate6: previousReport?.gate6 ?? {},
  frameTiming: previousReport?.frameTiming ?? {
    measurementContract: undefined,
    runs: {},
  },
  screenshots: [...(previousReport?.screenshots ?? [])],
  uiEvidence: {
    pending: [...(previousReport?.uiEvidence?.pending ?? [])],
    finalized: [...(previousReport?.uiEvidence?.finalized ?? [])],
    degraded: [...(previousReport?.uiEvidence?.degraded ?? [])],
    offline: [...(previousReport?.uiEvidence?.offline ?? [])],
  },
  failureEvidence: {
    ...(previousReport?.failureEvidence ?? {}),
  },
  restart: previousReport?.restart ?? {},
  processModel: {
    standalonePalaceServer: undefined,
    loaderSelection: undefined,
    runtimeArtifacts: undefined,
    observationMethod:
      "bounded /proc Basecamp descendant status, executable, and cmdline inventory",
    observations: [],
  },
  noPalaceServer: "running",
  fullGate4: "running",
  fullGate5: "running",
  fullGate6: "running",
  cleanup: { status: "pending", failures: [] },
  timings: {},
  failures: [],
};

let reportWrite = Promise.resolve();
function checkpointReport() {
  reportWrite = reportWrite.then(async () => {
    await durableReplace(
      reportPath,
      `${JSON.stringify(report, null, 2)}\n`,
    );
  });
  return reportWrite;
}

const workers = new Map();
let failure;
let phase = "preflight";
let gate3IdentityEvidence = {};
let processRuntimeArtifacts;
let processLoaderSelection;
let expectedProcessArtifactsByLabel = new Map();
let terminationSignal;
let terminationPromise;

function requestTermination(signal) {
  if (terminationPromise) return;
  terminationSignal = signal;
  failure ??= new Error(`Gate 4 termination requested by ${signal}`);
  terminationPromise = (async () => {
    report.status = "failed";
    report.fullGate4 = "failed";
    report.fullGate5 = "failed";
    report.fullGate6 = "failed";
    report.failures.push({
      phase,
      message: `terminated by ${signal}`,
    });
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

function safeSyncRejection(receipt) {
  return /^rejected=lez-sync;reason=(current-height-failed|last-synced-height-failed|chunk-failed|chunk-progress-mismatch|terminal-height-mismatch)$/.test(
    receipt,
  );
}

async function startLez(worker, attempts) {
  const password = stableId(`wallet-password/${worker.label}`);
  const deadline = Date.now() + lezStartupTimeoutMs;
  let lastReceipt = "";
  while (Date.now() < deadline) {
    const result = await invoke(
      worker,
      "gate4StartLez",
      [password],
      currentLezStateExpectation(releaseProgramId),
      lezStartupTimeoutMs,
    );
    const { lezState, ...actionResult } = result;
    lastReceipt = actionResult.receipt;
    attempts.push(actionResult);
    if (
      lastReceipt.startsWith("ok;") &&
      isCurrentLezState(lastReceipt, releaseProgramId)
    ) {
      return { receipt: lastReceipt, fields: statusFields(lastReceipt) };
    }
    if (
      lastReceipt.length === 0
      && isCurrentLezState(lezState, releaseProgramId)
    ) {
      return {
        receipt: lastReceipt,
        fields: statusFields(lezState),
        lezStateObservation: {
          source: "gate4LezState",
          receipt: lezState,
        },
      };
    }
    if (!safeSyncRejection(lastReceipt)) {
      throw new Error(`LEZ start ${worker.label} rejected: ${lastReceipt}`);
    }
    await sleep(1_000);
  }
  throw new Error(`LEZ start ${worker.label} timed out: ${lastReceipt}`);
}

function parseIdentity(receipt) {
  const fields = statusFields(receipt);
  if (
    !isHex64(fields.identity) ||
    !isHex64(fields.delivery_key) ||
    fields.key_epoch !== "1" ||
    !Object.values(displayNames).includes(fields.display)
  ) {
    throw new Error(`invalid identity receipt: ${receipt}`);
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

async function identityStatus(worker) {
  return invoke(worker, "gate4RefreshIdentity", [], undefined, 30_000);
}

async function ensureIdentity(worker, expectedIdentity) {
  const refreshed = await identityStatus(worker);
  if (statusFields(refreshed.receipt).identity === "none") {
    throw new Error(
      `Gate 3 production identity ${worker.label} is not persisted`,
    );
  }
  const receipt = refreshed.receipt;
  const identity = parseIdentity(receipt);
  if (
    identity.accountId !== expectedIdentity.accountId
    || identity.deliveryKey !== expectedIdentity.deliveryKey
    || identity.display !== expectedIdentity.display
  ) {
    throw new Error(
      `persisted identity ${worker.label} differs from Gate 3`,
    );
  }
  if (
    statusFields(receipt).registration !== "submitted"
    || identity.registrationTransaction
      !== expectedIdentity.registrationTransaction
    || identity.accountId !== expectedIdentity.accountId
    || identity.deliveryKey !== expectedIdentity.deliveryKey
    || identity.display !== expectedIdentity.display
  ) {
    throw new Error(
      `identity ${worker.label} registration differs from Gate 3`,
    );
  }
  return { identity, receipt, existing: true };
}

async function palaceStatus(worker) {
  const result = await invoke(
    worker,
    "gate4PalaceStatus",
    [],
    undefined,
    30_000,
  );
  return { ...result, fields: statusFields(result.receipt) };
}

async function waitPalaceTerminal(worker, palaceUri, allowUninitialized) {
  const opened = await invoke(
    worker,
    "gate4OpenPalace",
    [palaceUri],
    undefined,
    120_000,
  );
  if (opened.receipt.startsWith("rejected=")) {
    throw new Error(`open Palace ${worker.label}: ${opened.receipt}`);
  }
  const deadline = Date.now() + 10 * 60_000;
  let last;
  while (Date.now() < deadline) {
    last = await palaceStatus(worker);
    if (last.fields.palace === "open") {
      const action = Number(last.fields.action);
      if (
        last.fields.id !== palaceUri.slice("palace://".length) ||
        !Number.isSafeInteger(action) ||
        action < 0
      ) {
        throw new Error(`invalid Palace checkpoint: ${last.receipt}`);
      }
      return { state: "open", action, opened, status: last };
    }
    if (
      allowUninitialized &&
      last.fields.palace === "degraded" &&
      last.fields.reason === "palace-initialize-not-found"
    ) {
      return { state: "uninitialized", action: -1, opened, status: last };
    }
    if (
      last.fields.palace === "rejected" ||
      last.fields.palace === "degraded"
    ) {
      throw new Error(`Palace history ${worker.label}: ${last.receipt}`);
    }
    await sleep(500);
  }
  throw new Error(
    `Palace history ${worker.label} timed out: ${last?.receipt ?? "none"}`,
  );
}

async function openExact(worker, plan, expectedAction) {
  const current = await palaceStatus(worker);
  if (
    current.fields.palace === "open" &&
    current.fields.id === plan.ids.palace &&
    current.fields.action === String(expectedAction)
  ) {
    return { reused: true, status: current };
  }
  await startLez(worker, []);
  const opened = await waitPalaceTerminal(worker, plan.palaceUri, false);
  if (opened.action !== expectedAction) {
    throw new Error(
      `Palace ${worker.label} checkpoint ${opened.action}, expected ${expectedAction}`,
    );
  }
  return { reused: false, ...opened };
}

async function validateResumeEvidence(prefix, plan) {
  if (prefix < 0) return [];
  const priorFingerprint = previousReport?.plan?.fingerprint;
  if (
    previousReport?.plan
    && ![
        plan.fingerprint,
        plan.gate4Fingerprint,
        plan.legacyGate4Fingerprint,
      ].includes(priorFingerprint)
  ) {
    throw new Error(
      `finalized Palace prefix ${prefix} exists without matching Gate 4 plan evidence`,
    );
  }
  if (
    !previousReport
    && !/^\/var\/tmp\/logos-palace-[0-9]+\/active-[0-9a-f]{64}-[0-9a-f]{64}\.json$/.test(
      process.env.PALACE_MVP_CLAIM_PATH ?? "",
    )
  ) {
    throw new Error(
      "finalized Palace prefix lacks verified persistent active-run claim",
    );
  }
  const resumed = [];
  for (let index = 0; index <= prefix; index += 1) {
    const expected = plan.actions[index];
    const prior = previousReport.actions?.find(
      ({ actionId }) => actionId === expected.actionId,
    );
    const journal = await actionJournalEvidence(
      expected.caller,
      expected.actionId,
    );
    if (
      (
        prior
        && (
          prior?.transitionSha256 !== expected.transitionSha256
          || prior.kind !== expected.kind
          || prior.caller !== expected.caller
        )
      )
      || (
        isHex64(prior?.transactionHash)
        && prior.transactionHash !== journal.transactionHash
      )
    ) {
      throw new Error(
        `finalized action ${index} lacks exact prior Gate 4 evidence`,
      );
    }
    const priorTiming = resumableTimings(prior);
    const resumedRecord = {
      ...(prior ?? {
        actionId: expected.actionId,
        kind: expected.kind,
        caller: expected.caller,
        callerAccountId:
          report.identities[expected.caller].accountId,
        transitionSha256: expected.transitionSha256,
        recoveredFromDeterministicPlanJournalAndHistory: true,
      }),
      callerAccountId: report.identities[expected.caller].accountId,
      transitionSha256: expected.transitionSha256,
      transactionHash: journal.transactionHash,
      journal,
      status: "finalized",
      durableStatus: "finalized",
      finalStatus:
        prior?.finalStatus
        ?? "recovered=journal-and-finalized-history;durable=finalized",
      timings: priorTiming.timings,
      timingMeasurement: priorTiming.measurement,
      resumedFromFinalizedHistory: true,
    };
    recoverPersistedTimingEvidence(
      resumedRecord,
      "finalized",
      prior ?? resumedRecord,
    );
    finalizeLezTimingEvidence(resumedRecord);
    resumed.push(resumedRecord);
  }
  return resumed;
}

function submissionCanRetry(receipt) {
  return (
    receipt === "rejected=lez-submit;reason=module-rejected" ||
    receipt === "rejected=lez-current-root-call" ||
    receipt ===
      "rejected=lez-current-root;reason=invalid-account-response" ||
    safeSyncRejection(receipt)
  );
}

function observationCanRetry(receipt) {
  return (
    /^rejected=lez-stable-account-read;reason=(sync-(current-height-failed|last-synced-height-failed|chunk-failed|chunk-progress-mismatch|terminal-height-mismatch)|height-before|wallet-height-raced|account-[0-9]+|height-after|unstable-height)$/.test(
      receipt,
    ) ||
    receipt === "rejected=lez-observation;reason=observation-mismatch" ||
    receipt ===
      "rejected=lez-observation;reason=invalid-account-response" ||
    /^rejected=lez-finality-expectation;reason=invalid-account-snapshot:invalid-account-response$/.test(
      receipt,
    )
  );
}

async function actionStatus(worker, actionId) {
  const result = await invoke(
    worker,
    "gate4ActionStatus",
    [actionId],
    undefined,
    30_000,
  );
  const evidence = {
    ...result,
    fields: statusFields(result.receipt),
  };
  Object.defineProperty(evidence, "observationTiming", {
    value: result.observationTiming,
    enumerable: false,
  });
  return evidence;
}

async function executeAction(worker, action, plan, priorRecord) {
  const startedAt = performance.now();
  const priorTiming = resumableTimings(priorRecord);
  let status = await actionStatus(worker, action.actionId);
  let transactionHash = priorRecord?.transactionHash;
  let recoveredJournal;
  const expectedJournalStages = {
    submitted_to_lez: 2,
    observed: 3,
    finalized: 4,
  };
  if (Object.hasOwn(expectedJournalStages, status.fields.durable)) {
    recoveredJournal = await actionJournalRecord(
      action.caller,
      action.actionId,
    );
    if (
      recoveredJournal.stage
        !== expectedJournalStages[status.fields.durable]
      || !isHex64(recoveredJournal.transactionHash)
      || (
        status.fields.durable === "finalized"
        && recoveredJournal.deliveryPublished
      )
      || (
        isHex64(transactionHash)
        && transactionHash !== recoveredJournal.transactionHash
      )
    ) {
      throw new Error(
        `action ${action.actionId} status/journal evidence mismatch`,
      );
    }
    transactionHash = recoveredJournal.transactionHash;
  }
  const record = {
    actionId: action.actionId,
    kind: action.kind,
    caller: action.caller,
    callerAccountId: report.identities[action.caller].accountId,
    transitionSha256: action.transitionSha256,
    submissionMethod: priorRecord?.submissionMethod ?? null,
    transactionHash,
    status: "running",
    recoveredJournal,
    submitAttempts: [...(priorRecord?.submitAttempts ?? [])],
    observeAttempts: [...(priorRecord?.observeAttempts ?? [])],
    reconcileAttempts: [...(priorRecord?.reconcileAttempts ?? [])],
    timings: priorTiming.timings,
    timingMeasurement: priorTiming.measurement,
    timingBoundaries: {
      ...(priorRecord?.timingBoundaries ?? {}),
    },
  };
  report.actions = report.actions.filter(
    ({ actionId }) => actionId !== action.actionId,
  );
  report.actions.push(record);
  report.actions.sort(
    (left, right) => Number(left.actionId) - Number(right.actionId),
  );
  await checkpointReport();

  const unavailableTiming = recoverPersistedTimingEvidence(
    record,
    status.fields.durable,
    priorRecord ?? record,
  );
  if (
    priorRecord
    && status.fields.durable === "local_draft"
  ) {
    for (const field of ["submitMs", "totalMs"]) {
      const started =
        record.timingBoundaries[timingBoundaryNames(field).started];
      if (!Number.isSafeInteger(started) || started <= 0) {
        unavailableTiming.push(field);
        markRecoveredTimingUnmeasured(record, field);
      }
    }
  }
  const uniqueUnavailableTiming = [...new Set(unavailableTiming)];
  if (uniqueUnavailableTiming.length > 0) {
    await checkpointReport();
    throw new Error(
      `action ${action.actionId} cannot recover exact timing: ${uniqueUnavailableTiming.join(",")}`,
    );
  }

  if (status.fields.durable === "finalized") {
    if (!isHex64(record.transactionHash)) {
      throw new Error(
        `finalized local action ${action.actionId} lacks transaction evidence`,
      );
    }
    record.status = "finalized";
    record.durableStatus = "finalized";
    record.resumed = true;
    record.journal = recoveredJournal;
    record.finalStatus = status.receipt;
    record.recoveryMs = Math.round(performance.now() - startedAt);
    if (
      lezStageTimingFields.some(
        (field) => !isCompleteTimingMeasurement(
          field,
          record.timingMeasurement[field],
          record.timings[field],
        ),
      )
    ) {
      throw new Error(
        `finalized action ${action.actionId} lacks persisted measured timings`,
      );
    }
    finalizeLezTimingEvidence(record);
    await checkpointReport();
    return record;
  }

  if (
    status.fields.durable === "local_draft" ||
    status.fields.durable === "queued"
  ) {
    const deadline = Date.now() + 10 * 60_000;
    while (Date.now() < deadline) {
      let submissionName = "gate4Submit";
      let submissionArguments = [
        action.actionId,
        plan.rootAccountId,
        report.identities[action.caller].accountId,
        plan.programId,
        action.transitionJson,
      ];
      if (action.kind === "create_user_ban") {
        submissionName = "gate4BanUser";
        submissionArguments = [
          action.transition.subject_user_id_hex,
        ];
      } else if (action.kind === "create_asset_ban") {
        submissionName = "gate4BanProp";
        submissionArguments = [plan.propId];
      }
      const submitted = await invoke(
        worker,
        submissionName,
        submissionArguments,
        undefined,
        120_000,
      );
      startTimingBoundary(
        record,
        "submitMs",
        submitted.observationTiming.startedAtUnixMs,
      );
      startTimingBoundary(
        record,
        "totalMs",
        submitted.observationTiming.startedAtUnixMs,
      );
      record.submissionMethod = submissionName;
      record.submitAttempts.push(submitted);
      await checkpointReport();
      const submittedFields = statusFields(submitted.receipt);
      if (
        submitted.receipt.startsWith("ok;") &&
        submittedFields.durable === "submitted_to_lez" &&
        isHex64(submittedFields.tx_hash)
      ) {
        if (
          action.kind === "create_user_ban"
          || action.kind === "create_asset_ban"
        ) {
          const expectedKind =
            action.kind === "create_user_ban" ? "user" : "prop";
          if (
            submittedFields.moderation !== expectedKind
            || submittedFields.action !== action.actionId
            || submittedFields.ban_id
              !== action.transition.ban_id_hex
            || submittedFields.transition_sha256
              !== action.transitionSha256
          ) {
            throw new Error(
              `typed moderation action ${action.actionId} evidence mismatch`,
            );
          }
        }
        completeTimingBoundary(
          record,
          "submitMs",
          submitted.observationTiming.completedAtUnixMs,
        );
        startTimingBoundary(
          record,
          "observeMs",
          submitted.observationTiming.completedAtUnixMs,
        );
        const persisted = await actionJournalRecord(
          action.caller,
          action.actionId,
        );
        if (
          persisted.stage !== 2
          || persisted.transactionHash !== submittedFields.tx_hash
        ) {
          throw new Error(
            `action ${action.actionId} submitted journal mismatch`,
          );
        }
        record.transactionHash = submittedFields.tx_hash;
        record.recoveredJournal = persisted;
        record.status = "submitted";
        await checkpointReport();
        break;
      }
      if (!submissionCanRetry(submitted.receipt)) {
        throw new Error(
          `action ${action.actionId} submit rejected: ${submitted.receipt}`,
        );
      }
      status = await actionStatus(worker, action.actionId);
      if (status.fields.durable !== "queued") {
        throw new Error(
          `action ${action.actionId} retry not queued: ${status.receipt}`,
        );
      }
      await startLez(worker, []);
      await sleep(1_000);
    }
    if (!isHex64(record.transactionHash)) {
      throw new Error(`action ${action.actionId} submission timed out`);
    }
  } else if (
    status.fields.durable !== "submitted_to_lez" &&
    status.fields.durable !== "observed"
  ) {
    throw new Error(
      `action ${action.actionId} cannot resume: ${status.receipt}`,
    );
  } else if (!isHex64(record.transactionHash)) {
    throw new Error(
      `action ${action.actionId} resumed without transaction evidence`,
    );
  }
  if (
    status.fields.durable === "submitted_to_lez"
    || status.fields.durable === "observed"
  ) {
    startTimingBoundary(
      record,
      "observeMs",
      completedTimingTimestamp(record, "submitMs"),
    );
  }
  if (status.fields.durable === "observed") {
    startTimingBoundary(
      record,
      "finalityMs",
      completedTimingTimestamp(record, "observeMs"),
    );
  }

  let finalizedAtUnixMs;
  status = await actionStatus(worker, action.actionId);
  if (
    ["observed", "finalized"].includes(status.fields.durable)
    && !isCompleteTimingMeasurement(
      "observeMs",
      record.timingMeasurement.observeMs,
      record.timings.observeMs,
    )
  ) {
    completeTimingBoundary(
      record,
      "observeMs",
      status.observationTiming.completedAtUnixMs,
    );
    if (status.fields.durable === "finalized") {
      setCoalescedFinalityTiming(
        record,
        status.observationTiming.completedAtUnixMs,
      );
    } else {
      startTimingBoundary(
        record,
        "finalityMs",
        status.observationTiming.completedAtUnixMs,
      );
    }
  }
  if (status.fields.durable === "finalized") {
    if (
      !isCompleteTimingMeasurement(
        "finalityMs",
        record.timingMeasurement.finalityMs,
        record.timings.finalityMs,
      )
    ) {
      completeTimingBoundary(
        record,
        "finalityMs",
        status.observationTiming.completedAtUnixMs,
      );
    }
    finalizedAtUnixMs = status.observationTiming.completedAtUnixMs;
  }
  if (status.fields.durable === "submitted_to_lez") {
    await checkpointReport();
    const deadline = Date.now() + 10 * 60_000;
    while (Date.now() < deadline) {
      await startLez(worker, []);
      const observed = await invoke(
        worker,
        "gate4Observe",
        [action.actionId],
        undefined,
        120_000,
      );
      record.observeAttempts.push(observed);
      const fields = statusFields(observed.receipt);
      if (
        observed.receipt.startsWith("ok;") &&
        (fields.durable === "observed" || fields.durable === "finalized")
      ) {
        completeTimingBoundary(
          record,
          "observeMs",
          observed.observationTiming.completedAtUnixMs,
        );
        if (fields.durable === "finalized") {
          setCoalescedFinalityTiming(
            record,
            observed.observationTiming.completedAtUnixMs,
          );
          finalizedAtUnixMs =
            observed.observationTiming.completedAtUnixMs;
        } else {
          startTimingBoundary(
            record,
            "finalityMs",
            observed.observationTiming.completedAtUnixMs,
          );
        }
        record.status = fields.durable;
        await checkpointReport();
        break;
      }
      if (!observationCanRetry(observed.receipt)) {
        throw new Error(
          `action ${action.actionId} observe rejected: ${observed.receipt}`,
        );
      }
      await sleep(1_000);
    }
    status = await actionStatus(worker, action.actionId);
    if (
      status.fields.durable !== "observed" &&
      status.fields.durable !== "finalized"
    ) {
      throw new Error(
        `action ${action.actionId} observation timed out: ${status.receipt}`,
      );
    }
    if (
      status.fields.durable === "finalized"
      && finalizedAtUnixMs === undefined
    ) {
      if (
        !isCompleteTimingMeasurement(
          "finalityMs",
          record.timingMeasurement.finalityMs,
          record.timings.finalityMs,
        )
      ) {
        completeTimingBoundary(
          record,
          "finalityMs",
          status.observationTiming.completedAtUnixMs,
        );
      }
      finalizedAtUnixMs = status.observationTiming.completedAtUnixMs;
    }
  }

  if (status.fields.durable !== "finalized") {
    startTimingBoundary(
      record,
      "finalityMs",
      completedTimingTimestamp(record, "observeMs"),
    );
    await checkpointReport();
    const deadline = Date.now() + 15 * 60_000;
    while (Date.now() < deadline) {
      status = await actionStatus(worker, action.actionId);
      if (status.fields.durable === "finalized") break;
      if (status.fields.durable !== "observed") {
        throw new Error(
          `action ${action.actionId} finality stage: ${status.receipt}`,
        );
      }
      const reconciled = await invoke(
        worker,
        "gate4Reconcile",
        [action.actionId],
        undefined,
        120_000,
      );
      record.reconcileAttempts.push(reconciled);
      if (
        reconciled.receipt.startsWith("rejected=") &&
        !observationCanRetry(reconciled.receipt)
      ) {
        throw new Error(
          `action ${action.actionId} reconcile rejected: ${reconciled.receipt}`,
        );
      }
      await sleep(500);
    }
    if (status.fields.durable !== "finalized") {
      status = await actionStatus(worker, action.actionId);
    }
    if (status.fields.durable !== "finalized") {
      throw new Error(
        `action ${action.actionId} finality timed out: ${status.receipt}`,
      );
    }
    completeTimingBoundary(
      record,
      "finalityMs",
      status.observationTiming.completedAtUnixMs,
    );
    finalizedAtUnixMs = status.observationTiming.completedAtUnixMs;
  }
  if (!Number.isSafeInteger(finalizedAtUnixMs)) {
    throw new Error(
      `action ${action.actionId} lacks exact finalized response timestamp`,
    );
  }
  completeTimingBoundary(record, "totalMs", finalizedAtUnixMs);
  record.status = "finalized";
  record.durableStatus = "finalized";
  record.finalStatus = status.receipt;
  const finalizedJournal = await actionJournalEvidence(
    action.caller,
    action.actionId,
  );
  if (finalizedJournal.transactionHash !== record.transactionHash) {
    throw new Error(
      `action ${action.actionId} finalized transaction changed`,
    );
  }
  record.journal = finalizedJournal;
  finalizeLezTimingEvidence(record);
  await checkpointReport();
  return record;
}

async function waitFor(
  check,
  {
    timeout = 120_000,
    interval = 250,
    description = "condition",
  } = {},
) {
  const deadline = Date.now() + timeout;
  let lastError = new Error(`${description} not observed`);
  while (Date.now() < deadline) {
    try {
      return await check();
    } catch (error) {
      lastError = error instanceof Error ? error : new Error(String(error));
      await sleep(interval);
    }
  }
  try {
    return await check();
  } catch (error) {
    const finalError = error instanceof Error ? error : lastError;
    throw new Error(`${description}: ${finalError.message}`);
  }
}

async function storageBundleStatus(worker) {
  return invoke(
    worker,
    "gate3BundleStatus",
    [],
    undefined,
    30_000,
  );
}

async function startProductionStorage(worker, config) {
  const startedAt = performance.now();
  const start = await invoke(
    worker,
    "gate3StartStorage",
    [config],
    { prefix: "ok;" },
    120_000,
  );
  const running = await waitFor(
    async () => {
      const result = await invoke(
        worker,
        "gate3StorageStatus",
        [],
        undefined,
        30_000,
      );
      if (
        !result.receipt.includes("storage=running")
        || !result.receipt.includes("callback_registration=ready")
        || !result.receipt.includes("reconciliation_required=0")
      ) {
        throw new Error(result.receipt);
      }
      return result;
    },
    {
      timeout: 180_000,
      description: `production Storage ${worker.label}`,
    },
  );
  return {
    start,
    running,
    elapsedMs: Math.round(performance.now() - startedAt),
  };
}

async function fetchProductionCatalog(worker, catalog) {
  const expectedObjectCount = catalog.ordered.length;
  const startedAt = performance.now();
  const before = await storageBundleStatus(worker);
  const beforeFields = statusFields(before.receipt);
  let dispatched;
  let mode;
  let nativeSource;
  let nativeAvailable;
  let nativeTotal;
  const bindNativePreflight = (fields, receipt) => {
    const available = Number(fields.native_available);
    const total = Number(fields.native_total);
    if (
      !Number.isSafeInteger(available)
      || !Number.isSafeInteger(total)
      || available < 0
      || total !== expectedObjectCount
      || available > total
      || (
        fields.source === "cache"
          ? available !== total
          : fields.source === "network"
            ? available >= total
            : true
      )
    ) {
      throw new Error(
        `Storage ${worker.label} invalid native preflight: ${receipt}`,
      );
    }
    return { available, total };
  };
  if (
    beforeFields.state === "verified"
    && beforeFields.catalog === catalog.encoded
    && beforeFields.verified === String(expectedObjectCount)
  ) {
    mode = beforeFields.source;
    nativeSource = beforeFields.source;
    if (!["cache", "network"].includes(mode)) {
      throw new Error(
        `Storage ${worker.label} verified state omitted fetch source`,
      );
    }
    ({ available: nativeAvailable, total: nativeTotal } =
      bindNativePreflight(beforeFields, before.receipt));
    dispatched = {
      receipt: before.receipt,
      elapsedMs: 0,
      reusedVerifiedCatalog: true,
    };
  } else {
    if (beforeFields.state !== "missing") {
      throw new Error(
        `Storage ${worker.label} catalog not resumable: ${before.receipt}`,
      );
    }
    dispatched = await invoke(
      worker,
      "gate3FetchBundle",
      [catalog.encoded],
      { prefix: "ok;" },
      120_000,
    );
    if (
      !dispatched.receipt.includes("state=fetching")
      && !dispatched.receipt.includes("state=verified")
    ) {
      throw new Error(
        `Storage ${worker.label} omitted fetching state: ${dispatched.receipt}`,
      );
    }
    const dispatchedFields = statusFields(dispatched.receipt);
    if (!["cache", "network"].includes(dispatchedFields.source)) {
      throw new Error(
        `Storage ${worker.label} omitted exact fetch source: ${dispatched.receipt}`,
      );
    }
    mode = dispatchedFields.source;
    nativeSource = dispatchedFields.source;
    ({ available: nativeAvailable, total: nativeTotal } =
      bindNativePreflight(dispatchedFields, dispatched.receipt));
  }
  const completed = await waitFor(
    async () => {
      const result = await storageBundleStatus(worker);
      const fields = statusFields(result.receipt);
      if (
        fields.state !== "verified"
        || fields.published !== String(expectedObjectCount)
        || fields.verified !== String(expectedObjectCount)
        || fields.total !== String(expectedObjectCount)
        || fields.catalog !== catalog.encoded
        || fields.source !== nativeSource
        || fields.native_available !== String(nativeAvailable)
        || fields.native_total !== String(nativeTotal)
      ) {
        throw new Error(result.receipt);
      }
      return result;
    },
    {
      timeout: 5 * 60_000,
      description: `exact Gate 3 catalog on ${worker.label}`,
    },
  );
  const objects = [];
  for (const object of catalog.ordered) {
    const observed = await invoke(
      worker,
      "gate3ObjectStatus",
      [object.objectId],
      undefined,
      30_000,
    );
    const fields = statusFields(observed.receipt);
    if (fields.state !== "verified" || fields.cid !== object.cid) {
      throw new Error(
        `Storage ${worker.label}/${object.objectId}: ${observed.receipt}`,
      );
    }
    objects.push({
      objectId: object.objectId,
      cid: object.cid,
      receipt: observed.receipt,
    });
  }
  const endToEndMs = Math.round(performance.now() - startedAt);
  const retentionStarted = performance.now();
  const retentionStart = await invoke(
    worker,
    "gate3VerifyRetention",
    [],
    { prefix: "ok;" },
    120_000,
  );
  const retention = await waitFor(
    async () => {
      const result = await storageBundleStatus(worker);
      const fields = statusFields(result.receipt);
      if (
        fields.state !== "verified"
        || fields.retention !== "verified"
        || fields.catalog !== catalog.encoded
      ) {
        throw new Error(result.receipt);
      }
      return result;
    },
    {
      timeout: 5 * 60_000,
      description: `retained Gate 3 catalog on ${worker.label}`,
    },
  );
  return {
    mode,
    nativeSource,
    nativeAvailable,
    nativeTotal,
    clock: "performance.now monotonic milliseconds",
    startBoundary: "immediately before catalog status read",
    endBoundary:
      "all exact catalog objects re-read as CID-verified after completion",
    before,
    dispatched,
    completed,
    objects,
    retentionStart,
    retention,
    endToEndMs,
    retentionMs: Math.round(performance.now() - retentionStarted),
  };
}

async function proveRecoverableMissingStorageObject(worker, catalog) {
  const object = catalog.byId["background-atrium"];
  if (
    object?.objectId !== "background-atrium"
    || !isHex64(object.contentSha256)
    || !Number.isSafeInteger(object.byteLength)
    || object.byteLength <= 0
    || catalog.ordered.some(
      ({ cid }) => cid === missingStorageFixtureCid,
    )
  ) {
    throw new Error("missing-object failure fixture is not exact");
  }
  const before = await invoke(
    worker,
    "gate3AssetStatus",
    [object.cid],
    undefined,
    30_000,
  );
  if (before.receipt !== "missing") {
    throw new Error(
      `missing-object fixture was not missing: ${before.receipt}`,
    );
  }
  const dispatched = await invoke(
    worker,
    "gate3FetchPng",
    [
      missingStorageFixtureCid,
      object.cid,
      object.byteLength,
      object.contentSha256,
      1600,
      900,
    ],
    { prefix: "ok;asset=fetching;" },
    120_000,
  );
  const degraded = await waitFor(
    async () => {
      const status = await invoke(
        worker,
        "gate3AssetStatus",
        [object.cid],
        undefined,
        30_000,
      );
      if (!status.receipt.startsWith("degraded;reason=")) {
        throw new Error(status.receipt);
      }
      return status;
    },
    {
      timeout: 3 * 60_000,
      description: "recoverable missing Storage object",
    },
  );
  return {
    status: "passed",
    objectId: object.objectId,
    missingSourceCid: missingStorageFixtureCid,
    derivativeCid: object.cid,
    expectedContentSha256: object.contentSha256,
    states: ["missing", "fetching", "degraded"],
    before,
    dispatched,
    degraded,
    recovery:
      "missing source is explicit; canonical catalog remains verified",
  };
}

function parseDeliveryStatus(encoded) {
  const fields = statusFields(encoded);
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
    fields[key] = Number(fields[key] ?? -1);
    if (!Number.isSafeInteger(fields[key]) || fields[key] < 0) {
      throw new Error(`invalid Delivery ${key}: ${encoded}`);
    }
  }
  return fields;
}

function parseJsonObject(encoded, description) {
  let parsed;
  try {
    parsed = JSON.parse(String(encoded));
  } catch {
    throw new Error(`${description} is not JSON`);
  }
  if (!parsed || Array.isArray(parsed) || typeof parsed !== "object") {
    throw new Error(`${description} is not an object`);
  }
  return parsed;
}

function parseProjection(encoded) {
  let parsed;
  try {
    parsed = JSON.parse(String(encoded));
  } catch {
    throw new Error("Delivery projection is not JSON");
  }
  if (!Array.isArray(parsed)) {
    throw new Error("Delivery projection is not an array");
  }
  return parsed;
}

async function deliverySnapshot(worker) {
  const properties = await worker.call("properties");
  return {
    status: parseDeliveryStatus(properties.gate2Status),
    projection: parseProjection(properties.gate2Projection),
    nodeEvidence: parseJsonObject(
      properties.gate2NodeEvidence,
      "Delivery node evidence",
    ),
    receipt: String(properties.gate2Receipt ?? ""),
  };
}

function peerIdFromEvidence(evidence) {
  const candidates = [];
  if (typeof evidence.peerId === "string") candidates.push(evidence.peerId);
  if (evidence.peerId && typeof evidence.peerId === "object") {
    candidates.push(
      evidence.peerId.peerId,
      evidence.peerId.value,
      evidence.peerId.id,
    );
  }
  const peerId = candidates.find(
    (value) => typeof value === "string" && value.length > 20,
  );
  if (!peerId) {
    throw new Error(`Delivery evidence lacks peer ID: ${JSON.stringify(evidence)}`);
  }
  return peerId;
}

function addressStrings(value) {
  if (typeof value === "string") {
    return value.split(",").map((entry) => entry.trim()).filter(Boolean);
  }
  if (Array.isArray(value)) return value.flatMap(addressStrings);
  if (value && typeof value === "object") {
    return Object.values(value).flatMap(addressStrings);
  }
  return [];
}

function loopbackEntryNode(evidence, port) {
  const peerId = peerIdFromEvidence(evidence);
  const addresses = addressStrings(evidence.multiaddresses);
  const found = addresses.some((address) => {
    const match = address.match(
      /^\/(?:ip4|ip6)\/[^/]+\/tcp\/([0-9]+)(?:\/|$)/,
    );
    return match && Number(match[1]) === port;
  });
  if (!found) {
    throw new Error(
      `Delivery evidence lacks TCP ${port}: ${JSON.stringify(addresses)}`,
    );
  }
  return `/ip4/127.0.0.1/tcp/${port}/p2p/${peerId}`;
}

function connectedPeerIds(evidence) {
  const value = evidence.connectedPeers;
  if (value && typeof value === "object" && !Array.isArray(value)) {
    return Object.keys(value);
  }
  const encoded = JSON.stringify(value ?? "");
  return [...encoded.matchAll(/1[2-9A-HJ-NP-Za-km-z]{30,}/g)]
    .map((match) => match[0]);
}

function deliveryConfig(label, port, entryNodes, entryLabel = "a") {
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

async function waitDeliveryOnline(worker) {
  return waitFor(
    async () => {
      const snapshot = await deliverySnapshot(worker);
      if (
        snapshot.status.state !== "online"
        || snapshot.status.callbacks !== "1"
        || snapshot.status.node_running !== "1"
        || snapshot.status.profile !== report.identities[worker.label].accountId
      ) {
        throw new Error(JSON.stringify(snapshot.status));
      }
      return snapshot;
    },
    {
      timeout: 180_000,
      description: `production Delivery ${worker.label} online`,
    },
  );
}

async function startDeliveryMesh(activeLabels, ports, entryLabel) {
  const startedAt = performance.now();
  const entryWorker = workers.get(entryLabel);
  const entryConfig = deliveryConfig(
    entryLabel,
    ports[entryLabel],
    [],
    entryLabel,
  );
  const entryStart = await invoke(
    entryWorker,
    "gate2Start",
    [entryConfig],
    { prefix: "ok;state=" },
    120_000,
  );
  const entryEvidence = await waitFor(
    async () => {
      const snapshot = await deliverySnapshot(entryWorker);
      if (snapshot.nodeEvidence.success !== true) {
        throw new Error(JSON.stringify(snapshot.nodeEvidence));
      }
      loopbackEntryNode(snapshot.nodeEvidence, ports[entryLabel]);
      return snapshot.nodeEvidence;
    },
    {
      timeout: 180_000,
      description: `Delivery entry ${entryLabel}`,
    },
  );
  const entryNode = loopbackEntryNode(
    entryEvidence,
    ports[entryLabel],
  );
  const peerLabels = activeLabels.filter((label) => label !== entryLabel);
  const peerStarts = await Promise.all(
    peerLabels.map((label) =>
      invoke(
        workers.get(label),
        "gate2Start",
        [deliveryConfig(label, ports[label], [entryNode], entryLabel)],
        { prefix: "ok;state=" },
        120_000,
      )),
  );
  const online = Object.fromEntries(
    await Promise.all(
      activeLabels.map(async (label) => [
        label,
        await waitDeliveryOnline(workers.get(label)),
      ]),
    ),
  );
  const peerIds = Object.fromEntries(
    activeLabels.map((label) => [
      label,
      peerIdFromEvidence(online[label].nodeEvidence),
    ]),
  );
  const topology = {};
  topology[entryLabel] = await waitFor(
    async () => {
      const snapshot = await deliverySnapshot(entryWorker);
      const connected = connectedPeerIds(snapshot.nodeEvidence);
      for (const label of peerLabels) {
        if (!connected.includes(peerIds[label])) {
          throw new Error(`${label} not connected`);
        }
      }
      return snapshot.nodeEvidence;
    },
    {
      timeout: 180_000,
      description: `Delivery ${entryLabel} topology`,
    },
  );
  for (const label of peerLabels) {
    topology[label] = await waitFor(
      async () => {
        const snapshot = await deliverySnapshot(workers.get(label));
        if (!connectedPeerIds(snapshot.nodeEvidence).includes(
          peerIds[entryLabel],
        )) {
          throw new Error(`${entryLabel} not connected`);
        }
        return snapshot.nodeEvidence;
      },
      {
        timeout: 180_000,
        description: `Delivery ${label} topology`,
      },
    );
  }
  return {
    entryLabel,
    entryNode,
    configs: Object.fromEntries(
      activeLabels.map((label) => [
        label,
        deliveryConfig(
          label,
          ports[label],
          label === entryLabel ? [] : [entryNode],
          entryLabel,
        ),
      ]),
    ),
    starts: {
      [entryLabel]: entryStart,
      ...Object.fromEntries(
        peerLabels.map((label, index) => [label, peerStarts[index]]),
      ),
    },
    online,
    peerIds,
    topology,
    elapsedMs: Math.round(performance.now() - startedAt),
  };
}

function validateDirectEntryNodeTopology(mesh) {
  if (
    mesh?.entryLabel !== "a"
    || typeof mesh.entryNode !== "string"
    || !/^\/ip4\/127\.0\.0\.1\/tcp\/[0-9]+\/p2p\/[1-9A-HJ-NP-Za-km-z]+$/.test(
      mesh.entryNode,
    )
    || !mesh.configs
    || Object.keys(mesh.configs).sort().join(",") !== "a,b,c"
  ) {
    throw new Error("production Delivery topology is not direct-entry-node");
  }
  const configs = Object.fromEntries(
    labels.map((label) => [
      label,
      parseJsonObject(
        mesh.configs[label],
        `production Delivery topology ${label}`,
      ),
    ]),
  );
  const expectedKeys = [
    "clusterId",
    "discv5Discovery",
    "entryNodes",
    "listenAddress",
    "logLevel",
    "mode",
    "nat",
    "nodekey",
    "numShardsInNetwork",
    "quicSupport",
    "relay",
    "store",
    "tcpPort",
    "websocketSupport",
  ].sort().join(",");
  for (const label of labels) {
    const config = configs[label];
    if (
      Object.keys(config).sort().join(",") !== expectedKeys
      || config.mode !== (label === "a" ? "Core" : "Edge")
      || config.relay !== true
      || config.store !== false
      || config.clusterId !== deliveryClusterId
      || config.numShardsInNetwork !== 1
      || config.listenAddress !== "127.0.0.1"
      || config.nat !== "extip:127.0.0.1"
      || config.discv5Discovery !== false
      || config.websocketSupport !== false
      || config.quicSupport !== false
      || config.logLevel !== "WARN"
      || !Array.isArray(config.entryNodes)
      || !Number.isSafeInteger(config.tcpPort)
      || config.tcpPort < 1024
      || config.tcpPort > 65535
      || !isHex64(config.nodekey)
      || (
        label === "a"
          ? config.entryNodes.length !== 0
          : (
              config.entryNodes.length !== 1
              || config.entryNodes[0] !== mesh.entryNode
            )
      )
    ) {
      throw new Error(
        `production Delivery ${label} topology contract differs`,
      );
    }
  }
  if (
    new Set(labels.map((label) => configs[label].tcpPort)).size !== 3
    || new Set(labels.map((label) => configs[label].nodekey)).size !== 3
  ) {
    throw new Error("production Delivery topology reuses port or node key");
  }
}

function projectionByUser(projection) {
  return Object.fromEntries(
    projection.map((participant) => [participant.userId, participant]),
  );
}

async function waitProjection(worker, expected, description) {
  return waitFor(
    async () => {
      const snapshot = await deliverySnapshot(worker);
      const byUser = projectionByUser(snapshot.projection);
      for (const [userId, fields] of Object.entries(expected)) {
        const participant = byUser[userId];
        if (!participant || participant.present !== true) {
          throw new Error(`missing present user ${userId}`);
        }
        for (const [field, value] of Object.entries(fields)) {
          if (!exactStableJson(participant[field], value)) {
            throw new Error(
              `${userId}.${field}=${JSON.stringify(participant[field])}`,
            );
          }
        }
      }
      return snapshot;
    },
    { timeout: 180_000, description },
  );
}

async function verifyInitialDeliveryLifecycle(propId) {
  const profiles = Object.fromEntries(
    labels.map((label) => [
      report.identities[label].accountId,
      { displayName: displayNames[label] },
    ]),
  );
  await Promise.all(
    labels.map((label) =>
      invoke(
        workers.get(label),
        "gate2RefreshPresence",
        [],
        { prefix: "ok;request=" },
        60_000,
      )),
  );
  const presence = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => [
        label,
        await waitProjection(
          workers.get(label),
          profiles,
          `${label} production presence`,
        ),
      ]),
    ),
  );
  const alice = report.identities.a.accountId;
  const bob = report.identities.b.accountId;
  const carol = report.identities.c.accountId;
  const move = await invoke(
    workers.get("a"),
    "gate2Move",
    [2400, 3600],
    { prefix: "ok;request=" },
    60_000,
  );
  const speech = await invoke(
    workers.get("b"),
    "gate2Say",
    ["Gate 4 production mesh"],
    { prefix: "ok;request=" },
    60_000,
  );
  const propRequested = propId !== null;
  const wear = propRequested
    ? await invoke(
        workers.get("c"),
        "gate2Wear",
        [propId],
        { prefix: "ok;request=" },
        60_000,
      )
    : null;
  const expectedProjection = {
    [alice]: { x: 2400, y: 3600 },
    [bob]: { speech: "Gate 4 production mesh" },
    ...(propRequested
      ? { [carol]: { props: [propId] } }
      : {}),
  };
  const converged = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => [
        label,
        await waitProjection(
          workers.get(label),
          expectedProjection,
          `${label} production move/speech`
            + (propRequested ? "/wear" : ""),
        ),
      ]),
    ),
  );
  return {
    status: "passed",
    acceptanceProfile: false,
    presence,
    move,
    speech,
    converged,
    propStory: propRequested ? "requested" : "not-requested",
    approvedPropVisible: propRequested,
    ...(propRequested ? { wear } : {}),
  };
}

async function removeApprovedPropAfterEvidence(propId) {
  const carol = report.identities.c.accountId;
  const remove = await invoke(
    workers.get("c"),
    "gate2Remove",
    [propId],
    { prefix: "ok;request=" },
    60_000,
  );
  const removed = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => [
        label,
        await waitProjection(
          workers.get(label),
          { [carol]: { props: [] } },
          `${label} production remove`,
        ),
      ]),
    ),
  );
  return { remove, removed };
}

async function waitRejectedIngress(
  receiver,
  baseline,
  counter,
  description,
) {
  return waitFor(
    async () => {
      const snapshot = await deliverySnapshot(receiver);
      if (
        snapshot.status.received_rejected
          !== baseline.status.received_rejected + 1
        || snapshot.status[counter] !== baseline.status[counter] + 1
        || !exactStableJson(snapshot.projection, baseline.projection)
      ) {
        throw new Error(
          `counters/projection=${JSON.stringify(snapshot.status)}`,
        );
      }
      return snapshot;
    },
    { timeout: 180_000, description },
  );
}

const deliveryCounterFields = [
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
];

function validateStoredDeliveryRejection(
  baseline,
  rejected,
  counter,
  description,
) {
  if (
    !baseline
    || !rejected
    || !Array.isArray(baseline.projection)
    || !Array.isArray(rejected.projection)
    || !baseline.status
    || !rejected.status
    || deliveryCounterFields.some(
      (field) =>
        !Number.isSafeInteger(baseline.status[field])
        || baseline.status[field] < 0
        || !Number.isSafeInteger(rejected.status[field])
        || rejected.status[field] < 0,
    )
    || rejected.status.received_rejected
      !== baseline.status.received_rejected + 1
    || rejected.status[counter] !== baseline.status[counter] + 1
    || !exactStableJson(rejected.projection, baseline.projection)
  ) {
    throw new Error(`${description} counters/projection are not exact`);
  }
}

function validatePriorUserBanEvidence(evidence) {
  if (
    evidence?.status !== "passed"
    || evidence.staleCheckpoint !== 7
    || evidence.finalizedCheckpoint !== 8
    || evidence.sender !== "c"
    || evidence.projectionMutation !== false
    || evidence.rejectionClass !== "other"
    || typeof evidence.signedSend?.receipt !== "string"
    || !evidence.signedSend.receipt.startsWith("ok;request=")
    || typeof evidence.signedSend.elapsedMs !== "number"
    || !Number.isFinite(evidence.signedSend.elapsedMs)
    || evidence.signedSend.elapsedMs < 0
  ) {
    throw new Error("prior user-ban Delivery evidence is not exact");
  }
  for (const label of ["a", "b"]) {
    validateStoredDeliveryRejection(
      evidence.receiverBaselines?.[label],
      evidence.receiverRejections?.[label],
      "rejected_other",
      `prior user-ban ${label}`,
    );
  }
}

function validatePriorAssetBanEvidence(evidence) {
  if (
    evidence?.status !== "passed"
    || evidence.staleCheckpoint !== 8
    || evidence.finalizedCheckpoint !== 9
    || evidence.sender !== "a"
    || evidence.projectionMutation !== false
    || evidence.rejectionClass !== "payload"
    || typeof evidence.signedSend?.receipt !== "string"
    || !evidence.signedSend.receipt.startsWith("ok;request=")
    || typeof evidence.signedSend.elapsedMs !== "number"
    || !Number.isFinite(evidence.signedSend.elapsedMs)
    || evidence.signedSend.elapsedMs < 0
  ) {
    throw new Error("prior asset-ban Delivery evidence is not exact");
  }
  validateStoredDeliveryRejection(
    evidence.receiverBaseline,
    evidence.receiverRejection,
    "rejected_payload",
    "prior asset-ban",
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

async function uniqueStateFile(label, name) {
  const found = await findNamedFiles(join(usersDir, label), name);
  if (found.length !== 1) {
    throw new Error(
      `${label} expected one ${name}, got ${found.length}`,
    );
  }
  return found[0];
}

async function walletStateFiles(label, expectedInstanceRoot) {
  const configPath = await uniqueStateFile(
    label,
    "lez-wallet-config-v1.json",
  );
  const storagePath = await uniqueStateFile(
    label,
    "lez-wallet-storage-v1.json",
  );
  if (
    dirname(configPath) !== expectedInstanceRoot
    || dirname(storagePath) !== expectedInstanceRoot
  ) {
    throw new Error("LEZ wallet files span persistence roots");
  }
  return { configPath, storagePath };
}

async function walletStateEvidence(files) {
  const config = await fingerprintBoundedRegularFile(
    files.configPath,
    16 * 1024,
  );
  const storage = await fingerprintBoundedRegularFile(
    files.storagePath,
    64 * 1024 * 1024,
  );
  return {
    config,
    storage,
    combinedSha256: sha256(JSON.stringify({ config, storage })),
  };
}

async function storageDataRootEvidence(label) {
  const userRoot = await realpath(join(usersDir, label));
  const authorityPath = await uniqueStateFile(
    label,
    "lez-authority-bundle-v1",
  );
  const storagePath = join(dirname(authorityPath), "storage");
  const metadata = await lstat(storagePath);
  const canonical = await realpath(storagePath);
  if (
    metadata.isSymbolicLink()
    || !metadata.isDirectory()
    || (canonical !== userRoot
      && !canonical.startsWith(`${userRoot}/`))
  ) {
    throw new Error(`unsafe retained Storage root ${label}`);
  }
  const evidence = await boundedDirectoryFingerprint(storagePath);
  if (evidence.fileCount <= 0 || evidence.totalBytes <= 0) {
    throw new Error(`retained Storage root ${label} is empty`);
  }
  return evidence;
}

async function removeColdClientDerivedState(label) {
  const userRoot = await realpath(join(usersDir, label));
  const authorityPath = await uniqueStateFile(
    label,
    "lez-authority-bundle-v1",
  );
  const instanceRoot = dirname(authorityPath);
  const projectionPath = await uniqueStateFile(label, "projection-v1");
  const vmTurnPath = await uniqueStateFile(
    label,
    "palace-core-vm-turn-v1",
  );
  const vmFinalityPath = await uniqueStateFile(
    label,
    "palace_vm_finality_v1.bin",
  );
  const identityPath = await uniqueStateFile(
    label,
    "delivery-identity-v1",
  );
  const walletFiles = await walletStateFiles(label, instanceRoot);
  const storagePath = join(instanceRoot, "storage");
  const verifiedAssetsPath = join(
    instanceRoot,
    "verified_assets",
    "logos_palace_ui",
  );
  const paths = [
    authorityPath,
    projectionPath,
    vmTurnPath,
    vmFinalityPath,
    identityPath,
    walletFiles.configPath,
    walletFiles.storagePath,
    storagePath,
    verifiedAssetsPath,
  ];
  for (const path of paths) {
    const metadata = await lstat(path);
    const canonical = await realpath(path);
    if (
      metadata.isSymbolicLink()
      || (canonical !== userRoot
        && !canonical.startsWith(`${userRoot}/`))
      || (
        [storagePath, verifiedAssetsPath].includes(path)
          ? !metadata.isDirectory()
          : !metadata.isFile()
      )
    ) {
      throw new Error(`unsafe cold-rebuild target ${basename(path)}`);
    }
  }
  if (
    dirname(projectionPath) !== instanceRoot
    || dirname(vmTurnPath) !== instanceRoot
    || dirname(identityPath) !== instanceRoot
  ) {
    throw new Error("cold-rebuild state files span persistence roots");
  }

  const before = {
    authority: await fingerprintBoundedRegularFile(
      authorityPath,
      8 * 1024 * 1024,
    ),
    projection: await fingerprintBoundedRegularFile(
      projectionPath,
      1024 * 1024,
    ),
    vmTurn: await fingerprintBoundedRegularFile(
      vmTurnPath,
      64 * 1024,
    ),
    vmFinality: await fingerprintBoundedRegularFile(
      vmFinalityPath,
      1024 * 1024,
    ),
    storage: await boundedDirectoryFingerprint(storagePath),
    verifiedAssets:
      await boundedDirectoryFingerprint(verifiedAssetsPath),
    identity: await fingerprintBoundedRegularFile(
      identityPath,
      1024 * 1024,
    ),
    lezWallet: await walletStateEvidence(walletFiles),
  };
  await unlink(authorityPath);
  await unlink(projectionPath);
  await unlink(vmTurnPath);
  await unlink(vmFinalityPath);
  await rm(storagePath, { recursive: true, force: false });
  await rm(verifiedAssetsPath, { recursive: true, force: false });
  for (const path of [
    authorityPath,
    projectionPath,
    vmTurnPath,
    vmFinalityPath,
    storagePath,
    verifiedAssetsPath,
  ]) {
    try {
      await access(path);
      throw new Error(`cold-rebuild target survived: ${basename(path)}`);
    } catch (error) {
      if (error?.code !== "ENOENT") throw error;
    }
  }
  const identityAfterRemoval = await fingerprintBoundedRegularFile(
    identityPath,
    1024 * 1024,
  );
  const walletAfterRemoval = await walletStateEvidence(walletFiles);
  if (
    !exactStableJson(identityAfterRemoval, before.identity)
    || !exactStableJson(walletAfterRemoval, before.lezWallet)
  ) {
    throw new Error(
      "cold-rebuild preparation changed identity or LEZ wallet",
    );
  }
  return {
    status: "removed",
    label,
    removed: [
      "finalized-authority",
      "local-projection",
      "finalized-vm-turn",
      "vm-finality-journal",
      "storage-data-root",
      "verified-asset-cache",
    ],
    preserved: ["delivery-identity", "lez-wallet"],
    preservationProof: {
      deliveryIdentityUnchangedByDeletion: true,
      lezWalletUnchangedByDeletion: true,
    },
    before,
  };
}

async function actionJournalRecord(label, actionId, allowMissing = false) {
  const path = await uniqueStateFile(label, "action-journal-v2");
  const record = await readFile(path, "utf8");
  const newline = record.indexOf("\n");
  const state = record.slice(newline + 1);
  if (
    newline !== 64
    || sha256(state) !== record.slice(0, 64)
    || !state.startsWith("version=2\n")
    || !state.endsWith("\n")
  ) {
    throw new Error(`${label} action journal checksum mismatch`);
  }
  const encodedAction = Buffer.from(actionId, "utf8").toString("hex");
  const lines = state
    .slice("version=2\n".length)
    .split("\n")
    .filter(Boolean)
    .filter((entry) => entry.startsWith(`${encodedAction};`));
  if (allowMissing && lines.length === 0) return undefined;
  const fields = lines.length === 1 ? lines[0].split(";") : [];
  const stage = Number(fields[1]);
  const transactionHash = fields[3] === "-" ? undefined : fields[3];
  if (
    fields.length !== 4
    || fields[0] !== encodedAction
    || !Number.isSafeInteger(stage)
    || stage < 0
    || stage > 7
    || String(stage) !== fields[1]
    || !["0", "1"].includes(fields[2])
    || (
      transactionHash !== undefined
      && !isHex64(transactionHash)
    )
    || (
      [2, 3, 4, 6].includes(stage)
      && !transactionHash
    )
  ) {
    throw new Error(`${label} action ${actionId} journal record invalid`);
  }
  return {
    file: basename(path),
    journalFileSha256: sha256(record),
    recordSha256: sha256(`version=2\n${lines[0]}\n`),
    transactionHash,
    stage,
    durableStage: [
      "local_draft",
      "queued",
      "submitted_to_lez",
      "observed",
      "finalized",
      "rejected",
      "expired",
      "orphaned",
    ][stage],
    deliveryPublished: fields[2] === "1",
  };
}

async function actionJournalEvidence(label, actionId) {
  const evidence = await actionJournalRecord(label, actionId);
  if (
    evidence.stage !== 4
    || evidence.deliveryPublished
    || !isHex64(evidence.transactionHash)
  ) {
    throw new Error(`${label} action ${actionId} not durably finalized`);
  }
  return evidence;
}

async function finalActionJournalEvidence(label) {
  const expectedActions = report.plan.actions.filter(
    (action) => action.caller === label,
  );
  const records = {};
  let journalFileSha256;
  for (const action of expectedActions) {
    const evidence = await actionJournalEvidence(label, action.actionId);
    const reported = report.actions.find(
      ({ actionId }) => actionId === action.actionId,
    );
    if (
      reported?.transactionHash !== evidence.transactionHash
      || reported.caller !== label
      || reported.transitionSha256 !== action.transitionSha256
    ) {
      throw new Error(
        `${label} final action journal differs for action ${action.actionId}`,
      );
    }
    if (
      journalFileSha256
      && journalFileSha256 !== evidence.journalFileSha256
    ) {
      throw new Error(`${label} final action journal changed during read`);
    }
    journalFileSha256 = evidence.journalFileSha256;
    records[action.actionId] = {
      transactionHash: evidence.transactionHash,
      stage: evidence.stage,
      durableStage: evidence.durableStage,
      deliveryPublished: evidence.deliveryPublished,
      recordSha256: evidence.recordSha256,
    };
  }
  return {
    file: "action-journal-v2",
    journalFileSha256,
    actionIds: expectedActions.map(({ actionId }) => actionId),
    records,
  };
}

async function authorityBundleEvidence(label) {
  const path = await uniqueStateFile(label, "lez-authority-bundle-v1");
  const bytes = await readFile(path);
  if (bytes.length < 16 + 64) {
    throw new Error(`${label} authority bundle is truncated`);
  }
  return {
    file: basename(path),
    bytes: bytes.length,
    sha256: sha256(bytes),
  };
}

async function readBoundedProcFile(path, maximumBytes) {
  const handle = await open(path, "r");
  try {
    const buffer = Buffer.alloc(maximumBytes + 1);
    let offset = 0;
    while (offset < buffer.length) {
      const { bytesRead } = await handle.read(
        buffer,
        offset,
        buffer.length - offset,
        null,
      );
      if (bytesRead === 0) break;
      offset += bytesRead;
    }
    if (offset > maximumBytes) {
      throw new Error(`${path} exceeds ${maximumBytes} bytes`);
    }
    return buffer.subarray(0, offset);
  } finally {
    await handle.close();
  }
}

async function fingerprintProcExecutable(processId) {
  // /proc/<pid>/exe is a kernel magic link; following it in this single
  // open binds hashing, fstat identity, and canonical-path resolution.
  const handle = await open(
    `/proc/${processId}/exe`,
    fsConstants.O_RDONLY | fsConstants.O_CLOEXEC,
  );
  try {
    const artifact = await fingerprintBoundedOpenRegularFile(
      handle,
      processArtifactMaximumBytes,
    );
    const path = await realpath(`/proc/self/fd/${handle.fd}`);
    return { path, artifact };
  } finally {
    await handle.close();
  }
}

function parseBoundedCmdline(bytes, processId) {
  if (bytes.length === 0) {
    throw new Error(`process ${processId} has empty cmdline`);
  }
  const fields = bytes.toString("utf8").split("\0");
  if (fields.at(-1) === "") fields.pop();
  if (
    fields.length === 0
    || fields.length > 128
    || fields.some(
      (field) => field.length === 0 || Buffer.byteLength(field) > 4096,
    )
  ) {
    throw new Error(`process ${processId} has invalid bounded cmdline`);
  }
  const nameIndexes = fields
    .map((field, index) => field === "--name" ? index : -1)
    .filter((index) => index >= 0);
  const pathIndexes = fields
    .map((field, index) => field === "--path" ? index : -1)
    .filter((index) => index >= 0);
  if (
    nameIndexes.length > 1
    || pathIndexes.length > 1
    || nameIndexes.length !== pathIndexes.length
    || nameIndexes.some(
      (index) =>
        index + 1 >= fields.length
        || !/^[a-z][a-z0-9_-]{0,63}$/.test(fields[index + 1]),
    )
    || pathIndexes.some(
      (index) =>
        index + 1 >= fields.length
        || fields[index + 1].startsWith("--"),
    )
  ) {
    throw new Error(`process ${processId} has invalid module arguments`);
  }
  const firstArgument = basename(fields[0]);
  const programIndex =
    /^ld(?:-[a-z0-9_-]+)?-linux[^/]*\.so(?:\.[0-9]+)*$/i.test(
      firstArgument,
    )
      ? 1
      : 0;
  if (programIndex >= fields.length) {
    throw new Error(`process ${processId} has no bounded program argument`);
  }
  return {
    executableArgument: firstArgument,
    programArgument: basename(fields[programIndex]),
    executableArgumentPath: fields[0],
    programArgumentPath: fields[programIndex],
    argumentBasenames: fields.map((field) => basename(field)),
    moduleName:
      nameIndexes.length === 1 ? fields[nameIndexes[0] + 1] : undefined,
    modulePathArgument:
      pathIndexes.length === 1 ? fields[pathIndexes[0] + 1] : undefined,
  };
}

function boundedArgumentBasenames(bytes, processId) {
  if (bytes.length === 0) return [];
  const fields = bytes.toString("utf8").split("\0");
  if (fields.at(-1) === "") fields.pop();
  if (
    fields.length > 128
    || fields.some((field) => Buffer.byteLength(field) > 4096)
  ) {
    throw new Error(
      `process ${processId} exceeds standalone argv scan bound`,
    );
  }
  return fields.filter(Boolean).map((field) => basename(field));
}

function parseProcStat(bytes, processId) {
  const encoded = bytes.toString("utf8").trim();
  const close = encoded.lastIndexOf(")");
  if (
    !encoded.startsWith(`${processId} (`)
    || close < 3
  ) {
    throw new Error(`process ${processId} has invalid stat record`);
  }
  const fields = encoded.slice(close + 2).split(" ");
  const processGroupId = Number(fields[2]);
  const sessionId = Number(fields[3]);
  if (
    !Number.isSafeInteger(processGroupId)
    || processGroupId <= 0
    || !Number.isSafeInteger(sessionId)
    || sessionId <= 0
  ) {
    throw new Error(`process ${processId} has invalid group/session`);
  }
  return { processGroupId, sessionId };
}

const processInventoryObservations = [];

async function tcpListenerOwnership(
  rootPid,
  inventory,
  expectedTcpListeners,
) {
  const socketOwners = new Map();
  for (const process of inventory) {
    let entries;
    try {
      entries = await readdir(`/proc/${process.pid}/fd`, {
        withFileTypes: true,
      });
    } catch (error) {
      if (error?.code === "ENOENT") {
        throw new Error(
          `Basecamp process ${process.pid} exited during socket inventory`,
        );
      }
      throw error;
    }
    if (entries.length > 4096) {
      throw new Error("Basecamp process descriptor inventory exceeds bound");
    }
    for (const entry of entries) {
      if (!/^(?:0|[1-9][0-9]*)$/.test(entry.name)) continue;
      let target;
      try {
        target = await readlink(`/proc/${process.pid}/fd/${entry.name}`);
      } catch (error) {
        if (error?.code === "ENOENT") continue;
        throw error;
      }
      const match = target.match(/^socket:\[([1-9][0-9]*)\]$/);
      if (!match) continue;
      const owners = socketOwners.get(match[1]) ?? new Set();
      owners.add(process.pid);
      socketOwners.set(match[1], owners);
    }
  }
  const tcp4 = (
    await readBoundedProcFile(`/proc/${rootPid}/net/tcp`, 4 * 1024 * 1024)
  ).toString("utf8");
  const tcp6 = (
    await readBoundedProcFile(`/proc/${rootPid}/net/tcp6`, 4 * 1024 * 1024)
  ).toString("utf8");
  const listeners = [
    ...parseTcpListenTable(tcp4, "tcp4"),
    ...parseTcpListenTable(tcp6, "tcp6"),
  ];
  return validateExactTcpListenerOwnership(
    inventory,
    listeners,
    socketOwners,
    expectedTcpListeners,
  );
}

function validateFrameTiming(evidence) {
  validatePalaceFrameTimingMeasurement(evidence);
  if (evidence.measurementContract.basecampRevision !== report.basecampRevision) {
    throw new Error(
      "Palace frame timing Basecamp revision differs from the run",
    );
  }
  if (
    report.frameTiming.measurementContract
    && !exactStableJson(
      report.frameTiming.measurementContract,
      evidence.measurementContract,
    )
  ) {
    throw new Error("Palace frame timing contract changed within run");
  }
  report.frameTiming.measurementContract = evidence.measurementContract;
  return evidence;
}

async function captureFrameTiming(worker) {
  return validateFrameTiming(await worker.call("frameTimings"));
}

async function processMetrics(pid, expectedTcpListeners, label) {
  const expectedArtifacts = expectedProcessArtifactsByLabel.get(label);
  if (
    !Number.isSafeInteger(pid)
    || pid <= 0
    || !labels.includes(label)
    || !expectedArtifacts
  ) {
    throw new Error(`invalid Basecamp PID ${pid}`);
  }
  const readStatus = async (processId) => {
    const encoded = (
      await readBoundedProcFile(
        `/proc/${processId}/status`,
        64 * 1024,
      )
    ).toString("utf8");
    const value = (name) => {
      const match = encoded.match(
        new RegExp(`^${name}:\\s+([0-9]+) kB$`, "m"),
      );
      return match ? Number(match[1]) : undefined;
    };
    const name = encoded.match(/^Name:\s+(.+)$/m)?.[1];
    const effectiveUid = Number(
      encoded.match(
        /^Uid:\s+[0-9]+\s+([0-9]+)\s+[0-9]+\s+[0-9]+$/m,
      )?.[1],
    );
    if (!Number.isSafeInteger(effectiveUid) || effectiveUid < 0) {
      throw new Error(`process ${processId} has invalid effective UID`);
    }
    const cmdline = parseBoundedCmdline(
      await readBoundedProcFile(`/proc/${processId}/cmdline`, 64 * 1024),
      processId,
    );
    const processCwd = await realpath(`/proc/${processId}/cwd`);
    const procExecutable = await fingerprintProcExecutable(processId);
    const executablePath = procExecutable.path;
    const executableArgumentPath = await realpath(
      resolve(processCwd, cmdline.executableArgumentPath),
    );
    const programArgumentPath = await realpath(
      resolve(processCwd, cmdline.programArgumentPath),
    );
    let runtimeIdentity;
    if (
      processId === pid
      && cmdline.programArgument === ".LogosBasecamp.elf"
      && cmdline.moduleName === undefined
    ) {
      runtimeIdentity = processArtifactIdentity("basecamp-main", null);
    } else if (
      cmdline.programArgument === ".logos_host.elf"
      && exactProcessInventoryContract.coreModuleHosts.includes(
        cmdline.moduleName,
      )
    ) {
      runtimeIdentity = processArtifactIdentity(
        "core-module-host",
        cmdline.moduleName,
      );
    } else if (
      cmdline.programArgument === ".ui-host.elf"
      && cmdline.moduleName === "logos_palace_ui"
    ) {
      runtimeIdentity = processArtifactIdentity(
        "ui-module-host",
        cmdline.moduleName,
      );
    }
    const expectedArtifact = expectedArtifacts[runtimeIdentity];
    if (!expectedArtifact) {
      throw new Error(
        `process ${processId} has no pinned runtime artifact identity`,
      );
    }
    const moduleArgumentPath = cmdline.modulePathArgument === undefined
      ? null
      : await realpath(
        resolve(processCwd, cmdline.modulePathArgument),
      );
    if (
      (
        expectedArtifact.modulePath === null
          ? moduleArgumentPath !== null
          : moduleArgumentPath !== expectedArtifact.modulePath
      )
    ) {
      throw new Error(
        `process ${processId} module argv path differs from pinned artifact`,
      );
    }
    const directInterpreterPath =
      expectedArtifact.executionMode === "direct"
        ? expectedArtifact.loaderPath
        : null;
    const [
      programArgumentArtifact,
      directInterpreterArtifact,
      moduleArgumentArtifact,
    ] = await Promise.all([
      fingerprintBoundedRegularFileIdentity(
        programArgumentPath,
        processArtifactMaximumBytes,
      ),
      directInterpreterPath
        ? fingerprintBoundedRegularFileIdentity(
          directInterpreterPath,
          processArtifactMaximumBytes,
        )
        : Promise.resolve(null),
      moduleArgumentPath
        ? fingerprintBoundedRegularFileIdentity(
          moduleArgumentPath,
          processArtifactMaximumBytes,
        )
        : Promise.resolve(null),
    ]);
    const fileIdentity = ({ device, inode }) => ({ device, inode });
    const executableArtifact = procExecutable.artifact;
    const executableArgumentArtifact =
      expectedArtifact.executionMode === "fallback"
        ? executableArtifact
        : programArgumentArtifact;
    const procMaps = (
      await readBoundedProcFile(
        `/proc/${processId}/maps`,
        8 * 1024 * 1024,
      )
    ).toString("utf8");
    const programExecutableMapping =
      validateExpectedExecutableMapping(
        procMaps,
        expectedArtifact.programPath,
        fileIdentity(programArgumentArtifact),
      );
    const directInterpreterMapping = directInterpreterArtifact
      ? validateExpectedExecutableMapping(
        procMaps,
        directInterpreterPath,
        fileIdentity(directInterpreterArtifact),
      )
      : null;
    const moduleExecutableMapping = moduleArgumentArtifact
      ? validateExpectedExecutableMapping(
        procMaps,
        expectedArtifact.modulePath,
        fileIdentity(moduleArgumentArtifact),
      )
      : null;
    return {
      pid: processId,
      name,
      executable: basename(executablePath),
      executableArgument: cmdline.executableArgument,
      programArgument: cmdline.programArgument,
      executablePath,
      executableArgumentPath,
      programArgumentPath,
      executableSha256: executableArtifact.sha256,
      executableArgumentSha256: executableArgumentArtifact.sha256,
      programArgumentSha256: programArgumentArtifact.sha256,
      executableFileIdentity: fileIdentity(executableArtifact),
      programArgumentFileIdentity:
        fileIdentity(programArgumentArtifact),
      programExecutableMapping,
      directInterpreterPath,
      directInterpreterSha256:
        directInterpreterArtifact?.sha256 ?? null,
      directInterpreterFileIdentity:
        directInterpreterArtifact
          ? fileIdentity(directInterpreterArtifact)
          : null,
      directInterpreterMapping,
      moduleArgumentPath,
      moduleArgumentSha256: moduleArgumentArtifact?.sha256 ?? null,
      moduleArtifactPath: expectedArtifact.modulePath,
      moduleArtifactSha256:
        moduleArgumentArtifact?.sha256 ?? null,
      moduleArgumentFileIdentity:
        moduleArgumentArtifact
          ? fileIdentity(moduleArgumentArtifact)
          : null,
      moduleExecutableMapping,
      argumentBasenames: cmdline.argumentBasenames,
      moduleName: cmdline.moduleName,
      effectiveUid,
      vmHwmKiB: value("VmHWM"),
      vmRssKiB: value("VmRSS"),
    };
  };
  const root = await readStatus(pid);
  const vmHwmKiB = root.vmHwmKiB;
  const vmRssKiB = root.vmRssKiB;
  if (!Number.isSafeInteger(vmHwmKiB) || !Number.isSafeInteger(vmRssKiB)) {
    throw new Error(`Basecamp ${pid} omitted VmHWM/VmRSS`);
  }
  const standaloneScanProcesses = [];
  const processIds = [];
  const pending = [pid];
  const seen = new Set();
  while (pending.length > 0 && seen.size < 128) {
    const current = pending.shift();
    if (seen.has(current)) continue;
    seen.add(current);
    processIds.push(current);
    let children;
    try {
      children = await readFile(
        `/proc/${current}/task/${current}/children`,
        "utf8",
      );
    } catch (error) {
      if (error?.code === "ENOENT") continue;
      throw error;
    }
    for (const value of children.trim().split(/\s+/).filter(Boolean)) {
      const childPid = Number(value);
      if (Number.isSafeInteger(childPid) && childPid > 0) {
        pending.push(childPid);
      }
    }
  }
  const rootStat = parseProcStat(
    await readBoundedProcFile(`/proc/${pid}/stat`, 4096),
    pid,
  );
  const procEntries = await readdir("/proc", { withFileTypes: true });
  if (procEntries.length > 65_536) {
    throw new Error("/proc process inventory exceeds acceptance bound");
  }
  for (const entry of procEntries) {
    if (!entry.isDirectory() || !/^[1-9][0-9]*$/.test(entry.name)) continue;
    const processId = Number(entry.name);
    if (!Number.isSafeInteger(processId) || processId <= 0) continue;
    try {
      const encodedStatus = await readBoundedProcFile(
        `/proc/${processId}/status`,
        64 * 1024,
      );
      const status = encodedStatus.toString("utf8");
      const effectiveUid = Number(
        status.match(
          /^Uid:\s+[0-9]+\s+([0-9]+)\s+[0-9]+\s+[0-9]+$/m,
        )?.[1],
      );
      if (effectiveUid === root.effectiveUid) {
        const name = status.match(/^Name:\s+(.+)$/m)?.[1];
        const cmdlineBytes = await readBoundedProcFile(
          `/proc/${processId}/cmdline`,
          64 * 1024,
        );
        const argumentBasenames = boundedArgumentBasenames(
          cmdlineBytes,
          processId,
        );
        let executable;
        try {
          executable = basename(await readlink(`/proc/${processId}/exe`));
        } catch (error) {
          if (error?.code !== "ENOENT") throw error;
        }
        standaloneScanProcesses.push({
          pid: processId,
          name,
          executable,
          executableArgument: argumentBasenames[0],
          programArgument:
            /^ld(?:-[a-z0-9_-]+)?-linux[^/]*\.so(?:\.[0-9]+)*$/i.test(
              argumentBasenames[0] ?? "",
            )
              ? argumentBasenames[1]
              : argumentBasenames[0],
          argumentBasenames,
        });
      }
      const candidate = parseProcStat(
        await readBoundedProcFile(`/proc/${processId}/stat`, 4096),
        processId,
      );
      if (
        candidate.processGroupId === rootStat.processGroupId
        || candidate.sessionId === rootStat.sessionId
      ) {
        processIds.push(processId);
      }
    } catch (error) {
      if (error?.code !== "ENOENT" && error?.code !== "EACCES") throw error;
    }
  }
  const uniqueProcessIds = [...new Set(processIds)];
  const processes = [];
  for (const processId of uniqueProcessIds) {
    try {
      const value = await readStatus(processId);
      if (
        Number.isSafeInteger(value.vmHwmKiB)
        && Number.isSafeInteger(value.vmRssKiB)
      ) {
        processes.push(value);
      }
    } catch (error) {
      if (error?.code !== "ENOENT") throw error;
    }
  }
  const palaceVmHosts = processes.filter(
    ({ moduleName }) => moduleName === "palace_vm",
  );
  if (palaceVmHosts.length !== 1) {
    throw new Error(
      `Basecamp ${pid} expected one palace_vm host, got ${palaceVmHosts.length}`,
    );
  }
  const exactInventory = validateExactProcessInventory(
    processes,
    pid,
    expectedArtifacts,
  );
  const exactByPid = new Map(
    exactInventory.map((process) => [process.pid, process]),
  );
  const listeners = await tcpListenerOwnership(
    pid,
    exactInventory,
    expectedTcpListeners,
  );
  const standalonePalaceServerMatches =
    findStandalonePalaceServerMatches(standaloneScanProcesses);
  const observation = {
    rootPid: pid,
    rootProcessGroupId: rootStat.processGroupId,
    rootSessionId: rootStat.sessionId,
    scope: "recursive descendants plus matching Basecamp process group/session",
    standalonePalaceServerScanScope:
      "all same-effective-UID processes visible in bounded /proc scan",
    inventoryContract: exactProcessInventoryContract,
    processCount: processes.length,
    processes: processes.map((process) => ({
      pid: process.pid,
      name: process.name,
      executable: process.executable,
      executableArgument: process.executableArgument,
      programArgument: process.programArgument,
      executableSha256: process.executableSha256,
      executableArgumentSha256: process.executableArgumentSha256,
      programArgumentSha256: process.programArgumentSha256,
      executableFileIdentity: process.executableFileIdentity,
      programArgumentFileIdentity:
        process.programArgumentFileIdentity,
      programExecutableMapping: process.programExecutableMapping,
      executionMode: processLoaderSelection.mode,
      loaderPath:
        processLoaderSelection.mode === "fallback"
          ? process.executableArgumentPath
          : null,
      directInterpreterPath: process.directInterpreterPath,
      directInterpreterSha256: process.directInterpreterSha256,
      directInterpreterFileIdentity:
        process.directInterpreterFileIdentity,
      directInterpreterMapping: process.directInterpreterMapping,
      moduleArgumentPath: process.moduleArgumentPath,
      moduleArgumentSha256: process.moduleArgumentSha256,
      moduleArgumentFileIdentity:
        process.moduleArgumentFileIdentity,
      moduleArtifactPath: process.moduleArtifactPath,
      moduleArtifactSha256: process.moduleArtifactSha256,
      moduleExecutableMapping: process.moduleExecutableMapping,
      moduleName: process.moduleName ?? null,
      role: exactByPid.get(process.pid).role,
      program: exactByPid.get(process.pid).program,
    })),
    tcpListenerProof: {
      scope: "TCP LISTEN sockets owned by exact Basecamp process inventory",
      expectedOnly: true,
      listenerCount: listeners.length,
      listeners,
    },
    standalonePalaceServerMatches,
  };
  processInventoryObservations.push(observation);
  report.processModel = {
    standalonePalaceServer: processInventoryObservations.some(
      (entry) => entry.standalonePalaceServerMatches.length > 0,
    ),
    loaderSelection: processLoaderSelection,
    runtimeArtifacts: processRuntimeArtifacts,
    observationMethod:
      "bounded /proc exact process inventory, pinned runtime artifacts, and owned TCP LISTEN proof",
    observations: processInventoryObservations,
  };
  if (standalonePalaceServerMatches.length > 0) {
    throw new Error(
      "standalone Palace server found in bounded same-effective-UID scan",
    );
  }
  const palaceVmHost = palaceVmHosts[0];
  return {
    pid,
    metricScope: "Basecamp-main-only",
    vmHwmKiB,
    vmRssKiB,
    processTree: {
      metricScope: "Basecamp-process-tree",
      processCount: processes.length,
      currentVmRssKiB: processes.reduce(
        (sum, process) => sum + process.vmRssKiB,
        0,
      ),
      summedPerProcessVmHwmKiB: processes.reduce(
        (sum, process) => sum + process.vmHwmKiB,
        0,
      ),
      processes: processes.map((process) => ({
        pid: process.pid,
        name: process.name,
        executable: process.executable,
        executableArgument: process.executableArgument,
        programArgument: process.programArgument,
        executableSha256: process.executableSha256,
        executableArgumentSha256: process.executableArgumentSha256,
        programArgumentSha256: process.programArgumentSha256,
        executableFileIdentity: process.executableFileIdentity,
        programArgumentFileIdentity:
          process.programArgumentFileIdentity,
        programExecutableMapping: process.programExecutableMapping,
        executionMode: processLoaderSelection.mode,
        loaderPath:
          processLoaderSelection.mode === "fallback"
            ? process.executableArgumentPath
            : null,
        directInterpreterPath: process.directInterpreterPath,
        directInterpreterSha256: process.directInterpreterSha256,
        directInterpreterFileIdentity:
          process.directInterpreterFileIdentity,
        directInterpreterMapping: process.directInterpreterMapping,
        moduleArgumentPath: process.moduleArgumentPath,
        moduleArgumentSha256: process.moduleArgumentSha256,
        moduleArgumentFileIdentity:
          process.moduleArgumentFileIdentity,
        moduleArtifactPath: process.moduleArtifactPath,
        moduleArtifactSha256: process.moduleArtifactSha256,
        moduleExecutableMapping: process.moduleExecutableMapping,
        moduleName: process.moduleName,
        vmHwmKiB: process.vmHwmKiB,
        vmRssKiB: process.vmRssKiB,
      })),
    },
    palaceVmHost: {
      pid: palaceVmHost.pid,
      moduleName: palaceVmHost.moduleName,
      metricScope: "palace_vm module-host process lifetime",
      vmHwmKiB: palaceVmHost.vmHwmKiB,
      vmRssKiB: palaceVmHost.vmRssKiB,
    },
  };
}

async function screenshotFileEvidenceAt(artifactDirectory, file) {
  if (
    resolve(artifactDirectory) !== artifactDirectory
    || typeof file !== "string"
    || basename(file) !== file
    || !/^[a-z0-9][a-z0-9._-]{0,127}\.png$/.test(file)
  ) {
    throw new Error(`invalid screenshot artifact path: ${file}`);
  }
  const path = join(artifactDirectory, file);
  const metadata = await lstat(path);
  if (
    metadata.isSymbolicLink()
    || !metadata.isFile()
    || await realpath(path) !== path
    || dirname(path) !== artifactDirectory
  ) {
    throw new Error(`${file} is not a canonical regular artifact`);
  }
  const bytes = await readFile(path);
  if (
    bytes.length < 24
    || bytes.length > 64 * 1024 * 1024
    || !bytes.subarray(0, 8).equals(
      Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    )
    || bytes.readUInt32BE(8) !== 13
    || bytes.subarray(12, 16).toString("ascii") !== "IHDR"
  ) {
    throw new Error(`${file} is not a bounded PNG`);
  }
  const width = bytes.readUInt32BE(16);
  const height = bytes.readUInt32BE(20);
  if (width === 0 || height === 0) {
    throw new Error(`${file} has invalid PNG dimensions`);
  }
  return {
    file,
    artifactPath: file,
    width,
    height,
    byteLength: bytes.length,
    sha256: sha256(bytes),
  };
}

async function screenshotFileEvidence(file) {
  return screenshotFileEvidenceAt(artifactsDir, file);
}

function replaceScreenshot(entry) {
  report.screenshots = report.screenshots.filter(
    ({ file }) => file !== entry.file,
  );
  report.screenshots.push(entry);
}

async function captureScreenshot(spec) {
  const worker = workers.get(spec.label);
  if (!worker) {
    throw new Error(`screenshot worker ${spec.label} is not running`);
  }
  const captured = await worker.call(
    "screenshot",
    { name: spec.file },
    60_000,
  );
  const evidence = await screenshotFileEvidence(spec.file);
  if (
    captured.file !== evidence.file
    || captured.artifactPath !== evidence.artifactPath
    || captured.width !== evidence.width
    || captured.height !== evidence.height
    || captured.byteLength !== evidence.byteLength
    || captured.sha256 !== evidence.sha256
  ) {
    throw new Error(`${spec.file} screenshot evidence changed after capture`);
  }
  const entry = {
    stage: spec.stage,
    state: spec.state,
    label: spec.label,
    ...evidence,
  };
  replaceScreenshot(entry);
  await checkpointReport();
  return entry;
}

async function reusePriorScreenshot(spec) {
  const prior = previousReport?.screenshots?.find(
    (entry) =>
      entry?.file === spec.file
      && entry.stage === spec.stage
      && entry.state === spec.state
      && entry.label === spec.label,
  );
  if (!prior || !isHex64(prior.sha256)) {
    throw new Error(
      `resumed finalized state lacks ${spec.file} screenshot evidence`,
    );
  }
  const evidence = await screenshotFileEvidence(spec.file);
  if (
    prior.artifactPath !== evidence.artifactPath
    || prior.width !== evidence.width
    || prior.height !== evidence.height
    || prior.byteLength !== evidence.byteLength
    || prior.sha256 !== evidence.sha256
  ) {
    throw new Error(`${spec.file} prior screenshot digest mismatch`);
  }
  const entry = {
    stage: spec.stage,
    state: spec.state,
    label: spec.label,
    ...evidence,
  };
  replaceScreenshot(entry);
  return entry;
}

function processExists(pid) {
  if (!Number.isSafeInteger(pid) || pid <= 0) return false;
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

async function requireOwnedProcessGroupEmpty(processGroupId) {
  if (
    !Number.isSafeInteger(processGroupId)
    || processGroupId <= 0
  ) {
    return;
  }
  const members = await ownedProcessGroupMembers({
    processGroupId,
    claimPath: process.env.PALACE_MVP_CLAIM_PATH,
  });
  if (members.length === 0) return;
  requireOwnedProcessGroup(members, processGroupId);
  const survivors = [];
  for (const member of members) {
    if (await ownedProcessIdentityExists(member)) survivors.push(member);
  }
  if (survivors.length > 0) {
    throw new Error(
      `process group ${processGroupId} retained ${survivors.length} `
      + "owned process; exact outer scope cleanup required",
    );
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
    throw new Error("run-owned child remained in Gate 4 process group");
  }
  if (workload.length > 0) {
    throw new Error(
      "run-owned processes require exact outer scope cleanup",
    );
  }
  const remaining = claimWorkload(
    await claimBoundProcesses({ claimPath }),
  );
  if (remaining.length > 0) {
    throw new Error("run-owned processes survived Gate 4 cleanup");
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
    await attempt(() => requireOwnedProcessGroupEmpty(worker.basecampPid));
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
      requireOwnedProcessGroupEmpty(process.processGroupId));
  }
  await attempt(() => requireOwnedProcessGroupEmpty(worker.child.pid));
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
      await requireOwnedProcessGroupEmpty(processGroupId);
    }
    const remaining = claimWorkload(
      await claimBoundProcesses({
        claimPath: process.env.PALACE_MVP_CLAIM_PATH,
      }),
    ).filter(({ sessionId }) => sessionIds.has(sessionId));
    if (remaining.length > 0) {
      throw new Error(
        `${worker.processLabel} retained owned session processes after cleanup`,
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
        `${worker.processLabel} retained owned Basecamp processes after cleanup`,
      );
    }
  });
  if (failures.length > 0) {
    throw new Error([...new Set(failures)].join("; "));
  }
}

async function previewDoor(worker) {
  const startedAt = performance.now();
  const result = await invoke(
    worker,
    "gate5PreviewDoor",
    [],
    { prefix: "ok;spot=door;" },
    120_000,
  );
  const fields = statusFields(result.receipt);
  if (
    fields.action !== report.plan.doorActionId
    || fields.state_root !== report.plan.doorState.openedStateRootHex
    || !fields.receipt
    || !isHex64(fields.receipt_sha256)
    || fields.script_cid !== report.catalog.byId["script-door"].cid
    || fields.navigation !== "0"
  ) {
    throw new Error(`invalid Gate 5 preview ${worker.label}: ${result.receipt}`);
  }
  return {
    ...result,
    fields,
    totalMs: Math.round(performance.now() - startedAt),
  };
}

function validateStoredVmTurnMetric(
  metric,
  label,
  expectedReceiptSha256,
  expectedPhase = "provisional",
) {
  let durationNs;
  try {
    durationNs = BigInt(metric?.durationNs);
  } catch {
    throw new Error(`prior ${label} VM turn duration is invalid`);
  }
  if (
    metric?.actionId !== report.plan.doorActionId
    || metric.phase !== expectedPhase
    || metric.clock !== "steady_clock"
    || !/^[0-9]+$/.test(metric.durationNs)
    || durationNs > 60n * 60n * 1_000_000_000n
    || metric.durationMs !== Number(durationNs) / 1_000_000
    || metric.receiptSha256 !== expectedReceiptSha256
  ) {
    throw new Error(
      `prior ${label} ${expectedPhase} VM turn metric is not exact`,
    );
  }
}

function validatePriorPreviewEvidence(evidence, plan, provisionalMetrics) {
  if (
    evidence?.status !== "passed"
    || evidence.exactSameReceipt !== true
    || evidence.exactSameStateRoot !== true
    || evidence.navigationBeforeFinality !== 0
  ) {
    throw new Error("prior Gate 5 preview evidence is not exact");
  }
  for (const label of ["a", "b"]) {
    const preview = evidence[label];
    const fields = preview?.fields;
    if (
      typeof preview?.receipt !== "string"
      || preview.receipt.length === 0
      || preview.receipt.length > 128 * 1024
      || !exactStableJson(fields, statusFields(preview.receipt))
      || Object.keys(fields ?? {}).sort().join(",")
        !== [
          "action",
          "navigation",
          "receipt",
          "receipt_sha256",
          "room_epoch",
          "script_cid",
          "spot",
          "state_root",
        ].join(",")
      || fields.spot !== "door"
      || fields.action !== plan.doorActionId
      || fields.navigation !== "0"
      || fields.state_root !== plan.doorState.openedStateRootHex
      || fields.script_cid !== report.catalog.byId["script-door"].cid
      || !/^[0-9]+$/.test(fields.room_epoch)
      || !isHex64(fields.receipt_sha256)
      || typeof fields.receipt !== "string"
      || fields.receipt.length === 0
      || fields.receipt.length > 64 * 1024
      || Buffer.from(fields.receipt, "base64url").toString("base64url")
        !== fields.receipt
      || sha256(Buffer.from(fields.receipt, "base64url"))
        !== fields.receipt_sha256
      || typeof preview.elapsedMs !== "number"
      || !Number.isFinite(preview.elapsedMs)
      || preview.elapsedMs < 0
      || !Number.isSafeInteger(preview.totalMs)
      || preview.totalMs < 0
    ) {
      throw new Error(`prior Gate 5 preview ${label} is not exact`);
    }
  }
  if (
    evidence.a.fields.receipt !== evidence.b.fields.receipt
    || evidence.a.fields.receipt_sha256
      !== evidence.b.fields.receipt_sha256
    || evidence.a.fields.state_root !== evidence.b.fields.state_root
  ) {
    throw new Error("prior Gate 5 preview vectors differ");
  }
  for (const label of ["a", "b"]) {
    validateStoredVmTurnMetric(
      provisionalMetrics?.[label],
      label,
      evidence[label].fields.receipt_sha256,
    );
  }
}

function nearestRankPercentile(values, percentile) {
  const sorted = [...values].sort((left, right) => left - right);
  return sorted[Math.ceil(percentile * sorted.length) - 1];
}

function monotonicLatencySummary(values) {
  if (
    !Array.isArray(values)
    || values.length === 0
    || values.some(
      (value) => !Number.isSafeInteger(value) || value < 0,
    )
  ) {
    throw new Error("invalid monotonic latency samples");
  }
  return {
    sampleCount: values.length,
    p50Ms: nearestRankPercentile(values, 0.50),
    p95Ms: nearestRankPercentile(values, 0.95),
    maxMs: Math.max(...values),
  };
}

async function measureApplicationRoundTrip(worker) {
  const encoder = new TextEncoder();
  const payloads = [
    "",
    "\u00e9".repeat(128),
    "\u00e9".repeat(2048),
  ];
  const expectedSizes = [0, 256, 4096];
  const samplesPerSize = 20;
  const measurements = {};
  for (let index = 0; index < payloads.length; index += 1) {
    const payload = payloads[index];
    const requestBytes = encoder.encode(payload).byteLength;
    if (requestBytes !== expectedSizes[index]) {
      throw new Error(`application payload size ${requestBytes} is not exact`);
    }
    const samples = [];
    for (let sample = 0; sample < samplesPerSize; sample += 1) {
      const result = await invoke(
        worker,
        "acceptanceApplicationRoundTrip",
        [payload],
        { exact: payload },
        30_000,
      );
      const responseBytes = encoder.encode(result.receipt).byteLength;
      if (result.receipt !== payload || responseBytes !== requestBytes) {
        throw new Error("application round-trip payload changed");
      }
      samples.push({
        ordinal: sample + 1,
        requestUtf8Bytes: requestBytes,
        responseUtf8Bytes: responseBytes,
        roundTripMs: result.elapsedMs,
      });
    }
    measurements[String(requestBytes)] = {
      payloadUtf8Bytes: requestBytes,
      requestUtf8Bytes: requestBytes,
      responseUtf8Bytes: requestBytes,
      latency: monotonicLatencySummary(
        samples.map(({ roundTripMs }) => roundTripMs),
      ),
      samples,
    };
  }
  const rejectedSize = await invoke(
    worker,
    "acceptanceApplicationRoundTrip",
    ["x"],
    { exact: "rejected=application-round-trip-size" },
    30_000,
  );
  return {
    status: "passed",
    clock: "worker performance.now monotonic milliseconds",
    startBoundary:
      "immediately before inspector invokes the QML UI-backend call",
    endBoundary:
      "invocationSequence advanced and exact raw echo property was observed",
    payloadSemantics: "application UTF-8 bytes; not transport wire bytes",
    samplesPerSize,
    measurements,
    rejectedUnsupportedSize: rejectedSize.receipt,
  };
}

async function gate5VmTurnMetric(worker, phaseName, expectedReceiptSha256) {
  const result = await invoke(
    worker,
    "gate5VmTurnMetrics",
    [report.plan.doorActionId, phaseName],
    { prefix: "status=available;" },
    30_000,
  );
  const fields = statusFields(result.receipt);
  if (
    fields.status !== "available"
    || fields.action !== report.plan.doorActionId
    || fields.phase !== phaseName
    || fields.clock !== "steady_clock"
    || !/^[0-9]+$/.test(fields.duration_ns ?? "")
    || fields.receipt_sha256 !== expectedReceiptSha256
  ) {
    throw new Error(
      `invalid Gate 5 VM ${phaseName} metric: ${result.receipt}`,
    );
  }
  const durationNs = BigInt(fields.duration_ns);
  if (durationNs > 60n * 60n * 1_000_000_000n) {
    throw new Error(`Gate 5 VM ${phaseName} duration is out of bounds`);
  }
  return {
    actionId: report.plan.doorActionId,
    phase: phaseName,
    clock: fields.clock,
    durationNs: fields.duration_ns,
    durationMs: Number(durationNs) / 1_000_000,
    receiptSha256: fields.receipt_sha256,
  };
}

async function finalizeDoor(worker) {
  const action = report.actions.find(
    ({ actionId }) => actionId === report.plan.doorActionId,
  );
  if (!action) {
    throw new Error("Gate 5 action timing record is missing");
  }
  action.timings ??= {};
  action.timingMeasurement ??= {};
  action.timingBoundaries ??= {};
  const unavailableTiming = recoverPersistedTimingEvidence(
    action,
    action.journal?.durableStage,
  );
  if (action.journal || action.hasPriorTimingRecord === true) {
    for (const field of ["submitMs", "totalMs"]) {
      const started =
        action.timingBoundaries[timingBoundaryNames(field).started];
      if (!Number.isSafeInteger(started) || started <= 0) {
        unavailableTiming.push(field);
      }
    }
  }
  const uniqueUnavailableTiming = [...new Set(unavailableTiming)];
  if (uniqueUnavailableTiming.length > 0) {
    for (const field of uniqueUnavailableTiming) {
      markRecoveredTimingUnmeasured(action, field);
    }
    await checkpointReport();
    throw new Error(
      `Gate 5 cannot recover exact timing: ${uniqueUnavailableTiming.join(",")}`,
    );
  }

  const observeGate5Response = (evidence, fields) => {
    const completedAtUnixMs =
      evidence.observationTiming.completedAtUnixMs;
    const accepted = [
      "submitted_to_lez",
      "observed",
      "finalized",
    ].includes(fields.durable);
    if (accepted && fields.action !== report.plan.doorActionId) {
      throw new Error(
        `Gate 5 timing response action mismatch: ${evidence.receipt}`,
      );
    }

    let changed = false;
    if (
      accepted
      && !isCompleteTimingMeasurement(
        "submitMs",
        action.timingMeasurement.submitMs,
        action.timings.submitMs,
      )
    ) {
      completeTimingBoundary(
        action,
        "submitMs",
        completedAtUnixMs,
      );
      const observeNames = timingBoundaryNames("observeMs");
      if (
        action.timingBoundaries[observeNames.started] !== undefined
        || action.timingBoundaries[observeNames.completed] !== undefined
      ) {
        throw new Error(
          "Gate 5 observation boundary predates durable submission acceptance",
        );
      }
      startTimingBoundary(
        action,
        "observeMs",
        completedAtUnixMs,
      );
      changed = true;
    } else if (
      accepted
      && action.timingBoundaries[
        timingBoundaryNames("observeMs").started
      ] === undefined
    ) {
      startTimingBoundary(
        action,
        "observeMs",
        completedTimingTimestamp(action, "submitMs"),
      );
      changed = true;
    }

    if (!["observed", "finalized"].includes(fields.durable)) {
      if (fields.durable) action.status = fields.durable;
      return changed;
    }

    let observationCompletedHere = false;
    if (
      !isCompleteTimingMeasurement(
        "observeMs",
        action.timingMeasurement.observeMs,
        action.timings.observeMs,
      )
    ) {
      completeTimingBoundary(
        action,
        "observeMs",
        completedAtUnixMs,
      );
      observationCompletedHere = true;
      changed = true;
    }
    if (fields.durable === "observed") {
      if (
        action.timingBoundaries[
          timingBoundaryNames("finalityMs").started
        ] === undefined
      ) {
        startTimingBoundary(
          action,
          "finalityMs",
          completedAtUnixMs,
        );
        changed = true;
      }
      action.status = "observed";
      return changed;
    }

    if (
      !isCompleteTimingMeasurement(
        "finalityMs",
        action.timingMeasurement.finalityMs,
        action.timings.finalityMs,
      )
    ) {
      if (observationCompletedHere) {
        const finalityNames = timingBoundaryNames("finalityMs");
        if (
          action.timingBoundaries[finalityNames.started] !== undefined
          || action.timingBoundaries[finalityNames.completed] !== undefined
        ) {
          throw new Error(
            "Gate 5 finality boundary predates observation",
          );
        }
        setCoalescedFinalityTiming(action, completedAtUnixMs);
      } else {
        startTimingBoundary(
          action,
          "finalityMs",
          completedTimingTimestamp(action, "observeMs"),
        );
        completeTimingBoundary(
          action,
          "finalityMs",
          completedAtUnixMs,
        );
      }
      changed = true;
    }
    if (
      !isCompleteTimingMeasurement(
        "totalMs",
        action.timingMeasurement.totalMs,
        action.timings.totalMs,
      )
    ) {
      completeTimingBoundary(
        action,
        "totalMs",
        completedAtUnixMs,
      );
      changed = true;
    }
    action.status = "finalized";
    return changed;
  };

  const use = await invoke(
    worker,
    "gate5UseDoor",
    [],
    undefined,
    120_000,
  );
  startTimingBoundary(
    action,
    "totalMs",
    use.observationTiming.startedAtUnixMs,
  );
  startTimingBoundary(
    action,
    "submitMs",
    use.observationTiming.startedAtUnixMs,
  );
  if (
    use.receipt.startsWith("rejected=")
    && !use.receipt.startsWith(
      "rejected=spot-lez-submit-pending;",
    )
  ) {
    throw new Error(`Gate 5 use rejected: ${use.receipt}`);
  }
  const pendingEvidence = [];
  const firstFields = statusFields(use.receipt);
  if (
    firstFields.action !== report.plan.doorActionId
    || firstFields.vm === "promoted"
    || firstFields.durable === "finalized"
  ) {
    throw new Error(
      `Gate 5 did not expose pending state after submit: ${use.receipt}`,
    );
  }
  observeGate5Response(use, firstFields);
  pendingEvidence.push(use.receipt);
  action.status = firstFields.durable ?? action.status ?? "pending";
  await checkpointReport();

  const delayedObservationStartedAtUnixMs = Date.now();
  await sleep(1_500);
  const delayed = await invoke(
    worker,
    "gate5ActionStatus",
    [],
    undefined,
    30_000,
  );
  const delayedFields = statusFields(delayed.receipt);
  const delayedObservationMs =
    Date.now() - delayedObservationStartedAtUnixMs;
  if (
    delayedObservationMs < 1_000
    || delayedFields.action !== report.plan.doorActionId
    || delayedFields.vm === "promoted"
    || delayedFields.durable === "finalized"
  ) {
    throw new Error(
      `Gate 5 delayed LEZ state was not recoverable: ${delayed.receipt}`,
    );
  }
  observeGate5Response(delayed, delayedFields);
  pendingEvidence.push(delayed.receipt);
  report.failureEvidence.delayedLezUpdate = {
    status: "passed",
    actionId: report.plan.doorActionId,
    observationPaused: true,
    pauseClock: "Date.now wall-clock milliseconds",
    pauseMs: delayedObservationMs,
    before: use.receipt,
    after: delayed.receipt,
    recoverableState: delayedFields.durable,
  };
  report.uiEvidence.pending.push(
    use.receipt,
    delayed.receipt,
  );
  await captureScreenshot(screenshotSpecs.gate5PendingB);
  await checkpointReport();

  let final;
  const deadline = Date.now() + 20 * 60_000;
  while (Date.now() < deadline) {
    const status = await invoke(
      worker,
      "gate5ActionStatus",
      [],
      undefined,
      30_000,
    );
    const fields = statusFields(status.receipt);
    const timingChanged = observeGate5Response(status, fields);
    if (
      fields.vm === "promoted"
      && fields.action === report.plan.doorActionId
      && fields.durable === "finalized"
      && fields.navigation === "1"
      && fields.state_root === report.plan.doorState.openedStateRootHex
    ) {
      final = status;
      await checkpointReport();
      break;
    }
    if (
      fields.vm === "degraded"
      || status.receipt.startsWith("rejected=spot-degraded")
    ) {
      throw new Error(`Gate 5 degraded: ${status.receipt}`);
    }
    if (fields.vm && fields.vm !== "promoted") {
      pendingEvidence.push(status.receipt);
    }
    if (timingChanged) {
      await checkpointReport();
    }
    const reconciled = await invoke(
      worker,
      "gate5Reconcile",
      [],
      undefined,
      120_000,
    );
    if (reconciled.receipt.startsWith("rejected=spot-degraded")) {
      throw new Error(`Gate 5 reconcile degraded: ${reconciled.receipt}`);
    }
    if (
      reconciled.receipt.startsWith("rejected=")
      && !reconciled.receipt.startsWith(
        "rejected=spot-lez-submit-pending;",
      )
    ) {
      throw new Error(
        `Gate 5 reconcile rejected: ${reconciled.receipt}`,
      );
    }
    const reconciledFields = statusFields(reconciled.receipt);
    const reconciledTimingChanged = observeGate5Response(
      reconciled,
      reconciledFields,
    );
    if (
      reconciledFields.vm === "promoted"
      && reconciledFields.action === report.plan.doorActionId
      && reconciledFields.durable === "finalized"
      && reconciledFields.navigation === "1"
      && reconciledFields.state_root
        === report.plan.doorState.openedStateRootHex
    ) {
      final = reconciled;
      await checkpointReport();
      break;
    }
    if (reconciledTimingChanged) {
      await checkpointReport();
    }
    await sleep(500);
  }
  if (!final) throw new Error("Gate 5 door finality timed out");
  const properties = await worker.call("properties");
  if (
    String(properties.gate5RoomTitle) !== "Lounge"
    || statusFields(String(properties.gate5Status)).navigation !== "1"
  ) {
    throw new Error("Gate 5 finalized before Lounge UI projection");
  }
  return {
    use,
    pendingEvidence,
    final,
    roomTitle: String(properties.gate5RoomTitle),
    timings: { ...action.timings },
    timingMeasurement: { ...action.timingMeasurement },
    timingBoundaries: { ...action.timingBoundaries },
    totalMs: action.timings.totalMs,
  };
}

async function recoverFinalizedDoor(worker) {
  const startedAt = performance.now();
  const deadline = Date.now() + 3 * 60_000;
  let final;
  while (Date.now() < deadline) {
    const status = await invoke(
      worker,
      "gate5ActionStatus",
      [],
      undefined,
      30_000,
    );
    const fields = statusFields(status.receipt);
    if (
      fields.vm === "promoted"
      && fields.action === report.plan.doorActionId
      && fields.durable === "finalized"
      && fields.navigation === "1"
      && fields.state_root === report.plan.doorState.openedStateRootHex
    ) {
      final = status;
      break;
    }
    if (
      fields.vm === "degraded"
      || status.receipt.startsWith("rejected=spot-degraded")
    ) {
      throw new Error(
        `resumed Gate 5 turn degraded: ${status.receipt}`,
      );
    }
    await sleep(500);
  }
  if (!final) {
    throw new Error(
      "finalized action 10 could not be promoted without resubmission",
    );
  }
  const properties = await worker.call("properties");
  if (
    String(properties.gate5RoomTitle) !== "Lounge"
    || statusFields(String(properties.gate5Status)).navigation !== "1"
  ) {
    throw new Error("resumed Gate 5 UI did not recover Lounge");
  }
  return {
    use: {
      skipped: true,
      reason: "finalized-journal-and-history-resume",
    },
    pendingEvidence: [],
    final,
    roomTitle: String(properties.gate5RoomTitle),
    totalMs: Math.round(performance.now() - startedAt),
    recoveredWithoutSubmission: true,
  };
}

try {
  phase = "gate3-preflight";
  const liveReleasePreflight = await runPalaceReleasePreflight();
  report.release.programDeployment =
    liveReleasePreflight.programDeployment;
  report.release.rootAccountBeforeWrites =
    liveReleasePreflight.rootAccountBeforeWrites;
  const gate3 = await readJson(gate3ReportPath);
  if (
    gate3.productionIdentityMode !== true
    || !gate3.identities
    || !gate3.storageConfigs
  ) {
    throw new Error(
      "Gate 4-6 requires Gate 3 production identities in shared state",
    );
  }
  report.release.gate3Preflight = gate3.releasePreflight;
  report.release.gate3PreflightSha256 = sha256(
    JSON.stringify(gate3.releasePreflight),
  );
  report.release.revalidation = validateGate3ReleasePreflight(
    gate3.releasePreflight,
    liveReleasePreflight,
  );
  await checkpointReport();
  gate3IdentityEvidence = gate3.identities;
  const gate3StorageTcpPorts = new Set();
  const gate3StorageUdpPorts = new Set();
  for (const label of labels) {
    const identity = gate3.identities[label];
    const config = gate3.storageConfigs[label];
    if (
      !isHex64(identity?.accountId)
      || !isHex64(identity?.deliveryKey)
      || identity?.display !== displayNames[label]
      || !isHex64(identity?.registrationTransaction)
      || typeof config !== "string"
      || Buffer.byteLength(config, "utf8") > 64 * 1024
    ) {
      throw new Error(`Gate 3 production evidence invalid for ${label}`);
    }
    const parsedConfig = parseJsonObject(
      config,
      `Gate 3 Storage config ${label}`,
    );
    if (
      !exactProductionStorageConfig(parsedConfig)
      || gate3StorageTcpPorts.has(parsedConfig["listen-port"])
      || gate3StorageUdpPorts.has(parsedConfig["disc-port"])
    ) {
      throw new Error(`Gate 3 Storage config ${label} is not production-safe`);
    }
    gate3StorageTcpPorts.add(parsedConfig["listen-port"]);
    gate3StorageUdpPorts.add(parsedConfig["disc-port"]);
  }
  for (const field of [
    "accountId",
    "deliveryKey",
    "registrationTransaction",
  ]) {
    if (
      new Set(labels.map((label) => gate3.identities[label][field])).size
      !== labels.length
    ) {
      throw new Error(`Gate 3 production identities reuse ${field}`);
    }
  }
  const storageConfigs = Object.fromEntries(
    labels.map((label) => [label, gate3.storageConfigs[label]]),
  );
  const catalog = validateGate3(
    gate3,
    currentPackageHashes,
    currentBasecampDigest,
  );
  const propStoryRequested = catalog.propId !== null;
  const finalActionCheckpoint =
    8 + 1 + Number(propStoryRequested);
  const finalActionId = String(finalActionCheckpoint);
  const assetAuthoringScreenshot = await screenshotFileEvidenceAt(
    dirname(gate3ReportPath),
    catalog.assetAuthoringEvidence.screenshot.file,
  );
  if (
    assetAuthoringScreenshot.width
      !== catalog.assetAuthoringEvidence.screenshot.width
    || assetAuthoringScreenshot.height
      !== catalog.assetAuthoringEvidence.screenshot.height
    || assetAuthoringScreenshot.byteLength
      !== catalog.assetAuthoringEvidence.screenshot.byteLength
    || assetAuthoringScreenshot.sha256
      !== catalog.assetAuthoringEvidence.screenshot.sha256
  ) {
    throw new Error(
      "Gate 3 asset authoring screenshot artifact does not match report",
    );
  }
  report.gate3.productSnapshot = gate3.productSnapshot;
  report.gate3.catalogChecksum = gate3.publication.checksum;
  report.gate3.assetAuthoringEvidence = {
    ...catalog.assetAuthoringEvidence,
    screenshot: {
      ...catalog.assetAuthoringEvidence.screenshot,
      artifactVerified: true,
    },
  };
  report.catalog = {
    checksum: catalog.checksum,
    encoded: catalog.encoded,
    encodedSha256: sha256(catalog.encoded),
    canonicalSha256: sha256(catalog.canonical),
    byId: catalog.byId,
  };
  for (const label of labels) {
    report.installedPackages[label] = await readJson(
      join(artifactsDir, `installed-packages-${label}.json`),
    );
    report.installedRoots[label] = await readJson(
      join(artifactsDir, `installed-roots-${label}.json`),
    );
  }
  const processArtifacts = await buildProcessRuntimeArtifacts(
    report.installedPackages,
    report.installedRoots,
    currentPackageHashes,
  );
  processRuntimeArtifacts = processArtifacts.publicEvidence;
  processLoaderSelection = processArtifacts.loaderSelection;
  expectedProcessArtifactsByLabel =
    processArtifacts.expectedArtifactsByLabel;
  report.processModel.loaderSelection = processLoaderSelection;
  report.processModel.runtimeArtifacts = processRuntimeArtifacts;

  const ports = await ephemeralTcpPorts(6, gate3StorageTcpPorts);
  const inspectorPorts = ports.slice(0, 3);
  const deliveryTcpPorts = ports.slice(3, 6);
  report.inspectorPorts = Object.fromEntries(
    labels.map((label, index) => [label, inspectorPorts[index]]),
  );
  report.networkPorts = {
    storageConfigSha256: Object.fromEntries(
      labels.map((label) => [
        label,
        sha256(gate3.storageConfigs[label]),
      ]),
    ),
    deliveryTcp: Object.fromEntries(
      labels.map((label, index) => [label, deliveryTcpPorts[index]]),
    ),
  };
  const expectedTcpListeners = (
    label,
    { delivery = false, storage = false } = {},
  ) => {
    const labelIndex = labels.indexOf(label);
    if (labelIndex < 0) {
      throw new Error(`unknown process listener label ${label}`);
    }
    const listeners = [{
      protocol: "tcp4",
      address: "127.0.0.1",
      port: inspectorPorts[labelIndex],
      ownerRole: "basecamp-main",
      moduleName: null,
      purpose: "qml-inspector",
    }];
    if (delivery) {
      listeners.push({
        protocol: "tcp4",
        address: "127.0.0.1",
        port: deliveryTcpPorts[labelIndex],
        ownerRole: "core-module-host",
        moduleName: "delivery_module",
        purpose: "delivery-transport",
      });
    }
    if (storage) {
      const storageConfig = parseJsonObject(
        storageConfigs[label],
        `process listener Storage config ${label}`,
      );
      listeners.push({
        protocol: "tcp4",
        address: "0.0.0.0",
        port: storageConfig["listen-port"],
        ownerRole: "core-module-host",
        moduleName: "storage_module",
        purpose: "storage-transport",
      });
    }
    return listeners;
  };
  await checkpointReport();

  const priorCreatorPhase =
    previousReport?.gate6?.lifecycle?.phase;
  const resumeWithoutCreator =
    priorCreatorPhase === "creator-stop-pending"
    || priorCreatorPhase === "creator-offline"
    || previousReport?.gate6?.creator?.offline === true;
  const priorCreatorPid = Number(
    previousReport?.gate6?.creator?.pid,
  );
  let creatorIdentity;
  if (resumeWithoutCreator) {
    if (
      previousReport?.gate5?.convergence?.status !== "passed"
      || !Number.isSafeInteger(priorCreatorPid)
      || priorCreatorPid <= 0
    ) {
      throw new Error(
        "creator-offline resume evidence is incomplete",
      );
    }
    creatorIdentity = validateCreatorProcessIdentity(
      previousReport?.gate6?.creator?.processIdentity,
      priorCreatorPid,
    );
    if (await originalCreatorProcessExists(creatorIdentity)) {
      throw new Error(
        "persisted creator remains live after prior scope retirement",
      );
    }
  }
  report.gate6.resumeWithoutCreator = resumeWithoutCreator;
  delete report.gate6.recoveredCreatorProcess;

  phase = "initial-start";
  const initialLabels = resumeWithoutCreator ? ["b", "c"] : labels;
  for (const label of initialLabels) {
    const index = labels.indexOf(label);
    const worker = new WorkerClient(
      label,
      inspectorPorts[index],
      "initial",
    );
    workers.set(label, worker);
    report.startup[label] = await worker.init();
    report.startup[label].lez = await startLez(worker, []);
    report.startup[label].memory = await processMetrics(
      worker.basecampPid,
      expectedTcpListeners(label),
      label,
    );
  }
  const inactivePropObservers = [];
  for (const label of initialLabels) {
    const properties = await workers.get(label).call(
      "properties",
      {},
      30_000,
    );
    inactivePropObservers.push({
      label,
      projection: parseActivePropProjection(
        properties.gate4ActivePropAsset,
        `Gate 4 restored prop projection on ${label}`,
        false,
      ),
    });
  }
  report.gate3.activePropProjectionRecovery = {
    status: "restored-unverified",
    property: "gate4ActivePropAsset",
    beforeVerification: inactivePropObservers,
  };
  await checkpointReport();

  phase = "application-round-trip-metrics";
  report.applicationRoundTrip = await measureApplicationRoundTrip(
    workers.get("b"),
  );
  await checkpointReport();

  const palaceId = stableId("palace");
  const palaceUri = `palace://${palaceId}`;
  phase = "existing-prefix-probe";
  let priorActionZero = previousReport?.actions?.find(
    ({ actionId }) => actionId === "0",
  );
  if (
    report.release.rootAccountBeforeWrites.state === "initialized"
    && !priorActionZero
  ) {
    const journal = await actionJournalRecord("a", "0", true);
    if (journal && [2, 3, 4].includes(journal.stage)) {
      priorActionZero = {
        actionId: "0",
        kind: "initialize",
        caller: "a",
        transactionHash: journal.transactionHash,
        status: journal.stage === 4 ? "finalized" : journal.durableStage,
        journal,
        recoveredAfterDurabilityGap: true,
      };
    }
  }
  if (
    report.release.rootAccountBeforeWrites.state === "initialized"
    && priorActionZero?.kind === "initialize"
    && priorActionZero?.caller === "a"
    && isHex64(priorActionZero?.transitionSha256)
  ) {
    const journal = await actionJournalRecord("a", "0");
    if (
      ![2, 3, 4].includes(journal.stage)
      || !isHex64(journal.transactionHash)
      || (
        isHex64(priorActionZero.transactionHash)
        && priorActionZero.transactionHash !== journal.transactionHash
      )
    ) {
      throw new Error("action-zero report/journal evidence mismatch");
    }
    priorActionZero = {
      ...priorActionZero,
      transactionHash: journal.transactionHash,
      status: journal.stage === 4
        ? "finalized"
        : journal.durableStage,
      journal,
      recoveredBeforeHistory: true,
    };
    report.actions = report.actions.filter(
      ({ actionId }) => actionId !== "0",
    );
    report.actions.push(priorActionZero);
    await checkpointReport();
  }
  let finalizedPrefix = -1;
  const priorActionZeroFinalized =
    priorActionZero?.status === "finalized"
    && isHex64(priorActionZero.transactionHash);
  const priorActionZeroRecoverable =
    priorActionZero?.actionId === "0"
    && priorActionZero?.kind === "initialize"
    && priorActionZero?.caller === "a"
    && isHex64(priorActionZero?.transactionHash);
  if (
    report.release.rootAccountBeforeWrites.state === "initialized"
    && !priorActionZeroFinalized
    && !priorActionZeroRecoverable
  ) {
    throw new Error(
      "initialized release root lacks exact prior action-zero evidence",
    );
  }
  if (
    report.release.rootAccountBeforeWrites.state === "uninitialized"
    && priorActionZeroFinalized
  ) {
    throw new Error(
      "uninitialized release root contradicts prior finalized evidence",
    );
  }
  if (priorActionZeroFinalized) {
    const probe = await waitPalaceTerminal(
      workers.get("b"),
      palaceUri,
      false,
    );
    finalizedPrefix = probe.action;
    report.checkpoints.initialProbe = probe.status.receipt;
    report.checkpoints.initialProbeMode =
      "exact-prior-finalized-evidence";
  } else {
    report.checkpoints.initialProbe =
      "skipped=no-exact-prior-finalized-action-zero";
    report.checkpoints.initialProbeMode =
      "fresh-or-local-journal-resume";
  }
  report.detectedFinalizedPrefix = finalizedPrefix;
  if (finalizedPrefix > finalActionCheckpoint) {
    throw new Error(`unexpected finalized Palace action ${finalizedPrefix}`);
  }
  if (
    resumeWithoutCreator
    && finalizedPrefix !== finalActionCheckpoint
  ) {
    throw new Error(
      "creator-offline resume requires finalized door action checkpoint",
    );
  }
  phase = "identities";
  for (const label of labels) {
    if (resumeWithoutCreator && label === "a") {
      const prior = previousReport?.identities?.a;
      const gate3Identity = gate3IdentityEvidence.a;
      if (
        prior?.accountId !== gate3Identity.accountId
        || prior?.deliveryKey !== gate3Identity.deliveryKey
        || prior?.display !== gate3Identity.display
        || prior?.registrationTransaction
          !== gate3Identity.registrationTransaction
      ) {
        throw new Error(
          "offline creator identity differs from Gate 3/resume report",
        );
      }
      report.identities.a = {
        ...prior,
        offline: true,
        reusedWithoutProcess: true,
      };
      await checkpointReport();
      continue;
    }
    const ensured = await ensureIdentity(
      workers.get(label),
      gate3IdentityEvidence[label],
    );
    report.identities[label] = {
      ...ensured.identity,
      display: displayNames[label],
      existing: ensured.existing,
      receipt: ensured.receipt,
    };
    const gate3Identity = gate3IdentityEvidence[label];
    if (
      gate3Identity.accountId !== ensured.identity.accountId
      || gate3Identity.deliveryKey !== ensured.identity.deliveryKey
      || gate3Identity.display !== ensured.identity.display
    ) {
      throw new Error(
        `persistent production identity ${label} differs from Gate 3`,
      );
    }
    const prior = previousReport?.identities?.[label];
    if (
      finalizedPrefix >= 0 &&
      (prior?.accountId !== ensured.identity.accountId ||
        prior?.deliveryKey !== ensured.identity.deliveryKey)
    ) {
      throw new Error(`persistent identity ${label} does not match resume report`);
    }
    await checkpointReport();
  }

  phase = "plan";
  const plan = buildPlan(
    report.identities,
    catalog.byId,
    catalog.propObjectIds?.manifest ?? null,
  );
  if (
    plan.propStory !== (
      propStoryRequested ? "requested" : "not-requested"
    )
    || plan.doorActionId !== finalActionId
    || plan.actions.length !== finalActionCheckpoint + 1
  ) {
    throw new Error("dynamic Palace story plan is inconsistent");
  }
  report.plan = publicPlan(plan);
  if (resumeWithoutCreator) {
    validatePriorCreatorOfflineEvidence(
      previousReport,
      plan,
      priorCreatorPid,
    );
  }
  report.actions = await validateResumeEvidence(finalizedPrefix, plan);
  await checkpointReport();
  const resumedWithoutExactTiming = report.actions.flatMap((action) =>
    lezStageTimingFields
      .filter(
        (field) =>
          !isCompleteTimingMeasurement(
            field,
            action.timingMeasurement?.[field],
            action.timings?.[field],
          ),
      )
      .map((field) => `${action.actionId}:${field}`));
  if (resumedWithoutExactTiming.length > 0) {
    throw new Error(
      `finalized resume lacks exact persisted timing: ${resumedWithoutExactTiming.join(",")}`,
    );
  }

  phase = "gate4-actions-zero-through-seven";
  for (
    let index = finalizedPrefix + 1;
    index <= 7;
    index += 1
  ) {
    const action = plan.actions[index];
    const worker = workers.get(action.caller);
    if (index > 0) {
      report.checkpoints[`beforeAction${index}`] = await openExact(
        worker,
        plan,
        index - 1,
      );
    }
    const prior = previousReport?.actions?.find(
      ({ actionId }) => actionId === action.actionId,
    );
    await executeAction(worker, action, plan, prior);
  }

  if (finalizedPrefix <= 7) {
    phase = "checkpoint-seven-all-clients";
    for (const label of labels) {
      report.checkpoints[`action7${label.toUpperCase()}`] =
        await openExact(workers.get(label), plan, 7);
    }
    finalizedPrefix = 7;
  }
  await checkpointReport();

  phase = "production-storage";
  if (resumeWithoutCreator) {
    if (
      report.storage.initial?.status !== "passed"
      || report.storage.initial?.productionIdentityHolders !== true
      || report.storage.initial?.acceptanceHolderProfile !== false
    ) {
      throw new Error(
        "creator-offline resume lacks passing production Storage evidence",
      );
    }
    report.storage.initial.resumedWithoutCreator = true;
    report.storage.resumePreparation = {
      status: "running",
      clients: {},
      sourceFirst: true,
    };
    for (const label of ["c", "b"]) {
      const retainedDataRootBefore = label === "c"
        ? await storageDataRootEvidence(label)
        : undefined;
      const startup = await startProductionStorage(
        workers.get(label),
        storageConfigs[label],
      );
      const recovered = await fetchProductionCatalog(
        workers.get(label),
        catalog,
      );
      if (
        (
          label === "c"
          && (
            recovered.mode !== "cache"
            || recovered.nativeSource !== "cache"
          )
        )
        || !["cache", "network"].includes(recovered.mode)
        || recovered.nativeSource !== recovered.mode
      ) {
        throw new Error(
          `resume Storage ${label} mode ${recovered.mode} is unsafe`,
        );
      }
      report.storage.resumePreparation.clients[label] = {
        startup,
        recovered,
        retainedDataRootBefore,
      };
      await checkpointReport();
    }
    report.storage.resumePreparation.status = "passed";
    report.gate6.resumeRecovery = {
      status: "passed",
      creatorOffline: true,
      sourceFirst: true,
      retainedSourceLabel: "c",
      retainedSourceMode:
        report.storage.resumePreparation.clients.c.recovered.mode,
      coldClientMode:
        report.storage.resumePreparation.clients.b.recovered.mode,
      coldStateRehydrated:
        report.storage.resumePreparation.clients.b.recovered.mode
          === "network",
    };
    await checkpointReport();
  } else {
    report.storage.initial = {
      status: "running",
      productionIdentityHolders: true,
      acceptanceHolderProfile: false,
      exactGate3Configs: Object.fromEntries(
        labels.map((label) => [label, sha256(storageConfigs[label])]),
      ),
      startup: {},
      catalog: {},
    };
    for (const label of labels) {
      const retainedDataRootBefore =
        await storageDataRootEvidence(label);
      report.storage.initial.startup[label] =
        await startProductionStorage(
          workers.get(label),
          storageConfigs[label],
        );
      report.storage.initial.catalog[label] =
        await fetchProductionCatalog(workers.get(label), catalog);
      report.storage.initial.catalog[label].retainedDataRootBefore =
        retainedDataRootBefore;
      if (
        report.storage.initial.catalog[label].mode !== "cache"
        || report.storage.initial.catalog[label].nativeSource
          !== "cache"
      ) {
        throw new Error(
          `Gate 4 initial Storage ${label} did not reuse Gate 3 cache`,
        );
      }
      await checkpointReport();
    }
    report.storage.initial.status = "passed";
  }
  const expectedActivePropProjection =
    catalog.assetAuthoringEvidence.activePropProjection;
  const propProjectionRequested = catalog.propId !== null;
  const activePropObservers = [];
  for (const label of initialLabels) {
    const properties = await workers.get(label).call(
      "properties",
      {},
      30_000,
    );
    const projection = parseActivePropProjection(
      properties.gate4ActivePropAsset,
      `Gate 4 verified prop projection on ${label}`,
      propProjectionRequested,
    );
    if (!exactJson(projection, expectedActivePropProjection)) {
      throw new Error(
        `Gate 4 verified prop projection differs on ${label}`,
      );
    }
    activePropObservers.push({ label, projection });
  }
  report.gate3.activePropProjectionRecovery = {
    ...report.gate3.activePropProjectionRecovery,
    status: "passed",
    propStory: propProjectionRequested
      ? "requested"
      : "not-requested",
    projectionSha256: sha256(
      JSON.stringify(stableJson(expectedActivePropProjection)),
    ),
    afterVerification: activePropObservers,
  };
  await checkpointReport();
  if (report.failureEvidence.missingStorageObject?.status === "passed") {
    const prior = report.failureEvidence.missingStorageObject;
    if (
      prior.objectId !== "background-atrium"
      || prior.missingSourceCid !== missingStorageFixtureCid
      || prior.derivativeCid
        !== catalog.byId["background-atrium"].cid
      || prior.expectedContentSha256
        !== catalog.byId["background-atrium"].contentSha256
      || prior.states?.join(",") !== "missing,fetching,degraded"
      || !prior.degraded?.receipt?.startsWith("degraded;reason=")
    ) {
      throw new Error("prior missing Storage object evidence is not exact");
    }
    await reusePriorScreenshot(screenshotSpecs.gate4StorageDegraded);
  } else {
    report.failureEvidence.missingStorageObject =
      await proveRecoverableMissingStorageObject(
        workers.get("b"),
        catalog,
      );
    report.uiEvidence.degraded.push(
      report.failureEvidence.missingStorageObject.degraded.receipt,
    );
    await captureScreenshot(screenshotSpecs.gate4StorageDegraded);
  }
  await checkpointReport();

  phase = "production-delivery";
  const deliveryPorts = Object.fromEntries(
    labels.map((label, index) => [label, deliveryTcpPorts[index]]),
  );
  if (resumeWithoutCreator) {
    if (
      report.delivery.initialLifecycle?.status !== "passed"
      || report.delivery.initialLifecycle.propStory
        !== (propStoryRequested ? "requested" : "not-requested")
      || report.delivery.initialLifecycle.approvedPropVisible
        !== propStoryRequested
      || !report.delivery.initialMesh
    ) {
      throw new Error(
        "creator-offline resume lacks production Delivery evidence",
      );
    }
    report.delivery.initialMesh.resumedWithoutCreator = true;
    await reusePriorScreenshot(screenshotSpecs.gate4Convergence);
  } else {
    report.delivery.initialMesh = await startDeliveryMesh(
      labels,
      deliveryPorts,
      "a",
    );
    if (finalizedPrefix <= 7) {
      report.delivery.initialLifecycle =
        await verifyInitialDeliveryLifecycle(catalog.propId);
      await captureScreenshot(screenshotSpecs.gate4Convergence);
      if (propStoryRequested) {
        Object.assign(
          report.delivery.initialLifecycle,
          await removeApprovedPropAfterEvidence(catalog.propId),
        );
      }
    } else if (report.delivery.initialLifecycle?.status !== "passed") {
      throw new Error(
        "resumed post-ban prefix lacks production Delivery lifecycle evidence",
      );
    } else {
      await reusePriorScreenshot(screenshotSpecs.gate4Convergence);
    }
  }
  await checkpointReport();

  if (finalizedPrefix <= 7) {
    phase = "unauthorized-moderation";
    const unauthorizedAction = plan.actions[8];
    const priorUnauthorized =
      previousReport?.moderation?.unauthorized;
    if (priorUnauthorized?.status === "passed") {
      const queued = await actionStatus(
        workers.get("c"),
        unauthorizedAction.actionId,
      );
      const unchanged = await openExact(workers.get("b"), plan, 7);
      if (
        priorUnauthorized.caller !== "c"
        || priorUnauthorized.callerAccountId
          !== report.identities.c.accountId
        || priorUnauthorized.moduleRejected?.receipt
          !== "rejected=lez-submit;reason=module-rejected"
        || queued.fields.durable !== "queued"
        || queued.fields.finality !== "not-started"
      ) {
        throw new Error(
          "prior unauthorized moderation evidence is not exact",
        );
      }
      report.moderation.unauthorized = {
        ...priorUnauthorized,
        durableStatus: queued,
        chainUnchanged: unchanged,
        resumedWithoutResubmission: true,
      };
    } else {
      const unauthorized = await invoke(
        workers.get("c"),
        "gate4Submit",
        [
          unauthorizedAction.actionId,
          plan.rootAccountId,
          report.identities.c.accountId,
          plan.programId,
          unauthorizedAction.transitionJson,
        ],
        { exact: "rejected=lez-submit;reason=module-rejected" },
        120_000,
      );
      const queued = await actionStatus(
        workers.get("c"),
        unauthorizedAction.actionId,
      );
      if (
        queued.fields.durable !== "queued"
        || queued.fields.finality !== "not-started"
      ) {
        throw new Error(
          `unauthorized action not recoverable queued state: ${queued.receipt}`,
        );
      }
      const unchanged = await openExact(workers.get("b"), plan, 7);
      report.moderation.unauthorized = {
        status: "passed",
        caller: "c",
        callerAccountId: report.identities.c.accountId,
        moduleRejected: unauthorized,
        durableStatus: queued,
        recoverable: true,
        chainUnchanged: unchanged,
      };
    }
  } else if (report.moderation.unauthorized?.status !== "passed") {
    throw new Error(
      "resumed post-ban prefix lacks unauthorized moderation evidence",
    );
  }
  await checkpointReport();

  if (finalizedPrefix < 8) {
    phase = "user-ban-action-eight";
    const action = plan.actions[8];
    await executeAction(
      workers.get(action.caller),
      action,
      plan,
      previousReport?.actions?.find(({ actionId }) => actionId === "8"),
    );
    finalizedPrefix = 8;
  }
  if (finalizedPrefix === 8) {
    report.checkpoints.action8A =
      await openExact(workers.get("a"), plan, 8);
    report.checkpoints.action8B =
      await openExact(workers.get("b"), plan, 8);
    if (previousReport?.moderation?.userBan?.status === "passed") {
      const prior = previousReport.moderation.userBan;
      validatePriorUserBanEvidence(prior);
      report.moderation.userBan = {
        ...prior,
        resumedWithoutRepublish: true,
      };
    } else {
      phase = "stale-banned-user-delivery";
      const baselines = {
        a: await deliverySnapshot(workers.get("a")),
        b: await deliverySnapshot(workers.get("b")),
      };
      const staleSpeech = await invoke(
        workers.get("c"),
        "gate2Say",
        ["stale Carol signed speech"],
        { prefix: "ok;request=" },
        60_000,
      );
      const rejected = {
        a: await waitRejectedIngress(
          workers.get("a"),
          baselines.a,
          "rejected_other",
          "Alice rejects finalized user ban",
        ),
        b: await waitRejectedIngress(
          workers.get("b"),
          baselines.b,
          "rejected_other",
          "Bob rejects finalized user ban",
        ),
      };
      report.moderation.userBan = {
        status: "passed",
        staleCheckpoint: 7,
        finalizedCheckpoint: 8,
        sender: "c",
        signedSend: staleSpeech,
        receiverBaselines: baselines,
        receiverRejections: rejected,
        projectionMutation: false,
        rejectionClass: "other",
      };
    }
  } else if (report.moderation.userBan?.status !== "passed") {
    throw new Error("resumed action 9+ lacks stale user-ban evidence");
  }
  validatePriorUserBanEvidence(report.moderation.userBan);
  await checkpointReport();

  if (propStoryRequested) {
    if (finalizedPrefix < 9) {
    phase = "asset-ban-action-nine";
    const action = plan.actions[9];
    await executeAction(
      workers.get(action.caller),
      action,
      plan,
      previousReport?.actions?.find(({ actionId }) => actionId === "9"),
    );
    finalizedPrefix = 9;
  }
  if (report.moderation.assetBan?.status !== "passed") {
    phase = "stale-banned-asset-delivery";
    const baseline = await deliverySnapshot(workers.get("b"));
    const staleWear = await invoke(
      workers.get("a"),
      "gate2Wear",
      [catalog.propId],
      { prefix: "ok;request=" },
      60_000,
    );
    const rejected = await waitRejectedIngress(
      workers.get("b"),
      baseline,
      "rejected_payload",
      "Bob rejects finalized asset ban",
    );
    report.moderation.assetBan = {
      status: "passed",
      staleCheckpoint: 8,
      finalizedCheckpoint: 9,
      sender: "a",
      signedSend: staleWear,
      receiverBaseline: baseline,
      receiverRejection: rejected,
      projectionMutation: false,
      rejectionClass: "payload",
    };
  }
  validatePriorAssetBanEvidence(report.moderation.assetBan);
  if (finalizedPrefix === 9) {
    report.checkpoints.action9A =
      await openExact(workers.get("a"), plan, 9);
    report.checkpoints.action9B =
      await openExact(workers.get("b"), plan, 9);
    report.checkpoints.action9C =
      await openExact(workers.get("c"), plan, 9);
    await invoke(
      workers.get("b"),
      "gate4RefreshModeration",
      [],
      { prefix: "state=finalized;kind=prop;action=9;" },
      30_000,
    );
    const userBanAction = report.actions.find(
      ({ actionId }) => actionId === "8",
    );
    const propBanAction = report.actions.find(
      ({ actionId }) => actionId === "9",
    );
    const moderationProperties =
      await workers.get("b").call("properties");
    const moderationState = statusFields(
      String(moderationProperties.gate4ModerationState ?? ""),
    );
    if (
      userBanAction?.submissionMethod !== "gate4BanUser"
      || propBanAction?.submissionMethod !== "gate4BanProp"
      || moderationState.state !== "finalized"
      || moderationState.kind !== "prop"
      || moderationState.action !== "9"
    ) {
      throw new Error(
        "authorized moderation did not use finalized human UI path",
      );
    }
    report.moderation.humanUi = {
      status: "passed",
      userBanMethod: userBanAction.submissionMethod,
      propBanMethod: propBanAction.submissionMethod,
      finalUiState: String(
        moderationProperties.gate4ModerationState,
      ),
      rawTransitionJsonCrossedUiBoundary: false,
    };
    await captureScreenshot(screenshotSpecs.gate4Moderation);
  } else if (finalizedPrefix === finalActionCheckpoint) {
    await reusePriorScreenshot(screenshotSpecs.gate4Moderation);
  } else {
    throw new Error(`unexpected moderation checkpoint ${finalizedPrefix}`);
  }
  } else {
    report.moderation.assetBan = {
      status: "not-requested",
      propStory: "not-requested",
    };
    if (finalizedPrefix === 8) {
      await invoke(
        workers.get("b"),
        "gate4RefreshModeration",
        [],
        { prefix: "state=finalized;kind=user;action=8;" },
        30_000,
      );
      const userBanAction = report.actions.find(
        ({ actionId }) => actionId === "8",
      );
      const moderationProperties =
        await workers.get("b").call("properties");
      const moderationState = statusFields(
        String(moderationProperties.gate4ModerationState ?? ""),
      );
      if (
        userBanAction?.submissionMethod !== "gate4BanUser"
        || moderationState.state !== "finalized"
        || moderationState.kind !== "user"
        || moderationState.action !== "8"
      ) {
        throw new Error(
          "user moderation did not use finalized human UI path",
        );
      }
      report.moderation.humanUi = {
        status: "passed",
        propStory: "not-requested",
        userBanMethod: userBanAction.submissionMethod,
        propBanMethod: null,
        finalUiState: String(
          moderationProperties.gate4ModerationState,
        ),
        rawTransitionJsonCrossedUiBoundary: false,
      };
      await captureScreenshot(screenshotSpecs.gate4Moderation);
    } else if (finalizedPrefix === finalActionCheckpoint) {
      await reusePriorScreenshot(screenshotSpecs.gate4Moderation);
    } else {
      throw new Error(
        `unexpected user-only moderation checkpoint ${finalizedPrefix}`,
      );
    }
  }
  await checkpointReport();

  phase = "gate5-preview";
  if (finalizedPrefix < finalActionCheckpoint) {
    const [previewA, previewB] = await Promise.all([
      previewDoor(workers.get("a")),
      previewDoor(workers.get("b")),
    ]);
    if (
      previewA.fields.action !== previewB.fields.action
      || previewA.fields.state_root !== previewB.fields.state_root
      || previewA.fields.receipt !== previewB.fields.receipt
      || previewA.fields.receipt_sha256
        !== previewB.fields.receipt_sha256
      || previewA.fields.navigation !== "0"
      || previewB.fields.navigation !== "0"
    ) {
      throw new Error("Alice/Bob Gate 5 preview vectors differ");
    }
    report.gate5.preview = {
      status: "passed",
      a: previewA,
      b: previewB,
      exactSameReceipt: true,
      exactSameStateRoot: true,
      navigationBeforeFinality: 0,
    };
    report.gate5.vmTurnMetrics = {
      ...(report.gate5.vmTurnMetrics ?? {}),
      provisional: {
        a: await gate5VmTurnMetric(
          workers.get("a"),
          "provisional",
          previewA.fields.receipt_sha256,
        ),
        b: await gate5VmTurnMetric(
          workers.get("b"),
          "provisional",
          previewB.fields.receipt_sha256,
        ),
      },
    };
    await captureScreenshot(screenshotSpecs.gate5PreviewA);
    await captureScreenshot(screenshotSpecs.gate5PreviewB);
  } else if (report.gate5.preview?.status !== "passed") {
    throw new Error(
      "resumed door action lacks exact two-client preview evidence",
    );
  } else {
    await reusePriorScreenshot(screenshotSpecs.gate5PreviewA);
    await reusePriorScreenshot(screenshotSpecs.gate5PreviewB);
  }
  validatePriorPreviewEvidence(
    report.gate5.preview,
    plan,
    report.gate5.vmTurnMetrics?.provisional,
  );
  await checkpointReport();

  phase = "gate5-finalized-door";
  const doorAction = plan.actions[finalActionCheckpoint];
  if (finalizedPrefix < finalActionCheckpoint) {
    const priorDoorAction = previousReport?.actions?.find(
      ({ actionId }) => actionId === finalActionId,
    );
    if (
      priorDoorAction
      && (
        priorDoorAction.kind !== doorAction.kind
        || priorDoorAction.caller !== "b"
        || priorDoorAction.transitionSha256
          !== doorAction.transitionSha256
      )
    ) {
      throw new Error("prior Gate 5 action intent differs from plan");
    }
    const pendingJournal = await actionJournalRecord(
      "b",
      finalActionId,
      true,
    );
    if (
      pendingJournal
      && (
        ![0, 1, 2, 3, 4].includes(pendingJournal.stage)
        || (
          pendingJournal.stage >= 2
          && !priorDoorAction
        )
        || (
          isHex64(priorDoorAction?.transactionHash)
          && priorDoorAction.transactionHash
            !== pendingJournal.transactionHash
        )
      )
    ) {
      throw new Error("prior Gate 5 action journal differs from report");
    }
    report.actions = report.actions.filter(
      ({ actionId }) => actionId !== finalActionId,
    );
    const doorActionTimingRecord = {
      actionId: finalActionId,
      kind: doorAction.kind,
      caller: "b",
      callerAccountId: report.identities.b.accountId,
      transitionSha256: doorAction.transitionSha256,
      transactionHash:
        pendingJournal?.transactionHash
        ?? priorDoorAction?.transactionHash,
      status: pendingJournal?.durableStage ?? "running",
      journal: pendingJournal,
      timings: { ...(priorDoorAction?.timings ?? {}) },
      timingMeasurement: {
        ...(priorDoorAction?.timingMeasurement ?? {}),
      },
      timingBoundaries: {
        ...(priorDoorAction?.timingBoundaries ?? {}),
      },
    };
    Object.defineProperty(doorActionTimingRecord, "hasPriorTimingRecord", {
      value: priorDoorAction !== undefined,
      enumerable: false,
    });
    report.actions.push(doorActionTimingRecord);
    report.actions.sort(
      (left, right) => Number(left.actionId) - Number(right.actionId),
    );
    await checkpointReport();
    const gate5Final = await finalizeDoor(workers.get("b"));
    if (
      lezStageTimingFields.some(
        (field) =>
          !isCompleteTimingMeasurement(
            field,
            gate5Final.timingMeasurement?.[field],
            gate5Final.timings?.[field],
          ),
      )
      || gate5Final.pendingEvidence.length < 2
    ) {
      throw new Error("Gate 5 timing/pending evidence is incomplete");
    }
    const doorActionJournal = await actionJournalEvidence(
      "b",
      finalActionId,
    );
    report.actions = report.actions.filter(
      ({ actionId }) => actionId !== finalActionId,
    );
    report.actions.push({
      actionId: finalActionId,
      kind: doorAction.kind,
      caller: "b",
      callerAccountId: report.identities.b.accountId,
      transitionSha256: doorAction.transitionSha256,
      transactionHash: doorActionJournal.transactionHash,
      status: "finalized",
      durableStatus: "finalized",
      finalStatus: gate5Final.final.receipt,
      journal: doorActionJournal,
      timings: gate5Final.timings,
      timingMeasurement: gate5Final.timingMeasurement,
      timingBoundaries: gate5Final.timingBoundaries,
    });
    report.actions.sort(
      (left, right) => Number(left.actionId) - Number(right.actionId),
    );
    report.gate5.finality = {
      status: "passed",
      ...gate5Final,
      expectedSharedRevision: plan.doorState.openedRevision,
      expectedSharedStateRoot: plan.doorState.openedStateRootHex,
      journal: doorActionJournal,
    };
    report.uiEvidence.pending.push(...gate5Final.pendingEvidence);
    report.uiEvidence.finalized.push(gate5Final.final.receipt);
    await captureScreenshot(screenshotSpecs.gate5FinalB);
    finalizedPrefix = finalActionCheckpoint;
  } else {
    const doorActionJournal = await actionJournalEvidence(
      "b",
      finalActionId,
    );
    const resumedDoorAction = report.actions.find(
      ({ actionId }) => actionId === finalActionId,
    );
    if (
      resumedDoorAction?.kind !== doorAction.kind
      || resumedDoorAction?.caller !== "b"
      || resumedDoorAction?.transitionSha256
        !== doorAction.transitionSha256
      || resumedDoorAction?.transactionHash
        !== doorActionJournal.transactionHash
    ) {
      throw new Error("resumed Gate 5 finality evidence is not exact");
    }
    const unavailableTiming = recoverPersistedTimingEvidence(
      resumedDoorAction,
      "finalized",
    );
    if (unavailableTiming.length > 0) {
      await checkpointReport();
      throw new Error(
        `resumed Gate 5 finality lacks exact persisted timing: ${unavailableTiming.join(",")}`,
      );
    }
    if (
      lezStageTimingFields.some(
        (field) =>
          !isCompleteTimingMeasurement(
            field,
            resumedDoorAction.timingMeasurement?.[field],
            resumedDoorAction.timings?.[field],
          ),
      )
    ) {
      throw new Error(
        "resumed Gate 5 finality lacks persisted measured timings",
      );
    }
    await openExact(
      workers.get("b"),
      plan,
      finalActionCheckpoint,
    );
    if (report.gate5.finality?.status === "passed") {
      if (
        report.gate5.finality.expectedSharedRevision
          !== plan.doorState.openedRevision
        || report.gate5.finality.expectedSharedStateRoot
          !== plan.doorState.openedStateRootHex
      ) {
        throw new Error("prior Gate 5 finality vector differs");
      }
      report.gate5.finality = {
        ...report.gate5.finality,
        journal: doorActionJournal,
        resumed: true,
      };
    } else {
      const recoveredFinal = await recoverFinalizedDoor(
        workers.get("b"),
      );
      report.gate5.finality = {
        status: "passed",
        ...recoveredFinal,
        expectedSharedRevision: plan.doorState.openedRevision,
        expectedSharedStateRoot: plan.doorState.openedStateRootHex,
        journal: doorActionJournal,
        recoveredAfterReportCrash: true,
      };
      report.uiEvidence.finalized.push(
        recoveredFinal.final.receipt,
      );
    }
    if (
      report.failureEvidence.delayedLezUpdate?.status !== "passed"
      || report.failureEvidence.delayedLezUpdate.actionId
        !== finalActionId
      || report.failureEvidence.delayedLezUpdate.observationPaused !== true
      || !Number.isSafeInteger(
        report.failureEvidence.delayedLezUpdate.pauseMs,
      )
      || report.failureEvidence.delayedLezUpdate.pauseMs < 1_000
    ) {
      throw new Error("resumed delayed LEZ evidence is not exact");
    }
    await reusePriorScreenshot(screenshotSpecs.gate5PendingB);
    await captureScreenshot(screenshotSpecs.gate5FinalB);
  }
  const finalizedReceiptFields = statusFields(
    report.gate5.finality?.final?.receipt,
  );
  if (!isHex64(finalizedReceiptFields.receipt_sha256)) {
    throw new Error("Gate 5 final receipt omitted deterministic receipt hash");
  }
  const priorFinalizedVmMetric =
    previousReport?.gate5?.vmTurnMetrics?.finalized;
  let finalizedVmMetric;
  if (priorFinalizedVmMetric) {
    validateStoredVmTurnMetric(
      priorFinalizedVmMetric,
      "b",
      finalizedReceiptFields.receipt_sha256,
      "finalized",
    );
    finalizedVmMetric = priorFinalizedVmMetric;
    report.gate5.vmTurnMetricRecovery = {
      finalized: "persisted-exact-metric",
      liveProcessMetricRequired: false,
    };
  } else {
    finalizedVmMetric = await gate5VmTurnMetric(
      workers.get("b"),
      "finalized",
      finalizedReceiptFields.receipt_sha256,
    );
    report.gate5.vmTurnMetricRecovery = {
      finalized: "live-process-metric",
      liveProcessMetricRequired: true,
    };
  }
  report.gate5.vmTurnMetrics = {
    ...(report.gate5.vmTurnMetrics ?? {}),
    finalized: finalizedVmMetric,
  };
  await checkpointReport();

  phase = "checkpoint-door-all-clients";
  for (const label of initialLabels) {
    report.checkpoints[
      `action${finalActionId}${label.toUpperCase()}`
    ] = await openExact(
      workers.get(label),
      plan,
      finalActionCheckpoint,
    );
  }
  if (
    resumeWithoutCreator
    && !previousReport?.checkpoints?.[`action${finalActionId}A`]
  ) {
    throw new Error(
      "creator-offline resume lacks Alice door-action checkpoint",
    );
  }
  const authorityBundles = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => [
        label,
        await authorityBundleEvidence(label),
      ]),
    ),
  );
  if (
    new Set(
      Object.values(authorityBundles)
        .map(({ sha256: digest }) => digest),
    ).size !== 1
  ) {
    throw new Error(
      "door action finalized authority bundle digests differ",
    );
  }
  report.gate5.convergence = {
    status: "passed",
    checkpoint: finalActionCheckpoint,
    sharedRevision: plan.doorState.openedRevision,
    sharedStateRoot: plan.doorState.openedStateRootHex,
    authorityProjectionDigest: authorityBundles.a.sha256,
    authorityBundles,
  };
  await checkpointReport();

  phase = "creator-offline";
  let creatorPid;
  if (resumeWithoutCreator) {
    creatorPid = priorCreatorPid;
    if (await originalCreatorProcessExists(creatorIdentity)) {
      throw new Error(`creator Basecamp process ${creatorPid} is not offline`);
    }
    report.gate6.creator = {
      ...previousReport.gate6.creator,
      offline: true,
      offlineProvenOnResume: true,
    };
    report.gate6.lifecycle = {
      ...previousReport.gate6.lifecycle,
      phase: "creator-offline",
      creatorPid,
      resumedWithoutCreator: true,
    };
  } else {
    const creator = workers.get("a");
    creatorPid = creator.basecampPid;
    creatorIdentity = validateCreatorProcessIdentity(
      await captureOwnedProcessIdentity({ pid: creatorPid }),
      creatorPid,
    );
    report.gate6.creator = {
      label: "a",
      pid: creatorPid,
      processIdentity: creatorIdentity,
      memoryBeforeStop: await processMetrics(
        creatorPid,
        expectedTcpListeners("a", { delivery: true, storage: true }),
        "a",
      ),
      frameTimingBeforeStop: await captureFrameTiming(creator),
    };
    report.frameTiming.runs.aInitial =
      report.gate6.creator.frameTimingBeforeStop;
    report.gate6.lifecycle = {
      phase: "creator-stop-pending",
      creatorPid,
      actionCheckpoint: finalActionCheckpoint,
    };
    await checkpointReport();
    await creator.stop();
    workers.delete("a");
    await sleep(1_000);
    if (await originalCreatorProcessExists(creatorIdentity)) {
      throw new Error(`creator Basecamp process ${creatorPid} remained alive`);
    }
    report.gate6.creator.offline = true;
    report.gate6.lifecycle.phase = "creator-offline";
  }
  await checkpointReport();

  phase = "restart-bob-carol";
  for (const label of ["b", "c"]) {
    report.frameTiming.runs[`${label}Initial`] =
      await captureFrameTiming(workers.get(label));
  }
  const fullRebuildStarted = performance.now();
  const restartStarted = fullRebuildStarted;
  await Promise.all(
    ["b", "c"].map((label) => workers.get(label).stop()),
  );
  workers.delete("b");
  workers.delete("c");
  const retainedSourceDataRoot =
    await storageDataRootEvidence("c");
  const coldRebuildStarted = performance.now();
  report.failureEvidence.coldClientRebuild =
    await removeColdClientDerivedState("b");
  report.failureEvidence.coldClientRebuild.expectedAuthoritySha256 =
    authorityBundles.b.sha256;
  await checkpointReport();
  for (const label of ["b", "c"]) {
    const index = labels.indexOf(label);
    const worker = new WorkerClient(
      label,
      inspectorPorts[index],
      "gate6-restart",
    );
    workers.set(label, worker);
    const startup = await worker.init();
    if (label === "b") {
      const offlineProperties = await worker.call("properties");
      const deliveryState = statusFields(
        String(offlineProperties.gate2Status),
      ).state;
      const lezReady = statusFields(
        String(offlineProperties.gate4LezState),
      ).ready;
      if (
        deliveryState === "online"
        || lezReady === "1"
      ) {
        throw new Error(
          "cold client was not visibly offline before reconnect",
        );
      }
      report.failureEvidence.clientOffline = {
        status: "passed",
        label: "b",
        creatorOffline: true,
        deliveryState: deliveryState ?? "offline",
        lezReady: lezReady ?? "0",
        storageState: String(offlineProperties.gate3Status),
      };
      report.uiEvidence.offline.push({
        label: "b",
        deliveryState: deliveryState ?? "offline",
        lezReady: lezReady ?? "0",
      });
      await captureScreenshot(screenshotSpecs.gate6OfflineB);
      await checkpointReport();
    }
    const lez = await startLez(worker, []);
    if (lez.fields.wallet !== "opened") {
      throw new Error(
        `LEZ wallet ${label} was not preserved across restart`,
      );
    }
    const identityReceipt = await identityStatus(worker);
    const identity = parseIdentity(identityReceipt.receipt);
    const expected = report.identities[label];
    if (
      identity.accountId !== expected.accountId
      || identity.deliveryKey !== expected.deliveryKey
      || identity.display !== expected.display
    ) {
      throw new Error(`identity ${label} changed across restart`);
    }
    const checkpoint = await openExact(
      worker,
      plan,
      finalActionCheckpoint,
    );
    if (
      label === "b"
      && checkpoint.status?.fields?.authority
        !== `rebuilt-${finalActionId}`
    ) {
      throw new Error(
        `cold client did not rebuild LEZ history: ${checkpoint.status?.receipt}`,
      );
    }
    report.restart[label] = {
      startup,
      lez,
      identity: {
        accountId: identity.accountId,
        display: identity.display,
        deliveryKey: identity.deliveryKey,
        keyEpoch: identity.keyEpoch,
      },
      checkpoint,
      memory: await processMetrics(
        worker.basecampPid,
        expectedTcpListeners(label),
        label,
      ),
    };
    await checkpointReport();
  }
  report.timings.lezProjectionRebuildMs = Math.round(
    performance.now() - restartStarted,
  );

  phase = "gate6-lounge-recovery";
  report.gate6.rooms = {
    manualNavigationAfterRestart: false,
  };

  phase = "gate6-storage-reuse";
  report.storage.restart = {
    status: "running",
    retainedDataRoots: true,
    clients: {},
  };
  const sourceStartup = await startProductionStorage(
    workers.get("c"),
    storageConfigs.c,
  );
  const sourceRecovered = await fetchProductionCatalog(
    workers.get("c"),
    catalog,
  );
  if (
    sourceRecovered.mode !== "cache"
    || sourceRecovered.nativeSource !== "cache"
    || sourceRecovered.nativeAvailable !== catalog.ordered.length
    || sourceRecovered.nativeTotal !== catalog.ordered.length
  ) {
    throw new Error(
      `Gate 6 Storage c mode ${sourceRecovered.mode}, expected cache`,
    );
  }
  const sourceProcess = await processMetrics(
    workers.get("c").basecampPid,
    expectedTcpListeners("c", { storage: true }),
    "c",
  );
  if (
    await originalCreatorProcessExists(creatorIdentity)
    || report.failureEvidence.coldClientRebuild?.status !== "removed"
  ) {
    throw new Error(
      "cold Storage source topology is not isolated from creator/data root",
    );
  }
  report.storage.restart.clients.c = {
    startup: sourceStartup,
    recovered: sourceRecovered,
  };
  report.storage.restart.sourceBinding = {
    sourceLabel: "c",
    sourceAccountId: report.identities.c.accountId,
    exactCatalogChecksum: catalog.checksum,
    retainedDataRootBeforeRestart: retainedSourceDataRoot,
    retainedCatalogVerifiedBeforeColdFetch: true,
    sourceNativeAvailable: sourceRecovered.nativeAvailable,
    sourceNativeTotal: sourceRecovered.nativeTotal,
    creatorOffline: true,
    coldClientDataRootRemoved: true,
    coldClientStorageNotStarted: true,
    onlineRetainedHolderLabels: ["c"],
    storageProcess: sourceProcess,
    providerAttribution:
      "Storage API does not expose the serving peer; evidence binds the only retained local peer",
  };
  await checkpointReport();

  const coldStartup = await startProductionStorage(
    workers.get("b"),
    storageConfigs.b,
  );
  const coldRecovered = await fetchProductionCatalog(
    workers.get("b"),
    catalog,
  );
  if (
    coldRecovered.mode !== "network"
    || coldRecovered.nativeSource !== "network"
    || coldRecovered.nativeAvailable !== 0
    || coldRecovered.nativeTotal !== catalog.ordered.length
  ) {
    throw new Error(
      `Gate 6 Storage b mode ${coldRecovered.mode}, expected network`,
    );
  }
  report.storage.restart.clients.b = {
    startup: coldStartup,
    recovered: coldRecovered,
  };
  const sourceRetainedAfterTransfer = await fetchProductionCatalog(
    workers.get("c"),
    catalog,
  );
  if (
    sourceRetainedAfterTransfer.mode !== "cache"
    || sourceRetainedAfterTransfer.nativeSource !== "cache"
    || await originalCreatorProcessExists(creatorIdentity)
  ) {
    throw new Error(
      "Gate 6 retained source or creator-offline contract changed",
    );
  }
  report.storage.restart.sourceBinding = {
    ...report.storage.restart.sourceBinding,
    coldClientNativeAvailable: coldRecovered.nativeAvailable,
    coldClientNativeTotal: coldRecovered.nativeTotal,
    retainedCatalogVerifiedAfterColdFetch: true,
    sourceRetainedAfterTransfer,
  };
  report.storage.restart.status = "passed";
  const rebuiltAuthority = await authorityBundleEvidence("b");
  const rebuiltAuthorityPath = await uniqueStateFile(
    "b",
    "lez-authority-bundle-v1",
  );
  const rebuiltIdentityPath = await uniqueStateFile(
    "b",
    "delivery-identity-v1",
  );
  const rebuiltIdentity = await fingerprintBoundedRegularFile(
    rebuiltIdentityPath,
    1024 * 1024,
  );
  const rebuiltWallet = await walletStateEvidence(
    await walletStateFiles("b", dirname(rebuiltAuthorityPath)),
  );
  const coldRebuild = report.failureEvidence.coldClientRebuild;
  if (
    rebuiltAuthority.sha256 !== coldRebuild.expectedAuthoritySha256
    || !exactStableJson(rebuiltIdentity, coldRebuild.before.identity)
    || rebuiltWallet.config.sha256
      !== coldRebuild.before.lezWallet.config.sha256
    || rebuiltWallet.storage.byteLength <= 0
  ) {
    throw new Error(
      "cold client authority/identity/wallet differs after reconstruction",
    );
  }
  const rebuiltProperties = await workers.get("b").call("properties");
  const rebuiltVm = statusFields(
    String(rebuiltProperties.gate5Status),
  );
  if (
    rebuiltVm.vm !== "promoted"
    || rebuiltVm.action !== finalActionId
    || rebuiltVm.navigation !== "1"
    || rebuiltVm.state_root !== plan.doorState.openedStateRootHex
  ) {
    throw new Error(
      `cold client VM projection did not replay: ${rebuiltProperties.gate5Status}`,
    );
  }
  report.failureEvidence.coldClientRebuild = {
    ...coldRebuild,
    status: "passed",
    authorityAfter: rebuiltAuthority,
    identityAfterSha256: rebuiltIdentity.sha256,
    lezWalletAfter: rebuiltWallet,
    openedExistingLezWallet: true,
    storageRecoveryMode:
      report.storage.restart.clients.b.recovered.mode,
    vmProjection: {
      phase: rebuiltVm.vm,
      actionId: rebuiltVm.action,
      navigation: rebuiltVm.navigation,
      stateRoot: rebuiltVm.state_root,
    },
    exactFinalizedProjection: true,
  };
  report.timings.coldHistoryStorageVmRebuildMs = Math.round(
    performance.now() - coldRebuildStarted,
  );
  for (const label of ["b", "c"]) {
    const properties = await workers.get(label).call("properties");
    const expectedHandle =
      catalog.byId["background-lounge"].contentSha256;
    if (
      String(properties.gate5RoomTitle) !== "Lounge"
      || String(properties.gate3RoomHandle) !== expectedHandle
    ) {
      throw new Error(
        `${label} did not recover exact Lounge title/background`,
      );
    }
    report.gate6.rooms[label] = {
      title: String(properties.gate5RoomTitle),
      backgroundHandle: String(properties.gate3RoomHandle),
    };
  }
  await checkpointReport();

  phase = "gate6-delivery-reconnect";
  report.delivery.restartMesh = await startDeliveryMesh(
    ["b", "c"],
    deliveryPorts,
    "b",
  );
  const bobSpeech = await invoke(
    workers.get("b"),
    "gate2Say",
    ["Bob recovered without creator"],
    { prefix: "ok;request=" },
    60_000,
  );
  const bobAtCarol = await waitProjection(
    workers.get("c"),
    {
      [report.identities.b.accountId]: {
        speech: "Bob recovered without creator",
      },
    },
    "Bob speech after B/C-only restart",
  );
  const carolBanned = await invoke(
    workers.get("c"),
    "gate2Say",
    ["Carol remains banned"],
    {
      exact: "rejected=delivery-publish;preflight=sender-banned",
    },
    60_000,
  );
  const propBanned = propStoryRequested
    ? await invoke(
        workers.get("b"),
        "gate2Wear",
        [catalog.propId],
        {
          exact:
            "rejected=delivery-publish;preflight=invalid-or-banned-payload",
        },
        60_000,
      )
    : null;
  if (await originalCreatorProcessExists(creatorIdentity)) {
    throw new Error("creator restarted during Gate 6");
  }
  report.delivery.restartBehavior = {
    status: "passed",
    bobSpeech,
    bobAtCarol,
    carolBanned,
    propStory: propStoryRequested
      ? "requested"
      : "not-requested",
    ...(propStoryRequested ? { propBanned } : {}),
    creatorPidOffline: true,
  };
  await captureScreenshot(screenshotSpecs.gate6RestartB);
  await captureScreenshot(screenshotSpecs.gate6RestartC);
  for (const label of ["b", "c"]) {
    report.frameTiming.runs[`${label}Restart`] =
      await captureFrameTiming(workers.get(label));
  }
  report.timings.deliveryReconnectMs =
    report.delivery.restartMesh.elapsedMs;
  report.timings.fullRebuildMs = Math.round(
    performance.now() - fullRebuildStarted,
  );
  report.gate6.status = "passed";
  report.gate6.creator.offlineAtCompletion = true;
  report.gate6.assets = {
    status: "passed",
    exactCatalogChecksum: catalog.checksum,
    holders: ["b", "c"],
  };
  report.gate6.memory = {
    metricScope:
      "Basecamp main/process tree plus uniquely mapped palace_vm module host",
    b: await processMetrics(
      workers.get("b").basecampPid,
      expectedTcpListeners("b", { delivery: true, storage: true }),
      "b",
    ),
    c: await processMetrics(
      workers.get("c").basecampPid,
      expectedTcpListeners("c", { delivery: true, storage: true }),
      "c",
    ),
  };
  report.processModel = {
    standalonePalaceServer: processInventoryObservations.some(
      (entry) => entry.standalonePalaceServerMatches.length > 0,
    ),
    loaderSelection: processLoaderSelection,
    runtimeArtifacts: processRuntimeArtifacts,
    observationMethod:
      "bounded /proc exact process inventory, pinned runtime artifacts, and owned TCP LISTEN proof",
    observations: processInventoryObservations,
  };
  if (report.processModel.standalonePalaceServer) {
    throw new Error("standalone Palace server process evidence is not empty");
  }
  const expectedScreenshots = Object.values(screenshotSpecs);
  if (
    report.screenshots.length !== expectedScreenshots.length
    || new Set(report.screenshots.map(({ file }) => file)).size
      !== expectedScreenshots.length
  ) {
    throw new Error("Gate 4-6 screenshot evidence set is not exact");
  }
  for (const spec of expectedScreenshots) {
    const entry = report.screenshots.find(
      ({ file }) => file === spec.file,
    );
    const evidence = await screenshotFileEvidence(spec.file);
    if (
      entry?.stage !== spec.stage
      || entry.state !== spec.state
      || entry.label !== spec.label
      || entry.sha256 !== evidence.sha256
      || entry.byteLength !== evidence.byteLength
      || entry.width !== evidence.width
      || entry.height !== evidence.height
    ) {
      throw new Error(`${spec.file} final screenshot evidence mismatch`);
    }
  }
  report.actionJournals = Object.fromEntries(
    await Promise.all(
      labels.map(async (label) => [
        label,
        await finalActionJournalEvidence(label),
      ]),
    ),
  );
  validateDirectEntryNodeTopology(report.delivery.initialMesh);
  const storageNetworkIds = labels.map((label) =>
    parseJsonObject(
      storageConfigs[label],
      `release contract Storage config ${label}`,
    ).network);
  const entryVmProfile =
    plan.actions[0].transition.entry_room.vm_profile;
  const secondaryVmProfile =
    plan.actions[0].transition.secondary_room.vm_profile;
  const storageCatalogProtocol =
    catalog.canonical.slice(0, catalog.canonical.indexOf("\n"));
  const inexactFinalActionTiming = report.actions.flatMap((action) =>
    recoverPersistedTimingEvidence(action, "finalized")
      .map((field) => `${action.actionId}:${field}`));
  if (
    new Set(storageNetworkIds).size !== 1
    || storageNetworkIds[0] !== "logos.test"
    || entryVmProfile !== "iptscrae_mvp_v1"
    || secondaryVmProfile !== entryVmProfile
    || storageCatalogProtocol
      !== "logos-palace-mvp-storage-catalog-v1"
    || catalog.byId["palace-1"]?.type !== "palace_manifest"
    || report.release.programDeployment?.explorerOrigin
      !== "https://explorer.testnet.lez.logos.co"
  ) {
    throw new Error("runtime release contract evidence differs");
  }
  if (
    report.actions.length !== plan.actions.length
    || inexactFinalActionTiming.length > 0
    || report.actions.some(
      (action, index) =>
        action.actionId !== String(index)
        || lezStageTimingFields.some(
          (field) =>
            !isCompleteTimingMeasurement(
              field,
              action.timingMeasurement?.[field],
              action.timings?.[field],
            ),
        ),
    )
    || report.uiEvidence.pending.length < 2
    || report.uiEvidence.finalized.length < 1
    || report.uiEvidence.degraded.length < 1
    || report.uiEvidence.offline.length < 1
    || report.failureEvidence.missingStorageObject?.status !== "passed"
    || report.failureEvidence.delayedLezUpdate?.status !== "passed"
    || report.failureEvidence.clientOffline?.status !== "passed"
    || report.failureEvidence.coldClientRebuild?.status !== "passed"
    || report.failureEvidence.coldClientRebuild
        .exactFinalizedProjection !== true
    || report.failureEvidence.coldClientRebuild
        .preservationProof?.deliveryIdentityUnchangedByDeletion !== true
    || report.failureEvidence.coldClientRebuild
        .preservationProof?.lezWalletUnchangedByDeletion !== true
    || report.failureEvidence.coldClientRebuild
        .openedExistingLezWallet !== true
    || report.restart.b?.lez?.fields?.wallet !== "opened"
    || report.storage.restart.clients.b.recovered.mode !== "network"
    || report.storage.restart.clients.b.recovered.nativeSource
      !== "network"
    || report.storage.restart.clients.c.recovered.mode !== "cache"
    || report.storage.restart.clients.c.recovered.nativeSource
      !== "cache"
    || report.storage.restart.sourceBinding?.sourceLabel !== "c"
    || report.storage.restart.sourceBinding?.sourceAccountId
      !== report.identities.c.accountId
    || report.storage.restart.sourceBinding?.exactCatalogChecksum
      !== catalog.checksum
    || report.storage.restart.sourceBinding
        ?.retainedCatalogVerifiedBeforeColdFetch !== true
    || report.storage.restart.sourceBinding
        ?.retainedCatalogVerifiedAfterColdFetch !== true
    || report.storage.restart.sourceBinding
        ?.sourceNativeAvailable !== catalog.ordered.length
    || report.storage.restart.sourceBinding
        ?.sourceNativeTotal !== catalog.ordered.length
    || report.storage.restart.sourceBinding
        ?.coldClientNativeAvailable !== 0
    || report.storage.restart.sourceBinding
        ?.coldClientNativeTotal !== catalog.ordered.length
    || report.storage.restart.sourceBinding?.creatorOffline !== true
    || report.storage.restart.sourceBinding
        ?.coldClientDataRootRemoved !== true
    || report.storage.restart.sourceBinding
        ?.coldClientStorageNotStarted !== true
    || report.storage.restart.sourceBinding
        ?.onlineRetainedHolderLabels?.join(",") !== "c"
  ) {
    throw new Error("failure/recovery acceptance evidence is incomplete");
  }
  report.releaseContract = {
    protocols: {
      palaceSchema: "palace-schema-v3",
      deliveryEnvelope: "PalaceDeliveryEnvelopeV1",
      storageCatalog: storageCatalogProtocol,
      catalogManifest: "logos-palace-catalog-manifest-v1",
      roomMetadata: "logos-palace-room-v1",
      propMetadata: "logos-palace-prop-v1",
      palaceManifestKind: catalog.byId["palace-1"].type,
      vmProfile: entryVmProfile,
    },
    network: {
      productionDeliveryEnvelopeNetworkId:
        "logos-lez-testnet-v0.2.0",
      deliveryTransport: "direct-entry-node-test-topology",
      sharedFleetUsed: false,
      storageNetworkId: storageNetworkIds[0],
      lezNetworkId: "logos-lez-testnet-v0.2.0",
      lezModuleApiVersion: "0.4.0-alpha.2",
      lezModuleRevision,
      lezRuntimeRevision:
        "e923315c020d4966807849f9db10536b628d5739",
      lezSchemaId: "palace-schema-v3",
      lezPublicContractRevision:
        "2b67563baf590c32dd82e50e3252815ec56bdaec",
      lezProgramIdHex: palaceRelease.programIdHex,
      lezProgramBytecodeSha256:
        palaceRelease.programBytecodeSha256,
      lezSequencerOrigin: "https://testnet.lez.logos.co",
      lezReadOrigin: report.release.programDeployment.explorerOrigin,
    },
  };
  report.metrics = {
    lezMeasurementBoundaries,
    lezActions: report.actions.map((action) => ({
      actionId: action.actionId,
      kind: action.kind,
      transitionSha256: action.transitionSha256,
      transactionHash: action.transactionHash,
      timings: action.timings,
      timingMeasurement: action.timingMeasurement,
    })),
    storage: {
      gate3FirstNetwork: {
        providerB: {
          mode: gate3.providerBFetch?.mode,
          endToEndMs: gate3.providerBFetch?.endToEndMs,
        },
        coldC: {
          mode: gate3.coldCFetch?.mode,
          endToEndMs: gate3.coldCFetch?.endToEndMs,
        },
      },
      gate3Cached: {
        providerB: {
          mode: gate3.providerBCachedFetch?.mode,
          endToEndMs: gate3.providerBCachedFetch?.endToEndMs,
        },
        coldC: {
          mode: gate3.coldCCachedFetch?.mode,
          endToEndMs: gate3.coldCCachedFetch?.endToEndMs,
        },
      },
      gate4InitialCacheValidation: Object.fromEntries(
        labels.map((label) => [
          label,
          {
            mode: report.storage.initial?.catalog?.[label]?.mode,
            endToEndMs:
              report.storage.initial?.catalog?.[label]?.endToEndMs,
            retentionMs:
              report.storage.initial?.catalog?.[label]?.retentionMs,
          },
        ]),
      ),
      restart: Object.fromEntries(
        ["b", "c"].map((label) => [
          label,
          {
            mode:
              report.storage.restart?.clients?.[label]?.recovered?.mode,
            endToEndMs:
              report.storage.restart?.clients?.[label]
                ?.recovered?.endToEndMs,
            retentionMs:
              report.storage.restart?.clients?.[label]
                ?.recovered?.retentionMs,
          },
        ]),
      ),
    },
    gate5Vm: {
      previewPayload: {
        spot: "door",
        expectedActionId: finalActionId,
        transitionSha256:
          plan.actions[finalActionCheckpoint].transitionSha256,
      },
      previewRoundTripMs: {
        a: report.gate5.preview?.a?.elapsedMs,
        b: report.gate5.preview?.b?.elapsedMs,
      },
      executeTurn: report.gate5.vmTurnMetrics,
      useElapsedMs: report.gate5.finality?.use?.elapsedMs,
      endToEndMs: report.gate5.finality?.totalMs,
    },
    applicationRoundTrip: report.applicationRoundTrip,
    recovery: {
      lezProjectionRebuildMs: report.timings.lezProjectionRebuildMs,
      deliveryReconnectMs: report.timings.deliveryReconnectMs,
      fullRebuildMs: report.timings.fullRebuildMs,
      coldHistoryStorageVmRebuildMs:
        report.timings.coldHistoryStorageVmRebuildMs,
      fullRebuildStartBoundary:
        "immediately before stopping Bob and Carol Basecamp processes",
      fullRebuildEndBoundary:
        "peer Storage recovery, finalized history/VM replay, Delivery topology/behavior, exact Lounge projection, and restart PNG captures complete",
    },
    processMemory: report.gate6.memory,
    frameTiming: report.frameTiming,
  };

} catch (error) {
  failure = error instanceof Error ? error : new Error(String(error));
  report.status = "failed";
  report.fullGate4 = "failed";
  report.fullGate5 =
    report.gate5?.convergence?.status === "passed"
      ? "passed"
      : "failed";
  report.fullGate6 = "failed";
  report.noPalaceServer =
    report.processModel.standalonePalaceServer === false
      && report.processModel.observations.length > 0
      ? "passed"
      : "failed";
  report.failures.push({ phase, message: failure.message });
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
    report.status = "failed";
    report.fullGate4 = "failed";
    report.fullGate5 = "failed";
    report.fullGate6 = "failed";
    report.noPalaceServer = "failed";
    report.failures.push({
      phase: "terminal-cleanup",
      message: cleanupFailures.join("; "),
    });
    if (!failure) {
      failure = new Error(
        `Gate 4 terminal cleanup failed: ${cleanupFailures.join("; ")}`,
      );
    }
  } else if (!failure) {
    report.status = "passed";
    report.fullGate4 = "passed";
    report.fullGate5 = "passed";
    report.fullGate6 = "passed";
    report.noPalaceServer = "passed";
  }
  report.timings.totalMs = Math.round(performance.now() - runStartedAt);
  await checkpointReport();
  await reportWrite;
}

if (failure) throw failure;
