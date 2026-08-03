import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import {
  lstatSync,
  readFileSync,
  realpathSync,
} from "node:fs";
import https from "node:https";
import { join } from "node:path";

export const palaceRelease = Object.freeze({
  explorerOrigin: "https://explorer.testnet.lez.logos.co",
  explorerHost: "explorer.testnet.lez.logos.co",
  explorerSuffix: "3022937127152978530",
  programIdHex:
    "e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61",
  programByteLength: 297312,
  programBytecodeSha256:
    "69099492e8f860ab338846f33762f5fb563ffc40121779ec1e15ef0b35da7171",
  deploymentTransactionHash:
    "98711414b02a12a9abfdd17780337f7f32c962df1dea2f50e3deeecba3c7a0b5",
  deploymentBlockId: 41029,
  deploymentBlockHash:
    "0ea1852f8c91d9ba8003844d1c98c68ba1b6ddc08a7c9dbf23dec95eb0460b92",
  rootAccountIdHex:
    "12ff117a38d756f132cf616cea36fa007653c3c99727c1b475503caa345cbf2a",
  rootAccountIdBase58:
    "2H9vVPVToHwyHer6e7dAUGA7ToQmi4HSRscjounkkig9",
  systemProgramBase58: "11111111111111111111111111111111",
});

const releasePackageBindings = Object.freeze([
  Object.freeze({
    name: "palace_vm",
    artifact: "logos-palace_vm-module-lib.lgx",
    type: "core",
    version: "0.1.0",
    sha256: "5815634576998f931458d6748eb85b3bb4cf218b7238ffd5641049f3bbedc68a",
    source: "3esmit/logos-palace@2f3b169be4b7d263bb778a22f75d425fbb1c56c0",
    dependencies: Object.freeze([]),
  }),
  Object.freeze({
    name: "palace_core",
    artifact: "logos-palace_core-module-lib.lgx",
    type: "core",
    version: "0.1.0",
    sha256: "adcdeecbf86090075b15d2e007e4eeb224dda7a899c2c40b3818380cd5690f7d",
    source: "3esmit/logos-palace@2f3b169be4b7d263bb778a22f75d425fbb1c56c0",
    dependencies: Object.freeze(["palace_vm", "lez_core", "delivery_module", "storage_module"]),
  }),
  Object.freeze({
    name: "logos_palace_ui",
    artifact: "logos-logos_palace_ui-module.lgx",
    type: "ui_qml",
    version: "0.1.0",
    sha256: "d8ae6dcef4bf274033a4b10e26aced962d3da215b76ea29b3a5e8d1fea55436b",
    source: "3esmit/logos-palace@2f3b169be4b7d263bb778a22f75d425fbb1c56c0",
    dependencies: Object.freeze(["palace_core"]),
  }),
  Object.freeze({
    name: "delivery_module",
    artifact: "logos-delivery_module-module-lib.lgx",
    type: "core",
    version: "0.1.8",
    sha256: "986a6a81ad65d42c9b174c1e3c6530a9b2b3174efff57e8dc6de078e37101da2",
    source: "3esmit/logos-delivery-module@891c43bd6176e17b0aa536ef1aa369bb47e918f4",
    dependencies: Object.freeze([]),
  }),
  Object.freeze({
    name: "storage_module",
    artifact: "logos-storage_module-module-lib.lgx",
    type: "core",
    version: "2.3.0",
    sha256: "97077662328ee9df6fc3188e8d1444e2cdf011e6a9ee6ca50e8a2848b5cc178f",
    source: "3esmit/logos-storage-module@1c75ad9d1f02f562e845a2c445421bee6ea425ad",
    dependencies: Object.freeze([]),
  }),
  Object.freeze({
    name: "lez_core",
    artifact: "logos-lez_core-module-lib.lgx",
    type: "core",
    version: "0.4.0-alpha.2",
    sha256: "6ac67c2864bb2cb7d2993a0498d1b45bb8de234b55f3c00c8cff6522999c9833",
    source: "3esmit/logos-execution-zone-module@10c6c1dd76107cb96e99f651fec3f61c35e09901",
    dependencies: Object.freeze([]),
  }),
]);

