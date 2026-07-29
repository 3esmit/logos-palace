import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import test from "node:test";

import {
  findStandalonePalaceServerMatches,
  parseTcpListenTable,
  validateExactProcessInventory,
  validateExpectedExecutableMapping,
  validateExactTcpListenerOwnership,
} from "./basecamp_process_model.mjs";

const coreModules = [
  "capability_module",
  "delivery_module",
  "lez_core",
  "package_downloader",
  "package_manager",
  "palace_core",
  "palace_vm",
  "storage_module",
];

function sha256(value) {
  return createHash("sha256").update(value).digest("hex");
}

const basecampRoot =
  "/nix/store/22222222222222222222222222222222-basecamp";
const loaderPath =
  "/nix/store/11111111111111111111111111111111-glibc/lib/ld-linux-x86-64.so.2";
const loaderSha256 = sha256("pinned loader bytes");

function programPath(programArgument) {
  return `${basecampRoot}/bin/${programArgument}`;
}

function programSha256(programArgument) {
  return sha256(`pinned Basecamp bytes:${programArgument}`);
}

function programFileIdentity(programArgument) {
  return {
    device: "00:20",
    inode: String({
      ".LogosBasecamp.elf": 10,
      ".logos_host.elf": 11,
      ".ui-host.elf": 12,
    }[programArgument]),
  };
}

function modulePath(moduleName) {
  return `/run/exact-installed-lgx/${moduleName}/${moduleName}_plugin.so`;
}

function moduleSha256(moduleName) {
  return sha256(`pinned module bytes:${moduleName}`);
}

function moduleFileIdentity(moduleName) {
  return {
    device: "00:30",
    inode: String(100 + [
      ...coreModules,
      "logos_palace_ui",
    ].indexOf(moduleName)),
  };
}

function executableMapping(path, identity) {
  return {
    path,
    permissions: "r-xp",
    ...identity,
  };
}

const loaderFileIdentity = Object.freeze({
  device: "00:10",
  inode: "1",
});

function artifactKey(programArgument, moduleName) {
  if (programArgument === ".LogosBasecamp.elf") return "basecamp-main:";
  if (programArgument === ".ui-host.elf") {
    return `ui-module-host:${moduleName}`;
  }
  return `core-module-host:${moduleName}`;
}

function expectedArtifacts(loader = true) {
  return Object.fromEntries([
    [".LogosBasecamp.elf", undefined],
    ...coreModules.map((moduleName) => [".logos_host.elf", moduleName]),
    [".ui-host.elf", "logos_palace_ui"],
  ].map(([programArgument, moduleName]) => [
    artifactKey(programArgument, moduleName),
    {
      programArgument,
      programPath: programPath(programArgument),
      programSha256: programSha256(programArgument),
      programDevice: programFileIdentity(programArgument).device,
      programInode: programFileIdentity(programArgument).inode,
      executionMode: loader ? "fallback" : "direct",
      loaderPath,
      loaderArgument: loader ? "ld-linux-x86-64.so.2" : null,
      loaderExecutable: "ld-linux-x86-64.so.2",
      loaderSha256,
      loaderDevice: loaderFileIdentity.device,
      loaderInode: loaderFileIdentity.inode,
      modulePath: moduleName ? modulePath(moduleName) : null,
      moduleSha256: moduleName ? moduleSha256(moduleName) : null,
      moduleDevice:
        moduleName ? moduleFileIdentity(moduleName).device : null,
      moduleInode:
        moduleName ? moduleFileIdentity(moduleName).inode : null,
    },
  ]));
}

