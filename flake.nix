{
  description = "A high-performance static file web server.";

  inputs = {
    nixpkgs.url = "nixpkgs/nixos-23.11";
    nixpkgs-unstable.url = "nixpkgs/nixos-unstable";
    nixpkgs-cmake.url = "github:gracicot/nixpkgs/grc/update-cmake";
    utils.url = "github:numtide/flake-utils";

    flake-compat = {
      url = "github:edolstra/flake-compat";
      flake = false;
    };

    a3 = {
      url = "https://github.com/3541/liba3.git";
      type = "git";
      submodules = true;

      inputs = {
        nixpkgs.follows = "nixpkgs";
        utils.follows = "utils";
        flake-compat.follows = "flake-compat";
      };
    };
  };

  outputs = { self, nixpkgs, nixpkgs-unstable, nixpkgs-cmake, utils, a3, ... }:
    utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        llvm = pkgs.llvmPackages_17;

        pkgsGcc14 = import
          (nixpkgs-unstable.legacyPackages.${system}.applyPatches {
            name = "nixpkgs-gcc14";
            patches = [ ./gcc14.patch ];
            src = nixpkgs-unstable;
          }) { inherit system; };

        cmake = nixpkgs-cmake.legacyPackages.${system}.cmake;

        gcc14 = pkgsGcc14.wrapCC ((pkgsGcc14.gcc_latest.cc.override {
          majorMinorVersion = "14";
        }).overrideAttrs (prev: {
          version = "14.0.0-20240112";

          src = pkgs.fetchgit {
            url = "https://gcc.gnu.org/git/gcc.git";
            rev = "444a31f3b3542ccbecb67cef3a01df8aa9a43802";
            sha256 = "sha256-kmV6K61Mu8F+ykwqTvfasCZmE9fGZaQxAygWl688DpQ=";
          };
        }));
      in rec {
        packages = utils.lib.flattenTree {
          short-circuit = pkgs.lib.recurseIntoAttrs (let
            build = ({ buildType, sanitize ? false, compiler, extra ? [ ]
              , cmakeArgs ? "" }:
              pkgs.stdenv.mkDerivation {
                name = "short-circuit";
                version = "0.1.0-alpha";
                src = ./.;

                buildInputs = with pkgs; [
                  liburing
                  a3.packages.${system}."a3/clang/${buildType}"
                ];
                nativeBuildInputs = with pkgs;
                  [ compiler cmake ninja pkg-config ] ++ extra;

                cmakeArgs = cmakeArgs
                  + pkgs.lib.optionalString sanitize " -DSC_SANITIZE";

                configurePhase = ''
                  CMAKE_C_COMPILER=${compiler}/bin/cc CMAKE_CXX_COMPILER=${compiler}/bin/c++ cmake \
                      -DCMAKE_INSTALL_PREFIX=$out -DCMAKE_BUILD_TYPE=${buildType} -GNinja \
                      ${cmakeArgs} \
                      -B build -S .
                '';
                buildPhase = "ninja -C build";
                checkPhase = "ninja -C build test";
                doCheck = true;
                installPhase = "mkdir -p $out && ninja -C build install";
              });
            targets = (compiler: extra: cmakeArgs: {
              debug = build {
                buildType = "debug";
                inherit compiler extra cmakeArgs;
              };
              san = build {
                buildType = "debug";
                sanitize = true;
                inherit compiler extra cmakeArgs;
              };
              release = build {
                buildType = "release";
                inherit compiler extra cmakeArgs;
              };
            });
          in (targets gcc14 [ ] "") // {
            clang = pkgs.lib.recurseIntoAttrs (targets llvm.libcxxClang [ ]
              "--native-file boilerplate/meson/clang.ini");
          });
        };

        defaultPackage = packages."short-circuit/release";

        devShells = let
          s = build:
            pkgs.mkShell {
              packages = with pkgs; [
                gdb
                rr
                llvm.libllvm
                (pkgs.clang-tools_17.override { enableLibcxx = true; })
                moreutils
                (let unwrapped = include-what-you-use;
                in stdenv.mkDerivation {
                  pname = "include-what-you-use";
                  version = lib.getVersion unwrapped;

                  dontUnpack = true;

                  clang = llvm.libcxxClang;
                  inherit unwrapped;

                  installPhase = ''
                    runHook preInstall

                    mkdir -p $out/bin
                    substituteAll ${./nix-wrapper} $out/bin/include-what-you-use
                    chmod +x $out/bin/include-what-you-use

                    cp ${unwrapped}/bin/iwyu_tool.py $out/bin/iwyu_tool.py
                    sed -i \
                        "s,executable_name = '.*\$,executable_name = '$out/bin/include-what-you-use'," \
                        $out/bin/iwyu_tool.py

                    runHook postInstall
                  '';
                })
              ];

              inputsFrom = [ build ];

              shellHook = ''
                NIX_HARDENING_ENABLE="''${NIX_HARDENING_ENABLE/fortify/}"
                export NIX_HARDENING_ENABLE="''${NIX_HARDENING_ENABLE/fortify3/}"
              '';
            };
        in {
          default = s packages."short-circuit/clang/debug";
          gcc = s packages."short-circuit/debug";
        };
      });
}
