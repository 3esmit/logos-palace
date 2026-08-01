#!/usr/bin/env node

import assert from "node:assert/strict";
import {
  chmod,
  mkdir,
  mkdtemp,
  readFile,
  rm,
  symlink,
  writeFile,
} from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";
import test from "node:test";
import {
  auditedPrePublicWriteGate3Failures,
  createClaimLifecycle,
  releaseProgramId,
  releaseRootId,
  sha256,
  validateProcessScopeNames,
} from "./basecamp_claim_lifecycle.mjs";

const uid = process.getuid();
const sourceCommit = "a".repeat(40);
const successorCommit = "b".repeat(40);
const narHash = `sha256-${"A".repeat(43)}=`;
const runnerSha256 = "c".repeat(64);
const completedSha256 = "d".repeat(64);
const processScopeSlice = "logos-palace-run-NEW00001.slice";
const processScopePrefix = "logos-palace-run-NEW00001";
const gates = [
  "gate0",
  "gate1",
  "gate2",
  "gate3",
  "gate4",
  "gate5",
  "gate6",
];

async function writeMode(path, contents, mode = 0o600) {
  await writeFile(path, contents, { mode });
  await chmod(path, mode);
}

async function writeJson(path, value) {
  await writeMode(path, `${JSON.stringify(value, null, 2)}\n`);
}

function failedReport(productSnapshot, phase = "gate1") {
  return {
    schema: "logos.palace.basecamp-mvp-compiled-report",
    version: 1,
    status: "failed",
    fullMvp: "not-evaluated",
    productSnapshot,
    scope: {
      implementedGates: gates,
      pendingGates: [],
    },
    failure: {
      phase,
      message: "gate command exited with status 1",
    },
    gates: {
      gate0: { status: "unknown", report: null },
      gate1: {
        status: "unknown",
        report: "gate1/gate1-report.json",
      },
      gate2: {
        status: "unknown",
        report: "gate2/gate2-report.json",
      },
      gate3: {
        status: "unknown",
        report: "gate3/gate3-report.json",
      },
      gate4: {
        status: "unknown",
        report: "gate4/gate4-report.json",
      },
      gate5: {
        status: "unknown",
        report: "gate4/gate4-report.json",
      },
      gate6: {
        status: "unknown",
        report: "gate4/gate4-report.json",
      },
    },
  };
}

function auditedPrePublicWriteGate3Report(predecessor) {
  return {
    schema: "logos.palace.basecamp-gate3-report",
    version: 1,
    status: "failed",
    fullGate3: "failed",
    cleanup: { status: "passed", failures: [] },
    blockers: [],
    productSnapshot: predecessor.productSnapshot,
    productSnapshotNarHash: predecessor.snapshotNarHash,
    productSnapshotNarSize: predecessor.snapshotNarSize,
    snapshotRunnerSha256: predecessor.snapshotRunnerSha256,
    runtimeOutputManifestSha256: predecessor.runtimeManifestSha256,
    sourceCommit: predecessor.gitCommit,
    basecampRevision: "1".repeat(40),
    packageHashes: [],
    basecampBinarySha256: "2".repeat(64),
    installedPackages: { a: [], b: [], c: [] },
    productionIdentityMode: true,
    releasePreflight: {
      status: "passed",
      rootAccountBeforeWrites: { status: "passed", state: "uninitialized" },
    },
    identities: {},
    storageConfigs: { a: "{}", b: "{}", c: "{}" },
    startup: { a: { basecampPid: 2, startupMs: 0 } },
    storageStartup: {},
    providerBRetentionProofs: [],
    creatorOffline: false,
    pngRecovery: "failed",
    failure: "production LEZ a: rejected=lez-network-fingerprint",
  };
}

function identityRegistrationAndIdleStorageGate3Report(predecessor) {
  const registration = (label, display) => {
    const accountId = label.repeat(64);
    const deliveryKey = label === "a" ? "b".repeat(64)
      : label === "b" ? "c".repeat(64) : "d".repeat(64);
    const registrationTransaction = label === "a" ? "e".repeat(64)
      : label === "b" ? "f".repeat(64) : "1".repeat(64);
    return {
      accountId,
      display,
      deliveryKey,
      keyEpoch: "1",
      registrationTransaction,
      existing: false,
      receipt:
        `ok;existing=0;identity=${accountId};display=${display};`
        + `delivery_key=${deliveryKey};key_epoch=1;registration=submitted;`
        + `registration_ready=1;registration_tx=${registrationTransaction}`,
    };
  };
  const startup = (pid, height) => ({
    basecampPid: pid,
    startupMs: 0,
    lez: {
      receipt: "",
      elapsedMs: 1,
      lezStateObservation: {
        source: "gate4LezState",
        receipt:
          `wallet=created;ready=1;compatible=1;running=1;tracked=0;`
          + `sync=current;current_height=${height};synced_height=${height};`
          + "authority=missing;vm=idle;vm_action=none;"
          + `program=${releaseProgramId}`,
      },
    },
  });
  const storage = (state) => ({
    receipt:
      `ok;storage=${state};pending=0;callbacks=0;callback_registration=ready;`
      + "reconciliation_required=0;catalog=idle;catalog_verified=0;"
      + "retention_round=0;retained=0",
    elapsedMs: 1,
  });
  return {
    ...auditedPrePublicWriteGate3Report(predecessor),
    assetAuthoring: {
      version: 1,
      phase: "input-validated",
      inputManifest: {
        schema: "logos.palace.e2e-asset-inputs",
        version: 1,
        sha256: "a".repeat(64),
        assetCount: 1,
      },
      selectedAssetCount: 1,
      propStory: "not-requested",
      boundary: "selected bytes",
      assets: [],
      graphBindings: [],
      elapsedMs: 0,
    },
    identities: {
      a: registration("a", "Alice"),
      b: registration("b", "Bob"),
      c: registration("c", "Carol"),
    },
    startup: {
      a: startup(2, 10),
      b: startup(3, 11),
      c: startup(4, 12),
    },
    storageStartup: {
      a: { start: storage("starting"), running: storage("running") },
      b: { start: storage("starting"), running: storage("running") },
    },
    failure: "worker a: asset picker opened multiple dialogs",
  };
}

function identityRegistrationAndApprovalGuardedAssetsGate3Report(predecessor) {
  const report = identityRegistrationAndIdleStorageGate3Report(predecessor);
  const handle = "a".repeat(64);
  report.assetAuthoring = {
    version: 1,
    phase: "approval-guarded",
    inputManifest: {
      schema: "logos.palace.e2e-asset-inputs",
      version: 1,
      sha256: "b".repeat(64),
      assetCount: 1,
    },
    selectedAssetCount: 1,
    propStory: "not-requested",
    boundary: "selected bytes -> verified handle",
    assets: [{
      assetId: "fixture-background",
      file: "fixture-background.png",
      label: "fixture-background.png",
      role: "room-background",
      target: { kind: "room-background", roomId: "atrium" },
      handle,
      byteLength: 10,
      chunkBytes: 32 * 1024,
      chunkCount: 1,
      width: 1,
      height: 1,
      begin: {
        receipt:
          `ok;session=${"c".repeat(32)};next=0;maxChunkBytes=32768;`
          + "maxTotalBytes=10485760",
        elapsedMs: 1,
      },
      appends: [{
        sequence: 0,
        byteLength: 10,
        receipt: `ok;session=${"c".repeat(32)};next=1;bytes=10`,
        elapsedMs: 1,
      }],
      commit: {
        receipt: `ok;handle=${handle};width=1;height=1;bytes=10`,
        elapsedMs: 1,
      },
    }],
    graphBindings: [],
    guardedBeforeApproval: {
      receipt: "rejected=asset-not-approved",
      elapsedMs: 1,
    },
    elapsedMs: 1,
  };
  report.failure = "worker a: moderation asset upload failed";
  return report;
}

