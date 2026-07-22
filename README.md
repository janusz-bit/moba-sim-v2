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
PassiveResult(const Stats &base, const Stats &final, const Type &time)
  -> { std::vector<Modifier> mods; bool alive; }
```

- Receives `base` (stats from `mod_db`), `final` (current result), and `time`
  (absolute simulation time, starts at 0 and only increases).
- Returns a list of typed `Modifier`s (each with `Stat`, `ModType`
  Base/Inc/More, `value`, `source`) and an `alive` flag.
- Passive mods are folded into a copy of `mod_db` and run through the full
  Base/Inc/More pipeline, so a passive can express additive bonuses (Base),
  percent increases (Inc), or multiplicative multipliers (More).

A passive **is the sole authority on its lifetime** via the `alive` flag:

- **permanent**: always returns `alive=true` (never removed)
- **one-shot**: returns `alive=false` after its single application
- **temp**: returns `alive=false` once it decides to expire (e.g. by capturing
  a start time and checking `time - start < duration`)

All passives live in a single `passives` queue of `PassiveEntry` (id + passive).
Use `Champion::PassiveFactory::make()` to create entries with unique ids, and
`Champion::addPassive()` to insert — inserting an entry whose id already exists
**replaces** the existing passive (refresh), otherwise a new entry is appended.
The framework treats passives uniformly: call, fold mods into `mod_db`, remove
if `alive=false`. A passive may capture a `start_time` and `duration`, reset
itself, or change its effect — the framework knows nothing about
time/counters/handles; the passive is a black box with access to `time`.

## `addPassive(entry)` — insert or refresh

Passives are identified by a **type-safe enum id**. Define your own `PassiveId`
enum with one value per named passive slot. `addPassive` deduplicates by id:
same id = refresh (replace), new id = append.

```cpp
enum class PassiveId : std::size_t { Burn, Shield, Shred };

Champion::PassiveFactory factory;
champ.addPassive(factory.make(PassiveId::Burn, make_burn(0.0, 3.0)));
// later, refresh burn with a new start time (same id → replaces):
champ.addPassive(factory.make(PassiveId::Burn, make_burn(5.0, 5.0)));
```

## `applyPassives(base, final, time)` — one simulation step

Applies all passives in a single step:

1. For each passive: call it, fold its mods into a copy of `mod_db`.
2. If `alive=false`, erase it; the mods are still applied for this step.
3. Run the full Base/Inc/More pipeline on the resulting `mod_db`.

Returns the stats after the pipeline. Mutates `passives` (removes
expired/one-shot). The `base` parameter is passed to passives as informational
input only — the result is computed from `mod_db + passive mods`.

## `evaluateChampion(eps, max_iter, time)` — fixed-point of all passives

Iterates **all passives** to resolve fixed-point interactions, but **does not
remove** any during iteration — passives are applied repeatedly with `time`
(the fixed-point is immediate, not a simulation step). After convergence,
passives that returned `alive=false` on the final iteration are removed; those
with `alive=true` stay.

```
final = getBaseStats()
repeat:
  prev = final
  final = pipeline(mod_db + sum(passive(base, prev, time)))   // no removal
until delta(final, prev) <= eps  or  iter >= max_iter
then: remove passives where alive=false (from the final iteration)
```

Throws `ConvergenceError` if not converged within `max_iter`.

## Usage

**Static champion** (no time):

```cpp
enum class PassiveId : std::size_t { BonusAD };

Champion c{{Stat::MaxHP, 1000}, {Stat::AD, 50}, {Stat::AR, 100}};
Champion::PassiveFactory factory;
c.addPassive(factory.make(PassiveId::BonusAD, [](const Stats &, const Stats &, const Type &) {
  return Champion::PassiveResult{
      {{Stat::AD, ModType::Base, 10.0, {}}}, true};  // permanent +10 AD
}));
Stats final = c.evaluateChampion();  // fixed-point of all passives
```

**Time-based simulation:**

```cpp
Stats base = c.getBaseStats();
Stats final = base;
for (double t = 0.0; t < 10.0; t += dt) {
  final = c.applyPassives(base, final, t);  // all passives, remove expired
  // one-shot vanish after 1 step; temp expire when they return alive=false
}
```

**Temp passive** (e.g. a burn lasting 3 seconds):

```cpp
c.addPassive(factory.make(PassiveId::Burn,
    [start = 2.0, duration = 3.0](const Stats &, const Stats &, const Type &time) {
      return Champion::PassiveResult{
          {{Stat::AD, ModType::Base, 10.0, {}}}, time - start < duration};
    }));
```

**One-shot passive** (e.g. a burst that fires once):

```cpp
c.addPassive(factory.make(PassiveId::Burst,
    [](const Stats &, const Stats &, const Type &) {
      return Champion::PassiveResult{
          {{Stat::AD, ModType::Base, 20.0, {}}}, false};  // alive=false → removed
    }));
```

**Inc/More passive** (e.g. +20% AD as Inc, *1.1 AD as More):

```cpp
c.addPassive(factory.make(PassiveId::Rage,
    [](const Stats &, const Stats &, const Type &) {
      return Champion::PassiveResult{
          {{Stat::AD, ModType::Inc, 0.2, {}},
           {Stat::AD, ModType::More, 1.1, {}}},
          true};
    }));
```

To refresh a temp effect (e.g. extend burn duration), call `addPassive` again
with the same enum id but a new passive:

```cpp
c.addPassive(factory.make(PassiveId::Burn, make_burn(0.0, 3.0)));   // insert
c.addPassive(factory.make(PassiveId::Burn, make_burn(5.0, 5.0))); // refresh
```

To deal damage, use `mitigated_damage` to compute post-mitigation amount, then
apply it as a negative `Base` mod for `CurrentHP`:

```cpp
Champion target{{Stat::MaxHP, 1000}, {Stat::CurrentHP, 1000}, {Stat::AR, 100}};
Champion::PassiveFactory factory;

// 100 physical damage, 0 penetration → 50 post-mitigation
Type dealt = moba::mitigated_damage(100.0, TypeDamage::Physical,
                                    target.getBaseStats());
target.addPassive(factory.make(PassiveId::Hit,
    [dealt](const Stats &, const Stats &, const Type &) {
      return Champion::PassiveResult{
          {{Stat::CurrentHP, ModType::Base, -dealt, {}}}, false};  // one-shot
    }));
target.evaluateChampion();  // CurrentHP reduced by mitigated damage
```

- **Type damage** (physical/magic/true): `mitigated_damage` selects `AR`/`MR`
  internally; true damage bypasses mitigation.
- **Penetration** (flat + %): `mitigated_damage(raw, type, target, flat, pct)`
  reduces effective resistance before mitigation.
- **One-shot**: `alive=false` → removed after applying.
- **Lifesteal/heal**: a passive returning a positive `Base` mod for `CurrentHP`.
- **DoT**: a temp passive that accumulates damage in captured state per tick.
- **Armor shred**: a passive returning a negative `Base` mod for `AR` (debuff).

See `tests/test_combat.cpp` for working examples (trades, penetration, DoT,
lifesteal, shred, death).