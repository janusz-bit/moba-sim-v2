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

`getBaseStats()` (`src/moba_sim.cpp:136`) ewaluuje pełny potok dla wszystkich
statystyk — zwraca `Stats` bez passives.

---

## 5. Passives — serce systemu

Passive to `std::function` z sygnaturą (`include/moba/champion.hpp:46`):

```cpp
PassiveResult(const Stats &base, const Stats &final, const Type &time)
```

Otrzymuje:

- `base` — statystyki z `mod_db` (bez passives)
- `final` — aktualny wynik (z poprzedniej iteracji)
- `time` — czas symulacji (absolutny, rosnący)

Zwraca `PassiveResult` (`include/moba/champion.hpp:40`):

- `mods` — wektor `Modifier` (Base/Inc/More — wpinają się w potok!)
- `alive` — czy passive zostaje (`true`) czy jest usuwane (`false`)

### 5.1. Typy passives — passive sama decyduje o swoim lifetime

**Permanent** — zawsze `alive=true`:

```cpp
factory.make([](const Stats &, const Stats &, const Type &) {
  return Champion::PassiveResult{
      {{Stat::AD, ModType::Inc, 0.2, {}}}, true};  // +20% AD na zawsze
});
```

**One-shot** — `alive=false` po jednym zastosowaniu:

```cpp
factory.make([](const Stats &, const Stats &, const Type &) {
  return Champion::PassiveResult{
      {{Stat::AD, ModType::Base, 20.0, {}}}, false};  // +20 AD raz
});
```

**Temp** — wygasa na podstawie `time`:

```cpp
factory.make(
    [start = 2.0, duration = 3.0](const Stats &, const Stats &, const Type &time) {
      return Champion::PassiveResult{
          {{Stat::AD, ModType::Base, 10.0, {}}}, time - start < duration};
    });
```

**DoT z akumulacją** — mutable state:

```cpp
[per_tick = 40.0, accumulated = 0.0, next_tick = 0.0](
    const Stats &, const Stats &, const Type &time) mutable {
  if (time >= next_tick) { accumulated += per_tick; next_tick = time + 1.0; }
  return Champion::PassiveResult{
      {{Stat::CurrentHP, ModType::Base, -accumulated, {}}}, time < 3.0};
}
```

### 5.2. ID passives — auto-increment

`PassiveFactory::make()` (`include/moba/champion.hpp:75`) tworzy `PassiveEntry`
z auto-incrementowanym ID. `addPassive` deduplikuje po ID — **ten sam ID =
refresh** (zastąp passive), **nowy ID = dodaj**:

```cpp
Champion::PassiveFactory factory;
champ.addPassive(factory.make(make_burn(0.0, 3.0)));  // insert (id=0)
champ.addPassive(factory.make(make_burn(5.0, 5.0)));  // refresh? nie — nowy ID!
```

Aby odświeżyć ten sam slot, trzeba użyć tego samego ID:

```cpp
champ.addPassive(Champion::PassiveEntry{0, make_burn(5.0, 5.0)});  // refresh id=0
```

---

## 6. Jak `applyPassives` oblicza wynik

`applyPassives(base, final, time)` (`src/moba_sim.cpp:154`) — jeden krok
symulacji:

```
1. Skopiuj mod_db → working
2. Dla każdej passive:
   a. Wywołaj: passive(base, final, time) → result{mods, alive}
   b. Dodaj mods do working
   c. Jeśli alive=false → usuń passive z kolejki
3. Zwróć statsFromModDB(working) — pełny potok Base/Inc/More
```

Kluczowe: modyfikatory passives **wpinają się w potok** — passive `+10 AD`
przy `Inc=0.2, More=1.5` daje `10 * 1.2 * 1.5 = 18`, nie płaskie `10`.

Parametr `base` jest przekazywany do passives jako informacja — wynik jest
obliczany z `mod_db + passive mods` przez potok, nie z `base + flat bonus`.

---

## 7. Jak `evaluateChampion` znajduje fixed-point

`evaluateChampion(eps, max_iter, time)` (`src/moba_sim.cpp:180`) — rozwiązuje
interakcje:

```
final = getBaseStats()
repeat:
  prev = final
  final = pipeline(mod_db + Σ passive(base, prev, time))   // bez usuwania
until delta(final, prev) ≤ eps  or  iter ≥ max_iter
then: usuń passives gdzie alive=false (z ostatniej iteracji)
```

### Dlaczego pętla?

Bo passive B może zależeć od `final`, który zawiera efekt passive A. Np.:

- Passive 1: `AD += 10% * final[HP]`
- Passive 2: `HP += 0.5 * final[AD]`
- AD zależy od HP, HP od AD — potrzebna iteracja do punktu stałego.

### Przekazywanie czasu

`time` jest przekazywany do passives — temp passive widzą czas symulacji:

```cpp
champ.evaluateChampion(0.001, 1000, 5.0);  // rozwiąż fixed-point przy t=5
```

### ConvergenceError

Gdy passive ma wagę ≥1 (rozbiega się, np. `bonus = 1.5 * final`).