function sealedBundleRetainedAfterCreatorOfflineGate3Report(predecessor) {
  const report = identityRegistrationAndApprovalGuardedAssetsGate3Report(
    predecessor,
  );
  const template = report.assetAuthoring.assets[0];
  const cid = (suffix) => `z${"a".repeat(49)}${suffix}`;
  const makeAsset = ({ assetId, file, handle, objectCid, roomId }) => ({
    ...template,
    assetId,
    file,
    label: file,
    handle,
    target: { kind: "room-background", roomId },
    commit: {
      ...template.commit,
      receipt: `ok;handle=${handle};width=1;height=1;bytes=10`,
    },
    review: { receipt: `ok;handle=${handle};review=approved`, elapsedMs: 1 },
    publication: {
      dispatched: { receipt: "ok;asset=publishing", elapsedMs: 1 },
      completed: { receipt: `published;cid=${objectCid}`, elapsedMs: 1 },
    },
    cid: objectCid,
    assignment: { receipt: `ok;room=${roomId};handle=${handle}`, elapsedMs: 1 },
  });
  const atrium = makeAsset({
    assetId: "fixture-atrium",
    file: "fixture-atrium.png",
    handle: "a".repeat(64),
    objectCid: cid(1),
    roomId: "atrium",
  });
  const lounge = makeAsset({
    assetId: "fixture-lounge",
    file: "fixture-lounge.png",
    handle: "b".repeat(64),
    objectCid: cid(2),
    roomId: "lounge",
  });
  const objects = [
    ["background-atrium", atrium.cid, atrium.handle],
    ["background-lounge", lounge.cid, lounge.handle],
    ["room-atrium-metadata", cid(3), "c".repeat(64)],
    ["room-lounge-metadata", cid(4), "d".repeat(64)],
    ["palace-1", cid(5), "e".repeat(64)],
  ].map(([objectId, objectCid, contentSha256]) => ({
    objectId,
    cid: objectCid,
    contentSha256,
    byteLength: 10,
    mediaType: "application/octet-stream",
    type: "fixture",
  }));
  const binding = (asset, object, targetId) => ({
    kind: "room-background",
    targetId,
    objectId: object.objectId,
    assetId: asset.assetId,
    assignment: asset.assignment,
    cid: object.cid,
    contentSha256: object.contentSha256,
  });
  report.assetAuthoring = {
    ...report.assetAuthoring,
    phase: "complete",
    selectedAssetCount: 2,
    catalogCount: 2,
    inputManifest: { ...report.assetAuthoring.inputManifest, assetCount: 2 },
    assets: [atrium, lounge],
    assignments: {
      rooms: { atrium: atrium.handle, lounge: lounge.handle },
      prop: null,
    },
    graphBindings: [
      binding(atrium, objects[0], "atrium"),
      binding(lounge, objects[1], "lounge"),
    ],
  };
  report.publication = {
    checksum: "f".repeat(64),
    dispatched: { receipt: "ok;state=published", elapsedMs: 1 },
    completed: { receipt: "state=verified;catalog=fixture", elapsedMs: 1 },
    objects,
  };
  const retentionReceipt = (round, prefix = "") =>
    `${prefix}state=verified;published=5;verified=5;total=5;`
    + `retention=verified;retention_round=${round};source=cache;`
    + "native_available=5;native_total=5;catalog=fixture";
  const retention = (round) => ({
    round,
    proof:
      "native exists(cid)=true for every CID, then local-only Storage V2 retrieval "
      + "with exact length, SHA-256, and bytes",
    dispatched: { receipt: retentionReceipt(round, "ok;"), elapsedMs: 1 },
    completed: { receipt: retentionReceipt(round), elapsedMs: 1 },
    retained: objects.map((object) => ({
      objectId: object.objectId,
      cid: object.cid,
      receipt:
        `state=verified;publication=published;retention=verified;cid=${object.cid}`,
      elapsedMs: 1,
    })),
  });
  const bundleReceipt = (state) =>
    `state=${state};published=5;verified=${state === "fetching" ? 0 : 5};`
    + "total=5;retention=missing;retention_round=0;source=cache;"
    + "native_available=5;native_total=5"
    + (state === "fetching" ? "" : ";catalog=fixture");
  const verified = (includesCid = true) => objects.map((object) => ({
    objectId: object.objectId,
    ...(includesCid ? { cid: object.cid } : {}),
    receipt:
      `state=verified;publication=published;retention=missing;cid=${object.cid}`,
    elapsedMs: 1,
  }));
  const fetch = (mode) => ({
    mode,
    clock: "performance.now monotonic milliseconds",
    startBoundary: "immediately before first local object status read",
    endBoundary:
      "all exact catalog objects re-read as CID-verified after completion",
    before: mode === "network"
      ? objects.map((object) => ({
          objectId: object.objectId,
          receipt: "state=missing",
          elapsedMs: 1,
        }))
      : verified(false),
    dispatched: mode === "network"
      ? { receipt: `ok;${bundleReceipt("fetching")}`, elapsedMs: 1 }
      : {
          receipt: "ok;state=verified;catalog=fixture",
          elapsedMs: 0,
          reusedVerifiedCatalog: true,
        },
    completed: { receipt: bundleReceipt("verified"), elapsedMs: 1 },
    verified: verified(),
    endToEndMs: 1,
  });
  report.providerBFetch = fetch("network");
  report.providerBCachedFetch = fetch("cache");
  report.providerBRetentionProofs = [retention(1), retention(2)];
  const storagePeerEndpoints = {
    a: {
      peerId: "peer-a",
      spr: "spr:peer-a",
      addrs: ["/ip4/127.0.0.1/tcp/41001"],
      announceAddresses: ["/ip4/127.0.0.1/tcp/41001"],
      tablePeers: ["peer-a"],
    },
    b: {
      peerId: "peer-b",
      spr: "spr:peer-b",
      addrs: ["/ip4/127.0.0.1/tcp/41002"],
      announceAddresses: ["/ip4/127.0.0.1/tcp/41002"],
      tablePeers: ["peer-b", "peer-a"],
    },
  };
  const meshDial = (from, to) => ({
    from,
    to,
    peerId: storagePeerEndpoints[to].peerId,
    addresses: [
      storagePeerEndpoints[to].addrs[0],
      `${storagePeerEndpoints[to].addrs[0]}/p2p/${storagePeerEndpoints[to].peerId}`,
    ],
    result: { receipt: "ok;connect=sent;peers=2", elapsedMs: 1 },
  });
  const storageMesh = {
    labels: ["a", "b"],
    dials: [meshDial("a", "b"), meshDial("b", "a")],
    settleMs: 1,
  };
  const visibleEndpoint = (label, otherLabel) => {
    const source = storagePeerEndpoints[label];
    const observation = {
      addrs: source.addrs,
      announceAddresses: source.announceAddresses,
      peerId: source.peerId,
      seenPeers: [storagePeerEndpoints[otherLabel].peerId],
      spr: source.spr,
      tablePeers: [source.peerId, storagePeerEndpoints[otherLabel].peerId],
    };
    return {
      ...observation,
      receipt: `ok;${JSON.stringify(observation)}`,
      elapsedMs: 1,
    };
  };
  report.storagePeerEndpoints = storagePeerEndpoints;
  report.storageMesh = storageMesh;
  report.storageMeshPreFetch = JSON.parse(JSON.stringify(storageMesh));
  report.storageMeshVisibility = {
    ready: true,
    endpoints: {
      a: visibleEndpoint("a", "b"),
      b: visibleEndpoint("b", "a"),
    },
    waitedMs: 1,
  };
  report.storageBlockMaterialization = {
    fromRepo: "/fixture/a/repo",
    toRepo: "/fixture/b/repo",
    fromLabel: "a",
    toLabel: "b",
    mode: "co-located-block-copy",
    copied: [
      "blocks",
      "manifests",
      "dht/providers",
      "storage_publications",
      "verified_assets",
    ],
  };
  report.creatorStopIntent = {
    checkpointed: true,
    pid: report.startup.a.basecampPid,
  };
  report.creatorOffline = true;
  report.failure =
    "worker b: gate3FetchPng receipt timeout: before=\"missing\" after=\"\" "
    + "state=\"wallet=created;ready=1;compatible=1;running=1;tracked=0;"
    + "sync=current;current_height=10;synced_height=10;authority=missing;"
    + `vm=idle;vm_action=none;program=${releaseProgramId}\" sequence=1->2`;
  return report;
}

