#!/usr/bin/env node

import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { execFile } from "node:child_process";
import {
  access,
  mkdir,
  mkdtemp,
  readFile,
  rm,
  stat,
  unlink,
  writeFile,
} from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { deflateSync } from "node:zlib";
import test from "node:test";
import { fileURLToPath, pathToFileURL } from "node:url";
import { promisify } from "node:util";
import {
  buildPublicEvidence,
  validatePublicEvidence,
} from "./build_public_evidence.mjs";
import { lezMeasurementBoundaries } from "./basecamp_lez_timing.mjs";
import { palaceRelease } from "./basecamp_release_preflight.mjs";

const execFileAsync = promisify(execFile);
const builderPath = fileURLToPath(
  new URL("./build_public_evidence.mjs", import.meta.url),
);

const candidateCommit = "a".repeat(40);
const productSnapshot =
  `/nix/store/${"0".repeat(32)}-logos-palace-source`;
const productSnapshotNarHash =
  "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
const snapshotRunnerSha256 = "d".repeat(64);
const basecampRevision = "b".repeat(40);
const basecampSha256 = "c".repeat(64);
const releaseBytecodeSha256 = palaceRelease.programBytecodeSha256;
const releaseImageId = palaceRelease.programIdHex;
const releaseRootId = palaceRelease.rootAccountIdHex;
const releaseRootBase58 = palaceRelease.rootAccountIdBase58;
const missingStorageSourceCid =
  "zb2rhWeJ5rsKhbfi31kikfdS3hE2pvWzhW27Es4Jmc863vFdx";
const atriumBackgroundCid =
  "zb2rhWeJ5rsKhbfi31kikfdS3hE2pvWzhW27Es4Jmc863vFdy";
const atriumBackgroundSha256 = "9".repeat(64);
const coldReplayContractTests = [
  "core_vm_finalized_replay_builds_one_exact_idempotent_plan",
  "core_vm_finalized_replay_fails_closed_without_exact_evidence",
];
const acceptedSubmissionCrashRecoveryContractTests = [
  "terminal_recovery_states_require_a_nonfinal_durable_action",
  "core_lez_repairs_durable_coordinator_after_accept_to_journal_crash",
];
const palaceVmFinalityContractTests = [
  "shared_door_navigation_emits_only_after_finalized_replay",
  "tracked_finality_rejects_wrong_action_receipt_script_and_state",
  "exact_finality_promotion_is_one_shot_and_restart_safe",
  "untracked_finalized_compatibility_api_cannot_navigate",
];
const coldRebuildRemovedState = [
  "finalized-authority",
  "local-projection",
  "finalized-vm-turn",
  "vm-finality-journal",
  "storage-data-root",
  "verified-asset-cache",
];
const coldRebuildPreservedState = [
  "delivery-identity",
  "lez-wallet",
];
const sandboxTestOutput =
  `/nix/store/${"1".repeat(32)}-fixture-sandbox-output`;
const sandboxTestNarHash =
  `sha256-${"M".repeat(43)}=`;
const sandboxTestNarSize = 2048;

const screenshotSpecs = [
  [
    "gate4-a-three-user-atrium-converged.png",
    "gate4-delivery-convergence",
    "three-user-atrium-with-approved-prop",
    "a",
  ],
  [
    "gate4-b-storage-object-degraded.png",
    "gate4-storage-failure",
    "missing-storage-object-degraded",
    "b",
  ],
  [
    "gate4-b-atrium-after-moderation.png",
    "gate4-moderation",
    "atrium-after-human-user-and-prop-bans",
    "b",
  ],
  [
    "gate5-a-door-preview.png",
    "gate5-preview",
    "door-preview-before-finality",
    "a",
  ],
  [
    "gate5-b-door-preview.png",
    "gate5-preview",
    "door-preview-before-finality",
    "b",
  ],
  [
    "gate5-b-door-pending.png",
    "gate5-pending",
    "door-awaiting-lez-observation",
    "b",
  ],
  [
    "gate5-b-lounge-finalized.png",
    "gate5-finality",
    "lounge-after-door-finality",
    "b",
  ],
  [
    "gate6-b-offline-before-reconnect.png",
    "gate6-offline",
    "creator-offline-client-before-reconnect",
    "b",
  ],
  [
    "gate6-b-lounge-restarted.png",
    "gate6-restart",
    "lounge-after-bob-carol-restart",
    "b",
  ],
  [
    "gate6-c-lounge-restarted.png",
    "gate6-restart",
    "lounge-after-bob-carol-restart",
    "c",
  ],
];

const dependencyRevisions = {
  basecamp: {
    revision: basecampRevision,
    narHash: "sha256-BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB=",
  },
  delivery_module: {
    revision: "1".repeat(40),
    narHash: "sha256-CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC=",
  },
  storage_module: {
    revision: "2".repeat(40),
    narHash: "sha256-DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD=",
  },
  lez_core: {
    revision: "3".repeat(40),
    narHash: "sha256-EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE=",
  },
};

const runtimeOutputNames = [
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
];
const lgxPackageNames = [
  "logos-delivery_module-module-lib.lgx",
  "logos-lez_core-module-lib.lgx",
  "logos-logos_palace_ui-module.lgx",
  "logos-palace_core-module-lib.lgx",
  "logos-palace_vm-module-lib.lgx",
  "logos-storage_module-module-lib.lgx",
];
const lgxPackages = lgxPackageNames.map((file, index) => ({
  file,
  sha256: String(index + 4).repeat(64),
}));
const gate2LgxPackages = lgxPackages.map((entry) => (
  entry.file === "logos-palace_core-module-lib.lgx"
    ? { ...entry, sha256: "f".repeat(64) }
    : entry
));
const runtimeManifestFixture = {
  schema: "logos.palace.runtime-output-manifest",
  version: 1,
  outputs: runtimeOutputNames.map((name, index) => ({
    name,
    narHash:
      `sha256-${String.fromCharCode(65 + index).repeat(43)}=`,
    narSize: 1000 + index,
  })),
};
const runtimeManifestSha256 = digest(
  Buffer.from(`${JSON.stringify(runtimeManifestFixture, null, 2)}\n`),
);
const protocolContract = {
  palaceSchema: "palace-schema-v3",
  deliveryEnvelope: "PalaceDeliveryEnvelopeV1",
  storageCatalog: "logos-palace-mvp-storage-catalog-v1",
  catalogManifest: "logos-palace-catalog-manifest-v1",
  roomMetadata: "logos-palace-room-v1",
  propMetadata: "logos-palace-prop-v1",
  palaceManifestKind: "palace_manifest",
  vmProfile: "iptscrae_mvp_v1",
};
const networkContract = {
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
  lezProgramIdHex: palaceRelease.programIdHex,
  lezProgramBytecodeSha256: palaceRelease.programBytecodeSha256,
  lezSequencerOrigin: "https://testnet.lez.logos.co",
  lezReadOrigin: palaceRelease.explorerOrigin,
};

function storageConfig(tcpPort, discPort) {
  return JSON.stringify({
    "log-level": "INFO",
    "listen-ip": "0.0.0.0",
    "listen-port": tcpPort,
    "disc-port": discPort,
    nat: "any",
    network: "logos.test",
  });
}

function deliveryConfig(label, port, entryNode) {
  return JSON.stringify({
    mode: label === "a" ? "Core" : "Edge",
    relay: true,
    store: false,
    clusterId: 4346,
    numShardsInNetwork: 1,
    entryNodes: label === "a" ? [] : [entryNode],
    tcpPort: port,
    nat: "extip:127.0.0.1",
    listenAddress: "127.0.0.1",
    nodekey: String(["a", "b", "c"].indexOf(label) + 7).repeat(64),
    discv5Discovery: false,
    websocketSupport: false,
    quicSupport: false,
    logLevel: "WARN",
  });
}

const crcTable = Array.from({ length: 256 }, (_, index) => {
  let value = index;
  for (let bit = 0; bit < 8; bit += 1) {
    value = (value & 1) !== 0
      ? (value >>> 1) ^ 0xedb88320
      : value >>> 1;
  }
  return value >>> 0;
});

function crc32(bytes) {
  let value = 0xffffffff;
  for (const byte of bytes) {
    value = crcTable[(value ^ byte) & 0xff] ^ (value >>> 8);
  }
  return (value ^ 0xffffffff) >>> 0;
}

function pngChunk(type, data) {
  const typeBytes = Buffer.from(type, "ascii");
  const chunk = Buffer.alloc(12 + data.length);
  chunk.writeUInt32BE(data.length, 0);
  typeBytes.copy(chunk, 4);
  data.copy(chunk, 8);
  chunk.writeUInt32BE(
    crc32(Buffer.concat([typeBytes, data])),
    8 + data.length,
  );
  return chunk;
}

function validPng() {
  const header = Buffer.alloc(13);
  header.writeUInt32BE(1600, 0);
  header.writeUInt32BE(900, 4);
  header[8] = 8;
  header[9] = 2;
  const rows = Buffer.alloc(900 * (1 + 1600 * 3));
  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    pngChunk("IHDR", header),
    pngChunk("IDAT", deflateSync(rows)),
    pngChunk("IEND", Buffer.alloc(0)),
  ]);
}

