#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

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
  champ.mod_db.add(Stat::HP, ModType::Base, 500.0, Source{"Base", ""});
  Stats base = champ.getBaseStats();
  REQUIRE(base[std::to_underlying(Stat::AD)] == Catch::Approx(50.0));
  REQUIRE(base[std::to_underlying(Stat::HP)] == Catch::Approx(500.0));
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
  a[std::to_underlying(Stat::HP)] = 500.0;
  b[std::to_underlying(Stat::HP)] = 450.0;
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