function completedGate3StrictEvidenceRejectionReport(predecessor) {
  const report = sealedBundleRetainedAfterCreatorOfflineGate3Report(
    predecessor,
  );
  const clone = (value) => JSON.parse(JSON.stringify(value));
  const storageStatus = (state) => ({
    receipt:
      `ok;storage=${state};pending=0;callbacks=0;callback_registration=ready;`
      + "reconciliation_required=0;catalog=idle;catalog_verified=0;"
      + "retention_round=0;retained=0",
    elapsedMs: 1,
  });
  const endpointC = {
    peerId: "peer-c",
    spr: "spr:peer-c",
    addrs: ["/ip4/127.0.0.1/tcp/41003"],
    announceAddresses: ["/ip4/127.0.0.1/tcp/41003"],
    tablePeers: ["peer-c", "peer-b"],
    seenPeers: [],
  };
  const meshDial = (from, to) => ({
    from,
    to,
    peerId: report.storagePeerEndpoints[to].peerId,
    addresses: [
      report.storagePeerEndpoints[to].addrs[0],
      `${report.storagePeerEndpoints[to].addrs[0]}/p2p/${report.storagePeerEndpoints[to].peerId}`,
    ],
    result: { receipt: "ok;connect=sent;peers=2", elapsedMs: 1 },
  });
  const cid = report.publication.objects[0].cid;
  const handle = report.assetAuthoring.assets[0].handle;

  report.status = "passed";
  report.fullGate3 = "passed";
  delete report.failure;
  report.pngRecovery = "passed";
  report.storageStartup.c = {
    start: storageStatus("starting"),
    running: storageStatus("running"),
  };
  report.storagePeerEndpoints.c = endpointC;
  report.storageMeshC = {
    labels: ["b", "c"],
    dials: [meshDial("b", "c"), meshDial("c", "b")],
    settleMs: 1,
  };
  report.storageBlockMaterializationC = {
    fromRepo: "/fixture/b/repo",
    toRepo: "/fixture/c/repo",
    fromLabel: "b",
    toLabel: "c",
    mode: "co-located-block-copy",
    copied: [
      "blocks",
      "manifests",
      "dht/providers",
      "storage_publications",
      "verified_assets",
    ],
  };
  report.coldCFetch = clone(report.providerBFetch);
  report.coldCCachedFetch = clone(report.providerBCachedFetch);
  report.metrics = {
    storage: {
      clock: "performance.now monotonic milliseconds",
      firstNetworkFetch: {
        providerB: { mode: "network", endToEndMs: report.providerBFetch.endToEndMs },
        coldC: { mode: "network", endToEndMs: report.coldCFetch.endToEndMs },
      },
      cachedFetch: {
        providerB: { mode: "cache", endToEndMs: report.providerBCachedFetch.endToEndMs },
        coldC: { mode: "cache", endToEndMs: report.coldCCachedFetch.endToEndMs },
      },
    },
  };
  report.assetStateProof = {
    states: ["missing", "fetching", "verified", "degraded"],
    verifiedAsset: {
      role: "room-background",
      cid,
      before: { receipt: "missing", elapsedMs: 1 },
      dispatched: {
        receipt: "ok;asset=fetching;operation=fixture-1",
        elapsedMs: 1,
      },
      completed: { receipt: `verified;handle=${handle}`, elapsedMs: 1 },
    },
    degradedAsset: {
      role: "room-background",
      cid,
      dispatched: {
        receipt: "degraded;reason=byte-length-or-digest-mismatch",
        elapsedMs: 1,
      },
      completed: { receipt: "degraded;reason=fixture", elapsedMs: 1 },
    },
  };
  report.assetAuthoringScreenshot = {
    file: "gate3-admin-assets-published.png",
    artifactPath: "gate3-admin-assets-published.png",
    width: 1,
    height: 1,
    byteLength: 1,
    sha256: "f".repeat(64),
    stage: "gate3-admin-asset-authoring",
    state: "admin-selected-assets-approved-published-assigned",
    label: "a",
    renderEvidence: {
      schema: "logos.palace.asset-authoring-render",
      version: 1,
      open: true,
      cardCount: 2,
      readyImageCount: 2,
      publishedCount: 2,
      atriumAssigned: true,
      loungeAssigned: true,
      propAssigned: false,
      fenceRequest: 1,
      fenceState: "complete",
      fenceFrame: 1,
      epoch: 1,
    },
  };
  return report;
}

