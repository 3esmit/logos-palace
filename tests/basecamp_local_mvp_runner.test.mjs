#!/usr/bin/env node

import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import test from "node:test";

const runnerPath = fileURLToPath(
  new URL("../scripts/run-basecamp-local-mvp.sh", import.meta.url),
);
const flowPath = fileURLToPath(
  new URL("./basecamp_local_mvp_user_flow.mjs", import.meta.url),
);

test("local MVP runner requires explicit runtime and asset inputs", async () => {
  const runner = await readFile(runnerPath, "utf8");
  for (const variable of [
    "PALACE_LOCAL_MVP_SEQUENCER",
    "PALACE_LOCAL_MVP_SEQUENCER_CONFIG",
    "PALACE_LOCAL_MVP_BASECAMP",
    "PALACE_LOCAL_MVP_DEPLOY_TOOL",
    "PALACE_LOCAL_MVP_PROGRAM",
    "PALACE_LOCAL_MVP_CORE_LGX",
    "PALACE_LOCAL_MVP_UI_LGX",
    "PALACE_E2E_ASSET_INPUT_ROOT",
    "PALACE_E2E_ASSET_MANIFEST",
  ]) {
    assert.match(runner, new RegExp(variable));
  }
  assert.match(runner, /PALACE_LEZ_PROFILE.*local-development/);
  assert.match(runner, /basecamp_local_mvp_user_flow\.mjs/);
  assert.doesNotMatch(runner, /\/home\//);
  assert.doesNotMatch(runner, /PALACE_E2E_ASSET_DIR|PALACE_E2E_ASSET_INPUT_ROOT=.*default/);
});

test("local MVP user story derives assets from the validated manifest", async () => {
  const flow = await readFile(flowPath, "utf8");
  assert.match(flow, /loadGate3AssetInputs/);
  assert.match(flow, /PALACE_E2E_ASSET_INPUT_ROOT/);
  assert.match(flow, /PALACE_E2E_ASSET_MANIFEST/);
  assert.match(flow, /selectionPathFor/);
  assert.doesNotMatch(flow, /PALACE_E2E_ASSET_DIR/);
  assert.doesNotMatch(flow, /PALACE_E2E_ASSET_DIR|\/home\//);
});
