# Jak działa moba-sim — kompletny przewodnik

## 1. Statystyki (`Stat`)

Wszystko kręci się wokół 25 statystyk championa (`include/moba_sim.hpp:22`):

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

## 2. Modyfikatory i potok Base/Inc/More (`ModDB`)

`ModDB` (`include/moba_sim.hpp:83`) to wektor modyfikatorów. Każdy `Modifier` ma:
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

## 3. Champion

`Champion` (`include/moba_sim.hpp:122`) to struct z dwoma polami:

- `mod_db` — baza modyfikatorów (statystyki bazowe + z przedmiotów)
- `passives` — kolejka efektów pasywnych

Konstruktor przyjmuje initializer list par `(Stat, wartość)`:

```cpp
Champion c{{Stat::MaxHP, 1000}, {Stat::AD, 50}, {Stat::AR, 100}};
```

To dodaje modyfikatory `Base` do `mod_db` ze źródłem `"Base"`.

`getBaseStats()` (`src/moba_sim.cpp:132`) ewaluuje pełny potok dla wszystkich
statystyk — zwraca `Stats` bez passives.

---

## 4. Passives — serce systemu

Passive to `std::function` z sygnaturą (`include/moba_sim.hpp:145`):

```cpp
PassiveResult(const Stats &base, const Stats &final, const Type &time)
```

Otrzymuje:

- `base` — statystyki z `mod_db` (bez passives)
- `final` — aktualny wynik (z poprzedniej iteracji)
- `time` — czas symulacji (absolutny, rosnący)

Zwraca:

- `mods` — wektor `Modifier` (Base/Inc/More — wpinają się w potok!)
- `alive` — czy passive zostaje (`true`) czy jest usuwane (`false`)

### 4.1. Typy passives — passive sama decyduje o swoim lifetime

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

### 4.2. ID passives — type-safe enum slots

ID to enum zdefiniowany przez użytkownika (`include/moba_sim.hpp:152`):

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

---

## 5. Jak `applyPassives` oblicza wynik

`applyPassives(base, final, time)` (`src/moba_sim.cpp:146`) — jeden krok
symulacji:

```
1. Skopiuj mod_db → working
2. Dla każdej passive:
   a. Wywołaj: passive(base, final, time) → (mods, alive)
   b. Dodaj mods do working
   c. Jeśli alive=false → usuń passive z kolejki
3. Zwróć statsFromModDB(working) — pełny potok Base/Inc/More
```

Kluczowe: modyfikatory passives **wpinają się w potok** — passive `+10 AD`
przy `Inc=0.2, More=1.5` daje `10 * 1.2 * 1.5 = 18`, nie płaskie `10`.

Parametr `base` jest przekazywany do passives jako informacja — wynik jest
obliczany z `mod_db + passive mods` przez potok, nie z `base + flat bonus`.

---

## 6. Jak `evaluateChampion` znajduje fixed-point

`evaluateChampion(eps, max_iter, time)` (`src/moba_sim.cpp:172`) — rozwiązuje
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

## 7. Helpery dla obrażeń i CC

### `mitigated_damage(raw, type, target, flat_pen, pct_pen)`
(`src/moba_sim.cpp:206`)

```
effective_resistance = (resistance - flat_pen) * (1 - pct_pen)

dla dodatniego AR:
  post_mitigation = raw * 100 / (100 + effective_resistance)

dla ujemnego AR (amplifikacja, max 200%):
  post_mitigation = raw * (2 - 100/(100 - effective_resistance))
```

True damage omija mitigację całkowicie.

### `apply_damage_to_shield(shield, hp, mitigated)`
(`src/moba_sim.cpp:231`)

Shield absorbuje obrażenia przed HP. Zwraca
`{shield_remaining, hp_remaining}`.

- Jeśli `shield ≥ damage`: shield absorbing wszystko, HP nietknięte
- Jeśli `shield < damage`: shield = 0, HP traci resztę
- Jeśli `damage ≤ 0`: nic się nie dzieje (negatywny damage nie leczy)