### Usuwanie po konwergencji

Passives z `alive=false` są usuwane **dopiero po zbieżności**, nie w trakcie
pętli — bo w trakcie pętli passives muszą być aplikowane wielokrotnie.
Bonus z wygasającego passive jest still aplicowany w ostatniej iteracji,
ale passive jest usuwana dla przyszłych wywołań.

---

## 8. Helpery dla obrażeń

### `mitigated_damage(raw, type, target, flat_pen, pct_pen)`
(`src/moba_sim.cpp:214`)

```
effective_resistance = (resistance - flat_pen) * (1 - pct_pen)

dla dodatniego AR:
  post_mitigation = raw * 100 / (100 + effective_resistance)

dla ujemnego AR (amplifikacja, max 200%):
  post_mitigation = raw * (2 - 100/(100 - effective_resistance))
```

True damage omija mitigację całkowicie.

### `apply_damage_to_shield(shield, hp, mitigated)`
(`src/moba_sim.cpp:239`)

Shield absorbuje obrażenia przed HP. Zwraca
`{shield_remaining, hp_remaining}`.

- Jeśli `shield ≥ damage`: shield absorbuje wszystko, HP nietknięte
- Jeśli `shield < damage`: shield = 0, HP traci resztę
- Jeśli `damage ≤ 0`: nic się nie dzieje (negatywny damage nie leczy)

---

## 9. Event system — `Signal` + typowane eventy

### 9.1. `Signal<Args...>`

`Signal` (`include/moba/signal.hpp:20`) to typowany sygnał — subskrybenci są
powiadamiani gdy `emit()` jest wołane. Bezpieczne do unsubscribe-during-emit.

```cpp
Signal<int> s;
auto id = s.subscribe([](int x) { /* ... */ });
s.emit(42);        // wywołaj wszystkich subskrybentów
s.unsubscribe(id);  // wyrejestruj
```

### 9.2. Typowane eventy

Zamiast jednego `GameEvent` z enumem, każdy event to osobny struct
(`include/moba/event.hpp:19`):

```cpp
struct AttackHit      { actor_id, target_id, amount, damage_type, source, time };
struct DamageDealt    { actor_id, target_id, amount, damage_type, source, time };
struct DamageReceived { actor_id, target_id, amount, damage_type, source, time };
struct HealApplied    { target_id, amount, source, time };
struct Death          { actor_id, target_id, source, time };
```

`amount` w `DamageDealt`/`DamageReceived` to wartość **post-mitigation**.

### 9.3. `Simulation`

`Simulation` (`include/moba/simulation.hpp:23`) to struct z:

- `champions` — `std::vector<Champion>` (wszyscy uczestnicy walki)
- `onAttackHit`, `onDamageDealt`, `onDamageReceived`, `onHealApplied`, `onDeath`
  — po jednym `Signal` per typ eventa

Konstruktor `Simulation()` podpinaca **wewnętrzne handlery** które
implementują reguły gry:

```
AttackHit      → oblicz mitigated_damage → emit DamageReceived + DamageDealt
DamageReceived → aplikuj HP loss (shield absorbuje) → jeśli HP ≤ 0: emit Death
HealApplied    → aplikuj HP gain (cap MaxHP)
```

Użytkownik subskrybuje swoje pasywy do tych samych sygnałów — reakcje
łączą się z regułami frameworka.

### 9.4. Efekty zwrotne (feedback)

Subskrybent może emitować nowe eventy z poziomu handlera — wywołanie
`emit()` na innym `Signal` natychmiast wyzwala jego subskrybentów (synchronicznie).
Przykład łańcucha:

```
AttackHit → (wewnętrzny handler) → DamageDealt → (user handler: lifesteal)
  → HealApplied → (wewnętrzny handler) → HP gain
```

To pozwala budować reakcje: atak → damage → counter-heal → ...

### 9.5. Source chain w eventach

Wewnętrzny handler `AttackHit → DamageReceived` tworzy łańcuch provenance:
source eventa damage ma `parent` wskazujący na source eventa attack. Dzięki
temu subskrybenci widzą pełną historię:

```cpp
// ev.source.name           → "Damage"
// ev.source.parent->name   → "Basic attack"
// ev.source.parent->parent → source oryginalnego AttackHit
```

---

## 10. Pełny potok — od przedmiotów do obrażeń

