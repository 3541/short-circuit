{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  nativeBuildInputs = with pkgs; [ liburing ];
  packages = with pkgs; [
    gcc
    gtest
    gdb
    rr
    # NOTE: The placement of clang-tools /before/ llvmPackages.clang is critical. Otherwise, the
    # latter package will provide an un-wrapped `clangd`, which is unable to find system headers.
    clang-tools
    llvmPackages_latest.clang
    meson
    pkg-config
    ninja
  ];
}