function digest(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

function latency(sampleCount = 20) {
  return { sampleCount, p50Ms: 2, p95Ms: 4, maxMs: 5 };
}

function qsgRun() {
  return {
    parser: {
      basecampRevision,
      qtVersion: "6.9.2",
      renderLoop: "software",
      messagePattern: "%{category}: %{message}",
    },
    sampleCount: 20,
    summaries: Object.fromEntries(
      [
        "totalMs",
        "polishMs",
        "syncMs",
        "renderMs",
        "swapMs",
        "frameDeltaMs",
      ].map((name) => [name, { p50: 1, p95: 2, max: 3 }]),
    ),
  };
}

function rawGate4Metrics() {
  const lezActions = Array.from({ length: 11 }, (_, index) => ({
    actionId: String(index),
    kind: "fixture",
    transitionSha256: String(index).padStart(64, "0"),
    transactionHash: String(index + 20).padStart(64, "0"),
    timings: {
      submitMs: 1,
      observeMs: 2,
      finalityMs: 3,
      totalMs: 6,
    },
    timingMeasurement: {
      submitMs: "measured",
      observeMs: "measured",
      finalityMs: "measured",
      totalMs: "measured",
    },
  }));
  const storage = {
    gate3FirstNetwork: {
      providerB: { mode: "network", endToEndMs: 20 },
      coldC: { mode: "network", endToEndMs: 21 },
    },
    gate3Cached: {
      providerB: { mode: "cache", endToEndMs: 2 },
      coldC: { mode: "cache", endToEndMs: 3 },
    },
    gate4InitialCacheValidation: {
      a: { mode: "cache", endToEndMs: 2, retentionMs: 100 },
      b: { mode: "cache", endToEndMs: 2, retentionMs: 100 },
      c: { mode: "cache", endToEndMs: 3, retentionMs: 100 },
    },
    restart: {
      b: { mode: "network", endToEndMs: 30, retentionMs: 200 },
      c: { mode: "cache", endToEndMs: 4, retentionMs: 200 },
    },
  };
  const applicationRoundTrip = {
    status: "passed",
    clock: "worker performance.now monotonic milliseconds",
    startBoundary: "start",
    endBoundary: "end",
    payloadSemantics: "application UTF-8 bytes",
    samplesPerSize: 20,
    measurements: Object.fromEntries(
      [0, 256, 4096].map((bytes) => [
        String(bytes),
        {
          payloadUtf8Bytes: bytes,
          requestUtf8Bytes: bytes,
          responseUtf8Bytes: bytes,
          latency: latency(),
          samples: [],
        },
      ]),
    ),
    rejectedUnsupportedSize: "rejected fixture",
  };
  const vmMetric = (phase) => ({
    actionId: "10",
    phase,
    clock: "steady_clock",
    durationNs: "1000000",
    durationMs: 1,
    receiptSha256: "4".repeat(64),
  });
  const gate5Vm = {
    previewPayload: {
      spot: "door",
      expectedActionId: "10",
      transitionSha256: "5".repeat(64),
    },
    previewRoundTripMs: { a: 5, b: 6 },
    executeTurn: {
      provisional: {
        a: vmMetric("provisional"),
        b: vmMetric("provisional"),
      },
      finalized: vmMetric("finalized"),
    },
    useElapsedMs: 7,
    endToEndMs: 30,
  };
  const recovery = {
    lezProjectionRebuildMs: 10,
    deliveryReconnectMs: 11,
    fullRebuildMs: 12,
    coldHistoryStorageVmRebuildMs: 13,
    fullRebuildStartBoundary: "start",
    fullRebuildEndBoundary: "end",
  };
  const frameTiming = {
    parserContract: qsgRun().parser,
    runs: Object.fromEntries(
      [
        "aInitial",
        "bInitial",
        "cInitial",
        "bRestart",
        "cRestart",
      ].map((name) => [name, qsgRun()]),
    ),
  };
  const processMemory = {
    b: { palaceVmHost: { vmHwmKiB: 2048 } },
    c: { palaceVmHost: { vmHwmKiB: 3072 } },
  };
  return {
    lezActions,
    lezMeasurementBoundaries: { ...lezMeasurementBoundaries },
    storage,
    gate5Vm,
    applicationRoundTrip,
    recovery,
    processMemory,
    frameTiming,
  };
}

function compiledMetrics(gate1, gate2, gate4) {
  const metrics = gate4.metrics;
  return {
    render: {
      gate1QmlReadyMs: gate1.timings,
      markedActionToFramebufferCapture: {
        localMs:
          gate2.renderProbe.local.actionToFramebufferCaptureMs,
        remoteMs: {
          b: gate2.renderProbe.remote.b.actionToFramebufferCaptureMs,
          c: gate2.renderProbe.remote.c.actionToFramebufferCaptureMs,
        },
        screenshots: {
          local: { artifactPath: "/tmp/private-local.png" },
          remoteB: { artifactPath: "/tmp/private-b.png" },
          remoteC: { artifactPath: "/tmp/private-c.png" },
        },
      },
    },
    delivery: {
      orderedMessages: {
        total: gate2.orderedSpeech.count,
        perSender: gate2.orderedSpeech.perSenderCount,
      },
      sendToReceive:
        gate2.orderedSpeech.sendToReceiveLatency.allNodes,
      restartRecoveryMs: gate2.timings.restartRecoveryMs,
    },
    lez: {
      report: "gate4/gate4-report.json",
      jsonPointer: "/metrics/lezActions",
      measurementBoundaries: { ...metrics.lezMeasurementBoundaries },
      actions: metrics.lezActions.map(
        ({ actionId, timings, timingMeasurement }) => ({
          actionId,
          timings,
          timingMeasurement,
        }),
      ),
    },
    storage: metrics.storage,
    applicationRoundTrip: {
      clock: metrics.applicationRoundTrip.clock,
      payloadSemantics: metrics.applicationRoundTrip.payloadSemantics,
      measurements: Object.fromEntries(
        Object.entries(metrics.applicationRoundTrip.measurements).map(
          ([key, value]) => [
            key,
            {
              payloadUtf8Bytes: value.payloadUtf8Bytes,
              requestUtf8Bytes: value.requestUtf8Bytes,
              responseUtf8Bytes: value.responseUtf8Bytes,
              latency: value.latency,
            },
          ],
        ),
      ),
    },
    gate5Vm: metrics.gate5Vm,
    recovery: metrics.recovery,
    frameTiming: metrics.frameTiming,
    palaceVmPeakMemoryKiB: {
      b: metrics.processMemory.b.palaceVmHost.vmHwmKiB,
      c: metrics.processMemory.c.palaceVmHost.vmHwmKiB,
    },
  };
}

function commonSource() {
  return {
    productSnapshot,
    sourceCommit: candidateCommit,
    productSnapshotNarHash,
    productSnapshotNarSize: 123456,
    snapshotRunnerSha256,
    runtimeOutputManifestSha256: runtimeManifestSha256,
  };
}

async function writeJson(path, value) {
  await writeFile(path, `${JSON.stringify(value, null, 2)}\n`);
}

async function readJson(path) {
  return JSON.parse(await readFile(path, "utf8"));
}

async function rawReportDigest(path) {
  return digest(await readFile(path));
}

async function writeCompiled(runDir) {
  const gate1 = await readJson(join(runDir, "gate1/gate1-report.json"));
  const gate2 = await readJson(join(runDir, "gate2/gate2-report.json"));
  const gate4 = await readJson(join(runDir, "gate4/gate4-report.json"));
  const runtimeManifestPath = join(
    runDir,
    "runtime-output-manifest.json",
  );
  const runtimeManifest = await readJson(runtimeManifestPath);
  const verifier = runtimeManifest.outputs.find(
    ({ name }) => name === "release-verifier",
  );
  const coreContracts = runtimeManifest.outputs.find(
    ({ name }) => name === "palace-core-contracts",
  );
  const vmContracts = runtimeManifest.outputs.find(
    ({ name }) => name === "palace-vm-contracts",
  );
  const basecampRuntime = runtimeManifest.outputs.find(
    ({ name }) => name === "basecamp",
  );
  const hashes = {
    gate0: await rawReportDigest(
      join(runDir, "gate0/gate0-report.json"),
    ),
    gate1: await rawReportDigest(
      join(runDir, "gate1/gate1-report.json"),
    ),
    gate2: await rawReportDigest(
      join(runDir, "gate2/gate2-report.json"),
    ),
    gate3: await rawReportDigest(
      join(runDir, "gate3/gate3-report.json"),
    ),
    gate4: await rawReportDigest(
      join(runDir, "gate4/gate4-report.json"),
    ),
  };
  const gate = (name, report, reportSha256) => ({
    status: "passed",
    report,
    reportSha256,
  });
  const compiled = {
    schema: "logos.palace.basecamp-mvp-compiled-report",
    version: 1,
    status: "passed",
    fullMvp: "passed",
    ...commonSource(),
    productSnapshotNarSize: 123456,
    packageHashes: lgxPackages,
    dependencyRevisions,
    releaseVerifier: {
      narHash: verifier.narHash,
      narSize: verifier.narSize,
    },
    runtimeOutputs: {
      manifest: "runtime-output-manifest.json",
      manifestSha256: await rawReportDigest(runtimeManifestPath),
    },
    contractProofs: {
      acceptedSubmissionCrashRecovery: {
        status: "passed",
        tests: [...acceptedSubmissionCrashRecoveryContractTests],
        runtimeOutput: "palace-core-contracts",
        narHash: coreContracts.narHash,
        narSize: coreContracts.narSize,
      },
      coldReplay: {
        status: "passed",
        tests: [...coldReplayContractTests],
        runtimeOutput: "palace-core-contracts",
        narHash: coreContracts.narHash,
        narSize: coreContracts.narSize,
      },
      palaceVm: {
        status: "passed",
        tests: [...palaceVmFinalityContractTests],
        runtimeOutput: "palace-vm-contracts",
        narHash: vmContracts.narHash,
        narSize: vmContracts.narSize,
      },
    },
    basecamp: {
      revision: basecampRevision,
      sha256: basecampSha256,
      sandboxTestOutput,
      runtimeOutput: "basecamp",
      narHash: basecampRuntime.narHash,
      narSize: basecampRuntime.narSize,
      sandboxTestNarHash,
      sandboxTestNarSize,
    },
    gates: {
      gate0: {
        ...gate("gate0", "gate0/gate0-report.json", hashes.gate0),
        check: "sandbox-test",
        output: sandboxTestOutput,
      },
      gate1: gate(
        "gate1",
        "gate1/gate1-report.json",
        hashes.gate1,
      ),
      gate2: gate(
        "gate2",
        "gate2/gate2-report.json",
        hashes.gate2,
      ),
      gate3: gate(
        "gate3",
        "gate3/gate3-report.json",
        hashes.gate3,
      ),
      gate4: gate(
        "gate4",
        "gate4/gate4-report.json",
        hashes.gate4,
      ),
      gate5: gate(
        "gate5",
        "gate4/gate4-report.json",
        hashes.gate4,
      ),
      gate6: gate(
        "gate6",
        "gate4/gate4-report.json",
        hashes.gate4,
      ),
    },
    metrics: compiledMetrics(gate1, gate2, gate4),
  };
  const compiledPath = join(runDir, "compiled-mvp-report.json");
  await writeJson(compiledPath, compiled);
  await writeJson(
    join(runDir, "active-claim-completion.json"),
    {
      schema: "logos.palace.basecamp-active-run-completion",
      version: 1,
      status: "completed",
      completedAtUnixMs: 1_700_000_000_000,
      activeClaimSha256: "7".repeat(64),
      compiledReportSha256: await rawReportDigest(compiledPath),
      productSnapshot,
      sourceCommit: candidateCommit,
      productSnapshotNarHash,
      productSnapshotNarSize: 123456,
      snapshotRunnerSha256,
      runtimeOutputManifestSha256:
        compiled.runtimeOutputs.manifestSha256,
    },
  );
}

async function fixture() {
  const runDir = await mkdtemp(join(tmpdir(), "palace-public-evidence-"));
  for (const gate of ["gate0", "gate1", "gate2", "gate3", "gate4"]) {
    await mkdir(join(runDir, gate));
  }
  await writeJson(
    join(runDir, "runtime-output-manifest.json"),
    runtimeManifestFixture,
  );
  const png = validPng();
  const screenshots = [];
  for (const [file, stage, state, label] of screenshotSpecs) {
    await writeFile(join(runDir, "gate4", file), png);
    screenshots.push({
      file,
      artifactPath: file,
      stage,
      state,
      label,
      width: 1600,
      height: 900,
      byteLength: png.length,
      sha256: digest(png),
    });
  }
  const releasePreflight = {
    schema: "logos.palace.release-preflight",
    version: 1,
    status: "passed",
    release: {
      programIdHex: releaseImageId,
      programBytecodeSha256: releaseBytecodeSha256,
      programByteLength: 297312,
      deploymentTransactionHash:
        palaceRelease.deploymentTransactionHash,
      rootAccountIdHex: releaseRootId,
      rootAccountIdBase58: releaseRootBase58,
    },
    programDeployment: {
      status: "passed",
      explorerOrigin: palaceRelease.explorerOrigin,
      blockId: palaceRelease.deploymentBlockId,
      blockHash: palaceRelease.deploymentBlockHash,
      transactionHash: palaceRelease.deploymentTransactionHash,
      byteLength: 297312,
      bytecodeSha256: releaseBytecodeSha256,
      sha256: releaseBytecodeSha256,
      risc0ImageIdHex: releaseImageId,
      programIdHex: releaseImageId,
      bedrockStatus: "Finalized",
    },
  };
  const gate1 = {
    schema: "logos-palace-basecamp-gate1-report-v1",
    result: "PASS",
    cleanup: {
      status: "passed",
      failures: [],
    },
    ...commonSource(),
    basecamp: {
      binary: "/nix/store/private-basecamp/bin/Basecamp",
      revision: basecampRevision,
      sha256: basecampSha256,
    },
    lgxPackages,
    timings: {
      initialRenderMs: 10,
      roomTransitionMs: 11,
      restartRenderMs: 12,
    },
  };
  const gate2 = {
    schema: "logos-palace-basecamp-gate2-report-v1",
    result: "PASS",
    cleanup: {
      status: "passed",
      failures: [],
    },
    ...commonSource(),
    basecamp: {
      binary: "/nix/store/private-basecamp/bin/Basecamp",
      revision: basecampRevision,
      sha256: basecampSha256,
    },
    acceptanceContract: {
      deliveryEnvelope: "PalaceDeliveryEnvelopeV1",
      deliveryNetworkId: "logos.test",
    },
    runtimeVariants: {
      palaceCore: {
        kind: "test-only-acceptance-fixtures",
        file: "logos-palace_core-module-lib.lgx",
        runtimeOutput: "palace-core-acceptance-lgx",
        productionSha256: lgxPackages.find(
          ({ file }) => file
            === "logos-palace_core-module-lib.lgx",
        ).sha256,
        installedSha256: gate2LgxPackages.find(
          ({ file }) => file
            === "logos-palace_core-module-lib.lgx",
        ).sha256,
      },
    },
    lgxPackages: gate2LgxPackages,
    productionLgxPackages: lgxPackages,
    timings: { restartRecoveryMs: 30 },
    orderedSpeech: {
      count: 300,
      perSenderCount: { a: 100, b: 100, c: 100 },
      sendToReceiveLatency: { allNodes: latency(300) },
    },
    renderProbe: {
      local: { actionToFramebufferCaptureMs: 6 },
      remote: {
        b: { actionToFramebufferCaptureMs: 7 },
        c: { actionToFramebufferCaptureMs: 8 },
      },
    },
    restartRecovery: {
      launch: {
        basecampPid: 4202,
        crash: {
          previousPid: 4201,
          signal: "SIGKILL",
          exitCode: null,
          graceful: false,
        },
      },
    },
  };
  const gate3 = {
    schema: "logos.palace.basecamp-gate3-report",
    version: 1,
    status: "passed",
    fullGate3: "passed",
    cleanup: { status: "passed", failures: [] },
    productionIdentityMode: true,
    ...commonSource(),
    basecampRevision,
    basecampBinarySha256: basecampSha256,
    packageHashes: lgxPackages,
    storageConfigs: {
      a: storageConfig(31001, 32001),
      b: storageConfig(31002, 32002),
      c: storageConfig(31003, 32003),
    },
    releasePreflight,
  };
  const deliveryEntryNode =
    "/ip4/127.0.0.1/tcp/33001/p2p/12D3KooWFixtureEntry";
  const walletConfig = {
    byteLength: 512,
    sha256: "2".repeat(64),
  };
  const walletStorage = {
    byteLength: 1024,
    sha256: "3".repeat(64),
  };
  const walletEvidence = {
    config: walletConfig,
    storage: walletStorage,
    combinedSha256: digest(Buffer.from(JSON.stringify({
      config: walletConfig,
      storage: walletStorage,
    }))),
  };
  const retainedStorageRoot = {
    fileCount: 4,
    directoryCount: 2,
    totalBytes: 8192,
    sha256: "4".repeat(64),
  };
  const coreProcessNames = [
    "capability_module",
    "delivery_module",
    "lez_core",
    "package_downloader",
    "package_manager",
    "palace_core",
    "palace_vm",
    "storage_module",
  ];
  const processProgramSha256 = {
    "basecamp-main": "a".repeat(64),
    "core-module-host": "b".repeat(64),
    "ui-module-host": "c".repeat(64),
  };
  const processModuleSha256 = Object.fromEntries([
    ...coreProcessNames,
    "logos_palace_ui",
  ].map((moduleName, index) => [
    moduleName,
    (index + 1).toString(16).repeat(64),
  ]));
  const processLoaderSha256 = "f".repeat(64);
  const processLoaderPath =
    "/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2";
  const processLoaderFileIdentity = {
    device: "00:10",
    inode: "1",
  };
  const processLoaderSelection = {
    mode: "fallback",
    fallbackIndex: 0,
    candidatePath: "/lib64/ld-linux-x86-64.so.2",
    canonicalPath: processLoaderPath,
    argumentBasename: "ld-linux-x86-64.so.2",
    executableBasename: "ld-linux-x86-64.so.2",
    sha256: processLoaderSha256,
    ...processLoaderFileIdentity,
  };
  const processProgramArgument = {
    "basecamp-main": ".LogosBasecamp.elf",
    "core-module-host": ".logos_host.elf",
    "ui-module-host": ".ui-host.elf",
  };
  const processProgramPath = (role) =>
    `/nix/store/exact-basecamp/bin/${processProgramArgument[role]}`;
  const processProgramFileIdentity = (role) => ({
    device: "00:20",
    inode: String({
      "basecamp-main": 10,
      "core-module-host": 11,
      "ui-module-host": 12,
    }[role]),
  });
  const processModulePath = (moduleName) =>
    `/run/exact-installed-lgx/${moduleName}/${moduleName}_plugin.so`;
  const processModuleFileIdentity = (index) => ({
    device: "00:30",
    inode: String(1000 + index),
  });
  const processExecutableMapping = (path, identity) => ({
    path,
    permissions: "r-xp",
    ...identity,
  });
  const processModuleMapping = (moduleName, index) =>
    processExecutableMapping(
      processModulePath(moduleName),
      processModuleFileIdentity(index),
    );
  const processRuntimeArtifacts = {
    basecampBundlePrograms: [
      {
        role: "basecamp-main",
        program: "LogosBasecamp",
        argument: ".LogosBasecamp.elf",
        relativePath: "bin/.LogosBasecamp.elf",
        sha256: processProgramSha256["basecamp-main"],
      },
      {
        role: "core-module-host",
        program: "logos_host",
        argument: ".logos_host.elf",
        relativePath: "bin/.logos_host.elf",
        sha256: processProgramSha256["core-module-host"],
      },
      {
        role: "ui-module-host",
        program: "ui-host",
        argument: ".ui-host.elf",
        relativePath: "bin/.ui-host.elf",
        sha256: processProgramSha256["ui-module-host"],
      },
    ],
    basecampBundleModules: [
      "capability_module",
      "package_downloader",
      "package_manager",
    ].map((moduleName) => ({
      moduleName,
      relativePath:
        `modules/${moduleName}/${moduleName}_plugin.so`,
      sha256: processModuleSha256[moduleName],
    })),
    installedLgxModules: [
      [
        "delivery_module",
        "logos-delivery_module-module-lib.lgx",
        "delivery_module_plugin.so",
      ],
      [
        "lez_core",
        "logos-lez_core-module-lib.lgx",
        "lez_core_plugin.so",
      ],
      [
        "logos_palace_ui",
        "logos-logos_palace_ui-module.lgx",
        "logos_palace_ui_plugin.so",
      ],
      [
        "palace_core",
        "logos-palace_core-module-lib.lgx",
        "palace_core_plugin.so",
      ],
      [
        "palace_vm",
        "logos-palace_vm-module-lib.lgx",
        "palace_vm_plugin.so",
      ],
      [
        "storage_module",
        "logos-storage_module-module-lib.lgx",
        "storage_module_plugin.so",
      ],
    ].map(([moduleName, packageFile, mainFile], index) => ({
      moduleName,
      packageFile,
      packageSha256:
        lgxPackages.find(({ file }) => file === packageFile).sha256,
      installedRootSha256: (index + 1).toString(16).repeat(64),
      mainFile,
      mainFileSha256: processModuleSha256[moduleName],
    })),
    wrapperExecution: {
      mode: processLoaderSelection.mode,
      fallbackIndex: processLoaderSelection.fallbackIndex,
      argumentBasename: processLoaderSelection.argumentBasename,
      executableBasename: processLoaderSelection.executableBasename,
      sha256: processLoaderSelection.sha256,
    },
  };
  const processInventory = [
    {
      pid: 100,
      name: "LogosBasecamp",
      executable: "ld-linux-x86-64.so.2",
      executableSha256: processLoaderSha256,
      executableArgument: "ld-linux-x86-64.so.2",
      executableArgumentSha256: processLoaderSha256,
      programArgument: ".LogosBasecamp.elf",
      programArgumentSha256: processProgramSha256["basecamp-main"],
      executableFileIdentity: processLoaderFileIdentity,
      programArgumentFileIdentity:
        processProgramFileIdentity("basecamp-main"),
      programExecutableMapping: processExecutableMapping(
        processProgramPath("basecamp-main"),
        processProgramFileIdentity("basecamp-main"),
      ),
      executionMode: "fallback",
      loaderPath: processLoaderPath,
      directInterpreterPath: null,
      directInterpreterSha256: null,
      directInterpreterFileIdentity: null,
      directInterpreterMapping: null,
      moduleArgumentPath: null,
      moduleArgumentSha256: null,
      moduleArgumentFileIdentity: null,
      moduleArtifactPath: null,
      moduleArtifactSha256: null,
      moduleExecutableMapping: null,
      moduleName: null,
      role: "basecamp-main",
      program: "LogosBasecamp",
    },
    ...coreProcessNames.map((moduleName, index) => ({
      pid: 101 + index,
      name: "logos_host",
      executable: "ld-linux-x86-64.so.2",
      executableSha256: processLoaderSha256,
      executableArgument: "ld-linux-x86-64.so.2",
      executableArgumentSha256: processLoaderSha256,
      programArgument: ".logos_host.elf",
      programArgumentSha256: processProgramSha256["core-module-host"],
      executableFileIdentity: processLoaderFileIdentity,
      programArgumentFileIdentity:
        processProgramFileIdentity("core-module-host"),
      programExecutableMapping: processExecutableMapping(
        processProgramPath("core-module-host"),
        processProgramFileIdentity("core-module-host"),
      ),
      executionMode: "fallback",
      loaderPath: processLoaderPath,
      directInterpreterPath: null,
      directInterpreterSha256: null,
      directInterpreterFileIdentity: null,
      directInterpreterMapping: null,
      moduleArgumentPath: processModulePath(moduleName),
      moduleArgumentSha256: processModuleSha256[moduleName],
      moduleArgumentFileIdentity:
        processModuleFileIdentity(index),
      moduleArtifactPath: processModulePath(moduleName),
      moduleArtifactSha256: processModuleSha256[moduleName],
      moduleExecutableMapping:
        processModuleMapping(moduleName, index),
      moduleName,
      role: "core-module-host",
      program: "logos_host",
    })),
    {
      pid: 109,
      name: "ui-host",
      executable: "ld-linux-x86-64.so.2",
      executableSha256: processLoaderSha256,
      executableArgument: "ld-linux-x86-64.so.2",
      executableArgumentSha256: processLoaderSha256,
      programArgument: ".ui-host.elf",
      programArgumentSha256: processProgramSha256["ui-module-host"],
      executableFileIdentity: processLoaderFileIdentity,
      programArgumentFileIdentity:
        processProgramFileIdentity("ui-module-host"),
      programExecutableMapping: processExecutableMapping(
        processProgramPath("ui-module-host"),
        processProgramFileIdentity("ui-module-host"),
      ),
      executionMode: "fallback",
      loaderPath: processLoaderPath,
      directInterpreterPath: null,
      directInterpreterSha256: null,
      directInterpreterFileIdentity: null,
      directInterpreterMapping: null,
      moduleArgumentPath: processModulePath("logos_palace_ui"),
      moduleArgumentSha256:
        processModuleSha256.logos_palace_ui,
      moduleArgumentFileIdentity:
        processModuleFileIdentity(9),
      moduleArtifactPath: processModulePath("logos_palace_ui"),
      moduleArtifactSha256:
        processModuleSha256.logos_palace_ui,
      moduleExecutableMapping:
        processModuleMapping("logos_palace_ui", 9),
      moduleName: "logos_palace_ui",
      role: "ui-module-host",
      program: "ui-host",
    },
  ];
  const sourceProcessEvidence = {
    pid: 100,
    metricScope: "Basecamp-main-only",
    vmHwmKiB: 2048,
    vmRssKiB: 1024,
    processTree: {
      metricScope: "Basecamp-process-tree",
      processCount: processInventory.length,
      currentVmRssKiB: 4096,
      summedPerProcessVmHwmKiB: 8192,
      processes: processInventory.map(
        ({ role: _role, program: _program, ...process }, index) => ({
          ...process,
          vmHwmKiB: 100 + index,
          vmRssKiB: 50 + index,
        }),
      ),
    },
    palaceVmHost: {
      pid: 107,
      moduleName: "palace_vm",
      metricScope: "palace_vm module-host process lifetime",
      vmHwmKiB: 107,
      vmRssKiB: 57,
    },
  };
  const gate4Metrics = rawGate4Metrics();
  const gate4Actions = gate4Metrics.lezActions.map((entry, index) => {
    const startedAtUnixMs = 1_000 + index * 100;
    return {
      actionId: entry.actionId,
      timings: { ...entry.timings },
      timingMeasurement: { ...entry.timingMeasurement },
      timingBoundaries: {
        submitStartedAtUnixMs: startedAtUnixMs,
        submitCompletedAtUnixMs: startedAtUnixMs + 1,
        observeStartedAtUnixMs: startedAtUnixMs + 1,
        observeCompletedAtUnixMs: startedAtUnixMs + 3,
        finalityStartedAtUnixMs: startedAtUnixMs + 3,
        finalityCompletedAtUnixMs: startedAtUnixMs + 6,
        totalStartedAtUnixMs: startedAtUnixMs,
        totalCompletedAtUnixMs: startedAtUnixMs + 6,
      },
    };
  });
  const gate4 = {
    schema: "logos.palace.basecamp-gate4-6-report",
    version: 2,
    status: "passed",
    fullGate4: "passed",
    fullGate5: "passed",
    fullGate6: "passed",
    noPalaceServer: "passed",
    cleanup: { status: "passed", failures: [] },
    ...commonSource(),
    dependencyRevisions,
    basecampRevision,
    basecampBinarySha256: basecampSha256,
    packageHashes: lgxPackages,
    releaseContract: {
      protocols: protocolContract,
      network: networkContract,
    },
    release: {
      gate3Preflight: releasePreflight,
      programDeployment: releasePreflight.programDeployment,
    },
    identities: {
      c: {
        accountId: "7".repeat(64),
      },
    },
    restart: {
      b: {
        lez: {
          fields: {
            wallet: "opened",
          },
        },
      },
      c: {
        memory: sourceProcessEvidence,
      },
    },
    plan: {
      doorState: {
        openedStateRootHex: "8".repeat(64),
      },
    },
    catalog: {
      checksum: "6".repeat(64),
      byId: {
        "background-atrium": {
          cid: atriumBackgroundCid,
          contentSha256: atriumBackgroundSha256,
        },
      },
    },
    storage: {
      restart: {
        clients: {
          b: {
            recovered: {
              mode: "network",
              nativeSource: "network",
              nativeAvailable: 0,
              nativeTotal: 11,
            },
          },
          c: {
            recovered: {
              mode: "cache",
              nativeSource: "cache",
              nativeAvailable: 11,
              nativeTotal: 11,
            },
          },
        },
        sourceBinding: {
          sourceLabel: "c",
          sourceAccountId: "7".repeat(64),
          exactCatalogChecksum: "6".repeat(64),
          retainedDataRootBeforeRestart: retainedStorageRoot,
          retainedCatalogVerifiedBeforeColdFetch: true,
          retainedCatalogVerifiedAfterColdFetch: true,
          sourceNativeAvailable: 11,
          sourceNativeTotal: 11,
          coldClientNativeAvailable: 0,
          coldClientNativeTotal: 11,
          creatorOffline: true,
          coldClientDataRootRemoved: true,
          coldClientStorageNotStarted: true,
          onlineRetainedHolderLabels: ["c"],
          sourceRetainedAfterTransfer: {
            mode: "cache",
            nativeSource: "cache",
          },
          providerAttribution:
            "Storage API does not expose the serving peer; evidence binds the only retained local peer",
          storageProcess: sourceProcessEvidence,
        },
      },
    },
    delivery: {
      initialMesh: {
        entryLabel: "a",
        entryNode: deliveryEntryNode,
        configs: {
          a: deliveryConfig("a", 33001, deliveryEntryNode),
          b: deliveryConfig("b", 33002, deliveryEntryNode),
          c: deliveryConfig("c", 33003, deliveryEntryNode),
        },
      },
    },
    processModel: {
      standalonePalaceServer: false,
      loaderSelection: processLoaderSelection,
      runtimeArtifacts: processRuntimeArtifacts,
      observationMethod:
        "bounded /proc exact process inventory, pinned runtime artifacts, and owned TCP LISTEN proof",
      observations: [{
        rootPid: 100,
        rootProcessGroupId: 100,
        rootSessionId: 100,
        scope:
          "recursive descendants plus matching Basecamp process group/session",
        standalonePalaceServerScanScope:
          "all same-effective-UID processes visible in bounded /proc scan",
        inventoryContract: {
          basecampMain: "LogosBasecamp",
          coreModuleHosts: coreProcessNames,
          uiModuleHosts: ["logos_palace_ui"],
        },
        processCount: processInventory.length,
        processes: processInventory,
        tcpListenerProof: {
          scope:
            "TCP LISTEN sockets owned by exact Basecamp process inventory",
          expectedOnly: true,
          listenerCount: 2,
          listeners: [
            {
              protocol: "tcp4",
              address: "127.0.0.1",
              port: 34001,
              ownerPid: 100,
              ownerRole: "basecamp-main",
              moduleName: null,
              purpose: "qml-inspector",
            },
            {
              protocol: "tcp4",
              address: "0.0.0.0",
              port: 34002,
              ownerPid: 108,
              ownerRole: "core-module-host",
              moduleName: "storage_module",
              purpose: "storage-transport",
            },
          ],
        },
        standalonePalaceServerMatches: [],
      }],
    },
    failureEvidence: {
      delayedLezUpdate: {
        status: "passed",
        actionId: "10",
        observationPaused: true,
        pauseMs: 1500,
      },
      missingStorageObject: {
        status: "passed",
        objectId: "background-atrium",
        missingSourceCid: missingStorageSourceCid,
        derivativeCid: atriumBackgroundCid,
        expectedContentSha256: atriumBackgroundSha256,
        states: ["missing", "fetching", "degraded"],
        before: { receipt: "missing" },
        dispatched: {
          receipt: "ok;asset=fetching;operation=palace-asset-1",
        },
        degraded: {
          receipt: "degraded;reason=storage-download-not-found",
        },
      },
      clientOffline: {
        status: "passed",
        label: "b",
        creatorOffline: true,
      },
      coldClientRebuild: {
        status: "passed",
        label: "b",
        removed: [...coldRebuildRemovedState],
        preserved: [...coldRebuildPreservedState],
        before: {
          authority: {
            byteLength: 128,
            sha256: "a".repeat(64),
          },
          projection: {
            byteLength: 129,
            sha256: "b".repeat(64),
          },
          vmTurn: {
            byteLength: 130,
            sha256: "c".repeat(64),
          },
          vmFinality: {
            byteLength: 131,
            sha256: "d".repeat(64),
          },
          storage: {
            fileCount: 3,
            directoryCount: 2,
            totalBytes: 4096,
            sha256: "e".repeat(64),
          },
          verifiedAssets: {
            fileCount: 2,
            directoryCount: 2,
            totalBytes: 2048,
            sha256: "f".repeat(64),
          },
          identity: {
            byteLength: 132,
            sha256: "1".repeat(64),
          },
          lezWallet: walletEvidence,
        },
        preservationProof: {
          deliveryIdentityUnchangedByDeletion: true,
          lezWalletUnchangedByDeletion: true,
        },
        expectedAuthoritySha256: "a".repeat(64),
        authorityAfter: {
          file: "lez-authority-bundle-v1",
          bytes: 128,
          sha256: "a".repeat(64),
        },
        identityAfterSha256: "1".repeat(64),
        lezWalletAfter: walletEvidence,
        openedExistingLezWallet: true,
        storageRecoveryMode: "network",
        vmProjection: {
          phase: "promoted",
          actionId: "10",
          navigation: "1",
          stateRoot: "8".repeat(64),
        },
        exactFinalizedProjection: true,
      },
    },
    uiEvidence: {
      pending: ["pending-1", "pending-2"],
      finalized: ["finalized"],
      degraded: ["degraded"],
      offline: [{ label: "b" }],
    },
    screenshots,
    actions: gate4Actions,
    metrics: gate4Metrics,
  };
  await writeJson(
    join(runDir, "gate0/gate0-report.json"),
    {
      schema: "logos.palace.basecamp-gate0-report",
      version: 1,
      status: "passed",
      check: "sandbox-test",
      basecampRevision,
      basecampRuntimeOutput: "basecamp",
      basecampNarHash:
        runtimeManifestFixture.outputs.find(
          ({ name }) => name === "basecamp",
        ).narHash,
      basecampNarSize:
        runtimeManifestFixture.outputs.find(
          ({ name }) => name === "basecamp",
        ).narSize,
      sandboxTestNarHash,
      sandboxTestNarSize,
      sandboxTestOutput,
      ...commonSource(),
    },
  );
  await writeJson(join(runDir, "gate1/gate1-report.json"), gate1);
  await writeJson(join(runDir, "gate2/gate2-report.json"), gate2);
  await writeJson(join(runDir, "gate3/gate3-report.json"), gate3);
  await writeJson(join(runDir, "gate4/gate4-report.json"), gate4);
  await writeCompiled(runDir);
  return {
    runDir,
    output: join(runDir, "public-evidence.json"),
    processRuntimeArtifacts,
    processModuleSha256,
  };
}

async function withFixture(callback) {
  const value = await fixture();
  try {
    await callback(value);
  } finally {
    await rm(value.runDir, { recursive: true, force: true });
  }
}

function useDirectWrapperEvidence(gate4) {
  const interpreterPath =
    "/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2";
  const interpreterSha256 = "d".repeat(64);
  const interpreterFileIdentity = {
    device: "00:10",
    inode: "2",
  };
  gate4.processModel.loaderSelection = {
    mode: "direct",
    fallbackIndex: null,
    candidatePath: "/lib/ld-linux-x86-64.so.2",
    canonicalPath: interpreterPath,
    argumentBasename: null,
    executableBasename: "ld-linux-x86-64.so.2",
    sha256: interpreterSha256,
    ...interpreterFileIdentity,
  };
  gate4.processModel.runtimeArtifacts.wrapperExecution = {
    mode: "direct",
    fallbackIndex: null,
    argumentBasename: null,
    executableBasename: "ld-linux-x86-64.so.2",
    sha256: interpreterSha256,
  };
  const visit = (value) => {
    if (Array.isArray(value)) {
      value.forEach(visit);
      return;
    }
    if (!value || typeof value !== "object") return;
    if (
      Number.isSafeInteger(value.pid)
      && typeof value.executable === "string"
      && typeof value.programArgument === "string"
      && typeof value.programArgumentSha256 === "string"
    ) {
      value.executionMode = "direct";
      value.loaderPath = null;
      value.executable = value.programArgument;
      value.executableArgument = value.programArgument;
      value.executableSha256 = value.programArgumentSha256;
      value.executableArgumentSha256 = value.programArgumentSha256;
      value.executableFileIdentity = {
        ...value.programArgumentFileIdentity,
      };
      value.directInterpreterPath = interpreterPath;
      value.directInterpreterSha256 = interpreterSha256;
      value.directInterpreterFileIdentity = {
        ...interpreterFileIdentity,
      };
      value.directInterpreterMapping = {
        path: interpreterPath,
        permissions: "r-xp",
        ...interpreterFileIdentity,
      };
    }
    Object.values(value).forEach(visit);
  };
  visit(gate4);
  return { interpreterPath, interpreterSha256 };
}

test("builds exact allowlist-only public evidence", async () => {
  await withFixture(async ({
    runDir,
    output,
    processRuntimeArtifacts,
    processModuleSha256,
  }) => {
    const evidence = await buildPublicEvidence(runDir, output);
    const reopened = await readJson(output);
    assert.deepEqual(reopened, evidence);
    assert.deepEqual(reopened.terminalCompletion, {
      status: "completed",
      completedAtUnixMs: 1_700_000_000_000,
      activeClaimSha256: "7".repeat(64),
      compiledReportSha256: await rawReportDigest(
        join(runDir, "compiled-mvp-report.json"),
      ),
    });
    assert.equal(reopened.screenshots.length, 10);
    assert.equal(reopened.metrics.delivery.orderedMessageCount, 300);
    assert.deepEqual(reopened.components, {
      uiCommit: candidateCommit,
      coreCommit: candidateCommit,
      vmCommit: candidateCommit,
    });
    assert.equal(reopened.lgxPackages.length, 6);
    assert.equal(reopened.runtime.outputs.length, 13);
    assert.deepEqual(
      reopened.testOnlyVariants.gate2PalaceCore,
      {
        kind: "test-only-acceptance-fixtures",
        file: "logos-palace_core-module-lib.lgx",
        runtimeOutput: "palace-core-acceptance-lgx",
        productionSha256: lgxPackages.find(
          ({ file }) => file
            === "logos-palace_core-module-lib.lgx",
        ).sha256,
        installedSha256: gate2LgxPackages.find(
          ({ file }) => file
            === "logos-palace_core-module-lib.lgx",
        ).sha256,
      },
    );
    assert.equal(reopened.processProof.exactProcessInventory, true);
    assert.equal(reopened.processProof.exactPinnedRuntimeArtifacts, true);
    assert.equal(reopened.processProof.exactWrapperExecution, true);
    assert.equal(
      reopened.processProof.exactOpenedExecutableBindings,
      true,
    );
    assert.equal(
      reopened.processProof.exactProgramExecutableMappings,
      true,
    );
    assert.equal(
      reopened.processProof.exactDirectInterpreterMappings,
      true,
    );
    assert.equal(reopened.processProof.exactModuleArgumentArtifacts, true);
    assert.equal(reopened.processProof.exactExecutableModuleMappings, true);
    assert.equal(reopened.processProof.exactOwnedTcpListeners, true);
    assert.deepEqual(
      reopened.processProof.wrapperExecution,
      processRuntimeArtifacts.wrapperExecution,
    );
    assert.equal(
      reopened.processProof.moduleArgumentArtifacts.length,
      9,
    );
    assert.equal(
      reopened.processProof.moduleArgumentArtifacts.find(
        ({ moduleName }) => moduleName === "palace_core",
      ).sha256,
      processModuleSha256.palace_core,
    );
    assert.equal(reopened.recoveryEvidence.pendingAction, true);
    assert.deepEqual(
      reopened.recoveryEvidence.coldReplay.contractTests,
      coldReplayContractTests,
    );
    assert.deepEqual(
      reopened.recoveryEvidence.palaceVmFinality.contractTests,
      palaceVmFinalityContractTests,
    );
    assert.deepEqual(
      reopened.recoveryEvidence.acceptedSubmissionCrashRecovery
        .contractTests,
      acceptedSubmissionCrashRecoveryContractTests,
    );
    assert.deepEqual(
      reopened.recoveryEvidence.missingStorageObject,
      {
        passed: true,
        objectId: "background-atrium",
        missingSourceCid: missingStorageSourceCid,
        sourceAbsentFromCatalog: true,
        derivativeCid: atriumBackgroundCid,
        expectedContentSha256: atriumBackgroundSha256,
        states: ["missing", "fetching", "degraded"],
        transitionEvidence: {
          before: "missing",
          dispatched:
            "ok;asset=fetching;operation=palace-asset-1",
          degraded:
            "degraded;reason=storage-download-not-found",
        },
      },
    );
    assert.equal(
      reopened.recoveryEvidence.basecampCrashRestart.signal,
      "SIGKILL",
    );
    assert.equal(
      reopened.recoveryEvidence.basecampCrashRestart.processReplaced,
      true,
    );
    assert.equal(
      reopened.recoveryEvidence.coldClientRebuild.authority.hashEqual,
      true,
    );
    assert.equal(
      reopened.recoveryEvidence.coldClientRebuild.vmProjection.phase,
      "promoted",
    );
    assert.equal(
      reopened.recoveryEvidence.coldClientRebuild.lezStatePreserved,
      true,
    );
    assert.deepEqual(
      {
        cold:
          reopened.recoveryEvidence.coldClientRebuild.retainedSource
            .coldClientNativeAvailable,
        retained:
          reopened.recoveryEvidence.coldClientRebuild.retainedSource
            .retainedHolderNativeAvailable,
        total:
          reopened.recoveryEvidence.coldClientRebuild.retainedSource
            .retainedHolderNativeTotal,
      },
      { cold: 0, retained: 11, total: 11 },
    );
    assert.deepEqual(
      {
        runScoped:
          reopened.recoveryEvidence.coldClientRebuild.retainedSource
            .onlyRunScopedRetainedParticipantOnline,
        servingParticipantExposed:
          reopened.recoveryEvidence.coldClientRebuild.retainedSource
            .servingParticipantExposed,
        attributionBasis:
          reopened.recoveryEvidence.coldClientRebuild.retainedSource
            .attributionBasis,
      },
      {
        runScoped: true,
        servingParticipantExposed: false,
        attributionBasis: "topology-constrained inference",
      },
    );
    assert.deepEqual(
      reopened.metrics.lez.measurementBoundaries,
      lezMeasurementBoundaries,
    );
    assert.equal(
      reopened.sourceSnapshot.runnerSha256,
      snapshotRunnerSha256,
    );
    assert.equal(
      reopened.network.productionDeliveryEnvelopeNetworkId,
      "logos-lez-testnet-v0.2.0",
    );
    assert.equal(
      reopened.network.acceptanceDeliveryNetworkId,
      "logos.test",
    );
    assert.deepEqual(
      reopened.sandboxTest,
      {
        status: "passed",
        check: "sandbox-test",
        reportSha256: reopened.rawReports.gate0.sha256,
        basecampRevision,
        basecampRuntimeOutput: "basecamp",
        basecampNarHash: `sha256-${"B".repeat(43)}=`,
        basecampNarSize: 1001,
        sandboxNarHash: sandboxTestNarHash,
        sandboxNarSize: sandboxTestNarSize,
      },
    );
    const verifierOutput = runtimeManifestFixture.outputs.find(
      ({ name }) => name === "release-verifier",
    );
    assert.equal(
      reopened.release.verifierNarSize,
      verifierOutput.narSize,
    );
    assert.equal(
      reopened.release.verifierNarHash,
      verifierOutput.narHash,
    );
    assert.equal(
      (await stat(output)).mode & 0o777,
      0o600,
    );
    const encoded = JSON.stringify(reopened);
    for (const forbidden of [
      "/nix/store/",
      "/tmp/",
      "artifactPath",
      "sandboxTestOutput",
      "receiptSha256",
      "accountId",
      "\"ports\":",
      "\"device\":",
      "\"inode\":",
      "credentials",
      "apiToken",
    ]) {
      assert.equal(encoded.includes(forbidden), false, forbidden);
    }
  });
});

test("CLI writes only the sanitized JSON artifact", async () => {
  await withFixture(async ({ runDir, output }) => {
    const result = await execFileAsync(
      process.execPath,
      [builderPath, runDir, output],
      { timeout: 120_000, maxBuffer: 64 * 1024 },
    );
    assert.equal(result.stdout, "");
    assert.equal(result.stderr, "");
    assert.equal(
      (await readJson(output)).schema,
      "logos.palace.public-evidence",
    );
  });
});

test("writes exact owner-only mode even under restrictive umask", async () => {
  await withFixture(async ({ runDir, output }) => {
    const moduleUrl = pathToFileURL(builderPath).href;
    const program = [
      "process.umask(0o777);",
      `const { buildPublicEvidence } = await import(${JSON.stringify(moduleUrl)});`,
      "await buildPublicEvidence(process.argv[1], process.argv[2]);",
    ].join("\n");
    const result = await execFileAsync(
      process.execPath,
      ["--input-type=module", "--eval", program, runDir, output],
      { timeout: 120_000, maxBuffer: 64 * 1024 },
    );
    assert.equal(result.stdout, "");
    assert.equal(result.stderr, "");
    assert.equal((await stat(output)).mode & 0o777, 0o600);
  });
});

test("removes prior public evidence when terminal claim proof fails", async () => {
  await withFixture(async ({ runDir, output }) => {
    await buildPublicEvidence(runDir, output);
    const completionPath = join(
      runDir,
      "active-claim-completion.json",
    );
    const completion = await readJson(completionPath);
    completion.status = "active";
    await writeJson(completionPath, completion);

    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /terminal active-run completion is invalid/,
    );
    await assert.rejects(access(output), { code: "ENOENT" });
  });
});

