# Przewodnik przepisania moba-sim od zera

Dokument zawiera wskazówki, decyzje projektowe i lekcje wyniesione z obecnego
implementationu, które warto mieć na uwadze pisząc projekt od nowa.

---

## 1. Stack technologiczny

| Warstwa          | Technologia                                    |
| ---------------- | --------------------------------------------- |
| Język            | C++23 (ISO, bez rozszerzeń GNU)               |
| Build            | CMake ≥ 3.20 + Ninja                           |
| Testy C++        | Catch2 v3 (header-only, CTest integration)    |
| Testy Python     | pytest                                         |
| Python bindings  | nanobind + scikit-build-core                   |
| Dokumentacja     | Doxygen (XML) + Sphinx + Breathe + furo        |
| Dev environment  | Nix flakes (reproducible, declarative)         |
| Linting          | clang-format + clang-tidy (pre-commit hooks)  |

### Konfiguracja CMake

```cmake
cmake_minimum_required(VERSION 3.20)
project(moba_sim CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(src)

option(MOBA_SIM_BUILD_TESTS "Build C++ tests" ON)
option(MOBA_SIM_BUILD_PYTHON "Build Python bindings" OFF)
```

Kluczowe: `CMAKE_EXPORT_COMPILE_COMMANDS ON` — wymagane dla clang-tidy.

### Nix flake

Dev shell powinien od razu zawierać wszystko:
- toolchain (clang, cmake, ninja)
- Catch2
- Python z pakietami (nanobind, numpy, pytest, sphinx, breathe, furo)
- doxygen
- pre-commit hooks (clang-format, clang-tidy, nixfmt)

Python powinien być bundle'owany przez `python3.withPackages`, NIE przez
`pip install` ani `PYTHONPATH`. Pakiet Pythona budowany jako Nix derivation
(`python3Packages.buildPythonPackage` + scikit-build-core).

---

## 2. Struktura plików

```
include/
  moba_sim.hpp              Umbrella header (tylko #include sub-headers)
  moba/
    types.hpp               Type, Stat, ModType, TypeDamage, ConvergenceError
    source.hpp              Source, SourcePtr
    mod_db.hpp              Modifier, ModDB
    signal.hpp              Signal<Args...>
    event.hpp               AttackHit, DamageDealt, DamageReceived, HealApplied, Death
    champion.hpp            Champion, Passive, PassiveResult, PassiveEntry, PassiveFactory
    combat.hpp              mitigated_damage, apply_damage_to_shield, getStat/setStat
    simulation.hpp          Simulation
src/
  moba_sim.cpp              Cała implementacja (jeden plik OK dla tej wielkości)
python/
  moba_sim_ext.cpp          nanobind bindings
  moba/__init__.py          Python package
  tests/test_moba.py        pytest
tests/
  test_champion.cpp         Champion, passives, evaluateChampion
  test_combat.cpp           Walka, obrażenia, penetracja, DoT, shield
  test_scenarios.cpp        Scenariusze end-to-end
  test_mod_db.cpp           ModDB
  test_post_mitigation_damage.cpp  Formuła pancerza
  test_champion_stats.cpp   Statystyki + edge cases
  test_events.cpp           Signal system, event chaining, Simulation
docs/
  Doxyfile
  sphinx/conf.py
  sphinx/index.rst
  sphinx/overview.rst
  sphinx/python.rst
  sphinx/api/*.rst
nix/default.nix
```

### Zasada: jeden nagłówek = jedna odpowiedzialność

Nie łączyć różnych konceptów w jednym pliku. `signal.hpp` jest
samowystarczalny (nie zależy od `types.hpp` ani `champion.hpp`). `event.hpp`
zależy od `types.hpp` i `source.hpp`, ale NIE od `champion.hpp`.

---

## 3. Rdzeń: potok Base/Inc/More

### Koncepcja

```
getStat(stat) = sum(Base) * (1 + sum(Inc)) * product(More)
```

