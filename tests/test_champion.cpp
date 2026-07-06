#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "moba_sim.hpp"

using moba::Champion;
using moba::ModType;
using moba::Source;
using moba::Stat;
using Stats = Champion::Stats;
using moba::Type;

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
  champ.passives.push_back([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, true};
  });
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  Stats result = champ.applyPassives(base, base);
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
}

TEST_CASE("Champion applyPassives sums multiple independent passives",
          "[champion]") {
  Champion champ;
  champ.passives.push_back([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, true};
  });
  champ.passives.push_back([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 20.0;
    return Champion::PassiveResult{bonus, true};
  });
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  Stats result = champ.applyPassives(base, base);
  // 50 + 10 + 20 = 80
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(80.0));
}

TEST_CASE("Champion applyPassives is order-independent", "[champion]") {
  auto passiveA = [](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, true};
  };
  auto passiveB = [](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 20.0;
    return Champion::PassiveResult{bonus, true};
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

TEST_CASE("Champion applyPassives is callable", "[champion]") {
  Champion champ;
  champ.passives.push_back([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, true};
  });
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  Stats result = champ.applyPassives(base, base);
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
}

TEST_CASE("evaluateChampion with no passives returns base", "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  Stats result = champ.evaluateChampion();
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(50.0));
}

TEST_CASE("evaluateChampion with constant-bonus passive converges in one step",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.passives.push_back([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, true};
  });
  Stats result = champ.evaluateChampion();
  // 50 + 10 = 60, and second iteration: 50 + 10 = 60 (bonus doesn't depend on final)
  // so delta=0 after first step → converges immediately
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
}

TEST_CASE("evaluateChampion converges with final-dependent passive",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  // Bonus AD = 10% of final AD
  champ.passives.push_back([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = final[std::to_underlying(Stat::AD)] * 0.1;
    return Champion::PassiveResult{bonus, true};
  });
  // Fixed point: final = 50 + 0.1*final → final = 50/0.9 ≈ 55.5556
  Stats result = champ.evaluateChampion(0.0001);
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(55.5556).epsilon(0.001));
}

TEST_CASE("evaluateChampion respects eps for tighter convergence",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.passives.push_back([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = final[std::to_underlying(Stat::AD)] * 0.1;
    return Champion::PassiveResult{bonus, true};
  });
  Stats loose = champ.evaluateChampion(0.1);
  Stats tight = champ.evaluateChampion(0.00001);
  // Both should be near the fixed point; tight should be closer
  double fixed_point = 50.0 / 0.9;
  REQUIRE(std::abs(tight[std::to_underlying(Stat::AD)] - fixed_point) <=
          std::abs(loose[std::to_underlying(Stat::AD)] - fixed_point));
}

TEST_CASE("applyPassives consumes one-shot passives after one call",
          "[champion]") {
  Champion champ;
  // one-shot ignores dt; returns alive=false on its single call
  champ.one_shot_passives.push_back([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, false};
  });
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  Stats first = champ.applyPassives(base, base);
  REQUIRE(first[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
  REQUIRE(champ.one_shot_passives.empty());
  Stats second = champ.applyPassives(base, base);
  REQUIRE(second[std::to_underlying(Stat::AD)] == Catch::Approx(50.0));
}

TEST_CASE("applyPassives applies permanent, one-shot, and temp together",
          "[champion]") {
  Champion champ;
  champ.passives.push_back([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 5.0;
    return Champion::PassiveResult{bonus, true};
  });
  champ.one_shot_passives.push_back([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, false};
  });
  // temp: self-managed 1.0s lifetime; expires at time >= 1.0
  champ.temp_passives.push_back([](const Stats &, const Stats &, Type time) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 20.0;
    return Champion::PassiveResult{bonus, time < 1.0};
  });
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;

  Stats first = champ.applyPassives(base, base, 1.0);
  // 50 + 5 (perm) + 10 (one-shot) + 20 (temp) = 85
  REQUIRE(first[std::to_underlying(Stat::AD)] == Catch::Approx(85.0));
  REQUIRE(champ.one_shot_passives.empty());
  REQUIRE(champ.temp_passives.empty());

  Stats second = champ.applyPassives(base, base, 1.0);
  // only permanent remains: 50 + 5 = 55
  REQUIRE(second[std::to_underlying(Stat::AD)] == Catch::Approx(55.0));
}

