{
  description = "Logos Palace public two-room Basecamp MVP";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    nixpkgs.follows = "logos-module-builder/nixpkgs";

    # Pinned by flake.lock. Prefer maintained forks for runtime dependencies.
    basecamp.url = "github:3esmit/logos-basecamp/69c18c8";
    basecamp.flake = false;
    delivery_module.url = "github:3esmit/logos-delivery-module";
    storage_module.url = "github:3esmit/logos-storage-module";
    lez_core.url = "github:3esmit/logos-execution-zone-module";
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
    in {
      packages = forAllSystems (system: {
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
      });
    };
}