Trzy typy modyfikatorów:
- **Base** — sumowane addytywnie: `10 + 20 + 30 = 60`
- **Inc** — sumowane od 1.0: `1.0 + 0.1 + 0.2 = 1.3`
- **More** — mnożone od 1.0: `1.1 * 1.2 * 1.3 = 1.716`

### Implementacja

`ModDB` to wektor `Modifier{stat, type, value, source}`. Każdy getter iteruje
po wektorze i filtruje po `stat` + `type` (+ opcjonalny predykat po `source`).

Wskazówka: NIE kategoryzuj modifierów w osobnych strukturach (np. osobne mapy
dla Base/Inc/More). Jeden wektor z filtrowaniem jest prostszy i wystarczający.

### Filtrowanie po źródle

Każdy getter przyjmuje opcjonalny predykat `std::function<bool(const Modifier&)>`.
Domyślnie "akceptuj wszystkie". To pozwala np. sumować tylko bonusy z
przedmiotów:

```cpp
db.getSumStat(Stat::AD, [](const auto &m) { return m.source.name == "Item"; });
```

---

## 4. Passives — serce systemu

### Koncepcja

Passive to `std::function` — czarna skrzynka. Framework woła ją, zbiera
modyfikatory, nie wie co passive robi ani jak długo żyje.

```cpp
using Passive = std::function<PassiveResult(
    const Stats &base, const Stats &final, const Type &time)>;
```

### PassiveResult

```cpp
struct PassiveResult {
  std::vector<Modifier> mods;  // wpinają się w potok Base/Inc/More
  bool alive = true;           // false = usuń po aplikacji
};
```

### Kluczowa decyzja: passive jest jedynym autorytetem swojego lifetime

NIE próbuj zarządzać czasem życia pasyw z poziomu frameworka. Brak
`duration`, `start_time`, `expiry` w `PassiveEntry`. Passive sama decyduje:

- **permanent** — zawsze `alive=true`
- **one-shot** — `alive=false` po jednym wywołaniu
- **temp** — `alive=false` gdy `time - start >= duration` (sprawdza sama)

### ID passives — auto-increment

`PassiveFactory::make()` auto-inkrementuje ID. `addPassive()` deduplikuje:
ten sam ID = refresh (zastąp), nowy = dodaj.

Lekcja: NIE używaj enumów jako ID passives. Auto-increment jest prostszy i
wystarczający. Enum wymusza zmiany w definicji za każdym razem gdy dodajesz
pasyw.

### applyPassives vs evaluateChampion

Dwa tryby:

**`applyPassives(base, final, time)`** — jeden krok symulacji:
1. Skopiuj `mod_db` → working
2. Dla każdej passive: wywołaj, dodaj mods do working
3. Jeśli `alive=false` → usuń passive
4. Zwróć `pipeline(working)`

**`evaluateChampion(eps, max_iter, time)`** — fixed-point:
1. Iteruj `applyPassivesNoRemove` do zbieżności (delta ≤ eps)
2. NIE usuwaj passives podczas iteracji (muszą być aplikowane wielokrotnie)
3. Po zbieżności usuń `alive=false` z ostatniej iteracji
4. Rzuć `ConvergenceError` jeśli nie zbieżne w `max_iter`

Klucz: osobna funkcja `applyPassivesNoRemove` (anonimowa, w .cpp) — folduje
mods bez usuwania, zwraca `pair<Stats, vector<bool>>`.

---

## 5. Signal<Args...> — typowane sygnały

### Koncepcja

Zamiast jednego enuma `EventKind` + jednego `GameEvent` ze wszystkimi polami,
każdy event to osobny struct dispatchowany przez osobny `Signal`.

### Implementacja

```cpp
template <typename... Args> class Signal {
  using Callback = std::function<void(Args...)>;
  // wektor Listener{id, callback, dead}
  // subscribe() -> ListenerID
  // unsubscribe(id) -> mark dead, cleanup after emit
  // emit(args...) -> wywołaj wszystkich live, synchronicznie
  // clear() -> usuń wszystkich natychmiast
};
```