TEST_CASE("temp passive self-manages lifetime via absolute time",
          "[champion]") {
  Champion champ;
  // expires when time >= 2.0
  champ.temp_passives.push_back([](const Stats &, const Stats &, Type time) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, time < 2.0};
  });
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;

  Stats first = champ.applyPassives(base, base, 1.0);
  REQUIRE(first[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
  REQUIRE(champ.temp_passives.size() == 1);

  Stats second = champ.applyPassives(base, base, 2.0);
  REQUIRE(second[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
  REQUIRE(champ.temp_passives.empty());

  Stats third = champ.applyPassives(base, base, 3.0);
  REQUIRE(third[std::to_underlying(Stat::AD)] == Catch::Approx(50.0));
}

TEST_CASE("temp passive with start offset expires at start + duration",
          "[champion]") {
  Champion champ;
  // passive starts at t=2.0, duration 3.0 → expires at t=5.0
  champ.temp_passives.push_back(
      [start = 2.0, duration = 3.0](const Stats &, const Stats &, Type time) {
        Stats bonus{};
        bonus[std::to_underlying(Stat::AD)] = 10.0;
        return Champion::PassiveResult{bonus, time - start < duration};
      });
  Stats base{};

  REQUIRE(champ.applyPassives(base, base, 2.0)[std::to_underlying(Stat::AD)] == Catch::Approx(10.0));
  REQUIRE(champ.temp_passives.size() == 1);
  REQUIRE(champ.applyPassives(base, base, 4.0)[std::to_underlying(Stat::AD)] == Catch::Approx(10.0));
  REQUIRE(champ.temp_passives.size() == 1);
  // at t=5.0: passive expires this iteration — bonus still applied, then removed
  REQUIRE(champ.applyPassives(base, base, 5.0)[std::to_underlying(Stat::AD)] == Catch::Approx(10.0));
  REQUIRE(champ.temp_passives.empty());
  REQUIRE(champ.applyPassives(base, base, 6.0)[std::to_underlying(Stat::AD)] == Catch::Approx(0.0));
}

TEST_CASE("temp passive refresh is a new passive with fresh start time",
          "[champion]") {
  Champion champ;
  // initial burn: starts at 0, expires at t=3.0
  champ.temp_passives.push_back(
      [start = 0.0, duration = 3.0](const Stats &, const Stats &, Type time) {
        Stats bonus{};
        bonus[std::to_underlying(Stat::AD)] = 10.0;
        return Champion::PassiveResult{bonus, time - start < duration};
      });
  Stats base{};

  // at t=2.0 still alive; "re-apply" burn = replace with a fresh passive
  champ.applyPassives(base, base, 2.0);
  REQUIRE(champ.temp_passives.size() == 1);
  champ.temp_passives.clear();
  champ.temp_passives.push_back(
      [start = 2.0, duration = 3.0](const Stats &, const Stats &, Type time) {
        Stats bonus{};
        bonus[std::to_underlying(Stat::AD)] = 10.0;
        return Champion::PassiveResult{bonus, time - start < duration};
      });

  // now expires at t=5.0, not t=3.0
  champ.applyPassives(base, base, 4.0);
  REQUIRE(champ.temp_passives.size() == 1);
  champ.applyPassives(base, base, 5.0);
  REQUIRE(champ.temp_passives.empty());
}

TEST_CASE("two temp passives are fully independent", "[champion]") {
  Champion champ;
  // burn: expires at t=5.0
  champ.temp_passives.push_back(
      [start = 0.0, duration = 5.0](const Stats &, const Stats &, Type time) {
        Stats bonus{};
        bonus[std::to_underlying(Stat::AD)] = 10.0;
        return Champion::PassiveResult{bonus, time - start < duration};
      });
  // poison: expires at t=3.0
  champ.temp_passives.push_back(
      [start = 0.0, duration = 3.0](const Stats &, const Stats &, Type time) {
        Stats bonus{};
        bonus[std::to_underlying(Stat::AD)] = 7.0;
        return Champion::PassiveResult{bonus, time - start < duration};
      });
  REQUIRE(champ.temp_passives.size() == 2);

  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  Stats r = champ.applyPassives(base, base, 3.0);
  // poison expired (3.0), burn still alive (3 < 5)
  REQUIRE(champ.temp_passives.size() == 1);
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(67.0)); // 50+10+7
}

// --- additional coverage tests ---

TEST_CASE("multiple one-shot passives all apply and are consumed in one call",
          "[champion]") {
  Champion champ;
  champ.one_shot_passives.push_back([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, false};
  });
  champ.one_shot_passives.push_back([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 25.0;
    return Champion::PassiveResult{bonus, false};
  });
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  Stats r = champ.applyPassives(base, base);
  // both applied once, both consumed
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(85.0)); // 50+10+25
  REQUIRE(champ.one_shot_passives.empty());
  // second call: no one-shot left
  Stats r2 = champ.applyPassives(base, base);
  REQUIRE(r2[std::to_underlying(Stat::AD)] == Catch::Approx(50.0));
}

