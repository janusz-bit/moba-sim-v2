# Test Suite Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Catch2 v3 test suite with full coverage of `moba_sim` (post_mitigation_damage, ModDB, Champion, evaluateChampion).

**Architecture:** Catch2 v3 from nixpkgs, wired via CMake `find_package`. Tests in `tests/` directory, registered with CTest via `Catch2_discover_tests`. Three test files split by component.

**Tech Stack:** C++23, CMake, Catch2 v3, Nix flake, CTest.

---

## File Structure

- Modify: `nix/default.nix` — add `catch2_3` to devShell packages.
- Modify: `CMakeLists.txt` — `enable_testing()` + `add_subdirectory(tests)`.
- Create: `tests/CMakeLists.txt` — test executable + Catch2 discovery.
- Create: `tests/test_post_mitigation_damage.cpp` — armor reduction formula tests.
- Create: `tests/test_mod_db.cpp` — ModDB CRUD + stat aggregation tests.
- Create: `tests/test_champion.cpp` — Champion + applyPassives + evaluateChampion tests.

**Note on environment:** After adding `catch2_3` to the flake, you must re-enter the nix shell (`nix develop`) so CMake can find Catch2 via `find_package`. All build commands assume you are inside `nix develop`.

**Note on existing code:** The implementation already exists. Tests verify current behavior. If a test fails, it found a bug — report it as DONE_WITH_CONCERNS rather than "fixing" the test to match wrong behavior.

---

### Task 1: Nix + CMake setup for Catch2

**Files:**
- Modify: `nix/default.nix:33-43`
- Modify: `CMakeLists.txt:15`
- Create: `tests/CMakeLists.txt`

- [ ] **Step 1: Add `catch2_3` to the nix devShell**

In `nix/default.nix`, the `packages` list (lines 33-43) currently reads:

```nix
        packages =
          config.pre-commit.settings.enabledPackages
          ++ (with pkgs; [
            # add packages to use in shell
            cmake
            ninja
            clang
            pkgsCross.mingwW64.buildPackages.gcc
            wine64
            clang-tools
            lldb
            boost
          ]);
```

Change it to:

```nix
        packages =
          config.pre-commit.settings.enabledPackages
          ++ (with pkgs; [
            # add packages to use in shell
            cmake
            ninja
            clang
            pkgsCross.mingwW64.buildPackages.gcc
            wine64
            clang-tools
            lldb
            boost
            catch2_3
          ]);
```

- [ ] **Step 2: Enable testing and add tests subdirectory in root CMakeLists.txt**

In `CMakeLists.txt`, after the line `add_subdirectory(src)` (line 15), append:

```cmake
enable_testing()
add_subdirectory(tests)
```

- [ ] **Step 3: Create `tests/CMakeLists.txt`**

```cmake
find_package(Catch2 3 REQUIRED)

add_executable(moba_sim_tests
    test_post_mitigation_damage.cpp
    test_mod_db.cpp
    test_champion.cpp
)

target_link_libraries(moba_sim_tests
    PRIVATE
        moba_sim
        Catch2::Catch2WithMain
)

Catch2_discover_tests(moba_sim_tests)
```

- [ ] **Step 4: Re-enter nix shell and reconfigure CMake**

Exit the current shell if you're in one, then:

```bash
nix develop
cmake -B build
```

Expected: CMake configures successfully, finds Catch2 3. Output should mention "Found Catch2: ...".

- [ ] **Step 5: Create placeholder test files so the build doesn't fail**

Create `tests/test_post_mitigation_damage.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
```

Create `tests/test_mod_db.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
```

Create `tests/test_champion.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
```

- [ ] **Step 6: Verify build**

Run: `cmake --build build`
Expected: builds the `moba_sim_tests` target (no test cases yet, but compiles).

- [ ] **Step 7: Verify CTest sees the target**

Run: `ctest --test-dir build --list-tests`
Expected: lists 0 tests or "No tests found" (no TEST_CASE blocks yet). This confirms CTest is wired up.

- [ ] **Step 8: Commit**

```bash
git add nix/default.nix CMakeLists.txt tests/CMakeLists.txt tests/test_post_mitigation_damage.cpp tests/test_mod_db.cpp tests/test_champion.cpp
git commit -m "build: add Catch2 v3 test infrastructure"
```

---

### Task 2: Tests for `post_mitigation_damage`

