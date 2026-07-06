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

namespace {
Champion::PassiveFactory &factory() {
  static Champion::PassiveFactory f;
  return f;
}
} // namespace

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
  Stats result = champ.evaluateChampion();
  // No passives → result == base, converges in one iteration
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(144.0));
}

TEST_CASE("Scenario: final-dependent passive converges to fixed point",
          "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] =
        final[std::to_underlying(Stat::AD)] * 0.1;
    return Champion::PassiveResult{bonus, true};
  }));
  // Fixed point: final = 50 + 0.1*final → final = 50/0.9 ≈ 55.5556
  Stats result = champ.evaluateChampion(0.0001);
  REQUIRE(result[std::to_underlying(Stat::AD)] ==
          Catch::Approx(55.5556).epsilon(0.001));
  // Verify it's not just base
  REQUIRE(result[std::to_underlying(Stat::AD)] !=
          Catch::Approx(50.0).epsilon(0.001));
}

TEST_CASE("Scenario: cancelling passives return base", "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::AP, ModType::Base, 300.0, Source{"Base", ""});
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AP)] =
        final[std::to_underlying(Stat::AP)] * 0.1;
    return Champion::PassiveResult{bonus, true};
  }));
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AP)] =
        final[std::to_underlying(Stat::AP)] * -0.1;
    return Champion::PassiveResult{bonus, true};
  }));
  // +0.1*final and -0.1*final cancel → bonus = 0 → result = base = 300
  Stats result = champ.evaluateChampion();
  REQUIRE(result[std::to_underlying(Stat::AP)] == Catch::Approx(300.0));
}

TEST_CASE("Scenario: uneven weights reach non-base fixed point", "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::AP, ModType::Base, 300.0, Source{"Base", ""});
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AP)] =
        final[std::to_underlying(Stat::AP)] * 0.2;
    return Champion::PassiveResult{bonus, true};
  }));
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AP)] =
        final[std::to_underlying(Stat::AP)] * -0.1;
    return Champion::PassiveResult{bonus, true};
  }));
  // Net weight +0.1: final = 300 + 0.1*final → final = 300/0.9 ≈ 333.3333
  Stats result = champ.evaluateChampion(0.0001);
  REQUIRE(result[std::to_underlying(Stat::AP)] ==
          Catch::Approx(333.3333).epsilon(0.001));
  // Verify it's not base
  REQUIRE(result[std::to_underlying(Stat::AP)] !=
          Catch::Approx(300.0).epsilon(0.001));
}

TEST_CASE("Scenario: cross-stat dependency bonus from one stat to another",
          "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::HP, ModType::Base, 1000.0, Source{"Base", ""});
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    // AD bonus = 1% of final HP
    bonus[std::to_underlying(Stat::AD)] =
        final[std::to_underlying(Stat::HP)] * 0.01;
    return Champion::PassiveResult{bonus, true};
  }));
  // HP unchanged (passive doesn't touch HP) → final[HP] = 1000
  // AD gets +0.01*1000 = 10 each iteration, but base AD = 50
  // Iter 1: final = base + bonus = {HP:1000, AD:50} + {AD:10} = {HP:1000,
  // AD:60} Iter 2: final = base + bonus(final) = {HP:1000, AD:50} +
  // {AD:0.01*1000=10} = {HP:1000, AD:60} delta = 0 → converges, final[AD] = 60
  Stats result = champ.evaluateChampion(0.0001);
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
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] =
        final[std::to_underlying(Stat::AD)] * 0.1;
    return Champion::PassiveResult{bonus, true};
  }));
  // Fixed point: final = 144 + 0.1*final → final = 144/0.9 = 160
  Stats base = champ.getBaseStats();
  REQUIRE(base[std::to_underlying(Stat::AD)] == Catch::Approx(144.0));
  Stats result = champ.evaluateChampion(0.0001);
  REQUIRE(result[std::to_underlying(Stat::AD)] ==
          Catch::Approx(160.0).epsilon(0.001));
}

TEST_CASE("Scenario: non-converging passive throws ConvergenceError",
          "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] =
        final[std::to_underlying(Stat::AD)] * 1.5;
    return Champion::PassiveResult{bonus, true};
  }));
  // Weight 1.5 ≥ 1 → diverges, should throw after max_iter
  REQUIRE_THROWS_AS(champ.evaluateChampion(0.01, 5), moba::ConvergenceError);
}

