{
  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";

  outputs = { nixpkgs, ... }:
  let
    systems = [ "x86_64-linux" ];
    forEachSystem = f: nixpkgs.lib.genAttrs systems (sys: f nixpkgs.legacyPackages.${sys});
  in
  {
    devShells = forEachSystem (pkgs: {
      default = pkgs.mkShell {
        name = "wivrn";
        inherit (pkgs.wivrn) buildInputs;
        nativeBuildInputs = with pkgs; wivrn.nativeBuildInputs ++ [
          clang-tools
        ];
      };
    });
  };
}