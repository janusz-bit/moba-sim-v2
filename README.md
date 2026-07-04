# moba-sim

A C++23 library for simulating MOBA-style champion stat aggregation, inspired by
[League of Legends](https://wiki.leagueoflegends.com/). Models the
Base/Inc/More modifier pipeline used to compute final stats from items, buffs,
and passives.

## Build

Requires [Nix](https://nixos.org/) with flakes enabled. The development shell
provides the toolchain (clang, CMake, Catch2) and pre-commit hooks.

```sh
nix develop
cmake -B build
cmake --build build
```

## Test

Tests use [Catch2 v3](https://github.com/catchorg/Catch2), wired through CTest.

```sh
ctest --test-dir build
```

## Project layout

```
include/moba_sim.hpp   Public API: Stat, ModType, ModDB, Champion, passives
src/moba_sim.cpp       Implementation
tests/                 Catch2 test suite (one file per component)
nix/default.nix        Flake module: devShell, pre-commit hooks
```

## Stat model

A stat's final value combines three modifier types:

- **Base** — additive bonuses (`10 + 20 + 30`)
- **Inc** — multiplicative increases summed from 1.0 (`1.0 + 0.1 + 0.2`)
- **More** — multiplicative multipliers from 1.0 (`1.1 * 1.2 * 1.3`)

`getStat(stat) = sum(stat) * inc(stat) * more(stat)`.

`Champion::applyPassives(base, final)` applies each passive independently — each
sees the same `final` and returns a **bonus** (delta); bonuses are summed and
added to `base`. The free function `evaluateChampion(champion, eps)` iterates
`final = applyPassives(base, final)` until convergence (`getDeltaStats <= eps`),
resolving fixed-point interactions between passives.