# Jak działa moba-sim — kompletny przewodnik

## 1. Statystyki (`Stat`)

Wszystko kręci się wokół 25 statystyk championa (`include/moba_sim.hpp:25`):

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

`Source` (`include/moba_sim.hpp:67`) to struktura z polami:

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

### Wsteczna kompatybilność

`Source{"Item", "Bloodthirster", "attacker"}` tworzy `parent = make_shared<Source>("attacker")`.
Metoda `.origin()` zwraca `parent ? parent->name : ""` — zastępuje stare pole
`.origin` (które było `std::string`). Używaj `.origin()` zamiast `.origin`.

`operator==` porównuje rekurencyjnie przez łańcuch (deep equality, nie
shared_ptr identity).

---

## 3. Modyfikatory i potok Base/Inc/More (`ModDB`)

`ModDB` (`include/moba_sim.hpp:118`) to wektor modyfikatorów. Każdy `Modifier` ma:
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

`Champion` (`include/moba_sim.hpp:182`) to struct z dwoma polami:

- `mod_db` — baza modyfikatorów (statystyki bazowe + z przedmiotów)
- `passives` — kolejka efektów pasywnych

Konstruktor przyjmuje initializer list par `(Stat, wartość)`:

```cpp
Champion c{{Stat::MaxHP, 1000}, {Stat::AD, 50}, {Stat::AR, 100}};
```

To dodaje modyfikatory `Base` do `mod_db` ze źródłem `"Base"`.

`getBaseStats()` (`src/moba_sim.cpp:131`) ewaluuje pełny potok dla wszystkich
statystyk — zwraca `Stats` bez passives.

---

## 5. Passives — serce systemu

Passive to `std::function` z sygnaturą (`include/moba_sim.hpp:211`):

```cpp
PassiveResult(const Stats &base, const Stats &final, const Type &time)
```

Otrzymuje:

- `base` — statystyki z `mod_db` (bez passives)
- `final` — aktualny wynik (z poprzedniej iteracji)
- `time` — czas symulacji (absolutny, rosnący)

Zwraca `PassiveResult` (`include/moba_sim.hpp:201`):

- `mods` — wektor `Modifier` (Base/Inc/More — wpinają się w potok!)
- `emitted_events` — wektor `GameEvent` do wrzucenia na kolejkę eventów
  (patrz sekcja 9 — Event system)
- `alive` — czy passive zostaje (`true`) czy jest usuwane (`false`)

### 5.1. Typy passives — passive sama decyduje o swoim lifetime

**Permanent** — zawsze `alive=true`:

```cpp
factory.make(PassiveId::Rage, [](const Stats &, const Stats &, const Type &) {
  return Champion::PassiveResult{
      {{Stat::AD, ModType::Inc, 0.2, {}}}, true};  // +20% AD na zawsze
});
```

**One-shot** — `alive=false` po jednym zastosowaniu:

```cpp
factory.make(PassiveId::Burst, [](const Stats &, const Stats &, const Type &) {
  return Champion::PassiveResult{
      {{Stat::AD, ModType::Base, 20.0, {}}}, false};  // +20 AD raz
});
```

**Temp** — wygasa na podstawie `time`:

```cpp
factory.make(PassiveId::Burn,
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

### 5.2. ID passives — type-safe enum slots

ID to enum zdefiniowany przez użytkownika (`include/moba_sim.hpp:222`):

```cpp
enum class PassiveId : std::size_t { Burn, Shield, Shred, SterakGage };

Champion::PassiveFactory factory;
champ.addPassive(factory.make(PassiveId::Burn, make_burn(0.0, 3.0)));
```

`addPassive` deduplikuje po ID — **ten sam ID = refresh** (zastąp passive),
**nowy ID = dodaj**:

```cpp
// Przedłuż burn: nowy start, ten sam slot
champ.addPassive(factory.make(PassiveId::Burn, make_burn(5.0, 5.0)));
```

### 5.3. Event handler (`on_event`)

`PassiveEntry` ma opcjonalne pole `on_event` typu `EventPassive`
(`include/moba_sim.hpp:214`):

```cpp
using EventPassive = std::function<PassiveResult(
    const Stats &base, const Stats &final, const Type &time,
    const GameEvent &event)>;
