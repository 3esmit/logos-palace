#!/usr/bin/env node

import { createHash } from "node:crypto";
import { execFile } from "node:child_process";
import { constants } from "node:fs";
import {
  lstat,
  open,
  realpath,
  rename,
  unlink,
} from "node:fs/promises";
import { promisify } from "node:util";
import {
  basename,
  dirname,
  join,
  resolve,
} from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { palaceRelease } from "./basecamp_release_preflight.mjs";
import {
  validateProcessExecutableIdentity,
} from "./basecamp_process_model.mjs";
import {
  lezMeasurementBoundaries,
  recoverPersistedTimingEvidence,
} from "./basecamp_lez_timing.mjs";

const execFileAsync = promisify(execFile);
const scriptPath = fileURLToPath(import.meta.url);
const testsDir = dirname(scriptPath);
const screenshotValidator = join(
  testsDir,
  "validate_gate4_artifacts.mjs",
);
const missingStorageSourceCid =
  "zb2rhWeJ5rsKhbfi31kikfdS3hE2pvWzhW27Es4Jmc863vFdx";
const coldReplayContractTests = Object.freeze([
  "core_vm_finalized_replay_builds_one_exact_idempotent_plan",
  "core_vm_finalized_replay_fails_closed_without_exact_evidence",
]);
const acceptedSubmissionCrashRecoveryContractTests = Object.freeze([
  "terminal_recovery_states_require_a_nonfinal_durable_action",
  "core_lez_repairs_durable_coordinator_after_accept_to_journal_crash",
]);
const palaceVmFinalityContractTests = Object.freeze([
  "shared_door_navigation_emits_only_after_finalized_replay",
  "tracked_finality_rejects_wrong_action_receipt_script_and_state",
  "exact_finality_promotion_is_one_shot_and_restart_safe",
  "untracked_finalized_compatibility_api_cannot_navigate",
]);
const coldRebuildRemovedState = Object.freeze([
  "finalized-authority",
  "local-projection",
  "finalized-vm-turn",
  "vm-finality-journal",
  "storage-data-root",
  "verified-asset-cache",
]);
const coldRebuildPreservedState = Object.freeze([
  "delivery-identity",
  "lez-wallet",
]);
const coldRebuildPublicPreservedState = Object.freeze([
  "delivery-binding",
  "lez-wallet",
]);

const reportSpecs = Object.freeze([
  {
    name: "gate0",
    file: "gate0/gate0-report.json",
    schema: "logos.palace.basecamp-gate0-report",
    version: 1,
  },
  {
    name: "gate1",
    file: "gate1/gate1-report.json",
    schema: "logos-palace-basecamp-gate1-report-v1",
    version: 1,
  },
  {
    name: "gate2",
    file: "gate2/gate2-report.json",
    schema: "logos-palace-basecamp-gate2-report-v1",
    version: 1,
  },
  {
    name: "gate3",
    file: "gate3/gate3-report.json",
    schema: "logos.palace.basecamp-gate3-report",
    version: 1,
  },
  {
    name: "gate4To6",
    file: "gate4/gate4-report.json",
    schema: "logos.palace.basecamp-gate4-6-report",
    version: 2,
  },
]);

const screenshotSpecs = Object.freeze([
  {
    file: "gate4-a-three-user-atrium-converged.png",
    stage: "gate4-delivery-convergence",
    state: "three-user-atrium-with-approved-prop",
    label: "a",
  },
  {
    file: "gate4-b-storage-object-degraded.png",
    stage: "gate4-storage-failure",
    state: "missing-storage-object-degraded",
    label: "b",
  },
  {
    file: "gate4-b-atrium-after-moderation.png",
    stage: "gate4-moderation",
    state: "atrium-after-human-user-and-prop-bans",
    label: "b",
  },
  {
    file: "gate5-a-door-preview.png",
    stage: "gate5-preview",
    state: "door-preview-before-finality",
    label: "a",
  },
  {
    file: "gate5-b-door-preview.png",
    stage: "gate5-preview",
    state: "door-preview-before-finality",
    label: "b",
  },
  {
    file: "gate5-b-door-pending.png",
    stage: "gate5-pending",
    state: "door-awaiting-lez-observation",
    label: "b",
  },
  {
    file: "gate5-b-lounge-finalized.png",
    stage: "gate5-finality",
    state: "lounge-after-door-finality",
    label: "b",
  },
  {
    file: "gate6-b-offline-before-reconnect.png",
    stage: "gate6-offline",
    state: "creator-offline-client-before-reconnect",
    label: "b",
  },
  {
    file: "gate6-b-lounge-restarted.png",
    stage: "gate6-restart",
    state: "lounge-after-bob-carol-restart",
    label: "b",
  },
  {
    file: "gate6-c-lounge-restarted.png",
    stage: "gate6-restart",
    state: "lounge-after-bob-carol-restart",
    label: "c",
  },
]);

const dependencySpecs = Object.freeze([
  ["basecamp", "basecamp"],
  ["delivery_module", "deliveryModule"],
  ["storage_module", "storageModule"],
  ["lez_core", "lezCore"],
]);

const qsgSummaryFields = Object.freeze([
  "totalMs",
  "polishMs",
  "syncMs",
  "renderMs",
  "swapMs",
  "frameDeltaMs",
]);

const qsgRunNames = Object.freeze([
  "aInitial",
  "bInitial",
  "cInitial",
  "bRestart",
  "cRestart",
]);

const runtimeOutputNames = Object.freeze([
  "acceptance-tools",
  "basecamp",
  "delivery-module-lgx",
  "lez-core-lgx",
  "palace-core-acceptance-lgx",
  "palace-core-contracts",
  "palace-core-lgx",
  "palace-ui-lgx",
  "palace-vm-contracts",
  "palace-vm-lgx",
  "qt-mcp",
  "release-verifier",
  "storage-module-lgx",
]);

const processInventoryContract = Object.freeze({
  basecampMain: "LogosBasecamp",
  coreModuleHosts: Object.freeze([
    "capability_module",
    "delivery_module",
    "lez_core",
    "package_downloader",
    "package_manager",
    "palace_core",
    "palace_vm",
    "storage_module",
  ]),
  uiModuleHosts: Object.freeze(["logos_palace_ui"]),
});

const processProgramArtifactSpecs = Object.freeze([
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
  },
  {
    moduleName: "lez_core",
    packageFile: "logos-lez_core-module-lib.lgx",
    mainFile: "lez_core_plugin.so",
  },
  {
    moduleName: "logos_palace_ui",
    packageFile: "logos-logos_palace_ui-module.lgx",
    mainFile: "logos_palace_ui_plugin.so",
  },
  {
    moduleName: "palace_core",
    packageFile: "logos-palace_core-module-lib.lgx",
    mainFile: "palace_core_plugin.so",
  },
  {
    moduleName: "palace_vm",
    packageFile: "logos-palace_vm-module-lib.lgx",
    mainFile: "palace_vm_plugin.so",
  },
  {
    moduleName: "storage_module",
    packageFile: "logos-storage_module-module-lib.lgx",
    mainFile: "storage_module_plugin.so",
  },
]);
const processModuleArtifactBasenames = Object.freeze(
  Object.fromEntries([
    ...bundledProcessModuleSpecs.map(({ moduleName, relativePath }) => [
      moduleName,
      basename(relativePath),
    ]),
    ...installedProcessModuleSpecs.map(({ moduleName, mainFile }) => [
      moduleName,
      mainFile,
    ]),
  ]),
);

const lgxPackageNames = Object.freeze([
  "logos-delivery_module-module-lib.lgx",
  "logos-lez_core-module-lib.lgx",
  "logos-logos_palace_ui-module.lgx",
  "logos-palace_core-module-lib.lgx",
  "logos-palace_vm-module-lib.lgx",
  "logos-storage_module-module-lib.lgx",
]);

const protocolContract = Object.freeze({
  palaceSchema: "palace-schema-v3",
  deliveryEnvelope: "PalaceDeliveryEnvelopeV1",
  storageCatalog: "logos-palace-mvp-storage-catalog-v1",
  catalogManifest: "logos-palace-catalog-manifest-v1",
  roomMetadata: "logos-palace-room-v1",
  propMetadata: "logos-palace-prop-v1",
  palaceManifestKind: "palace_manifest",
  vmProfile: "iptscrae_mvp_v1",
});

const networkContract = Object.freeze({
  productionDeliveryEnvelopeNetworkId: "logos-lez-testnet-v0.2.0",
  deliveryTransport: "direct-entry-node-test-topology",
  sharedFleetUsed: false,
  storageNetworkId: "logos.test",
  lezNetworkId: "logos-lez-testnet-v0.2.0",
  lezModuleApiVersion: "0.4.0-alpha.2",
  lezModuleRevision: "e8d84103660604b1a6a06ddd66d20da7a2fdeb3f",
  lezRuntimeRevision: "e923315c020d4966807849f9db10536b628d5739",
  lezSchemaId: "palace-schema-v3",
  lezPublicContractRevision:
    "2b67563baf590c32dd82e50e3252815ec56bdaec",
  lezProgramIdHex:
    "e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61",
  lezProgramBytecodeSha256:
    "69099492e8f860ab338846f33762f5fb563ffc40121779ec1e15ef0b35da7171",
  lezSequencerOrigin: "https://testnet.lez.logos.co",
  lezReadOrigin: "https://explorer.testnet.lez.logos.co",
});

function isObject(value) {
  return (
    value !== null
    && typeof value === "object"
    && !Array.isArray(value)
  );
}

function exactKeys(value, expected) {
  return (
    isObject(value)
    && JSON.stringify(Object.keys(value).sort())
      === JSON.stringify([...expected].sort())
  );
}

function isHex(value, length) {
  return (
    typeof value === "string"
    && new RegExp(`^[0-9a-f]{${length}}$`).test(value)
  );
}

function isSha256(value) {
  return isHex(value, 64);
}

function isNarHash(value) {
  return (
    typeof value === "string"
    && /^sha256-[A-Za-z0-9+/]{43}=$/.test(value)
  );
}

function isBase58(value) {
  return (
    typeof value === "string"
    && value.length >= 32
    && value.length <= 64
    && /^[1-9A-HJ-NP-Za-km-z]+$/.test(value)
  );
}

function nonnegativeNumber(value, description) {
  if (
    typeof value !== "number"
    || !Number.isFinite(value)
    || value < 0
  ) {
    throw new Error(`${description} is not a finite nonnegative number`);
  }
  return value;
}

function positiveInteger(value, description) {
  if (!Number.isSafeInteger(value) || value <= 0) {
    throw new Error(`${description} is not a positive safe integer`);
  }
  return value;
}

function nonnegativeInteger(value, description) {
  if (!Number.isSafeInteger(value) || value < 0) {
    throw new Error(`${description} is not a nonnegative safe integer`);
  }
  return value;
}

function stable(value) {
  if (Array.isArray(value)) return value.map(stable);
  if (isObject(value)) {
    return Object.fromEntries(
      Object.keys(value)
        .sort()
        .map((key) => [key, stable(value[key])]),
    );
  }
  return value;
}

function exactJson(left, right) {
  return JSON.stringify(stable(left)) === JSON.stringify(stable(right));
}

