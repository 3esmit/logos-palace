#!/usr/bin/env node

import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import {
  mkdtemp,
  symlink,
  writeFile,
} from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import {
  gate3AssetInputLimits,
  loadGate3AssetInputs,
} from "./basecamp_gate3_asset_inputs.mjs";

function png(width, height, suffix) {
  const bytes = Buffer.alloc(24 + suffix.length);
  Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]).copy(bytes);
  bytes.write("IHDR", 12, "ascii");
  bytes.writeUInt32BE(width, 16);
  bytes.writeUInt32BE(height, 20);
  Buffer.from(suffix).copy(bytes, 24);
  return bytes;
}

async function syntheticInputs({ includeProp = true } = {}) {
  const root = await mkdtemp(join(tmpdir(), "palace-gate3-inputs."));
  const assets = [];
  const specs = [
    [
      "asset-a",
      "room-background",
      { kind: "room-background", roomId: "atrium" },
    ],
    [
      "asset-b",
      "room-background",
      { kind: "room-background", roomId: "lounge" },
    ],
    [
      "asset-c",
      "room-background",
      { kind: "room-background", roomId: "atrium" },
    ],
    [
      "asset_d-1",
      "prop-image",
      {
        kind: "prop-image",
        propId: "test_prop-1",
        anchorX: 3,
        anchorY: 4,
        layer: "head",
      },
    ],
  ];
  if (!includeProp) specs.pop();
  for (let index = 0; index < specs.length; index += 1) {
    const [assetId, role, assignment] = specs[index];
    const bytes = png(index + 1, index + 2, assetId);
    const outputFile = `${assetId}.png`;
    const digest = createHash("sha256").update(bytes).digest("hex");
    await writeFile(join(root, outputFile), bytes);
    const asset = {
      assetId,
      title: assetId,
      sourceUrl: `https://example.invalid/${assetId}.png`,
      sourceSha256: digest,
      sourceDimensions: [index + 1, index + 2],
      transform: "synthetic",
      outputFile,
      outputSha256: digest,
      outputDimensions: [index + 1, index + 2],
      outputBytes: bytes.length,
      technicalProfile: "palace-png-v1",
      role,
    };
    if (assignment !== undefined) asset.assignment = assignment;
    assets.push(asset);
  }
  const manifest = {
    schema: "logos.palace.e2e-asset-inputs",
    version: 1,
    retrievedAt: "2026-07-29",
    sourceHost: "example.invalid",
    rights: {},
    assets,
  };
  const manifestPath = join(root, "manifest-v1.json");
  await writeFile(manifestPath, `${JSON.stringify(manifest)}\n`);
  return { root, manifestPath, manifest };
}

test("loads bounded unique media fixtures with manifest-derived selection paths", async () => {
  const inputs = await syntheticInputs();
  const result = await loadGate3AssetInputs({
    manifestPath: inputs.manifestPath,
    inputRoot: inputs.root,
  });
  assert.equal(result.manifest.assetCount, 4);
  assert.match(result.manifest.sha256, /^[0-9a-f]{64}$/);
  assert.deepEqual(
    result.fixtures.at(-1).assignment,
    {
      kind: "prop-image",
      propId: "test_prop-1",
      anchorX: 3,
      anchorY: 4,
      layer: "head",
    },
  );
  assert.deepEqual(
    result.fixtures.map(({ assetId, role, assignment }) => ({
      assetId,
      role,
      assignment,
    })),
    inputs.manifest.assets.map(({ assetId, role, assignment }) => ({
      assetId,
      role,
      assignment,
    })),
  );
  assert.equal(Object.hasOwn(result.fixtures[0], "bytes"), false);
  assert.equal(
    result.selectionPathFor("asset-a"),
    join(inputs.root, "asset-a.png"),
  );
  assert.throws(
    () => result.selectionPathFor("unknown-asset"),
    /asset input selection is unknown/,
  );
  assert.equal(Object.keys(result).includes("selectionPathFor"), false);
  assert.equal(JSON.stringify(result).includes(inputs.root), false);
});

