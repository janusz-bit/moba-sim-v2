# applyPassives Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix `Champion::applyPassives` bug (passives overwrite each other) and extract convergence loop into a free function `evaluateChampion(const Champion &, Type eps)`.

**Architecture:** `applyPassives` becomes a single step: sum independent passive bonuses (each computed from the same `final`) and add to `base`. The free function `evaluateChampion` iterates `final = applyPassives(base, final)` until `getDeltaStats(final, prev) <= eps`.

**Tech Stack:** C++23, CMake, clang-format/clang-tidy (pre-commit). No test framework present — verification via build.

---

## File Structure

- Modify: `include/moba_sim.hpp` — drop member `evaluateChampion`, mark `applyPassives const`, add `Passive` contract comment, declare free `evaluateChampion`.
- Modify: `src/moba_sim.cpp` — add `addStats` helper in anon namespace, rewrite `applyPassives`, drop member `evaluateChampion`, add free `evaluateChampion`.

---

### Task 1: Header changes

**Files:**
- Modify: `include/moba_sim.hpp:79-94`

- [ ] **Step 1: Edit `include/moba_sim.hpp`**

Replace the `Champion` struct and trailing free-function declaration. Current block (lines 79-94):

```cpp
struct Champion {
  using Stats = std::array<Type, std::to_underlying(Stat::Count)>;
  using Passive = std::function<Stats(const Stats &base, const Stats &final)>;
  using Passives = std::vector<Passive>;
  ModDB mod_db;
  Passives passives;

  [[nodiscard]] Stats getBaseStats() const;

  Stats applyPassives(const Stats &base, const Stats &final);

  [[nodiscard]] static Type getDeltaStats(const Stats &stats1,
                                          const Stats &stats2);

  Stats evaluateChampion();
};
```

New block:

```cpp
struct Champion {
  using Stats = std::array<Type, std::to_underlying(Stat::Count)>;
  // Passive returns a *bonus* (delta): only stats it adds, others = 0.
  // Computed from (base, final); passives are independent within one
  // applyPassives call (order does not matter).
  using Passive = std::function<Stats(const Stats &base, const Stats &final)>;
  using Passives = std::vector<Passive>;
  ModDB mod_db;
  Passives passives;

  [[nodiscard]] Stats getBaseStats() const;

  [[nodiscard]] Stats applyPassives(const Stats &base,
                                    const Stats &final) const;

  [[nodiscard]] static Type getDeltaStats(const Stats &stats1,
                                          const Stats &stats2);
};

[[nodiscard]] Stats evaluateChampion(const Champion &champion, Type eps = 0.01);
```

- [ ] **Step 2: Verify build**

Run: `cmake --build build`
Expected: builds cleanly (header-only change; `.cpp` will fail to link the dropped member next, but should still compile — if it errors on the missing `Champion::evaluateChampion` definition, that's expected and fixed in Task 3).

- [ ] **Step 3: Commit**

```bash
git add include/moba_sim.hpp
git commit -m "refactor(Champion): mark applyPassives const, drop member evaluateChampion, declare free evaluateChampion"
```

---

### Task 2: Rewrite `applyPassives` and add `addStats` helper

**Files:**
- Modify: `src/moba_sim.cpp:3` (insert anon-namespace helper), `src/moba_sim.cpp:112-118`

- [ ] **Step 1: Add `addStats` helper in anonymous namespace**

Insert at top of `src/moba_sim.cpp`, right after `#include "moba_sim.hpp"` and the `<algorithm>` include, inside `namespace moba {` before any definitions:

```cpp
namespace {
Champion::Stats addStats(const Champion::Stats &a, const Champion::Stats &b) {
  Champion::Stats out{};
  for (std::size_t i = 0; i < std::to_underlying(Stat::Count); ++i)
    out[i] = a[i] + b[i];
  return out;
}
} // namespace
```

Note: `Champion::Stats` is a public nested type, accessible from within `namespace moba`.

- [ ] **Step 2: Rewrite `Champion::applyPassives`**

Replace the current implementation (src/moba_sim.cpp:112-118):

```cpp
Champion::Stats Champion::applyPassives(const Stats &base, const Stats &final) {
  Stats result = final;
  for (const auto &passive : passives) {
    result = passive(base, final);
  }
  return result;
}
```

with:

```cpp
Champion::Stats Champion::applyPassives(const Stats &base,
                                         const Stats &final) const {
  Stats bonus{};
  for (const auto &passive : passives)
    bonus = addStats(bonus, passive(base, final));
  return addStats(base, bonus);
}
```

- [ ] **Step 3: Verify build**

Run: `cmake --build build`
Expected: FAIL — `Champion::evaluateChampion` still defined in the .cpp (Task 3 removes it) but no longer declared in header. Error like "no declaration matches" or "no member named 'evaluateChampion'". If instead it complains only about the free `evaluateChampion` being declared but not defined, proceed to Task 3.

- [ ] **Step 4: Do not commit yet**

Continue to Task 3 to keep the header/cpp consistent in one commit.

---

### Task 3: Replace member `evaluateChampion` with free function

**Files:**
- Modify: `src/moba_sim.cpp:128-142`

- [ ] **Step 1: Remove member `evaluateChampion`**

Delete the current member definition (src/moba_sim.cpp:128-142):

```cpp
Champion::Stats Champion::evaluateChampion() {
  Stats stats = getBaseStats();
  const Stats base = stats;
  Stats final = stats;
  final = applyPassives(base, final);

  Type delta = getDeltaStats(base, final);
  Stats final_now = final;
  while (delta > 0.01) {
    final_now = applyPassives(base, final_now);
    delta = getDeltaStats(final_now, final);
    final = final_now;
  }
  return final;
}
```

- [ ] **Step 2: Add free `evaluateChampion`**

In its place, add:

```cpp
Stats evaluateChampion(const Champion &champion, Type eps) {
  const Champion::Stats base = champion.getBaseStats();
  Champion::Stats final = base;
  Champion::Stats prev = base;
  do {
    prev = final;
    final = champion.applyPassives(base, prev);
  } while (Champion::getDeltaStats(final, prev) > eps);
  return final;
}
```

- [ ] **Step 3: Verify build**

Run: `cmake --build build`
Expected: clean build, no warnings.

- [ ] **Step 4: Run clang-format and clang-tidy on changed files**

```bash
clang-format -style=file -i include/moba_sim.hpp src/moba_sim.cpp
clang-tidy -p build --fix src/moba_sim.cpp include/moba_sim.hpp
```

Re-run build if any fixes applied: `cmake --build build`.

- [ ] **Step 5: Commit**

```bash
git add src/moba_sim.cpp
git commit -m "refactor(Champion): sum independent passives, extract convergence loop to free evaluateChampion"
```