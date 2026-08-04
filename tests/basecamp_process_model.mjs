const coreModuleNames = Object.freeze([
  "capability_module",
  "delivery_module",
  "lez_core",
  "package_downloader",
  "package_manager",
  "palace_core",
  "palace_vm",
  "storage_module",
]);

const expectedIdentities = Object.freeze([
  "basecamp-main:",
  ...coreModuleNames.map((name) => `core-module-host:${name}`),
  "ui-module-host:logos_palace_ui",
]);

const dynamicLoaderPattern =
  /^ld(?:-[a-z0-9_-]+)?-linux[^/]*\.so(?:\.[0-9]+)*$/i;
const standalonePalaceServerPattern =
  /(?:^|[._-])palace[._-]?server(?:[._-]|$)/i;

function identity({ role, moduleName }) {
  return `${role}:${moduleName ?? ""}`;
}

function compareIdentity(left, right) {
  return identity(left).localeCompare(identity(right));
}

function exactKeys(value, expected) {
  return (
    value
    && typeof value === "object"
    && !Array.isArray(value)
    && JSON.stringify(Object.keys(value).sort())
      === JSON.stringify([...expected].sort())
  );
}

function isSha256(value) {
  return typeof value === "string" && /^[0-9a-f]{64}$/.test(value);
}

function isCanonicalAbsolutePath(value) {
  return (
    typeof value === "string"
    && value.startsWith("/")
    && !value.includes("\0")
    && !value.split("/").some((part) => part === "." || part === "..")
  );
}

function validFileIdentity(identity) {
  return (
    exactKeys(identity, ["device", "inode"])
    && /^[0-9a-f]+:[0-9a-f]+$/.test(identity.device)
    && /^[1-9][0-9]*$/.test(identity.inode)
  );
}

function sameFileIdentity(left, right) {
  return (
    validFileIdentity(left)
    && validFileIdentity(right)
    && left.device === right.device
    && left.inode === right.inode
  );
}

function validExecutableMappingEvidence(
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
        validFileIdentity(expectedIdentity)
        && mapping.device === expectedIdentity.device
        && mapping.inode === expectedIdentity.inode
      )
    )
  );
}