### `effective_cc_duration(raw_duration, tenacity)`
(`src/moba_sim.cpp:226`)

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

## 8. Pełny potok — od przedmiotów do obrażeń

```
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

## 9. Typowy przepływ walki

```cpp
// 1. Stwórz championów
Champion attacker{{Stat::MaxHP, 1800}, {Stat::AD, 320}, {Stat::AR, 80}, ...};
Champion target{{Stat::MaxHP, 2800}, {Stat::AD, 180}, {Stat::AR, 120}, ...};

// 2. Dodaj pasywne
enum class PassiveId : std::size_t { AdcPassive, BruiserShred, Burn, Hit };
Champion::PassiveFactory factory;

attacker.addPassive(factory.make(PassiveId::AdcPassive,
    [](const Stats &, const Stats &final, const Type &) {
      Type missing = (final[MaxHP] - final[CurrentHP]) / final[MaxHP];
      return Champion::PassiveResult{
          {{Stat::AD, ModType::Base, missing * 100.0, {}}}, true};
    }));

target.addPassive(factory.make(PassiveId::BruiserShred,
    [](const Stats &, const Stats &, const Type &) {
      return Champion::PassiveResult{
          {{Stat::AR, ModType::Base, -20.0, {}}}, true};
    }));

// 3. Rozwiąż statystyki (fixed-point passives)
Stats atk_stats = attacker.evaluateChampion();
Stats tgt_stats = target.evaluateChampion();

// 4. Oblicz obrażenia
Type dealt = mitigated_damage(
    atk_stats[std::to_underlying(Stat::AD)],
    TypeDamage::Physical, tgt_stats,
    atk_stats[std::to_underlying(Stat::ArmorPenFlat)],
    atk_stats[std::to_underlying(Stat::ArmorPenPct)]);

// 5. Shield absorbuje, potem HP
auto [shield_left, hp_left] = apply_damage_to_shield(
    tgt_stats[std::to_underlying(Stat::ShieldHP)],
    tgt_stats[std::to_underlying(Stat::CurrentHP)], dealt);

// 6. Persystuj stan
target.mod_db.replace(Stat::CurrentHP, ModType::Base, hp_left,
                      Source{"Base", ""});

// 7. Lifesteal leczy atakującego
Type heal = lifesteal_heal(
    dealt, atk_stats[std::to_underlying(Stat::LifeSteal)]);

// 8. Symulacja krokowa (DoT, temp effects, regen)
Stats base = target.getBaseStats();
Stats final = base;
for (double t = 1.0; t <= 5.0; t += 1.0) {
  final = target.applyPassives(base, final, t);  // burn, regen, etc.
}

// 9. Odśwież temp passive (przedłuż burn)
target.addPassive(factory.make(PassiveId::Burn, make_burn(5.0, 3.0)));

// 10. CC z tenacity
Type stun = effective_cc_duration(1.5,
    tgt_stats[std::to_underlying(Stat::Tenacity)]);  // 1.5 * 0.7 = 1.05s
```

---

## 10. Struktura plików

```
include/moba_sim.hpp   Publiczne API (258 linii)
src/moba_sim.cpp       Implementacja (247 linii)
tests/
  test_champion.cpp          Champion, passives, evaluateChampion
  test_combat.cpp            Walka, obrażenia, penetracja, DoT, shield, lifesteal
  test_scenarios.cpp         Scenariusze end-to-end
  test_mod_db.cpp            ModDB, modyfikatory, predykaty
  test_post_mitigation_damage.cpp  Formuła pancerza
  test_champion_stats.cpp    Nowe statystyki + edge cases
nix/default.nix          Flake: devShell, pre-commit, paczka, checks
```

## 11. Budowanie i testowanie

```sh
nix develop          # dev shell z toolchain + hooks
cmake -B build
cmake --build build
ctest --test-dir build           # 237 testów
nix flake check                  # pre-commit + build + tests
```