**Files:**
- Modify: `tests/test_post_mitigation_damage.cpp`

The function signature (from `include/moba_sim.hpp:16-17`):

```cpp
Type post_mitigation_damage(const Type &raw_damage,
                            const Type &resistanse) noexcept;
```

`Type` is `double`. The formula:
- armor ≥ 0: `raw * 100 / (100 + armor)`
- armor < 0: `raw * (2 - 100 / (100 - armor))`

- [ ] **Step 1: Write the test file**

Replace the contents of `tests/test_post_mitigation_damage.cpp` with:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "moba_sim.hpp"

using moba::post_mitigation_damage;
using moba::Type;

TEST_CASE("post_mitigation_damage with zero armor returns raw damage",
          "[post_mitigation_damage]") {
  REQUIRE(post_mitigation_damage(100.0, 0.0) == Catch::Approx(100.0));
}

TEST_CASE("post_mitigation_damage with positive armor reduces damage",
          "[post_mitigation_damage]") {
  REQUIRE(post_mitigation_damage(100.0, 100.0) == Catch::Approx(50.0));
  REQUIRE(post_mitigation_damage(100.0, 200.0) == Catch::Approx(33.3333).epsilon(0.01));
  REQUIRE(post_mitigation_damage(100.0, 300.0) == Catch::Approx(25.0));
}

TEST_CASE("post_mitigation_damage with negative armor amplifies damage",
          "[post_mitigation_damage]") {
  REQUIRE(post_mitigation_damage(100.0, -50.0) == Catch::Approx(133.3333).epsilon(0.01));
  REQUIRE(post_mitigation_damage(100.0, -100.0) == Catch::Approx(150.0));
  REQUIRE(post_mitigation_damage(100.0, -200.0) == Catch::Approx(166.6667).epsilon(0.01));
}

TEST_CASE("post_mitigation_damage approaches 200% for very negative armor",
          "[post_mitigation_damage]") {
  Type result = post_mitigation_damage(100.0, -10000.0);
  REQUIRE(std::isfinite(result));
  REQUIRE(result == Catch::Approx(199.0).epsilon(0.01));
}

TEST_CASE("post_mitigation_damage with zero raw damage returns zero",
          "[post_mitigation_damage]") {
  REQUIRE(post_mitigation_damage(0.0, 100.0) == 0.0);
  REQUIRE(post_mitigation_damage(0.0, -50.0) == 0.0);
  REQUIRE(post_mitigation_damage(0.0, 0.0) == 0.0);
}
```

- [ ] **Step 2: Build and run tests**

```bash
cmake --build build
ctest --test-dir build -R post_mitigation_damage --verbose
```

Expected: all 5 test cases PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/test_post_mitigation_damage.cpp
git commit -m "test: add post_mitigation_damage tests"
```

---

### Task 3: Tests for `ModDB`

**Files:**
- Modify: `tests/test_mod_db.cpp`

Key API facts (from `include/moba_sim.hpp:45-77`):
- `ModDB::add(stat, type, value, source)` — appends a modifier.
- `ModDB::remove(stat, type, source)` — erases first match.
- `ModDB::remove(predicate)` — erases all matching.
- `ModDB::replace(stat, type, value, source)` — insert or update by (stat, type, source).
- `getSumStat` sums `ModType::Base` values, starts at 0.0.
- `getIncStat` sums `ModType::Inc` values, starts at 1.0 (stores fractional increases like 0.1).
- `getMoreStat` multiplies `ModType::More` values, starts at 1.0 (stores multipliers like 1.1).
- `getStat` = `getSumStat * getIncStat * getMoreStat`.
- All four getters accept an optional predicate `std::function<bool(const Modifier &)>` defaulting to "match all".
- `Source` has `name`, `description`, and `operator==`.

- [ ] **Step 1: Write the test file**

Replace the contents of `tests/test_mod_db.cpp` with:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "moba_sim.hpp"

using moba::ModDB;
using moba::Modifier;
using moba::ModType;
using moba::Source;
using moba::Stat;
using moba::Type;

TEST_CASE("ModDB add stores modifiers", "[mod_db]") {
  ModDB db;
  Source src{"Item", "desc"};
  db.add(Stat::AD, ModType::Base, 10.0, src);
  REQUIRE(db.get_mods().size() == 1);
  REQUIRE(db.get_mods()[0].stat == Stat::AD);
  REQUIRE(db.get_mods()[0].type == ModType::Base);
  REQUIRE(db.get_mods()[0].value == 10.0);
  REQUIRE(db.get_mods()[0].source == src);
}