export function validateProcessExecutableIdentity(
  process,
  expectedArtifact,
) {
  const fallbackExecution =
    expectedArtifact?.executionMode === "fallback";
  const directExecution =
    expectedArtifact?.executionMode === "direct";
  if (
    !process
    || typeof process.executable !== "string"
    || process.executable.length === 0
    || typeof process.executableArgument !== "string"
    || process.executableArgument.length === 0
    || typeof process.programArgument !== "string"
    || process.programArgument.length === 0
    || !isSha256(process.executableSha256)
    || !isSha256(process.executableArgumentSha256)
    || !isSha256(process.programArgumentSha256)
    || !expectedArtifact
    || typeof expectedArtifact !== "object"
    || typeof expectedArtifact.programArgument !== "string"
    || !isSha256(expectedArtifact.programSha256)
    || (!fallbackExecution && !directExecution)
    || !(
      expectedArtifact.loaderPath === undefined
      || isCanonicalAbsolutePath(expectedArtifact.loaderPath)
    )
    || typeof expectedArtifact.loaderExecutable !== "string"
    || expectedArtifact.loaderExecutable.length === 0
    || expectedArtifact.loaderExecutable.includes("/")
    || !isSha256(expectedArtifact.loaderSha256)
    || (
      fallbackExecution
        ? (
          typeof expectedArtifact.loaderArgument !== "string"
          || !dynamicLoaderPattern.test(expectedArtifact.loaderArgument)
        )
        : expectedArtifact.loaderArgument !== null
    )
    || !(
      expectedArtifact.moduleSha256 === null
      || isSha256(expectedArtifact.moduleSha256)
    )
    || !(
      process.moduleArtifactSha256 === null
      || isSha256(process.moduleArtifactSha256)
    )
    || !(
      process.moduleArgumentSha256 === null
      || isSha256(process.moduleArgumentSha256)
    )
    || !(
      process.directInterpreterSha256 === null
      || isSha256(process.directInterpreterSha256)
    )
  ) {
    throw new Error("Basecamp process executable identity is incomplete");
  }
  const observedDynamicLoader = dynamicLoaderPattern.test(
    process.executableArgument,
  );
  if (
    observedDynamicLoader !== fallbackExecution
    || (
      fallbackExecution
        ? (
          process.executable !== expectedArtifact.loaderExecutable
          || process.executableArgument !== expectedArtifact.loaderArgument
          || process.executableSha256 !== expectedArtifact.loaderSha256
          || process.executableArgumentSha256
            !== expectedArtifact.loaderSha256
        )
        : (
          process.executable !== process.programArgument
          || process.executableArgument !== process.programArgument
          || process.executableSha256 !== process.programArgumentSha256
          || process.executableArgumentSha256
            !== process.programArgumentSha256
          || process.directInterpreterSha256
            !== expectedArtifact.loaderSha256
        )
    )
    || (
      fallbackExecution
      && process.directInterpreterSha256 !== null
    )
  ) {
    throw new Error(
      "Basecamp process execution mode differs from pinned wrapper selection",
    );
  }
  if (
    process.programArgument !== expectedArtifact.programArgument
    || process.programArgumentSha256 !== expectedArtifact.programSha256
    || process.moduleArtifactSha256 !== expectedArtifact.moduleSha256
    || process.moduleArgumentSha256 !== expectedArtifact.moduleSha256
  ) {
    throw new Error(
      "Basecamp process executable differs from pinned runtime artifact",
    );
  }
  const pathFields = [
    process.executablePath,
    process.executableArgumentPath,
    process.programArgumentPath,
    process.directInterpreterPath,
    process.moduleArgumentPath,
    process.moduleArtifactPath,
  ];
  if (pathFields.some((value) => value !== undefined)) {
    if (
      !isCanonicalAbsolutePath(process.executablePath)
      || !isCanonicalAbsolutePath(process.executableArgumentPath)
      || !isCanonicalAbsolutePath(process.programArgumentPath)
      || !isCanonicalAbsolutePath(expectedArtifact.programPath)
      || process.programArgumentPath !== expectedArtifact.programPath
      || !sameFileIdentity(
        process.programArgumentFileIdentity,
        {
          device: expectedArtifact.programDevice,
          inode: expectedArtifact.programInode,
        },
      )
      || !validExecutableMappingEvidence(
        process.programExecutableMapping,
        expectedArtifact.programPath,
        process.programArgumentFileIdentity,
      )
      || (
        fallbackExecution
          ? (
            !isCanonicalAbsolutePath(expectedArtifact.loaderPath)
            || process.executablePath !== expectedArtifact.loaderPath
            || process.executableArgumentPath
              !== expectedArtifact.loaderPath
            || process.directInterpreterPath !== null
            || process.directInterpreterFileIdentity !== null
            || process.directInterpreterMapping !== null
            || !sameFileIdentity(
              process.executableFileIdentity,
              {
                device: expectedArtifact.loaderDevice,
                inode: expectedArtifact.loaderInode,
              },
            )
          )
          : (
            process.executablePath !== process.programArgumentPath
            || process.executableArgumentPath
              !== process.programArgumentPath
            || !isCanonicalAbsolutePath(expectedArtifact.loaderPath)
            || process.directInterpreterPath
              !== expectedArtifact.loaderPath
            || !sameFileIdentity(
              process.executableFileIdentity,
              process.programArgumentFileIdentity,
            )
            || !sameFileIdentity(
              process.directInterpreterFileIdentity,
              {
                device: expectedArtifact.loaderDevice,
                inode: expectedArtifact.loaderInode,
              },
            )
            || !validExecutableMappingEvidence(
              process.directInterpreterMapping,
              expectedArtifact.loaderPath,
              process.directInterpreterFileIdentity,
            )
          )
      )
      || (
        expectedArtifact.moduleSha256 === null
          ? (
            process.moduleArgumentPath !== null
            || process.moduleArtifactPath !== null
            || process.moduleArgumentFileIdentity !== null
            || process.moduleExecutableMapping !== null
            || expectedArtifact.modulePath !== null
          )
          : (
            !isCanonicalAbsolutePath(process.moduleArgumentPath)
            || !isCanonicalAbsolutePath(process.moduleArtifactPath)
            || !isCanonicalAbsolutePath(expectedArtifact.modulePath)
            || process.moduleArgumentPath !== expectedArtifact.modulePath
            || process.moduleArtifactPath !== expectedArtifact.modulePath
            || !sameFileIdentity(
              process.moduleArgumentFileIdentity,
              {
                device: expectedArtifact.moduleDevice,
                inode: expectedArtifact.moduleInode,
              },
            )
            || !validExecutableMappingEvidence(
              process.moduleExecutableMapping,
              expectedArtifact.modulePath,
              process.moduleArgumentFileIdentity,
            )
          )
      )
    ) {
      throw new Error(
        "Basecamp canonical executable path differs from pinned runtime artifact",
      );
    }
  }
  return true;
}

function decodeProcMappedPath(value) {
  return value.replace(
    /\\([0-7]{3})/g,
    (_match, encoded) =>
      String.fromCharCode(Number.parseInt(encoded, 8)),
  );
}

function canonicalProcMapDevice(major, minor) {
  const canonical = (value) =>
    BigInt(`0x${value}`).toString(16).padStart(2, "0");
  return `${canonical(major)}:${canonical(minor)}`;
}