async function fixture({
  legacy = false,
  additionalPrePublicWriteAudits = [],
} = {}) {
  const root = await mkdtemp(join(tmpdir(), "palace-claim-lifecycle-"));
  const claimDirectory = join(root, "claims");
  const runs = join(root, "runs");
  const predecessorRun = join(runs, "run.OLD00001");
  const successorRun = join(runs, "run.NEW00001");
  const predecessorSnapshot = join(root, "snapshot-old");
  const successorSnapshot = join(root, "snapshot-new");
  const predecessorGcRoot = join(root, "old-product-snapshot");
  const successorGcRoot = join(root, "new-product-snapshot");
  await mkdir(claimDirectory, { mode: 0o700 });
  await mkdir(runs, { mode: 0o700 });
  await mkdir(predecessorRun, { mode: 0o700 });
  await mkdir(successorRun, { mode: 0o700 });
  await mkdir(predecessorSnapshot, { mode: 0o700 });
  await mkdir(successorSnapshot, { mode: 0o700 });
  await mkdir(join(predecessorRun, "shared-state"), { mode: 0o700 });
  await mkdir(join(predecessorRun, "gate0"), { mode: 0o700 });
  await mkdir(join(predecessorRun, "gate1"), { mode: 0o700 });
  await symlink(predecessorSnapshot, predecessorGcRoot);
  await symlink(successorSnapshot, successorGcRoot);

  const predecessorManifest = Buffer.from('{"outputs":["old"]}\n');
  const successorManifest = Buffer.from('{"outputs":["new"]}\n');
  const predecessorManifestPath = join(
    predecessorRun,
    "runtime-output-manifest.json",
  );
  const successorManifestPath = join(
    successorRun,
    "runtime-output-manifest.json",
  );
  await writeMode(predecessorManifestPath, predecessorManifest);
  await writeMode(successorManifestPath, successorManifest);

  const predecessorCommon = {
    schema: "logos.palace.basecamp-active-run-claim",
    version: legacy ? 1 : 2,
    uid,
    releaseProgramId,
    releaseRootId,
    runDirectory: predecessorRun,
    productSnapshot: predecessorSnapshot,
    gcRootPath: predecessorGcRoot,
    gcRootTarget: predecessorSnapshot,
    gitCommit: sourceCommit,
    snapshotNarHash: narHash,
    snapshotNarSize: 4096,
    snapshotRunnerSha256: runnerSha256,
    runtimeManifestPath: predecessorManifestPath,
    runtimeManifestSha256: sha256(predecessorManifest),
    ...(
      legacy
        ? {}
        : {
            processScopeSlice: "logos-palace-run-OLD00001.slice",
            processScopePrefix: "logos-palace-run-OLD00001",
          }
    ),
  };
  const successorCommon = {
    schema: "logos.palace.basecamp-active-run-claim",
    version: 2,
    uid,
    releaseProgramId,
    releaseRootId,
    runDirectory: successorRun,
    productSnapshot: successorSnapshot,
    gcRootPath: successorGcRoot,
    gcRootTarget: successorSnapshot,
    gitCommit: successorCommit,
    snapshotNarHash: narHash,
    snapshotNarSize: 8192,
    snapshotRunnerSha256: "e".repeat(64),
    runtimeManifestPath: successorManifestPath,
    runtimeManifestSha256: sha256(successorManifest),
    processScopeSlice,
    processScopePrefix,
  };
  const predecessorClaim = {
    ...predecessorCommon,
    status: legacy ? "active" : "active-pre-gate3",
    createdAtUnixMs: 1_700_000_000_000,
  };
  const claimPath = join(
    claimDirectory,
    `active-${releaseProgramId}-${releaseRootId}.json`,
  );
  await writeJson(claimPath, predecessorClaim);
  await writeMode(
    join(predecessorRun, "product-snapshot"),
    `${predecessorSnapshot}\n`,
  );
  await writeJson(join(predecessorRun, "source-identity.json"), {
    schema: "logos.palace.basecamp-source-identity",
    version: 1,
    gitCommit: predecessorCommon.gitCommit,
    productSnapshot: predecessorSnapshot,
    snapshotNarHash: predecessorCommon.snapshotNarHash,
    snapshotNarSize: predecessorCommon.snapshotNarSize,
    snapshotRunnerSha256: predecessorCommon.snapshotRunnerSha256,
    trackedPathsSha256: "1".repeat(64),
    snapshotEvidenceSha256: "2".repeat(64),
    snapshotGcRoot: predecessorGcRoot,
  });
  const compiledPath = join(predecessorRun, "compiled-mvp-report.json");
  await writeJson(compiledPath, failedReport(predecessorSnapshot));
  const gate0Path = join(predecessorRun, "gate0", "gate0-report.json");
  const gate1Path = join(predecessorRun, "gate1", "gate1-report.json");
  await writeMode(gate0Path, '{"result":"PASS"}\n');
  await writeMode(gate1Path, '{"result":"FAIL"}\n');

  const legacyAudit = {
    gitCommit: predecessorCommon.gitCommit,
    snapshotNarHash: predecessorCommon.snapshotNarHash,
    snapshotNarSize: predecessorCommon.snapshotNarSize,
    snapshotRunnerSha256: predecessorCommon.snapshotRunnerSha256,
    runtimeManifestSha256: predecessorCommon.runtimeManifestSha256,
    compiledReportSha256: sha256(await readFile(compiledPath)),
    gate0ReportSha256: sha256(await readFile(gate0Path)),
    gate1ReportSha256: sha256(await readFile(gate1Path)),
  };
  const prePublicWriteAudit = {
    gitCommit: predecessorCommon.gitCommit,
    snapshotNarHash: predecessorCommon.snapshotNarHash,
    snapshotNarSize: predecessorCommon.snapshotNarSize,
    snapshotRunnerSha256: predecessorCommon.snapshotRunnerSha256,
    runtimeManifestSha256: predecessorCommon.runtimeManifestSha256,
    compiledReportSha256: sha256(await readFile(compiledPath)),
    gate3ReportSha256: "e".repeat(64),
    gate3Failure: "production LEZ a: rejected=lez-network-fingerprint",
    retirementStatus: "audited-fingerprint-rejection",
  };
  let timestamp = 1_700_000_001_000;
  let lockChecks = 0;
  let processScans = 0;
  let scopeRetirements = 0;
  let processResults = [[]];
  let lockFailure;
  let retirementHook;
  const lifecycle = createClaimLifecycle({
    uid,
    claimDirectory,
    claimPath,
    common: successorCommon,
    now: () => timestamp++,
    assertReleaseLockHeld: async () => {
      lockChecks += 1;
      if (lockFailure) throw lockFailure;
    },
    scanClaimBoundProcesses: async () => {
      processScans += 1;
      return processResults.length > 1
        ? processResults.shift()
        : processResults[0];
    },
    retirePredecessorScope: async (input) => {
      scopeRetirements += 1;
      await retirementHook?.(input, scopeRetirements);
    },
    validateImmutableSnapshot: async () => {},
    completedReportSha256: async () => completedSha256,
    writeCompletionRecord: async () =>
      join(successorRun, "active-claim-completion.json"),
    legacyAudit,
    prePublicWriteAudits: [
      ...additionalPrePublicWriteAudits,
      prePublicWriteAudit,
    ],
  });
  return {
    root,
    claimDirectory,
    claimPath,
    predecessorRun,
    successorRun,
    predecessorClaim,
    prePublicWriteAudit,
    successorCommon,
    lifecycle,
    counters: {
      lockChecks: () => lockChecks,
      processScans: () => processScans,
      scopeRetirements: () => scopeRetirements,
    },
    setProcessResult(value) {
      processResults = [value];
    },
    setProcessResults(value) {
      processResults = value;
    },
    setLockFailure(value) {
      lockFailure = value;
    },
    setRetirementHook(value) {
      retirementHook = value;
    },
    setTimestamp(value) {
      timestamp = value;
    },
  };
}

async function withFixture(options, operation) {
  const value = await fixture(options);
  try {
    return await operation(value);
  } finally {
    await rm(value.root, { recursive: true, force: true });
  }
}

test("process scope names require one exact shared token", () => {
  assert.deepEqual(
    validateProcessScopeNames(processScopeSlice, processScopePrefix),
    { processScopeSlice, processScopePrefix },
  );
  assert.throws(
    () => validateProcessScopeNames(
      "logos-palace-run-a1b2C3d4.slice",
      "logos-palace-run-different",
    ),
    /process scope names are invalid/,
  );
  assert.throws(
    () => validateProcessScopeNames(
      "/user.slice/logos-palace-run-a1b2C3d4.slice",
      processScopePrefix,
    ),
    /process scope names are invalid/,
  );
});