TEST_CASE("Scenario: temp passive expires mid-simulation", "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 5.0;
    return Champion::PassiveResult{bonus, true}; // permanent
  }));
  // buff: +20 AD for 3 seconds starting at t=0
  champ.addPassive(factory().make(
      [start = 0.0, duration = 3.0](const Stats &, const Stats &, Type time) {
        Stats bonus{};
        bonus[std::to_underlying(Stat::AD)] = 20.0;
        return Champion::PassiveResult{bonus, time - start < duration};
      }));

  Stats base = champ.getBaseStats();
  REQUIRE(base[std::to_underlying(Stat::AD)] == Catch::Approx(50.0));

  // t=1: perm(5) + temp(20) = 75; temp alive
  Stats t1 = champ.applyPassives(base, base, 1.0);
  REQUIRE(t1[std::to_underlying(Stat::AD)] == Catch::Approx(75.0));
  // perm + temp remain
  REQUIRE(champ.passives.size() == 2);

  // t=3: temp expires this step (bonus applied, then removed): 50+5+20 = 75
  Stats t3 = champ.applyPassives(base, base, 3.0);
  REQUIRE(t3[std::to_underlying(Stat::AD)] == Catch::Approx(75.0));
  // only perm remains
  REQUIRE(champ.passives.size() == 1);

  // t=4: only perm: 55
  Stats t4 = champ.applyPassives(base, base, 4.0);
  REQUIRE(t4[std::to_underlying(Stat::AD)] == Catch::Approx(55.0));
  REQUIRE(champ.passives.size() == 1);
}

TEST_CASE("Scenario: one-shot passive fires once then permanent remains",
          "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::HP, ModType::Base, 1000.0, Source{"Base", ""});
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::HP)] = 100.0;
    return Champion::PassiveResult{bonus, true}; // permanent
  }));
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::HP)] = 500.0;
    return Champion::PassiveResult{bonus, false}; // one-shot
  }));

  Stats base = champ.getBaseStats();
  // first step: 1000 + 100 (perm) + 500 (one-shot) = 1600
  Stats first = champ.applyPassives(base, base, 0.0);
  REQUIRE(first[std::to_underlying(Stat::HP)] == Catch::Approx(1600.0));
  // one-shot consumed; only perm remains
  REQUIRE(champ.passives.size() == 1);

  // second step: only perm: 1000 + 100 = 1100
  Stats second = champ.applyPassives(base, base, 1.0);
  REQUIRE(second[std::to_underlying(Stat::HP)] == Catch::Approx(1100.0));
  REQUIRE(champ.passives.size() == 1);
}

TEST_CASE("Scenario: evaluateChampion includes one-shot in fixed-point",
          "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  // one-shot: +100 AD
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 100.0;
    return Champion::PassiveResult{bonus, false};
  }));
  // permanent: AD += 10% of final AD
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] =
        final[std::to_underlying(Stat::AD)] * 0.1;
    return Champion::PassiveResult{bonus, true};
  }));
  // fixed-point includes one-shot: final = 50 + 100 + 0.1*final → 150/0.9 ≈
  // 166.67
  Stats r = champ.evaluateChampion(0.0001);
  REQUIRE(r[std::to_underlying(Stat::AD)] ==
          Catch::Approx(166.6667).epsilon(0.001));
  // one-shot consumed; permanent stays → 1 passive
  REQUIRE(champ.passives.size() == 1);
}

TEST_CASE(
    "Scenario: evaluateChampion with temp that returns alive=false is removed",
    "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  // temp: already expired (alive=false) but gives +30 once
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 30.0;
    return Champion::PassiveResult{bonus, false};
  }));
  Stats r = champ.evaluateChampion();
  // bonus applied during iteration: 50 + 30 = 80
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(80.0));
  // temp removed because alive=false on final iteration
  REQUIRE(champ.passives.empty());
}

