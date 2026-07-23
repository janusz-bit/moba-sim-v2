# moba-sim

A C++23 library for simulating MOBA-style champion stat aggregation, inspired by
[League of Legends](https://wiki.leagueoflegends.com/). Models the
Base/Inc/More modifier pipeline, passive effects, and a unified event-aware
passive system for cross-champion combat. Includes Python bindings via [nanobind](https://github.com/wjakob/nanobind)
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
    signal.hpp              Signal<Args...> (observer signals)
    event.hpp               Typed event structs + PassiveEvent variant
    champion.hpp            Champion, Passive, PassiveResult, PassiveFactory
    combat.hpp              mitigated_damage, apply_damage_to_shield, getStat
    simulation.hpp          Simulation (dispatchEvent + internal rules)
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
PassiveResult(const Stats &base, const Stats &final, const Type &time,
              const PassiveEvent &event)
  -> { std::vector<Modifier> mods; std::vector<PassiveEvent> emitted_events; bool alive; }
```

- Receives `base` (stats from `mod_db`), `final` (current result), `time`
  (absolute simulation time), and `event` (the event being dispatched, or
  `std::monostate` for normal stat evaluation).
- Returns a list of typed `Modifier`s, an optional list of `PassiveEvent`s to
  chain, and an `alive` flag.
- Passive mods are folded into a copy of `mod_db` and run through the full
  Base/Inc/More pipeline.
- When `event` is a concrete event (e.g. `DamageReceived`), the passive can
  react — e.g. grant shield, counter-heal, emit new events.

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
`HealApplied`, `Death`), unified via `PassiveEvent` — a `std::variant` of
`std::monostate` (no event, normal evaluation) and all event structs.

`Simulation::dispatchEvent(event)` is the main entry point:

1. **Internal game rules** (via `std::visit`):
   - `AttackHit` -> mitigated damage -> enqueue `DamageReceived` + `DamageDealt`
   - `DamageDealt` -> lifesteal (physical) + omnivamp (all) -> enqueue `HealApplied`
   - `DamageReceived` -> HP loss (shield absorbs) -> enqueue `Death` if HP <= 0
   - `HealApplied` -> HP gain (cap MaxHP)
2. **Observer signals** — `Signal<EventType>` fires synchronously for
   side-effect-free reactions (logging, UI, meta)
3. **Broadcast to passives** — all passives of all champions receive the event;
   their mods are folded into the pipeline, their `emitted_events` are enqueued
4. **Re-evaluate** affected champions (fixed-point)
5. **Flush queue** — chained events are processed iteratively (up to max_iter)

Lifesteal and omnivamp are **built-in** — no manual subscription needed.

## Usage (C++)

**Static champion** (no events):

```cpp
Champion c{{Stat::MaxHP, 1000}, {Stat::AD, 50}, {Stat::AR, 100}};
Champion::PassiveFactory factory;
c.addPassive(factory.make([](const Stats &, const Stats &, const Type &,
                             const auto &) {
  return Champion::PassiveResult{
      {{Stat::AD, ModType::Base, 10.0, {}}}, true};  // permanent +10 AD
}));
Stats final = c.evaluateChampion();  // fixed-point of all passives
```

**Reactive passive** (shield on DamageReceived):

```cpp
c.addPassive(factory.make(
    [](const Stats &, const Stats &, Type, const PassiveEvent &ev)
        -> Champion::PassiveResult {
      if (!std::holds_alternative<DamageReceived>(ev)) return {};
      return {{{Stat::ShieldHP, ModType::Base, 200.0, {}}}, false};
    }));
```

**Temp passive** (burn lasting 3 seconds):

```cpp
c.addPassive(factory.make(
    [start = 2.0, duration = 3.0](const Stats &, const Stats &, const Type &time,
                                  const auto &) {
      return Champion::PassiveResult{
          {{Stat::AD, ModType::Base, 10.0, {}}}, time - start < duration};
    }));
```

**Simulation with events** (lifesteal is built-in):

```cpp
Simulation sim;
sim.champions.push_back(Champion{{Stat::MaxHP, 1000}, {Stat::CurrentHP, 800},
                                  {Stat::AD, 100}, {Stat::LifeSteal, 0.12}});
sim.champions.push_back(Champion{{Stat::MaxHP, 1000}, {Stat::CurrentHP, 1000},
                                  {Stat::AR, 100}});

// Shield proc as a passive (not a signal subscription)
sim.champions[1].addPassive(factory.make(
    [](const Stats &, const Stats &, Type, const PassiveEvent &ev)
        -> Champion::PassiveResult {
      if (!std::holds_alternative<DamageReceived>(ev)) return {};
      return {{{Stat::ShieldHP, ModType::Base, 200.0, {}}}, false};
    }));

// Observer: log deaths (side-effect-free)
sim.onDeath.subscribe([](const Death &ev) { log(ev); });

sim.dispatchEvent(AttackHit{0, 1, 100.0, TypeDamage::Physical,
                            Source{"Basic attack"}, 0.0});
```

- **Type damage** (physical/magic/true): `mitigated_damage` selects `AR`/`MR`
  internally; true damage bypasses mitigation.
- **Penetration** (flat + %): `mitigated_damage(raw, type, target, flat, pct)`
  reduces effective resistance before mitigation.
- **One-shot**: `alive=false` -> removed after applying.
- **Lifesteal/heal**: built into `Simulation`, heals the attacker automatically.
- **DoT**: a temp passive that accumulates damage in captured state per tick.
- **Armor shred**: a passive returning a negative `Base` mod for `AR` (debuff).

See `tests/test_combat.cpp` and `tests/test_events.cpp` for working examples.

## Usage (Python)

```python
from moba import Champion, Simulation, Stat, ModType, TypeDamage, Source

# Static champion
c = Champion({Stat.MaxHP: 1000, Stat.AD: 50, Stat.AR: 100})
c.add_passive(lambda base, final, time, event: [(Stat.AD, ModType.Base, 10.0)])
print(c.evaluate_champion()[Stat.AD])  # 60.0

# One-shot passive (dict result)
c.add_passive(lambda b, f, t, ev: {"mods": [(Stat.AD, ModType.Base, 25.0)],
                                    "alive": False})
print(c.evaluate_champion()[Stat.AD])  # 85.0, passive consumed

# Simulation with events (lifesteal is built-in)
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
- **Passives** accept Python callables: `callback(base, final, time, event) -> list | dict`.
  `event` is `None` for normal evaluation, or an event object for reactive
  dispatch. List = `[(Stat, ModType, value, [Source]), ...]` (alive=True). Dict =
  `{"mods": [...], "alive": bool}`.
- **Signals**: `sim.on_*_subscribe(callback)` for observers (logging, UI).
  `sim.emit_attack_hit(...)` dispatches the full event pipeline.
- **Lifesteal/Omnivamp**: built into `Simulation` — automatically heals the
  attacker based on `LifeSteal` (physical only) and `Omnivamp` (all types).
- **TypeDamage.True_** is used instead of `True` (Python keyword).

See `python/tests/test_moba.py` for the full test suite.