export function validateExpectedExecutableMapping(
  encoded,
  expectedPath,
  expectedIdentity,
) {
  if (
    typeof encoded !== "string"
    || encoded.length === 0
    || !isCanonicalAbsolutePath(expectedPath)
    || (
      expectedIdentity !== undefined
      && !validFileIdentity(expectedIdentity)
    )
  ) {
    throw new Error("Basecamp executable map input is invalid");
  }
  let executableMapping;
  for (const line of encoded.trimEnd().split("\n")) {
    if (!line) continue;
    const match = line.match(
      /^([0-9a-f]+)-([0-9a-f]+)\s+([r-][w-][x-][ps])\s+([0-9a-f]+)\s+([0-9a-f]+):([0-9a-f]+)\s+([0-9]+)(?:\s+(.*))?$/i,
    );
    if (!match) {
      throw new Error("Basecamp process maps record is invalid");
    }
    const mappedPath = match[8] === undefined
      ? undefined
      : decodeProcMappedPath(match[8]);
    const mappedDevice = canonicalProcMapDevice(match[5], match[6]);
    if (mappedPath === `${expectedPath} (deleted)`) {
      throw new Error("Basecamp expected executable mapping is deleted");
    }
    if (
      mappedPath === expectedPath
      && match[3][2] === "x"
      && /^[1-9][0-9]*$/.test(match[7])
      && (
        expectedIdentity === undefined
        || (
          mappedDevice === expectedIdentity.device
          && match[7] === expectedIdentity.inode
        )
      )
    ) {
      executableMapping = {
        path: mappedPath,
        permissions: match[3],
        device: mappedDevice,
        inode: match[7],
      };
    }
  }
  if (!executableMapping) {
    throw new Error(
      "Basecamp expected artifact lacks exact executable mapping",
    );
  }
  return executableMapping;
}

export function findStandalonePalaceServerMatches(processes) {
  if (!Array.isArray(processes)) {
    throw new Error("Basecamp standalone server scan input is invalid");
  }
  return processes
    .map((process) => {
      const signatures = [
        process?.name,
        process?.executable,
        process?.executableArgument,
        process?.programArgument,
        ...(Array.isArray(process?.argumentBasenames)
          ? process.argumentBasenames
          : []),
      ].filter(
        (value) =>
          typeof value === "string"
          && standalonePalaceServerPattern.test(value),
      );
      return {
        process,
        signatures: [...new Set(signatures)].sort(),
      };
    })
    .filter(({ signatures }) => signatures.length > 0)
    .map(({ process, signatures }) => ({
      pid: process.pid,
      name: process.name,
      executable: process.executable,
      executableArgument: process.executableArgument,
      programArgument: process.programArgument,
      signatures,
    }));
}

export function validateExactProcessInventory(
  processes,
  rootPid,
  expectedArtifacts,
) {
  if (
    !Array.isArray(processes)
    || !Number.isSafeInteger(rootPid)
    || rootPid <= 0
    || processes.length !== expectedIdentities.length
    || !exactKeys(expectedArtifacts, expectedIdentities)
  ) {
    throw new Error("Basecamp process inventory cardinality is not exact");
  }
  const pids = new Set();
  const inventory = processes.map((process) => {
    if (
      !process
      || !Number.isSafeInteger(process.pid)
      || process.pid <= 0
      || pids.has(process.pid)
    ) {
      throw new Error("Basecamp process inventory entry is invalid");
    }
    pids.add(process.pid);
    let inventoryEntry;
    if (
      process.pid === rootPid
      && process.programArgument === ".LogosBasecamp.elf"
      && process.moduleName === undefined
    ) {
      inventoryEntry = {
        pid: process.pid,
        role: "basecamp-main",
        moduleName: null,
        program: "LogosBasecamp",
      };
    } else if (
      process.programArgument === ".logos_host.elf"
      && coreModuleNames.includes(process.moduleName)
    ) {
      inventoryEntry = {
        pid: process.pid,
        role: "core-module-host",
        moduleName: process.moduleName,
        program: "logos_host",
      };
    } else if (
      process.programArgument === ".ui-host.elf"
      && process.moduleName === "logos_palace_ui"
    ) {
      inventoryEntry = {
        pid: process.pid,
        role: "ui-module-host",
        moduleName: process.moduleName,
        program: "ui-host",
      };
    } else {
      throw new Error("unexpected process in Basecamp runtime inventory");
    }
    validateProcessExecutableIdentity(
      process,
      expectedArtifacts[identity(inventoryEntry)],
    );
    return inventoryEntry;
  });
  const actualIdentities = inventory
    .map(identity)
    .sort();
  if (
    JSON.stringify(actualIdentities)
    !== JSON.stringify([...expectedIdentities].sort())
  ) {
    throw new Error("Basecamp process inventory roles are not exact");
  }
  return inventory.sort(compareIdentity);
}