test("removes prior public evidence when late compiled reopen fails", async () => {
  await withFixture(async ({ runDir, output }) => {
    await buildPublicEvidence(runDir, output);
    const compiledPath = join(runDir, "compiled-mvp-report.json");
    const compiled = await readJson(compiledPath);
    compiled.status = "failed";
    await writeJson(compiledPath, compiled);

    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /compiled|terminal active-run completion is invalid/,
    );
    await assert.rejects(access(output), { code: "ENOENT" });
  });
});

test("rejects passing Gate 1 report without terminal cleanup proof", async () => {
  await withFixture(async ({ runDir, output }) => {
    await buildPublicEvidence(runDir, output);
    const gate1Path = join(runDir, "gate1/gate1-report.json");
    const gate1 = await readJson(gate1Path);
    delete gate1.cleanup;
    await writeJson(gate1Path, gate1);
    await writeCompiled(runDir);

    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /raw gate schema or pass status is invalid/,
    );
    await assert.rejects(access(output), { code: "ENOENT" });
  });
});

test("rejects passing Gate 2 report with failed cleanup", async () => {
  await withFixture(async ({ runDir, output }) => {
    await buildPublicEvidence(runDir, output);
    const gate2Path = join(runDir, "gate2/gate2-report.json");
    const gate2 = await readJson(gate2Path);
    gate2.cleanup = {
      status: "failed",
      failures: ["b Basecamp process group 302 survived cleanup"],
    };
    await writeJson(gate2Path, gate2);
    await writeCompiled(runDir);

    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /raw gate schema or pass status is invalid/,
    );
    await assert.rejects(access(output), { code: "ENOENT" });
  });
});