test("atomically archives and rolls active v2 pre-Gate 3 claim", async () => {
  await withFixture({}, async ({
    root,
    claimPath,
    predecessorClaim,
    successorRun,
    successorCommon,
    lifecycle,
    counters,
  }) => {
    const priorBytes = await readFile(claimPath);
    const acquired = await lifecycle.execute("acquire-or-roll-forward");
    assert.equal(acquired.output, claimPath);
    assert.equal(acquired.claim.status, "active-pre-gate3");
    assert.equal(acquired.claim.gitCommit, successorCommon.gitCommit);
    assert.equal(counters.lockChecks(), 2);
    assert.equal(counters.processScans(), 2);
    assert.equal(counters.scopeRetirements(), 2);
    assert.deepEqual(
      JSON.parse(
        await readFile(
          join(successorRun, "retired-active-claim.json"),
          "utf8",
        ),
      ),
      predecessorClaim,
    );
    const evidenceBytes = await readFile(
      join(successorRun, "claim-roll-forward.json"),
    );
    const evidence = JSON.parse(evidenceBytes);
    assert.equal(evidence.status, "retired-before-gate3");
    assert.equal(evidence.proof.claimBoundProcessCount, 0);
    assert.equal(evidence.proof.gate3ClaimState, "never-entered");
    assert.equal(JSON.stringify(evidence).includes(root), false);
    assert.equal(
      acquired.claim.rollForward.evidenceSha256,
      sha256(evidenceBytes),
    );
    assert.equal(
      acquired.claim.rollForward.retiredClaimArchiveSha256,
      sha256(priorBytes),
    );

    const repeated = await lifecycle.execute("acquire-or-roll-forward");
    assert.deepEqual(repeated.claim, acquired.claim);
    assert.equal(counters.lockChecks(), 3);
    assert.equal(counters.processScans(), 2);
  });
});

test("rolls only proven-safe non-gate failure phases forward", async () => {
  for (const phase of [
    "signal",
    "run-scope",
    "active-run-claim",
    "gate3-claim-transition",
  ]) {
    await withFixture({}, async ({
      predecessorClaim,
      predecessorRun,
      lifecycle,
    }) => {
      await writeJson(
        join(predecessorRun, "compiled-mvp-report.json"),
        failedReport(predecessorClaim.productSnapshot, phase),
      );
      const acquired = await lifecycle.execute("acquire-or-roll-forward");
      assert.equal(acquired.claim.status, "active-pre-gate3");
      assert.equal(
        JSON.parse(
          await readFile(
            join(acquired.claim.runDirectory, "claim-roll-forward.json"),
            "utf8",
          ),
        ).predecessor.failurePhase,
        phase,
      );
    });
  }

  await withFixture({}, async ({
    predecessorClaim,
    predecessorRun,
    lifecycle,
  }) => {
    await writeJson(
      join(predecessorRun, "compiled-mvp-report.json"),
      failedReport(predecessorClaim.productSnapshot, "resume-required"),
    );
    await assert.rejects(
      lifecycle.execute("acquire-or-roll-forward"),
      /pre-Gate 3 failed compiled report is invalid/,
    );
  });
});

test("supports exact audited legacy v1 predecessor only", async () => {
  await withFixture({ legacy: true }, async ({
    claimPath,
    successorRun,
    lifecycle,
  }) => {
    const acquired = await lifecycle.execute("acquire-or-roll-forward");
    assert.equal(acquired.claim.status, "active-pre-gate3");
    const evidence = JSON.parse(
      await readFile(join(successorRun, "claim-roll-forward.json"), "utf8"),
    );
    assert.equal(evidence.predecessor.claimVersion, 1);
    assert.equal(evidence.predecessor.failurePhase, "gate1");
    assert.equal(
      sha256(await readFile(join(successorRun, "retired-active-claim.json"))),
      acquired.claim.rollForward.retiredClaimArchiveSha256,
    );
    assert.equal(acquired.output, claimPath);
  });
});

test("reopens exact roll-forward files before accepting current claim", async () => {
  for (const file of [
    "claim-roll-forward.json",
    "retired-active-claim.json",
  ]) {
    await withFixture({}, async ({
      successorRun,
      lifecycle,
    }) => {
      await lifecycle.execute("acquire-or-roll-forward");
      await rm(join(successorRun, file));
      await assert.rejects(
        lifecycle.execute("state"),
        /ENOENT|roll-forward/,
      );
    });
  }

  await withFixture({}, async ({
    claimPath,
    successorRun,
    lifecycle,
  }) => {
    const acquired = await lifecycle.execute("acquire-or-roll-forward");
    const evidencePath = join(successorRun, "claim-roll-forward.json");
    const evidence = JSON.parse(await readFile(evidencePath, "utf8"));
    evidence.proof.claimBoundProcessCount = 1;
    await writeJson(evidencePath, evidence);
    const claim = acquired.claim;
    claim.rollForward.evidenceSha256 = sha256(await readFile(evidencePath));
    await writeJson(claimPath, claim);
    await assert.rejects(
      lifecycle.execute("state"),
      /roll-forward evidence is invalid/,
    );
  });

  await withFixture({}, async ({
    successorRun,
    lifecycle,
  }) => {
    await lifecycle.execute("acquire-or-roll-forward");
    await writeMode(
      join(successorRun, "claim-roll-forward.json"),
      '{"changed":true}\n',
    );
    await assert.rejects(
      lifecycle.execute("enter-gate3"),
      /roll-forward/,
    );
  });
});

test("rejects legacy report bytes outside exact audit", async () => {
  await withFixture({ legacy: true }, async ({
    predecessorRun,
    lifecycle,
  }) => {
    await writeMode(
      join(predecessorRun, "gate1", "gate1-report.json"),
      '{"result":"FAIL","changed":true}\n',
    );
    await assert.rejects(
      lifecycle.execute("acquire-or-roll-forward"),
      /gate reports are not the audited reports/,
    );
  });
});

test("rejects claim-bound process before writing retirement evidence", async () => {
  await withFixture({}, async ({
    claimPath,
    successorRun,
    lifecycle,
    setProcessResult,
  }) => {
    const before = await readFile(claimPath);
    setProcessResult([{ pid: 77 }]);
    await assert.rejects(
      lifecycle.execute("acquire-or-roll-forward"),
      /retains claim-bound processes/,
    );
    assert.deepEqual(await readFile(claimPath), before);
    await assert.rejects(
      readFile(join(successorRun, "claim-roll-forward.json")),
      (error) => error?.code === "ENOENT",
    );
  });
});

test("second process scan brackets atomic replacement and retry reuses archive", async () => {
  await withFixture({}, async ({
    claimPath,
    predecessorClaim,
    successorRun,
    lifecycle,
    setProcessResult,
    setProcessResults,
  }) => {
    setProcessResults([[], [{ pid: 88 }]]);
    await assert.rejects(
      lifecycle.execute("acquire-or-roll-forward"),
      /retains claim-bound processes/,
    );
    assert.deepEqual(JSON.parse(await readFile(claimPath)), predecessorClaim);
    assert.equal(
      JSON.parse(
        await readFile(
          join(successorRun, "claim-roll-forward.json"),
          "utf8",
        ),
      ).status,
      "retired-before-gate3",
    );

    setProcessResult([]);
    const acquired = await lifecycle.execute("acquire-or-roll-forward");
    assert.equal(acquired.claim.status, "active-pre-gate3");
    assert.equal(acquired.claim.gitCommit, successorCommit);
  });
});

test("requires release lock before predecessor retirement", async () => {
  await withFixture({}, async ({
    claimPath,
    lifecycle,
    setLockFailure,
  }) => {
    const before = await readFile(claimPath);
    setLockFailure(new Error("release lock unavailable"));
    await assert.rejects(
      lifecycle.execute("acquire-or-roll-forward"),
      /release lock unavailable/,
    );
    assert.deepEqual(await readFile(claimPath), before);
  });
});