const releaseSchemaBindings = Object.freeze({
  palaceProgram: "palace-schema-v3",
  catalogManifest: "logos-palace-catalog-manifest-v1",
  roomManifest: "logos-palace-room-v1",
  propManifest: "logos-palace-prop-v1",
  deliveryEnvelope: "logos-palace-room-v1",
  vmProfile: "classic-mvp-v1",
  lezVmProfile: "iptscrae_mvp_v1",
});

const releaseNetworkBindings = Object.freeze({
  lezNetworkId: "logos-lez-testnet-v0.2.0",
  deliveryNetworkId: "logos-lez-testnet-v0.2.0",
  sequencerOrigin: "https://testnet.lez.logos.co",
  explorerOrigin: "https://explorer.testnet.lez.logos.co",
  deliveryTopology: "participant-hosted",
  storageTopology: "participant-hosted",
});

const releaseDependencyBindings = Object.freeze({
  basecamp: "3esmit/logos-basecamp@98888ed952dd3c147c66aab48aeec6fe41329af3",
  logosModuleBuilder: "logos-co/logos-module-builder@fd07679ecfa1b2d8cfdd06799f3e03ed385b57f6",
  nixpkgs: "NixOS/nixpkgs@535f3e6942cb1cead3929c604320d3db54b542b9",
});

const explorerPostMaximumAttempts = 3;
const retryableExplorerTransportCodes = new Set([
  "EAI_AGAIN",
  "ECONNABORTED",
  "ECONNREFUSED",
  "ECONNRESET",
  "EHOSTUNREACH",
  "ENETDOWN",
  "ENETUNREACH",
  "EPIPE",
  "ERR_SOCKET_CLOSED",
  "ERR_SOCKET_TIMEOUT",
  "ERR_STREAM_PREMATURE_CLOSE",
  "ETIMEDOUT",
]);

function sha256(value) {
  return createHash("sha256").update(value).digest("hex");
}

function isHex64(value) {
  return typeof value === "string" && /^[0-9a-f]{64}$/.test(value);
}

function exactJson(left, right) {
  return JSON.stringify(left) === JSON.stringify(right);
}

function exactKeys(value, keys) {
  return (
    value
    && !Array.isArray(value)
    && typeof value === "object"
    && exactJson(Object.keys(value).sort(), [...keys].sort())
  );
}

function canonicalBase64(value, maximumBytes) {
  if (
    typeof value !== "string"
    || value.length > Math.ceil(maximumBytes / 3) * 4 + 4
    || !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(
      value,
    )
  ) {
    return undefined;
  }
  const bytes = Buffer.from(value, "base64");
  return bytes.toString("base64") === value ? bytes : undefined;
}

function base58Encode(bytes) {
  const alphabet =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
  let numeric = 0n;
  for (const byte of bytes) numeric = numeric * 256n + BigInt(byte);
  let encoded = "";
  while (numeric > 0n) {
    encoded = alphabet[Number(numeric % 58n)] + encoded;
    numeric /= 58n;
  }
  let zeroes = 0;
  while (zeroes < bytes.length && bytes[zeroes] === 0) {
    encoded = `1${encoded}`;
    zeroes += 1;
  }
  return encoded;
}

export function loadImmutableReleaseArtifact() {
  const root = process.env.PALACE_RELEASE_ARTIFACT ?? "";
  if (
    !/^\/nix\/store\/[0-9abcdfghijklmnpqrsvwxyz]{32}-[^/]+$/.test(root)
    || realpathSync(root) !== root
  ) {
    throw new Error("release artifact is not one canonical Nix output");
  }
  const manifestPath = join(
    root,
    "share",
    "logos-palace",
    "release.json",
  );
  const verifierPath = join(root, "bin", "palace-image-id");
  for (const path of [manifestPath, verifierPath]) {
    const status = lstatSync(path);
    if (!status.isFile() || status.isSymbolicLink() || realpathSync(path) !== path) {
      throw new Error("release artifact contains non-regular path");
    }
  }
  if ((lstatSync(verifierPath).mode & 0o111) === 0) {
    throw new Error("release artifact verifier is not executable");
  }
  const embeddedBytecode = lstatSync(
    join(root, "share", "logos-palace", "palace.bin"),
    { throwIfNoEntry: false },
  );
  if (embeddedBytecode !== undefined) {
    throw new Error("release artifact must not embed deployed bytecode");
  }

  const manifestBody = readFileSync(manifestPath, "utf8");
  const validated = validateReleaseManifest(manifestBody);
  return Object.freeze({
    ...validated,
    verifierPath,
  });
}