TEST_CASE("ModDB remove by stat/type/source erases first match", "[mod_db]") {
  ModDB db;
  Source src{"Item", "desc"};
  db.add(Stat::AD, ModType::Base, 10.0, src);
  db.add(Stat::MaxHP, ModType::Base, 20.0, src);
  db.remove(Stat::AD, ModType::Base, src);
  REQUIRE(db.get_mods().size() == 1);
  REQUIRE(db.get_mods()[0].stat == Stat::MaxHP);
}

TEST_CASE("ModDB remove by stat/type/source is no-op when not found", "[mod_db]") {
  ModDB db;
  Source src{"Item", "desc"};
  db.add(Stat::AD, ModType::Base, 10.0, src);
  db.remove(Stat::MaxHP, ModType::Base, src);
  REQUIRE(db.get_mods().size() == 1);
}

TEST_CASE("ModDB remove by predicate erases all matching", "[mod_db]") {
  ModDB db;
  Source src1{"Item", "desc"};
  Source src2{"Buff", "desc"};
  db.add(Stat::AD, ModType::Base, 10.0, src1);
  db.add(Stat::AD, ModType::Base, 20.0, src2);
  db.add(Stat::MaxHP, ModType::Base, 30.0, src1);
  db.remove([](const Modifier &m) { return m.stat == Stat::AD; });
  REQUIRE(db.get_mods().size() == 1);
  REQUIRE(db.get_mods()[0].stat == Stat::MaxHP);
}

TEST_CASE("ModDB replace inserts when not present", "[mod_db]") {
  ModDB db;
  Source src{"Item", "desc"};
  db.replace(Stat::AD, ModType::Base, 10.0, src);
  REQUIRE(db.get_mods().size() == 1);
  REQUIRE(db.get_mods()[0].value == 10.0);
}

TEST_CASE("ModDB replace updates value when present", "[mod_db]") {
  ModDB db;
  Source src{"Item", "desc"};
  db.replace(Stat::AD, ModType::Base, 10.0, src);
  db.replace(Stat::AD, ModType::Base, 25.0, src);
  REQUIRE(db.get_mods().size() == 1);
  REQUIRE(db.get_mods()[0].value == 25.0);
}

TEST_CASE("ModDB getSumStat sums Base modifiers for matching stat", "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::Base, 10.0, src);
  db.add(Stat::AD, ModType::Base, 20.0, src);
  db.add(Stat::MaxHP, ModType::Base, 30.0, src);
  db.add(Stat::AD, ModType::Inc, 0.1, src);
  REQUIRE(db.getSumStat(Stat::AD) == Catch::Approx(30.0));
  REQUIRE(db.getSumStat(Stat::MaxHP) == Catch::Approx(30.0));
  REQUIRE(db.getSumStat(Stat::AP) == Catch::Approx(0.0));
}

TEST_CASE("ModDB getIncStat sums Inc modifiers starting from 1.0", "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::Inc, 0.1, src);
  db.add(Stat::AD, ModType::Inc, 0.2, src);
  db.add(Stat::AD, ModType::Inc, 0.3, src);
  REQUIRE(db.getIncStat(Stat::AD) == Catch::Approx(1.6));
}

TEST_CASE("ModDB getMoreStat multiplies More modifiers starting from 1.0",
          "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::More, 1.1, src);
  db.add(Stat::AD, ModType::More, 1.2, src);
  db.add(Stat::AD, ModType::More, 1.3, src);
  REQUIRE(db.getMoreStat(Stat::AD) == Catch::Approx(1.716).epsilon(0.001));
}

TEST_CASE("ModDB getStat combines sum * inc * more", "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::Base, 50.0, src);
  db.add(Stat::AD, ModType::Inc, 0.1, src);
  db.add(Stat::AD, ModType::More, 1.5, src);
  // 50 * 1.1 * 1.5 = 82.5
  REQUIRE(db.getStat(Stat::AD) == Catch::Approx(82.5));
}

