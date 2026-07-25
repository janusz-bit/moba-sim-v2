# Analiza v3 — wnioski z lolmath i podobnych projektów

Dokument zawiera analizę porównawczą moba-sim-v2 z produkcyjnymi projektami
(z naciskiem na lolmath.net) oraz rekomendacje architektoniczne dla ewentualnego
v3 pisanego od zera. Data analizy: 2026-07-25.

---

## 1. Co zostawić z v2 (sprawdzone wzorce)

### 1.1 Potok Base/Inc/More

```
getStat = sum(Base) * (1 + sum(Inc)) * prod(More)
```

Jeden wektor `Modifier` z filtrowaniem po `ModType`, nie osobne mapy per typ.
To jest fundament modelu statystyk i działa poprawnie.

### 1.2 Passive jako jedyny autorytet swojego lifetime

`alive` flag zamiast `duration`/`start_time` w frameworku. Auto-increment ID,
deduplikacja przez `addPassive`. Pasyw sam decyduje kiedy się kończy.

### 1.3 Ujednolicony PassiveEvent variant

Pasywa reagują na eventy i na normalną ewaluację w jednej sygnaturze
`(base, final, time, event)`. Eliminuje dwa równoległe systemy (kluczowa
decyzja z rozdziału 14 rewrite-guide).

### 1.4 Signal dla observerów

Side-effect-free, synchroniczne. Nie modyfikują statów, nie uczestniczą
w fixed-point. Dla logging/UI/meta.

### 1.5 Source provenance chain

`shared_ptr<Source>` z parent. Pozwala śledzić każdy modyfikator do jego
źródła (champion, item, rune, passive).

### 1.6 Stack technologiczny

C++23 + Catch2 v3 + nanobind + Nix flakes + Doxygen/Sphinx. Sprawdzony,
reproducible, dobrze zintegrowany.

---

## 2. Co napisać inaczej / dodać

### 2.1 Model statystyk — rozdzielenie base/bonus

v2 ma płaską tablicę `Stats[25]`. Potrzeba rozdzielenia na base i bonus:

```cpp
struct StatValue { double base, bonus; };
```

Powody:
- Lethality/flat reduction afektuje bonus armor first
- % penetration działa na total, ale reduction zmienia actual armor targeta
- lolmath wylicza durability na podstawie base vs bonus

### 2.2 Growth formula per level (brak w v2)

v2 nie modeluje poziomów championa. Wzór z lol-sim-spec:

```
stat = base + growth * (n-1) * (0.7025 + 0.0175*(n-1))
```

Essential dla realistycznej symulacji — bez tego symulujesz staty
z "powietrza". Champion ma levels 1-18.

### 2.3 Przedmioty jako first-class obywatele

v2 dodaje staty przez ModDB ręcznie. Potrzeba `Item` z `ItemStats`,
`ItemPassive`, `ItemActive`, `buildsFrom`. To potrzebne dla:
- Build optimization (jak lolmath — przetestuj wszystkie kombinacje)
- Gold efficiency analysis
- Recipe tracking

### 2.4 Runy — pełny system Runes Reforged

v2 nie ma run wcale. Potrzeba 5 drzew, 16 keystones, stat shards.
Bez run nie ma realistycznego DPS — Conqueror, Lethal Tempo, Electrocute
drastycznie zmieniają output.

### 2.5 Combat scenarios (inspiracja lolmath)

Najważniejszy wniosek z lolmath. Ich item optimizer nie liczy "surowego
DPS" — używa weighted combat scenarios:

| Scenariusz | Waga  | Opis                                    |
| ---------- | ----- | --------------------------------------- |
| DPS        | 25%   | Sustained autoattack damage             |
| Burst      | 25%   | Szybki combo, AH ignorowane             |
| All-Out    | 50%   | Pełny combo z abilities                 |

Każdy scenariusz ma zmienne: liczba autoattacków, ability casts, combat
duration, multi-target, ranged poke, attack turret. Waga scenariusza
zależy od fazy gry (early/mid/late).

Zamiast jednego "deals 100 damage" masz modelowane realistyczne combo.

### 2.6 Abilities/spells (brak w v2)

v2 ma tylko `AttackHit` — basic attack. Potrzeba abilities z scalingiem
(`80 + 0.7*AP`), cooldownami (ability haste), typami obrażeń. Bez abilities
połowa championów nie ma sensu.

### 2.7 JSON data loading

v2 jest hardcode. Ładowanie champions/items/runes z JSON pozwala auto-update
z patchami (jak lolmath — "automatically updated with every patch").
Źródło danych: Riot Games Data Dragon API.

### 2.8 Penetration vs Reduction — pełna kolejność

v2 ma `mitigated_damage(raw, type, target, flat_pen, pct_pen)` — uproszczone.
Potrzeba 4 kroków:

```
1. flat reduction (zmienia actual armor targeta, widoczna dla wszystkich)
2. % reduction
3. % penetration (ignoruje % armor, tylko dla atakującego)
4. flat penetration (Lethality — ignoruje flat armor, tylko dla atakującego)
```

Reduction vs penetration to kluczowa różnica — reduction jest widoczna dla
wszystkich atakujących, penetration tylko dla danego atakującego.

### 2.9 Enemy team analysis (jak lolmath)

lolmath bierze enemy team comp i optymalizuje build przeciwko niemu.
v2 symuluje 1v1. Rozszerzenie do 5v5 z enemy armor/MR/threat levels.