TEST_CASE("Scenario: complex pipeline with mod_db + perm + one-shot + temp",
          "[scenario]") {
  Champion champ;
  Source src{"Item", ""};
  // AD base: 80 * 1.2 * 1.5 = 144
  champ.mod_db.add(Stat::AD, ModType::Base, 80.0, src);
  champ.mod_db.add(Stat::AD, ModType::Inc, 0.2, src);
  champ.mod_db.add(Stat::AD, ModType::More, 1.5, src);
  // HP base: 1000
  champ.mod_db.add(Stat::HP, ModType::Base, 1000.0, Source{"Base", ""});
  // permanent: AD += 5% of final HP
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] =
        final[std::to_underlying(Stat::HP)] * 0.05;
    return Champion::PassiveResult{bonus, true};
  }));
  // one-shot: +200 HP
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::HP)] = 200.0;
    return Champion::PassiveResult{bonus, false};
  }));
  // temp: +50 AD for this evaluation (alive=true, stays)
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 50.0;
    return Champion::PassiveResult{bonus, true};
  }));

  Stats r = champ.evaluateChampion(0.0001);
  // HP: 1000 + 200 (one-shot) = 1200
  REQUIRE(r[std::to_underlying(Stat::HP)] == Catch::Approx(1200.0));
  // AD: 144 (base) + 50 (temp) + 0.05*1200 = 144 + 50 + 60 = 254
  REQUIRE(r[std::to_underlying(Stat::AD)] ==
          Catch::Approx(254.0).epsilon(0.01));
  // one-shot consumed (alive=false); perm and temp stay (alive=true) → 2
  // passives
  REQUIRE(champ.passives.size() == 2);
}

TEST_CASE("Scenario: simulation step does not affect evaluateChampion passives",
          "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, true}; // permanent
  }));
  champ.addPassive(factory().make(
      [start = 0.0, duration = 5.0](const Stats &, const Stats &, Type time) {
        Stats bonus{};
        bonus[std::to_underlying(Stat::AD)] = 20.0;
        return Champion::PassiveResult{bonus, time - start < duration}; // temp
      }));

  Stats base = champ.getBaseStats();
  // simulation step at t=1: perm + temp = 50 + 10 + 20 = 80; both stay
  // (alive=true)
  Stats sim = champ.applyPassives(base, base, 1.0);
  REQUIRE(sim[std::to_underlying(Stat::AD)] == Catch::Approx(80.0));
  REQUIRE(champ.passives.size() == 2);

  // evaluateChampion: includes both (alive=true at time=0) → 50 + 10 + 20 = 80
  // both stay (alive=true)
  Stats eval = champ.evaluateChampion();
  REQUIRE(eval[std::to_underlying(Stat::AD)] == Catch::Approx(80.0));
  REQUIRE(champ.passives.size() == 2);
}

TEST_CASE("Scenario: two one-shot passives fire in single evaluateChampion",
          "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 20.0;
    return Champion::PassiveResult{bonus, false};
  }));
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 30.0;
    return Champion::PassiveResult{bonus, false};
  }));
  Stats r = champ.evaluateChampion();
  // 50 + 20 + 30 = 100
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(100.0));
  // both consumed
  REQUIRE(champ.passives.empty());
}

TEST_CASE("Scenario: large negative passive brings stat below zero",
          "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = -75.0;
    return Champion::PassiveResult{bonus, true};
  }));
  Stats r = champ.evaluateChampion();
  // 50 - 75 = -25 (no clamping)
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(-25.0));
}

TEST_CASE("Scenario: passive chain AD→AP→HP converges", "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.mod_db.add(Stat::AP, ModType::Base, 50.0, Source{"Base", ""});
  champ.mod_db.add(Stat::HP, ModType::Base, 1000.0, Source{"Base", ""});
  // AD += 10% of AP
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] =
        final[std::to_underlying(Stat::AP)] * 0.1;
    return Champion::PassiveResult{bonus, true};
  }));
  // AP += 1% of HP
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AP)] =
        final[std::to_underlying(Stat::HP)] * 0.01;
    return Champion::PassiveResult{bonus, true};
  }));
  // HP += 0.5 * AD
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::HP)] =
        final[std::to_underlying(Stat::AD)] * 0.5;
    return Champion::PassiveResult{bonus, true};
  }));
  // Should converge — just verify no exception and stable
  REQUIRE_NOTHROW(champ.evaluateChampion(0.0001, 10000));
  Stats r = champ.evaluateChampion(0.0001, 10000);
  // HP should be > 1000 (gains from AD), AD > 50 (gains from AP), AP > 50
  // (gains from HP)
  REQUIRE(r[std::to_underlying(Stat::HP)] > 1000.0);
  REQUIRE(r[std::to_underlying(Stat::AD)] > 50.0);
  REQUIRE(r[std::to_underlying(Stat::AP)] > 50.0);
}

