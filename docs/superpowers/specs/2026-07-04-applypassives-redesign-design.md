# Design: `applyPassives` redesign

## Problem

`Champion::applyPassives` (src/moba_sim.cpp:112) ma bug: każda pasywka nadpisuje
wynik poprzedniej:

```cpp
for (const auto &passive : passives) {
    result = passive(base, final);   // nadpisuje result
}
```

Przy wielu pasywkach zostaje tylko efekt ostatniej. Dodatkowo `evaluateChampion`
jest zaszyta w klasie jako członek, a użytkownik chce sam kontrolować pętlę
konwergencji.

## Kontrakt

- `Passive = std::function<Stats(const Stats &base, const Stats &final)>`
  zwraca **bonus** (delta) — tylko staty, które ta pasywka dodaje; reszta = 0.
- Pasywki są **niezależne** w obrębie jednego wywołania `applyPassives`: każda
  liczy swój bonus od tego samego `final`. Kolejność pasywek nie ma znaczenia.
- Konwergencja do punktu stałego jest poza `applyPassives` — wywołujący iteruje.

## Zmiany

### `include/moba_sim.hpp`

1. Usunąć deklarację `Stats evaluateChampion();` z `struct Champion`.
2. Oznaczyć `applyPassives` jako `const`.
3. Dodać komentarz dokumentujący kontrakt `Passive` przy definicji typu alias.
4. Dodać wolnostojącą deklarację w przestrzeni `moba`:
   `[[nodiscard]] Stats evaluateChampion(const Champion &champion, Type eps = 0.01);`

### `src/moba_sim.cpp`

1. Dodać helper `addStats(const Stats &a, const Stats &b)` — element-wise `+=`,
   zdefiniowany w anonimowej przestrzeni nazw w cpp.
2. Przepisać `Champion::applyPassives` na:
   ```cpp
   Stats Champion::applyPassives(const Stats &base, const Stats &final) const {
     Stats bonus{};
     for (const auto &passive : passives)
       bonus = addStats(bonus, passive(base, final));
     return addStats(base, bonus);
   }
   ```
3. Usunąć implementację `Champion::evaluateChampion`.
4. Dodać implementację wolnostojącej `evaluateChampion(const Champion &champion,
   Type eps)`:
   ```cpp
   Stats evaluateChampion(const Champion &champion, Type eps) {
     const Stats base = champion.getBaseStats();
     Stats final = base;
     Stats prev = final;
     do {
       prev = final;
       final = champion.applyPassives(base, prev);
     } while (Champion::getDeltaStats(final, prev) > eps);
     return final;
   }
   ```

## Poza zakresem

- Testy jednostkowe (brak w projekcie — osobny temat).
- Literówka `resistanse` w `post_mitigation_damage` (kosmetyka, publiczne API,
  nie dotyczy tej zmiany).
- Brak `#include <array>` w headerze (kosmetyka, nie dotyczy tej zmiany).