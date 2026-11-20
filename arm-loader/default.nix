let
  sources = import ./nix/sources.nix;
  haskellNix = import sources."haskell.nix" { system = "x86_64-linux"; };
  pkgs = import sources.nixpkgs {
    inherit (haskellNix.nixpkgsArgs) system config;
    overlays = haskellNix.nixpkgsArgs.overlays;
  };
  arm = pkgs.pkgsCross.arm-embedded;
  tlsf = arm.stdenv.mkDerivation {
    name = "tlsf";
    src = pkgs.lib.cleanSource ./tlsf;
  };
  common = arm.callPackage ./common { inherit tlsf; };
  notc = arm.stdenv.mkDerivation {
    name = "notc";
    src = pkgs.lib.cleanSource ./notc;
    buildInputs = [ common ];
    enableParallelBuilding = true;
    hardeningDisable = [ "fortify" "stackprotector" ];
    dontStrip = true;
  };
in arm.stdenv.mkDerivation {
  name = "chainloader";
  src = pkgs.lib.cleanSource ./arm_chainloader;
  buildInputs = [ tlsf common notc ];
  enableParallelBuilding = true;
  installPhase = ''
    mkdir -p $out
    cp build/arm_chainloader.{bin,elf,map} $out/
  '';
}