test("rejects passing Gate 3 report without terminal cleanup proof", async () => {
  await withFixture(async ({ runDir, output }) => {
    await buildPublicEvidence(runDir, output);
    const gate3Path = join(runDir, "gate3/gate3-report.json");
    const gate3 = await readJson(gate3Path);
    delete gate3.cleanup;
    await writeJson(gate3Path, gate3);
    await writeCompiled(runDir);

    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /raw gate schema or pass status is invalid/,
    );
    await assert.rejects(access(output), { code: "ENOENT" });
  });
});

test("rejects passing Gate 4 report with failed cleanup", async () => {
  await withFixture(async ({ runDir, output }) => {
    await buildPublicEvidence(runDir, output);
    const gate4Path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(gate4Path);
    gate4.cleanup = {
      status: "failed",
      failures: ["c Basecamp process group 402 survived cleanup"],
    };
    await writeJson(gate4Path, gate4);
    await writeCompiled(runDir);

    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /raw gate schema or pass status is invalid/,
    );
    await assert.rejects(access(output), { code: "ENOENT" });
  });
});

test("ignores arbitrary sensitive raw keys without publishing them", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate2/gate2-report.json");
    const report = await readJson(path);
    report.untrustedExtra = {
      apiToken: "super-secret-token",
      credentials: "/tmp/private-credential",
      peer: "peer-123",
      accountIdentity: "7".repeat(64),
      ports: [54321],
      receipt: "private receipt",
      config: { seed: "secret seed" },
      userDir: "/home/public-test-user/private",
    };
    await writeJson(path, report);
    await writeCompiled(runDir);
    const evidence = await buildPublicEvidence(runDir, output);
    const encoded = JSON.stringify(evidence);
    for (const forbidden of [
      "super-secret-token",
      "/tmp/private-credential",
      "peer-123",
      "\"peer\":",
      "accountIdentity",
      "\"ports\":",
      "private receipt",
      "\"receipt\":",
      "\"config\":",
      "secret seed",
      "\"apiToken\":",
      "\"credentials\":",
      "/home/public-test-user/private",
      "\"userDir\":",
    ]) {
      assert.equal(encoded.includes(forbidden), false, forbidden);
    }
  });
});

