# moba-sim

A C++23 library for simulating MOBA-style champion stat aggregation, inspired by
[League of Legends](https://wiki.leagueoflegends.com/). Models the
Base/Inc/More modifier pipeline, passive effects, and a signal-based event
system for cross-champion combat. Includes Python bindings via [nanobind](https://github.com/wjakob/nanobind)
and API documentation via [Doxygen](https://doxygen.nl/) + [Sphinx](https://www.sphinx-doc.org/).

## Build

Requires [Nix](https://nixos.org/) with flakes enabled. The development shell
provides the toolchain (clang, CMake, Catch2, nanobind, numpy, pytest, doxygen,
sphinx) and pre-commit hooks.

```sh
nix develop
cmake -B build
cmake --build build
```

### Python bindings

The Python package is built as a Nix derivation and included in the dev shell
via `python3.withPackages`. No `PYTHONPATH` or `pip install` needed.

```sh
nix develop
python3 -c "from moba import Champion, Stat; print('ok')"
python3 -m pytest python/tests/
```

### Documentation

```sh
nix develop
cd docs && doxygen Doxyfile
sphinx-build -b html sphinx _build
# Open docs/_build/index.html in a browser
```

## Test

C++ tests use [Catch2 v3](https://github.com/catchorg/Catch2), wired through CTest.
Python tests use pytest.

```sh
ctest --test-dir build                                    # C++ (218 tests)
python3 -m pytest python/tests/                           # Python (19 tests)
```

## Nix

| Command              | Description                                        |
| -------------------- | -------------------------------------------------- |
| `nix develop`        | Dev shell with toolchain + pre-commit hooks        |
| `nix build`          | Build the `moba_sim` package (headers + static lib) |
| `nix build .#moba-sim-python` | Build the Python wheel package             |
| `nix flake check`    | Run pre-commit hooks + build & run tests            |
| `pre-commit run -a`  | Run linters/formatters manually (inside dev shell)  |

The package installs `include/moba_sim.hpp` (umbrella) and `lib/libmoba_sim.a`.
Tests are gated behind `MOBA_SIM_BUILD_TESTS` (ON by default, OFF when packaged).
Python bindings are gated behind `MOBA_SIM_BUILD_PYTHON` (OFF by default).

## Project layout

```
include/
  moba_sim.hpp              Umbrella header (includes all sub-headers)
  moba/
    types.hpp               Type, Stat, ModType, TypeDamage, ConvergenceError
    source.hpp              Source (provenance chain)
    mod_db.hpp              Modifier, ModDB
    event.hpp               Typed event structs (AttackHit, DamageReceived, ...)
    signal.hpp              Signal<Args...> (typed pub-sub)
    champion.hpp            Champion, Passive, PassiveResult, PassiveFactory
    combat.hpp              mitigated_damage, apply_damage_to_shield, getStat
    simulation.hpp          Simulation (signals + internal handlers)
src/moba_sim.cpp            Implementation
python/
  moba_sim_ext.cpp          nanobind bindings (high-level API)
  moba/__init__.py          Python package
  tests/test_moba.py        pytest suite
tests/                      Catch2 test suite (one file per component)
docs/
  Doxyfile                  Doxygen config (generates XML for Breathe)
  sphinx/                   Sphinx documentation source (RST + conf.py)
nix/default.nix             Flake module: devShell, pre-commit, package, checks
```

## Stat model

A stat's final value combines three modifier types:

- **Base** — additive bonuses (`10 + 20 + 30`)
- **Inc** — multiplicative increases summed from 1.0 (`1.0 + 0.1 + 0.2`)
- **More** — multiplicative multipliers from 1.0 (`1.1 * 1.2 * 1.3`)

`getStat(stat) = sum(stat) * inc(stat) * more(stat)`.

## Champion pipeline

```
mod_db (modifiers)  ->  getBaseStats()  ->  passives  ->  final
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
Use `Champion::PassiveFactory::make()` to create entries with auto-incremented
ids, and `Champion::addPassive()` to insert — inserting an entry whose id
already exists **replaces** the existing passive (refresh), otherwise a new
entry is appended.

## Event system

Events are typed structs (`AttackHit`, `DamageDealt`, `DamageReceived`,
`HealApplied`, `Death`), each carrying event-specific fields. `Simulation`
owns one `Signal<EventType>` per event type. The constructor wires internal
handlers that implement the core rules:

```
AttackHit      -> mitigated_damage -> emit DamageReceived + DamageDealt
DamageDealt    -> lifesteal (physical) + omnivamp (all) -> emit HealApplied
DamageReceived -> HP loss (shield absorbs) -> emit Death if HP <= 0
HealApplied    -> HP gain (cap MaxHP)
```

Lifesteal and omnivamp are **built-in** — no manual signal subscription
needed. LifeSteal heals the attacker for a percentage of physical damage
dealt; Omnivamp heals from all damage types.

User code subscribes to the same signals to react or chain new events:

```cpp
sim.onDamageReceived.subscribe([&](const DamageReceived &ev) {
  // Shield proc, counter-attack, etc.
  sim.champions[ev.target_id].mod_db.add(
      Stat::ShieldHP, ModType::Base, 200.0, Source{"Sterak's Gage"});
});
sim.onAttackHit.emit({0, 1, 100.0, TypeDamage::Physical, Source{"Auto"}, 0.0});
```

## Usage (C++)

**Static champion** (no time):

```cpp
Champion c{{Stat::MaxHP, 1000}, {Stat::AD, 50}, {Stat::AR, 100}};
Champion::PassiveFactory factory;
c.addPassive(factory.make([](const Stats &, const Stats &, const Type &) {
  return Champion::PassiveResult{
      {{Stat::AD, ModType::Base, 10.0, {}}}, true};  // permanent +10 AD
}));
Stats final = c.evaluateChampion();  // fixed-point of all passives
```

**Temp passive** (e.g. a burn lasting 3 seconds):

```cpp
c.addPassive(factory.make(
    [start = 2.0, duration = 3.0](const Stats &, const Stats &, const Type &time) {
      return Champion::PassiveResult{
          {{Stat::AD, ModType::Base, 10.0, {}}}, time - start < duration};
    }));
```

**One-shot passive** (e.g. a burst that fires once):

```cpp
c.addPassive(factory.make(
    [](const Stats &, const Stats &, const Type &) {
      return Champion::PassiveResult{
          {{Stat::AD, ModType::Base, 20.0, {}}}, false};  // alive=false -> removed
    }));
```

**Inc/More passive** (e.g. +20% AD as Inc, *1.1 AD as More):

```cpp
c.addPassive(factory.make(
    [](const Stats &, const Stats &, const Type &) {
      return Champion::PassiveResult{
          {{Stat::AD, ModType::Inc, 0.2, {}},
           {Stat::AD, ModType::More, 1.1, {}}},
          true};
    }));
```

To deal damage, use `mitigated_damage` to compute post-mitigation amount, then
apply it as a negative `Base` mod for `CurrentHP`:

```cpp
Champion target{{Stat::MaxHP, 1000}, {Stat::CurrentHP, 1000}, {Stat::AR, 100}};

Type dealt = moba::mitigated_damage(100.0, TypeDamage::Physical,
                                    target.getBaseStats());
target.addPassive(factory.make(
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
- **One-shot**: `alive=false` -> removed after applying.
- **Lifesteal/heal**: a passive returning a positive `Base` mod for `CurrentHP`.
- **DoT**: a temp passive that accumulates damage in captured state per tick.
- **Armor shred**: a passive returning a negative `Base` mod for `AR` (debuff).

See `tests/test_combat.cpp` for working examples (trades, penetration, DoT,
lifesteal, shred, death).

## Usage (Python)

```python
from moba import Champion, Simulation, Stat, ModType, TypeDamage, Source

# Static champion
c = Champion({Stat.MaxHP: 1000, Stat.AD: 50, Stat.AR: 100})
c.add_passive(lambda base, final, time: [(Stat.AD, ModType.Base, 10.0)])
print(c.evaluate_champion()[Stat.AD])  # 60.0

# One-shot passive (dict result)
c.add_passive(lambda b, f, t: {"mods": [(Stat.AD, ModType.Base, 25.0)], "alive": False})
print(c.evaluate_champion()[Stat.AD])  # 85.0, passive consumed

# Simulation with events (lifesteal is built-in — no manual subscription)
sim = Simulation()
sim.add_champion(Champion({Stat.MaxHP: 1000, Stat.CurrentHP: 800,
                          Stat.AD: 100, Stat.LifeSteal: 0.12}))
sim.add_champion(Champion({Stat.MaxHP: 1000, Stat.CurrentHP: 1000, Stat.AR: 100}))

sim.emit_attack_hit(0, 1, 100.0, TypeDamage.Physical, Source("Basic attack"), 0.0)

print(sim.get_champion(0).get_base_stats()[Stat.CurrentHP])  # 806.0 (lifesteal: +6)
print(sim.get_champion(1).get_base_stats()[Stat.CurrentHP])  # 950.0 (50 mitigated)
```

- **Stats** are returned as `numpy.ndarray` (float64, shape `[25]`), indexable
  by `Stat` enum values.
- **Passives** accept Python callables: `callback(base, final, time) -> list | dict`.
  List = `[(Stat, ModType, value, [Source]), ...]` (alive=True). Dict =
  `{"mods": [...], "alive": bool}`.
- **Signals**: `sim.on_*_subscribe(callback)` to react; `sim.emit_*()` to fire.
- **Lifesteal/Omnivamp**: built into `Simulation` — automatically heals the
  attacker based on `LifeSteal` (physical only) and `Omnivamp` (all types).
- **TypeDamage.True_** is used instead of `True` (Python keyword).

See `python/tests/test_moba.py` for the full test suite.