export function validateReleaseManifest(
  manifestBody,
  release = palaceRelease,
) {
  const manifest = parseStrictJson(manifestBody, "release manifest");
  const deployment = manifest?.deployment;
  const packages = manifest?.packages;
  const schemas = manifest?.schemas;
  const network = manifest?.network;
  const dependencies = manifest?.dependencies;
  const expectedDeployment = {
    networkId: "logos-lez-testnet-v0.2.0",
    explorerOrigin: release.explorerOrigin,
    transactionHash: release.deploymentTransactionHash,
    blockId: release.deploymentBlockId,
    blockHash: release.deploymentBlockHash,
    rootAccountIdHex: release.rootAccountIdHex,
    rootAccountIdBase58: release.rootAccountIdBase58,
    systemProgramBase58: release.systemProgramBase58,
  };
  if (
    !exactKeys(
      manifest,
      [
        "schema",
        "version",
        "platform",
        "risc0BinfmtVersion",
        "byteLength",
        "sha256",
        "imageIdHex",
        "deployment",
        "packages",
        "schemas",
        "network",
        "dependencies",
      ],
    )
    || manifest.schema !== "logos.palace.release"
    || manifest.version !== 2
    || manifest.platform !== "x86_64-linux"
    || manifest.risc0BinfmtVersion !== "3.0.5"
    || manifest.byteLength !== release.programByteLength
    || manifest.sha256 !== release.programBytecodeSha256
    || manifest.imageIdHex !== release.programIdHex
    || !exactKeys(deployment, Object.keys(expectedDeployment))
    || !exactJson(deployment, expectedDeployment)
    || !Array.isArray(packages)
    || packages.length !== releasePackageBindings.length
    || !packages.every((entry, index) =>
      exactKeys(entry, Object.keys(releasePackageBindings[index]))
      && exactJson(entry, releasePackageBindings[index]))
    || !exactKeys(schemas, Object.keys(releaseSchemaBindings))
    || !exactJson(schemas, releaseSchemaBindings)
    || !exactKeys(network, Object.keys(releaseNetworkBindings))
    || !exactJson(network, releaseNetworkBindings)
    || !exactKeys(dependencies, Object.keys(releaseDependencyBindings))
    || !exactJson(dependencies, releaseDependencyBindings)
  ) {
    throw new Error("immutable release manifest mismatch");
  }
  return {
    programByteLength: manifest.byteLength,
    programBytecodeSha256: manifest.sha256,
    programIdHex: manifest.imageIdHex,
    deployment,
    packages,
    schemas,
    network,
    dependencies,
  };
}

function validateFetchedReleaseDigest(bytecode, release) {
  if (!Buffer.isBuffer(bytecode)) {
    throw new Error("fetched release bytecode type mismatch");
  }
  const digest = sha256(bytecode);
  if (
    bytecode.length !== release.programByteLength
    || digest !== release.programBytecodeSha256
  ) {
    throw new Error("fetched release bytecode mismatch");
  }
  return digest;
}

export function validateFetchedReleaseValues(
  bytecode,
  imageIdHex,
  release = palaceRelease,
) {
  const digest = validateFetchedReleaseDigest(bytecode, release);
  if (
    !isHex64(imageIdHex)
    || imageIdHex !== release.programIdHex
  ) {
    throw new Error("computed RISC0 image ID mismatch");
  }
  return {
    byteLength: bytecode.length,
    digest,
    imageIdHex,
  };
}