```
Simulation
├── champions[]          ← lista championów
├── onAttackHit          ← Signal<AttackHit>
├── onDamageDealt        ← Signal<DamageDealt>
├── onDamageReceived     ← Signal<DamageReceived>
├── onHealApplied        ← Signal<HealApplied>
├── onDeath              ← Signal<Death>
│
├── onAttackHit.emit({0, 1, 100.0, Physical, src, 0.0})
│
└── [wewnętrzne handlery + user subskrybenci]:
    │  1. AttackHit → mitigated_damage → DamageReceived + DamageDealt
    │  2. DamageReceived → HP loss (shield absorbuje) → Death jeśli HP ≤ 0
    │  3. HealApplied → HP gain (cap MaxHP)
    │  4. User passives reagują na sygnały (lifesteal, shield proc, counter, ...)
    │  5. User passives mogą emitować nowe eventy (feedback)
    └── wszystko synchroniczne, kolejność = kolejność subskrypcji

Champion
├── mod_db          ← przedmioty, baza, runy (Base/Inc/More modyfikatory)
├── passives        ← pasywne efekty (zwracają typed Modifier[])
│
├── getBaseStats()  → Stats    [potok Base/Inc/More na mod_db]
│
├── applyPassives(base, final, time)
│   │  1. Skopiuj mod_db
│   │  2. Dodaj mods od każdej passive
│   │  3. Usuń alive=false
│   │  4. Zwróć pipeline(working_mod_db)
│   └── → Stats
│
├── evaluateChampion(eps, max_iter, time)
│   │  Iteruje applyPassives do fixed-point
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
// 1. Stwórz championów
Champion attacker{{Stat::MaxHP, 1800}, {Stat::AD, 320}, {Stat::AR, 80}, ...};
Champion target{{Stat::MaxHP, 2800}, {Stat::AD, 180}, {Stat::AR, 120}, ...};

// 2. Dodaj pasywne
attacker.addPassive(factory.make(
    [](const Stats &, const Stats &final, const Type &) {
      return Champion::PassiveResult{
          {{Stat::AD, ModType::Base, missing * 100.0, {}}}, true};
    }));

// 3. Rozwiąż statystyki
Stats atk_stats = attacker.evaluateChampion();
Stats tgt_stats = target.evaluateChampion();

// 4. Oblicz obrażenia ręcznie
Type dealt = mitigated_damage(atk_stats[AD], Physical, tgt_stats, ...);

// 5. Persystuj stan ręcznie
target.mod_db.replace(Stat::CurrentHP, Base, hp_left, Source{"Base", ""});
```

### Styl z eventami (Signal)

```cpp
// 1. Stwórz Simulation
Simulation sim;
sim.champions.push_back(Champion{{Stat::MaxHP, 1000}, {Stat::CurrentHP, 1000},
                                  {Stat::AD, 100}, {Stat::LifeSteal, 0.12}});
sim.champions.push_back(Champion{{Stat::MaxHP, 1000}, {Stat::CurrentHP, 1000},
                                  {Stat::AR, 100}});

// 2. Subskrybuj pasywy do sygnałów
// Lifesteal: on DamageDealt → heal attacker
sim.onDamageDealt.subscribe([&sim](const DamageDealt &ev) {
  auto atk = sim.champions[ev.actor_id].getBaseStats();
  Type heal = ev.amount * getStat(atk, Stat::LifeSteal);
  sim.onHealApplied.emit({ev.actor_id, heal, Source{"Lifesteal", ""}, ev.time});
});

// Shield proc: on DamageReceived → +200 shield
sim.onDamageReceived.subscribe([&sim](const DamageReceived &ev) {
  if (ev.target_id == 1)
    sim.champions[1].mod_db.add(
        Stat::ShieldHP, ModType::Base, 200.0, Source{"Sterak's Gage", ""});
});

// 3. Emituj attack event
auto jinx = std::make_shared<Source>("Jinx", "champion");
sim.onAttackHit.emit({0, 1, 100.0, TypeDamage::Physical,
                      Source{"Basic attack", "auto", jinx}, 0.0});

// 4. Sprawdź wynik
Stats t = sim.champions[1].getBaseStats();
// t[CurrentHP] = 950 (50 damage mitigated)
// t[ShieldHP] = 200 (shield proc)
```

---

## 12. Struktura plików

```
include/
  moba_sim.hpp              Umbrella header (12 linii)
  moba/
    types.hpp               Type, Stat, ModType, TypeDamage, ConvergenceError
    source.hpp              Source, SourcePtr
    mod_db.hpp               Modifier, ModDB
    event.hpp               AttackHit, DamageDealt, DamageReceived, HealApplied, Death
    signal.hpp              Signal<Args...>
    champion.hpp            Champion, Passive, PassiveResult, PassiveEntry, PassiveFactory
    combat.hpp              mitigated_damage, apply_damage_to_shield, getStat/setStat
    simulation.hpp          Simulation
src/
  moba_sim.cpp              Implementacja (324 linie)
tests/
  test_champion.cpp          Champion, passives, evaluateChampion
  test_combat.cpp            Walka, obrażenia, penetracja, DoT, shield, lifesteal
  test_scenarios.cpp         Scenariusze end-to-end
  test_mod_db.cpp            ModDB, modyfikatory, predykaty
  test_post_mitigation_damage.cpp  Formuła pancerza
  test_champion_stats.cpp    Statystyki + edge cases + Source
  test_events.cpp            Signal system, event chaining, Simulation
nix/default.nix              Flake: devShell, pre-commit, paczka, checks
```

## 13. Budowanie i testowanie

```sh
nix develop          # dev shell z toolchain + hooks
cmake -B build
cmake --build build
ctest --test-dir build           # 216 testów
nix flake check                  # pre-commit + build + tests
```