function sha256(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

async function canonicalDirectory(path, description) {
  const absolute = resolve(path);
  const metadata = await lstat(absolute);
  if (
    metadata.isSymbolicLink()
    || !metadata.isDirectory()
    || await realpath(absolute) !== absolute
  ) {
    throw new Error(`${description} is not a canonical directory`);
  }
  return absolute;
}

async function boundedFile(path, maximumBytes, description) {
  const absolute = resolve(path);
  if (await realpath(absolute) !== absolute) {
    throw new Error(`${description} is not canonical`);
  }
  const pathMetadata = await lstat(absolute);
  if (
    pathMetadata.isSymbolicLink()
    || !pathMetadata.isFile()
    || pathMetadata.size <= 0
    || pathMetadata.size > maximumBytes
  ) {
    throw new Error(`${description} is not a bounded regular file`);
  }
  const handle = await open(
    absolute,
    constants.O_RDONLY | constants.O_NOFOLLOW,
  );
  try {
    const before = await handle.stat({ bigint: true });
    const bytes = await handle.readFile();
    const after = await handle.stat({ bigint: true });
    if (
      !before.isFile()
      || before.dev !== after.dev
      || before.ino !== after.ino
      || before.size !== after.size
      || before.mtimeNs !== after.mtimeNs
      || bytes.length !== Number(after.size)
      || bytes.length <= 0
      || bytes.length > maximumBytes
    ) {
      throw new Error(`${description} changed while reading`);
    }
    return bytes;
  } finally {
    await handle.close();
  }
}

async function readReport(path, description) {
  const bytes = await boundedFile(path, 64 * 1024 * 1024, description);
  let value;
  try {
    value = JSON.parse(bytes.toString("utf8"));
  } catch {
    throw new Error(`${description} is not valid JSON`);
  }
  if (!isObject(value)) {
    throw new Error(`${description} is not a JSON object`);
  }
  return { value, sha256: sha256(bytes) };
}

function validateReportStatuses(reports) {
  const gate0 = reports.gate0.value;
  const gate1 = reports.gate1.value;
  const gate2 = reports.gate2.value;
  const gate3 = reports.gate3.value;
  const gate4 = reports.gate4To6.value;
  if (
    gate0.schema !== reportSpecs[0].schema
    || gate0.version !== 1
    || gate0.status !== "passed"
    || gate0.check !== "sandbox-test"
    || gate1.schema !== reportSpecs[1].schema
    || gate1.result !== "PASS"
    || !exactJson(gate1.cleanup, {
      status: "passed",
      failures: [],
    })
    || gate2.schema !== reportSpecs[2].schema
    || gate2.result !== "PASS"
    || !exactJson(gate2.cleanup, {
      status: "passed",
      failures: [],
    })
    || gate3.schema !== reportSpecs[3].schema
    || gate3.version !== 1
    || gate3.status !== "passed"
    || gate3.fullGate3 !== "passed"
    || !exactJson(gate3.cleanup, {
      status: "passed",
      failures: [],
    })
    || gate3.productionIdentityMode !== true
    || gate4.schema !== reportSpecs[4].schema
    || gate4.version !== 2
    || gate4.status !== "passed"
    || gate4.fullGate4 !== "passed"
    || gate4.fullGate5 !== "passed"
    || gate4.fullGate6 !== "passed"
    || gate4.noPalaceServer !== "passed"
    || !exactJson(gate4.cleanup, {
      status: "passed",
      failures: [],
    })
  ) {
    throw new Error("raw gate schema or pass status is invalid");
  }
}

function validateCompiledBindings(compiled, reports) {
  if (
    compiled.schema !== "logos.palace.basecamp-mvp-compiled-report"
    || compiled.version !== 1
    || compiled.status !== "passed"
    || compiled.fullMvp !== "passed"
    || !isHex(compiled.sourceCommit, 40)
    || !isNarHash(compiled.productSnapshotNarHash)
    || !isSha256(compiled.snapshotRunnerSha256)
    || !Number.isSafeInteger(compiled.productSnapshotNarSize)
    || compiled.productSnapshotNarSize <= 0
    || compiled.productSnapshotNarSize > 64 * 1024 * 1024
    || typeof compiled.productSnapshot !== "string"
    || !/^\/nix\/store\/[0-9abcdfghijklmnpqrsvwxyz]{32}-[^/]+$/.test(
      compiled.productSnapshot,
    )
    || !isObject(compiled.gates)
  ) {
    throw new Error("compiled report envelope is invalid");
  }
  const expectedGateBindings = {
    gate0: ["gate0", "gate0/gate0-report.json"],
    gate1: ["gate1", "gate1/gate1-report.json"],
    gate2: ["gate2", "gate2/gate2-report.json"],
    gate3: ["gate3", "gate3/gate3-report.json"],
    gate4: ["gate4To6", "gate4/gate4-report.json"],
    gate5: ["gate4To6", "gate4/gate4-report.json"],
    gate6: ["gate4To6", "gate4/gate4-report.json"],
  };
  if (
    JSON.stringify(Object.keys(compiled.gates).sort())
      !== JSON.stringify(Object.keys(expectedGateBindings).sort())
  ) {
    throw new Error("compiled gate set is not exact");
  }
  for (const [gate, [reportName, reportFile]] of Object.entries(
    expectedGateBindings,
  )) {
    const binding = compiled.gates[gate];
    if (
      !isObject(binding)
      || binding.status !== "passed"
      || binding.report !== reportFile
      || binding.reportSha256 !== reports[reportName].sha256
    ) {
      throw new Error(`${gate} compiled report binding is invalid`);
    }
  }
  for (const { value } of Object.values(reports)) {
    if (
      value.productSnapshot !== compiled.productSnapshot
      || value.sourceCommit !== compiled.sourceCommit
      || value.productSnapshotNarHash !== compiled.productSnapshotNarHash
      || value.productSnapshotNarSize !== compiled.productSnapshotNarSize
      || value.snapshotRunnerSha256 !== compiled.snapshotRunnerSha256
      || value.runtimeOutputManifestSha256
        !== compiled.runtimeOutputs?.manifestSha256
    ) {
      throw new Error("raw report source identity differs");
    }
  }
}

function publicDependencies(compiled, gate4) {
  const source = compiled.dependencyRevisions;
  if (
    !isObject(source)
    || JSON.stringify(Object.keys(source).sort())
      !== JSON.stringify(dependencySpecs.map(([name]) => name).sort())
    || !exactJson(source, gate4.dependencyRevisions)
  ) {
    throw new Error("dependency evidence is incomplete or inconsistent");
  }
  return Object.fromEntries(
    dependencySpecs.map(([inputName, outputName]) => {
      const pin = source[inputName];
      if (
        !exactKeys(pin, ["revision", "narHash"])
        || !isHex(pin.revision, 40)
        || !isNarHash(pin.narHash)
      ) {
        throw new Error(`${inputName} dependency pin is invalid`);
      }
      return [
        outputName,
        {
          revision: pin.revision,
          narHash: pin.narHash,
        },
      ];
    }),
  );
}

function publicLgxPackages(compiled, reports) {
  const candidates = [
    compiled.packageHashes,
    reports.gate1.value.lgxPackages,
    reports.gate2.value.productionLgxPackages,
    reports.gate3.value.packageHashes,
    reports.gate4To6.value.packageHashes,
  ];
  const first = candidates[0];
  if (
    !Array.isArray(first)
    || first.length !== lgxPackageNames.length
    || JSON.stringify(first.map(({ file }) => file))
      !== JSON.stringify(lgxPackageNames)
  ) {
    throw new Error("compiled LGX package evidence is not exact");
  }
  for (const candidate of candidates) {
    if (
      !Array.isArray(candidate)
      || !exactJson(candidate, first)
      || candidate.some(
        (entry, index) =>
          !exactKeys(entry, ["file", "sha256"])
          || entry.file !== lgxPackageNames[index]
          || !isSha256(entry.sha256),
      )
    ) {
      throw new Error("raw and compiled LGX package evidence differs");
    }
  }
  return first.map(({ file, sha256: digest }) => ({
    file,
    sha256: digest,
  }));
}

function publicTestOnlyVariants(gate2, productionPackages) {
  const palaceCoreFile = "logos-palace_core-module-lib.lgx";
  const installedPackages = gate2.lgxPackages;
  const declaredProduction = gate2.productionLgxPackages;
  const installedCore = installedPackages?.find(
    ({ file }) => file === palaceCoreFile,
  );
  const productionCore = declaredProduction?.find(
    ({ file }) => file === palaceCoreFile,
  );
  const installedNonCore = installedPackages?.filter(
    ({ file }) => file !== palaceCoreFile,
  );
  const productionNonCore = declaredProduction?.filter(
    ({ file }) => file !== palaceCoreFile,
  );
  const expectedBinding = {
    kind: "test-only-acceptance-fixtures",
    file: palaceCoreFile,
    runtimeOutput: "palace-core-acceptance-lgx",
    productionSha256: productionCore?.sha256,
    installedSha256: installedCore?.sha256,
  };
  if (
    !Array.isArray(installedPackages)
    || installedPackages.length !== lgxPackageNames.length
    || !exactJson(declaredProduction, productionPackages)
    || !exactJson(installedNonCore, productionNonCore)
    || !installedCore
    || !productionCore
    || !isSha256(installedCore.sha256)
    || installedCore.sha256 === productionCore.sha256
    || !exactJson(gate2.runtimeVariants, {
      palaceCore: expectedBinding,
    })
  ) {
    throw new Error(
      "Gate 2 test-only Palace Core replacement is not exact",
    );
  }
  return {
    gate2PalaceCore: expectedBinding,
  };
}

function publicTerminalCompletion(completion, compiled) {
  const value = completion.value;
  if (
    !exactKeys(
      value,
      [
        "schema",
        "version",
        "status",
        "completedAtUnixMs",
        "activeClaimSha256",
        "compiledReportSha256",
        "productSnapshot",
        "sourceCommit",
        "productSnapshotNarHash",
        "productSnapshotNarSize",
        "snapshotRunnerSha256",
        "runtimeOutputManifestSha256",
      ],
    )
    || value.schema !== "logos.palace.basecamp-active-run-completion"
    || value.version !== 1
    || value.status !== "completed"
    || !Number.isSafeInteger(value.completedAtUnixMs)
    || value.completedAtUnixMs <= 0
    || !isSha256(value.activeClaimSha256)
    || value.compiledReportSha256 !== compiled.sha256
    || value.productSnapshot !== compiled.value.productSnapshot
    || value.sourceCommit !== compiled.value.sourceCommit
    || value.productSnapshotNarHash
      !== compiled.value.productSnapshotNarHash
    || value.productSnapshotNarSize
      !== compiled.value.productSnapshotNarSize
    || value.snapshotRunnerSha256
      !== compiled.value.snapshotRunnerSha256
    || value.runtimeOutputManifestSha256
      !== compiled.value.runtimeOutputs?.manifestSha256
  ) {
    throw new Error("terminal active-run completion is invalid");
  }
  return {
    status: value.status,
    completedAtUnixMs: value.completedAtUnixMs,
    activeClaimSha256: value.activeClaimSha256,
    compiledReportSha256: value.compiledReportSha256,
  };
}

function validateRuntimeManifest(compiled, runtimeManifest) {
  if (
    !exactKeys(
      compiled.runtimeOutputs,
      ["manifest", "manifestSha256"],
    )
    || compiled.runtimeOutputs.manifest !== "runtime-output-manifest.json"
    || compiled.runtimeOutputs.manifestSha256 !== runtimeManifest.sha256
    || runtimeManifest.value.schema
      !== "logos.palace.runtime-output-manifest"
    || runtimeManifest.value.version !== 1
    || !Array.isArray(runtimeManifest.value.outputs)
    || runtimeManifest.value.outputs.length !== runtimeOutputNames.length
    || JSON.stringify(
      runtimeManifest.value.outputs.map(({ name }) => name),
    ) !== JSON.stringify(runtimeOutputNames)
  ) {
    throw new Error("runtime output manifest binding is invalid");
  }
  for (const output of runtimeManifest.value.outputs) {
    if (
      !exactKeys(
        output,
        ["name", "narHash", "narSize"],
      )
      || !runtimeOutputNames.includes(output.name)
      || !isNarHash(output.narHash)
      || !Number.isSafeInteger(output.narSize)
      || output.narSize <= 0
    ) {
      throw new Error("runtime output manifest entry is invalid");
    }
  }
  const verifier = runtimeManifest.value.outputs.find(
    ({ name }) => name === "release-verifier",
  );
  if (!exactJson(
    compiled.releaseVerifier,
    {
      narHash: verifier.narHash,
      narSize: verifier.narSize,
    },
  )) {
    throw new Error("release verifier differs from runtime manifest");
  }
  return {
    manifestSha256: runtimeManifest.sha256,
    outputs: runtimeManifest.value.outputs.map(
      ({ name, narHash, narSize }) => ({
        name,
        narHash,
        narSize,
      }),
    ),
  };
}

function processRuntimeIdentity(process) {
  const moduleName = process?.moduleName ?? null;
  let role = process?.role;
  if (role === undefined) {
    if (
      moduleName === null
      && process?.programArgument === ".LogosBasecamp.elf"
    ) {
      role = "basecamp-main";
    } else if (
      moduleName === "logos_palace_ui"
      && process?.programArgument === ".ui-host.elf"
    ) {
      role = "ui-module-host";
    } else if (
      processInventoryContract.coreModuleHosts.includes(moduleName)
      && process?.programArgument === ".logos_host.elf"
    ) {
      role = "core-module-host";
    }
  }
  if (
    !["basecamp-main", "core-module-host", "ui-module-host"].includes(role)
  ) {
    return undefined;
  }
  return `${role}:${moduleName ?? ""}`;
}

function canonicalAbsoluteProcessPath(value) {
  return (
    typeof value === "string"
    && value.startsWith("/")
    && !value.includes("\0")
    && !value.split("/").some((part) => part === "." || part === "..")
  );
}

function validRawFileIdentity(identity) {
  return (
    exactKeys(identity, ["device", "inode"])
    && /^[0-9a-f]+:[0-9a-f]+$/.test(identity.device)
    && /^[1-9][0-9]*$/.test(identity.inode)
  );
}

function sameRawFileIdentity(left, right) {
  return (
    validRawFileIdentity(left)
    && validRawFileIdentity(right)
    && left.device === right.device
    && left.inode === right.inode
  );
}

function validRawExecutableMapping(
  mapping,
  expectedPath,
  expectedIdentity,
) {
  return (
    exactKeys(mapping, ["path", "permissions", "device", "inode"])
    && mapping.path === expectedPath
    && /^[r-][w-]x[ps]$/.test(mapping.permissions)
    && /^[0-9a-f]+:[0-9a-f]+$/.test(mapping.device)
    && /^[1-9][0-9]*$/.test(mapping.inode)
    && (
      expectedIdentity === undefined
      || (
        validRawFileIdentity(expectedIdentity)
        && mapping.device === expectedIdentity.device
        && mapping.inode === expectedIdentity.inode
      )
    )
  );
}

function exactWrapperExecution(runtime, model) {
  const publicExecution = runtime?.wrapperExecution;
  const rawSelection = model?.loaderSelection;
  const executionKeys = [
    "mode",
    "fallbackIndex",
    "argumentBasename",
    "executableBasename",
    "sha256",
  ];
  if (
    !exactKeys(publicExecution, executionKeys)
    || !exactKeys(
      rawSelection,
      [
        "mode",
        "fallbackIndex",
        "candidatePath",
        "canonicalPath",
        "argumentBasename",
        "executableBasename",
        "sha256",
        "device",
        "inode",
      ],
    )
    || !exactJson(
      Object.fromEntries(
        executionKeys.map((key) => [key, rawSelection[key]]),
      ),
      publicExecution,
    )
    || !validRawFileIdentity({
      device: rawSelection.device,
      inode: rawSelection.inode,
    })
  ) {
    throw new Error("Basecamp wrapper execution proof is incomplete");
  }
  if (publicExecution.mode === "direct") {
    if (
      publicExecution.fallbackIndex !== null
      || publicExecution.argumentBasename !== null
      || rawSelection.candidatePath !== basecampWrapperDirectLoaderPath
      || !canonicalAbsoluteProcessPath(rawSelection.canonicalPath)
      || basename(rawSelection.canonicalPath)
        !== publicExecution.executableBasename
      || typeof publicExecution.executableBasename !== "string"
      || publicExecution.executableBasename.length === 0
      || publicExecution.executableBasename.includes("/")
      || !isSha256(publicExecution.sha256)
    ) {
      throw new Error("Basecamp direct wrapper execution proof is invalid");
    }
    return publicExecution;
  }
  const fallbackIndex = publicExecution.fallbackIndex;
  const candidatePath =
    basecampWrapperFallbackLoaderPaths[fallbackIndex];
  if (
    publicExecution.mode !== "fallback"
    || !Number.isSafeInteger(fallbackIndex)
    || fallbackIndex < 0
    || fallbackIndex >= basecampWrapperFallbackLoaderPaths.length
    || rawSelection.candidatePath !== candidatePath
    || !canonicalAbsoluteProcessPath(rawSelection.canonicalPath)
    || basename(candidatePath) !== publicExecution.argumentBasename
    || basename(rawSelection.canonicalPath)
      !== publicExecution.executableBasename
    || !/^ld(?:-[a-z0-9_-]+)?-linux[^/]*\.so(?:\.[0-9]+)*$/i.test(
      publicExecution.argumentBasename,
    )
    || typeof publicExecution.executableBasename !== "string"
    || publicExecution.executableBasename.length === 0
    || publicExecution.executableBasename.includes("/")
    || !isSha256(publicExecution.sha256)
  ) {
    throw new Error("Basecamp fallback wrapper execution proof is invalid");
  }
  return publicExecution;
}

function exactProcessRuntimeArtifacts(gate4) {
  const runtime = gate4.processModel?.runtimeArtifacts;
  const wrapperExecution = exactWrapperExecution(
    runtime,
    gate4.processModel,
  );
  const programs = runtime?.basecampBundlePrograms;
  const bundledModules = runtime?.basecampBundleModules;
  const installedModules = runtime?.installedLgxModules;
  if (
    !exactKeys(
      runtime,
      [
        "basecampBundlePrograms",
        "basecampBundleModules",
        "installedLgxModules",
        "wrapperExecution",
      ],
    )
    || !Array.isArray(programs)
    || programs.length !== processProgramArtifactSpecs.length
    || programs.some((entry, index) => {
      const expected = processProgramArtifactSpecs[index];
      return (
        !exactKeys(
          entry,
          ["role", "program", "argument", "relativePath", "sha256"],
        )
        || !exactJson(
          {
            role: entry.role,
            program: entry.program,
            argument: entry.argument,
            relativePath: entry.relativePath,
          },
          expected,
        )
        || !isSha256(entry.sha256)
      );
    })
    || !Array.isArray(bundledModules)
    || bundledModules.length !== bundledProcessModuleSpecs.length
    || bundledModules.some((entry, index) => {
      const expected = bundledProcessModuleSpecs[index];
      return (
        !exactKeys(entry, ["moduleName", "relativePath", "sha256"])
        || entry.moduleName !== expected.moduleName
        || entry.relativePath !== expected.relativePath
        || !isSha256(entry.sha256)
      );
    })
    || !Array.isArray(installedModules)
    || installedModules.length !== installedProcessModuleSpecs.length
    || installedModules.some((entry, index) => {
      const expected = installedProcessModuleSpecs[index];
      const source = gate4.packageHashes?.find(
        ({ file }) => file === expected.packageFile,
      );
      return (
        !exactKeys(
          entry,
          [
            "moduleName",
            "packageFile",
            "packageSha256",
            "installedRootSha256",
            "mainFile",
            "mainFileSha256",
          ],
        )
        || entry.moduleName !== expected.moduleName
        || entry.packageFile !== expected.packageFile
        || entry.mainFile !== expected.mainFile
        || !source
        || entry.packageSha256 !== source.sha256
        || !isSha256(entry.packageSha256)
        || !isSha256(entry.installedRootSha256)
        || !isSha256(entry.mainFileSha256)
      );
    })
  ) {
    throw new Error("Basecamp pinned runtime artifact proof is incomplete");
  }
  const programByRole = new Map(
    programs.map((entry) => [entry.role, entry]),
  );
  const moduleByName = new Map([
    ...bundledModules.map((entry) => [
      entry.moduleName,
      {
        sha256: entry.sha256,
        basename: basename(entry.relativePath),
      },
    ]),
    ...installedModules.map((entry) => [
      entry.moduleName,
      {
        sha256: entry.mainFileSha256,
        basename: entry.mainFile,
      },
    ]),
  ]);
  const expected = new Map();
  const add = (role, moduleName) => {
    const program = programByRole.get(role);
    const module = moduleName ? moduleByName.get(moduleName) : undefined;
    if (!program || (moduleName && !module)) {
      throw new Error("Basecamp pinned runtime artifact map is incomplete");
    }
    expected.set(`${role}:${moduleName ?? ""}`, {
      programArgument: program.argument,
      programSha256: program.sha256,
      executionMode: wrapperExecution.mode,
      loaderPath: undefined,
      loaderArgument: wrapperExecution.argumentBasename,
      loaderExecutable: wrapperExecution.executableBasename,
      loaderSha256: wrapperExecution.sha256,
      moduleSha256: module?.sha256 ?? null,
      moduleBasename: module?.basename ?? null,
    });
  };
  add("basecamp-main", null);
  for (const moduleName of processInventoryContract.coreModuleHosts) {
    add("core-module-host", moduleName);
  }
  add("ui-module-host", "logos_palace_ui");
  if (expected.size !== 10) {
    throw new Error("Basecamp pinned runtime artifact map is not exact");
  }
  expected.loaderSelection = gate4.processModel.loaderSelection;
  expected.wrapperExecution = wrapperExecution;
  return expected;
}

function validateRawProcessArtifactBinding(
  process,
  expectedArtifact,
  runtimeArtifacts,
) {
  if (!expectedArtifact) {
    throw new Error("Basecamp process has no pinned runtime artifact");
  }
  validateProcessExecutableIdentity(
    {
      executable: process.executable,
      executableSha256: process.executableSha256,
      executableArgument: process.executableArgument,
      executableArgumentSha256: process.executableArgumentSha256,
      programArgument: process.programArgument,
      programArgumentSha256: process.programArgumentSha256,
      directInterpreterSha256: process.directInterpreterSha256,
      moduleArgumentSha256: process.moduleArgumentSha256,
      moduleArtifactSha256: process.moduleArtifactSha256,
    },
    expectedArtifact,
  );
  const loaderSelection = runtimeArtifacts.loaderSelection;
  const fallback = loaderSelection.mode === "fallback";
  const loaderFileIdentity = {
    device: loaderSelection.device,
    inode: loaderSelection.inode,
  };
  if (
    process.executionMode !== loaderSelection.mode
    || !canonicalAbsoluteProcessPath(
      process.programExecutableMapping?.path,
    )
    || basename(process.programExecutableMapping.path)
      !== process.programArgument
    || !validRawExecutableMapping(
      process.programExecutableMapping,
      process.programExecutableMapping.path,
      process.programArgumentFileIdentity,
    )
    || (
      fallback
        ? (
          !canonicalAbsoluteProcessPath(process.loaderPath)
          || process.loaderPath !== loaderSelection.canonicalPath
          || !sameRawFileIdentity(
            process.executableFileIdentity,
            loaderFileIdentity,
          )
        )
        : (
          process.loaderPath !== null
          || !sameRawFileIdentity(
            process.executableFileIdentity,
            process.programArgumentFileIdentity,
          )
        )
    )
    || (
      fallback
        ? (
          process.directInterpreterPath !== null
          || process.directInterpreterSha256 !== null
          || process.directInterpreterFileIdentity !== null
          || process.directInterpreterMapping !== null
        )
        : (
          !canonicalAbsoluteProcessPath(
            process.directInterpreterPath,
          )
          || process.directInterpreterPath
            !== loaderSelection.canonicalPath
          || process.directInterpreterSha256
            !== loaderSelection.sha256
          || !sameRawFileIdentity(
            process.directInterpreterFileIdentity,
            loaderFileIdentity,
          )
          || !validRawExecutableMapping(
            process.directInterpreterMapping,
            loaderSelection.canonicalPath,
            process.directInterpreterFileIdentity,
          )
        )
    )
  ) {
    throw new Error(
      "Basecamp raw process loader path differs from wrapper selection",
    );
  }
  if (expectedArtifact.moduleSha256 === null) {
    if (
      process.moduleArgumentPath !== null
      || process.moduleArtifactPath !== null
      || process.moduleArgumentFileIdentity !== null
      || process.moduleExecutableMapping !== null
    ) {
      throw new Error("Basecamp main process has unexpected module binding");
    }
    return true;
  }
  const mapping = process.moduleExecutableMapping;
  if (
    !canonicalAbsoluteProcessPath(process.moduleArgumentPath)
    || process.moduleArgumentPath !== process.moduleArtifactPath
    || basename(process.moduleArgumentPath)
      !== expectedArtifact.moduleBasename
    || !validRawExecutableMapping(
      mapping,
      process.moduleArtifactPath,
      process.moduleArgumentFileIdentity,
    )
  ) {
    throw new Error(
      "Basecamp module argv or executable mapping proof is invalid",
    );
  }
  return true;
}

function publicProcessProof(gate4) {
  const model = gate4.processModel;
  const runtimeArtifacts = exactProcessRuntimeArtifacts(gate4);
  if (
    !exactKeys(
      model,
      [
        "standalonePalaceServer",
        "loaderSelection",
        "runtimeArtifacts",
        "observationMethod",
        "observations",
      ],
    )
    || model.standalonePalaceServer !== false
    || model.observationMethod
      !== "bounded /proc exact process inventory, pinned runtime artifacts, and owned TCP LISTEN proof"
    || !Array.isArray(model.observations)
    || model.observations.length === 0
  ) {
    throw new Error("Basecamp process proof is incomplete");
  }
  const expectedProcesses = [
    ["basecamp-main", null, "LogosBasecamp"],
    ...processInventoryContract.coreModuleHosts.map(
      (name) => ["core-module-host", name, "logos_host"],
    ),
    ["ui-module-host", "logos_palace_ui", "ui-host"],
  ].sort();
  const moduleArgumentArtifacts = [
    ...processInventoryContract.coreModuleHosts.map(
      (moduleName) => ["core-module-host", moduleName],
    ),
    ["ui-module-host", "logos_palace_ui"],
  ].map(([role, moduleName]) => {
    const artifact = runtimeArtifacts.get(`${role}:${moduleName}`);
    return {
      role,
      moduleName,
      argumentBasename: artifact.moduleBasename,
      sha256: artifact.moduleSha256,
    };
  });
  for (const observation of model.observations) {
    const processes = observation?.processes;
    const listenerProof = observation?.tcpListenerProof;
    const processEntries = Array.isArray(processes) ? processes : [];
    const listenerEntries = Array.isArray(listenerProof?.listeners)
      ? listenerProof.listeners
      : [];
    const processByPid = new Map(
      processEntries.map((process) => [process.pid, process]),
    );
    const listenerPurposes = listenerEntries
      .map(({ purpose }) => purpose)
      .sort();
    const validListenerPurposeSet = (
      listenerPurposes.length >= 1
      && listenerPurposes.length <= 3
      && listenerPurposes.filter(
        (purpose) => purpose === "qml-inspector",
      ).length === 1
      && listenerPurposes.filter(
        (purpose) => purpose === "delivery-transport",
      ).length <= 1
      && listenerPurposes.filter(
        (purpose) => purpose === "storage-transport",
      ).length <= 1
      && listenerPurposes.every((purpose) =>
        [
          "delivery-transport",
          "qml-inspector",
          "storage-transport",
        ].includes(purpose))
    );
    if (
      !exactKeys(
        observation,
        [
          "rootPid",
          "rootProcessGroupId",
          "rootSessionId",
          "scope",
          "standalonePalaceServerScanScope",
          "inventoryContract",
          "processCount",
          "processes",
          "tcpListenerProof",
          "standalonePalaceServerMatches",
        ],
      )
      || !Number.isSafeInteger(observation.rootPid)
      || observation.rootPid <= 0
      || !Number.isSafeInteger(observation.rootProcessGroupId)
      || observation.rootProcessGroupId <= 0
      || !Number.isSafeInteger(observation.rootSessionId)
      || observation.rootSessionId <= 0
      || observation.scope
        !== "recursive descendants plus matching Basecamp process group/session"
      || observation.standalonePalaceServerScanScope
        !== "all same-effective-UID processes visible in bounded /proc scan"
      || !exactJson(
        observation.inventoryContract,
        processInventoryContract,
      )
      || observation.standalonePalaceServerMatches?.length !== 0
      || observation.processCount !== 10
      || !Array.isArray(processes)
      || processes.length !== observation.processCount
      || processByPid.size !== 10
      || !processes.some(
        ({ pid, role }) =>
          pid === observation.rootPid && role === "basecamp-main",
      )
      || processes.some((process) => {
        const expectedArtifact = runtimeArtifacts.get(
          processRuntimeIdentity(process),
        );
        return (
          !exactKeys(
            process,
            [
              "pid",
              "name",
              "executable",
              "executableSha256",
              "executableArgument",
              "executableArgumentSha256",
              "programArgument",
              "programArgumentSha256",
              "executableFileIdentity",
              "programArgumentFileIdentity",
              "programExecutableMapping",
              "executionMode",
              "loaderPath",
              "directInterpreterPath",
              "directInterpreterSha256",
              "directInterpreterFileIdentity",
              "directInterpreterMapping",
              "moduleArgumentPath",
              "moduleArgumentSha256",
              "moduleArgumentFileIdentity",
              "moduleArtifactPath",
              "moduleArtifactSha256",
              "moduleExecutableMapping",
              "moduleName",
              "role",
              "program",
            ],
          )
          || !Number.isSafeInteger(process.pid)
          || process.pid <= 0
          || typeof process.name !== "string"
          || typeof process.executable !== "string"
          || typeof process.executableArgument !== "string"
          || !expectedArtifact
          || (() => {
            try {
              return !validateRawProcessArtifactBinding(
                process,
                expectedArtifact,
                runtimeArtifacts,
              );
            } catch {
              return true;
            }
          })()
        );
      })
      || !exactJson(
        processes.map(
          ({ role, moduleName, program }) =>
            [role, moduleName, program],
        ).sort(),
        expectedProcesses,
      )
      || listenerProof?.scope
        !== "TCP LISTEN sockets owned by exact Basecamp process inventory"
      || !exactKeys(
        listenerProof,
        ["scope", "expectedOnly", "listenerCount", "listeners"],
      )
      || listenerProof.expectedOnly !== true
      || !Array.isArray(listenerProof.listeners)
      || listenerProof.listenerCount !== listenerProof.listeners.length
      || listenerProof.listenerCount <= 0
      || !validListenerPurposeSet
      || new Set(listenerProof.listeners.map(
        ({ protocol, address, port, ownerPid }) =>
          `${protocol}\0${address}\0${port}\0${ownerPid}`,
      )).size !== listenerProof.listenerCount
    ) {
      throw new Error("Basecamp process observation is not exact");
    }
    for (const listener of listenerProof.listeners) {
      const owner = processes.find(({ pid }) => pid === listener.ownerPid);
      const validPurpose = (
        listener.purpose === "qml-inspector"
        && listener.address === "127.0.0.1"
        && listener.ownerRole === "basecamp-main"
        && listener.moduleName === null
      ) || (
        listener.purpose === "delivery-transport"
        && listener.address === "127.0.0.1"
        && listener.ownerRole === "core-module-host"
        && listener.moduleName === "delivery_module"
      ) || (
        listener.purpose === "storage-transport"
        && listener.address === "0.0.0.0"
        && listener.ownerRole === "core-module-host"
        && listener.moduleName === "storage_module"
      );
      if (
        !exactKeys(
          listener,
          [
            "protocol",
            "address",
            "port",
            "ownerPid",
            "ownerRole",
            "moduleName",
            "purpose",
          ],
        )
        || listener.protocol !== "tcp4"
        || !Number.isSafeInteger(listener.port)
        || listener.port < 1024
        || listener.port > 65535
        || !owner
        || owner.role !== listener.ownerRole
        || owner.moduleName !== listener.moduleName
        || !validPurpose
      ) {
        throw new Error("Basecamp TCP listener proof is invalid");
      }
    }
  }
  return {
    standalonePalaceServer: false,
    exactProcessInventory: true,
    exactPinnedRuntimeArtifacts: true,
    exactWrapperExecution: true,
    exactOpenedExecutableBindings: true,
    exactProgramExecutableMappings: true,
    exactDirectInterpreterMappings: true,
    exactModuleArgumentArtifacts: true,
    exactExecutableModuleMappings: true,
    exactOwnedTcpListeners: true,
    observationCount: model.observations.length,
    inventory: processInventoryContract,
    wrapperExecution: runtimeArtifacts.wrapperExecution,
    moduleArgumentArtifacts,
  };
}

function processIdentityInventory(
  value,
  observation,
  runtimeArtifacts,
) {
  const rootPid = observation ? value?.rootPid : value?.pid;
  const processCount = observation
    ? value?.processCount
    : value?.processTree?.processCount;
  const processes = observation
    ? value?.processes
    : value?.processTree?.processes;
  if (
    !Number.isSafeInteger(rootPid)
    || rootPid <= 0
    || processCount !== 10
    || !Array.isArray(processes)
    || processes.length !== processCount
    || new Set(processes.map(({ pid }) => pid)).size !== processCount
    || !processes.some(({ pid }) => pid === rootPid)
  ) {
    return undefined;
  }
  const identities = [];
  for (const process of processes) {
    const expectedArtifact = runtimeArtifacts.get(
      processRuntimeIdentity(process),
    );
    try {
      validateRawProcessArtifactBinding(
        process,
        expectedArtifact,
        runtimeArtifacts,
      );
    } catch {
      return undefined;
    }
    if (
      !Number.isSafeInteger(process.pid)
      || process.pid <= 0
      || typeof process.name !== "string"
      || process.name.length === 0
      || (
        process.moduleName !== undefined
        && process.moduleName !== null
        && typeof process.moduleName !== "string"
      )
    ) {
      return undefined;
    }
    identities.push({
      pid: process.pid,
      name: process.name,
      executable: process.executable,
      executableSha256: process.executableSha256,
      executableArgument: process.executableArgument,
      executableArgumentSha256: process.executableArgumentSha256,
      programArgument: process.programArgument,
      programArgumentSha256: process.programArgumentSha256,
      executionMode: process.executionMode,
      loaderArgument: expectedArtifact.loaderArgument,
      loaderExecutable: expectedArtifact.loaderExecutable,
      loaderSha256: expectedArtifact.loaderSha256,
      executableFileIdentityBound: true,
      programExecutableMapped: true,
      directInterpreterSha256: process.directInterpreterSha256,
      directInterpreterMapped:
        process.directInterpreterMapping !== null,
      moduleArgumentSha256: process.moduleArgumentSha256,
      moduleArtifactSha256: process.moduleArtifactSha256,
      moduleExecutableMapped:
        process.moduleExecutableMapping !== null,
      moduleName: process.moduleName ?? null,
    });
  }
  identities.sort((left, right) => left.pid - right.pid);
  return {
    rootPid,
    processCount,
    processes: identities,
  };
}

function publicRecoveryEvidence(compiled, runtime, gate2, gate4) {
  const coreOutput = runtime.outputs.find(
    ({ name }) => name === "palace-core-contracts",
  );
  const vmOutput = runtime.outputs.find(
    ({ name }) => name === "palace-vm-contracts",
  );
  const acceptedSubmission =
    compiled.contractProofs?.acceptedSubmissionCrashRecovery;
  const coldReplay = compiled.contractProofs?.coldReplay;
  const vm = compiled.contractProofs?.palaceVm;
  const failure = gate4.failureEvidence;
  const ui = gate4.uiEvidence;
  const activeBackground = gate4.catalog?.byId?.["background-atrium"];
  const crashLaunch = gate2.restartRecovery?.launch;
  const crash = crashLaunch?.crash;
  const coldRebuild = failure?.coldClientRebuild;
  const coldBefore = coldRebuild?.before;
  const storageRestart = gate4.storage?.restart;
  const storageSource = storageRestart?.sourceBinding;
  const sourceProcess = storageSource?.storageProcess;
  const runtimeArtifacts = exactProcessRuntimeArtifacts(gate4);
  const sourceProcessInventory = processIdentityInventory(
    sourceProcess,
    false,
    runtimeArtifacts,
  );
  const restartCProcessInventory = processIdentityInventory(
    gate4.restart?.c?.memory,
    false,
    runtimeArtifacts,
  );
  const sourceProcessObservations = (
    gate4.processModel?.observations ?? []
  ).filter((observation) => (
    exactJson(
      processIdentityInventory(observation, true, runtimeArtifacts),
      sourceProcessInventory,
    )
    && exactJson(
      observation.tcpListenerProof?.listeners
        ?.map(({ purpose }) => purpose)
        .sort(),
      ["qml-inspector", "storage-transport"],
    )
  ));
  const sourceProcessObservation = sourceProcessObservations[0];
  const missingBeforeReceipt = failure?.missingStorageObject
    ?.before?.receipt;
  const missingDispatchedReceipt = failure?.missingStorageObject
    ?.dispatched?.receipt;
  const missingDegradedReceipt = failure?.missingStorageObject
    ?.degraded?.receipt;
  const validFingerprint = (value) => (
    exactKeys(
      value,
      ["fileCount", "directoryCount", "totalBytes", "sha256"],
    )
    && Number.isSafeInteger(value.fileCount)
    && value.fileCount >= 0
    && Number.isSafeInteger(value.directoryCount)
    && value.directoryCount >= 1
    && Number.isSafeInteger(value.totalBytes)
    && value.totalBytes >= 0
    && isSha256(value.sha256)
  );
  const validFileDigest = (value) => (
    exactKeys(value, ["byteLength", "sha256"])
    && Number.isSafeInteger(value.byteLength)
    && value.byteLength > 0
    && isSha256(value.sha256)
  );
  const validWalletEvidence = (value) => (
    exactKeys(value, ["config", "storage", "combinedSha256"])
    && validFileDigest(value.config)
    && validFileDigest(value.storage)
    && isSha256(value.combinedSha256)
    && value.combinedSha256 === sha256(Buffer.from(JSON.stringify({
      config: value.config,
      storage: value.storage,
    })))
  );
  if (
    !coreOutput
    || !vmOutput
    || !exactKeys(
      compiled.contractProofs,
      ["acceptedSubmissionCrashRecovery", "coldReplay", "palaceVm"],
    )
    || !exactKeys(
      acceptedSubmission,
      ["status", "tests", "runtimeOutput", "narHash", "narSize"],
    )
    || acceptedSubmission.status !== "passed"
    || !exactJson(
      acceptedSubmission.tests,
      acceptedSubmissionCrashRecoveryContractTests,
    )
    || acceptedSubmission.runtimeOutput !== coreOutput.name
    || acceptedSubmission.narHash !== coreOutput.narHash
    || acceptedSubmission.narSize !== coreOutput.narSize
    || !exactKeys(
      coldReplay,
      ["status", "tests", "runtimeOutput", "narHash", "narSize"],
    )
    || coldReplay.status !== "passed"
    || !exactJson(coldReplay.tests, coldReplayContractTests)
    || coldReplay.runtimeOutput !== coreOutput.name
    || coldReplay.narHash !== coreOutput.narHash
    || coldReplay.narSize !== coreOutput.narSize
    || !exactKeys(
      vm,
      ["status", "tests", "runtimeOutput", "narHash", "narSize"],
    )
    || vm.status !== "passed"
    || !exactJson(vm.tests, palaceVmFinalityContractTests)
    || vm.runtimeOutput !== vmOutput.name
    || vm.narHash !== vmOutput.narHash
    || vm.narSize !== vmOutput.narSize
    || !Number.isSafeInteger(crash?.previousPid)
    || crash.previousPid <= 0
    || !Number.isSafeInteger(crashLaunch?.basecampPid)
    || crashLaunch.basecampPid <= 0
    || crashLaunch.basecampPid === crash.previousPid
    || crash.signal !== "SIGKILL"
    || crash.exitCode !== null
    || crash.graceful !== false
    || failure?.delayedLezUpdate?.status !== "passed"
    || failure.delayedLezUpdate.actionId !== "10"
    || failure.delayedLezUpdate.observationPaused !== true
    || !Number.isSafeInteger(failure.delayedLezUpdate.pauseMs)
    || failure.delayedLezUpdate.pauseMs < 1000
    || failure?.missingStorageObject?.status !== "passed"
    || failure.missingStorageObject.objectId !== "background-atrium"
    || failure.missingStorageObject.missingSourceCid
      !== missingStorageSourceCid
    || !isObject(gate4.catalog?.byId)
    || Object.values(gate4.catalog.byId).some(
      ({ cid }) => cid === missingStorageSourceCid,
    )
    || failure.missingStorageObject.derivativeCid
      !== activeBackground?.cid
    || failure.missingStorageObject.derivativeCid
      === failure.missingStorageObject.missingSourceCid
    || !isBase58(failure.missingStorageObject.derivativeCid)
    || failure.missingStorageObject.expectedContentSha256
      !== activeBackground?.contentSha256
    || !isSha256(failure.missingStorageObject.expectedContentSha256)
    || failure.missingStorageObject.states?.join(",")
      !== "missing,fetching,degraded"
    || missingBeforeReceipt !== "missing"
    || !/^ok;asset=fetching;operation=palace-asset-(?:0|[1-9][0-9]{0,19})$/.test(
      missingDispatchedReceipt ?? "",
    )
    || !/^degraded;reason=storage-download-[a-z0-9][a-z0-9-]{0,127}$/.test(
      missingDegradedReceipt ?? "",
    )
    || failure?.clientOffline?.status !== "passed"
    || failure.clientOffline.label !== "b"
    || failure.clientOffline.creatorOffline !== true
    || failure?.coldClientRebuild?.status !== "passed"
    || coldRebuild.label !== "b"
    || !exactJson(coldRebuild.removed, coldRebuildRemovedState)
    || !exactJson(coldRebuild.preserved, coldRebuildPreservedState)
    || !exactKeys(
      coldBefore,
      [
        "authority",
        "projection",
        "vmTurn",
        "vmFinality",
        "storage",
        "verifiedAssets",
        "identity",
        "lezWallet",
      ],
    )
    || !validFileDigest(coldBefore.authority)
    || !validFileDigest(coldBefore.projection)
    || !validFileDigest(coldBefore.vmTurn)
    || !validFileDigest(coldBefore.vmFinality)
    || !validFingerprint(coldBefore.storage)
    || !validFingerprint(coldBefore.verifiedAssets)
    || !validFileDigest(coldBefore.identity)
    || !validWalletEvidence(coldBefore.lezWallet)
    || !exactKeys(
      coldRebuild.preservationProof,
      [
        "deliveryIdentityUnchangedByDeletion",
        "lezWalletUnchangedByDeletion",
      ],
    )
    || coldRebuild.preservationProof
      .deliveryIdentityUnchangedByDeletion !== true
    || coldRebuild.preservationProof
      .lezWalletUnchangedByDeletion !== true
    || coldRebuild.expectedAuthoritySha256
      !== coldBefore.authority.sha256
    || !exactKeys(
      coldRebuild.authorityAfter,
      ["file", "bytes", "sha256"],
    )
    || coldRebuild.authorityAfter.file !== "lez-authority-bundle-v1"
    || coldRebuild.authorityAfter.bytes !== coldBefore.authority.byteLength
    || coldRebuild.authorityAfter.sha256
      !== coldBefore.authority.sha256
    || coldRebuild.identityAfterSha256 !== coldBefore.identity.sha256
    || !validWalletEvidence(coldRebuild.lezWalletAfter)
    || coldRebuild.lezWalletAfter.config.sha256
      !== coldBefore.lezWallet.config.sha256
    || coldRebuild.openedExistingLezWallet !== true
    || gate4.restart?.b?.lez?.fields?.wallet !== "opened"
    || coldRebuild.storageRecoveryMode !== "network"
    || !exactKeys(
      coldRebuild.vmProjection,
      ["phase", "actionId", "navigation", "stateRoot"],
    )
    || coldRebuild.vmProjection.phase !== "promoted"
    || coldRebuild.vmProjection.actionId !== "10"
    || coldRebuild.vmProjection.navigation !== "1"
    || !isHex(coldRebuild.vmProjection.stateRoot, 64)
    || coldRebuild.vmProjection.stateRoot
      !== gate4.plan?.doorState?.openedStateRootHex
    || coldRebuild.exactFinalizedProjection !== true
    || gate4.metrics?.storage?.restart?.b?.mode !== "network"
    || gate4.metrics?.storage?.restart?.c?.mode !== "cache"
    || storageRestart?.clients?.b?.recovered?.mode !== "network"
    || storageRestart.clients.b.recovered.nativeSource !== "network"
    || storageRestart.clients.b.recovered.nativeAvailable !== 0
    || storageRestart.clients.b.recovered.nativeTotal !== 11
    || storageRestart?.clients?.c?.recovered?.mode !== "cache"
    || storageRestart.clients.c.recovered.nativeSource !== "cache"
    || storageRestart.clients.c.recovered.nativeAvailable !== 11
    || storageRestart.clients.c.recovered.nativeTotal !== 11
    || storageSource?.sourceLabel !== "c"
    || storageSource.sourceAccountId !== gate4.identities?.c?.accountId
    || storageSource.exactCatalogChecksum !== gate4.catalog?.checksum
    || !isSha256(storageSource.exactCatalogChecksum)
    || !validFingerprint(storageSource.retainedDataRootBeforeRestart)
    || storageSource.retainedCatalogVerifiedBeforeColdFetch !== true
    || storageSource.retainedCatalogVerifiedAfterColdFetch !== true
    || storageSource.sourceNativeAvailable !== 11
    || storageSource.sourceNativeTotal !== 11
    || storageSource.coldClientNativeAvailable !== 0
    || storageSource.coldClientNativeTotal !== 11
    || storageSource.creatorOffline !== true
    || storageSource.coldClientDataRootRemoved !== true
    || storageSource.coldClientStorageNotStarted !== true
    || !exactJson(storageSource.onlineRetainedHolderLabels, ["c"])
    || storageSource.sourceRetainedAfterTransfer?.mode !== "cache"
    || storageSource.sourceRetainedAfterTransfer?.nativeSource !== "cache"
    || storageSource.providerAttribution
      !== "Storage API does not expose the serving peer; evidence binds the only retained local peer"
    || !sourceProcessInventory
    || !restartCProcessInventory
    || !exactJson(sourceProcessInventory, restartCProcessInventory)
    || sourceProcessObservations.length !== 1
    || !sourceProcessObservation
    || sourceProcessObservation.processCount !== 10
    || sourceProcess?.processTree?.processCount !== 10
    || !Array.isArray(sourceProcess.processTree.processes)
    || !exactJson(
      sourceProcess.processTree.processes.map(({ pid }) => pid).sort(
        (left, right) => left - right,
      ),
      sourceProcessObservation.processes.map(({ pid }) => pid).sort(
        (left, right) => left - right,
      ),
    )
    || sourceProcess.palaceVmHost?.moduleName !== "palace_vm"
    || !sourceProcessObservation.processes.some(
      ({ pid, moduleName }) =>
        pid === sourceProcess.palaceVmHost.pid
        && moduleName === "palace_vm",
    )
    || !sourceProcessObservation.tcpListenerProof?.listeners?.some(
      ({ ownerRole, moduleName, purpose }) =>
        ownerRole === "core-module-host"
        && moduleName === "storage_module"
        && purpose === "storage-transport",
    )
    || !Array.isArray(ui?.pending)
    || ui.pending.length < 2
    || !Array.isArray(ui.finalized)
    || ui.finalized.length < 1
    || !Array.isArray(ui.degraded)
    || ui.degraded.length < 1
    || !Array.isArray(ui.offline)
    || ui.offline.length < 1
  ) {
    throw new Error("recoverable failure evidence is incomplete");
  }
  return {
    delayedLezUpdate: true,
    pendingAction: true,
    missingStorageObject: {
      passed: true,
      objectId: failure.missingStorageObject.objectId,
      missingSourceCid: failure.missingStorageObject.missingSourceCid,
      sourceAbsentFromCatalog: true,
      derivativeCid: failure.missingStorageObject.derivativeCid,
      expectedContentSha256:
        failure.missingStorageObject.expectedContentSha256,
      states: [...failure.missingStorageObject.states],
      transitionEvidence: {
        before: missingBeforeReceipt,
        dispatched: missingDispatchedReceipt,
        degraded: missingDegradedReceipt,
      },
    },
    clientOffline: true,
    basecampCrashRestart: {
      passed: true,
      previousPid: crash.previousPid,
      replacementPid: crashLaunch.basecampPid,
      signal: crash.signal,
      exitCode: crash.exitCode,
      graceful: crash.graceful,
      processReplaced: true,
    },
    coldClientRebuild: {
      passed: true,
      label: coldRebuild.label,
      removed: [...coldRebuild.removed],
      preserved: [...coldRebuildPublicPreservedState],
      authority: {
        beforeSha256: coldBefore.authority.sha256,
        afterSha256: coldRebuild.authorityAfter.sha256,
        hashEqual: true,
      },
      deliveryBinding: {
        beforeSha256: coldBefore.identity.sha256,
        afterSha256: coldRebuild.identityAfterSha256,
        hashEqual: true,
      },
      lezStatePreserved: true,
      storageBefore: { ...coldBefore.storage },
      verifiedAssetCacheBefore: { ...coldBefore.verifiedAssets },
      storageRecoveryMode: coldRebuild.storageRecoveryMode,
      retainedSource: {
        coldClientMode: storageRestart.clients.b.recovered.mode,
        coldClientNativeSource:
          storageRestart.clients.b.recovered.nativeSource,
        coldClientDataRootRemoved: true,
        coldClientStorageNotStarted: true,
        coldClientNativeAvailable:
          storageSource.coldClientNativeAvailable,
        coldClientNativeTotal: storageSource.coldClientNativeTotal,
        retainedHolderLabel: storageSource.sourceLabel,
        retainedHolderMode: storageRestart.clients.c.recovered.mode,
        retainedHolderNativeSource:
          storageRestart.clients.c.recovered.nativeSource,
        retainedHolderNativeAvailable:
          storageSource.sourceNativeAvailable,
        retainedHolderNativeTotal: storageSource.sourceNativeTotal,
        retainedDataRootBeforeRestart: {
          ...storageSource.retainedDataRootBeforeRestart,
        },
        catalogChecksum: storageSource.exactCatalogChecksum,
        catalogVerifiedBeforeColdFetch: true,
        catalogVerifiedAfterColdFetch: true,
        creatorOffline: true,
        onlyRunScopedRetainedParticipantOnline: true,
        servingParticipantExposed: false,
        attributionBasis: "topology-constrained inference",
        exactProcessProof: true,
      },
      vmProjection: {
        phase: coldRebuild.vmProjection.phase,
        actionId: coldRebuild.vmProjection.actionId,
        navigation: true,
        stateRoot: coldRebuild.vmProjection.stateRoot,
      },
      exactFinalizedProjection: true,
    },
    acceptedSubmissionCrashRecovery: {
      passed: true,
      contractTests: [...acceptedSubmission.tests],
      runtimeOutput: acceptedSubmission.runtimeOutput,
      narHash: acceptedSubmission.narHash,
      narSize: acceptedSubmission.narSize,
    },
    coldReplay: {
      passed: true,
      contractTests: [...coldReplay.tests],
      runtimeOutput: coldReplay.runtimeOutput,
      narHash: coldReplay.narHash,
      narSize: coldReplay.narSize,
    },
    palaceVmFinality: {
      passed: true,
      contractTests: [...vm.tests],
      runtimeOutput: vm.runtimeOutput,
      narHash: vm.narHash,
      narSize: vm.narSize,
    },
  };
}

function validateDirectEntryTopology(gate4) {
  const mesh = gate4.delivery?.initialMesh;
  if (
    mesh?.entryLabel !== "a"
    || typeof mesh.entryNode !== "string"
    || !/^\/ip4\/127\.0\.0\.1\/tcp\/[0-9]+\/p2p\/[1-9A-HJ-NP-Za-km-z]+$/.test(
      mesh.entryNode,
    )
    || !exactKeys(mesh.configs, ["a", "b", "c"])
  ) {
    throw new Error("raw Delivery topology is not direct-entry-node");
  }
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
  ];
  const configs = {};
  for (const label of ["a", "b", "c"]) {
    try {
      configs[label] = JSON.parse(mesh.configs[label]);
    } catch {
      throw new Error("raw Delivery topology config is invalid JSON");
    }
    const config = configs[label];
    if (
      !exactKeys(config, expectedKeys)
      || config.mode !== (label === "a" ? "Core" : "Edge")
      || config.relay !== true
      || config.store !== false
      || config.clusterId !== 4346
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
      || !isSha256(config.nodekey)
      || (
        label === "a"
          ? config.entryNodes.length !== 0
          : (
              config.entryNodes.length !== 1
              || config.entryNodes[0] !== mesh.entryNode
            )
      )
    ) {
      throw new Error(`raw Delivery ${label} topology differs`);
    }
  }
  if (
    new Set(Object.values(configs).map(({ tcpPort }) => tcpPort)).size !== 3
    || new Set(Object.values(configs).map(({ nodekey }) => nodekey)).size !== 3
  ) {
    throw new Error("raw Delivery topology reuses port or node key");
  }
}

