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

A passive **manages its own lifetime** — it may capture a `start_time` and
`duration`, check `time - start < duration`, reset itself, or change its effect.
The framework knows nothing about time/counters/handles; the passive is a black
box with access to `time`.

## Three passive queues

| Queue              | Behavior              | Removed when                            |
|--------------------|-----------------------|-----------------------------------------|
| `passives`          | permanent, `alive` ignored | never                              |
| `one_shot_passives` | applied once          | after a single `applyPassives` call     |
| `temp_passives`     | self-managed via time | when returning `alive=false`            |

## `applyPassives(base, final, time)` — one simulation step

Applies all three queues in a single step:

1. Sums bonuses from permanent `passives`.
2. Sums bonuses from `one_shot_passives`, then **clears** the queue.
3. For each `temp_passive`: calls it, sums the bonus; if `alive=false`, erases
   it (using the iterator returned by `erase`).

Returns `base + sum(bonuses)`. Mutates `one_shot_passives` and `temp_passives`.

## `evaluateChampion(eps, max_iter)` — fixed-point of permanent passives

Iterates **only** permanent `passives` to resolve fixed-point interactions
(e.g. `bonus = 0.1 * final` → converges to `final = base / 0.9`):

```
final = base
repeat:
  prev = final
  final = base + sum(passive(base, prev, 0.0))
until delta(final, prev) <= eps  or  iter >= max_iter
```

Does **not** touch `one_shot_passives` or `temp_passives` — those are
time-based concepts for simulation and do not participate in the immediate
fixed-point. Throws `ConvergenceError` if not converged within `max_iter`.

## Usage

**Static champion** (no time):

```cpp
Champion c;
c.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
c.passives.push_back([](const Stats &base, const Stats &final, Type) {
  Stats bonus{};
  bonus[std::to_underlying(Stat::AD)] = 10.0;
  return Champion::PassiveResult{bonus, true};
});
Stats final = c.evaluateChampion();  // fixed-point of permanent passives
```

**Time-based simulation:**

```cpp
for (double t = 0.0; t < 10.0; t += dt) {
  Stats final = c.applyPassives(base, final, t);  // perm + one_shot + temp
  // one_shot vanish after 1 step; temp expire when they decide
}
```

**Temp passive** (e.g. a burn lasting 3 seconds):

```cpp
c.temp_passives.push_back(
    [start = 2.0, duration = 3.0](const Stats &, const Stats &, Type time) {
      Stats bonus{};
      bonus[std::to_underlying(Stat::AD)] = 10.0;
      return Champion::PassiveResult{bonus, time - start < duration};  // alive until t=5.0
    });
```

To refresh an effect, re-insert a new passive with an updated `start`.