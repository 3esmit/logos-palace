import { createHash } from "node:crypto";
import {
  lstat,
  readFile,
  realpath,
} from "node:fs/promises";
import { isAbsolute, relative, resolve } from "node:path";

const maxManifestBytes = 1024 * 1024;
const maxAssetBytes = 10 * 1024 * 1024;
const pngSignature = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);

function sha256(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

function exactObjectKeys(value, keys) {
  return (
    value
    && typeof value === "object"
    && !Array.isArray(value)
    && Object.keys(value).sort().join(",") === [...keys].sort().join(",")
  );
}

function canonicalDescendant(root, candidate, description) {
  const child = relative(root, candidate);
  if (
    child === ""
    || child === ".."
    || child.startsWith("../")
    || isAbsolute(child)
  ) {
    throw new Error(`${description} is not a canonical input-root descendant`);
  }
}

function pngDimensions(bytes, description) {
  if (
    bytes.length < 24
    || !bytes.subarray(0, pngSignature.length).equals(pngSignature)
    || bytes.toString("ascii", 12, 16) !== "IHDR"
  ) {
    throw new Error(`${description} is not a PNG with an IHDR`);
  }
  const width = bytes.readUInt32BE(16);
  const height = bytes.readUInt32BE(20);
  if (width <= 0 || height <= 0) {
    throw new Error(`${description} has invalid PNG dimensions`);
  }
  return [width, height];
}

async function canonicalRegularFile(
  path,
  root,
  description,
  maximumBytes,
) {
  const inputPath = resolve(path);
  const inputMetadata = await lstat(inputPath);
  if (!inputMetadata.isFile() || inputMetadata.isSymbolicLink()) {
    throw new Error(`${description} is not a regular non-symlink file`);
  }
  const canonical = await realpath(inputPath);
  canonicalDescendant(root, canonical, description);
  const canonicalMetadata = await lstat(canonical);
  if (
    !canonicalMetadata.isFile()
    || canonicalMetadata.isSymbolicLink()
    || canonicalMetadata.size <= 0
    || canonicalMetadata.size > maximumBytes
  ) {
    throw new Error(`${description} has invalid file type or size`);
  }
  return canonical;
}

function validateAssetEntry(entry, index) {
  const description = `asset input ${index}`;
  const requiredKeys = [
    "assetId",
    "title",
    "outputFile",
    "outputSha256",
    "outputDimensions",
    "outputBytes",
    "technicalProfile",
    "role",
  ];
  const allowedKeys = new Set([
    ...requiredKeys,
    "sourceUrl",
    "sourceSha256",
    "sourceDimensions",
    "transform",
    "assignment",
  ]);
  if (
    !entry
    || typeof entry !== "object"
    || Array.isArray(entry)
    || requiredKeys.some((key) => !Object.hasOwn(entry, key))
    || Object.keys(entry).some((key) => !allowedKeys.has(key))
    || typeof entry.assetId !== "string"
    || !/^[a-z][a-z0-9_-]{0,63}$/.test(entry.assetId)
    || typeof entry.title !== "string"
    || entry.title.length < 1
    || entry.title.length > 128
    || typeof entry.outputFile !== "string"
    || entry.outputFile.length < 5
    || entry.outputFile.length > 128
    || isAbsolute(entry.outputFile)
    || entry.outputFile.includes("\\")
    || !entry.outputFile.endsWith(".png")
    || !/^[0-9a-f]{64}$/.test(entry.outputSha256)
    || !Array.isArray(entry.outputDimensions)
    || entry.outputDimensions.length !== 2
    || entry.outputDimensions.some(
      (dimension) => !Number.isSafeInteger(dimension) || dimension <= 0,
    )
    || !Number.isSafeInteger(entry.outputBytes)
    || entry.outputBytes <= 0
    || entry.outputBytes > maxAssetBytes
    || entry.technicalProfile !== "palace-png-v1"
    || !["room-background", "prop-image"].includes(entry.role)
  ) {
    throw new Error(`${description} manifest entry is invalid`);
  }
  if (entry.assignment !== undefined) {
    if (
      entry.role === "room-background"
      && (
        !exactObjectKeys(entry.assignment, ["kind", "roomId"])
        || entry.assignment.kind !== "room-background"
        || !["atrium", "lounge"].includes(entry.assignment.roomId)
      )
    ) {
      throw new Error(`${description} room assignment is invalid`);
    }
    if (
      entry.role === "prop-image"
      && (
        !exactObjectKeys(
          entry.assignment,
          ["kind", "propId", "anchorX", "anchorY", "layer"],
        )
        || entry.assignment.kind !== "prop-image"
        || typeof entry.assignment.propId !== "string"
        || !/^[a-z][a-z0-9_-]{0,63}$/.test(entry.assignment.propId)
        || !Number.isSafeInteger(entry.assignment.anchorX)
        || entry.assignment.anchorX < 0
        || !Number.isSafeInteger(entry.assignment.anchorY)
        || entry.assignment.anchorY < 0
        || !["head", "body", "hand", "back"].includes(
          entry.assignment.layer,
        )
      )
    ) {
      throw new Error(`${description} prop assignment is invalid`);
    }
  }
}

export async function loadGate3AssetInputs({
  manifestPath,
  inputRoot,
}) {
  if (!manifestPath || !inputRoot) {
    throw new Error(
      "PALACE_E2E_ASSET_MANIFEST and PALACE_E2E_ASSET_INPUT_ROOT are required",
    );
  }
  const rootPath = resolve(inputRoot);
  const rootMetadata = await lstat(rootPath);
  if (!rootMetadata.isDirectory() || rootMetadata.isSymbolicLink()) {
    throw new Error("asset input root is not a directory");
  }
  const canonicalRoot = await realpath(rootPath);
  if (canonicalRoot !== rootPath) {
    throw new Error("asset input root is not canonical");
  }
  const canonicalManifest = await canonicalRegularFile(
    manifestPath,
    canonicalRoot,
    "asset input manifest",
    maxManifestBytes,
  );
  const manifestBytes = await readFile(canonicalManifest);
  let manifest;
  try {
    manifest = JSON.parse(manifestBytes.toString("utf8"));
  } catch {
    throw new Error("asset input manifest is not JSON");
  }
  if (
    !exactObjectKeys(manifest, [
      "schema",
      "version",
      "retrievedAt",
      "sourceHost",
      "rights",
      "assets",
    ])
    || manifest.schema !== "logos.palace.e2e-asset-inputs"
    || manifest.version !== 1
    || !Array.isArray(manifest.assets)
    || manifest.assets.length < 2
    || manifest.assets.length > 128
  ) {
    throw new Error("asset input manifest envelope is invalid");
  }

  const fixtures = [];
  const selectionPaths = new Map();
  for (let index = 0; index < manifest.assets.length; index += 1) {
    const entry = manifest.assets[index];
    validateAssetEntry(entry, index);
    const assetPath = await canonicalRegularFile(
      resolve(canonicalRoot, entry.outputFile),
      canonicalRoot,
      `asset input ${entry.assetId}`,
      maxAssetBytes,
    );
    const bytes = await readFile(assetPath);
    const dimensions = pngDimensions(bytes, `asset input ${entry.assetId}`);
    if (
      bytes.length !== entry.outputBytes
      || sha256(bytes) !== entry.outputSha256
      || dimensions[0] !== entry.outputDimensions[0]
      || dimensions[1] !== entry.outputDimensions[1]
    ) {
      throw new Error(`asset input ${entry.assetId} bytes mismatch`);
    }
    fixtures.push({
      assetId: entry.assetId,
      title: entry.title,
      file: entry.outputFile,
      handle: entry.outputSha256,
      byteLength: bytes.length,
      width: dimensions[0],
      height: dimensions[1],
      role: entry.role,
      assignment: entry.assignment,
    });
    selectionPaths.set(entry.assetId, assetPath);
  }
  if (
    new Set(fixtures.map(({ assetId }) => assetId)).size !== fixtures.length
    || new Set(fixtures.map(({ handle }) => handle)).size !== fixtures.length
  ) {
    throw new Error("asset inputs are not unique assets");
  }
  const roomAssignments = fixtures
    .filter(({ assignment }) => assignment?.kind === "room-background")
    .map(({ assetId, assignment }) => ({
      assetId,
      roomId: assignment.roomId,
    }));
  const propAssignments = fixtures.filter(
    ({ assignment }) => assignment?.kind === "prop-image",
  );
  if (
    roomAssignments.length < 2
    || roomAssignments.length
      !== fixtures.filter(({ role }) => role === "room-background").length
    || new Set(roomAssignments.map(({ roomId }) => roomId)).size !== 2
    || !roomAssignments.some(({ roomId }) => roomId === "atrium")
    || !roomAssignments.some(({ roomId }) => roomId === "lounge")
  ) {
    throw new Error("room-background input assignments are invalid");
  }
  if (propAssignments.length > 1) {
    throw new Error("prop-image inputs contain multiple assignments");
  }
  const result = {
    manifest: {
      schema: manifest.schema,
      version: manifest.version,
      sha256: sha256(manifestBytes),
      assetCount: fixtures.length,
    },
    fixtures,
  };
  Object.defineProperty(result, "selectionPathFor", {
    enumerable: false,
    value(assetId) {
      if (typeof assetId !== "string" || !selectionPaths.has(assetId)) {
        throw new Error("asset input selection is unknown");
      }
      return selectionPaths.get(assetId);
    },
  });
  return result;
}

export const gate3AssetInputLimits = Object.freeze({
  maxManifestBytes,
  maxAssetBytes,
});