function decodeIpv4(encoded) {
  if (!/^[0-9A-Fa-f]{8}$/.test(encoded)) {
    throw new Error("invalid /proc IPv4 listener address");
  }
  return [
    encoded.slice(6, 8),
    encoded.slice(4, 6),
    encoded.slice(2, 4),
    encoded.slice(0, 2),
  ].map((octet) => Number.parseInt(octet, 16)).join(".");
}

export function parseTcpListenTable(encoded, protocol) {
  if (
    typeof encoded !== "string"
    || !["tcp4", "tcp6"].includes(protocol)
  ) {
    throw new Error("invalid TCP listener table input");
  }
  const lines = encoded.trimEnd().split("\n");
  if (lines.length === 0 || !lines[0].includes("local_address")) {
    throw new Error("invalid /proc TCP listener table header");
  }
  const listeners = [];
  for (const line of lines.slice(1)) {
    if (!line.trim()) continue;
    const fields = line.trim().split(/\s+/);
    if (fields.length < 10 || fields[3] !== "0A") continue;
    const separator = fields[1].lastIndexOf(":");
    const addressHex = fields[1].slice(0, separator);
    const portHex = fields[1].slice(separator + 1);
    const inode = fields[9];
    const port = Number.parseInt(portHex, 16);
    if (
      separator <= 0
      || !/^[0-9A-Fa-f]{4}$/.test(portHex)
      || !Number.isSafeInteger(port)
      || port <= 0
      || port > 65535
      || !/^[1-9][0-9]*$/.test(inode)
    ) {
      throw new Error("invalid /proc TCP listener row");
    }
    listeners.push({
      protocol,
      address:
        protocol === "tcp4"
          ? decodeIpv4(addressHex)
          : addressHex.toLowerCase(),
      port,
      inode,
    });
  }
  return listeners;
}

function listenerIdentity(listener) {
  return [
    listener.protocol,
    listener.address,
    listener.port,
    listener.ownerRole,
    listener.moduleName ?? "",
  ].join(":");
}

export function validateExactTcpListenerOwnership(
  inventory,
  listeners,
  socketOwners,
  expectedListeners,
) {
  if (
    !Array.isArray(inventory)
    || !Array.isArray(listeners)
    || !(socketOwners instanceof Map)
    || !Array.isArray(expectedListeners)
  ) {
    throw new Error("TCP listener ownership proof input is invalid");
  }
  const processByPid = new Map(
    inventory.map((process) => [process.pid, process]),
  );
  if (processByPid.size !== inventory.length) {
    throw new Error("TCP listener ownership process set is not unique");
  }
  const scoped = [];
  for (const listener of listeners) {
    const owners = [...(socketOwners.get(listener.inode) ?? [])]
      .filter((pid) => processByPid.has(pid));
    if (owners.length === 0) continue;
    if (owners.length !== 1) {
      throw new Error("TCP listener has ambiguous Basecamp process ownership");
    }
    const owner = processByPid.get(owners[0]);
    scoped.push({
      protocol: listener.protocol,
      address: listener.address,
      port: listener.port,
      ownerPid: owner.pid,
      ownerRole: owner.role,
      moduleName: owner.moduleName,
    });
  }
  const expected = expectedListeners.map((listener) => {
    if (
      !exactKeys(
        listener,
        [
          "protocol",
          "address",
          "port",
          "ownerRole",
          "moduleName",
          "purpose",
        ],
      )
      || listener.protocol !== "tcp4"
      || !["127.0.0.1", "0.0.0.0"].includes(listener.address)
      || !Number.isSafeInteger(listener.port)
      || listener.port < 1024
      || listener.port > 65535
      || ![
        "qml-inspector",
        "delivery-transport",
        "storage-transport",
      ].includes(listener.purpose)
    ) {
      throw new Error("expected TCP listener contract is invalid");
    }
    return listener;
  });
  const actualIdentities = scoped.map(listenerIdentity).sort();
  const expectedIdentitiesForListeners =
    expected.map(listenerIdentity).sort();
  if (
    JSON.stringify(actualIdentities)
    !== JSON.stringify(expectedIdentitiesForListeners)
  ) {
    throw new Error("Basecamp TCP listener inventory is not exact");
  }
  const purposeByIdentity = new Map(
    expected.map((listener) => [
      listenerIdentity(listener),
      listener.purpose,
    ]),
  );
  return scoped
    .map((listener) => ({
      ...listener,
      purpose: purposeByIdentity.get(listenerIdentity(listener)),
    }))
    .sort((left, right) =>
      listenerIdentity(left).localeCompare(listenerIdentity(right)));
}

export const exactProcessInventoryContract = Object.freeze({
  basecampMain: "LogosBasecamp",
  coreModuleHosts: coreModuleNames,
  uiModuleHosts: Object.freeze(["logos_palace_ui"]),
});
