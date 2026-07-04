# Design: Integration Scenarios Test Suite

## Problem

Obecne testy (31 przypadków) są jednostkowe — testują każdy komponent osobno.
Brakuje testów integracyjnych, które składają `Champion` z `mod_db` + `passives`
i weryfikują `evaluateChampion` w realnych scenariuszach. Te testy łapią interakcje
między komponentami (np. `getBaseStats` jako baza iteracji, pasywki widzące
`final` z poprzedniej iteracji), których jednostkowe testy nie widzą.

## Podejście

Nowy plik `tests/test_scenarios.cpp` z 6 scenariuszami integracyjnymi. Każdy
`TEST_CASE` buduje pełnego `Champion` z `mod_db` i `passives`, wywołuje
`evaluateChampion`, i weryfikuje wynik + zbieżność. Bez mocków — realny pipeline.

## Zmiany

### `tests/CMakeLists.txt`

Dodać `test_scenarios.cpp` do listy źródeł w `add_executable(moba_sim_tests ...)`.

### `tests/test_scenarios.cpp` (nowy)

6 scenariuszy:

1. **Brak pasywek, tylko mod_db** — Champion z Base/Inc/More modyfikatorami.
   `evaluateChampion` zwraca dokładnie `getBaseStats` (delta=0 po pierwszej
   iteracji). Weryfikacja: `result == getBaseStats()`.

2. **Pasywka zależna od `final`, zbiegająca do punktu stałego** — `mod_db` daje
   `base[AD]=50`, pasywka `bonus[AD] = 0.1*final[AD]`. Fixed point: `50/0.9 ≈
   55.5556`. Weryfikacja wartości (z tolerancją) i że `result != base`.

3. **Pasywki znoszące się** — `base[AP]=300`, dwie pasywki `+0.1*final[AP]` i
   `-0.1*final[AP]`. Wynik = `base` (bonusy sumują się do 0). Weryfikacja:
   `result[AP] == 300`, zbiega w jednej iteracji (delta=0).

4. **Nierównowaga wag** — `base[AP]=300`, `+0.2*final` i `-0.1*final`. Fixed
   point: `300/0.9 ≈ 333.3333`. Weryfikacja: `result[AP] ≈ 333.33` i `result[AP]
   != base` (interakcja zmienia wynik).

5. **Cross-stat dependency** — `base[HP]=1000`, `base[AD]=50`. Pasywka:
   `bonus[AD] = 0.01 * final[HP]` (AD rośnie od HP). HP się nie zmienia (pasywka
   nie dotyka HP), więc `final[HP] = 1000` (stałe). AD rośnie o stały `0.01*1000
   = 10`, więc `final[AD] = 50+10 = 60` w jednej iteracji (bo HP stałe między
   iteracjami). Weryfikacja: `final[AD] ≈ 60`, `final[HP] == 1000`.

6. **Pełny pipeline ModDB + passives** — `mod_db` z `Base=80, Inc=0.2, More=1.5`
   dla AD (→ `getBaseStats[AD] = 80*1.2*1.5 = 144`). Pasywka `bonus[AD] =
   0.1*final[AD]`. Fixed point: `144/0.9 = 160`. Weryfikacja: `result[AD] ≈ 160`,
   potwierdza że `evaluateChampion` używa `getBaseStats()` (nie ręcznego `base`)
   jako bazy iteracji.

## Kontrakty weryfikowane przez scenariusze

- `evaluateChampion` używa `mod_db.getBaseStats()` jako bazy iteracji (nie
  ręcznego `base` podanego przez wywołującego).
- Pasywki widzą `final` z poprzedniej iteracji, nie z poprzedniej pasywki
  (niezależność w obrębie jednego `applyPassives`).
- Zbieżność działa przy zerowych i dodatnich wagach netto.
- Cross-stat dependency: pasywka może czytać jeden stat z `final` i dawać
  bonus do innego statu.

## Poza zakresem

- Testy niezbieżności (wagi |netto| ≥ 1) — wymagałoby guarda, którego nie ma.
- Property-based testing (GENERATE).
- Benchmarki wydajności.