function processIdentity(pid, programArgument, moduleName, loader = true) {
  const programArgumentPath = programPath(programArgument);
  const executableArgumentPath = loader
    ? loaderPath
    : programArgumentPath;
  const executablePath = executableArgumentPath;
  const executableArgument = loader
    ? "ld-linux-x86-64.so.2"
    : programArgument;
  const programIdentity = programFileIdentity(programArgument);
  const moduleIdentity =
    moduleName ? moduleFileIdentity(moduleName) : null;
  return {
    pid,
    name: programArgument,
    executable: executableArgument,
    executableArgument,
    programArgument,
    executablePath,
    executableArgumentPath,
    programArgumentPath,
    executableSha256: loader
      ? loaderSha256
      : programSha256(programArgument),
    executableArgumentSha256: loader
      ? loaderSha256
      : programSha256(programArgument),
    programArgumentSha256: programSha256(programArgument),
    executableFileIdentity:
      loader ? loaderFileIdentity : programIdentity,
    programArgumentFileIdentity: programIdentity,
    programExecutableMapping: executableMapping(
      programArgumentPath,
      programIdentity,
    ),
    directInterpreterPath: loader ? null : loaderPath,
    directInterpreterSha256: loader ? null : loaderSha256,
    directInterpreterFileIdentity:
      loader ? null : loaderFileIdentity,
    directInterpreterMapping: loader
      ? null
      : executableMapping(loaderPath, loaderFileIdentity),
    moduleArgumentPath: moduleName ? modulePath(moduleName) : null,
    moduleArgumentSha256:
      moduleName ? moduleSha256(moduleName) : null,
    moduleArtifactPath: moduleName ? modulePath(moduleName) : null,
    moduleArtifactSha256:
      moduleName ? moduleSha256(moduleName) : null,
    moduleArgumentFileIdentity: moduleIdentity,
    moduleExecutableMapping: moduleName
      ? executableMapping(modulePath(moduleName), moduleIdentity)
      : null,
    moduleName,
  };
}

function processes(loader = true) {
  return [
    processIdentity(100, ".LogosBasecamp.elf", undefined, loader),
    ...coreModules.map((moduleName, index) =>
      processIdentity(101 + index, ".logos_host.elf", moduleName, loader)),
    processIdentity(109, ".ui-host.elf", "logos_palace_ui", loader),
  ];
}

function tcpRow(slot, address, port, inode) {
  return [
    `${slot}:`,
    `${address}:${port.toString(16).padStart(4, "0").toUpperCase()}`,
    "00000000:0000",
    "0A",
    "00000000:00000000",
    "00:00000000",
    "00000000",
    "1000",
    "0",
    String(inode),
    "1",
  ].join(" ");
}

test("accepts exact Basecamp/module inventory and listener ownership", () => {
  const inventory = validateExactProcessInventory(
    processes(),
    100,
    expectedArtifacts(),
  );
  const table = [
    "sl local_address rem_address st tx_queue rx_queue tr tm->when retrnsmt uid timeout inode",
    tcpRow(0, "0100007F", 31000, 500),
    tcpRow(1, "0100007F", 32000, 501),
    tcpRow(2, "00000000", 33000, 502),
    tcpRow(3, "00000000", 22, 999),
  ].join("\n");
  const listeners = parseTcpListenTable(table, "tcp4");
  const socketOwners = new Map([
    ["500", new Set([100])],
    ["501", new Set([102])],
    ["502", new Set([108])],
  ]);
  const proof = validateExactTcpListenerOwnership(
    inventory,
    listeners,
    socketOwners,
    [
      {
        protocol: "tcp4",
        address: "127.0.0.1",
        port: 31000,
        ownerRole: "basecamp-main",
        moduleName: null,
        purpose: "qml-inspector",
      },
      {
        protocol: "tcp4",
        address: "127.0.0.1",
        port: 32000,
        ownerRole: "core-module-host",
        moduleName: "delivery_module",
        purpose: "delivery-transport",
      },
      {
        protocol: "tcp4",
        address: "0.0.0.0",
        port: 33000,
        ownerRole: "core-module-host",
        moduleName: "storage_module",
        purpose: "storage-transport",
      },
    ],
  );
  assert.equal(inventory.length, 10);
  assert.equal(proof.length, 3);
  assert.deepEqual(
    proof.map(({ purpose }) => purpose).sort(),
    ["delivery-transport", "qml-inspector", "storage-transport"],
  );
});

test("accepts direct executable identity without a dynamic loader argv", () => {
  assert.equal(
    validateExactProcessInventory(
      processes(false),
      100,
      expectedArtifacts(false),
    ).length,
    10,
  );
});

test("rejects forged argv that differs from proc executable identity", () => {
  const candidate = processes();
  candidate[0].executableArgument = ".LogosBasecamp.elf";
  assert.throws(
    () => validateExactProcessInventory(
      candidate,
      100,
      expectedArtifacts(),
    ),
    /execution mode differs from pinned wrapper selection/,
  );

  const loaderCandidate = processes();
  loaderCandidate[1].executable = ".logos_host.elf";
  assert.throws(
    () => validateExactProcessInventory(
      loaderCandidate,
      100,
      expectedArtifacts(),
    ),
    /execution mode differs from pinned wrapper selection/,
  );
});