function publicContracts(reports) {
  const gate2 = reports.gate2.value;
  const gate3 = reports.gate3.value;
  const gate4 = reports.gate4To6.value;
  if (
    !exactJson(
      gate2.acceptanceContract,
      {
        deliveryEnvelope: protocolContract.deliveryEnvelope,
        deliveryNetworkId: "logos.test",
      },
    )
    || !exactJson(gate4.releaseContract?.protocols, protocolContract)
    || !exactJson(gate4.releaseContract?.network, networkContract)
    || networkContract.lezProgramIdHex !== palaceRelease.programIdHex
    || networkContract.lezProgramBytecodeSha256
      !== palaceRelease.programBytecodeSha256
  ) {
    throw new Error("raw protocol or network release contract differs");
  }
  if (
    !exactKeys(gate3.storageConfigs, ["a", "b", "c"])
    || Object.values(gate3.storageConfigs).some((encoded) => {
      let config;
      try {
        config = JSON.parse(encoded);
      } catch {
        return true;
      }
      return (
        !exactKeys(
          config,
          [
            "disc-port",
            "listen-ip",
            "listen-port",
            "log-level",
            "nat",
            "network",
          ],
        )
        || config.network !== networkContract.storageNetworkId
        || config["log-level"] !== "INFO"
        || config["listen-ip"] !== "0.0.0.0"
        || config.nat !== "any"
        || !Number.isSafeInteger(config["listen-port"])
        || config["listen-port"] < 1024
        || config["listen-port"] > 65535
        || !Number.isSafeInteger(config["disc-port"])
        || config["disc-port"] < 1024
        || config["disc-port"] > 65535
      );
    })
  ) {
    throw new Error("raw Storage network evidence differs");
  }
  validateDirectEntryTopology(gate4);
  return {
    protocols: { ...protocolContract },
    network: {
      acceptanceDeliveryNetworkId:
        gate2.acceptanceContract.deliveryNetworkId,
      ...networkContract,
    },
  };
}

