# Nix

## Using last version through overlay in nixos config

Add this repository as an input to your flake:

```nix
nixpkgs.url = "nixpkgs/nixos-unstable";
skippy-xd = {
  url = "github:felixfung/skippy-xd";
  inputs.nixpkgs.follows = "nixpkgs";
};
```

Then use the overlay somewhere in your config:

```nix
{inputs, ...}: {nixpkgs.overlays = [inputs.skippy-xd.overlays.default];}
```

## Building binary

Just run following command in the source directory:

```shell
nix build
```

Then you can use the built binary:

```shell
./result/bin/skippy-xd --help
```
