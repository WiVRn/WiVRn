{
  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  inputs.flake-parts.url = "github:hercules-ci/flake-parts";

  outputs = inputs@{ nixpkgs, flake-parts, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [ "x86_64-linux" ];
      perSystem = { pkgs, ... }: let
        # Tools used for development work (clangd, clang-format)
        devTools = with pkgs; [
          clang-tools
        ];
        # Extra build inputs not yet in the nixpkgs recipe.
        # Should be submitted to nixpkgs on release
        extraBuildInputs = with pkgs; [
          # XRT_FEATURE_DEBUG_GUI requires SDL2
          sdl2-compat
        ];

        package = pkgs.wivrn.overrideAttrs (finalAttrs: oldAttrs: {
          src = ./.;
          version = "next";

          # Because src is just a folder path and not a set from a fetcher, it doesn't need to be unpacked, so having a postUnpack throws an error.
          postUnpack = null;
          prePatch = ''
            ourMonadoRev="${finalAttrs.monado.src.rev}"
            theirMonadoRev=$(sed -n '/FetchContent_Declare(monado/,/)/p' CMakeLists.txt | grep "GIT_TAG" | awk '{print $2}')
            if [ ! "$theirMonadoRev" == "$ourMonadoRev" ]; then
              echo "Our Monado source revision doesn't match CMakeLists.txt." >&2
              echo "  theirs: $theirMonadoRev" >&2
              echo "    ours: $ourMonadoRev" >&2
              return 1
            fi
          '';

          monado = pkgs.applyPatches {
            inherit (oldAttrs.monado) patches postPatch;
            src = pkgs.fetchFromGitLab {
              inherit (oldAttrs.monado.src) owner repo;
              domain = "gitlab.freedesktop.org";
              # Keep in sync with CMakeLists.txt monado rev
              rev = "2a6932d46dad9aa957205e8a47ec2baa33041076";
              hash = "sha256-Bus9GTNC4+nOSwN8pUsMaFsiXjlpHYioQfBLxbQEF+0=";
            };
          };

          buildInputs = oldAttrs.buildInputs ++ extraBuildInputs;
        });
      in {
        packages.default = package;
        devShells.default = package.overrideAttrs (oldAttrs: {
          nativeBuildInputs = oldAttrs.nativeBuildInputs ++ devTools;
        });
      };
    };
}