function basecampEvidence(compiled, reports, dependencies) {
  const candidates = [
    compiled.basecamp,
    reports.gate1.value.basecamp,
    reports.gate2.value.basecamp,
    {
      revision: reports.gate3.value.basecampRevision,
      sha256: reports.gate3.value.basecampBinarySha256,
    },
    {
      revision: reports.gate4To6.value.basecampRevision,
      sha256: reports.gate4To6.value.basecampBinarySha256,
    },
  ].map((value) => ({
    revision: value?.revision,
    sha256: value?.sha256,
  }));
  if (
    candidates.some(
      (value) =>
        !isHex(value.revision, 40)
        || !isSha256(value.sha256)
        || !exactJson(value, candidates[0]),
    )
    || candidates[0].revision !== dependencies.basecamp.revision
  ) {
    throw new Error("Basecamp revision or binary digest differs");
  }
  return {
    revision: candidates[0].revision,
    binarySha256: candidates[0].sha256,
  };
}

function sandboxTestEvidence(compiled, reports, basecamp, runtime) {
  const raw = reports.gate0.value;
  const binding = compiled.gates?.gate0;
  const basecampOutput = runtime.outputs.find(
    ({ name }) => name === "basecamp",
  );
  if (
    !basecampOutput
    || raw.check !== "sandbox-test"
    || raw.status !== "passed"
    || raw.basecampRevision !== basecamp.revision
    || raw.basecampRuntimeOutput !== basecampOutput.name
    || raw.basecampNarHash !== basecampOutput.narHash
    || raw.basecampNarSize !== basecampOutput.narSize
    || !isNarHash(raw.sandboxTestNarHash)
    || !Number.isSafeInteger(raw.sandboxTestNarSize)
    || raw.sandboxTestNarSize <= 0
    || typeof raw.sandboxTestOutput !== "string"
    || !/^\/nix\/store\/[0-9abcdfghijklmnpqrsvwxyz]{32}-[^/]+$/.test(
      raw.sandboxTestOutput,
    )
    || binding?.check !== "sandbox-test"
    || binding.output !== raw.sandboxTestOutput
    || binding.reportSha256 !== reports.gate0.sha256
    || compiled.basecamp?.sandboxTestOutput !== raw.sandboxTestOutput
    || compiled.basecamp?.runtimeOutput !== basecampOutput.name
    || compiled.basecamp?.narHash !== basecampOutput.narHash
    || compiled.basecamp?.narSize !== basecampOutput.narSize
    || compiled.basecamp?.sandboxTestNarHash
      !== raw.sandboxTestNarHash
    || compiled.basecamp?.sandboxTestNarSize
      !== raw.sandboxTestNarSize
  ) {
    throw new Error("sandbox test evidence is incomplete");
  }
  return {
    status: "passed",
    check: "sandbox-test",
    reportSha256: reports.gate0.sha256,
    basecampRevision: basecamp.revision,
    basecampRuntimeOutput: basecampOutput.name,
    basecampNarHash: basecampOutput.narHash,
    basecampNarSize: basecampOutput.narSize,
    sandboxNarHash: raw.sandboxTestNarHash,
    sandboxNarSize: raw.sandboxTestNarSize,
  };
}