export function computeRisc0ImageId(
  bytecode,
  verifierPath,
) {
  if (
    !Buffer.isBuffer(bytecode)
    || typeof verifierPath !== "string"
    || verifierPath.length === 0
  ) {
    throw new Error("RISC0 image verifier input invalid");
  }
  try {
    const output = execFileSync(
      verifierPath,
      ["-"],
      {
        input: bytecode,
        encoding: "utf8",
        maxBuffer: 4096,
        stdio: ["pipe", "pipe", "pipe"],
        timeout: 30_000,
        windowsHide: true,
      },
    );
    if (!/^[0-9a-f]{64}\n?$/.test(output)) {
      throw new Error("RISC0 image verifier output invalid");
    }
    return output.endsWith("\n")
      ? output.slice(0, -1)
      : output;
  } catch {
    throw new Error("RISC0 image verifier rejected fetched bytecode");
  }
}

export function verifyFetchedReleaseBytecode(
  bytecode,
  releaseArtifact,
) {
  if (
    !releaseArtifact
    || typeof releaseArtifact.verifierPath !== "string"
  ) {
    throw new Error("immutable release verifier missing");
  }
  validateFetchedReleaseDigest(bytecode, releaseArtifact);
  const imageIdHex = computeRisc0ImageId(
    bytecode,
    releaseArtifact.verifierPath,
  );
  return validateFetchedReleaseValues(
    bytecode,
    imageIdHex,
    releaseArtifact,
  );
}

export function assertReleaseRootBindings(release = palaceRelease) {
  if (
    base58Encode(Buffer.from(release.rootAccountIdHex, "hex"))
    !== release.rootAccountIdBase58
  ) {
    throw new Error("release root base58 binding mismatch");
  }
  if (
    derivePalaceRootPda(release.programIdHex)
    !== release.rootAccountIdHex
  ) {
    throw new Error("release root PDA derivation mismatch");
  }
}

export function derivePalaceRootPda(programIdHex) {
  if (!isHex64(programIdHex)) {
    throw new Error("invalid release program ID");
  }
  const input = Buffer.alloc(96);
  Buffer.from("/LEE/v0.2/AccountId/PDA/", "ascii").copy(input, 0);
  Buffer.from(programIdHex, "hex").copy(input, 32);
  Buffer.from("palace-root", "ascii").copy(input, 64);
  return sha256(input);
}

function parseStrictJson(body, description) {
  if (
    typeof body !== "string"
    || body.length === 0
    || body.length > 16 * 1024 * 1024
  ) {
    throw new Error(`${description} size invalid`);
  }
  let cursor = 0;
  let nodes = 0;
  const whitespace = /[\u0009\u000a\u000d\u0020]/;
  const skipWhitespace = () => {
    while (cursor < body.length && whitespace.test(body[cursor])) cursor += 1;
  };
  const parseString = () => {
    if (body[cursor] !== "\"") {
      throw new Error(`${description} JSON string invalid`);
    }
    const start = cursor;
    cursor += 1;
    while (cursor < body.length) {
      const code = body.charCodeAt(cursor);
      if (code === 0x22) {
        cursor += 1;
        try {
          return JSON.parse(body.slice(start, cursor));
        } catch {
          throw new Error(`${description} JSON string invalid`);
        }
      }
      if (code < 0x20) {
        throw new Error(`${description} JSON control character invalid`);
      }
      if (code === 0x5c) {
        cursor += 1;
        if (cursor >= body.length) {
          throw new Error(`${description} JSON escape truncated`);
        }
        if (body[cursor] === "u") {
          if (!/^[0-9a-fA-F]{4}$/.test(
            body.slice(cursor + 1, cursor + 5),
          )) {
            throw new Error(`${description} JSON unicode escape invalid`);
          }
          cursor += 5;
          continue;
        }
        if (!"\"\\/bfnrt".includes(body[cursor])) {
          throw new Error(`${description} JSON escape invalid`);
        }
      }
      cursor += 1;
    }
    throw new Error(`${description} JSON string truncated`);
  };
  const parseValue = (depth) => {
    nodes += 1;
    if (depth > 128 || nodes > 500_000) {
      throw new Error(`${description} JSON complexity exceeds bound`);
    }
    skipWhitespace();
    if (body[cursor] === "\"") return parseString();
    if (body[cursor] === "[") {
      cursor += 1;
      const result = [];
      skipWhitespace();
      if (body[cursor] === "]") {
        cursor += 1;
        return result;
      }
      while (cursor < body.length) {
        result.push(parseValue(depth + 1));
        skipWhitespace();
        if (body[cursor] === "]") {
          cursor += 1;
          return result;
        }
        if (body[cursor] !== ",") {
          throw new Error(`${description} JSON array delimiter invalid`);
        }
        cursor += 1;
      }
      throw new Error(`${description} JSON array truncated`);
    }
    if (body[cursor] === "{") {
      cursor += 1;
      const result = Object.create(null);
      const keys = new Set();
      skipWhitespace();
      if (body[cursor] === "}") {
        cursor += 1;
        return result;
      }
      while (cursor < body.length) {
        skipWhitespace();
        const key = parseString();
        if (keys.has(key)) {
          throw new Error(`${description} JSON duplicate key`);
        }
        keys.add(key);
        skipWhitespace();
        if (body[cursor] !== ":") {
          throw new Error(`${description} JSON object delimiter invalid`);
        }
        cursor += 1;
        result[key] = parseValue(depth + 1);
        skipWhitespace();
        if (body[cursor] === "}") {
          cursor += 1;
          return result;
        }
        if (body[cursor] !== ",") {
          throw new Error(`${description} JSON object delimiter invalid`);
        }
        cursor += 1;
      }
      throw new Error(`${description} JSON object truncated`);
    }
    const remainder = body.slice(cursor);
    const number = remainder.match(
      /^-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?/,
    );
    if (number) {
      cursor += number[0].length;
      const value = Number(number[0]);
      if (!Number.isFinite(value)) {
        throw new Error(`${description} JSON number invalid`);
      }
      return value;
    }
    for (const [literal, value] of [
      ["true", true],
      ["false", false],
      ["null", null],
    ]) {
      if (body.startsWith(literal, cursor)) {
        cursor += literal.length;
        return value;
      }
    }
    throw new Error(`${description} JSON value invalid`);
  };
  const value = parseValue(0);
  skipWhitespace();
  if (cursor !== body.length) {
    throw new Error(`${description} JSON trailing data`);
  }
  return value;
}

