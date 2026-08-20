{
  description = "Logos Palace public two-room Basecamp MVP";

  inputs = {
    logos-module-builder.url = "github:3esmit/logos-module-builder/1afad12";
    nixpkgs.follows = "logos-module-builder/nixpkgs";

    # Pinned by flake.lock. Prefer maintained forks for runtime dependencies.
    basecamp.url = "github:3esmit/logos-basecamp/4313ef96a20887552b1dced32d41347a3c16944b";
    basecamp.flake = false;
    delivery_module.url = "github:3esmit/logos-delivery-module/9287a3412976e4af171df66d0feeb4555179a60d";
    delivery_module.inputs.logos-module-builder.url = "github:3esmit/logos-module-builder?rev=324b459c3f7b59171d249f3ccbcc362403b3fcaf";
    storage_module.url = "github:3esmit/logos-storage-module/753dc2c";
    storage_module.inputs.logos-module-builder.follows = "logos-module-builder";
    lez_core.url = "github:3esmit/logos-execution-zone-module/62b8a89";
    lez_core.inputs.logos-module-builder.follows = "logos-module-builder";
  };

  outputs = inputs@{ nixpkgs, logos-module-builder, ... }:
    let
      palaceVm = logos-module-builder.lib.mkLogosModule {
        src = ./packages/palace_vm;
        configFile = ./packages/palace_vm/metadata.json;
        flakeInputs = inputs;
      };

      palaceCore = logos-module-builder.lib.mkLogosModule {
        src = ./packages/palace_core;
        configFile = ./packages/palace_core/metadata.json;
        flakeInputs = inputs // { palace_vm = palaceVm; };
      };

      palaceUi = logos-module-builder.lib.mkLogosQmlModule {
        src = ./packages/logos_palace_ui;
        configFile = ./packages/logos_palace_ui/metadata.json;
        flakeInputs = inputs // {
          palace_vm = palaceVm;
          palace_core = palaceCore;
        };
      };

      systems = builtins.attrNames palaceVm.packages;
      forAllSystems = nixpkgs.lib.genAttrs systems;
      palaceCoreProductionFixtureAuditFor = system:
        let
          pkgs = import nixpkgs { inherit system; };
          productionCoreLib = palaceCore.packages.${system}.lib;
          productionUiLib = palaceUi.packages.${system}.lib;
        in pkgs.runCommand
          "logos-palace-production-binary-audit"
          {
            nativeBuildInputs = [
              pkgs.binutils
              pkgs.findutils
              pkgs.gnugrep
            ];
          }
          ''
            core_plugin="$(
              find "${productionCoreLib}" -type f \
                -name 'palace_core_plugin.*' -print -quit
            )"
            if [ -z "$core_plugin" ]; then
              echo "Production Core plugin was not found" >&2
              exit 1
            fi
            ui_plugin="$(
              find "${productionUiLib}" -type f \
                -name 'logos_palace_ui_plugin.*' -print -quit
            )"
            if [ -z "$ui_plugin" ]; then
              echo "Production UI plugin was not found" >&2
              exit 1
            fi
            for production_artifact in \
              "${productionCoreLib}" \
              "${productionUiLib}"
            do
              for forbidden in \
                logos-palace-storage-acceptance-holder-v1 \
                palace_delivery_acceptance_fixture.cpp \
                applicationRoundTrip \
                acceptanceRoundTripResponse \
                ScreenshotFence \
                frameTiming \
                assetAuthoringEvidence \
                deliveryNodeEvidence \
                gate1 gate2 gate3 gate4 gate5 \
                9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60 \
                4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb \
                c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7
              do
                if grep -R -a -F -q \
                  -- "$forbidden" "$production_artifact"; then
                  echo "Production Palace artifact contains forbidden marker: $forbidden" >&2
                  exit 1
                fi
            done
            done
            for plugin in "$core_plugin" "$ui_plugin"; do
              for forbidden_wide in \
                palaceAcceptanceProfile \
                palaceAcceptanceHolderProfile
              do
                if strings -a -el "$plugin" \
                    | grep -F -- "$forbidden_wide" >/dev/null; then
                  echo "Production Palace plugin contains forbidden profile hook" >&2
                  exit 1
                fi
            done
            done
            mkdir -p "$out"
            touch "$out/passed"
          '';
      palaceImageIdFor = system:
        let pkgs = import nixpkgs { inherit system; };
        in pkgs.rustPlatform.buildRustPackage {
          pname = "palace-image-id";
          version = "0.1.0";
          src = ./tools/palace-image-id;
          cargoLock.lockFile = ./tools/palace-image-id/Cargo.lock;
          strictDeps = true;
          doCheck = true;
        };
      palaceReleaseArtifactFor = system:
        let
          pkgs = import nixpkgs { inherit system; };
          palaceImageId = palaceImageIdFor system;
          lgxPackageOutputs = [
            {
              artifact = "logos-palace_vm-module-lib.lgx";
              output = palaceVm.packages.${system}.lgx-portable;
            }
            {
              artifact = "logos-palace_core-module-lib.lgx";
              output = palaceCore.packages.${system}.lgx-portable;
            }
            {
              artifact = "logos-logos_palace_ui-module.lgx";
              output = palaceUi.packages.${system}.lgx-portable;
            }
            {
              artifact = "logos-delivery_module-module-lib.lgx";
              output = inputs.delivery_module.packages.${system}.lgx-portable;
            }
            {
              artifact = "logos-storage_module-module-lib.lgx";
              output = inputs.storage_module.packages.${system}.lgx-portable;
            }
            {
              artifact = "logos-lez_core-module-lib.lgx";
              output = inputs.lez_core.packages.${system}.lgx-portable;
            }
          ];
          lgxHashChecks = if system == "x86_64-linux" then
            pkgs.lib.concatStringsSep "\n" (map
              (package: ''
                expected_hash=$(jq -er --arg artifact "${package.artifact}" \
                  '.packages[] | select(.artifact == $artifact) | .sha256' \
                  "$source_manifest")
                actual_hash=$(sha256sum "${package.output}/${package.artifact}" \
                  | cut -d' ' -f1)
                if [ "$actual_hash" != "$expected_hash" ]; then
                  printf 'release LGX hash mismatch: %s\\n' \
                    "${package.artifact}" >&2
                  exit 1
                fi
              '') lgxPackageOutputs)
          else
            "printf 'release LGX hashes are pinned for x86_64-linux; skipping native check on ${system}\\n'"
          ;
          releasePreflightTests = pkgs.lib.fileset.toSource {
            root = ./tests;
            fileset = pkgs.lib.fileset.unions [
              ./tests/basecamp_release_preflight.mjs
              ./tests/basecamp_release_preflight.test.mjs
            ];
          };
        in pkgs.runCommand
          "logos-palace-risc0-release"
          {
            nativeBuildInputs = [
              pkgs.coreutils
              pkgs.jq
              pkgs.nodejs
              palaceImageId
            ];
          }
          ''
            source_manifest=${./program/release/release.json}
            expected_size=297312
            expected_sha=69099492e8f860ab338846f33762f5fb563ffc40121779ec1e15ef0b35da7171
            expected_image=e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61

            jq -e \
              --argjson size "$expected_size" \
              --arg sha "$expected_sha" \
              --arg image "$expected_image" \
              '
                .schema == "logos.palace.release"
                and .version == 2
                and .platform == "x86_64-linux"
                and .risc0BinfmtVersion == "3.0.5"
                and .byteLength == $size
                and .sha256 == $sha
                and .imageIdHex == $image
                and (.packages | length == 6)
                and (.schemas.vmProfile == "classic-mvp-v1")
                and (.schemas.lezVmProfile == "iptscrae_mvp_v1")
                and (.network.lezNetworkId == "logos-lez-testnet-v0.2.0")
              ' "$source_manifest" >/dev/null

            ${lgxHashChecks}

            mkdir -p "$out/bin" "$out/share/logos-palace"
            install -m 0555 \
              ${palaceImageId}/bin/palace-image-id \
              "$out/bin/palace-image-id"
            install -m 0444 "$source_manifest" \
              "$out/share/logos-palace/release.json"

            PALACE_RELEASE_ARTIFACT="$out" \
              node --test \
                ${releasePreflightTests}/basecamp_release_preflight.test.mjs
          '';
      palaceProductChecksFor = system:
        let
          pkgs = import nixpkgs { inherit system; };
          productSource = pkgs.lib.fileset.toSource {
            root = ./.;
            fileset = pkgs.lib.fileset.unions [
              ./packages/logos_palace_ui/src/qml
              ./packages/logos_palace_ui/src/logos_palace_ui_backend.cpp
              ./packages/logos_palace_ui/src/logos_palace_ui_backend.h
              ./packages/logos_palace_ui/src/logos_palace_ui.rep
              ./packages/logos_palace_ui/metadata.json
              ./packages/palace_core/src/palace_core_impl.cpp
              ./tests/basecamp_local_mvp_runner.test.mjs
              ./scripts/run-basecamp-local-mvp.sh
              ./scripts/run-palace-e2e.sh
              ./tests/palace_asset_inputs.mjs
              ./tests/palace_ui_lifecycle.mjs
              ./tests/palace_ui_lifecycle.test.mjs
              ./tests/basecamp_local_mvp_user_flow.mjs
              ./tests/palace_e2e_user_story.mjs
            ];
          };
        in pkgs.runCommand "logos-palace-product-checks"
          { nativeBuildInputs = [ pkgs.nodejs ]; }
          ''
            node --check ${productSource}/tests/palace_e2e_user_story.mjs
            node --check ${productSource}/tests/basecamp_local_mvp_user_flow.mjs
            node --test ${productSource}/tests/basecamp_local_mvp_runner.test.mjs \
              ${productSource}/tests/palace_ui_lifecycle.test.mjs
            mkdir -p "$out"
            touch "$out/passed"
          '';
    in {
      packages = forAllSystems (system:
        let
          palaceImageId = palaceImageIdFor system;
          palaceReleaseArtifact = palaceReleaseArtifactFor system;
        in {
          palace-image-id = palaceImageId;
          palace-release-artifact = palaceReleaseArtifact;
          delivery-module-lgx-portable =
            inputs.delivery_module.packages.${system}.lgx-portable;
          palace-core-production-fixture-audit =
            palaceCoreProductionFixtureAuditFor system;
          storage-module-lgx-portable =
            inputs.storage_module.packages.${system}.lgx-portable;
          lez-core-lgx-portable =
            inputs.lez_core.packages.${system}.lgx-portable;
          palace-vm = palaceVm.packages.${system}.default;
          palace-vm-lgx = palaceVm.packages.${system}.lgx;
          palace-vm-lgx-portable = palaceVm.packages.${system}.lgx-portable;
          palace-core = palaceCore.packages.${system}.default;
          palace-core-lgx = palaceCore.packages.${system}.lgx;
          palace-core-lgx-portable =
            palaceCore.packages.${system}.lgx-portable;
          logos-palace-ui = palaceUi.packages.${system}.default;
          logos-palace-ui-lgx = palaceUi.packages.${system}.lgx;
          logos-palace-ui-lgx-portable =
            palaceUi.packages.${system}.lgx-portable;
          default = palaceUi.packages.${system}.default;
        });

      checks = forAllSystems (system:
        {
          palace-vm-contracts = palaceVm.checks.${system}.unit-tests;
          palace-core-contracts = palaceCore.checks.${system}.unit-tests;
          palace-core-production-fixture-audit =
            palaceCoreProductionFixtureAuditFor system;
          palace-product-checks = palaceProductChecksFor system;
          palace-release-artifact = palaceReleaseArtifactFor system;
        });
    };
}