Kluczowe szczegóły:
- `emit_depth_` licznik — bezpieczne unsubscribe-during-emit (dead listenery
  czyszczone po outermost emit)
- Nowi subskrybenci dodani podczas emit NIE są wołani w tej samej emit
- `clear()` — do rozbijania cykli referencyjnych (ważne dla Pythona)

### Lekcja: NIE używaj kolejki FIFO + processEvents

Pierwotna implementacja miała `std::deque<GameEvent>` + `processEvents()` —
pętlę z hardcoded handlerami. Przepisane na sygnały jest:
- Prostsze (brak pętli, brak max_iter na kolejkę)
- Bardziej type-safe (kompilator wie co każdy event zawiera)
- Naturalniejsze dla usera (subscribe zamiast enqueue + process)

---

## 6. Typowane eventy

### Struktury

```cpp
struct AttackHit      { actor_id, target_id, amount, damage_type, source, time };
struct DamageDealt    { actor_id, target_id, amount, damage_type, source, time };
struct DamageReceived { actor_id, target_id, amount, damage_type, source, time };
struct HealApplied    { target_id, amount, source, time };
struct Death          { actor_id, target_id, source, time };
```

`amount` w `DamageDealt`/`DamageReceived` to wartość **post-mitigation**.

### Kolejność pól i nazewnictwo

- `actor_id` = kto zadał (index w `Simulation::champions`)
- `target_id` = kto otrzymał
- `source` = łańcuch provenance (`Source` z `parent`)
- `time` = czas symulacji

Wskazówka: NIE wrzucaj pól specyficzne dla danego eventu do wspólnego
structa. `HealApplied` nie ma `damage_type` ani `actor_id` — bo heal nie ma
typu obrażeń i jest self-targeted.

---

## 7. Simulation — wewnętrzne handlery

### Koncepcja

Konstruktor `Simulation()` podpina wewnętrzne handlery do sygnałów. User
subskrybuje te same sygnały — reakcje łączą się z regułami frameworka.

```
AttackHit      -> mitigated_damage -> emit DamageReceived + DamageDealt
DamageDealt    -> lifesteal (physical) + omnivamp (all) -> emit HealApplied
DamageReceived -> HP loss (shield absorbs) -> emit Death if HP <= 0
HealApplied    -> HP gain (cap MaxHP)
```

### Kolejność subskrypcji ma znaczenie

Wewnętrzne handlery subskrybują w konstruktorze — są pierwsze. User
subskrybuje później — jego handlery idą po wewnętrznych. To oznacza że
user widzi już-zaaplikowane efekty (np. HP już zmniejszone w
`onDamageReceived`).

Jeśli chcesz żeby user widział stan PRZED aplikacją (np. shield proc przed
damage), musisz przemyśleć kolejność. Rozwiązania:
- User subskrybuje DamageReceived (które przychodzi po AttackHit ale przed
  aplikacją HP loss — bo HP loss jest w tym samym handlerze)
- Albo osobny signal `BeforeDamageReceived`

Obecnie: DamageReceived jest emitowane PRZED aplikacją HP loss w tym samym
handlerze. User widzi event ale HP jeszcze niezmienione.

### Lifesteal/Omnivamp — wbudowane

NIE rób tego jako osobna pasywka. Wbudowany handler na `DamageDealt`:
- LifeSteal: leczy za `amount * LifeSteal` (tylko physical)
- Omnivamp: leczy za `amount * Omnivamp` (wszystkie typy)
- Emituje `HealApplied` wewnętrznie

### Source chain w eventach

Wewnętrzny handler `AttackHit -> DamageReceived` tworzy łańcuch:
`source.damage.parent = source.attack`. To pozwala subskrybentom śledzić
provenance.

---

## 8. Source — łańcuch provenance

```cpp
struct Source {
  std::string name;
  std::string description;
  SourcePtr parent;  // shared_ptr<Source>, nullptr = root
};
```

Konstruktory:
- `Source(name, desc, parent_ptr)` — jawny parent
- `Source(name, desc, origin_string)` — tworzy root parent z nazwy