TEST_CASE("ModDB empty getStat returns zero", "[mod_db]") {
  ModDB db;
  // getSumStat=0, getIncStat=1, getMoreStat=1 → 0*1*1=0
  REQUIRE(db.getStat(Stat::AD) == Catch::Approx(0.0));
}

TEST_CASE("ModDB getters respect predicate filtering by source", "[mod_db]") {
  ModDB db;
  Source item{"Item", ""};
  Source buff{"Buff", ""};
  db.add(Stat::AD, ModType::Base, 10.0, item);
  db.add(Stat::AD, ModType::Base, 20.0, buff);
  REQUIRE(db.getSumStat(Stat::AD,
                        [](const Modifier &m) { return m.source.name == "Item"; }) ==
          Catch::Approx(10.0));
  REQUIRE(db.getSumStat(Stat::AD,
                        [](const Modifier &m) { return m.source.name == "Buff"; }) ==
          Catch::Approx(20.0));
}

TEST_CASE("ModDB multiple modifiers of same stat and type aggregate", "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::MaxHP, ModType::Base, 100.0, src);
  db.add(Stat::MaxHP, ModType::Base, 200.0, src);
  db.add(Stat::MaxHP, ModType::Inc, 0.5, src);
  db.add(Stat::MaxHP, ModType::More, 2.0, src);
  // sum=300, inc=1.5, more=2.0 → 300*1.5*2.0=900
  REQUIRE(db.getStat(Stat::MaxHP) == Catch::Approx(900.0));
}
```

- [ ] **Step 2: Build and run tests**

```bash
cmake --build build
ctest --test-dir build -R mod_db --verbose
```

Expected: all 13 test cases PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/test_mod_db.cpp
git commit -m "test: add ModDB tests"
```

---

### Task 4: Tests for `Champion` and `evaluateChampion`

**Files:**
- Modify: `tests/test_champion.cpp`

Key API facts (from `include/moba_sim.hpp:79-99` and `src/moba_sim.cpp`):
- `Champion::Stats` = `std::array<double, std::to_underlying(Stat::Count)>`.
- `Champion::Passive` = `std::function<Stats(const Stats &base, const Stats &final)>` — returns a **bonus** (delta).
- `Champion::getBaseStats()` — reads `mod_db.getStat` for each stat index.
- `Champion::applyPassives(base, final)` const — returns `base + Σ passive(base, final)`.
- `Champion::getDeltaStats(a, b)` static — max abs element-wise difference.
- Free function `evaluateChampion(champion, eps=0.01)` — iterates `final = applyPassives(base, prev)` until `getDeltaStats(final, prev) <= eps`.

- [ ] **Step 1: Write the test file**

Replace the contents of `tests/test_champion.cpp` with:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "moba_sim.hpp"

using moba::Champion;
using moba::ModDB;
using moba::ModType;
using moba::Source;
using moba::Stat;
using moba::Type;
using Stats = Champion::Stats;

TEST_CASE("Champion getBaseStats with empty mod_db returns all zeros",
          "[champion]") {
  Champion champ;
  Stats base = champ.getBaseStats();
  for (std::size_t i = 0; i < std::to_underlying(Stat::Count); ++i) {
    REQUIRE(base[i] == Catch::Approx(0.0));
  }
}

TEST_CASE("Champion getBaseStats reads from mod_db", "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.mod_db.add(Stat::MaxHP, ModType::Base, 500.0, Source{"Base", ""});
  Stats base = champ.getBaseStats();
  REQUIRE(base[std::to_underlying(Stat::AD)] == Catch::Approx(50.0));
  REQUIRE(base[std::to_underlying(Stat::MaxHP)] == Catch::Approx(500.0));
  REQUIRE(base[std::to_underlying(Stat::AP)] == Catch::Approx(0.0));
}

TEST_CASE("Champion getDeltaStats is zero for identical stats", "[champion]") {
  Stats a{};
  a[std::to_underlying(Stat::AD)] = 50.0;
  REQUIRE(Champion::getDeltaStats(a, a) == Catch::Approx(0.0));
}

TEST_CASE("Champion getDeltaStats returns max abs element difference",
          "[champion]") {
  Stats a{};
  Stats b{};
  a[std::to_underlying(Stat::AD)] = 50.0;
  b[std::to_underlying(Stat::AD)] = 55.0;
  a[std::to_underlying(Stat::MaxHP)] = 500.0;
  b[std::to_underlying(Stat::MaxHP)] = 450.0;
  // |55-50|=5, |450-500|=50 → max=50
  REQUIRE(Champion::getDeltaStats(a, b) == Catch::Approx(50.0));
}

