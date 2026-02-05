{
  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  inputs.flake-parts.url = "github:hercules-ci/flake-parts";

  outputs = inputs@{ nixpkgs, flake-parts, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [ "x86_64-linux" ];
      perSystem = { lib, pkgs, ... }: let
        # Tools used for development work (clangd, clang-format)
        devTools = [
          pkgs.clang-tools
          pkgs.gdb
          pkgs.ktx-tools
        ];
        # Extra build inputs not yet in the nixpkgs recipe.
        extraBuildInputs = [
          # XRT_FEATURE_DEBUG_GUI requires SDL2
          pkgs.sdl2-compat

          pkgs.librsvg
          pkgs.libpng
          pkgs.libarchive
        ];
        extraNativeBuildInputs = [
          pkgs.util-linux
        ];

        package = pkgs.enableDebugging (pkgs.wivrn.overrideAttrs (finalAttrs: oldAttrs: {
          # Filter the directories and files we don't need to keep to avoid needless rebuilds
          src = lib.cleanSourceWith {
            filter = name: type: let
              baseName = baseNameOf (toString name);
            in
            (lib.cleanSourceFilter name type) &&
            !(
              (type == "directory" && (
                baseName == ".direnv"
                || baseName == ".cxx"
                || lib.hasPrefix "build" baseName))
              || baseName == ".envrc"
            );
            src = ./.;
          };
          version = "next";

          # Because src is just a folder path and not a set from a fetcher, it doesn't need to be unpacked, so having a postUnpack throws an error.
          # We also don't need the check since we read the revision from the monado-rev file.
          postUnpack = null;

          separateDebugInfo = false;

          monado = pkgs.applyPatches {
            inherit (oldAttrs.monado) patches postPatch;
            # Force a refetch when the monado rev changes.
            src = pkgs.testers.invalidateFetcherByDrvHash pkgs.fetchFromGitLab {
              inherit (oldAttrs.monado.src) owner repo;
              domain = "gitlab.freedesktop.org";
              # Keep in sync with CMakeLists.txt monado rev
              rev = lib.strings.trim (builtins.readFile ./monado-rev);
              # Nix will output the correct hash when it doesn't match
              hash = "sha256-vZHrxFCRYzP2Kua/zYPvS4Xkp6yfzhHlFTnoKfHCicA=";
            };
          };

          buildInputs = oldAttrs.buildInputs ++ extraBuildInputs;
          nativeBuildInputs = oldAttrs.nativeBuildInputs ++ extraNativeBuildInputs;
        }));
      in {
        packages = {
          default = package;
          dissector = package.overrideAttrs (oldAttrs: {
            pname = "wivrn-dissector";
            cmakeFlags = (lib.filter (flag: !(lib.hasPrefix "-DWIVRN_BUILD" flag)) oldAttrs.cmakeFlags) ++ [
              (lib.cmakeBool "WIVRN_BUILD_CLIENT" false)
              (lib.cmakeBool "WIVRN_BUILD_SERVER" false)
              (lib.cmakeBool "WIVRN_BUILD_DASHBOARD" false)
              (lib.cmakeBool "WIVRN_BUILD_WIVRNCTL" false)
              (lib.cmakeBool "WIVRN_BUILD_DISSECTOR" true)
            ];
            buildInputs = oldAttrs.buildInputs ++ [
              (pkgs.symlinkJoin {
                name = "wireshark-dev-out-combined";
                paths = [
                  pkgs.wireshark.out
                  pkgs.wireshark.dev
                ];
              })
            ];
            preFixup = null;
          });
        };
        devShells.default = package.overrideAttrs (oldAttrs: {
          nativeBuildInputs = oldAttrs.nativeBuildInputs ++ devTools;
        });
      };
    };
}