`origin()` zwraca `parent ? parent->name : ""`.
`operator==` porównuje rekurencyjnie przez łańcuch.

### Lekcja: NIE nadużywaj konstruktorów

Pierwotnie było 5 konstruktorów (default, 2-arg, 3-arg ptr, 3-arg string,
initializer_list). initializer_list usunięty — zbędny, nieintuicyjny. 3
konstruktory wystarczają.

---

## 9. Combat helpers

### mitigated_damage

```cpp
Type mitigated_damage(Type raw, TypeDamage type, const Stats &target,
                      Type flat_pen = 0.0, Type pct_pen = 0.0) noexcept;
```

```
effective_resistance = (resistance - flat_pen) * (1 - pct_pen)
positive: raw * 100 / (100 + eff_res)
negative: raw * (2 - 100 / (100 - eff_res))   // max 200%
true:     raw (bypass)
```

### apply_damage_to_shield

```cpp
DamageAfterShield apply_damage_to_shield(Type shield, Type hp, Type mitigated);
```

Shield absorbuje przed HP. `damage ≤ 0` = no-op.

### Czego NIE robić

NIE twórz trywialnych helperów typu `lifesteal_heal(a, b) = a * b`. To
powoduje eksplozję funkcji które nic nie wnoszą. Inline'uj je w miejscu
użycia. Usunęliśmy: `lifesteal_heal`, `omnivamp_heal`, `amplified_heal`,
`effective_cc_duration` — każda była one-linerem.

---

## 10. Python bindings (nanobind)

### Architektura

High-level API — ukryj `ModDB`, `PassiveEntry`, `PassiveFactory`. Odsłoń:
`Champion`, `Simulation`, event structs, combat helpers, `Source`.

### Passives jako Python callables

```python
champion.add_passive(callback)
# callback(base: ndarray, final: ndarray, time: float) -> list | dict
# list = [(Stat, ModType, value, [Source]), ...]  -> alive=True
# dict = {"mods": [...], "alive": bool}
```

### Stats jako numpy.ndarray

`getBaseStats()` / `evaluateChampion()` zwracają `ndarray<float64, [25]>`.
Stat enum ma `__index__` dla indeksowania numpy.

### Signal subscription

```python
sim.on_damage_dealt_subscribe(callback)
sim.emit_attack_hit(actor, target, amount, type, source, time)
```

### Kwestie Pythonowe

1. **TypeDamage.True_** — `True` to keyword w Pythonie. Dodaj alias `True_`.
2. **Reference cycles** — nanobind nie uczestniczy w cyklicznym GC Pythona.
   `Simulation` ma `clear_signals()` do rozbijania cykli. Destruktor C++
   wywołuje `clearSignals()`. Testy używają `try/finally`.
3. **Champion construction** — `initializer_list` nie działa z nanobind.
   Użyj `__init__` z dict `{Stat: value}` lub list `[(Stat, value), ...]`.
4. **ndarray ownership** — kopiuj dane do nowego bufora z `nb::capsule`
   jako owner, żeby Python mógł go GC'ować.
5. **Nix** — buduj jako `python3Packages.buildPythonPackage`, NIE przez
   `pip install` (read-only nix store). Dev shell używa
   `python3.withPackages`.

---

## 11. Testowanie

### C++ (Catch2 v3)

- Jeden plik per komponent
- Testy edge cases: NaN, zero, negative, overflow, oscillation
- Testy convergence: zbieżne, rozbieżne, max_iter
- Testy passives: permanent, one-shot, temp, DoT, cross-stat
- ~218 testów

### Python (pytest)

- Champion basics, passives, combat helpers
- Simulation events (attack, lifesteal, death, true damage)
- Source chain, numpy integration
- `try/finally` z `sim.clear_signals()` na każdym teście z Simulation
- ~19 testów

### Wskazówki

- Pisz testy PRZEWD implementacją (TDD) — szczególnie dla fixed-point
  convergence i edge cases
- Test oscillation: passive która na przemian dodaje i odejmuje — musi
  rzucić `ConvergenceError`