function releaseEvidence(compiled, gate3, gate4) {
  const preflight = gate3.releasePreflight;
  const deployment = preflight?.programDeployment;
  const release = preflight?.release;
  if (
    preflight?.schema !== "logos.palace.release-preflight"
    || preflight.version !== 1
    || preflight.status !== "passed"
    || !isObject(release)
    || !isObject(deployment)
    || !positiveInteger(release.programByteLength, "release byte length")
    || !isSha256(release.programBytecodeSha256)
    || !isHex(release.programIdHex, 64)
    || !isHex(release.rootAccountIdHex, 64)
    || !isBase58(release.rootAccountIdBase58)
    || release.programByteLength !== palaceRelease.programByteLength
    || release.programBytecodeSha256
      !== palaceRelease.programBytecodeSha256
    || release.programIdHex !== palaceRelease.programIdHex
    || release.deploymentTransactionHash
      !== palaceRelease.deploymentTransactionHash
    || release.rootAccountIdHex !== palaceRelease.rootAccountIdHex
    || release.rootAccountIdBase58
      !== palaceRelease.rootAccountIdBase58
    || deployment.status !== "passed"
    || deployment.bedrockStatus !== "Finalized"
    || deployment.explorerOrigin !== palaceRelease.explorerOrigin
    || deployment.blockId !== palaceRelease.deploymentBlockId
    || deployment.blockHash !== palaceRelease.deploymentBlockHash
    || deployment.transactionHash
      !== palaceRelease.deploymentTransactionHash
    || deployment.byteLength !== release.programByteLength
    || deployment.bytecodeSha256 !== release.programBytecodeSha256
    || deployment.sha256 !== release.programBytecodeSha256
    || deployment.risc0ImageIdHex !== release.programIdHex
    || deployment.programIdHex !== release.programIdHex
    || !exactJson(gate4.release?.gate3Preflight, preflight)
    || !exactJson(gate4.release?.programDeployment, deployment)
    || !exactKeys(compiled.releaseVerifier, ["narHash", "narSize"])
    || !isNarHash(compiled.releaseVerifier.narHash)
    || !Number.isSafeInteger(compiled.releaseVerifier.narSize)
    || compiled.releaseVerifier.narSize <= 0
  ) {
    throw new Error("release evidence is incomplete or inconsistent");
  }
  return {
    byteLength: release.programByteLength,
    bytecodeSha256: release.programBytecodeSha256,
    computedImageIdHex: deployment.risc0ImageIdHex,
    deploymentTransactionHash: deployment.transactionHash,
    deploymentBlockId: deployment.blockId,
    deploymentBlockHash: deployment.blockHash,
    bedrockStatus: deployment.bedrockStatus,
    rootIdHex: release.rootAccountIdHex,
    rootIdBase58: release.rootAccountIdBase58,
    verifierNarHash: compiled.releaseVerifier.narHash,
    verifierNarSize: compiled.releaseVerifier.narSize,
  };
}

function latencySummary(value, description) {
  if (
    !exactKeys(value, ["sampleCount", "p50Ms", "p95Ms", "maxMs"])
    || !Number.isSafeInteger(value.sampleCount)
    || value.sampleCount <= 0
  ) {
    throw new Error(`${description} latency summary is invalid`);
  }
  const result = {
    sampleCount: value.sampleCount,
    p50Ms: nonnegativeNumber(value.p50Ms, `${description} p50`),
    p95Ms: nonnegativeNumber(value.p95Ms, `${description} p95`),
    maxMs: nonnegativeNumber(value.maxMs, `${description} max`),
  };
  if (result.p50Ms > result.p95Ms || result.p95Ms > result.maxMs) {
    throw new Error(`${description} latency percentiles are unordered`);
  }
  return result;
}

function renderMetrics(metrics, gate1, gate2) {
  const source = metrics?.render;
  if (
    !exactJson(source?.gate1QmlReadyMs, gate1.timings)
    || source?.markedActionToFramebufferCapture?.localMs
      !== gate2.renderProbe?.local?.actionToFramebufferCaptureMs
    || source?.markedActionToFramebufferCapture?.remoteMs?.b
      !== gate2.renderProbe?.remote?.b?.actionToFramebufferCaptureMs
    || source?.markedActionToFramebufferCapture?.remoteMs?.c
      !== gate2.renderProbe?.remote?.c?.actionToFramebufferCaptureMs
  ) {
    throw new Error("compiled render metrics differ from raw evidence");
  }
  return {
    qmlReadyMs: {
      initial:
        nonnegativeNumber(gate1.timings?.initialRenderMs, "initial render"),
      roomTransition:
        nonnegativeNumber(
          gate1.timings?.roomTransitionMs,
          "room transition",
        ),
      restart:
        nonnegativeNumber(gate1.timings?.restartRenderMs, "restart render"),
    },
    actionToFramebufferMs: {
      sender: nonnegativeNumber(
        source.markedActionToFramebufferCapture.localMs,
        "sender framebuffer",
      ),
      receiverB: nonnegativeNumber(
        source.markedActionToFramebufferCapture.remoteMs.b,
        "receiver B framebuffer",
      ),
      receiverC: nonnegativeNumber(
        source.markedActionToFramebufferCapture.remoteMs.c,
        "receiver C framebuffer",
      ),
    },
  };
}

function deliveryMetrics(metrics, gate2) {
  const source = metrics?.delivery;
  if (
    source?.orderedMessages?.total !== gate2.orderedSpeech?.count
    || !exactJson(
      source?.orderedMessages?.perSender,
      gate2.orderedSpeech?.perSenderCount,
    )
    || !exactJson(
      source?.sendToReceive,
      gate2.orderedSpeech?.sendToReceiveLatency?.allNodes,
    )
    || source?.restartRecoveryMs !== gate2.timings?.restartRecoveryMs
  ) {
    throw new Error("compiled Delivery metrics differ from raw evidence");
  }
  const perSender = source.orderedMessages.perSender;
  if (
    !exactKeys(perSender, ["a", "b", "c"])
    || !Number.isSafeInteger(source.orderedMessages.total)
    || source.orderedMessages.total < 300
    || ["a", "b", "c"].some(
      (label) =>
        !Number.isSafeInteger(perSender[label])
        || perSender[label] < 100,
    )
    || perSender.a + perSender.b + perSender.c
      !== source.orderedMessages.total
  ) {
    throw new Error("Delivery message-count evidence is invalid");
  }
  return {
    orderedMessageCount: source.orderedMessages.total,
    perSender: {
      a: perSender.a,
      b: perSender.b,
      c: perSender.c,
    },
    sendToReceiveMs:
      latencySummary(source.sendToReceive, "Delivery send-to-receive"),
    restartRecoveryMs:
      nonnegativeNumber(source.restartRecoveryMs, "Delivery restart"),
  };
}

function lezMetrics(metrics, gate4) {
  const source = metrics?.lez?.actions;
  const measurementBoundaries = metrics?.lez?.measurementBoundaries;
  if (
    !Array.isArray(source)
    || !exactJson(source, gate4.metrics?.lezActions?.map(
      ({ actionId, timings, timingMeasurement }) => ({
        actionId,
        timings,
        timingMeasurement,
      }),
    ))
    || source.length !== 11
    || !exactJson(measurementBoundaries, lezMeasurementBoundaries)
    || !exactJson(
      measurementBoundaries,
      gate4.metrics?.lezMeasurementBoundaries,
    )
    || source.some(
      (entry, index) =>
        entry?.actionId !== String(index)
        || !isObject(entry.timings)
        || !isObject(entry.timingMeasurement),
    )
  ) {
    throw new Error("compiled LEZ metrics differ from raw evidence");
  }
  if (
    !Array.isArray(gate4.actions)
    || gate4.actions.length !== source.length
    || source.some((entry, index) => {
      const action = gate4.actions[index];
      if (
        action?.actionId !== entry.actionId
        || !exactJson(action.timings, entry.timings)
        || !exactJson(
          action.timingMeasurement,
          entry.timingMeasurement,
        )
      ) {
        return true;
      }
      const timingRecord = {
        timings: { ...action.timings },
        timingMeasurement: { ...action.timingMeasurement },
        timingBoundaries: { ...(action.timingBoundaries ?? {}) },
      };
      return recoverPersistedTimingEvidence(
        timingRecord,
        "finalized",
      ).length > 0;
    })
  ) {
    throw new Error("raw LEZ timing boundaries are not exact");
  }
  const fields = [
    ["submitMs", "submitMeasurement"],
    ["observeMs", "observeMeasurement"],
    ["finalityMs", "finalityMeasurement"],
    ["totalMs", "totalMeasurement"],
  ];
  return {
    actionCount: source.length,
    measurementBoundaries: { ...measurementBoundaries },
    actions: source.map((entry) => {
      const result = { actionId: entry.actionId };
      for (const [inputName, outputName] of fields) {
        const measurement = entry.timingMeasurement[inputName];
        const duration = nonnegativeNumber(
          entry.timings[inputName],
          `LEZ ${entry.actionId} ${inputName}`,
        );
        const complete = measurement === "measured"
          || (
            inputName === "finalityMs"
            && measurement === "measured-coalesced"
            && duration === 0
          );
        if (!complete) {
          throw new Error(`LEZ ${entry.actionId} ${inputName} status invalid`);
        }
        result[inputName] = duration;
        result[outputName] = measurement;
      }
      return result;
    }),
  };
}

function modeTiming(value, expectedMode, description, retention = false) {
  const output = {
    mode: value?.mode,
    endToEndMs:
      nonnegativeNumber(value?.endToEndMs, `${description} end-to-end`),
  };
  if (output.mode !== expectedMode) {
    throw new Error(`${description} mode is not ${expectedMode}`);
  }
  if (retention) {
    output.retentionMs =
      nonnegativeNumber(value?.retentionMs, `${description} retention`);
  }
  return output;
}

function storageMetrics(metrics, gate4) {
  const source = metrics?.storage;
  if (!exactJson(source, gate4.metrics?.storage)) {
    throw new Error("compiled Storage metrics differ from raw evidence");
  }
  return {
    firstNetwork: {
      providerB: modeTiming(
        source?.gate3FirstNetwork?.providerB,
        "network",
        "provider B first fetch",
      ),
      coldC: modeTiming(
        source?.gate3FirstNetwork?.coldC,
        "network",
        "cold C first fetch",
      ),
    },
    cached: {
      providerB: modeTiming(
        source?.gate3Cached?.providerB,
        "cache",
        "provider B cached fetch",
      ),
      coldC: modeTiming(
        source?.gate3Cached?.coldC,
        "cache",
        "cold C cached fetch",
      ),
    },
    initialCache: Object.fromEntries(
      ["a", "b", "c"].map((label) => [
        label,
        modeTiming(
          source?.gate4InitialCacheValidation?.[label],
          "cache",
          `initial cache ${label}`,
          true,
        ),
      ]),
    ),
    restart: {
      b: modeTiming(
        source?.restart?.b,
        "network",
        "restart B network recovery",
        true,
      ),
      c: modeTiming(
        source?.restart?.c,
        "cache",
        "restart C retained cache",
        true,
      ),
    },
  };
}

function applicationMetrics(metrics, gate4) {
  const source = metrics?.applicationRoundTrip;
  const raw = gate4.metrics?.applicationRoundTrip;
  if (
    source?.clock !== raw?.clock
    || source?.payloadSemantics !== raw?.payloadSemantics
    || !isObject(source?.measurements)
    || !exactJson(
      source.measurements,
      Object.fromEntries(
        Object.entries(raw?.measurements ?? {}).map(([key, value]) => [
          key,
          {
            payloadUtf8Bytes: value.payloadUtf8Bytes,
            requestUtf8Bytes: value.requestUtf8Bytes,
            responseUtf8Bytes: value.responseUtf8Bytes,
            latency: value.latency,
          },
        ]),
      ),
    )
    || JSON.stringify(Object.keys(source.measurements).sort())
      !== JSON.stringify(["0", "256", "4096"])
  ) {
    throw new Error("application round-trip metrics differ from raw evidence");
  }
  return {
    measurements: ["0", "256", "4096"].map((key) => {
      const value = source.measurements[key];
      const bytes = Number(key);
      if (
        value.payloadUtf8Bytes !== bytes
        || value.requestUtf8Bytes !== bytes
        || value.responseUtf8Bytes !== bytes
      ) {
        throw new Error(`application ${key}-byte measurement is invalid`);
      }
      return {
        bytes,
        latencyMs:
          latencySummary(value.latency, `application ${key}-byte`),
      };
    }),
  };
}

function vmMetric(value, phase, description) {
  if (
    value?.actionId !== "10"
    || value.phase !== phase
    || value.clock !== "steady_clock"
    || typeof value.durationNs !== "string"
    || value.durationNs.length > 16
    || !/^[0-9]+$/.test(value.durationNs)
    || !isSha256(value.receiptSha256)
  ) {
    throw new Error(`${description} VM metric is invalid`);
  }
  const durationMs = nonnegativeNumber(
    value.durationMs,
    `${description} VM duration`,
  );
  if (Number(BigInt(value.durationNs)) / 1_000_000 !== durationMs) {
    throw new Error(`${description} VM nanoseconds differ`);
  }
  return durationMs;
}

