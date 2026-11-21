{
  description = "C countdown/stopwatch program";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };
  outputs =
    { nixpkgs, ... }:
    let
      inherit (nixpkgs) lib;
      merge =
        with builtins;
        x:
        if length x == 1 then
          head x
        else if length x > 1 && all isAttrs x then
          zipAttrsWith (name: merge) x
        else if length x > 1 && all isList x then
          foldl' (acc: cur: acc ++ cur) [ ] x
        else
        # Doesn't match other conditions, fail
        if length x == 0 then
          throw "Cannot merge no items"
        else
          trace "Trying to merge" trace x throw "Don't know how to merge this";
      systems = lib.systems.flakeExposed;
      genSystemAttrs =
        fn:
        merge (
          map (
            system:
            lib.attrsets.mapAttrs (name: value: { ${system} = value; }) (
              fn system nixpkgs.legacyPackages.${system}
            )
          ) systems
        );
    in
    genSystemAttrs (
      system: pkgs:
      let
        buildInputs = with pkgs; [
          figlet
          coreutils
        ];
        stdenv = pkgs.gccStdenv;
      in
      {
        devShells.default = pkgs.mkShell {
          inherit buildInputs;
        };
        packages = rec {
          timer = stdenv.mkDerivation {
            nativeBuildInputs = [ pkgs.cmake ];
            inherit buildInputs;
            name = "timer";
            src = ./.;
            installPhase = "install -D timer $out/bin/timer";
          };
          default = timer;
        };
      }
    );
}