test("fails closed when a selected metric carries private data", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "compiled-mvp-report.json");
    const report = await readJson(path);
    report.metrics.delivery.restartRecoveryMs =
      "/tmp/credential-token";
    await writeJson(path, report);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /Delivery metrics|restart|nonnegative/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects raw runner identity differing from compiled evidence", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate2/gate2-report.json");
    const report = await readJson(path);
    report.snapshotRunnerSha256 = "e".repeat(64);
    await writeJson(path, report);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /source identity differs/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects one raw LGX digest differing from compiled evidence", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const report = await readJson(path);
    report.packageHashes[0].sha256 = "e".repeat(64);
    await writeJson(path, report);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /LGX package evidence differs/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects test-only Core bound to a different runtime output", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate2/gate2-report.json");
    const report = await readJson(path);
    report.runtimeVariants.palaceCore.runtimeOutput =
      "palace-core-lgx";
    await writeJson(path, report);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /test-only Palace Core replacement is not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects private paths added to runtime output identity", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "runtime-output-manifest.json");
    const manifest = await readJson(path);
    manifest.outputs[0].storePath =
      `/nix/store/${"1".repeat(32)}-unexpected`;
    await writeJson(path, manifest);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /runtime output manifest entry is invalid/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects accepted-submit crash proof differing from contract NAR", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "compiled-mvp-report.json");
    const compiled = await readJson(path);
    compiled.contractProofs.acceptedSubmissionCrashRecovery.narSize += 1;
    await writeJson(path, compiled);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /recoverable failure evidence is incomplete/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects incomplete Palace VM semantic contract proof", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "compiled-mvp-report.json");
    const compiled = await readJson(path);
    compiled.contractProofs.palaceVm.tests.pop();
    await writeJson(path, compiled);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /recoverable failure evidence is incomplete/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects Palace VM proof differing from its runtime NAR", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "compiled-mvp-report.json");
    const compiled = await readJson(path);
    compiled.contractProofs.palaceVm.narHash =
      "sha256-ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ=";
    await writeJson(path, compiled);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /recoverable failure evidence is incomplete/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects incomplete cold replay contract proof", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "compiled-mvp-report.json");
    const compiled = await readJson(path);
    compiled.contractProofs.coldReplay.tests.pop();
    await writeJson(path, compiled);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /recoverable failure evidence is incomplete/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects Basecamp process executable identity mismatch", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    gate4.processModel.observations[0].processes[0]
      .executableSha256 = "e".repeat(64);
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /process observation is not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects same-name loader from wrong canonical path", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    gate4.processModel.observations[0].processes[0].loaderPath =
      "/tmp/forged/ld-linux-x86-64.so.2";
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /process observation is not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects wrong loader bytes at selected path with same name", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    const process = gate4.processModel.observations[0].processes[0];
    process.executableSha256 = "e".repeat(64);
    process.executableArgumentSha256 = "e".repeat(64);
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /process observation is not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects wrapper loader digest differing from raw selection", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    gate4.processModel.runtimeArtifacts.wrapperExecution.sha256 =
      "e".repeat(64);
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /wrapper execution proof is incomplete/,
    );
    await assert.rejects(access(output));
  });
});

