# Design: Convergence Guard for evaluateChampion

## Problem

`evaluateChampion` iteruje `final = applyPassives(base, final)` w pętli `do-while`
do osiągnięcia punktu stałego (`getDeltaStats(final, prev) <= eps`). Przy wadze
netto |w| ≥ 1 (np. pasywka dająca `bonus = 1.5 * final`) pętla nigdy nie zbiega —
biega w nieskończoność. Brak guardu to ukryty bug dla wywołującego.

## Podejście

Dodać parametr `max_iter` z domyślną wartością 1000 oraz typ wyjątku
`ConvergenceError`. Po przekroczeniu limitu bez zbieżności funkcja rzuca
wyjątek z diagnostycznym komunikatem.

## Zmiany

### `include/moba_sim.hpp`

1. Dodać jawny `#include <stdexcept>` (już dostępny pośrednio, ale jawny include
   utrzymuje porządek).
2. Dodać klasę `ConvergenceError` w przestrzeni `moba`, przed deklaracją
   `evaluateChampion`:

```cpp
class ConvergenceError : public std::runtime_error {
public:
  explicit ConvergenceError(const std::string &msg) : std::runtime_error(msg) {}
};
```

3. Zmienić deklarację `evaluateChampion` dodając `max_iter`:

```cpp
[[nodiscard]] Champion::Stats evaluateChampion(const Champion &champion,
                                               Type eps = 0.01,
                                               std::size_t max_iter = 1000);
```

### `src/moba_sim.cpp`

W `evaluateChampion` dodać licznik iteracji i rzucić wyjątek po przekroczeniu:

```cpp
Champion::Stats evaluateChampion(const Champion &champion, Type eps,
                                  std::size_t max_iter) {
  const Champion::Stats base = champion.getBaseStats();
  Champion::Stats final = base;
  Champion::Stats prev = base;
  std::size_t iter = 0;
  do {
    prev = final;
    final = champion.applyPassives(base, prev);
    ++iter;
  } while (Champion::getDeltaStats(final, prev) > eps && iter < max_iter);
  if (iter >= max_iter && Champion::getDeltaStats(final, prev) > eps) {
    throw ConvergenceError(
        "evaluateChampion did not converge after " + std::to_string(max_iter) +
        " iterations (eps=" + std::to_string(eps) +
        ", delta=" + std::to_string(Champion::getDeltaStats(final, prev)) + ")");
  }
  return final;
}
```

Uwaga: warunek pętli zostaje rozszerzony o `&& iter < max_iter` — pętla kończy
gdy zbieżna ALBO przekroczony limit. Po pętli dodatkowy check upewnia się, że
ostatnia delta > eps (czyli faktycznie niezbiegła, nie że zbiegła akurat na
ostatniej iteracji).

### `tests/test_scenarios.cpp`

Dodać TEST_CASE weryfikujący wyjątek:

```cpp
TEST_CASE("Scenario: non-converging passive throws ConvergenceError",
          "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.passives.push_back([](const Stats &, const Stats &final) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = final[std::to_underlying(Stat::AD)] * 1.5;
    return bonus;
  });
  // Weight 1.5 ≥ 1 → diverges, should throw after max_iter
  REQUIRE_THROWS_AS(moba::evaluateChampion(champ, 0.01, 5),
                    moba::ConvergenceError);
}
```

## Poza zakresem

- Naprawa martwego kodu `TypeDamage`/`KindDamage` (osobny temat).
- Guard w `applyPassives` (jednokrokowa, nie pętla — guard nie dotyczy).
- Testy property-based dla wag granicznych (|w| = 1 dokładnie).