test("rejects any Gate 3 artifact and a gate3-entered predecessor", async () => {
  await withFixture({}, async ({
    predecessorRun,
    lifecycle,
  }) => {
    await mkdir(join(predecessorRun, "gate3"), { mode: 0o700 });
    await assert.rejects(
      lifecycle.execute("acquire-or-roll-forward"),
      /Gate 3 evidence must be absent/,
    );
  });
  await withFixture({}, async ({
    claimPath,
    predecessorClaim,
    lifecycle,
  }) => {
    await writeJson(claimPath, {
      ...predecessorClaim,
      status: "gate3-entered",
      gate3EnteredAtUnixMs: predecessorClaim.createdAtUnixMs + 1,
    });
    await assert.rejects(
      lifecycle.execute("acquire-or-roll-forward"),
      /already crossed Gate 3|claim v2 state is invalid|audited|ENOENT/,
    );
  });
});

test("rolls forward only the matching audited Gate 3 pre-public-write failure", async () => {
  await withFixture({
    additionalPrePublicWriteAudits: [{
      gitCommit: "f".repeat(40),
      snapshotNarHash: narHash,
      snapshotNarSize: 4096,
      snapshotRunnerSha256: runnerSha256,
      runtimeManifestSha256: "0".repeat(64),
      compiledReportSha256: "1".repeat(64),
      gate3ReportSha256: "2".repeat(64),
      gate3Failure: "unmatched pre-public-write failure",
      retirementStatus: "audited-pre-public-write-failure",
    }],
  }, async ({
    claimPath,
    predecessorClaim,
    predecessorRun,
    successorRun,
    prePublicWriteAudit,
    lifecycle,
  }) => {
    const compiledPath = join(predecessorRun, "compiled-mvp-report.json");
    await writeJson(
      compiledPath,
      failedReport(predecessorClaim.productSnapshot, "gate3"),
    );
    const gate3Directory = join(predecessorRun, "gate3");
    await mkdir(gate3Directory, { mode: 0o700 });
    const gate3Path = join(gate3Directory, "gate3-report.json");
    await writeJson(
      gate3Path,
      auditedPrePublicWriteGate3Report(predecessorClaim),
    );
    prePublicWriteAudit.compiledReportSha256 = sha256(
      await readFile(compiledPath),
    );
    prePublicWriteAudit.gate3ReportSha256 = sha256(
      await readFile(gate3Path),
    );
    await writeJson(claimPath, {
      ...predecessorClaim,
      status: "gate3-entered",
      gate3EnteredAtUnixMs: predecessorClaim.createdAtUnixMs + 1,
    });

    const acquired = await lifecycle.execute("acquire-or-roll-forward");
    assert.equal(acquired.claim.status, "active-pre-gate3");
    const certificate = JSON.parse(
      await readFile(
        join(predecessorRun, "pre-public-write-gate3-retirement.json"),
        "utf8",
      ),
    );
    assert.equal(certificate.status, "audited-fingerprint-rejection");
    const evidence = JSON.parse(
      await readFile(join(successorRun, "claim-roll-forward.json"), "utf8"),
    );
    assert.equal(evidence.version, 2);
    assert.equal(evidence.status, "retired-pre-public-write");
    assert.equal(
      evidence.proof.gate3Artifacts,
      "audited-fingerprint-rejection",
    );
    assert.equal(
      (await lifecycle.execute("state")).output,
      "active-pre-gate3",
    );
  });
});

test("rolls forward exact identity and idle-storage state before a Palace write", async () => {
  await withFixture({}, async ({
    claimPath,
    predecessorClaim,
    predecessorRun,
    successorRun,
    prePublicWriteAudit,
    lifecycle,
  }) => {
    const compiledPath = join(predecessorRun, "compiled-mvp-report.json");
    await writeJson(
      compiledPath,
      failedReport(predecessorClaim.productSnapshot, "gate3"),
    );
    const gate3Directory = join(predecessorRun, "gate3");
    await mkdir(gate3Directory, { mode: 0o700 });
    const gate3Path = join(gate3Directory, "gate3-report.json");
    await writeJson(
      gate3Path,
      identityRegistrationAndIdleStorageGate3Report(predecessorClaim),
    );
    prePublicWriteAudit.reportProfile =
      "identity-registration-and-idle-storage-before-palace-write";
    prePublicWriteAudit.retirementStatus =
      "audited-pre-public-write-failure";
    prePublicWriteAudit.gate3Failure =
      "worker a: asset picker opened multiple dialogs";
    prePublicWriteAudit.compiledReportSha256 = sha256(
      await readFile(compiledPath),
    );
    prePublicWriteAudit.gate3ReportSha256 = sha256(
      await readFile(gate3Path),
    );
    await writeJson(claimPath, {
      ...predecessorClaim,
      status: "gate3-entered",
      gate3EnteredAtUnixMs: predecessorClaim.createdAtUnixMs + 1,
    });

    const acquired = await lifecycle.execute("acquire-or-roll-forward");
    assert.equal(acquired.claim.status, "active-pre-gate3");
    const certificate = JSON.parse(
      await readFile(
        join(predecessorRun, "pre-public-write-gate3-retirement.json"),
        "utf8",
      ),
    );
    assert.equal(certificate.status, "audited-pre-public-write-failure");
    const evidence = JSON.parse(
      await readFile(join(successorRun, "claim-roll-forward.json"), "utf8"),
    );
    assert.equal(
      evidence.proof.gate3Artifacts,
      "audited-pre-public-write-failure",
    );
  });
});

test("rolls forward exact approval-guarded staged assets before a Palace write", async () => {
  await withFixture({}, async ({
    claimPath,
    predecessorClaim,
    predecessorRun,
    successorRun,
    prePublicWriteAudit,
    lifecycle,
  }) => {
    const compiledPath = join(predecessorRun, "compiled-mvp-report.json");
    await writeJson(
      compiledPath,
      failedReport(predecessorClaim.productSnapshot, "gate3"),
    );
    const gate3Directory = join(predecessorRun, "gate3");
    await mkdir(gate3Directory, { mode: 0o700 });
    const gate3Path = join(gate3Directory, "gate3-report.json");
    await writeJson(
      gate3Path,
      identityRegistrationAndApprovalGuardedAssetsGate3Report(
        predecessorClaim,
      ),
    );
    prePublicWriteAudit.reportProfile =
      "identity-registration-and-approval-guarded-assets-before-palace-write";
    prePublicWriteAudit.retirementStatus =
      "audited-pre-public-write-failure";
    prePublicWriteAudit.gate3Failure =
      "worker a: moderation asset upload failed";
    prePublicWriteAudit.compiledReportSha256 = sha256(
      await readFile(compiledPath),
    );
    prePublicWriteAudit.gate3ReportSha256 = sha256(
      await readFile(gate3Path),
    );
    await writeJson(claimPath, {
      ...predecessorClaim,
      status: "gate3-entered",
      gate3EnteredAtUnixMs: predecessorClaim.createdAtUnixMs + 1,
    });

    const acquired = await lifecycle.execute("acquire-or-roll-forward");
    assert.equal(acquired.claim.status, "active-pre-gate3");
    const certificate = JSON.parse(
      await readFile(
        join(predecessorRun, "pre-public-write-gate3-retirement.json"),
        "utf8",
      ),
    );
    assert.equal(certificate.status, "audited-pre-public-write-failure");
    const evidence = JSON.parse(
      await readFile(join(successorRun, "claim-roll-forward.json"), "utf8"),
    );
    assert.equal(
      evidence.proof.gate3Artifacts,
      "audited-pre-public-write-failure",
    );
  });
});