```

Passive z `on_event` reaguje na eventy z kolejki `Simulation`. Passive bez
`on_event` ignoruje eventy — działa jak wcześniej. Konstruktor
`PassiveResult` jest wstecznie kompatybilny: `PassiveResult{mods, alive}`
działa (trzeci argument `emitted_events` ma default `{}`).

Tworzenie passive z event handlerem:

```cpp
champ.addPassive(factory.make(
    /*passive=*/[](const Stats &, const Stats &, Type) {
      return Champion::PassiveResult{{}, true};
    },
    /*source=*/Source{"Bloodthirster", "lifesteal"},
    /*on_event=*/[](const Stats &, const Stats &final, Type,
                   const GameEvent &ev) -> Champion::PassiveResult {
      if (ev.kind != EventKind::DamageDealt) return {};
      Type heal = moba::lifesteal_heal(ev.amount,
                                      getStat(final, Stat::LifeSteal));
      return {{},
              true,
              {{EventKind::HealApplied, ev.actor_id, ev.actor_id, heal,
                TypeDamage::True, Source{"Lifesteal", ""}, ev.time}}};
    }));
```

`PassiveFactory::make(Passive, Source, EventPassive)` — nowe przeciążenie
(`include/moba_sim.hpp:243`).

---

## 6. Jak `applyPassives` oblicza wynik

`applyPassives(base, final, time)` (`src/moba_sim.cpp:149`) — jeden krok
symulacji:

```
1. Skopiuj mod_db → working
2. Dla każdej passive:
   a. Wywołaj: passive(base, final, time) → result{mods, events, alive}
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

`evaluateChampion(eps, max_iter, time)` (`src/moba_sim.cpp:175`) — rozwiązuje
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

## 8. Helpery dla obrażeń i CC

### `mitigated_damage(raw, type, target, flat_pen, pct_pen)`
(`src/moba_sim.cpp:209`)

```
effective_resistance = (resistance - flat_pen) * (1 - pct_pen)

dla dodatniego AR:
  post_mitigation = raw * 100 / (100 + effective_resistance)

dla ujemnego AR (amplifikacja, max 200%):
  post_mitigation = raw * (2 - 100/(100 - effective_resistance))
```

True damage omija mitigację całkowicie.

### `apply_damage_to_shield(shield, hp, mitigated)`
(`src/moba_sim.cpp:234`)

Shield absorbuje obrażenia przed HP. Zwraca
`{shield_remaining, hp_remaining}`.

- Jeśli `shield ≥ damage`: shield absorbing wszystko, HP nietknięte
- Jeśli `shield < damage`: shield = 0, HP traci resztę
- Jeśli `damage ≤ 0`: nic się nie dzieje (negatywny damage nie leczy)

### `effective_cc_duration(raw_duration, tenacity)`
(`src/moba_sim.cpp:229`)

```
effective = raw_duration * (1 - tenacity)
if effective < 0.3 → effective = 0.3   (floor per LoL Wiki)
```

Ujemna tenacity (np. Brittle) amplifikuje CC — duration rośnie.

### `lifesteal_heal` / `omnivamp_heal` / `amplified_heal`

- `lifesteal_heal(post_mitigated, pct)` = `post_mitigated * pct`
- `omnivamp_heal(post_mitigated, pct)` = `post_mitigated * pct`
- `amplified_heal(base_heal, heal_shield_power)` =
  `base_heal * (1 + heal_shield_power)`

---

## 9. Event system i `Simulation`

### 9.1. Eventy

`EventKind` (`include/moba_sim.hpp:159`):

