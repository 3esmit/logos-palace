// Compatibility wrapper for historical contract tests. The supported local
// story imports palace_asset_inputs.mjs directly.
export {
  loadPalaceAssetInputs as loadGate3AssetInputs,
  palaceAssetInputLimits as gate3AssetInputLimits,
} from "./palace_asset_inputs.mjs";