test("rolls forward sealed storage retained after creator shutdown before a Palace write", async () => {
  await withFixture({}, async ({
    claimPath,
    predecessorClaim,
    predecessorRun,
    successorRun,
    prePublicWriteAudit,
    lifecycle,
  }) => {
    const compiledPath = join(predecessorRun, "compiled-mvp-report.json");
    await writeJson(
      compiledPath,
      failedReport(predecessorClaim.productSnapshot, "gate3"),
    );
    const gate3Directory = join(predecessorRun, "gate3");
    await mkdir(gate3Directory, { mode: 0o700 });
    const gate3Path = join(gate3Directory, "gate3-report.json");
    const report = sealedBundleRetainedAfterCreatorOfflineGate3Report(
      predecessorClaim,
    );
    await writeJson(gate3Path, report);
    prePublicWriteAudit.reportProfile =
      "identity-registration-and-sealed-mvp-bundle-retained-after-creator-offline-before-palace-write";
    prePublicWriteAudit.retirementStatus =
      "audited-pre-public-write-failure";
    prePublicWriteAudit.gate3Failure = report.failure;
    prePublicWriteAudit.compiledReportSha256 = sha256(
      await readFile(compiledPath),
    );
    prePublicWriteAudit.gate3ReportSha256 = sha256(
      await readFile(gate3Path),
    );
    await writeJson(claimPath, {
      ...predecessorClaim,
      status: "gate3-entered",
      gate3EnteredAtUnixMs: predecessorClaim.createdAtUnixMs + 1,
    });

    const acquired = await lifecycle.execute("acquire-or-roll-forward");
    assert.equal(acquired.claim.status, "active-pre-gate3");
    const certificate = JSON.parse(
      await readFile(
        join(predecessorRun, "pre-public-write-gate3-retirement.json"),
        "utf8",
      ),
    );
    assert.equal(certificate.status, "audited-pre-public-write-failure");
    const evidence = JSON.parse(
      await readFile(join(successorRun, "claim-roll-forward.json"), "utf8"),
    );
    assert.equal(evidence.status, "retired-pre-public-write");
  });
});

test("rolls forward audited completed Gate 3 strict-evidence rejection before a Palace write", async () => {
  await withFixture({}, async ({
    claimPath,
    predecessorClaim,
    predecessorRun,
    successorRun,
    prePublicWriteAudit,
    lifecycle,
  }) => {
    const compiledPath = join(predecessorRun, "compiled-mvp-report.json");
    const compiled = failedReport(predecessorClaim.productSnapshot, "gate3");
    compiled.failure.message = "gate report failed strict validation";
    await writeJson(compiledPath, compiled);
    const gate3Directory = join(predecessorRun, "gate3");
    await mkdir(gate3Directory, { mode: 0o700 });
    const gate3Path = join(gate3Directory, "gate3-report.json");
    await writeJson(
      gate3Path,
      completedGate3StrictEvidenceRejectionReport(predecessorClaim),
    );
    prePublicWriteAudit.reportProfile =
      "completed-gate3-strict-evidence-rejection-before-palace-write";
    prePublicWriteAudit.retirementStatus = "audited-strict-evidence-rejection";
    prePublicWriteAudit.gate3Failure = "gate report failed strict validation";
    prePublicWriteAudit.compiledReportSha256 = sha256(
      await readFile(compiledPath),
    );
    prePublicWriteAudit.gate3ReportSha256 = sha256(
      await readFile(gate3Path),
    );
    await writeJson(claimPath, {
      ...predecessorClaim,
      status: "gate3-entered",
      gate3EnteredAtUnixMs: predecessorClaim.createdAtUnixMs + 1,
    });

    const acquired = await lifecycle.execute("acquire-or-roll-forward");
    assert.equal(acquired.claim.status, "active-pre-gate3");
    const certificate = JSON.parse(
      await readFile(
        join(predecessorRun, "pre-public-write-gate3-retirement.json"),
        "utf8",
      ),
    );
    assert.equal(certificate.status, "audited-strict-evidence-rejection");
    const evidence = JSON.parse(
      await readFile(join(successorRun, "claim-roll-forward.json"), "utf8"),
    );
    assert.equal(evidence.status, "retired-pre-public-write");
  });
});

test("pins the exact audited direct-degradation Gate 3 rejection", () => {
  assert.deepEqual(
    auditedPrePublicWriteGate3Failures.find((audit) =>
      audit.gitCommit === "398c71e9d8bff6a2c5e9d692e3db22c95fb0c8a0"
    ),
    {
      gitCommit: "398c71e9d8bff6a2c5e9d692e3db22c95fb0c8a0",
      snapshotNarHash:
        "sha256-4NV82UhBVk9g5hjMJDIUIojijCwLmftSqFMiGQfiLT0=",
      snapshotNarSize: 7393464,
      snapshotRunnerSha256:
        "5de670ea699bb1226a7a3176e77cc0fd8f0938817a6cc837a55507243f9b1a08",
      runtimeManifestSha256:
        "35c0d419be617d9e86a430bef1f831e82040c9682014e7af1936b63ffa0811dc",
      compiledReportSha256:
        "cd8775a91b1dd97a5879b5d4f410f41dd055348b01cc267b0ccb497385f38b8b",
      gate3ReportSha256:
        "34300a1ffc26f4b57b98b9cd5a55020fe9982f79488ce0dfc7a34ae3594bb57f",
      gate3Failure: "gate report failed strict validation",
      retirementStatus: "audited-strict-evidence-rejection",
      reportProfile:
        "completed-gate3-strict-evidence-rejection-before-palace-write",
    },
  );
});

test("rejects post-creator retention evidence with a catalog CID mismatch", async () => {
  await withFixture({}, async ({
    claimPath,
    predecessorClaim,
    predecessorRun,
    prePublicWriteAudit,
    lifecycle,
  }) => {
    const compiledPath = join(predecessorRun, "compiled-mvp-report.json");
    await writeJson(
      compiledPath,
      failedReport(predecessorClaim.productSnapshot, "gate3"),
    );
    const gate3Directory = join(predecessorRun, "gate3");
    await mkdir(gate3Directory, { mode: 0o700 });
    const gate3Path = join(gate3Directory, "gate3-report.json");
    const report = sealedBundleRetainedAfterCreatorOfflineGate3Report(
      predecessorClaim,
    );
    report.providerBRetentionProofs[1].retained[0].cid =
      `z${"b".repeat(50)}`;
    await writeJson(gate3Path, report);
    prePublicWriteAudit.reportProfile =
      "identity-registration-and-sealed-mvp-bundle-retained-after-creator-offline-before-palace-write";
    prePublicWriteAudit.retirementStatus =
      "audited-pre-public-write-failure";
    prePublicWriteAudit.gate3Failure = report.failure;
    prePublicWriteAudit.compiledReportSha256 = sha256(
      await readFile(compiledPath),
    );
    prePublicWriteAudit.gate3ReportSha256 = sha256(
      await readFile(gate3Path),
    );
    await writeJson(claimPath, {
      ...predecessorClaim,
      status: "gate3-entered",
      gate3EnteredAtUnixMs: predecessorClaim.createdAtUnixMs + 1,
    });

    await assert.rejects(
      lifecycle.execute("acquire-or-roll-forward"),
      /pre-public-write report is invalid/,
    );
  });
});

