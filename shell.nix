{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  packages = with pkgs; [
    gcc
    gdb
    rr
    # NOTE: The placement of clang-tools /before/ llvmPackages.clang is critical. Otherwise, the
    # latter package will provide an un-wrapped `clangd`, which is unable to find system headers.
    (clang-tools.override { llvmPackages = llvmPackages_12; })
    llvmPackages_latest.clang
    doxygen
    meson
    gtest
    pkg-config
    ninja
    liburing
    wrk
    valgrind
  ];
}
