{ pkgs ? import <nixpkgs> { } }:
pkgs.stdenv.mkDerivation {
  name = "short-circuit";
  nativeBuildInputs = with pkgs; [ gdb rr meson ninja pkg-config ];
  buildInputs = with pkgs; [ liburing ];
  shellHook = ''
    unset CPATH
    unset CPLUS_INCLUDE_PATH
  '';
}
