{ pkgs ? import <nixpkgs> { } }:
pkgs.mkShell {
  buildInputs = [ pkgs.liburing ];
  nativeBuildInputs = with pkgs; [
    llvmPackages.clang
    gcc
    doxygen
    meson
    gtest
    pkg-config
    ninja
  ];
  packages = with pkgs; [ gdb rr clang-tools wrk valgrind inetutils ];
}