function isRetryableExplorerTransportError(error) {
  return (
    error instanceof Error
    && retryableExplorerTransportCodes.has(error.code)
  );
}

function explorerPostOnce(
  path,
  formBody,
  maximumBytes,
  requestImplementation,
) {
  return new Promise((resolveRequest, rejectRequest) => {
    const request = requestImplementation(
      {
        protocol: "https:",
        hostname: palaceRelease.explorerHost,
        port: 443,
        method: "POST",
        path,
        headers: {
          Accept: "application/json",
          "Content-Type": "application/x-www-form-urlencoded",
          "Content-Length": Buffer.byteLength(formBody, "utf8"),
        },
        timeout: 30_000,
      },
      (response) => {
        const statusCode = Number(response.statusCode ?? 0);
        const contentType = String(
          response.headers["content-type"] ?? "",
        ).split(";", 1)[0].trim().toLowerCase();
        if (
          statusCode !== 200
          || contentType !== "application/json"
          || response.headers.location !== undefined
        ) {
          response.resume();
          rejectRequest(
            new Error(
              `explorer response rejected: status=${statusCode} content-type=${contentType}`,
            ),
          );
          return;
        }
        const declaredText = response.headers["content-length"];
        const declared = declaredText === undefined
          ? undefined
          : Number(declaredText);
        if (
          declared !== undefined
          && (
            !Number.isSafeInteger(declared)
            || declared < 0
            || declared > maximumBytes
          )
        ) {
          response.destroy();
          rejectRequest(new Error("explorer response length invalid"));
          return;
        }
        const chunks = [];
        let length = 0;
        response.on("data", (chunk) => {
          length += chunk.length;
          if (length > maximumBytes) {
            response.destroy(
              new Error("explorer response exceeds bound"),
            );
            return;
          }
          chunks.push(chunk);
        });
        response.once("error", rejectRequest);
        response.once("end", () => {
          if (
            !response.complete
            || length === 0
            || length > maximumBytes
            || (declared !== undefined && declared !== length)
          ) {
            rejectRequest(new Error("explorer response size invalid"));
            return;
          }
          resolveRequest(Buffer.concat(chunks).toString("utf8"));
        });
      },
    );
    request.once("timeout", () => {
      const error = new Error("explorer request timed out");
      error.code = "ETIMEDOUT";
      request.destroy(error);
    });
    request.once("error", rejectRequest);
    request.end(formBody);
  });
}

