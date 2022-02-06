# { pkgs ? import <nixpkgs> { } }:

# pkgs.mkShell {
#   nativeBuildInputs = with pkgs; [ liburing ];
#   packages = with pkgs; [
#     gdb
#     rr
#     # NOTE: The placement of clang-tools /before/ llvmPackages.clang is critical. Otherwise, the
#     # latter package will provide an un-wrapped `clangd`, which is unable to find system headers.
#     clang-tools
#     llvmPackages_latest.libcxxClang
#     meson
#     pkg-config
#     ninja
#   ];
# }

{ pkgs ? import <nixpkgs> { } }:
let
  llvm = pkgs.llvmPackages_latest;
  stdenv = llvm.libcxxStdenv;
in stdenv.mkDerivation {
  name = "short-circuit";
  nativeBuildInputs = with pkgs; [ gdb rr meson ninja pkg-config clang-tools ];
  buildInputs = with pkgs; [ liburing ];
  shellHook = ''
    export CPATH=$CPATH''${CPATH:+":"}${llvm.libcxx.dev}/include/c++/v1:${stdenv.glibc.dev}/include
    export CPLUS_INCLUDE_PATH=$CPATH
  '';
}