test("accepts direct wrapper with pinned mapped interpreter", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    const direct = useDirectWrapperEvidence(gate4);
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    const evidence = await buildPublicEvidence(runDir, output);
    assert.deepEqual(evidence.processProof.wrapperExecution, {
      mode: "direct",
      fallbackIndex: null,
      argumentBasename: null,
      executableBasename: "ld-linux-x86-64.so.2",
      sha256: direct.interpreterSha256,
    });
    assert.equal(
      JSON.stringify(evidence).includes(direct.interpreterPath),
      false,
    );
  });
});

test("rejects wrong direct interpreter bytes at selected path", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    useDirectWrapperEvidence(gate4);
    gate4.processModel.observations[0].processes[0]
      .directInterpreterSha256 = "e".repeat(64);
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /process observation is not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects invalid direct interpreter executable mapping", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    useDirectWrapperEvidence(gate4);
    gate4.processModel.observations[0].processes[0]
      .directInterpreterMapping.permissions = "r--p";
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /process observation is not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects direct interpreter mapping from different inode", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    useDirectWrapperEvidence(gate4);
    gate4.processModel.observations[0].processes[0]
      .directInterpreterMapping.inode = "999";
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /process observation is not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects fallback process executable identity from wrong loader inode", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    gate4.processModel.observations[0].processes[0]
      .executableFileIdentity.inode = "999";
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /process observation is not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects fallback program without executable mapping", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    gate4.processModel.observations[0].processes[0]
      .programExecutableMapping = null;
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /process observation is not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects mapped old program inode after exact-byte replacement", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    const process = gate4.processModel.observations[0].processes[1];
    process.programArgumentFileIdentity.inode = "999";
    assert.equal(
      process.programArgumentSha256,
      gate4.processModel.runtimeArtifacts.basecampBundlePrograms[1]
        .sha256,
    );
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /process observation is not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects wrong module argv path with expected artifact mapped", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    const process = gate4.processModel.observations[0].processes[1];
    process.moduleArgumentPath =
      "/tmp/forged/capability_module_plugin.so";
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /process observation is not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects wrong module argv bytes at exact artifact path", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    gate4.processModel.observations[0].processes[1]
      .moduleArgumentSha256 = "e".repeat(64);
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /process observation is not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects module mapped without executable permission", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    gate4.processModel.observations[0].processes[1]
      .moduleExecutableMapping.permissions = "r--p";
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /process observation is not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects module mapping from different inode", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    gate4.processModel.observations[0].processes[1]
      .moduleExecutableMapping.inode = "999";
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /process observation is not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects process bytes differing from pinned Basecamp artifact", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    gate4.processModel.observations[0].processes[0]
      .programArgumentSha256 = "e".repeat(64);
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /process observation is not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects installed module artifact differing from source LGX", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    gate4.processModel.runtimeArtifacts.installedLgxModules[0]
      .packageSha256 = "e".repeat(64);
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /pinned runtime artifact proof is incomplete/,
    );
    await assert.rejects(access(output));
  });
});