function gate5VmMetrics(metrics, gate4) {
  const source = metrics?.gate5Vm;
  if (!exactJson(source, gate4.metrics?.gate5Vm)) {
    throw new Error("compiled Gate 5 VM metrics differ from raw evidence");
  }
  return {
    previewRoundTripMs: {
      a: nonnegativeNumber(
        source?.previewRoundTripMs?.a,
        "Gate 5 preview A",
      ),
      b: nonnegativeNumber(
        source?.previewRoundTripMs?.b,
        "Gate 5 preview B",
      ),
    },
    executeTurnMs: {
      provisionalA:
        vmMetric(
          source?.executeTurn?.provisional?.a,
          "provisional",
          "provisional A",
        ),
      provisionalB:
        vmMetric(
          source?.executeTurn?.provisional?.b,
          "provisional",
          "provisional B",
        ),
      finalized:
        vmMetric(
          source?.executeTurn?.finalized,
          "finalized",
          "finalized",
        ),
    },
    endToEndMs:
      nonnegativeNumber(source?.endToEndMs, "Gate 5 end-to-end"),
  };
}

function recoveryMetrics(metrics, gate4) {
  const source = metrics?.recovery;
  if (!exactJson(source, gate4.metrics?.recovery)) {
    throw new Error("compiled recovery metrics differ from raw evidence");
  }
  return {
    lezProjectionRebuildMs: nonnegativeNumber(
      source?.lezProjectionRebuildMs,
      "LEZ projection rebuild",
    ),
    deliveryReconnectMs: nonnegativeNumber(
      source?.deliveryReconnectMs,
      "Delivery reconnect",
    ),
    fullRebuildMs:
      nonnegativeNumber(source?.fullRebuildMs, "full rebuild"),
    coldHistoryStorageVmRebuildMs: nonnegativeNumber(
      source?.coldHistoryStorageVmRebuildMs,
      "cold history, Storage, and VM rebuild",
    ),
  };
}

function qsgSummary(value, description) {
  if (
    !exactKeys(value, ["p50", "p95", "max"])
    || !Number.isSafeInteger(value.p50)
    || !Number.isSafeInteger(value.p95)
    || !Number.isSafeInteger(value.max)
    || value.p50 < 0
    || value.p50 > value.p95
    || value.p95 > value.max
  ) {
    throw new Error(`${description} QSG summary is invalid`);
  }
  return { p50: value.p50, p95: value.p95, max: value.max };
}

function frameMetrics(metrics, gate4) {
  const source = metrics?.frameTiming;
  if (
    !exactJson(source, gate4.metrics?.frameTiming)
    || !isObject(source?.runs)
    || JSON.stringify(Object.keys(source.runs).sort())
      !== JSON.stringify([...qsgRunNames].sort())
  ) {
    throw new Error("compiled frame metrics differ from raw evidence");
  }
  return {
    runs: qsgRunNames.map((name) => {
      const run = source.runs[name];
      if (
        !Number.isSafeInteger(run?.sampleCount)
        || run.sampleCount <= 0
        || !exactKeys(run.summaries, qsgSummaryFields)
      ) {
        throw new Error(`${name} QSG evidence is invalid`);
      }
      return {
        name,
        sampleCount: run.sampleCount,
        summaries: Object.fromEntries(
          qsgSummaryFields.map((field) => [
            field,
            qsgSummary(run.summaries[field], `${name} ${field}`),
          ]),
        ),
      };
    }),
  };
}

function memoryMetrics(metrics, gate4) {
  const source = metrics?.palaceVmPeakMemoryKiB;
  if (
    source?.b !== gate4.metrics?.processMemory?.b?.palaceVmHost?.vmHwmKiB
    || source?.c !== gate4.metrics?.processMemory?.c?.palaceVmHost?.vmHwmKiB
  ) {
    throw new Error("compiled memory metrics differ from raw evidence");
  }
  return {
    b: positiveInteger(source.b, "Palace VM B peak memory"),
    c: positiveInteger(source.c, "Palace VM C peak memory"),
  };
}

function metricEvidence(compiled, reports) {
  const metrics = compiled.metrics;
  if (!isObject(metrics)) throw new Error("compiled metrics are missing");
  return {
    render: renderMetrics(
      metrics,
      reports.gate1.value,
      reports.gate2.value,
    ),
    delivery: deliveryMetrics(metrics, reports.gate2.value),
    lez: lezMetrics(metrics, reports.gate4To6.value),
    storage: storageMetrics(metrics, reports.gate4To6.value),
    applicationRoundTrip:
      applicationMetrics(metrics, reports.gate4To6.value),
    gate5Vm: gate5VmMetrics(metrics, reports.gate4To6.value),
    recovery: recoveryMetrics(metrics, reports.gate4To6.value),
    frameTiming: frameMetrics(metrics, reports.gate4To6.value),
    palaceVmPeakMemoryKiB:
      memoryMetrics(metrics, reports.gate4To6.value),
  };
}

async function publicScreenshots(gate4, gate4Dir) {
  if (
    !Array.isArray(gate4.screenshots)
    || gate4.screenshots.length !== screenshotSpecs.length
  ) {
    throw new Error("screenshot evidence set is not exact");
  }
  const byFile = new Map(
    gate4.screenshots.map((entry) => [entry?.file, entry]),
  );
  if (byFile.size !== screenshotSpecs.length) {
    throw new Error("screenshot filenames are not unique");
  }
  return Promise.all(screenshotSpecs.map(async (spec) => {
    const entry = byFile.get(spec.file);
    if (
      entry?.stage !== spec.stage
      || entry.state !== spec.state
      || entry.label !== spec.label
      || entry.width !== 1600
      || entry.height !== 900
      || !positiveInteger(entry.byteLength, `${spec.file} byte length`)
      || !isSha256(entry.sha256)
    ) {
      throw new Error(`${spec.file} screenshot metadata is invalid`);
    }
    const bytes = await boundedFile(
      join(gate4Dir, spec.file),
      64 * 1024 * 1024,
      `${spec.file} screenshot`,
    );
    if (bytes.length !== entry.byteLength || sha256(bytes) !== entry.sha256) {
      throw new Error(`${spec.file} screenshot bytes changed`);
    }
    return {
      file: spec.file,
      stage: spec.stage,
      state: spec.state,
      width: 1600,
      height: 900,
      byteLength: entry.byteLength,
      sha256: entry.sha256,
    };
  }));
}

function keyWords(name) {
  return name
    .replace(/([a-z0-9])([A-Z])/g, "$1 $2")
    .toLowerCase()
    .split(/[^a-z0-9]+/)
    .filter(Boolean);
}

function assertNoSensitiveData(value, path = "$") {
  const forbiddenWords = new Set([
    "password",
    "passwd",
    "passphrase",
    "secret",
    "credential",
    "credentials",
    "key",
    "keys",
    "seed",
    "token",
    "tokens",
    "peer",
    "peers",
    "account",
    "accounts",
    "identity",
    "identities",
    "receipt",
    "receipts",
    "config",
    "configs",
    "configuration",
    "port",
    "ports",
  ]);
  if (typeof value === "string") {
    if (
      coldReplayContractTests.includes(value)
      || acceptedSubmissionCrashRecoveryContractTests.includes(value)
      || palaceVmFinalityContractTests.includes(value)
    ) {
      return;
    }
    const words = keyWords(value);
    if (
      value.startsWith("/")
      || /^[A-Za-z]:[\\/]/.test(value)
      || value.includes("/nix/store/")
      || value.includes("/tmp/")
      || value.includes("\\tmp\\")
      || words.includes("workspace")
      || words.includes("personal")
      || /(?:local|internal)[-_ ]only/i.test(value)
      || words.some((word) => forbiddenWords.has(word))
    ) {
      throw new Error(`${path} contains non-public data`);
    }
    return;
  }
  if (Array.isArray(value)) {
    value.forEach((entry, index) =>
      assertNoSensitiveData(entry, `${path}[${index}]`));
    return;
  }
  if (isObject(value)) {
    for (const [key, entry] of Object.entries(value)) {
      if (keyWords(key).some((word) => forbiddenWords.has(word))) {
        throw new Error(`${path}.${key} is not a public field`);
      }
      assertNoSensitiveData(entry, `${path}.${key}`);
    }
  }
}

function assertPublicMetricShape(metrics) {
  if (
    !exactKeys(
      metrics,
      [
        "render",
        "delivery",
        "lez",
        "storage",
        "applicationRoundTrip",
        "gate5Vm",
        "recovery",
        "frameTiming",
        "palaceVmPeakMemoryKiB",
      ],
    )
    || !exactKeys(metrics.render, ["qmlReadyMs", "actionToFramebufferMs"])
    || !exactKeys(
      metrics.render.qmlReadyMs,
      ["initial", "roomTransition", "restart"],
    )
    || !exactKeys(
      metrics.render.actionToFramebufferMs,
      ["sender", "receiverB", "receiverC"],
    )
    || !exactKeys(
      metrics.delivery,
      [
        "orderedMessageCount",
        "perSender",
        "sendToReceiveMs",
        "restartRecoveryMs",
      ],
    )
    || !exactKeys(metrics.delivery.perSender, ["a", "b", "c"])
    || !exactKeys(
      metrics.delivery.sendToReceiveMs,
      ["sampleCount", "p50Ms", "p95Ms", "maxMs"],
    )
    || !exactKeys(
      metrics.lez,
      ["actionCount", "measurementBoundaries", "actions"],
    )
    || !exactJson(
      metrics.lez.measurementBoundaries,
      lezMeasurementBoundaries,
    )
    || !exactKeys(
      metrics.storage,
      ["firstNetwork", "cached", "initialCache", "restart"],
    )
    || !exactKeys(metrics.storage.firstNetwork, ["providerB", "coldC"])
    || !exactKeys(metrics.storage.cached, ["providerB", "coldC"])
    || !exactKeys(metrics.storage.initialCache, ["a", "b", "c"])
    || !exactKeys(metrics.storage.restart, ["b", "c"])
    || !exactKeys(metrics.applicationRoundTrip, ["measurements"])
    || !exactKeys(
      metrics.gate5Vm,
      ["previewRoundTripMs", "executeTurnMs", "endToEndMs"],
    )
    || !exactKeys(metrics.gate5Vm.previewRoundTripMs, ["a", "b"])
    || !exactKeys(
      metrics.gate5Vm.executeTurnMs,
      ["provisionalA", "provisionalB", "finalized"],
    )
    || !exactKeys(
      metrics.recovery,
      [
        "lezProjectionRebuildMs",
        "deliveryReconnectMs",
        "fullRebuildMs",
        "coldHistoryStorageVmRebuildMs",
      ],
    )
    || !exactKeys(metrics.frameTiming, ["runs"])
    || !exactKeys(metrics.palaceVmPeakMemoryKiB, ["b", "c"])
  ) {
    throw new Error("public metric keys are not exact");
  }
  for (const value of [
    ...Object.values(metrics.render.qmlReadyMs),
    ...Object.values(metrics.render.actionToFramebufferMs),
    metrics.delivery.restartRecoveryMs,
    ...Object.values(metrics.gate5Vm.previewRoundTripMs),
    ...Object.values(metrics.gate5Vm.executeTurnMs),
    metrics.gate5Vm.endToEndMs,
    ...Object.values(metrics.recovery),
  ]) {
    nonnegativeNumber(value, "public metric");
  }
  if (
    !Number.isSafeInteger(metrics.delivery.orderedMessageCount)
    || metrics.delivery.orderedMessageCount < 300
    || Object.values(metrics.delivery.perSender).some(
      (value) => !Number.isSafeInteger(value) || value < 100,
    )
  ) {
    throw new Error("public Delivery count is invalid");
  }
  latencySummary(
    metrics.delivery.sendToReceiveMs,
    "public Delivery",
  );
  const lezActionKeys = [
    "actionId",
    "submitMs",
    "submitMeasurement",
    "observeMs",
    "observeMeasurement",
    "finalityMs",
    "finalityMeasurement",
    "totalMs",
    "totalMeasurement",
  ];
  if (
    metrics.lez.actionCount !== 11
    || !Array.isArray(metrics.lez.actions)
    || metrics.lez.actions.length !== 11
  ) {
    throw new Error("public LEZ action set is invalid");
  }
  for (const [index, action] of metrics.lez.actions.entries()) {
    if (
      !exactKeys(action, lezActionKeys)
      || action.actionId !== String(index)
    ) {
      throw new Error("public LEZ action keys are invalid");
    }
    for (const [duration, measurement] of [
      ["submitMs", "submitMeasurement"],
      ["observeMs", "observeMeasurement"],
      ["finalityMs", "finalityMeasurement"],
      ["totalMs", "totalMeasurement"],
    ]) {
      if (
        !(
          action[measurement] === "measured"
          || (
            duration === "finalityMs"
            && action[measurement] === "measured-coalesced"
            && action[duration] === 0
          )
        )
        || !Number.isSafeInteger(action[duration])
        || action[duration] < 0
      ) {
        throw new Error("public LEZ measurement status is invalid");
      }
    }
  }
  const validatePublicMode = (
    value,
    expectedMode,
    includeRetention,
  ) => {
    const keys = includeRetention
      ? ["mode", "endToEndMs", "retentionMs"]
      : ["mode", "endToEndMs"];
    if (!exactKeys(value, keys) || value.mode !== expectedMode) {
      throw new Error("public Storage mode evidence is invalid");
    }
    nonnegativeNumber(value.endToEndMs, "public Storage duration");
    if (includeRetention) {
      nonnegativeNumber(value.retentionMs, "public Storage retention");
    }
  };
  for (const value of Object.values(metrics.storage.firstNetwork)) {
    validatePublicMode(value, "network", false);
  }
  for (const value of Object.values(metrics.storage.cached)) {
    validatePublicMode(value, "cache", false);
  }
  for (const value of Object.values(metrics.storage.initialCache)) {
    validatePublicMode(value, "cache", true);
  }
  validatePublicMode(metrics.storage.restart.b, "network", true);
  validatePublicMode(metrics.storage.restart.c, "cache", true);
  if (
    !Array.isArray(metrics.applicationRoundTrip.measurements)
    || metrics.applicationRoundTrip.measurements.length !== 3
  ) {
    throw new Error("public application measurements are invalid");
  }
  for (const [index, measurement] of
    metrics.applicationRoundTrip.measurements.entries()) {
    if (
      !exactKeys(measurement, ["bytes", "latencyMs"])
      || measurement.bytes !== [0, 256, 4096][index]
    ) {
      throw new Error("public application measurement keys are invalid");
    }
    latencySummary(
      measurement.latencyMs,
      "public application round-trip",
    );
  }
  if (
    !Array.isArray(metrics.frameTiming.runs)
    || metrics.frameTiming.runs.length !== qsgRunNames.length
  ) {
    throw new Error("public frame run set is invalid");
  }
  for (const [index, run] of metrics.frameTiming.runs.entries()) {
    if (
      !exactKeys(run, ["name", "sampleCount", "summaries"])
      || run.name !== qsgRunNames[index]
      || !Number.isSafeInteger(run.sampleCount)
      || run.sampleCount <= 0
      || !exactKeys(run.summaries, qsgSummaryFields)
    ) {
      throw new Error("public frame run is invalid");
    }
    for (const field of qsgSummaryFields) {
      qsgSummary(run.summaries[field], `public frame ${field}`);
    }
  }
  for (const value of Object.values(metrics.palaceVmPeakMemoryKiB)) {
    positiveInteger(value, "public Palace VM peak memory");
  }
}

