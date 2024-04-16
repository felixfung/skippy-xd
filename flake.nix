{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

    gitignore.url = "github:hercules-ci/gitignore.nix";
    gitignore.inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs = inputs @ {
    nixpkgs,
    flake-parts,
    gitignore,
    ...
  }:
    flake-parts.lib.mkFlake {inherit inputs;}
    {
      imports = [
        flake-parts.flakeModules.easyOverlay
      ];

      systems = ["x86_64-linux" "aarch64-linux"];

      perSystem = {
        pkgs,
        self',
        ...
      }: let
        inherit (gitignore.lib) gitignoreSource;
        src = gitignoreSource ./.;
        version = "0.7.2";
      in {
        overlayAttrs = {
          inherit (self'.packages) skippy-xd;
        };

        packages.skippy-xd = pkgs.skippy-xd.overrideAttrs (_oldAttrs: {inherit src version;});
        packages.default = self'.packages.skippy-xd;
      };
    };
}