TEST_CASE("one-shot passive with alive=true is still consumed", "[champion]") {
  Champion champ;
  // one-shot queue removes the passive regardless of the alive flag
  champ.one_shot_passives.push_back([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, true}; // alive=true, but one-shot
  });
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  champ.applyPassives(base, base);
  REQUIRE(champ.one_shot_passives.empty());
}

TEST_CASE("temp passive returning alive=false on first call is removed after applying bonus",
          "[champion]") {
  Champion champ;
  champ.temp_passives.push_back([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 15.0;
    return Champion::PassiveResult{bonus, false};
  });
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  Stats r = champ.applyPassives(base, base, 0.0);
  // bonus applied this iteration, then removed
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(65.0));
  REQUIRE(champ.temp_passives.empty());
  Stats r2 = champ.applyPassives(base, base, 1.0);
  REQUIRE(r2[std::to_underlying(Stat::AD)] == Catch::Approx(50.0));
}

TEST_CASE("permanent passive can read base stats", "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  // bonus = 10% of base AD (not final)
  champ.passives.push_back([](const Stats &base, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] =
        base[std::to_underlying(Stat::AD)] * 0.1;
    return Champion::PassiveResult{bonus, true};
  });
  Stats base = champ.getBaseStats();
  Stats r = champ.applyPassives(base, base);
  // 50 + 5 = 55
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(55.0));
}

TEST_CASE("applyPassives default time is 0.0", "[champion]") {
  Champion champ;
  // temp that is alive while time < 1.0
  champ.temp_passives.push_back([](const Stats &, const Stats &, Type time) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, time < 1.0};
  });
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  // no time arg → time=0.0 → alive (0 < 1), bonus applied, stays
  Stats r = champ.applyPassives(base, base);
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
  REQUIRE(champ.temp_passives.size() == 1);
}

TEST_CASE("passives can affect multiple stats at once", "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.mod_db.add(Stat::HP, ModType::Base, 1000.0, Source{"Base", ""});
  champ.passives.push_back([](const Stats &base, const Stats &, Type) {
    Stats bonus{};
    // +5% of base HP as AD, +10% of base AD as HP
    bonus[std::to_underlying(Stat::AD)] =
        base[std::to_underlying(Stat::HP)] * 0.05;
    bonus[std::to_underlying(Stat::HP)] =
        base[std::to_underlying(Stat::AD)] * 0.10;
    return Champion::PassiveResult{bonus, true};
  });
  Stats base = champ.getBaseStats();
  Stats r = champ.applyPassives(base, base);
  // AD: 50 + 1000*0.05 = 100; HP: 1000 + 50*0.10 = 1005
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(100.0));
  REQUIRE(r[std::to_underlying(Stat::HP)] == Catch::Approx(1005.0));
}

