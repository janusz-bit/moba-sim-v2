# Jak działa moba-sim — kompletny przewodnik

## 1. Statystyki (`Stat`)

Wszystko kręci się wokół 25 statystyk championa (`include/moba/types.hpp:14`):

```
MaxHP, CurrentHP, Mana, CurrentMana, AP, AD, MS, AR, MR, CDR,
ArmorPenFlat, ArmorPenPct, MagicPenFlat, MagicPenPct,
AttackSpeed, CritChance, CritDamage, LifeSteal, Omnivamp,
Tenacity, SlowResist, HealShieldPower, HPRegen, MPRegen, ShieldHP
```

Statystyki żyją w `Stats` — tablicy `std::array<double, 25>` indeksowanej enumem.
Dostęp przez `stats[std::to_underlying(Stat::AD)]` albo helper
`moba::getStat(stats, Stat::AD)`.

---

## 2. Source — łańcuch provenance

`Source` (`include/moba/source.hpp:11`) to struktura z polami:

- `name` — nazwa źródła (np. `"Bloodthirster"`, `"Basic attack"`)
- `description` — opis (np. `"lifesteal proc"`)
- `parent` — `std::shared_ptr<Source>` wskazujący na źródło tego źródła.
  `nullptr` = root source.

Łańcuch można schodzić w dół:

```cpp
auto jinx     = std::make_shared<Source>("Jinx", "champion");
auto attack   = std::make_shared<Source>("Basic attack", "auto hit", jinx);
Source heal_src{"Bloodthirster", "lifesteal proc", attack};

// heal_src.parent->name           → "Basic attack"
// heal_src.parent->parent->name   → "Jinx"
// heal_src.parent->parent->parent → nullptr
```

`Source{"Item", "Bloodthirster", "attacker"}` tworzy `parent = make_shared<Source>("attacker")`
(wygodne przeciążenie — 3. arg jako string tworzy root parent).
Metoda `.origin()` zwraca `parent ? parent->name : ""`.

`operator==` porównuje rekurencyjnie przez łańcuch (deep equality, nie
shared_ptr identity).

---

## 3. Modyfikatory i potok Base/Inc/More (`ModDB`)

`ModDB` (`include/moba/mod_db.hpp:16`) to wektor modyfikatorów. Każdy `Modifier` ma:
`stat`, `type` (Base/Inc/More), `value`, `source`.

Trzy typy modyfikatorów określają jak wartość statystyki jest obliczana:

```
getStat(stat) = sum(stat) * inc(stat) * more(stat)
```

- **Base** — sumowane addytywnie: `10 + 20 + 30 = 60`
- **Inc** — sumowane od 1.0: `1.0 + 0.1 + 0.2 = 1.3`
- **More** — mnożone od 1.0: `1.1 * 1.2 * 1.3 = 1.716`

Np. AD z `Base=80, Inc=0.2, More=1.5`: `80 * 1.2 * 1.5 = 144`.

`ModDB` wspiera filtrowanie po źródle — każdy getter przyjmuje predykat,
np. suma tylko z przedmiotów:

```cpp
db.getSumStat(Stat::AD, [](const auto &m) { return m.source.name == "Item"; });
```

---

## 4. Champion

`Champion` (`include/moba/champion.hpp:18`) to struct z dwoma polami:

- `mod_db` — baza modyfikatorów (statystyki bazowe + z przedmiotów)
- `passives` — kolejka efektów pasywnych

Konstruktor przyjmuje initializer list par `(Stat, wartość)`:

```cpp
Champion c{{Stat::MaxHP, 1000}, {Stat::AD, 50}, {Stat::AR, 100}};
```

To dodaje modyfikatory `Base` do `mod_db` ze źródłem `"Base"`.

`getBaseStats()` ewaluuje pełny potok dla wszystkich statystyk — zwraca `Stats`
bez passives.

---

## 5. Passives — serce systemu

Passive to `std::function` z sygnaturą (`include/moba/champion.hpp:46`):

```cpp
PassiveResult(const Stats &base, const Stats &final, const Type &time,
              const PassiveEvent &event)
```