TEST_CASE("Scenario: temp passive re-inserted after expiry in simulation",
          "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  // burn: 2s duration
  auto make_burn = [duration = 2.0](Type start) {
    return [start, duration](const Stats &, const Stats &, Type time) {
      Stats bonus{};
      bonus[std::to_underlying(Stat::AD)] = 15.0;
      return Champion::PassiveResult{bonus, time - start < duration};
    };
  };
  Stats base = champ.getBaseStats();

  // initial burn at t=0
  champ.addPassive(factory().make(make_burn(0.0)));
  // t=1: alive
  champ.applyPassives(base, base, 1.0);
  REQUIRE(champ.passives.size() == 1);
  // t=2: expires
  champ.applyPassives(base, base, 2.0);
  REQUIRE(champ.passives.empty());
  // re-apply at t=2 (refresh)
  champ.addPassive(factory().make(make_burn(2.0)));
  // t=3: alive (3 - 2 = 1 < 2)
  champ.applyPassives(base, base, 3.0);
  REQUIRE(champ.passives.size() == 1);
  // t=4: expires (4 - 2 = 2, not < 2)
  champ.applyPassives(base, base, 4.0);
  REQUIRE(champ.passives.empty());
}

TEST_CASE("Scenario: evaluateChampion with all passive types and cross-stat "
          "dependencies",
          "[scenario]") {
  Champion champ;
  Source src{"Item", ""};
  // Base stats
  champ.mod_db.add(Stat::AD, ModType::Base, 80.0, src);
  champ.mod_db.add(Stat::AD, ModType::Inc, 0.2, src);
  champ.mod_db.add(Stat::AD, ModType::More, 1.5, src); // AD = 80*1.2*1.5 = 144
  champ.mod_db.add(Stat::HP, ModType::Base, 1500.0, Source{"Base", ""});
  champ.mod_db.add(Stat::AP, ModType::Base, 100.0, Source{"Base", ""});

  // permanent: AD += 2% of final HP
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] =
        final[std::to_underlying(Stat::HP)] * 0.02;
    return Champion::PassiveResult{bonus, true};
  }));
  // one-shot: +300 HP
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::HP)] = 300.0;
    return Champion::PassiveResult{bonus, false};
  }));
  // temp: +40 AP, alive=true (stays after evaluation)
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AP)] = 40.0;
    return Champion::PassiveResult{bonus, true};
  }));

  Stats r = champ.evaluateChampion(0.0001);
  // HP: 1500 + 300 (one-shot) = 1800
  REQUIRE(r[std::to_underlying(Stat::HP)] == Catch::Approx(1800.0));
  // AP: 100 + 40 (temp) = 140
  REQUIRE(r[std::to_underlying(Stat::AP)] == Catch::Approx(140.0));
  // AD: 144 (base) + 0.02*1800 = 144 + 36 = 180
  REQUIRE(r[std::to_underlying(Stat::AD)] ==
          Catch::Approx(180.0).epsilon(0.01));
  // one-shot consumed; perm and temp stay → 2 passives
  REQUIRE(champ.passives.size() == 2);
}

TEST_CASE("Scenario: non-converging two-passive system throws", "[scenario]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.mod_db.add(Stat::HP, ModType::Base, 1000.0, Source{"Base", ""});
  // AD += 2 * final HP (runaway)
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] =
        final[std::to_underlying(Stat::HP)] * 2.0;
    return Champion::PassiveResult{bonus, true};
  }));
  // HP += 2 * final AD (runaway)
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::HP)] =
        final[std::to_underlying(Stat::AD)] * 2.0;
    return Champion::PassiveResult{bonus, true};
  }));
  REQUIRE_THROWS_AS(champ.evaluateChampion(0.01, 100), moba::ConvergenceError);
}