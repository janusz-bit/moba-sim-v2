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

## Champion pipeline

```
mod_db (modifiers)  →  getBaseStats()  →  passives  →  final
```

`Champion::getBaseStats()` reads from `mod_db` and returns the array of all
stats after the Base/Inc/More pipeline.

## Passives

A `Passive` is a `std::function` with signature:

```cpp
PassiveResult(const Stats &base, const Stats &final, Type time) -> { Stats bonus; bool alive; }
```

- Receives `base` (stats from `mod_db`), `final` (current result), and `time`
  (absolute simulation time, starts at 0 and only increases).
- Returns a `bonus` (delta added to `final`) and an `alive` flag.

A passive **is the sole authority on its lifetime** via the `alive` flag:

- **permanent**: always returns `alive=true` (never removed)
- **one-shot**: returns `alive=false` after its single application
- **temp**: returns `alive=false` once it decides to expire (e.g. by capturing
  a start time and checking `time - start < duration`)

All passives live in a single `passives` queue. The framework treats them
uniformly: call, sum the bonus, remove if `alive=false`. A passive may capture
a `start_time` and `duration`, reset itself, or change its effect — the
framework knows nothing about time/counters/handles; the passive is a black
box with access to `time`.

## `applyPassives(base, final, time)` — one simulation step

Applies all passives in a single step:

1. For each passive: call it, sum the bonus.
2. If `alive=false`, erase it (using the iterator returned by `erase`); the
   bonus is still applied for this step.

Returns `base + sum(bonuses)`. Mutates `passives` (removes expired/one-shot).

## `evaluateChampion(eps, max_iter)` — fixed-point of all passives

Iterates **all passives** to resolve fixed-point interactions, but **does not
remove** any during iteration — passives are applied repeatedly with
`time = 0.0` (the fixed-point is immediate, not a simulation step). After
convergence, passives that returned `alive=false` on the final iteration are
removed; those with `alive=true` stay.

```
final = base
repeat:
  prev = final
  final = base + sum(passive(base, prev, 0.0))   // no removal during iteration
until delta(final, prev) <= eps  or  iter >= max_iter
then: remove passives where alive=false (from the final iteration)
```

Throws `ConvergenceError` if not converged within `max_iter`.

## Usage

**Static champion** (no time):

```cpp
Champion c;
c.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
c.passives.push_back([](const Stats &base, const Stats &final, Type) {
  Stats bonus{};
  bonus[std::to_underlying(Stat::AD)] = 10.0;
  return Champion::PassiveResult{bonus, true};  // permanent
});
Stats final = c.evaluateChampion();  // fixed-point of all passives
```

**Time-based simulation:**

```cpp
for (double t = 0.0; t < 10.0; t += dt) {
  Stats final = c.applyPassives(base, final, t);  // all passives, remove expired
  // one-shot vanish after 1 step; temp expire when they return alive=false
}
```

**Temp passive** (e.g. a burn lasting 3 seconds):

```cpp
c.passives.push_back(
    [start = 2.0, duration = 3.0](const Stats &, const Stats &, Type time) {
      Stats bonus{};
      bonus[std::to_underlying(Stat::AD)] = 10.0;
      return Champion::PassiveResult{bonus, time - start < duration};  // alive until t=5.0
    });
```

**One-shot passive** (e.g. a burst that fires once):

```cpp
c.passives.push_back([](const Stats &, const Stats &, Type) {
  Stats bonus{};
  bonus[std::to_underlying(Stat::AD)] = 20.0;
  return Champion::PassiveResult{bonus, false};  // alive=false → removed after applying
});
```

To refresh a temp effect, re-insert a new passive with an updated `start`.