- Test NaN: passive zwracająca NaN — "konwerguje" natychmiast (NaN > eps
  jest false), ale nie crashuje
- Test max_iter=0: do-while musi wykonać się raz, potem sprawdzić

---

## 12. Dokumentacja

### Doxygen

- `@brief`, `@param`, `@return`, `@throws` na każdej funkcji/klasie
- `@code`/`@endcode` dla przykładów
- `@defgroup` dla grupowania (events, combat)
- `EXTRACT_ALL = YES`, `GENERATE_XML = YES` (dla Breathe)

### Sphinx

- `conf.py` z `breathe` extension i `furo` theme
- `doxygengroup`, `doxygenstruct`, `doxygenfunction` dyrektywy
- Osobna strona `.rst` per moduł
- Strona `python.rst` z Python API guide

### Build

```sh
cd docs && doxygen Doxyfile
sphinx-build -b html sphinx _build
```

---

## 13. Pułapki i antywzorce

1. **Nie używaj enum jako ID passives** — auto-increment jest prostszy
2. **Nie używaj FIFO queue + processEvents** — sygnały są lepsze
3. **Nie twórz trywialnych helperów** (`a * b`) — inline'uj
4. **Nie usuwaj passives podczas fixed-point iteration** — dopiero po
   zbieżności
5. **Nie kategoryzuj modifierów w osobnych mapach** — jeden wektor z
   filtrowaniem
6. **Nie używaj `PYTHONPATH`** — buduj jako Nix derivation
7. **Nie używaj `pip install` w nix** — read-only store; użyj
   `python3.withPackages`
8. **Nie zapomnij o `clear_signals()`** — cykle referencyjne w Pythonie
9. **Nie używaj `initializer_list` w nanobind** — użyj dict/list
10. **Nie używaj `True` jako nazwy enuma w Python** — dodaj `True_` alias

---

## 14. Ujednolicenie passives i signals

### Problem w obecnym projekcie

Obecnie istnieją **dwa rozłączne systemy reakcji**:

1. **Passives** (`Champion::Passive`) — wołane podczas `evaluateChampion()` /
   `applyPassives()`. Otrzymują `(base, final, time)`, zwracają modyfikatory.
   Nie widzą eventów. Nie mogą zareagować na obrażenia, heal, śmierć.

2. **Signals** (`Simulation::onDamageReceived.subscribe(...)`) — wołane gdy
   event jest emitowany. Widzą event, mogą modyfikować `mod_db` ręcznie, ale
   nie uczestniczą w fixed-point iteration. Nie zwracają `PassiveResult`.

To zmusza użytkownika do pisania **two parallel code paths**:
- Pasyw dająca bonus AD → `addPassive(callback)`
- Pasyw dająca shield na DamageReceived → `on_damage_received_subscribe(callback)`
- Pasyw dająca bonus AD i shield na DamageReceived → **oba**, zduplikowana logika

Nie da się napisać jednej pasywki która "daje +10 AD permanentnie i +200
shield gdy otrzyma damage". Trzeba to rozbić na dwie części w dwóch
systemach.

### Rozwiązanie: jeden callback z event-aware sygnaturą

Passive otrzymuje opcjonalny event. Gdy jest wołana z `event = monostate`
(zwykła ewaluacja statów) — działa jak dziś. Gdy jest wołana z konkretnym
eventem — może zareagować:

```cpp
// Event który passive może zobaczyć (lub nie — monostate = normalna ewaluacja)
using PassiveEvent = std::variant<
    std::monostate,       // brak eventu — normalna ewaluacja
    AttackHit,
    DamageDealt,
    DamageReceived,
    HealApplied,
    Death
>;

using Passive = std::function<PassiveResult(
    const Stats &base,
    const Stats &final,
    const Type &time,
    const PassiveEvent &event)>;
```

### PassiveResult — rozszerzone

Passive może nie tylko zwracać modyfikatory, ale też emitować eventy
(jak obecne `emitted_events` które zostało usunięte — teraz wraca, ale
czystsze):