test("rejects identity and idle-storage recovery after any staged asset", async () => {
  await withFixture({}, async ({
    claimPath,
    predecessorClaim,
    predecessorRun,
    prePublicWriteAudit,
    lifecycle,
  }) => {
    const compiledPath = join(predecessorRun, "compiled-mvp-report.json");
    await writeJson(
      compiledPath,
      failedReport(predecessorClaim.productSnapshot, "gate3"),
    );
    const gate3Directory = join(predecessorRun, "gate3");
    await mkdir(gate3Directory, { mode: 0o700 });
    const gate3Path = join(gate3Directory, "gate3-report.json");
    const report = identityRegistrationAndIdleStorageGate3Report(predecessorClaim);
    report.assetAuthoring.assets = [{ handle: "a".repeat(64) }];
    await writeJson(gate3Path, report);
    prePublicWriteAudit.reportProfile =
      "identity-registration-and-idle-storage-before-palace-write";
    prePublicWriteAudit.retirementStatus =
      "audited-pre-public-write-failure";
    prePublicWriteAudit.gate3Failure =
      "worker a: asset picker opened multiple dialogs";
    prePublicWriteAudit.compiledReportSha256 = sha256(
      await readFile(compiledPath),
    );
    prePublicWriteAudit.gate3ReportSha256 = sha256(
      await readFile(gate3Path),
    );
    await writeJson(claimPath, {
      ...predecessorClaim,
      status: "gate3-entered",
      gate3EnteredAtUnixMs: predecessorClaim.createdAtUnixMs + 1,
    });

    await assert.rejects(
      lifecycle.execute("acquire-or-roll-forward"),
      /pre-public-write report is invalid/,
    );
  });
});

test("rejects approval-guarded recovery after an asset publication field", async () => {
  await withFixture({}, async ({
    claimPath,
    predecessorClaim,
    predecessorRun,
    prePublicWriteAudit,
    lifecycle,
  }) => {
    const compiledPath = join(predecessorRun, "compiled-mvp-report.json");
    await writeJson(
      compiledPath,
      failedReport(predecessorClaim.productSnapshot, "gate3"),
    );
    const gate3Directory = join(predecessorRun, "gate3");
    await mkdir(gate3Directory, { mode: 0o700 });
    const gate3Path = join(gate3Directory, "gate3-report.json");
    const report = identityRegistrationAndApprovalGuardedAssetsGate3Report(
      predecessorClaim,
    );
    report.assetAuthoring.assets[0].publication = { cid: "forbidden" };
    await writeJson(gate3Path, report);
    prePublicWriteAudit.reportProfile =
      "identity-registration-and-approval-guarded-assets-before-palace-write";
    prePublicWriteAudit.retirementStatus =
      "audited-pre-public-write-failure";
    prePublicWriteAudit.gate3Failure =
      "worker a: moderation asset upload failed";
    prePublicWriteAudit.compiledReportSha256 = sha256(
      await readFile(compiledPath),
    );
    prePublicWriteAudit.gate3ReportSha256 = sha256(
      await readFile(gate3Path),
    );
    await writeJson(claimPath, {
      ...predecessorClaim,
      status: "gate3-entered",
      gate3EnteredAtUnixMs: predecessorClaim.createdAtUnixMs + 1,
    });

    await assert.rejects(
      lifecycle.execute("acquire-or-roll-forward"),
      /pre-public-write report is invalid/,
    );
  });
});

test("rejects altered audited Gate 3 pre-public-write evidence", async () => {
  await withFixture({}, async ({
    claimPath,
    predecessorClaim,
    predecessorRun,
    prePublicWriteAudit,
    lifecycle,
  }) => {
    const compiledPath = join(predecessorRun, "compiled-mvp-report.json");
    await writeJson(
      compiledPath,
      failedReport(predecessorClaim.productSnapshot, "gate3"),
    );
    const gate3Directory = join(predecessorRun, "gate3");
    await mkdir(gate3Directory, { mode: 0o700 });
    const gate3Path = join(gate3Directory, "gate3-report.json");
    const report = auditedPrePublicWriteGate3Report(predecessorClaim);
    report.identities = { a: { unexpected: true } };
    await writeJson(gate3Path, report);
    prePublicWriteAudit.compiledReportSha256 = sha256(
      await readFile(compiledPath),
    );
    prePublicWriteAudit.gate3ReportSha256 = sha256(
      await readFile(gate3Path),
    );
    await writeJson(claimPath, {
      ...predecessorClaim,
      status: "gate3-entered",
      gate3EnteredAtUnixMs: predecessorClaim.createdAtUnixMs + 1,
    });

    await assert.rejects(
      lifecycle.execute("acquire-or-roll-forward"),
      /pre-public-write report is invalid/,
    );
  });
});

test("enforces durable Gate 3 transition before verify and complete", async () => {
  const value = await fixture();
  try {
    await rm(value.claimPath);
    const acquired = await value.lifecycle.execute("acquire-or-roll-forward");
    assert.equal(acquired.claim.status, "active-pre-gate3");
    await assert.rejects(
      value.lifecycle.execute("verify"),
      /has not entered Gate 3/,
    );
    await assert.rejects(
      value.lifecycle.execute("complete"),
      /cannot complete/,
    );

    const entered = await value.lifecycle.execute("enter-gate3");
    assert.equal(entered.claim.status, "gate3-entered");
    assert.equal(
      (await value.lifecycle.execute("state")).output,
      "gate3-entered",
    );
    assert.equal(
      (await value.lifecycle.execute("verify")).output,
      value.claimPath,
    );
    assert.deepEqual(
      (await value.lifecycle.execute("enter-gate3")).claim,
      entered.claim,
    );

    const completed = await value.lifecycle.execute("complete");
    assert.equal(completed.claim.status, "completed");
    assert.equal(completed.claim.compiledReportSha256, completedSha256);
    assert.equal(
      (await value.lifecycle.execute("state")).output,
      "completed",
    );
    assert.equal(
      (await value.lifecycle.execute("completion")).output,
      join(value.successorRun, "active-claim-completion.json"),
    );
    await assert.rejects(
      value.lifecycle.execute("enter-gate3"),
      /cannot enter Gate 3/,
    );
    assert.equal(value.counters.lockChecks(), 9);
  } finally {
    await rm(value.root, { recursive: true, force: true });
  }
});

test("clamps rollback timestamps before durable state transitions", async () => {
  const value = await fixture();
  try {
    await rm(value.claimPath);
    const acquired = await value.lifecycle.execute("acquire-or-roll-forward");
    value.setTimestamp(1);
    const entered = await value.lifecycle.execute("enter-gate3");
    assert.equal(
      entered.claim.gate3EnteredAtUnixMs,
      acquired.claim.createdAtUnixMs,
    );
    value.setTimestamp(1);
    const completed = await value.lifecycle.execute("complete");
    assert.equal(
      completed.claim.completedAtUnixMs,
      entered.claim.gate3EnteredAtUnixMs,
    );
    assert.equal(
      (await value.lifecycle.execute("state")).output,
      "completed",
    );
  } finally {
    await rm(value.root, { recursive: true, force: true });
  }
});

test("rejects invalid creation timestamp before writing a claim", async () => {
  const value = await fixture();
  try {
    await rm(value.claimPath);
    value.setTimestamp(0);
    await assert.rejects(
      value.lifecycle.execute("acquire-or-roll-forward"),
      /transition timestamp is invalid/,
    );
    await assert.rejects(
      readFile(value.claimPath),
      (error) => error?.code === "ENOENT",
    );
  } finally {
    await rm(value.root, { recursive: true, force: true });
  }
});