export function validatePublicEvidence(evidence) {
  if (
    !exactKeys(
      evidence,
      [
        "schema",
        "version",
        "terminalCompletion",
        "candidate",
        "components",
        "sourceSnapshot",
        "runtime",
        "lgxPackages",
        "testOnlyVariants",
        "dependencies",
        "basecamp",
        "sandboxTest",
        "release",
        "protocols",
        "network",
        "rawReports",
        "gates",
        "processProof",
        "recoveryEvidence",
        "metrics",
        "screenshots",
      ],
    )
    || evidence.schema !== "logos.palace.public-evidence"
    || evidence.version !== 1
    || !exactKeys(
      evidence.terminalCompletion,
      [
        "status",
        "completedAtUnixMs",
        "activeClaimSha256",
        "compiledReportSha256",
      ],
    )
    || evidence.terminalCompletion.status !== "completed"
    || !Number.isSafeInteger(
      evidence.terminalCompletion.completedAtUnixMs,
    )
    || evidence.terminalCompletion.completedAtUnixMs <= 0
    || !isSha256(evidence.terminalCompletion.activeClaimSha256)
    || !isSha256(evidence.terminalCompletion.compiledReportSha256)
    || !exactKeys(evidence.candidate, ["commit"])
    || !isHex(evidence.candidate.commit, 40)
    || !exactKeys(
      evidence.components,
      ["uiCommit", "coreCommit", "vmCommit"],
    )
    || Object.values(evidence.components).some(
      (commit) =>
        !isHex(commit, 40)
        || commit !== evidence.candidate.commit,
    )
    || !exactKeys(
      evidence.sourceSnapshot,
      ["narHash", "narSize", "runnerSha256"],
    )
    || !isNarHash(evidence.sourceSnapshot.narHash)
    || !Number.isSafeInteger(evidence.sourceSnapshot.narSize)
    || evidence.sourceSnapshot.narSize <= 0
    || evidence.sourceSnapshot.narSize > 64 * 1024 * 1024
    || !isSha256(evidence.sourceSnapshot.runnerSha256)
    || !exactKeys(evidence.runtime, ["manifestSha256", "outputs"])
    || !isSha256(evidence.runtime.manifestSha256)
    || !Array.isArray(evidence.runtime.outputs)
    || evidence.runtime.outputs.length !== runtimeOutputNames.length
    || evidence.runtime.outputs.some(
      (output, index) =>
        !exactKeys(output, ["name", "narHash", "narSize"])
        || output.name !== runtimeOutputNames[index]
        || !isNarHash(output.narHash)
        || !Number.isSafeInteger(output.narSize)
        || output.narSize <= 0,
    )
    || !Array.isArray(evidence.lgxPackages)
    || evidence.lgxPackages.length !== lgxPackageNames.length
    || evidence.lgxPackages.some(
      (entry, index) =>
        !exactKeys(entry, ["file", "sha256"])
        || entry.file !== lgxPackageNames[index]
        || !isSha256(entry.sha256),
    )
    || !exactKeys(evidence.testOnlyVariants, ["gate2PalaceCore"])
    || !exactKeys(
      evidence.testOnlyVariants.gate2PalaceCore,
      [
        "kind",
        "file",
        "runtimeOutput",
        "productionSha256",
        "installedSha256",
      ],
    )
    || evidence.testOnlyVariants.gate2PalaceCore.kind
      !== "test-only-acceptance-fixtures"
    || evidence.testOnlyVariants.gate2PalaceCore.file
      !== "logos-palace_core-module-lib.lgx"
    || evidence.testOnlyVariants.gate2PalaceCore.runtimeOutput
      !== "palace-core-acceptance-lgx"
    || !evidence.runtime.outputs.some(
      ({ name }) =>
        name === evidence.testOnlyVariants.gate2PalaceCore.runtimeOutput,
    )
    || !isSha256(
      evidence.testOnlyVariants.gate2PalaceCore.productionSha256,
    )
    || !isSha256(
      evidence.testOnlyVariants.gate2PalaceCore.installedSha256,
    )
    || evidence.testOnlyVariants.gate2PalaceCore.productionSha256
      === evidence.testOnlyVariants.gate2PalaceCore.installedSha256
    || evidence.testOnlyVariants.gate2PalaceCore.productionSha256
      !== evidence.lgxPackages.find(
        ({ file }) => file
          === evidence.testOnlyVariants.gate2PalaceCore.file,
      )?.sha256
    || !exactKeys(
      evidence.dependencies,
      dependencySpecs.map(([, outputName]) => outputName),
    )
    || Object.values(evidence.dependencies).some(
      (pin) =>
        !exactKeys(pin, ["revision", "narHash"])
        || !isHex(pin.revision, 40)
        || !isNarHash(pin.narHash),
    )
    || !exactKeys(evidence.basecamp, ["revision", "binarySha256"])
    || !isHex(evidence.basecamp.revision, 40)
    || !isSha256(evidence.basecamp.binarySha256)
    || evidence.basecamp.revision
      !== evidence.dependencies.basecamp.revision
    || !exactKeys(
      evidence.sandboxTest,
      [
        "status",
        "check",
        "reportSha256",
        "basecampRevision",
        "basecampRuntimeOutput",
        "basecampNarHash",
        "basecampNarSize",
        "sandboxNarHash",
        "sandboxNarSize",
      ],
    )
    || evidence.sandboxTest.status !== "passed"
    || evidence.sandboxTest.check !== "sandbox-test"
    || !isSha256(evidence.sandboxTest.reportSha256)
    || evidence.sandboxTest.basecampRevision
      !== evidence.basecamp.revision
    || evidence.sandboxTest.reportSha256
      !== evidence.rawReports?.gate0?.sha256
    || evidence.sandboxTest.basecampRuntimeOutput !== "basecamp"
    || !isNarHash(evidence.sandboxTest.basecampNarHash)
    || !Number.isSafeInteger(evidence.sandboxTest.basecampNarSize)
    || evidence.sandboxTest.basecampNarSize <= 0
    || !isNarHash(evidence.sandboxTest.sandboxNarHash)
    || !Number.isSafeInteger(evidence.sandboxTest.sandboxNarSize)
    || evidence.sandboxTest.sandboxNarSize <= 0
    || !exactKeys(
      evidence.processProof,
      [
        "standalonePalaceServer",
        "exactProcessInventory",
        "exactPinnedRuntimeArtifacts",
        "exactWrapperExecution",
        "exactOpenedExecutableBindings",
        "exactProgramExecutableMappings",
        "exactDirectInterpreterMappings",
        "exactModuleArgumentArtifacts",
        "exactExecutableModuleMappings",
        "exactOwnedTcpListeners",
        "observationCount",
        "inventory",
        "wrapperExecution",
        "moduleArgumentArtifacts",
      ],
    )
    || evidence.processProof.standalonePalaceServer !== false
    || evidence.processProof.exactProcessInventory !== true
    || evidence.processProof.exactPinnedRuntimeArtifacts !== true
    || evidence.processProof.exactWrapperExecution !== true
    || evidence.processProof.exactOpenedExecutableBindings !== true
    || evidence.processProof.exactProgramExecutableMappings !== true
    || evidence.processProof.exactDirectInterpreterMappings !== true
    || evidence.processProof.exactModuleArgumentArtifacts !== true
    || evidence.processProof.exactExecutableModuleMappings !== true
    || evidence.processProof.exactOwnedTcpListeners !== true
    || !Number.isSafeInteger(evidence.processProof.observationCount)
    || evidence.processProof.observationCount <= 0
    || !exactJson(
      evidence.processProof.inventory,
      processInventoryContract,
    )
    || !exactKeys(
      evidence.processProof.wrapperExecution,
      [
        "mode",
        "fallbackIndex",
        "argumentBasename",
        "executableBasename",
        "sha256",
      ],
    )
    || (
      evidence.processProof.wrapperExecution.mode === "direct"
        ? (
          evidence.processProof.wrapperExecution.fallbackIndex !== null
          || evidence.processProof.wrapperExecution
            .argumentBasename !== null
          || typeof evidence.processProof.wrapperExecution
            .executableBasename !== "string"
          || evidence.processProof.wrapperExecution
            .executableBasename.length === 0
          || evidence.processProof.wrapperExecution
            .executableBasename.includes("/")
          || !isSha256(
            evidence.processProof.wrapperExecution.sha256,
          )
        )
        : (
          evidence.processProof.wrapperExecution.mode !== "fallback"
          || !Number.isSafeInteger(
            evidence.processProof.wrapperExecution.fallbackIndex,
          )
          || evidence.processProof.wrapperExecution.fallbackIndex < 0
          || evidence.processProof.wrapperExecution.fallbackIndex
            >= basecampWrapperFallbackLoaderPaths.length
          || basename(basecampWrapperFallbackLoaderPaths[
            evidence.processProof.wrapperExecution.fallbackIndex
          ]) !== evidence.processProof.wrapperExecution.argumentBasename
          || typeof evidence.processProof.wrapperExecution
            .executableBasename !== "string"
          || evidence.processProof.wrapperExecution
            .executableBasename.length === 0
          || evidence.processProof.wrapperExecution
            .executableBasename.includes("/")
          || !isSha256(
            evidence.processProof.wrapperExecution.sha256,
          )
        )
    )
    || !Array.isArray(evidence.processProof.moduleArgumentArtifacts)
    || evidence.processProof.moduleArgumentArtifacts.length !== 9
    || evidence.processProof.moduleArgumentArtifacts.some(
      (artifact, index) => {
        const moduleName = [
          ...processInventoryContract.coreModuleHosts,
          "logos_palace_ui",
        ][index];
        return (
          !exactKeys(
            artifact,
            ["role", "moduleName", "argumentBasename", "sha256"],
          )
          || artifact.role !== (
            moduleName === "logos_palace_ui"
              ? "ui-module-host"
              : "core-module-host"
          )
          || artifact.moduleName !== moduleName
          || artifact.argumentBasename
            !== processModuleArtifactBasenames[moduleName]
          || !isSha256(artifact.sha256)
        );
      },
    )
    || !exactKeys(
      evidence.recoveryEvidence,
      [
        "delayedLezUpdate",
        "pendingAction",
        "missingStorageObject",
        "clientOffline",
        "basecampCrashRestart",
        "coldClientRebuild",
        "acceptedSubmissionCrashRecovery",
        "coldReplay",
        "palaceVmFinality",
      ],
    )
    || [
      "delayedLezUpdate",
      "pendingAction",
      "clientOffline",
    ].some((field) => evidence.recoveryEvidence[field] !== true)
    || !exactKeys(
      evidence.recoveryEvidence.missingStorageObject,
      [
        "passed",
        "objectId",
        "missingSourceCid",
        "sourceAbsentFromCatalog",
        "derivativeCid",
        "expectedContentSha256",
        "states",
        "transitionEvidence",
      ],
    )
    || evidence.recoveryEvidence.missingStorageObject.passed !== true
    || evidence.recoveryEvidence.missingStorageObject.objectId
      !== "background-atrium"
    || evidence.recoveryEvidence.missingStorageObject.missingSourceCid
      !== missingStorageSourceCid
    || evidence.recoveryEvidence.missingStorageObject
      .sourceAbsentFromCatalog !== true
    || !isBase58(
      evidence.recoveryEvidence.missingStorageObject.derivativeCid,
    )
    || evidence.recoveryEvidence.missingStorageObject.derivativeCid
      === evidence.recoveryEvidence.missingStorageObject.missingSourceCid
    || !isSha256(
      evidence.recoveryEvidence.missingStorageObject
        .expectedContentSha256,
    )
    || evidence.recoveryEvidence.missingStorageObject.states?.join(",")
      !== "missing,fetching,degraded"
    || !exactKeys(
      evidence.recoveryEvidence.missingStorageObject.transitionEvidence,
      ["before", "dispatched", "degraded"],
    )
    || evidence.recoveryEvidence.missingStorageObject
      .transitionEvidence.before !== "missing"
    || !/^ok;asset=fetching;operation=palace-asset-(?:0|[1-9][0-9]{0,19})$/.test(
      evidence.recoveryEvidence.missingStorageObject
        .transitionEvidence.dispatched,
    )
    || !/^degraded;reason=storage-download-[a-z0-9][a-z0-9-]{0,127}$/.test(
      evidence.recoveryEvidence.missingStorageObject
        .transitionEvidence.degraded,
    )
    || !exactKeys(
      evidence.recoveryEvidence.basecampCrashRestart,
      [
        "passed",
        "previousPid",
        "replacementPid",
        "signal",
        "exitCode",
        "graceful",
        "processReplaced",
      ],
    )
    || evidence.recoveryEvidence.basecampCrashRestart.passed !== true
    || !Number.isSafeInteger(
      evidence.recoveryEvidence.basecampCrashRestart.previousPid,
    )
    || evidence.recoveryEvidence.basecampCrashRestart.previousPid <= 0
    || !Number.isSafeInteger(
      evidence.recoveryEvidence.basecampCrashRestart.replacementPid,
    )
    || evidence.recoveryEvidence.basecampCrashRestart.replacementPid <= 0
    || evidence.recoveryEvidence.basecampCrashRestart.replacementPid
      === evidence.recoveryEvidence.basecampCrashRestart.previousPid
    || evidence.recoveryEvidence.basecampCrashRestart.signal !== "SIGKILL"
    || evidence.recoveryEvidence.basecampCrashRestart.exitCode !== null
    || evidence.recoveryEvidence.basecampCrashRestart.graceful !== false
    || evidence.recoveryEvidence.basecampCrashRestart
      .processReplaced !== true
    || !exactKeys(
      evidence.recoveryEvidence.coldClientRebuild,
      [
        "passed",
        "label",
        "removed",
        "preserved",
        "authority",
        "deliveryBinding",
        "lezStatePreserved",
        "storageBefore",
        "verifiedAssetCacheBefore",
        "storageRecoveryMode",
        "retainedSource",
        "vmProjection",
        "exactFinalizedProjection",
      ],
    )
    || evidence.recoveryEvidence.coldClientRebuild.passed !== true
    || evidence.recoveryEvidence.coldClientRebuild.label !== "b"
    || !exactJson(
      evidence.recoveryEvidence.coldClientRebuild.removed,
      coldRebuildRemovedState,
    )
    || !exactJson(
      evidence.recoveryEvidence.coldClientRebuild.preserved,
      coldRebuildPublicPreservedState,
    )
    || !exactKeys(
      evidence.recoveryEvidence.coldClientRebuild.authority,
      ["beforeSha256", "afterSha256", "hashEqual"],
    )
    || !isSha256(
      evidence.recoveryEvidence.coldClientRebuild.authority.beforeSha256,
    )
    || evidence.recoveryEvidence.coldClientRebuild.authority.afterSha256
      !== evidence.recoveryEvidence.coldClientRebuild.authority.beforeSha256
    || evidence.recoveryEvidence.coldClientRebuild.authority
      .hashEqual !== true
    || !exactKeys(
      evidence.recoveryEvidence.coldClientRebuild.deliveryBinding,
      ["beforeSha256", "afterSha256", "hashEqual"],
    )
    || !isSha256(
      evidence.recoveryEvidence.coldClientRebuild.deliveryBinding
        .beforeSha256,
    )
    || evidence.recoveryEvidence.coldClientRebuild.deliveryBinding
      .afterSha256
      !== evidence.recoveryEvidence.coldClientRebuild.deliveryBinding
        .beforeSha256
    || evidence.recoveryEvidence.coldClientRebuild.deliveryBinding
      .hashEqual !== true
    || evidence.recoveryEvidence.coldClientRebuild
      .lezStatePreserved !== true
    || ["storageBefore", "verifiedAssetCacheBefore"].some((field) => {
      const fingerprint =
        evidence.recoveryEvidence.coldClientRebuild[field];
      return (
        !exactKeys(
          fingerprint,
          ["fileCount", "directoryCount", "totalBytes", "sha256"],
        )
        || !Number.isSafeInteger(fingerprint.fileCount)
        || fingerprint.fileCount < 0
        || !Number.isSafeInteger(fingerprint.directoryCount)
        || fingerprint.directoryCount < 1
        || !Number.isSafeInteger(fingerprint.totalBytes)
        || fingerprint.totalBytes < 0
        || !isSha256(fingerprint.sha256)
      );
    })
    || evidence.recoveryEvidence.coldClientRebuild
      .storageRecoveryMode !== "network"
    || !exactKeys(
      evidence.recoveryEvidence.coldClientRebuild.retainedSource,
      [
        "coldClientMode",
        "coldClientNativeSource",
        "coldClientDataRootRemoved",
        "coldClientStorageNotStarted",
        "coldClientNativeAvailable",
        "coldClientNativeTotal",
        "retainedHolderLabel",
        "retainedHolderMode",
        "retainedHolderNativeSource",
        "retainedHolderNativeAvailable",
        "retainedHolderNativeTotal",
        "retainedDataRootBeforeRestart",
        "catalogChecksum",
        "catalogVerifiedBeforeColdFetch",
        "catalogVerifiedAfterColdFetch",
        "creatorOffline",
        "onlyRunScopedRetainedParticipantOnline",
        "servingParticipantExposed",
        "attributionBasis",
        "exactProcessProof",
      ],
    )
    || evidence.recoveryEvidence.coldClientRebuild.retainedSource
      .coldClientMode !== "network"
    || evidence.recoveryEvidence.coldClientRebuild.retainedSource
      .coldClientNativeSource !== "network"
    || evidence.recoveryEvidence.coldClientRebuild.retainedSource
      .coldClientDataRootRemoved !== true
    || evidence.recoveryEvidence.coldClientRebuild.retainedSource
      .coldClientStorageNotStarted !== true
    || evidence.recoveryEvidence.coldClientRebuild.retainedSource
      .coldClientNativeAvailable !== 0
    || evidence.recoveryEvidence.coldClientRebuild.retainedSource
      .coldClientNativeTotal !== 11
    || evidence.recoveryEvidence.coldClientRebuild.retainedSource
      .retainedHolderLabel !== "c"
    || evidence.recoveryEvidence.coldClientRebuild.retainedSource
      .retainedHolderMode !== "cache"
    || evidence.recoveryEvidence.coldClientRebuild.retainedSource
      .retainedHolderNativeSource !== "cache"
    || evidence.recoveryEvidence.coldClientRebuild.retainedSource
      .retainedHolderNativeAvailable !== 11
    || evidence.recoveryEvidence.coldClientRebuild.retainedSource
      .retainedHolderNativeTotal !== 11
    || !exactKeys(
      evidence.recoveryEvidence.coldClientRebuild.retainedSource
        .retainedDataRootBeforeRestart,
      ["fileCount", "directoryCount", "totalBytes", "sha256"],
    )
    || !isSha256(
      evidence.recoveryEvidence.coldClientRebuild.retainedSource
        .retainedDataRootBeforeRestart.sha256,
    )
    || !Number.isSafeInteger(
      evidence.recoveryEvidence.coldClientRebuild.retainedSource
        .retainedDataRootBeforeRestart.fileCount,
    )
    || evidence.recoveryEvidence.coldClientRebuild.retainedSource
      .retainedDataRootBeforeRestart.fileCount <= 0
    || !Number.isSafeInteger(
      evidence.recoveryEvidence.coldClientRebuild.retainedSource
        .retainedDataRootBeforeRestart.directoryCount,
    )
    || evidence.recoveryEvidence.coldClientRebuild.retainedSource
      .retainedDataRootBeforeRestart.directoryCount <= 0
    || !Number.isSafeInteger(
      evidence.recoveryEvidence.coldClientRebuild.retainedSource
        .retainedDataRootBeforeRestart.totalBytes,
    )
    || evidence.recoveryEvidence.coldClientRebuild.retainedSource
      .retainedDataRootBeforeRestart.totalBytes <= 0
    || !isSha256(
      evidence.recoveryEvidence.coldClientRebuild.retainedSource
        .catalogChecksum,
    )
    || [
      "catalogVerifiedBeforeColdFetch",
      "catalogVerifiedAfterColdFetch",
      "creatorOffline",
      "onlyRunScopedRetainedParticipantOnline",
      "exactProcessProof",
    ].some(
      (field) =>
        evidence.recoveryEvidence.coldClientRebuild.retainedSource[field]
          !== true,
    )
    || evidence.recoveryEvidence.coldClientRebuild.retainedSource
      .servingParticipantExposed !== false
    || evidence.recoveryEvidence.coldClientRebuild.retainedSource
      .attributionBasis !== "topology-constrained inference"
    || !exactKeys(
      evidence.recoveryEvidence.coldClientRebuild.vmProjection,
      ["phase", "actionId", "navigation", "stateRoot"],
    )
    || evidence.recoveryEvidence.coldClientRebuild.vmProjection
      .phase !== "promoted"
    || evidence.recoveryEvidence.coldClientRebuild.vmProjection
      .actionId !== "10"
    || evidence.recoveryEvidence.coldClientRebuild.vmProjection
      .navigation !== true
    || !isHex(
      evidence.recoveryEvidence.coldClientRebuild.vmProjection.stateRoot,
      64,
    )
    || evidence.recoveryEvidence.coldClientRebuild
      .exactFinalizedProjection !== true
    || !exactKeys(
      evidence.recoveryEvidence.acceptedSubmissionCrashRecovery,
      [
        "passed",
        "contractTests",
        "runtimeOutput",
        "narHash",
        "narSize",
      ],
    )
    || evidence.recoveryEvidence.acceptedSubmissionCrashRecovery
      .passed !== true
    || !exactJson(
      evidence.recoveryEvidence.acceptedSubmissionCrashRecovery
        .contractTests,
      acceptedSubmissionCrashRecoveryContractTests,
    )
    || evidence.recoveryEvidence.acceptedSubmissionCrashRecovery
      .runtimeOutput
      !== "palace-core-contracts"
    || !isNarHash(
      evidence.recoveryEvidence.acceptedSubmissionCrashRecovery.narHash,
    )
    || !Number.isSafeInteger(
      evidence.recoveryEvidence.acceptedSubmissionCrashRecovery.narSize,
    )
    || evidence.recoveryEvidence.acceptedSubmissionCrashRecovery
      .narSize <= 0
    || !exactKeys(
      evidence.recoveryEvidence.coldReplay,
      [
        "passed",
        "contractTests",
        "runtimeOutput",
        "narHash",
        "narSize",
      ],
    )
    || evidence.recoveryEvidence.coldReplay.passed !== true
    || !exactJson(
      evidence.recoveryEvidence.coldReplay.contractTests,
      coldReplayContractTests,
    )
    || evidence.recoveryEvidence.coldReplay.runtimeOutput
      !== "palace-core-contracts"
    || !isNarHash(evidence.recoveryEvidence.coldReplay.narHash)
    || !Number.isSafeInteger(
      evidence.recoveryEvidence.coldReplay.narSize,
    )
    || evidence.recoveryEvidence.coldReplay.narSize <= 0
    || !exactKeys(
      evidence.recoveryEvidence.palaceVmFinality,
      [
        "passed",
        "contractTests",
        "runtimeOutput",
        "narHash",
        "narSize",
      ],
    )
    || evidence.recoveryEvidence.palaceVmFinality.passed !== true
    || !exactJson(
      evidence.recoveryEvidence.palaceVmFinality.contractTests,
      palaceVmFinalityContractTests,
    )
    || evidence.recoveryEvidence.palaceVmFinality.runtimeOutput
      !== "palace-vm-contracts"
    || !isNarHash(evidence.recoveryEvidence.palaceVmFinality.narHash)
    || !Number.isSafeInteger(
      evidence.recoveryEvidence.palaceVmFinality.narSize,
    )
    || evidence.recoveryEvidence.palaceVmFinality.narSize <= 0
    || !exactJson(evidence.protocols, protocolContract)
    || !exactJson(
      evidence.network,
      {
        acceptanceDeliveryNetworkId: "logos.test",
        ...networkContract,
      },
    )
    || !exactKeys(
      evidence.release,
      [
        "byteLength",
        "bytecodeSha256",
        "computedImageIdHex",
        "deploymentTransactionHash",
        "deploymentBlockId",
        "deploymentBlockHash",
        "bedrockStatus",
        "rootIdHex",
        "rootIdBase58",
        "verifierNarHash",
        "verifierNarSize",
      ],
    )
    || !Number.isSafeInteger(evidence.release.byteLength)
    || evidence.release.byteLength !== palaceRelease.programByteLength
    || evidence.release.bytecodeSha256
      !== palaceRelease.programBytecodeSha256
    || evidence.release.computedImageIdHex !== palaceRelease.programIdHex
    || evidence.release.deploymentTransactionHash
      !== palaceRelease.deploymentTransactionHash
    || evidence.release.deploymentBlockId
      !== palaceRelease.deploymentBlockId
    || evidence.release.deploymentBlockHash
      !== palaceRelease.deploymentBlockHash
    || evidence.release.bedrockStatus !== "Finalized"
    || evidence.release.rootIdHex !== palaceRelease.rootAccountIdHex
    || evidence.release.rootIdBase58
      !== palaceRelease.rootAccountIdBase58
    || !isNarHash(evidence.release.verifierNarHash)
    || !Number.isSafeInteger(evidence.release.verifierNarSize)
    || evidence.release.verifierNarSize <= 0
    || !exactKeys(
      evidence.rawReports,
      reportSpecs.map(({ name }) => name),
    )
    || reportSpecs.some(({ name, schema, version }) => {
      const report = evidence.rawReports[name];
      return (
        !exactKeys(report, ["schema", "version", "sha256"])
        || report.schema !== schema
        || report.version !== version
        || !isSha256(report.sha256)
      );
    })
    || !Array.isArray(evidence.gates)
    || evidence.gates.length !== 7
    || evidence.gates.some(
      (gate, index) =>
        !exactKeys(gate, ["gate", "status"])
        || gate.gate !== `gate${index}`
        || gate.status !== "passed",
    )
    || !Array.isArray(evidence.screenshots)
    || evidence.screenshots.length !== screenshotSpecs.length
    || evidence.screenshots.some((entry, index) => {
      const spec = screenshotSpecs[index];
      return (
        !exactKeys(
          entry,
          [
            "file",
            "stage",
            "state",
            "width",
            "height",
            "byteLength",
            "sha256",
          ],
        )
        || entry.file !== spec.file
        || entry.stage !== spec.stage
        || entry.state !== spec.state
        || entry.width !== 1600
        || entry.height !== 900
        || !Number.isSafeInteger(entry.byteLength)
        || entry.byteLength <= 0
        || entry.byteLength > 64 * 1024 * 1024
        || !isSha256(entry.sha256)
      );
    })
  ) {
    throw new Error("public evidence schema is invalid");
  }
  const publicCoreContracts = evidence.runtime.outputs.find(
    ({ name }) => name === "palace-core-contracts",
  );
  const publicVmContracts = evidence.runtime.outputs.find(
    ({ name }) => name === "palace-vm-contracts",
  );
  const publicBasecamp = evidence.runtime.outputs.find(
    ({ name }) => name === "basecamp",
  );
  if (
    !publicCoreContracts
    || !publicVmContracts
    || !publicBasecamp
    || !exactJson(
      {
        name:
          evidence.recoveryEvidence.acceptedSubmissionCrashRecovery
            .runtimeOutput,
        narHash:
          evidence.recoveryEvidence.acceptedSubmissionCrashRecovery
            .narHash,
        narSize:
          evidence.recoveryEvidence.acceptedSubmissionCrashRecovery
            .narSize,
      },
      publicCoreContracts,
    )
    || !exactJson(
      {
        name: evidence.recoveryEvidence.coldReplay.runtimeOutput,
        narHash: evidence.recoveryEvidence.coldReplay.narHash,
        narSize: evidence.recoveryEvidence.coldReplay.narSize,
      },
      publicCoreContracts,
    )
    || !exactJson(
      {
        name: evidence.recoveryEvidence.palaceVmFinality.runtimeOutput,
        narHash: evidence.recoveryEvidence.palaceVmFinality.narHash,
        narSize: evidence.recoveryEvidence.palaceVmFinality.narSize,
      },
      publicVmContracts,
    )
    || !exactJson(
      {
        name: evidence.sandboxTest.basecampRuntimeOutput,
        narHash: evidence.sandboxTest.basecampNarHash,
        narSize: evidence.sandboxTest.basecampNarSize,
      },
      publicBasecamp,
    )
  ) {
    throw new Error("public evidence schema is invalid");
  }
  try {
    assertPublicMetricShape(evidence.metrics);
  } catch {
    throw new Error("public evidence schema is invalid");
  }
  assertNoSensitiveData(evidence);
  return evidence;
}

