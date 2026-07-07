# Integration Scenarios Test Suite Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 6 integration scenario tests that compose Champion + mod_db + passives + evaluateChampion in real pipelines, verifying cross-component interactions.

**Architecture:** New test file `tests/test_scenarios.cpp` with one TEST_CASE per scenario. Each builds a full Champion, configures mod_db and passives, calls evaluateChampion, and asserts final values + convergence behavior. Added to existing `moba_sim_tests` executable.

**Tech Stack:** C++23, CMake, Catch2 v3, CTest, Nix flake.

---

## File Structure

- Modify: `tests/CMakeLists.txt` — add `test_scenarios.cpp` to source list.
- Create: `tests/test_scenarios.cpp` — 6 integration scenario TEST_CASEs.

---

### Task 1: Add scenarios test file and wire CMake

**Files:**
- Modify: `tests/CMakeLists.txt:4-8`
- Create: `tests/test_scenarios.cpp`

- [ ] **Step 1: Create placeholder `tests/test_scenarios.cpp`**

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "moba_sim.hpp"

using moba::Champion;
using moba::ModType;
using moba::Source;
using moba::Stat;
using Stats = Champion::Stats;
```

- [ ] **Step 2: Add `test_scenarios.cpp` to `tests/CMakeLists.txt`**

In `tests/CMakeLists.txt`, the `add_executable` block (lines 4-8) currently reads:

```cmake
add_executable(moba_sim_tests
    test_post_mitigation_damage.cpp
    test_mod_db.cpp
    test_champion.cpp
)
```

Change it to:

```cmake
add_executable(moba_sim_tests
    test_post_mitigation_damage.cpp
    test_mod_db.cpp
    test_champion.cpp
    test_scenarios.cpp
)
```

- [ ] **Step 3: Reconfigure and build**

```bash
cmake -B build
cmake --build build
```

Expected: builds cleanly (placeholder file compiles, no TEST_CASE yet).

- [ ] **Step 4: Verify CTest still sees existing tests**

```bash
ctest --test-dir build -N
```

Expected: `Total Tests: 31` (existing tests unchanged).

- [ ] **Step 5: Commit**

```bash
git add tests/CMakeLists.txt tests/test_scenarios.cpp
git commit -m "build: wire test_scenarios.cpp into moba_sim_tests"
```

---

### Task 2: Write 6 integration scenarios

**Files:**
- Modify: `tests/test_scenarios.cpp`

- [ ] **Step 1: Write all 6 scenarios**

Replace the contents of `tests/test_scenarios.cpp` with:

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "moba_sim.hpp"

using moba::Champion;
using moba::ModType;
using moba::Source;
using moba::Stat;
using Stats = Champion::Stats;

TEST_CASE("Scenario: no passives, mod_db only returns getBaseStats",
          "[scenario]") {
  Champion champ;
  Source src{"Base", ""};
  champ.mod_db.add(Stat::AD, ModType::Base, 80.0, src);
  champ.mod_db.add(Stat::AD, ModType::Inc, 0.2, src);
  champ.mod_db.add(Stat::AD, ModType::More, 1.5, src);
  // getBaseStats[AD] = 80 * 1.2 * 1.5 = 144
  Stats base = champ.getBaseStats();
  REQUIRE(base[std::to_underlying(Stat::AD)] == Catch::Approx(144.0));
  Stats result = moba::evaluateChampion(champ);
  // No passives → result == base, converges in one iteration
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(144.0));
}

TEST_CASE("Scenario: final-dependent passive converges to fixed point",
          "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.passives.push_back([](const Stats &, const Stats &final) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = final[std::to_underlying(Stat::AD)] * 0.1;
    return bonus;
  });
  // Fixed point: final = 50 + 0.1*final → final = 50/0.9 ≈ 55.5556
  Stats result = moba::evaluateChampion(champ, 0.0001);
  REQUIRE(result[std::to_underlying(Stat::AD)] ==
          Catch::Approx(55.5556).epsilon(0.001));
  // Verify it's not just base
  REQUIRE(result[std::to_underlying(Stat::AD)] != Catch::Approx(50.0).epsilon(0.001));
}

TEST_CASE("Scenario: cancelling passives return base", "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::AP, ModType::Base, 300.0, Source{"Base", ""});
  champ.passives.push_back([](const Stats &, const Stats &final) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AP)] = final[std::to_underlying(Stat::AP)] * 0.1;
    return bonus;
  });
  champ.passives.push_back([](const Stats &, const Stats &final) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AP)] = final[std::to_underlying(Stat::AP)] * -0.1;
    return bonus;
  });
  // +0.1*final and -0.1*final cancel → bonus = 0 → result = base = 300
  Stats result = moba::evaluateChampion(champ);
  REQUIRE(result[std::to_underlying(Stat::AP)] == Catch::Approx(300.0));
}

TEST_CASE("Scenario: uneven weights reach non-base fixed point", "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::AP, ModType::Base, 300.0, Source{"Base", ""});
  champ.passives.push_back([](const Stats &, const Stats &final) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AP)] = final[std::to_underlying(Stat::AP)] * 0.2;
    return bonus;
  });
  champ.passives.push_back([](const Stats &, const Stats &final) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AP)] = final[std::to_underlying(Stat::AP)] * -0.1;
    return bonus;
  });
  // Net weight +0.1: final = 300 + 0.1*final → final = 300/0.9 ≈ 333.3333
  Stats result = moba::evaluateChampion(champ, 0.0001);
  REQUIRE(result[std::to_underlying(Stat::AP)] ==
          Catch::Approx(333.3333).epsilon(0.001));
  // Verify it's not base
  REQUIRE(result[std::to_underlying(Stat::AP)] != Catch::Approx(300.0).epsilon(0.001));
}

TEST_CASE("Scenario: cross-stat dependency bonus from one stat to another",
          "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::MaxHP, ModType::Base, 1000.0, Source{"Base", ""});
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.passives.push_back([](const Stats &, const Stats &final) {
    Stats bonus{};
    // AD bonus = 1% of final HP
    bonus[std::to_underlying(Stat::AD)] =
        final[std::to_underlying(Stat::MaxHP)] * 0.01;
    return bonus;
  });
  // HP unchanged (passive doesn't touch HP) → final[HP] = 1000
  // AD gets +0.01*1000 = 10 each iteration, but base AD = 50
  // Iter 1: final = base + bonus = {HP:1000, AD:50} + {AD:10} = {HP:1000, AD:60}
  // Iter 2: final = base + bonus(final) = {HP:1000, AD:50} + {AD:0.01*1000=10} = {HP:1000, AD:60}
  // delta = 0 → converges, final[AD] = 60
  Stats result = moba::evaluateChampion(champ, 0.0001);
  REQUIRE(result[std::to_underlying(Stat::MaxHP)] == Catch::Approx(1000.0));
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
}

TEST_CASE("Scenario: full pipeline ModDB + passives", "[scenario]") {
  Champion champ;
  Source src{"Item", ""};
  champ.mod_db.add(Stat::AD, ModType::Base, 80.0, src);
  champ.mod_db.add(Stat::AD, ModType::Inc, 0.2, src);
  champ.mod_db.add(Stat::AD, ModType::More, 1.5, src);
  // getBaseStats[AD] = 80 * (1+0.2) * 1.5 = 80 * 1.2 * 1.5 = 144
  champ.passives.push_back([](const Stats &, const Stats &final) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = final[std::to_underlying(Stat::AD)] * 0.1;
    return bonus;
  });
  // Fixed point: final = 144 + 0.1*final → final = 144/0.9 = 160
  Stats base = champ.getBaseStats();
  REQUIRE(base[std::to_underlying(Stat::AD)] == Catch::Approx(144.0));
  Stats result = moba::evaluateChampion(champ, 0.0001);
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(160.0).epsilon(0.001));
}
```

- [ ] **Step 2: Build and run scenario tests**

```bash
cmake --build build
ctest --test-dir build -R scenario --verbose
```

Expected: all 6 scenario tests PASS.

- [ ] **Step 3: Run the full test suite**

```bash
ctest --test-dir build
```

Expected: all 37 tests pass (31 existing + 6 new).

- [ ] **Step 4: Commit**

```bash
git add tests/test_scenarios.cpp
git commit -m "test: add integration scenarios for Champion pipeline"
```