TEST_CASE("Champion applyPassives with no passives returns base", "[champion]") {
  Champion champ;
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  Stats result = champ.applyPassives(base, base);
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(50.0));
}

TEST_CASE("Champion applyPassives with one passive adds bonus to base",
          "[champion]") {
  Champion champ;
  champ.passives.push_back([](const Stats &, const Stats &) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return bonus;
  });
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  Stats result = champ.applyPassives(base, base);
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
}

TEST_CASE("Champion applyPassives sums multiple independent passives",
          "[champion]") {
  Champion champ;
  champ.passives.push_back([](const Stats &, const Stats &) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return bonus;
  });
  champ.passives.push_back([](const Stats &, const Stats &) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 20.0;
    return bonus;
  });
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  Stats result = champ.applyPassives(base, base);
  // 50 + 10 + 20 = 80
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(80.0));
}

TEST_CASE("Champion applyPassives is order-independent", "[champion]") {
  auto passiveA = [](const Stats &, const Stats &) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return bonus;
  };
  auto passiveB = [](const Stats &, const Stats &) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 20.0;
    return bonus;
  };
  Champion champ1;
  champ1.passives.push_back(passiveA);
  champ1.passives.push_back(passiveB);
  Champion champ2;
  champ2.passives.push_back(passiveB);
  champ2.passives.push_back(passiveA);
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  Stats r1 = champ1.applyPassives(base, base);
  Stats r2 = champ2.applyPassives(base, base);
  REQUIRE(r1[std::to_underlying(Stat::AD)] == Catch::Approx(r2[std::to_underlying(Stat::AD)]));
  REQUIRE(r1[std::to_underlying(Stat::AD)] == Catch::Approx(80.0));
}

TEST_CASE("Champion applyPassives is const-callable", "[champion]") {
  Champion champ;
  champ.passives.push_back([](const Stats &, const Stats &) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return bonus;
  });
  const Champion &const_champ = champ;
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  Stats result = const_champ.applyPassives(base, base);
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
}

TEST_CASE("evaluateChampion with no passives returns base", "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  Stats result = moba::evaluateChampion(champ);
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(50.0));
}

TEST_CASE("evaluateChampion with constant-bonus passive converges in one step",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.passives.push_back([](const Stats &, const Stats &) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return bonus;
  });
  Stats result = moba::evaluateChampion(champ);
  // 50 + 10 = 60, and second iteration: 50 + 10 = 60 (bonus doesn't depend on final)
  // so delta=0 after first step → converges immediately
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
}

TEST_CASE("evaluateChampion converges with final-dependent passive",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  // Bonus AD = 10% of final AD
  champ.passives.push_back([](const Stats &, const Stats &final) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = final[std::to_underlying(Stat::AD)] * 0.1;
    return bonus;
  });
  // Fixed point: final = 50 + 0.1*final → final = 50/0.9 ≈ 55.5556
  Stats result = moba::evaluateChampion(champ, 0.0001);
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(55.5556).epsilon(0.001));
}

TEST_CASE("evaluateChampion respects eps for tighter convergence",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.passives.push_back([](const Stats &, const Stats &final) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = final[std::to_underlying(Stat::AD)] * 0.1;
    return bonus;
  });
  Stats loose = moba::evaluateChampion(champ, 0.1);
  Stats tight = moba::evaluateChampion(champ, 0.00001);
  // Both should be near the fixed point; tight should be closer
  double fixed_point = 50.0 / 0.9;
  REQUIRE(std::abs(tight[std::to_underlying(Stat::AD)] - fixed_point) <=
          std::abs(loose[std::to_underlying(Stat::AD)] - fixed_point));
}
```

- [ ] **Step 2: Build and run tests**

```bash
cmake --build build
ctest --test-dir build -R champion --verbose
```

Expected: all 13 test cases PASS.

- [ ] **Step 3: Run the full test suite**

```bash
ctest --test-dir build --verbose
```

Expected: all tests across all three files PASS (5 + 13 + 13 = 31 test cases).

- [ ] **Step 4: Commit**

```bash
git add tests/test_champion.cpp
git commit -m "test: add Champion and evaluateChampion tests"
```