test("rejects wrong direct path with the same argv basename and bytes", () => {
  const candidate = processes(false);
  const forgedPath = "/tmp/forged/.logos_host.elf";
  candidate[1].executablePath = forgedPath;
  candidate[1].executableArgumentPath = forgedPath;
  candidate[1].programArgumentPath = forgedPath;
  assert.equal(
    candidate[1].executable,
    candidate[1].programArgument,
  );
  assert.throws(
    () => validateExactProcessInventory(
      candidate,
      100,
      expectedArtifacts(false),
    ),
    /canonical executable path differs from pinned runtime artifact/,
  );
});

test("rejects wrong direct interpreter bytes at exact selected path", () => {
  const candidate = processes(false);
  candidate[0].directInterpreterSha256 =
    sha256("forged direct interpreter bytes");
  assert.throws(
    () => validateExactProcessInventory(
      candidate,
      100,
      expectedArtifacts(false),
    ),
    /execution mode differs from pinned wrapper selection/,
  );
});

test("rejects non-executable direct interpreter mapping", () => {
  const candidate = processes(false);
  candidate[0].directInterpreterMapping.permissions = "r--p";
  assert.throws(
    () => validateExactProcessInventory(
      candidate,
      100,
      expectedArtifacts(false),
    ),
    /canonical executable path differs from pinned runtime artifact/,
  );
});

test("rejects direct interpreter mapping from different inode", () => {
  const candidate = processes(false);
  candidate[0].directInterpreterMapping.inode = "2";
  assert.throws(
    () => validateExactProcessInventory(
      candidate,
      100,
      expectedArtifacts(false),
    ),
    /canonical executable path differs from pinned runtime artifact/,
  );
});

test("rejects wrong loader path with same basename and bytes", () => {
  const candidate = processes();
  const forgedLoaderPath =
    "/tmp/forged/ld-linux-x86-64.so.2";
  candidate[1].executablePath = forgedLoaderPath;
  candidate[1].executableArgumentPath = forgedLoaderPath;
  assert.equal(candidate[1].executable, "ld-linux-x86-64.so.2");
  assert.throws(
    () => validateExactProcessInventory(
      candidate,
      100,
      expectedArtifacts(),
    ),
    /canonical executable path differs from pinned runtime artifact/,
  );
});

test("rejects wrong loader bytes at exact selected path", () => {
  const candidate = processes();
  candidate[1].executableSha256 = sha256("forged loader bytes");
  candidate[1].executableArgumentSha256 =
    candidate[1].executableSha256;
  assert.throws(
    () => validateExactProcessInventory(
      candidate,
      100,
      expectedArtifacts(),
    ),
    /execution mode differs from pinned wrapper selection/,
  );
});

test("rejects fallback program without executable mapping", () => {
  const candidate = processes();
  candidate[0].programExecutableMapping = null;
  assert.throws(
    () => validateExactProcessInventory(
      candidate,
      100,
      expectedArtifacts(),
    ),
    /canonical executable path differs from pinned runtime artifact/,
  );
});

test("rejects mapped old program inode after exact-byte replacement", () => {
  const candidate = processes();
  const artifacts = expectedArtifacts();
  const replacementIdentity = { device: "00:20", inode: "999" };
  candidate[1].programArgumentFileIdentity = replacementIdentity;
  artifacts["core-module-host:capability_module"].programInode =
    replacementIdentity.inode;
  assert.equal(
    candidate[1].programArgumentSha256,
    artifacts["core-module-host:capability_module"].programSha256,
  );
  assert.throws(
    () => validateExactProcessInventory(candidate, 100, artifacts),
    /canonical executable path differs from pinned runtime artifact/,
  );
});

test("rejects wrong module argv path with expected module separately mapped", () => {
  const candidate = processes();
  candidate[1].moduleArgumentPath =
    "/tmp/forged/capability_module_plugin.so";
  assert.equal(
    validateExpectedExecutableMapping(
      [
        "7f100000-7f101000 r--p 00000000 00:20 123 "
          + modulePath("capability_module"),
        "7f101000-7f102000 r-xp 00001000 00:20 123 "
          + modulePath("capability_module"),
      ].join("\n"),
      modulePath("capability_module"),
    ).permissions,
    "r-xp",
  );
  assert.throws(
    () => validateExactProcessInventory(
      candidate,
      100,
      expectedArtifacts(),
    ),
    /canonical executable path differs from pinned runtime artifact/,
  );
});

test("rejects wrong module argv bytes at exact pinned path", () => {
  const candidate = processes();
  candidate[1].moduleArgumentSha256 =
    sha256("forged module argv bytes");
  assert.throws(
    () => validateExactProcessInventory(
      candidate,
      100,
      expectedArtifacts(),
    ),
    /differs from pinned runtime artifact/,
  );
});

