{
  description = "C countdown/stopwatch program";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };
  outputs =
    { nixpkgs, ... }:
    let
      inherit (nixpkgs) lib;
      systems = lib.systems.flakeExposed;
      genSystemAttrs =
        fn:
        lib.attrsets.zipAttrsWith (_: lib.attrsets.mergeAttrsList) (
          map (
            system:
            lib.attrsets.mapAttrs (_: outputs: { ${system} = outputs; }) (fn rec {
              inherit system;
              pkgs = nixpkgs.legacyPackages.${system};
              lib = pkgs.lib;
            })
          ) systems
        );
    in
    genSystemAttrs (
      { pkgs, ... }:
      let
        buildInputs = with pkgs; [
          figlet
          coreutils
        ];
      in
      {
        devShells.default = pkgs.mkShell {
          inherit buildInputs;
        };
        packages = rec {
          timer = pkgs.gccStdenv.mkDerivation {
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
