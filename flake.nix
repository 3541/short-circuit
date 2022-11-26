{
  description = "A high-performance static web server.";

  inputs = {
    nixpkgs.url = "nixpkgs/nixos-22.05";
    nixpkgs-unstable.url = "nixpkgs/nixos-unstable";
    utils.url = "github:numtide/flake-utils";
    flake-compat = {
      url = "github:edolstra/flake-compat";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, nixpkgs-unstable, utils, ... }:
    utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        llvm = pkgs.llvmPackages_latest;
      in rec {
        packages = utils.lib.flattenTree {
          short-circuit = pkgs.lib.recurseIntoAttrs (let
            scbuild = ({ buildType, san ? false, compiler, extra ? [ ]
              , extraMesonArgs ? "" }:
              pkgs.stdenv.mkDerivation {
                name = "short-circuit";
                version = "0.0.0";

                nativeBuildInputs = with pkgs;
                  [
                    compiler
                    meson
                    ninja
                    pkg-config
                    nixpkgs-unstable.legacyPackages.x86_64-linux.liburing
                  ] ++ extra;
                src = ./.;

                mesonArgs = (if buildType == "release" then
                  "-Db_lto=true"
                else
                  "-Db_coverage=true") + (pkgs.lib.optionalString san
                    "-Db_sanitize=address,undefined") + extraMesonArgs;

                configurePhase = ''
                  CC=${compiler}/bin/cc CXX=${compiler}/bin/c++ meson setup --prefix $out \
                      --buildtype ${buildType} --wrap-mode nodownload build
                '';
                buildPhase = "meson compile -C build";
                checkPhase = "meson test -C build";
                doCheck = true;
                installPhase = "mkdir -p $out && touch $out/PLACEHOLDER";
              });
            buildTypes = (compiler: extra: mesonArgs: {
              debug = scbuild {
                buildType = "debug";
                compiler = compiler;
                extra = extra;
                extraMesonArgs = mesonArgs;
              };
              san = scbuild {
                buildType = "debug";
                compiler = compiler;
                extra = extra;
                extraMesonArgs = mesonArgs;
                san = true;
              };
              release = scbuild {
                buildType = "release";
                compiler = compiler;
                extra = extra;
                extraMesonArgs = mesonArgs;
              };
            });
          in (buildTypes pkgs.gcc [ ] "") // {
            clang = pkgs.lib.recurseIntoAttrs
              (buildTypes llvm.libcxxClang [ llvm.libllvm ]
                "--native-file boilerplate/meson/clang.ini");
          });
        };
        defaultPackage = packages."short-circuit/release";

        devShell = pkgs.mkShell {
          packages = with pkgs; [
            gdb
            rr
            (clang-tools.overrideAttrs (prev: { clang = llvm.libcxxClang; }))
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

          inputsFrom = [
            packages."short-circuit/clang/debug"
            packages."short-circuit/debug"
          ];

          shellHook = ''
            unset AR
          '';
        };
      });
}