```cpp
struct PassiveResult {
    std::vector<Modifier> mods;
    std::vector<PassiveEvent> emitted_events;  // nowe eventy do dispatchu
    bool alive = true;
};
```

### Jak to działa w fixed-point

`evaluateChampion()` woła passive z `event = monostate` — pasywa zwraca
modyfikatory, iteracja do zbieżności. Jak dziś.

### Jak to działa z eventami

`Simulation` dispatchuje event do **wszystkich passyw wszystkich championów**:

```
1. Event arrives (np. DamageReceived dla championa #1)
2. Simulation.iterate:
   for each champion:
     for each passive in champion.passives:
       result = passive(base, final, time, event)
       apply result.mods to champion.mod_db
       enqueue result.emitted_events
   re-evaluate affected champions (fixed-point z eventem w ręce)
3. Flush emitted_events (kolejne iteracje)
```

Klucz: passive widzi event **wraz z** normalną ewaluacją. Nie trzeba dwóch
systemów. Pasywa która daje +10 AD i +200 shield na damage to **jeden
callback**:

```cpp
factory.make([](const Stats &base, const Stats &final, const Type &time,
                const PassiveEvent &ev) -> PassiveResult {
    std::vector<Modifier> mods;
    mods.push_back({Stat::AD, ModType::Base, 10.0, {}});  // permanent

    if (std::holds_alternative<DamageReceived>(ev)) {
        mods.push_back({Stat::ShieldHP, ModType::Base, 200.0, {}});  // reactive
    }

    return {mods, {}, true};
});
```

### Signal nadal istnieje — ale dla user-level reactions

`Signal` nie znika. Służy do rzeczy które **nie są pasywami**:
- Logging / UI updates (`onDeath.subscribe([](const Death &ev) { show_ui(); })`)
- Meta-systems (achievement tracking, replay recording)
- Externally triggered events (`onAttackHit.emit(...)`)

Rozróżnienie:
- **Passive** — modyfikuje statystyki championa, uczestniczy w fixed-point
- **Signal subscriber** — observer, nie modyfikuje statów, nie uczestniczy
  w fixed-point

### Simulation.dispatchEvent

Nowa metoda która łączy pasywy i eventy:

```cpp
void Simulation::dispatchEvent(const PassiveEvent &ev) {
    // 1. Broadcast eventu do wszystkich pasyw wszystkich championów
    for (auto &champ : champions) {
        auto base = champ.getBaseStats();
        auto final = base;
        std::vector<PassiveEvent> new_events;

        for (auto it = champ.passives.begin(); it != champ.passives.end();) {
            auto result = it->passive(base, final, 0.0, ev);
            for (const auto &m : result.mods) {
                champ.mod_db.add(m.stat, m.type, m.value, m.source);
            }
            for (auto &ne : result.emitted_events) {
                new_events.push_back(std::move(ne));
            }
            if (!result.alive) {
                it = champ.passives.erase(it);
            } else {
                ++it;
            }
        }

        // Kolejkuj nowe eventy
        for (auto &ne : new_events) {
            event_queue_.push_back(std::move(ne));
        }
    }

    // 2. Re-ewaluacja (fixed-point)
    evaluateAll();

    // 3. Flush kolejki (rekurencyjnie, ale z limitem)
    while (!event_queue_.empty() && iter++ < max_iter) {
        auto next = std::move(event_queue_.front());
        event_queue_.pop_front();
        dispatchEvent(next);
    }
}
```

### Wewnętrzne handlery nadal istnieją

`AttackHit → DamageReceived`, `DamageReceived → HP loss → Death`,
`HealApplied → HP gain` — to nadal wewnętrzne handlery w `Simulation`
konstruktorze. Ale zamiast subskrybować sygnały, mogą być częścią
`dispatchEvent`:

```cpp
void Simulation::dispatchEvent(const PassiveEvent &ev) {
    // Wewnętrzne reguły (hardcoded game logic)
    std::visit overloaded{
        [](std::monostate) {},  // normalna ewaluacja, nic do zrobienia
        [&](const AttackHit &e) {
            // mitigated_damage -> emit DamageReceived + DamageDealt
            ...
        },
        [&](const DamageReceived &e) {
            // HP loss (shield absorbs) -> emit Death if HP <= 0
            ...
        },
        [&](const HealApplied &e) {
            // HP gain (cap MaxHP)
            ...
        },
        [&](const Death &) {},
    }, ev);

    // Broadcast do pasyw
    for (auto &champ : champions) { ... }

    // Re-ewaluacja + flush
    ...
}
```

### Korzyści

1. **Jeden system** — pasywa reagują na eventy i na normalną ewaluację w tej
   samej sygnaturze
2. **Brak duplikacji** — "bonus AD + shield na damage" to jeden callback, nie
   dwa
3. **Pasywy uczestniczą w fixed-point** — modyfikatory od pasyw są aplikowane
   przez potok, nie bypassują go
4. **Signals zostają dla observerów** — logging, UI, meta — bez modyfikacji
   statów
5. **Event chaining** — pasywa emitują nowe eventy przez `emitted_events`,
   `dispatchEvent` flushuje kolejkę rekurencyjnie

### Kompatybilność z Python bindings

Passive callback w Python:

```python
def my_passive(base, final, time, event):
    mods = [(Stat.AD, ModType.Base, 10.0)]
    if isinstance(event, DamageReceived):
        mods.append((Stat.ShieldHP, ModType.Base, 200.0))
    return {"mods": mods, "alive": True}
```

Nanobind mapuje `std::variant<monostate, AttackHit, ...>` na Python
`None | AttackHit | DamageReceived | ...`. `isinstance` działa naturalnie.

### Co z `Signal::emit()`?

`emit()` nadal jest synchroniczne dla signals (observerów). Ale
`Simulation::dispatchEvent()` używa kolejki dla pasyw (żeby uniknąć
stack overflow na deep chain). To rozdziela:
- **Signals** — natychmiastowe, synchroniczne, dla observerów (bezpieczne
  bo nie emitują rekurencyjnie)
- **Passive events** — kolejkowane, flushowane z limitem iteracji, dla
  modyfikacji statów

---

## 16. Kolejność implementacji (sugerowana)

1. **types.hpp** — Type, Stat, ModType, TypeDamage, ConvergenceError
2. **source.hpp** — Source z parent chain
3. **mod_db.hpp + testy** — Modifier, ModDB, pipeline Base/Inc/More
4. **combat.hpp + testy** — post_mitigation_damage, mitigated_damage,
   apply_damage_to_shield
5. **signal.hpp** — Signal<Args...> (dla observerów)
6. **event.hpp** — PassiveEvent variant + event structy
7. **champion.hpp + testy** — Champion, Passive (z event-aware sygnaturą),
   PassiveResult (z emitted_events), PassiveEntry, PassiveFactory,
   applyPassives, evaluateChampion
8. **simulation.hpp + testy** — Simulation z dispatchEvent, wewnętrznymi
   regułami (AttackHit→DamageReceived, lifesteal, death, heal), kolejką
   eventów
9. **Python bindings** — nanobind, high-level API, passive callback z event
10. **Dokumentacja** — Doxygen comments, Sphinx config, RST pages
11. **Nix flake** — devShell, package, checks

### Iteracyjnie

Pisz testy równolegle z implementacją. Po każdym module:
- `cmake --build build && ctest --test-dir build`
- Format: `pre-commit run -a`

---

## 17. Liczby orientacyjne

| Metryka             | Wartość (obecny projekt) |
| ------------------- | ----------------------- |
| Linie nagłówków     | ~450 (8 plików)         |
| Linie implementacji | ~360 (1 plik)           |
| Testy C++           | 218                     |
| Testy Python        | 19                      |
| Linie testów C++    | ~4200                   |
| Linie testów Python | ~230                    |
| Linie Python binding| ~460                    |
| Moduły              | 8 (types, source, mod_db, signal, event, champion, combat, simulation) |