```
AttackHit, DamageDealt, DamageReceived, HealApplied,
ShieldBroken, CCApplied, Kill, Death
```

`GameEvent` (`include/moba_sim.hpp:170`):

- `kind` — typ eventa
- `actor_id` — index w `Simulation::champions` (kto wywołał)
- `target_id` — index (może == `actor_id` dla self-heal)
- `amount` — wartość (raw damage / heal / etc.)
- `damage_type` — Physical/Magic/True (dla damage)
- `source` — łańcuch provenance (`Source` z `parent`)
- `time` — kiedy się stało

### 9.2. `Simulation`

`Simulation` (`include/moba_sim.hpp:321`) to struct z:

- `champions` — `std::vector<Champion>` (wszyscy uczestnicy walki)
- `event_queue` — `std::deque<GameEvent>` (kolejka FIFO)
- `enqueue(GameEvent)` — wrzuca event na koniec kolejki
- `processEvents(eps, max_iter)` — przetwarza kolejkę

### 9.3. Jak `processEvents` działa

```
while queue not empty and iter < max_iter:
  ev = queue.pop_front()

  // Wewnętrzne eventy:
  AttackHit → oblicz mitigated_damage → enqueue DamageReceived + DamageDealt
  DamageReceived → aplikuj HP loss (shield absorbuje) → jeśli HP ≤ 0: enqueue Death
  HealApplied → aplikuj HP gain (cap MaxHP)

  // Broadcast do wszystkich championów:
  for each champion:
    for each passive with on_event:
      result = passive.on_event(base, final, time, ev)
      apply result.mods to champion.mod_db
      enqueue result.emitted_events (na koniec kolejki)
      if !result.alive → usuń passive

  // Re-ewaluacja (fixed-point statów):
  for each champion with passives:
    champion.evaluateChampion()

if iter >= max_iter and queue not empty → ConvergenceError
```

### 9.4. Efekty zwrotne (feedback)

Passive może emitować nowe eventy przez `PassiveResult.emitted_events`. Nowe
eventy są wrzucane na **koniec** kolejki — są przetwarzane w kolejnych
iteracjach. Przykład:

```
AttackHit → DamageReceived → passive "Counter heal" emituje HealApplied
→ HealApplied jest na końcu kolejki → zostanie przetworzone dalej
```

To pozwala budować łańcuchy reakcji: atak → damage → counter → heal → ...

### 9.5. Source chain w eventach

`processEvents` tworzy łańcuch provenance gdy konwertuje `AttackHit` na
`DamageReceived`: source eventa damage ma `parent` wskazujący na source
eventa attack. Dzięki temu passive widzą pełną historię:

```cpp
// ev.source.name           → "Damage"
// ev.source.parent->name   → "Basic attack"
// ev.source.parent->parent → source oryginalnego AttackHit
```

---

## 10. Pełny potok — od przedmiotów do obrażeń

```
Simulation
├── champions[]     ← lista championów
├── event_queue     ← kolejka GameEvent (FIFO)
│
├── enqueue(AttackEvent)
│
└── processEvents()
    │  1. AttackHit → mitigated_damage → DamageReceived + DamageDealt
    │  2. DamageReceived → HP loss (shield absorbuje) → Death jeśli HP ≤ 0
    │  3. HealApplied → HP gain (cap MaxHP)
    │  4. Broadcast do on_event handlerów → mods + emitted_events
    │  5. Nowe eventy na koniec kolejki (feedback)
    │  6. Re-ewaluacja championów (fixed-point)
    └── repeat until queue empty

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
    ├── apply_damage_to_shield(shield, hp, damage)
    ├── effective_cc_duration(raw_duration, tenacity)
    ├── lifesteal_heal(post_mitigated, lifesteal_pct)
    ├── omnivamp_heal(post_mitigated, omnivamp_pct)
    └── amplified_heal(base_heal, heal_shield_power)
```

---

## 11. Typowy przepływ walki

### Stary styl (bez eventów) — nadal działa