export async function postExplorerReadOnly(
  path,
  formBody,
  maximumBytes = 16 * 1024 * 1024,
  requestImplementation = https.request,
) {
  if (
    !/^\/api\/(?:get_blocks|get_account)[0-9]+$/.test(path)
    || typeof formBody !== "string"
    || Buffer.byteLength(formBody, "utf8") > 1024
    || typeof requestImplementation !== "function"
  ) {
    throw new Error("invalid explorer request boundary");
  }
  for (let attempt = 1; attempt <= explorerPostMaximumAttempts; attempt += 1) {
    try {
      return await explorerPostOnce(
        path,
        formBody,
        maximumBytes,
        requestImplementation,
      );
    } catch (error) {
      if (
        attempt === explorerPostMaximumAttempts
        || !isRetryableExplorerTransportError(error)
      ) {
        throw error;
      }
    }
  }
  throw new Error("explorer retry attempts invalid");
}

function validateExplorerBlock(block) {
  if (
    !exactKeys(block, ["header", "body", "bedrock_status"])
    || !exactKeys(
      block.header,
      ["block_id", "prev_block_hash", "hash", "timestamp", "signature"],
    )
    || !exactKeys(block.body, ["transactions"])
    || !Number.isSafeInteger(block.header.block_id)
    || block.header.block_id <= 0
    || !isHex64(block.header.prev_block_hash)
    || !isHex64(block.header.hash)
    || block.header.hash === "0".repeat(64)
    || !Number.isSafeInteger(block.header.timestamp)
    || block.header.timestamp < 0
    || typeof block.header.signature !== "string"
    || !/^[0-9a-f]{128}$/.test(block.header.signature)
    || !["Pending", "Safe", "Finalized"].includes(block.bedrock_status)
    || !Array.isArray(block.body.transactions)
    || block.body.transactions.length > 4096
  ) {
    throw new Error("explorer block schema invalid");
  }
  for (const transaction of block.body.transactions) {
    if (
      !transaction
      || Array.isArray(transaction)
      || typeof transaction !== "object"
      || Object.keys(transaction).length !== 1
      || !["Public", "PrivacyPreserving", "ProgramDeployment"].includes(
        Object.keys(transaction)[0],
      )
      || !transaction[Object.keys(transaction)[0]]
      || Array.isArray(transaction[Object.keys(transaction)[0]])
      || typeof transaction[Object.keys(transaction)[0]] !== "object"
    ) {
      throw new Error("explorer transaction envelope invalid");
    }
  }
  return block;
}

export async function verifyPalaceProgramDeployment(
  releaseArtifact = loadImmutableReleaseArtifact(),
  postExplorer = postExplorerReadOnly,
) {
  const path = `/api/get_blocks${palaceRelease.explorerSuffix}`;
  const body = await postExplorer(
    path,
    `limit=1&before=${palaceRelease.deploymentBlockId + 1}`,
  );
  const parsed = parseStrictJson(body, "explorer deployment block");
  if (!Array.isArray(parsed) || parsed.length !== 1) {
    throw new Error("explorer deployment block response invalid");
  }
  const block = validateExplorerBlock(parsed[0]);
  if (
    block.header.block_id !== palaceRelease.deploymentBlockId
    || block.header.hash !== palaceRelease.deploymentBlockHash
  ) {
    throw new Error("release deployment block identity mismatch");
  }
  for (const transaction of block.body.transactions) {
    const deployment = transaction.ProgramDeployment;
    if (!deployment) continue;
    if (
      !exactKeys(deployment, ["hash", "message"])
      || !isHex64(deployment.hash)
      || !exactKeys(deployment.message, ["bytecode"])
    ) {
      throw new Error("ProgramDeployment schema invalid");
    }
    const bytecode = canonicalBase64(
      deployment.message.bytecode,
      614195,
    );
    if (!bytecode) {
      throw new Error("ProgramDeployment bytecode is not canonical");
    }
    const digest = sha256(bytecode);
    const knownTransaction =
      deployment.hash === palaceRelease.deploymentTransactionHash;
    const exactBytecode =
      bytecode.length === palaceRelease.programByteLength
      && digest === palaceRelease.programBytecodeSha256;
    if (knownTransaction !== exactBytecode) {
      throw new Error(
        "release ProgramDeployment transaction/bytecode binding mismatch",
      );
    }
    if (knownTransaction) {
      const verifiedRelease = verifyFetchedReleaseBytecode(
        bytecode,
        releaseArtifact,
      );
      if (block.bedrock_status !== "Finalized") {
        throw new Error("matching ProgramDeployment is not finalized");
      }
      return {
        status: "passed",
        explorerOrigin: palaceRelease.explorerOrigin,
        path,
        pagesScanned: 1,
        blocksScanned: 1,
        blockId: block.header.block_id,
        blockHash: block.header.hash,
        transactionHash: deployment.hash,
        byteLength: verifiedRelease.byteLength,
        bytecodeSha256: verifiedRelease.digest,
        sha256: verifiedRelease.digest,
        risc0ImageIdHex: verifiedRelease.imageIdHex,
        programIdHex: verifiedRelease.imageIdHex,
        bedrockStatus: block.bedrock_status,
      };
    }
  }
  throw new Error("release ProgramDeployment missing from pinned block");
}