test("accepts exact coalesced LEZ finality measurement", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    const metric = gate4.metrics.lezActions[0];
    const action = gate4.actions[0];
    metric.timings.finalityMs = 0;
    metric.timingMeasurement.finalityMs = "measured-coalesced";
    metric.timings.totalMs = 3;
    action.timings.finalityMs = 0;
    action.timingMeasurement.finalityMs = "measured-coalesced";
    action.timings.totalMs = 3;
    action.timingBoundaries.finalityCompletedAtUnixMs =
      action.timingBoundaries.finalityStartedAtUnixMs;
    action.timingBoundaries.totalCompletedAtUnixMs =
      action.timingBoundaries.finalityCompletedAtUnixMs;
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    const evidence = await buildPublicEvidence(runDir, output);
    assert.equal(
      evidence.metrics.lez.actions[0].finalityMeasurement,
      "measured-coalesced",
    );
    assert.equal(evidence.metrics.lez.actions[0].finalityMs, 0);
  });
});

test("rejects nonzero coalesced LEZ finality measurement", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    gate4.metrics.lezActions[0].timingMeasurement.finalityMs =
      "measured-coalesced";
    gate4.actions[0].timingMeasurement.finalityMs =
      "measured-coalesced";
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /raw LEZ timing boundaries are not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects recovered-unmeasured LEZ duration", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    for (const source of [
      gate4.metrics.lezActions[0],
      gate4.actions[0],
    ]) {
      source.timings.observeMs = null;
      source.timingMeasurement.observeMs = "recovered-unmeasured";
    }
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /raw LEZ timing boundaries are not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects forged raw LEZ timing boundary", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    gate4.actions[0].timingBoundaries.observeCompletedAtUnixMs += 1;
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /raw LEZ timing boundaries are not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects changed LEZ measurement boundary", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    gate4.metrics.lezMeasurementBoundaries.finalityMs = "ambiguous";
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /compiled LEZ metrics differ from raw evidence/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects missing Storage transition receipt mutation", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    gate4.failureEvidence.missingStorageObject.dispatched.receipt =
      "ok;asset=fetching;operation=../../private";
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /recoverable failure evidence is incomplete/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects cold rebuild authority hash mismatch", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    gate4.failureEvidence.coldClientRebuild.authorityAfter.sha256 =
      "2".repeat(64);
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /recoverable failure evidence is incomplete/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects cold rebuild retained-source count mismatch", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    gate4.storage.restart.sourceBinding.sourceNativeAvailable = 10;
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /recoverable failure evidence is incomplete/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects alternate exact process observation as Storage source", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    const offset = 1_000;
    const alternateObservation = structuredClone(
      gate4.processModel.observations[0],
    );
    alternateObservation.rootPid += offset;
    alternateObservation.rootProcessGroupId += offset;
    alternateObservation.rootSessionId += offset;
    for (const process of alternateObservation.processes) {
      process.pid += offset;
    }
    for (const listener of
      alternateObservation.tcpListenerProof.listeners) {
      listener.ownerPid += offset;
    }
    gate4.processModel.observations.push(alternateObservation);

    const alternateProcess = structuredClone(
      gate4.storage.restart.sourceBinding.storageProcess,
    );
    alternateProcess.pid += offset;
    for (const process of alternateProcess.processTree.processes) {
      process.pid += offset;
    }
    alternateProcess.palaceVmHost.pid += offset;
    gate4.storage.restart.sourceBinding.storageProcess =
      alternateProcess;

    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /recoverable failure evidence is incomplete/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects Gate 2 crash without process replacement", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate2/gate2-report.json");
    const gate2 = await readJson(path);
    gate2.restartRecovery.launch.basecampPid =
      gate2.restartRecovery.launch.crash.previousPid;
    await writeJson(path, gate2);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /recoverable failure evidence is incomplete/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects Gate 0 Basecamp runtime NAR mismatch", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate0/gate0-report.json");
    const gate0 = await readJson(path);
    gate0.basecampNarSize += 1;
    await writeJson(path, gate0);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /sandbox test evidence is incomplete/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects Gate 0 sandbox output NAR mismatch", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate0/gate0-report.json");
    const gate0 = await readJson(path);
    gate0.sandboxTestNarSize += 1;
    await writeJson(path, gate0);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /sandbox test evidence is incomplete/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects process outside exact Basecamp/module inventory", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    gate4.processModel.observations[0].processes.push({
      pid: 999,
      role: "standalone-server",
      moduleName: null,
      program: "palace-server",
    });
    gate4.processModel.observations[0].processCount += 1;
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /process observation is not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects duplicate allowed Basecamp listener role", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(runDir, "gate4/gate4-report.json");
    const gate4 = await readJson(path);
    const proof = gate4.processModel.observations[0].tcpListenerProof;
    proof.listeners.push({
      ...proof.listeners[0],
      port: proof.listeners[0].port + 1,
    });
    proof.listenerCount = proof.listeners.length;
    await writeJson(path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /Basecamp process observation is not exact/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects release deployment identity differing from pinned block", async () => {
  await withFixture(async ({ runDir, output }) => {
    const gate3Path = join(runDir, "gate3/gate3-report.json");
    const gate4Path = join(runDir, "gate4/gate4-report.json");
    const gate3 = await readJson(gate3Path);
    const gate4 = await readJson(gate4Path);
    gate3.releasePreflight.programDeployment.blockHash = "e".repeat(64);
    gate4.release.gate3Preflight = gate3.releasePreflight;
    gate4.release.programDeployment =
      gate3.releasePreflight.programDeployment;
    await writeJson(gate3Path, gate3);
    await writeJson(gate4Path, gate4);
    await writeCompiled(runDir);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /release evidence is incomplete or inconsistent/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects unexpected public evidence keys", async () => {
  await withFixture(async ({ runDir, output }) => {
    const evidence = await buildPublicEvidence(runDir, output);
    evidence.metrics.render.unexpected = 1;
    assert.throws(
      () => validatePublicEvidence(evidence),
      /public evidence schema/,
    );
  });
});

test("rejects direct Storage provider attribution overclaim", async () => {
  await withFixture(async ({ runDir, output }) => {
    const evidence = await buildPublicEvidence(runDir, output);
    evidence.recoveryEvidence.coldClientRebuild.retainedSource
      .attributionBasis = "direct provider proof";
    assert.throws(
      () => validatePublicEvidence(evidence),
      /public evidence schema/,
    );
  });
});

test("rejects mutated screenshot bytes", async () => {
  await withFixture(async ({ runDir, output }) => {
    const path = join(
      runDir,
      "gate4/gate4-a-three-user-atrium-converged.png",
    );
    const bytes = await readFile(path);
    bytes[Math.floor(bytes.length / 2)] ^= 0xff;
    await writeFile(path, bytes);
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /strict screenshot evidence validation failed/,
    );
    await assert.rejects(access(output));
  });
});

test("rejects deleted screenshot evidence", async () => {
  await withFixture(async ({ runDir, output }) => {
    await unlink(
      join(runDir, "gate4/gate6-c-lounge-restarted.png"),
    );
    await assert.rejects(
      buildPublicEvidence(runDir, output),
      /strict screenshot evidence validation failed/,
    );
    await assert.rejects(access(output));
  });
});
