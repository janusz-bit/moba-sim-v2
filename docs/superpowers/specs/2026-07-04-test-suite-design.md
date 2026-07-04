# Test Suite Implementation Design

## Problem

Projekt `moba_sim` nie ma testów. Ostatni refactor (`applyPassives`, `evaluateChampion`)
zmienił kontrakt pasywek (bonus/delta zamiast nadpisywania) i wyodrębnił pętlę
konwergencji do wolnostojącej funkcji — to zachowanie, które powinno być zamknięte
testami. Dodatkowo istniejący kod (`ModDB`, `post_mitigation_damage`) też nie ma
pokrycia.

## Podejście

Pełne pokrycie `moba_sim` w Catch2 v3, dostarczonym przez `nixpkgs` w `flake.nix`.
Testy w katalogu `tests/`, uruchamiane przez CTest.

## Zmiany

### `flake.nix` / `nix/default.nix`

Dodać `catch2_3` do `packages` w `devShells.default` (obok `cmake`, `clang`, itp.).
CMake znajdzie go przez `find_package(Catch2)` dzięki standardowym env vars z `mkShell`.

### `CMakeLists.txt` (root)

Dodać:
- `enable_testing()` — włącza CTest.
- `add_subdirectory(tests)` — testy jako osobny target.

### `tests/CMakeLists.txt`

- `find_package(Catch2 REQUIRED)` — wymaga Catch2 v3.
- `add_executable(moba_sim_tests test_post_mitigation_damage.cpp test_mod_db.cpp test_champion.cpp)`.
- `target_link_libraries(moba_sim_tests PRIVATE moba_sim Catch2::Catch2WithMain)`.
- `Catch2_discover_tests(moba_sim_tests)` — rejestruje każdy `TEST_CASE` w CTest.

### `tests/test_post_mitigation_damage.cpp`

Testy dla `post_mitigation_damage`:
- Armor ≥ 0: redukcja o `100/(100+armor)`. Przypadki: armor=0 (brak redukcji),
  armor=100 (50% damage), armor=200 (33%).
- Armor < 0: amplifikacja. Przypadki: armor=-50 (133% damage), armor=-100 (200%),
  armor=-200 (300%). Poblisko asymptoty: armor=-99 → bardzo duży ale skończony
  mnożnik (sprawdzić, że nie jest inf/nan).
- `raw_damage=0` → 0 niezależnie od armor.

### `tests/test_mod_db.cpp`

Testy dla `ModDB`:
- `add` + `get_mods`: modyfikator trafia na listę.
- `remove(stat, type, source)`: usuwa pierwszy pasujący; niekruszy się gdy nie ma.
- `remove(predicate)`: usuwa wszystkie pasujące; zostawia resztę.
- `replace`: insert lub update istniejącego (po `stat,type,source`).
- `getSumStat`: suma `ModType::Base` dla danego `stat`; filtruje po `stat` i `type`;
  respektuje predykat (np. tylko `source.name == "X"`).
- `getIncStat`: suma `ModType::Inc` (zaczyna od 1.0, dodaje wartości).
- `getMoreStat`: iloczyn `ModType::More` (zaczyna od 1.0, mnoży wartości).
- `getStat`: `sum * inc * more`; kombinacja wszystkich trzech typów.
- Pusta `ModDB`: `getStat` → 0 (bo `getSumStat` = 0, inc = 1, more = 1).
- Kilka modyfikatorów tego samego `stat`+`type` sumuje/łączy się poprawnie.
- Predykat domyślny (brak) = wszystkie modyfikatory.

### `tests/test_champion.cpp`

Testy dla `Champion` i wolnostojącej `evaluateChampion`:
- `getBaseStats`: pusty `mod_db` → wszystkie staty = 0. Z modyfikatorami →
  `getBaseStats()[i] == mod_db.getStat(Stat{i})`.
- `getDeltaStats`: zerowa delta dla identycznych statów; max abs różnica po
  elementach; poprawnie dla statów rosnących i malejących.
- `applyPassives`:
  - Puste `passives` → zwraca `base` (bonus = 0).
  - Jedna pasywka dodająca stały bonus → `result == base + bonus`.
  - Dwie pasywki niezależne → `result == base + bonus1 + bonus2` (sumowanie,
    nie nadpisywanie — regresja na bug).
  - Kolejność pasywek nie ma znaczenia (niezależność).
  - `const`: wywołanie na `const Champion&` kompiluje się (test compile-time,
    ewentualnie przez `static_assert` lub po prostu wywołanie w teście).
- `evaluateChampion`:
  - Puste pasywki → zwraca `base` (jedna iteracja, delta=0).
  - Pasywka zależna od `final` (np. bonus AD = 1% finalnego AD) → zbiega do
    punktu stałego; wynik stabilny między iteracjami (delta < eps).
  - `eps` respektowane: mniejsze `eps` → więcej iteracji, większa precyzja.
  - Pasywki niezależne dodające stałe bonusy → zbiega w jednej iteracji
    (bo bonusy nie zależą od `final`).

## Poza zakresem

- Benchmarki wydajności.
- Testy mutacyjne / property-based.
- Integracja z CI (poza CTest lokalnie).
- Naprawa literówki `resistanse` (kosmetyka, osobny temat).