Otrzymuje:

- `base` — statystyki z `mod_db` (bez passives)
- `final` — aktualny wynik (z poprzedniej iteracji)
- `time` — czas symulacji (absolutny, rosnący)
- `event` — event being dispatched, lub `std::monostate` dla normalnej ewaluacji

Zwraca `PassiveResult` (`include/moba/champion.hpp:40`):

- `mods` — wektor `Modifier` (Base/Inc/More — wpinają się w potok!)
- `emitted_events` — wektor `PassiveEvent` do dispatchu po aktualnym kroku
  (umożliwia chaining: pasywa reaguje na DamageReceived → emituje HealApplied)
- `alive` — czy passive zostaje (`true`) czy jest usuwane (`false`)

### 5.1. `PassiveEvent` — variant

`PassiveEvent` (`include/moba/event.hpp:78`) to `std::variant`:

```cpp
using PassiveEvent = std::variant<
    std::monostate,       // brak eventa — normalna ewaluacja (evaluateChampion)
    AttackHit,
    DamageDealt,
    DamageReceived,
    HealApplied,
    Death
>;
```

Gdy passive jest wołana z `monostate` — to normalna ewaluacja statystyk
(jak w `evaluateChampion`). Gdy jest wołana z konkretnym eventem — może
zareagować (np. dać shield na `DamageReceived`).

### 5.2. Typy passives — passive sama decyduje o swoim lifetime

**Permanent** — zawsze `alive=true`:

```cpp
factory.make([](const Stats &, const Stats &, const Type &, const auto &) {
  return Champion::PassiveResult{
      {{Stat::AD, ModType::Inc, 0.2, {}}}, true};  // +20% AD na zawsze
});
```

**One-shot** — `alive=false` po jednym zastosowaniu:

```cpp
factory.make([](const Stats &, const Stats &, const Type &, const auto &) {
  return Champion::PassiveResult{
      {{Stat::AD, ModType::Base, 20.0, {}}}, false};  // +20 AD raz
});
```

**Temp** — wygasa na podstawie `time`:

```cpp
factory.make(
    [start = 2.0, duration = 3.0](const Stats &, const Stats &, const Type &time,
                                  const auto &) {
      return Champion::PassiveResult{
          {{Stat::AD, ModType::Base, 10.0, {}}}, time - start < duration};
    });
```

**Reaktywna** — reaguje na event (np. shield na DamageReceived):

```cpp
factory.make(
    [](const Stats &, const Stats &, Type, const PassiveEvent &ev)
        -> Champion::PassiveResult {
      if (!std::holds_alternative<DamageReceived>(ev)) return {};
      return {{{Stat::ShieldHP, ModType::Base, 200.0, {}}}, false};
    });
```

**DoT z akumulacją** — mutable state:

```cpp
[per_tick = 40.0, accumulated = 0.0, next_tick = 0.0](
    const Stats &, const Stats &, const Type &time, const auto &) mutable {
  if (time >= next_tick) { accumulated += per_tick; next_tick = time + 1.0; }
  return Champion::PassiveResult{
      {{Stat::CurrentHP, ModType::Base, -accumulated, {}}}, time < 3.0};
}
```

### 5.3. ID passives — auto-increment

`PassiveFactory::make()` tworzy `PassiveEntry` z auto-incrementowanym ID.
`addPassive` deduplikuje po ID — **ten sam ID = refresh** (zastąp passive),
**nowy ID = dodaj**:

```cpp
Champion::PassiveFactory factory;
champ.addPassive(factory.make(make_burn(0.0, 3.0)));  // insert (id=0)
champ.addPassive(Champion::PassiveEntry{0, make_burn(5.0, 5.0)});  // refresh id=0
```

---

## 6. Jak `applyPassives` oblicza wynik

`applyPassives(base, final, time)` — jeden krok symulacji:

```
1. Skopiuj mod_db → working
2. Dla każdej passive:
   a. Wywołaj: passive(base, final, time, monostate) → result{mods, events, alive}
   b. Dodaj mods do working
   c. Jeśli alive=false → usuń passive z kolejki
3. Zwróć statsFromModDB(working) — pełny potok Base/Inc/More
```

Kluczowe: modyfikatory passives **wpinają się w potok** — passive `+10 AD`
przy `Inc=0.2, More=1.5` daje `10 * 1.2 * 1.5 = 18`, nie płaskie `10`.

---

## 7. Jak `evaluateChampion` znajduje fixed-point

`evaluateChampion(eps, max_iter, time)` — rozwiązuje interakcje:

```
final = getBaseStats()
repeat:
  prev = final
  final = pipeline(mod_db + Σ passive(base, prev, time, monostate))   // bez usuwania
until delta(final, prev) ≤ eps  or  iter ≥ max_iter
then: usuń passives gdzie alive=false (z ostatniej iteracji)
```

### Dlaczego pętla?

Bo passive B może zależeć od `final`, który zawiera efekt passive A. Np.:

- Passive 1: `AD += 10% * final[HP]`
- Passive 2: `HP += 0.5 * final[AD]`
- AD zależy od HP, HP od AD — potrzebna iteracja do punktu stałego.

### ConvergenceError

Gdy passive ma wagę ≥1 (rozbiega się, np. `bonus = 1.5 * final`).

### Usuwanie po konwergencji

Passives z `alive=false` są usuwane **dopiero po zbieżności**, nie w trakcie
pętli — bo w trakcie pętli passives muszą być aplikowane wielokrotnie.

---

## 8. Helpery dla obrażeń

### `mitigated_damage(raw, type, target, flat_pen, pct_pen)`

```
effective_resistance = (resistance - flat_pen) * (1 - pct_pen)

dla dodatniego AR:
  post_mitigation = raw * 100 / (100 + effective_resistance)

dla ujemnego AR (amplifikacja, max 200%):
  post_mitigation = raw * (2 - 100/(100 - effective_resistance))
```

True damage omija mitigację całkowicie.

### `apply_damage_to_shield(shield, hp, mitigated)`

Shield absorbuje obrażenia przed HP. Zwraca
`{shield_remaining, hp_remaining}`.

- Jeśli `shield ≥ damage`: shield absorbuje wszystko, HP nietknięte
- Jeśli `shield < damage`: shield = 0, HP traci resztę
- Jeśli `damage ≤ 0`: nic się nie dzieje

---

## 9. Event system — `PassiveEvent` + `dispatchEvent` + observer signals

### 9.1. `PassiveEvent` — typowany variant

Zamiast jednego `GameEvent` z enumem, każdy event to osobny struct
(`include/moba/event.hpp:19`), unified via `PassiveEvent` variant:

```cpp
struct AttackHit      { actor_id, target_id, amount, damage_type, source, time };
struct DamageDealt    { actor_id, target_id, amount, damage_type, source, time };
struct DamageReceived { actor_id, target_id, amount, damage_type, source, time };
struct HealApplied    { target_id, amount, source, time };
struct Death          { actor_id, target_id, source, time };

using PassiveEvent = std::variant<std::monostate, AttackHit, DamageDealt,
                                  DamageReceived, HealApplied, Death>;
```

`amount` w `DamageDealt`/`DamageReceived` to wartość **post-mitigation**.
`std::monostate` = brak eventa (normalna ewaluacja statów).

### 9.2. `Simulation::dispatchEvent` — główny entry point

`Simulation` (`include/moba/simulation.hpp:45`) to struct z:

- `champions` — `std::vector<Champion>` (wszyscy uczestnicy walki)
- `onAttackHit`, `onDamageDealt`, `onDamageReceived`, `onHealApplied`, `onDeath`
  — observer `Signal`s (side-effect-free reakcje: logging, UI, meta)
- `dispatchEvent(event, eps, max_iter)` — przetwarza event przez pełny pipeline

`dispatchEvent` działa w 5 krokach:

```
1. Internal game rules (std::visit na PassiveEvent):
   AttackHit      → oblicz mitigated_damage → enqueue DamageReceived + DamageDealt
   DamageDealt    → lifesteal (physical) + omnivamp (all) → enqueue HealApplied
   DamageReceived → aplikuj HP loss (shield absorbuje) → jeśli HP ≤ 0: enqueue Death
   HealApplied    → aplikuj HP gain (cap MaxHP)

2. Observer signals (synchroniczne, side-effect-free):
   onAttackHit.emit(ev), onDamageDealt.emit(ev), ...

3. Broadcast do wszystkich pasyw wszystkich championów:
   passive(base, final, time, event) → mods + emitted_events + alive
   mods → mod_db, emitted_events → kolejka, alive=false → usuń

4. Re-ewaluacja championów (fixed-point)

5. Flush kolejki (chained events, aż do max_iter lub empty)
```

### 9.3. Efekty zwrotne (feedback) przez `emitted_events`

Passive może zwrócić `emitted_events` w `PassiveResult`. Te eventy są
kolejkowane i dispatchowane w kolejnych iteracjach. Przykład łańcucha:

```
AttackHit → (wewnętrzne reguły) → DamageReceived → (broadcast do pasyw)
  → passive "Counter heal" emituje HealApplied → (kolejka)
  → HealApplied → (wewnętrzne reguły) → HP gain
```

To pozwala budować reakcje: atak → damage → counter-heal → ...

### 9.4. Observer signals vs passive dispatch

Dwa rozłączne mechanizmy:

- **Pasywa** (`passive(base, final, time, event)`) — modyfikują statystyki,
  uczestniczą w fixed-point, mogą emitować eventy. Jednolity system.
- **Observer signals** (`sim.onDeath.subscribe(...)`) — side-effect-free,
  synchroniczne, nie modyfikują statów. Dla logging, UI, meta-systems.

### 9.5. Source chain w eventach

Wewnętrzna reguła `AttackHit → DamageReceived` tworzy łańcuch provenance:
source eventa damage ma `parent` wskazujący na source eventa attack.

```cpp
// ev.source.name           → "Damage"
// ev.source.parent->name   → "Basic attack"
// ev.source.parent->parent → source oryginalnego AttackHit
```

---

## 10. Pełny potok — od przedmiotów do obrażeń

```
Simulation
├── champions[]              ← lista championów
├── onAttackHit..onDeath      ← observer Signals (side-effect-free)
├── event_queue_              ← kolejka PassiveEvent (chaining)
│
├── dispatchEvent(AttackHit{0, 1, 100.0, Physical, src, 0.0})
│
└── pipeline per event (loop):
    │  1. Internal rules (std::visit):
    │     AttackHit → mitigated_damage → DamageReceived + DamageDealt (enqueue)
    │     DamageDealt → lifesteal + omnivamp → HealApplied (enqueue)
    │     DamageReceived → HP loss (shield) → Death if HP ≤ 0 (enqueue)
    │     HealApplied → HP gain (cap MaxHP)
    │  2. Observer signals (synchroniczne)
    │  3. Broadcast do pasyw: passive(base, final, time, event)
    │     → mods → mod_db, emitted_events → kolejka, alive=false → usuń
    │  4. Re-ewaluacja (fixed-point)
    └── 5. Flush kolejki (następne eventy, aż do max_iter)

Champion
├── mod_db          ← przedmioty, baza, runy (Base/Inc/More modyfikatory)
├── passives        ← pasywne efekty (zwracają typed Modifier[] + emitted_events)
│
├── getBaseStats()  → Stats    [potok Base/Inc/More na mod_db]
│
├── applyPassives(base, final, time)
│   │  1. Skopiuj mod_db
│   │  2. Dodaj mods od każdej passive (z event=monostate)
│   │  3. Usuń alive=false
│   │  4. Zwróć pipeline(working_mod_db)
│   └── → Stats
│
├── evaluateChampion(eps, max_iter, time)
│   │  Iteruje applyPassives do fixed-point (z event=monostate)
│   │  Rzuca ConvergenceError jeśli nie zbieżne
│   └── → Stats
│
└── Helpery zewnętrzne:
    ├── mitigated_damage(raw, type, target, flat_pen, pct_pen)
    └── apply_damage_to_shield(shield, hp, damage)
```

