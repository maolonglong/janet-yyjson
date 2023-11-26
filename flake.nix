{
  description = "janet-yyjson";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    nur.url = "github:nix-community/NUR";
    maolonglong-nur.url = "github:maolonglong/nur-packages";
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
    nur,
    maolonglong-nur,
    ...
  }: let
    overlays = [
      (final: prev: {
        nur = import nur {
          nurpkgs = prev;
          pkgs = prev;
          repoOverrides = {
            maolonglong = import maolonglong-nur {pkgs = prev;};
          };
        };
      })
    ];
  in
    flake-utils.lib.eachDefaultSystem (
      system: let
        pkgs = import nixpkgs {
          inherit system overlays;
        };
      in {
        devShells.default = pkgs.mkShell {
          buildInputs =
            (with pkgs; [
              git
              just
            ])
            ++ (with pkgs.nur.repos.maolonglong; [
              janet
              jpm
            ]);
          shellHook = ''
            export JANET_TREE="$PWD/.jpm_tree"
            export JANET_PATH="$JANET_TREE/lib"
            export PATH="$JANET_TREE/bin:$PATH"
          '';
        };
      }
    );
}
