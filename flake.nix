{
  description = "Logos Palace public two-room Basecamp MVP";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder/0.2.5";
    nixpkgs.follows = "logos-module-builder/nixpkgs";

    # Pinned by flake.lock. Prefer maintained forks for runtime dependencies.
    basecamp.url = "github:3esmit/logos-basecamp/fd13085f7fda6a8b1ada53a959d3064c5747c9d1";
    basecamp.flake = false;
    delivery_module.url = "github:3esmit/logos-delivery-module/891c43bd6176e17b0aa536ef1aa369bb47e918f4";
    delivery_module.inputs.logos-module-builder.follows = "logos-module-builder";
    storage_module.url = "github:3esmit/logos-storage-module/1c75ad9d1f02f562e845a2c445421bee6ea425ad";
    lez_core.url = "github:3esmit/logos-execution-zone-module/e8d84103660604b1a6a06ddd66d20da7a2fdeb3f";
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

      # Acceptance gates may receive a Core binary compiled with public
      # fixture identities. Production Core outputs keep the option disabled.
      palaceCoreAcceptance = logos-module-builder.lib.mkLogosModule {
        src = ./packages/palace_core;
        configFile = ./packages/palace_core/metadata.json;
        flakeInputs = inputs // { palace_vm = palaceVm; };
        preConfigure = ''
          cmakeFlagsArray+=(
            "-DPALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE=ON"
          )
        '';
      };

      palaceUi = logos-module-builder.lib.mkLogosQmlModule {
        src = ./packages/logos_palace_ui;
        configFile = ./packages/logos_palace_ui/metadata.json;
        flakeInputs = inputs // {
          palace_vm = palaceVm;
          palace_core = palaceCore;
        };
      };

      palaceDeliveryAcceptance = logos-module-builder.lib.mkLogosQmlModule {
        src = ./packages/palace_delivery_acceptance;
        configFile = ./packages/palace_delivery_acceptance/metadata.json;
        flakeInputs = inputs;
      };

      systems = builtins.attrNames palaceVm.packages;
      forAllSystems = nixpkgs.lib.genAttrs systems;
      palaceCoreProductionFixtureAuditFor = system:
        let
          pkgs = import nixpkgs { inherit system; };
          productionCoreLib = palaceCore.packages.${system}.lib;
        in pkgs.runCommand
          "logos-palace-core-production-fixture-audit"
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
            for forbidden in \
              logos-palace-storage-acceptance-holder-v1 \
              palace_delivery_acceptance_fixture.cpp \
              fixture-cid-hat \
              9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60 \
              4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb \
              c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7
            do
              if grep -R -a -F -q \
                -- "$forbidden" "${productionCoreLib}"; then
                echo "Production Core contains an acceptance fixture marker" >&2
                exit 1
              fi
            done
            for forbidden_wide in \
              palaceAcceptanceProfile \
              palaceAcceptanceHolderProfile
            do
              if strings -a -el "$core_plugin" \
                  | grep -F -- "$forbidden_wide" >/dev/null; then
                echo "Production Core contains an acceptance profile hook" >&2
                exit 1
              fi
            done
            mkdir -p "$out"
            touch "$out/passed"
          '';
      palaceCoreAcceptanceFixtureAuditFor = system:
        let
          pkgs = import nixpkgs { inherit system; };
          acceptanceCoreLib =
            palaceCoreAcceptance.packages.${system}.lib;
        in pkgs.runCommand
          "logos-palace-core-acceptance-fixture-audit"
          {
            nativeBuildInputs = [
              pkgs.binutils
              pkgs.findutils
              pkgs.gnugrep
            ];
          }
          ''
            core_plugin="$(
              find "${acceptanceCoreLib}" -type f \
                -name 'palace_core_plugin.*' -print -quit
            )"
            if [ -z "$core_plugin" ]; then
              echo "Acceptance Core plugin was not found" >&2
              exit 1
            fi
            for required in \
              logos-palace-storage-acceptance-holder-v1 \
              palace_delivery_acceptance_fixture.cpp \
              9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60
            do
              if ! grep -R -a -F -q \
                -- "$required" "${acceptanceCoreLib}"; then
                echo "Acceptance Core is missing its fixture marker" >&2
                exit 1
              fi
            done
            for required_wide in \
              palaceAcceptanceProfile \
              palaceAcceptanceHolderProfile
            do
              if ! strings -a -el "$core_plugin" \
                  | grep -F -- "$required_wide" >/dev/null; then
                echo "Acceptance Core is missing a profile hook" >&2
                exit 1
              fi
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
                (keys | sort) == ([
                  "schema",
                  "version",
                  "risc0BinfmtVersion",
                  "byteLength",
                  "sha256",
                  "imageIdHex"
                ] | sort)
                and .schema == "logos.palace.risc0-release"
                and .version == 1
                and .risc0BinfmtVersion == "3.0.5"
                and .byteLength == $size
                and .sha256 == $sha
                and .imageIdHex == $image
              ' "$source_manifest" >/dev/null

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
    in {
      packages = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          palaceImageId = palaceImageIdFor system;
          palaceReleaseArtifact = palaceReleaseArtifactFor system;
        in {
        acceptance-tools =
          pkgs.symlinkJoin {
            name = "logos-palace-acceptance-tools";
            paths = [
              pkgs.coreutils
              pkgs.findutils
              pkgs.jq
              pkgs.nodejs
              pkgs.util-linux
            ];
          };
        palace-image-id = palaceImageId;
        palace-release-artifact = palaceReleaseArtifact;
        delivery-module-lgx-portable = inputs.delivery_module.packages.${system}.lgx-portable;
        palace-delivery-acceptance-lgx-portable =
          palaceDeliveryAcceptance.packages.${system}.lgx-portable;
        palace-core-acceptance-lgx-portable =
          palaceCoreAcceptance.packages.${system}.lgx-portable;
        palace-core-production-fixture-audit =
          palaceCoreProductionFixtureAuditFor system;
        palace-core-acceptance-fixture-audit =
          palaceCoreAcceptanceFixtureAuditFor system;
        storage-module-lgx-portable = inputs.storage_module.packages.${system}.lgx-portable;
        lez-core-lgx-portable = inputs.lez_core.packages.${system}.lgx-portable;
        palace-vm = palaceVm.packages.${system}.default;
        palace-vm-lgx = palaceVm.packages.${system}.lgx;
        palace-vm-lgx-portable = palaceVm.packages.${system}.lgx-portable;
        palace-core = palaceCore.packages.${system}.default;
        palace-core-lgx = palaceCore.packages.${system}.lgx;
        palace-core-lgx-portable = palaceCore.packages.${system}.lgx-portable;
        logos-palace-ui = palaceUi.packages.${system}.default;
        logos-palace-ui-lgx = palaceUi.packages.${system}.lgx;
        logos-palace-ui-lgx-portable = palaceUi.packages.${system}.lgx-portable;
        default = palaceUi.packages.${system}.default;
      });

      checks = forAllSystems (system: {
        palace-vm-contracts = palaceVm.checks.${system}.unit-tests;
        palace-core-contracts = palaceCore.checks.${system}.unit-tests;
        palace-core-production-fixture-audit =
          palaceCoreProductionFixtureAuditFor system;
        palace-core-acceptance-fixture-audit =
          palaceCoreAcceptanceFixtureAuditFor system;
        palace-release-artifact = palaceReleaseArtifactFor system;
      });
    };
}
