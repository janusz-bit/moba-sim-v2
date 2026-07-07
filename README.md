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

## Nix

| Command              | Description                                        |
| -------------------- | -------------------------------------------------- |
| `nix develop`        | Dev shell with toolchain + pre-commit hooks        |
| `nix build`          | Build the `moba_sim` package (headers + static lib) |
| `nix flake check`    | Run pre-commit hooks + build & run tests            |
| `pre-commit run -a`  | Run linters/formatters manually (inside dev shell)  |

The package installs `include/moba_sim.hpp` and `lib/libmoba_sim.a`. Tests are
gated behind the `MOBA_SIM_BUILD_TESTS` CMake option (ON by default, OFF when
built as a package).

## Project layout

```
include/moba_sim.hpp   Public API: Stat, ModType, ModDB, Champion, passives
src/moba_sim.cpp       Implementation
tests/                 Catch2 test suite (one file per component)
nix/default.nix        Flake module: devShell, pre-commit hooks, package, checks
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

All passives live in a single `passives` queue of `PassiveEntry` (id + passive).
Use `Champion::PassiveFactory::make()` to create entries with unique ids, and
`Champion::addPassive()` to insert — inserting an entry whose id already exists
**replaces** the existing passive (refresh), otherwise a new entry is appended.
The framework treats passives uniformly: call, sum the bonus, remove if
`alive=false`. A passive may capture a `start_time` and `duration`, reset
itself, or change its effect — the framework knows nothing about
time/counters/handles; the passive is a black box with access to `time`.

## `addPassive(entry)` — insert or refresh

```cpp
Champion::PassiveFactory factory;
auto e = factory.make(myPassive);
champ.addPassive(e);          // insert (new id)
// later, refresh with a new passive but the same id:
champ.addPassive({e.id, newPassive});  // replace existing
```

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
Champion c{{Stat::HP, 1000}, {Stat::AD, 50}, {Stat::AR, 100}};
Champion::PassiveFactory factory;
c.addPassive(factory.make([](const Stats &base, const Stats &final, const Type &) {
  Stats bonus{};
  bonus[std::to_underlying(Stat::AD)] = 10.0;
  return Champion::PassiveResult{bonus, true};  // permanent
}));
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
c.addPassive(factory.make(
    [start = 2.0, duration = 3.0](const Stats &, const Stats &, const Type &time) {
      Stats bonus{};
      bonus[std::to_underlying(Stat::AD)] = 10.0;
      return Champion::PassiveResult{bonus, time - start < duration};  // alive until t=5.0
    }));
```

**One-shot passive** (e.g. a burst that fires once):

```cpp
c.addPassive(factory.make([](const Stats &, const Stats &, const Type &) {
  Stats bonus{};
  bonus[std::to_underlying(Stat::AD)] = 20.0;
  return Champion::PassiveResult{bonus, false};  // alive=false → removed after applying
}));
```

To refresh a temp effect, call `addPassive` again with the same id but a new
passive (e.g. with an updated `start`):

```cpp
auto burn = factory.make(make_burn(0.0, 3.0));
c.addPassive(burn);                       // insert
c.addPassive({burn.id, make_burn(5.0, 5.0)});  // refresh: same id, new passive
```

## Damage as a passive

Damage fits the passive model without extending the type system.
`PassiveFactory::makeDamage` wraps a passive that produces raw damage (as
negative `bonus[HP]`) and applies `post_mitigation_damage` based on the damage
type and the target's resistances. The inner passive controls the raw amount
and its `alive` flag (so it can scale with `base`/`final`/`time`, be one-shot,
temp, etc.); the wrapper only applies mitigation.

```cpp
Champion::PassiveFactory factory;
Champion::Passive inner = [](const Stats &, const Stats &, Type) {
  Stats bonus{};
  bonus[std::to_underlying(Stat::HP)] = -100.0;
  return Champion::PassiveResult{bonus, false};  // one-shot, 100 raw
};
Stats target_final = target.evaluateChampion();
target.addPassive(factory.makeDamage(inner, TypeDamage::Physical,
                                     10.0, 0.0, target_final));
target.evaluateChampion();  // HP reduced by mitigated damage
```

- **Type damage** (physical/magic/true): choose `AR`/`MR`/none internally.
- **Penetration** (flat + %): reduce effective resistance before mitigation.
- **One-shot**: `alive=false` → removed after applying.
- **Lifesteal/heal**: a passive returning positive `bonus[HP]`.
- **DoT**: a temp passive that accumulates damage in captured state per tick.
- **Armor shred**: a passive returning negative `bonus[AR]` (debuff).

See `tests/test_combat.cpp` for working examples (trades, penetration, DoT,
lifesteal, shred, death).