```cpp
// 1. Stwórz championów
Champion attacker{{Stat::MaxHP, 1800}, {Stat::AD, 320}, {Stat::AR, 80}, ...};
Champion target{{Stat::MaxHP, 2800}, {Stat::AD, 180}, {Stat::AR, 120}, ...};

// 2. Dodaj pasywne (bez on_event)
attacker.addPassive(factory.make(PassiveId::AdcPassive,
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

### Nowy styl (z eventami)

```cpp
// 1. Stwórz Simulation
Simulation sim;
sim.champions.push_back(Champion{{Stat::MaxHP, 1000}, {Stat::CurrentHP, 1000},
                                  {Stat::AD, 100}, {Stat::LifeSteal, 0.12}});
sim.champions.push_back(Champion{{Stat::MaxHP, 1000}, {Stat::CurrentHP, 1000},
                                  {Stat::AR, 100}});

// 2. Dodaj passives z on_event handlerami
// Lifesteal: on DamageDealt → heal attacker
sim.champions[0].addPassive(factory.make(
    [](const Stats &, const Stats &, Type) {
      return Champion::PassiveResult{{}, true};
    },
    Source{"Bloodthirster", "lifesteal"},
    [](const Stats &, const Stats &final, Type,
       const GameEvent &ev) -> Champion::PassiveResult {
      if (ev.kind != EventKind::DamageDealt) return {};
      Type heal = moba::lifesteal_heal(ev.amount,
                                      getStat(final, Stat::LifeSteal));
      return {{},
              true,
              {{EventKind::HealApplied, ev.actor_id, ev.actor_id, heal,
                TypeDamage::True, Source{"Lifesteal", ""}, ev.time}}};
    }));

// Shield proc: on DamageReceived → +200 shield, one-shot
sim.champions[1].addPassive(factory.make(
    [](const Stats &, const Stats &, Type) {
      return Champion::PassiveResult{{}, true};
    },
    Source{"Sterak's Gage", "shield proc"},
    [](const Stats &, const Stats &, Type,
       const GameEvent &ev) -> Champion::PassiveResult {
      if (ev.kind != EventKind::DamageReceived) return {};
      return {{{Stat::ShieldHP, ModType::Base, 200.0, {}}}, false};
    }));

// 3. Enqueue attack event
auto jinx = std::make_shared<Source>("Jinx", "champion");
sim.enqueue({EventKind::AttackHit, 0, 1, 100.0, TypeDamage::Physical,
             Source{"Basic attack", "auto", jinx}, 0.0});

// 4. Process — kolejka FIFO przetwarza wszystko:
//    AttackHit → DamageDealt → lifesteal → HealApplied
//              → DamageReceived → shield proc
//                → Death (jeśli HP ≤ 0)
sim.processEvents();

// 5. Sprawdź wynik
Stats t = sim.champions[1].getBaseStats();
// t[CurrentHP] = 950 (50 damage mitigated)
// t[ShieldHP] = 200 (shield proc)
```

---

## 12. Struktura plików

```
include/moba_sim.hpp   Publiczne API (336 linii)
src/moba_sim.cpp       Implementacja (372 linie)
tests/
  test_champion.cpp          Champion, passives, evaluateChampion
  test_combat.cpp            Walka, obrażenia, penetracja, DoT, shield, lifesteal
  test_scenarios.cpp         Scenariusze end-to-end
  test_mod_db.cpp            ModDB, modyfikatory, predykaty
  test_post_mitigation_damage.cpp  Formuła pancerza
  test_champion_stats.cpp    Nowe statystyki + edge cases + Source
  test_events.cpp            Event system, Source chain, Simulation
nix/default.nix          Flake: devShell, pre-commit, paczka, checks
```

## 13. Budowanie i testowanie

```sh
nix develop          # dev shell z toolchain + hooks
cmake -B build
cmake --build build
ctest --test-dir build           # 261 testów
nix flake check                  # pre-commit + build + tests
```