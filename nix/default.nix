{ inputs, ... }:
{
  systems = [
    "x86_64-linux"
    "aarch64-linux"
  ];

  imports = [
    inputs.git-hooks-nix.flakeModule
  ];

  perSystem =
    {
      config,
      pkgs,
      ...
    }:
    {
      formatter = pkgs.nixfmt-tree;
      pre-commit.settings.hooks = {
        nixfmt.enable = true;
        clang-format.enable = true;
        clang-tidy = {
          enable = true;
          excludes = [
            "^tests/"
            "^python/"
          ];
          args = [
            "--extra-arg=-std=c++23"
            "--extra-arg=-Iinclude"
          ];
        };
      };

      devShells.default = pkgs.mkShell {
        shellHook = ''
          ${config.pre-commit.shellHook}
        '';

        packages =
          config.pre-commit.settings.enabledPackages
          ++ (with pkgs; [
            # add packages to use in shell
            cmake
            ninja
            clang
            pkgsCross.mingwW64.buildPackages.gcc
            wine64
            clang-tools
            lldb
            boost
            catch2_3
            # Python bindings toolchain — bundle Python with packages so
            # PYTHONPATH is wired automatically (per NixOS wiki / withPackages).
            (python3.withPackages (
              ps: with ps; [
                nanobind
                scikit-build-core
                numpy
                pytest
                virtualenv
              ]
            ))
          ]);
      };

      packages.default = pkgs.stdenv.mkDerivation {
        name = "moba-sim";
        src = ../.;
        nativeBuildInputs = [
          pkgs.cmake
          pkgs.ninja
        ];
        cmakeFlags = [
          "-G Ninja"
          "-DMOBA_SIM_BUILD_TESTS=OFF"
        ];
        doCheck = false;
      };

      checks.tests = pkgs.stdenv.mkDerivation {
        name = "moba-sim-tests";
        src = ../.;
        nativeBuildInputs = [
          pkgs.cmake
          pkgs.ninja
        ];
        buildInputs = [ pkgs.catch2_3 ];
        cmakeFlags = [ "-G Ninja" ];
        doCheck = true;
        checkPhase = ''
          ctest --output-on-failure
        '';
        installPhase = "mkdir -p $out";
      };
    };
}