export async function probePalaceRootAccount(
  postExplorer = postExplorerReadOnly,
) {
  const body = await postExplorer(
    `/api/get_account${palaceRelease.explorerSuffix}`,
    `account_id=${palaceRelease.rootAccountIdBase58}`,
    2 * 1024 * 1024,
  );
  const value = parseStrictJson(body, "root account");
  if (
    !exactKeys(value, ["program_owner", "balance", "data", "nonce"])
    || typeof value.program_owner !== "string"
    || !Number.isSafeInteger(value.balance)
    || value.balance < 0
    || !Number.isSafeInteger(value.nonce)
    || value.nonce < 0
    || typeof value.data !== "string"
  ) {
    throw new Error("root account schema invalid");
  }
  const data = canonicalBase64(value.data, 1024 * 1024);
  if (!data) throw new Error("root account data is not canonical base64");
  const programBase58 = base58Encode(
    Buffer.from(palaceRelease.programIdHex, "hex"),
  );
  let state;
  if (
    value.program_owner === palaceRelease.systemProgramBase58
    && value.nonce === 0
    && data.length === 0
  ) {
    state = "uninitialized";
  } else if (
    value.program_owner === programBase58
    && data.length > 0
  ) {
    state = "initialized";
  } else {
    throw new Error("root account owner/data state is not accepted");
  }
  return {
    status: "passed",
    state,
    explorerOrigin: palaceRelease.explorerOrigin,
    path: `/api/get_account${palaceRelease.explorerSuffix}`,
    accountIdBase58: palaceRelease.rootAccountIdBase58,
    programOwner: value.program_owner,
    balance: value.balance,
    nonce: value.nonce,
    dataBytes: data.length,
    dataSha256: sha256(data),
    responseSha256: sha256(body),
  };
}

export async function runPalaceReleasePreflight() {
  const releaseArtifact = loadImmutableReleaseArtifact();
  assertReleaseRootBindings();
  const startedAtUnixMs = Date.now();
  const programDeployment =
    await verifyPalaceProgramDeployment(releaseArtifact);
  const rootAccountBeforeWrites = await probePalaceRootAccount();
  return {
    schema: "logos.palace.release-preflight",
    version: 1,
    status: "passed",
    startedAtUnixMs,
    completedAtUnixMs: Date.now(),
    release: {
      programIdHex: programDeployment.risc0ImageIdHex,
      programBytecodeSha256: programDeployment.bytecodeSha256,
      programByteLength: programDeployment.byteLength,
      deploymentTransactionHash:
        palaceRelease.deploymentTransactionHash,
      rootAccountIdHex: palaceRelease.rootAccountIdHex,
      rootAccountIdBase58: palaceRelease.rootAccountIdBase58,
    },
    programDeployment,
    rootAccountBeforeWrites,
  };
}
