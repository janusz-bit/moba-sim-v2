# Convergence Guard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `max_iter` parameter and `ConvergenceError` exception to `evaluateChampion` so non-converging passives fail loudly instead of looping forever.

**Architecture:** New `ConvergenceError` class in `moba` namespace (header). `evaluateChampion` gains `max_iter` parameter with default 1000; loop condition extended to also stop at iteration cap; post-loop check throws `ConvergenceError` if delta still exceeds eps. One new scenario test verifies the exception.

**Tech Stack:** C++23, CMake, Catch2 v3, CTest, Nix flake.

---

## File Structure

- Modify: `include/moba_sim.hpp` — add `<stdexcept>` include, `ConvergenceError` class, change `evaluateChampion` signature.
- Modify: `src/moba_sim.cpp` — update `evaluateChampion` definition with iteration counter and exception.
- Modify: `tests/test_scenarios.cpp` — add non-convergence test case.

---

### Task 1: Header — add ConvergenceError and max_iter parameter

**Files:**
- Modify: `include/moba_sim.hpp`

- [ ] **Step 1: Add `<stdexcept>` include**

The current includes block (lines 1-9):

```cpp
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>
```

Change to:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
```

Note: `<cstddef>` is re-added (needed for `std::size_t` in the new parameter).

- [ ] **Step 2: Add ConvergenceError class before evaluateChampion declaration**

The current end of file (around line 95-99):

```cpp
};

[[nodiscard]] Champion::Stats evaluateChampion(const Champion &champion,
                                               Type eps = 0.01);
```

Change to:

```cpp
};

class ConvergenceError : public std::runtime_error {
public:
  explicit ConvergenceError(const std::string &msg) : std::runtime_error(msg) {}
};

[[nodiscard]] Champion::Stats evaluateChampion(const Champion &champion,
                                               Type eps = 0.01,
                                               std::size_t max_iter = 1000);
```

- [ ] **Step 3: Verify build**

```bash
cmake --build build
```

Expected: FAIL — `src/moba_sim.cpp` still has the old `evaluateChampion` signature without `max_iter`, causing a signature mismatch error. This is expected; fixed in Task 2.

- [ ] **Step 4: Do not commit yet**

Continue to Task 2 to keep header/cpp consistent in one commit.

---

### Task 2: Implementation — iteration counter and exception

**Files:**
- Modify: `src/moba_sim.cpp`

- [ ] **Step 1: Update evaluateChampion definition**

The current definition (around line 139-148):

```cpp
Champion::Stats evaluateChampion(const Champion &champion, Type eps) {
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

Replace with:

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

Note: The loop condition `iter < max_iter` ensures we stop at the cap. The post-loop check `iter >= max_iter && delta > eps` distinguishes "hit cap while still diverging" from "converged exactly on the last allowed iteration".

- [ ] **Step 2: Verify build**

```bash
cmake --build build
```

Expected: clean build, no warnings.

- [ ] **Step 3: Run existing tests to verify no regression**

```bash
ctest --test-dir build
```

Expected: all 37 tests pass (existing tests use default `max_iter=1000`, which is plenty for convergent cases).

- [ ] **Step 4: Run clang-format and clang-tidy**

```bash
clang-format -style=file -i include/moba_sim.hpp src/moba_sim.cpp
clang-tidy -p build --fix src/moba_sim.cpp include/moba_sim.hpp
```

Re-run build if any fixes applied: `cmake --build build`.

- [ ] **Step 5: Commit header and cpp together**

```bash
git add include/moba_sim.hpp src/moba_sim.cpp
git commit -m "feat(Champion): add max_iter and ConvergenceError to evaluateChampion"
```

---

### Task 3: Test — non-convergence throws ConvergenceError

**Files:**
- Modify: `tests/test_scenarios.cpp`

- [ ] **Step 1: Add non-convergence test case**

Append the following TEST_CASE to the end of `tests/test_scenarios.cpp`:

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

- [ ] **Step 2: Build and run tests**

```bash
cmake --build build
ctest --test-dir build --verbose
```

Expected: all 38 tests pass (37 existing + 1 new). The new test verifies that `ConvergenceError` is thrown when `max_iter=5` is exceeded.

- [ ] **Step 3: Commit**

```bash
git add tests/test_scenarios.cpp
git commit -m "test: add non-convergence scenario for evaluateChampion"
```