---

## 11. Typowy przepływ walki

### Styl statyczny (bez eventów)

```cpp
Champion attacker{{Stat::MaxHP, 1800}, {Stat::AD, 320}, {Stat::AR, 80}};
Champion target{{Stat::MaxHP, 2800}, {Stat::AD, 180}, {Stat::AR, 120}};

Champion::PassiveFactory factory;
attacker.addPassive(factory.make(
    [](const Stats &, const Stats &final, const Type &, const auto &) {
      return Champion::PassiveResult{
          {{Stat::AD, ModType::Base, 100.0, {}}}, true};
    }));

Stats atk_stats = attacker.evaluateChampion();
Stats tgt_stats = target.evaluateChampion();
Type dealt = mitigated_damage(getStat(atk_stats, Stat::AD),
                              TypeDamage::Physical, tgt_stats);
target.mod_db.replace(Stat::CurrentHP, ModType::Base,
                      getStat(tgt_stats, Stat::CurrentHP) - dealt,
                      Source{"Base", ""});
```

### Styl z eventami (dispatchEvent)

```cpp
Simulation sim;
sim.champions.push_back(Champion{{Stat::MaxHP, 1000}, {Stat::CurrentHP, 800},
                                  {Stat::AD, 100}, {Stat::LifeSteal, 0.12}});
sim.champions.push_back(Champion{{Stat::MaxHP, 1000}, {Stat::CurrentHP, 1000},
                                  {Stat::AR, 100}});

// Shield proc jako pasywa (nie signal subscription)
sim.champions[1].addPassive(factory.make(
    [](const Stats &, const Stats &, Type, const PassiveEvent &ev)
        -> Champion::PassiveResult {
      if (!std::holds_alternative<DamageReceived>(ev)) return {};
      return {{{Stat::ShieldHP, ModType::Base, 200.0, {}}}, false};
    }));

// Observer: log deaths
sim.onDeath.subscribe([](const Death &ev) { /* ... */ });

// Dispatch — lifesteal i omnivamp są wbudowane
sim.dispatchEvent(AttackHit{0, 1, 100.0, TypeDamage::Physical,
                            Source{"Basic attack"}, 0.0});

Stats t = sim.champions[1].getBaseStats();
// t[CurrentHP] = 950 (50 damage mitigated)
// t[ShieldHP] = 200 (shield proc via passive)
```

---

## 12. Struktura plików

```
include/
  moba_sim.hpp              Umbrella header
  moba/
    types.hpp               Type, Stat, ModType, TypeDamage, ConvergenceError
    source.hpp              Source, SourcePtr
    mod_db.hpp              Modifier, ModDB
    signal.hpp              Signal<Args...> (observer signals)
    event.hpp               AttackHit, DamageDealt, ..., PassiveEvent variant
    champion.hpp            Champion, Passive, PassiveResult, PassiveFactory
    combat.hpp              mitigated_damage, apply_damage_to_shield, getStat
    simulation.hpp          Simulation (dispatchEvent + internal rules)
src/moba_sim.cpp            Implementacja
tests/
  test_champion.cpp          Champion, passives, evaluateChampion
  test_combat.cpp            Walka, obrażenia, penetracja, DoT, shield
  test_scenarios.cpp         Scenariusze end-to-end
  test_mod_db.cpp            ModDB
  test_post_mitigation_damage.cpp  Formuła pancerza
  test_champion_stats.cpp    Statystyki + edge cases + Source
  test_events.cpp            PassiveEvent, dispatchEvent, Simulation
nix/default.nix              Flake: devShell, pre-commit, paczka, checks
```

## 13. Budowanie i testowanie

```sh
nix develop          # dev shell z toolchain + hooks
cmake -B build
cmake --build build
ctest --test-dir build           # 218 testów
python3 -m pytest python/tests/  # 19 testów
nix flake check                  # pre-commit + build + tests
```