async function validateScreenshotsFirst(gate4Report, gate4Dir) {
  try {
    await execFileAsync(
      process.execPath,
      [screenshotValidator, gate4Report, gate4Dir],
      {
        timeout: 120_000,
        maxBuffer: 64 * 1024,
        windowsHide: true,
      },
    );
  } catch {
    throw new Error("strict screenshot evidence validation failed");
  }
}

async function durableWrite(path, body) {
  const temporary = join(
    dirname(path),
    `.public-evidence.${process.pid}.${Date.now()}.tmp`,
  );
  let handle;
  let renamed = false;
  try {
    handle = await open(temporary, "wx", 0o600);
    await handle.chmod(0o600);
    await handle.writeFile(body, "utf8");
    await handle.sync();
    await handle.close();
    handle = undefined;
    await rename(temporary, path);
    renamed = true;
    const metadata = await lstat(path);
    if (
      metadata.isSymbolicLink()
      || !metadata.isFile()
      || (metadata.mode & 0o777) !== 0o600
      || (
        typeof process.getuid === "function"
        && metadata.uid !== process.getuid()
      )
      || await realpath(path) !== path
    ) {
      throw new Error("public evidence output mode or owner is unsafe");
    }
    const directory = await open(dirname(path), "r");
    try {
      await directory.sync();
    } finally {
      await directory.close();
    }
  } catch (error) {
    await handle?.close().catch(() => {});
    for (const candidate of renamed ? [path] : [temporary]) {
      await unlink(candidate).catch((unlinkError) => {
        if (unlinkError?.code !== "ENOENT") throw unlinkError;
      });
    }
    throw error;
  }
}

async function invalidateOutput(path) {
  try {
    const metadata = await lstat(path);
    if (metadata.isDirectory()) {
      throw new Error("existing public evidence output is unsafe");
    }
    await unlink(path);
    const directory = await open(dirname(path), "r");
    try {
      await directory.sync();
    } finally {
      await directory.close();
    }
  } catch (error) {
    if (error?.code !== "ENOENT") throw error;
  }
}

export async function buildPublicEvidence(runArgument, outputArgument) {
  if (!runArgument || !outputArgument) {
    throw new Error(
      "usage: node tests/build_public_evidence.mjs "
      + "<run-dir> <run-dir>/public-evidence.json",
    );
  }
  const runDir = await canonicalDirectory(runArgument, "MVP run directory");
  const outputPath = resolve(outputArgument);
  if (
    outputPath !== join(runDir, "public-evidence.json")
    || dirname(outputPath) !== runDir
  ) {
    throw new Error("public evidence output must be the exact run child");
  }
  await invalidateOutput(outputPath);

  const gate4Dir = await canonicalDirectory(
    join(runDir, "gate4"),
    "Gate 4 evidence directory",
  );
  const gate4Report = join(gate4Dir, "gate4-report.json");

  // Screenshot byte validation intentionally precedes JSON projection.
  await validateScreenshotsFirst(gate4Report, gate4Dir);

  const reportEntries = await Promise.all(
    reportSpecs.map(async (spec) => [
      spec.name,
      await readReport(join(runDir, spec.file), `${spec.name} report`),
    ]),
  );
  const reports = Object.fromEntries(reportEntries);
  const compiled = await readReport(
    join(runDir, "compiled-mvp-report.json"),
    "compiled MVP report",
  );
  const completion = await readReport(
    join(runDir, "active-claim-completion.json"),
    "terminal active-run completion",
  );
  const runtimeManifest = await readReport(
    join(runDir, "runtime-output-manifest.json"),
    "runtime output manifest",
  );

  validateReportStatuses(reports);
  const runtime = validateRuntimeManifest(
    compiled.value,
    runtimeManifest,
  );
  validateCompiledBindings(compiled.value, reports);
  const lgxPackages = publicLgxPackages(compiled.value, reports);
  const testOnlyVariants = publicTestOnlyVariants(
    reports.gate2.value,
    lgxPackages,
  );
  const processProof = publicProcessProof(reports.gate4To6.value);
  const recoveryEvidence = publicRecoveryEvidence(
    compiled.value,
    runtime,
    reports.gate2.value,
    reports.gate4To6.value,
  );
  const dependencies = publicDependencies(
    compiled.value,
    reports.gate4To6.value,
  );
  const contracts = publicContracts(reports);
  const basecamp = basecampEvidence(
    compiled.value,
    reports,
    dependencies,
  );
  const sandboxTest = sandboxTestEvidence(
    compiled.value,
    reports,
    basecamp,
    runtime,
  );
  const release = releaseEvidence(
    compiled.value,
    reports.gate3.value,
    reports.gate4To6.value,
  );
  const metrics = metricEvidence(compiled.value, reports);
  const screenshots = await publicScreenshots(
    reports.gate4To6.value,
    gate4Dir,
  );
  const terminalCompletion = publicTerminalCompletion(
    completion,
    compiled,
  );
  const evidence = {
    schema: "logos.palace.public-evidence",
    version: 1,
    terminalCompletion,
    candidate: {
      commit: compiled.value.sourceCommit,
    },
    components: {
      uiCommit: compiled.value.sourceCommit,
      coreCommit: compiled.value.sourceCommit,
      vmCommit: compiled.value.sourceCommit,
    },
    sourceSnapshot: {
      narHash: compiled.value.productSnapshotNarHash,
      narSize: compiled.value.productSnapshotNarSize,
      runnerSha256: compiled.value.snapshotRunnerSha256,
    },
    runtime,
    lgxPackages,
    testOnlyVariants,
    dependencies,
    basecamp,
    sandboxTest,
    release,
    protocols: contracts.protocols,
    network: contracts.network,
    rawReports: Object.fromEntries(
      reportSpecs.map(({ name, schema, version }) => [
        name,
        {
          schema,
          version,
          sha256: reports[name].sha256,
        },
      ]),
    ),
    gates: Array.from({ length: 7 }, (_, index) => ({
      gate: `gate${index}`,
      status: "passed",
    })),
    processProof,
    recoveryEvidence,
    metrics,
    screenshots,
  };
  validatePublicEvidence(evidence);
  const encoded = `${JSON.stringify(evidence, null, 2)}\n`;
  await durableWrite(outputPath, encoded);
  return evidence;
}

const isMain = (
  process.argv[1]
  && pathToFileURL(resolve(process.argv[1])).href === import.meta.url
);
if (isMain) {
  const [runArgument, outputArgument] = process.argv.slice(2);
  await buildPublicEvidence(runArgument, outputArgument);
}
