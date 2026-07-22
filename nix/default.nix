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
    let
      # Build the moba-sim Python package as a proper Nix derivation.
      # scikit-build-core compiles the nanobind extension via CMake.
      moba-sim-py = pkgs.python3Packages.buildPythonPackage {
        pname = "moba-sim";
        version = "0.1.0";
        src = ../.;
        pyproject = true;
        build-system = with pkgs.python3Packages; [
          scikit-build-core
          nanobind
        ];
        nativeBuildInputs = [
          pkgs.cmake
          pkgs.ninja
        ];
        dependencies = [ pkgs.python3Packages.numpy ];
        dontUseCmakeConfigure = true;
        # scikit-build-core needs to find our CMake project
        preConfigure = ''
          export CMAKE_ARGS="-DMOBA_SIM_BUILD_TESTS=OFF -DMOBA_SIM_BUILD_PYTHON=ON $CMAKE_ARGS"
        '';
        # The C++ static lib is built as part of the Python wheel build.
        pythonImportsCheck = [ "moba" ];
      };
    in
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
            # Docs toolchain
            doxygen
            (python3.withPackages (
              ps: with ps; [
                numpy
                pytest
                nanobind
                scikit-build-core
                moba-sim-py
                sphinx
                breathe
                furo
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

      packages.moba-sim-python = moba-sim-py;

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