TEST_CASE("evaluateChampion throws ConvergenceError for non-converging passive",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  // weight 1.5 ≥ 1 → diverges
  champ.passives.push_back([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] =
        final[std::to_underlying(Stat::AD)] * 1.5;
    return Champion::PassiveResult{bonus, true};
  });
  REQUIRE_THROWS_AS(champ.evaluateChampion(0.01, 5), moba::ConvergenceError);
}

TEST_CASE("evaluateChampion does not consume one-shot or temp passives",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.passives.push_back([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, true};
  });
  champ.one_shot_passives.push_back([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 100.0;
    return Champion::PassiveResult{bonus, false};
  });
  champ.temp_passives.push_back([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 200.0;
    return Champion::PassiveResult{bonus, true};
  });

  Stats result = champ.evaluateChampion();
  // all passives contribute: 50 + 10 (perm) + 100 (one-shot) + 200 (temp) = 360
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(360.0));
  // one-shot consumed after evaluation
  REQUIRE(champ.one_shot_passives.empty());
  // temp with alive=true stays
  REQUIRE(champ.temp_passives.size() == 1);
}

TEST_CASE("evaluateChampion removes temp passives returning alive=false",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  // temp already expired (alive=false) — should be removed after evaluation
  champ.temp_passives.push_back([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 30.0;
    return Champion::PassiveResult{bonus, false};
  });

  Stats result = champ.evaluateChampion();
  // bonus applied during iteration: 50 + 30 = 80
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(80.0));
  // temp removed because alive=false on final iteration
  REQUIRE(champ.temp_passives.empty());
}

TEST_CASE("getDeltaStats is zero for two empty stats arrays", "[champion]") {
  Stats a{};
  Stats b{};
  REQUIRE(Champion::getDeltaStats(a, b) == Catch::Approx(0.0));
}

TEST_CASE("full simulation: permanent + one-shot + temp across timesteps",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.passives.push_back([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 5.0;
    return Champion::PassiveResult{bonus, true};
  });
  // burn temp: lasts 2.0s from t=0
  champ.temp_passives.push_back(
      [start = 0.0, duration = 2.0](const Stats &, const Stats &, Type time) {
        Stats bonus{};
        bonus[std::to_underlying(Stat::AD)] = 10.0;
        return Champion::PassiveResult{bonus, time - start < duration};
      });
  // burst one-shot: +20 once
  champ.one_shot_passives.push_back([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 20.0;
    return Champion::PassiveResult{bonus, false};
  });

  Stats base = champ.getBaseStats();
  // t=0: perm(5) + one-shot(20) + temp(10) = 50+35 = 85; one-shot consumed
  Stats t0 = champ.applyPassives(base, base, 0.0);
  REQUIRE(t0[std::to_underlying(Stat::AD)] == Catch::Approx(85.0));
  REQUIRE(champ.one_shot_passives.empty());
  REQUIRE(champ.temp_passives.size() == 1);

  // t=1: perm(5) + temp(10) = 65; temp still alive
  Stats t1 = champ.applyPassives(base, base, 1.0);
  REQUIRE(t1[std::to_underlying(Stat::AD)] == Catch::Approx(65.0));
  REQUIRE(champ.temp_passives.size() == 1);

  // t=2: temp expires this step (bonus applied, then removed): perm(5)+temp(10)=65
  Stats t2 = champ.applyPassives(base, base, 2.0);
  REQUIRE(t2[std::to_underlying(Stat::AD)] == Catch::Approx(65.0));
  REQUIRE(champ.temp_passives.empty());

  // t=3: only perm: 50+5 = 55
  Stats t3 = champ.applyPassives(base, base, 3.0);
  REQUIRE(t3[std::to_underlying(Stat::AD)] == Catch::Approx(55.0));
}