test("rejects module mapping from different inode", () => {
  const candidate = processes();
  candidate[1].moduleExecutableMapping.inode = "999";
  assert.throws(
    () => validateExactProcessInventory(
      candidate,
      100,
      expectedArtifacts(),
    ),
    /canonical executable path differs from pinned runtime artifact/,
  );
});

test("requires exact non-deleted executable module mapping", () => {
  const expected = modulePath("palace_core");
  assert.equal(
    validateExpectedExecutableMapping(
      [
        `7f100000-7f101000 r--p 00000000 00:20 123 ${expected}`,
        `7f101000-7f102000 r-xp 00001000 00:20 123 ${expected}`,
      ].join("\n"),
      expected,
    ).inode,
    "123",
  );
  assert.throws(
    () => validateExpectedExecutableMapping(
      `7f100000-7f101000 r--p 00000000 00:20 123 ${expected}`,
      expected,
    ),
    /lacks exact executable mapping/,
  );
  assert.throws(
    () => validateExpectedExecutableMapping(
      `7f100000-7f101000 r-xp 00000000 00:20 123 ${expected}`,
      expected,
      { device: "00:20", inode: "124" },
    ),
    /lacks exact executable mapping/,
  );
  assert.throws(
    () => validateExpectedExecutableMapping(
      `7f100000-7f101000 r-xp 00000000 00:20 123 ${expected} (deleted)`,
      expected,
    ),
    /expected executable mapping is deleted/,
  );
});

test("rejects wrong bytes at exact pinned executable path", () => {
  const candidate = processes();
  candidate[1].programArgumentSha256 = sha256("forged program bytes");
  assert.throws(
    () => validateExactProcessInventory(
      candidate,
      100,
      expectedArtifacts(),
    ),
    /differs from pinned runtime artifact/,
  );
});

test("rejects extra standalone process even when its name looks related", () => {
  const candidate = processes();
  candidate.push(processIdentity(110, "palace-server", undefined, false));
  assert.throws(
    () => validateExactProcessInventory(
      candidate,
      100,
      expectedArtifacts(),
    ),
    /cardinality is not exact/,
  );
});

test("derives standalone Palace server match outside Basecamp group/session", () => {
  const candidate = processes();
  assert.deepEqual(findStandalonePalaceServerMatches(candidate), []);
  const detached = {
    pid: 9_999,
    processGroupId: 8_888,
    sessionId: 7_777,
    name: "node",
    executable: "node",
    executableArgument: "node",
    programArgument: "node",
    argumentBasenames: ["node", "logos-palace-server.js"],
  };
  assert.deepEqual(findStandalonePalaceServerMatches([
    ...candidate,
    detached,
  ]), [{
    pid: detached.pid,
    name: detached.name,
    executable: detached.executable,
    executableArgument: detached.executableArgument,
    programArgument: detached.programArgument,
    signatures: ["logos-palace-server.js"],
  }]);
});

test("rejects missing required Basecamp module host", () => {
  const candidate = processes().filter(
    ({ moduleName }) => moduleName !== "palace_vm",
  );
  assert.throws(
    () => validateExactProcessInventory(
      candidate,
      100,
      expectedArtifacts(),
    ),
    /cardinality is not exact/,
  );
});

test("rejects unexpected or wrongly owned TCP listener", () => {
  const inventory = validateExactProcessInventory(
    processes(),
    100,
    expectedArtifacts(),
  );
  const table = [
    "sl local_address rem_address st tx_queue rx_queue tr tm->when retrnsmt uid timeout inode",
    tcpRow(0, "0100007F", 31000, 500),
  ].join("\n");
  const listeners = parseTcpListenTable(table, "tcp4");
  const expected = [{
    protocol: "tcp4",
    address: "127.0.0.1",
    port: 31000,
    ownerRole: "basecamp-main",
    moduleName: null,
    purpose: "qml-inspector",
  }];
  assert.throws(
    () => validateExactTcpListenerOwnership(
      inventory,
      listeners,
      new Map([["500", new Set([102])]]),
      expected,
    ),
    /listener inventory is not exact/,
  );
  assert.throws(
    () => validateExactTcpListenerOwnership(
      inventory,
      [
        ...listeners,
        {
          protocol: "tcp4",
          address: "127.0.0.1",
          port: 32000,
          inode: "501",
        },
      ],
      new Map([
        ["500", new Set([100])],
        ["501", new Set([102])],
      ]),
      expected,
    ),
    /listener inventory is not exact/,
  );
});
