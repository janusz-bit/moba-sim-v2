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
  champ.mod_db.add(Stat::HP, ModType::Base, 1000.0, Source{"Base", ""});
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.passives.push_back([](const Stats &, const Stats &final) {
    Stats bonus{};
    // AD bonus = 1% of final HP
    bonus[std::to_underlying(Stat::AD)] =
        final[std::to_underlying(Stat::HP)] * 0.01;
    return bonus;
  });
  // HP unchanged (passive doesn't touch HP) → final[HP] = 1000
  // AD gets +0.01*1000 = 10 each iteration, but base AD = 50
  // Iter 1: final = base + bonus = {HP:1000, AD:50} + {AD:10} = {HP:1000, AD:60}
  // Iter 2: final = base + bonus(final) = {HP:1000, AD:50} + {AD:0.01*1000=10} = {HP:1000, AD:60}
  // delta = 0 → converges, final[AD] = 60
  Stats result = moba::evaluateChampion(champ, 0.0001);
  REQUIRE(result[std::to_underlying(Stat::HP)] == Catch::Approx(1000.0));
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