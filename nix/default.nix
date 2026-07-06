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
          excludes = [ "^tests/" ];
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
          ]);
      };
    };
}