test("accepts background-only operator input", async () => {
  const inputs = await syntheticInputs({ includeProp: false });
  const result = await loadGate3AssetInputs({
    manifestPath: inputs.manifestPath,
    inputRoot: inputs.root,
  });
  assert.equal(result.manifest.assetCount, 3);
  assert.equal(
    result.fixtures.some(({ role }) => role === "prop-image"),
    false,
  );
});

test("rejects authored IDs without an alphabetic prefix", async () => {
  for (const invalid of [
    "1test_prop-1",
    "-test_prop-1",
    "_test_prop-1",
  ]) {
    const assetInputs = await syntheticInputs();
    assetInputs.manifest.assets[0].assetId = invalid;
    await writeFile(
      assetInputs.manifestPath,
      `${JSON.stringify(assetInputs.manifest)}\n`,
    );
    await assert.rejects(
      loadGate3AssetInputs({
        manifestPath: assetInputs.manifestPath,
        inputRoot: assetInputs.root,
      }),
      /manifest entry is invalid/,
    );

    const propInputs = await syntheticInputs();
    propInputs.manifest.assets.at(-1).assignment.propId = invalid;
    await writeFile(
      propInputs.manifestPath,
      `${JSON.stringify(propInputs.manifest)}\n`,
    );
    await assert.rejects(
      loadGate3AssetInputs({
        manifestPath: propInputs.manifestPath,
        inputRoot: propInputs.root,
      }),
      /prop assignment is invalid/,
    );
  }
});

test("rejects a symlinked fixture", async () => {
  const inputs = await syntheticInputs();
  const target = join(inputs.root, "asset-c-target.png");
  await writeFile(target, png(3, 4, "asset-c"));
  await symlink(target, join(inputs.root, "asset-c-link.png"));
  inputs.manifest.assets[2].outputFile = "asset-c-link.png";
  await writeFile(
    inputs.manifestPath,
    `${JSON.stringify(inputs.manifest)}\n`,
  );
  await assert.rejects(
    loadGate3AssetInputs({
      manifestPath: inputs.manifestPath,
      inputRoot: inputs.root,
    }),
    /regular non-symlink file/,
  );
});

test("rejects fixture paths escaping the input root", async () => {
  const inputs = await syntheticInputs();
  const outside = join(inputs.root, "..", "palace-outside.png");
  await writeFile(outside, png(3, 4, "asset-c"));
  inputs.manifest.assets[2].outputFile = "../palace-outside.png";
  await writeFile(
    inputs.manifestPath,
    `${JSON.stringify(inputs.manifest)}\n`,
  );
  await assert.rejects(
    loadGate3AssetInputs({
      manifestPath: inputs.manifestPath,
      inputRoot: inputs.root,
    }),
    /canonical input-root descendant/,
  );
});

test("rejects manifest byte, digest, and dimension mismatches", async () => {
  for (const field of ["outputBytes", "outputSha256", "outputDimensions"]) {
    const inputs = await syntheticInputs();
    if (field === "outputBytes") inputs.manifest.assets[0][field] += 1;
    if (field === "outputSha256") {
      inputs.manifest.assets[0][field] = "0".repeat(64);
    }
    if (field === "outputDimensions") {
      inputs.manifest.assets[0][field] = [99, 99];
    }
    await writeFile(
      inputs.manifestPath,
      `${JSON.stringify(inputs.manifest)}\n`,
    );
    await assert.rejects(
      loadGate3AssetInputs({
        manifestPath: inputs.manifestPath,
        inputRoot: inputs.root,
      }),
      /bytes mismatch/,
    );
  }
});

test("rejects files larger than ten MiB before reading them", async () => {
  const inputs = await syntheticInputs();
  const oversized = Buffer.alloc(gate3AssetInputLimits.maxAssetBytes + 1);
  await writeFile(join(inputs.root, "asset-c.png"), oversized);
  await writeFile(
    inputs.manifestPath,
    `${JSON.stringify(inputs.manifest)}\n`,
  );
  await assert.rejects(
    loadGate3AssetInputs({
      manifestPath: inputs.manifestPath,
      inputRoot: inputs.root,
    }),
    /invalid file type or size/,
  );
});
