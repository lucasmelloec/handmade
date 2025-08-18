{
  inputs = {
    nixpkgs.url = "nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };
  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        formatter = pkgs.nixfmt-rfc-style;
        devShells.default = pkgs.mkShell rec {
          packages = with pkgs; [
            gcc
            clang-tools
            clang
            gdb
            # Used to allow 32 bit compilation
            # pkgs.pkgsi686Linux.glibc.dev
          ];

          buildInputs = with pkgs; [
            xorg.libX11
            xorg.xorgproto
            libevdev
            alsa-lib
          ];

          env = {
            LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath buildInputs;

            CPATH = builtins.concatStringsSep ":" [
              (pkgs.lib.makeSearchPathOutput "dev" "include" buildInputs)
              "${pkgs.libevdev}/include/libevdev-1.0"
            ];

            NIX_HARDENING_ENABLE = "";
          };
        };
      }
    );
}