### 2.10 Build optimizer (jak lolmath)

Największy feature lolmath: przetestuj wszystkie kombinacje przedmiotów,
rankuj po Damage/Durability/Utility. v2 nie ma tego wcale — to byłby
killer feature.

---

## 3. Sugerowana architektura v3 (od zera)

```
core/
  stats.hpp         — StatValue{base,bonus}, 28 typów, growth formula
  champion.hpp      — Champion z levels 1-18, abilities, rune page
  item.hpp          — Item z stats/passives/actives/recipe/gold
  rune.hpp          — RunePage, Keystone, stat shards
  damage.hpp        — pełny pipeline: raw→cap→reduction→pen→mitigation→shield→HP
  combat.hpp        — CombatScenario (lolmath-style: DPS/Burst/AllOut z wagami)
  simulation.hpp    — tick-based engine z eventami (jak v2 dispatchEvent)
data/
  champions/*.json  — staty championów z wiki/Data Dragon
  items.json        — wszystkie przedmioty
  runes.json        — wszystkie runy
optimizer/
  build_optimizer.hpp — brute-force/combinatorial item combinations
  evaluation.hpp     — Damage/Durability/Utility scoring (lolmath model)
bindings/
  lol_sim_ext.cpp   — nanobind, high-level API
python/
  simulator.py      — uruchamianie symulacji
  optimizer.py      — build optimization interface
  report.py         — raporty DPS/Durability/Utility
```

### Kolejność implementacji

1. stats + growth formula + testy
2. damage pipeline (pełna kolejność penetration/reduction) + testy
3. champion z levels + testy
4. item + JSON loading + testy
5. combat scenarios (DPS/Burst/AllOut) + testy
6. simulation (event system z v2 — PassiveEvent, dispatchEvent, signals)
7. runes + testy
8. build optimizer + testy
9. Python bindings + testy
10. dokumentacja + Nix

---

## 4. Podobne projekty

### 4.1 lolmath.net — najbliższy odpowiednik

Produkcyjny item optimizer (patch 26.14 w momencie analizy):
- Próbuje wszystkich kombinacji przedmiotów
- 3 osie oceny: Damage (kalkulowane), Durability (kalkulowane), Utility
  (trade-off usera)
- Combat scenarios z wagami (DPS 25%, Burst 25%, AllOut 50%)
- Enemy team analysis, rune integration, multi game mode
- Auto-update z patchami
- Artykuły: penetration calculator, ability haste vs CDR, combat scenarios,
  starting items comparison, Mejai's viability
- Open source archive: gitlab.com/lol-math/itemop-archive

### 4.2 LoL Alchemy Lab (lolalchemylab.com)

Theorycrafting tools, pokazuje formułę "gdzie każdy number pochodzi",
penetration step-by-step. Mniej ambitne niż lolmath, ale edukacyjne.

### 4.3 TB-MOBA-Simulator (github: thefireblade)

Turn-based MOBA sim na Androida. Bardzo prosty, nieporównywalnie mniejszy
zakres.

### 4.4 BattleSimulator (github: gregparkes)

2D battle sim inspirowany TABS, z ML do uczenia patternów.
Nie LoL-specific.

### 4.5 Unity/UE4 MOBA frameworks

OpenMOBA, unity-moba, MOBA_CSharp_Unity — to są pełne silniki gier,
nie symulatory statystyk. Inna kategoria.

### 4.6 Riot Games Data Dragon

Nie projekt, ale API z danymi championów/items. lolmath prawdopodobnie
z tego korzysta do auto-update.

---

## 5. Główne luki v2 vs lolmath

| Obszar               | v2                          | lolmath                          |
| -------------------- | --------------------------- | -------------------------------- |
| Build optimization   | Brak — pojedynczy setup    | Wszystkie kombinacje, ranking    |
| Combat scenarios     | Pojedynczy AttackHit        | DPS/Burst/AllOut z wagami        |
| Data-driven          | Hardcode                    | JSON, auto-update z patchami     |
| Runy                 | Brak                        | Pełny system Runes Reforged      |
| Abilities            | Brak (tylko basic attack)   | Pełne abilities z scaling/cooldown |
| Poziomy championa    | Brak                        | Levels 1-18 z growth formula     |
| Enemy team           | 1v1                         | 5v5 z team comp analysis         |
| Penetration pipeline | Uproszczony (2 kroki)       | Pełny (4 kroki: reduction→pen)   |

---

## 6. Podsumowanie

lol-sim-spec.md (jeśli istnieje w repo) już przewiduje większość tych
luk — ma JSON loading, items, runes, abilities, growth formula.
Specyfikacja jest solidna. Jeśli pisać v3 od zera, trzymać się tej
specyfikacji, dodając combat scenarios z lolmath i build optimizer
jako capstone feature.

Największe priorytety v3 (w kolejności impact/effort):
1. **Combat scenarios** (lolmath-style) — najwyższy impact, średni effort
2. **Build optimizer** — najwyższy impact, wysoki effort (capstone)
3. **Abilities + growth formula** — wysoki impact, średni effort
4. **JSON data loading** — średni impact, niski effort (enabler)
5. **Penetration pipeline** — średni impact, niski effort
6. **Runy** — średni impact, średni effort
7. **Base/bonus split** — średni impact, niski effort (fundament)