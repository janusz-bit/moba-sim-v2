#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>

#include "moba_sim.hpp"

using moba::Champion;
using moba::ModType;
using moba::Source;
using moba::Stat;
using Stats = Champion::Stats;
using moba::Type;

// Factory instance shared across tests in this file. Each make() produces a
// PassiveEntry with a unique id, used by Champion::addPassive for dedup.
namespace {
Champion::PassiveFactory &factory() {
  static Champion::PassiveFactory f;
  return f;
}
} // namespace

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

TEST_CASE("Champion applyPassives with no passives returns base",
          "[champion]") {
  Champion champ;
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  Stats result = champ.applyPassives(base, base);
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(50.0));
}

TEST_CASE("Champion applyPassives with one passive adds bonus to base",
          "[champion]") {
  Champion champ;
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, true};
  }));
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  Stats result = champ.applyPassives(base, base);
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
}

TEST_CASE("Champion applyPassives sums multiple independent passives",
          "[champion]") {
  Champion champ;
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, true};
  }));
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 20.0;
    return Champion::PassiveResult{bonus, true};
  }));
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
  champ1.addPassive(factory().make(passiveA));
  champ1.addPassive(factory().make(passiveB));
  Champion champ2;
  champ2.addPassive(factory().make(passiveB));
  champ2.addPassive(factory().make(passiveA));
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  Stats r1 = champ1.applyPassives(base, base);
  Stats r2 = champ2.applyPassives(base, base);
  REQUIRE(r1[std::to_underlying(Stat::AD)] ==
          Catch::Approx(r2[std::to_underlying(Stat::AD)]));
  REQUIRE(r1[std::to_underlying(Stat::AD)] == Catch::Approx(80.0));
}

TEST_CASE("Champion applyPassives is callable", "[champion]") {
  Champion champ;
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, true};
  }));
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
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, true};
  }));
  Stats result = champ.evaluateChampion();
  // 50 + 10 = 60, and second iteration: 50 + 10 = 60 (bonus doesn't depend on
  // final) so delta=0 after first step → converges immediately
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
}

TEST_CASE("evaluateChampion converges with final-dependent passive",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  // Bonus AD = 10% of final AD
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
}

TEST_CASE("evaluateChampion respects eps for tighter convergence",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] =
        final[std::to_underlying(Stat::AD)] * 0.1;
    return Champion::PassiveResult{bonus, true};
  }));
  Stats loose = champ.evaluateChampion(0.1);
  Stats tight = champ.evaluateChampion(0.00001);
  // Both should be near the fixed point; tight should be closer
  double fixed_point = 50.0 / 0.9;
  REQUIRE(std::abs(tight[std::to_underlying(Stat::AD)] - fixed_point) <=
          std::abs(loose[std::to_underlying(Stat::AD)] - fixed_point));
}

// --- one-shot passives (alive=false, removed after one application) ---

TEST_CASE("one-shot passive is removed after one call", "[champion]") {
  Champion champ;
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, false};
  }));
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  Stats first = champ.applyPassives(base, base);
  REQUIRE(first[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
  REQUIRE(champ.passives.empty());
  Stats second = champ.applyPassives(base, base);
  REQUIRE(second[std::to_underlying(Stat::AD)] == Catch::Approx(50.0));
}

TEST_CASE("multiple one-shot passives all apply and are consumed in one call",
          "[champion]") {
  Champion champ;
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, false};
  }));
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 25.0;
    return Champion::PassiveResult{bonus, false};
  }));
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  Stats r = champ.applyPassives(base, base);
  // both applied once, both consumed
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(85.0)); // 50+10+25
  REQUIRE(champ.passives.empty());
  Stats r2 = champ.applyPassives(base, base);
  REQUIRE(r2[std::to_underlying(Stat::AD)] == Catch::Approx(50.0));
}

TEST_CASE("one-shot passive with zero bonus is still consumed", "[champion]") {
  Champion champ;
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{Stats{}, false};
  }));
  champ.applyPassives(Stats{}, Stats{}, 0.0);
  REQUIRE(champ.passives.empty());
}

TEST_CASE("many one-shot passives all fire once then queue is empty",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  for (int i = 0; i < 5; ++i) {
    champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
      Stats bonus{};
      bonus[std::to_underlying(Stat::AD)] = 10.0;
      return Champion::PassiveResult{bonus, false};
    }));
  }
  Stats base = champ.getBaseStats();
  Stats r = champ.applyPassives(base, base);
  // 50 + 5*10 = 100
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(100.0));
  REQUIRE(champ.passives.empty());
  Stats r2 = champ.applyPassives(base, base);
  REQUIRE(r2[std::to_underlying(Stat::AD)] == Catch::Approx(50.0));
}

// --- temp passives (self-managed lifetime via time) ---

TEST_CASE("temp passive self-manages lifetime via absolute time",
          "[champion]") {
  Champion champ;
  // expires when time >= 2.0
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type time) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, time < 2.0};
  }));
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;

  Stats first = champ.applyPassives(base, base, 1.0);
  REQUIRE(first[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
  REQUIRE(champ.passives.size() == 1);

  Stats second = champ.applyPassives(base, base, 2.0);
  REQUIRE(second[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
  REQUIRE(champ.passives.empty());

  Stats third = champ.applyPassives(base, base, 3.0);
  REQUIRE(third[std::to_underlying(Stat::AD)] == Catch::Approx(50.0));
}

TEST_CASE("temp passive with start offset expires at start + duration",
          "[champion]") {
  Champion champ;
  // passive starts at t=2.0, duration 3.0 → expires at t=5.0
  champ.addPassive(factory().make(
      [start = 2.0, duration = 3.0](const Stats &, const Stats &, Type time) {
        Stats bonus{};
        bonus[std::to_underlying(Stat::AD)] = 10.0;
        return Champion::PassiveResult{bonus, time - start < duration};
      }));
  Stats base{};

  REQUIRE(champ.applyPassives(base, base, 2.0)[std::to_underlying(Stat::AD)] ==
          Catch::Approx(10.0));
  REQUIRE(champ.passives.size() == 1);
  REQUIRE(champ.applyPassives(base, base, 4.0)[std::to_underlying(Stat::AD)] ==
          Catch::Approx(10.0));
  REQUIRE(champ.passives.size() == 1);
  // at t=5.0: passive expires this iteration — bonus still applied, then
  // removed
  REQUIRE(champ.applyPassives(base, base, 5.0)[std::to_underlying(Stat::AD)] ==
          Catch::Approx(10.0));
  REQUIRE(champ.passives.empty());
  REQUIRE(champ.applyPassives(base, base, 6.0)[std::to_underlying(Stat::AD)] ==
          Catch::Approx(0.0));
}

TEST_CASE("temp passive refresh is a new passive with fresh start time",
          "[champion]") {
  Champion champ;
  // initial burn: starts at 0, expires at t=3.0
  champ.addPassive(factory().make(
      [start = 0.0, duration = 3.0](const Stats &, const Stats &, Type time) {
        Stats bonus{};
        bonus[std::to_underlying(Stat::AD)] = 10.0;
        return Champion::PassiveResult{bonus, time - start < duration};
      }));
  Stats base{};

  // at t=2.0 still alive; "re-apply" burn = replace with a fresh passive
  champ.applyPassives(base, base, 2.0);
  REQUIRE(champ.passives.size() == 1);
  champ.passives.clear();
  champ.addPassive(factory().make(
      [start = 2.0, duration = 3.0](const Stats &, const Stats &, Type time) {
        Stats bonus{};
        bonus[std::to_underlying(Stat::AD)] = 10.0;
        return Champion::PassiveResult{bonus, time - start < duration};
      }));

  // now expires at t=5.0, not t=3.0
  champ.applyPassives(base, base, 4.0);
  REQUIRE(champ.passives.size() == 1);
  champ.applyPassives(base, base, 5.0);
  REQUIRE(champ.passives.empty());
}

TEST_CASE("two temp passives are fully independent", "[champion]") {
  Champion champ;
  // burn: expires at t=5.0
  champ.addPassive(factory().make(
      [start = 0.0, duration = 5.0](const Stats &, const Stats &, Type time) {
        Stats bonus{};
        bonus[std::to_underlying(Stat::AD)] = 10.0;
        return Champion::PassiveResult{bonus, time - start < duration};
      }));
  // poison: expires at t=3.0
  champ.addPassive(factory().make(
      [start = 0.0, duration = 3.0](const Stats &, const Stats &, Type time) {
        Stats bonus{};
        bonus[std::to_underlying(Stat::AD)] = 7.0;
        return Champion::PassiveResult{bonus, time - start < duration};
      }));
  REQUIRE(champ.passives.size() == 2);

  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  Stats r = champ.applyPassives(base, base, 3.0);
  // poison expired (3.0), burn still alive (3 < 5)
  REQUIRE(champ.passives.size() == 1);
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(67.0)); // 50+10+7
}

TEST_CASE("temp passive returning alive=false on first call is removed after "
          "applying bonus",
          "[champion]") {
  Champion champ;
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 15.0;
    return Champion::PassiveResult{bonus, false};
  }));
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  Stats r = champ.applyPassives(base, base, 0.0);
  // bonus applied this iteration, then removed
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(65.0));
  REQUIRE(champ.passives.empty());
  Stats r2 = champ.applyPassives(base, base, 1.0);
  REQUIRE(r2[std::to_underlying(Stat::AD)] == Catch::Approx(50.0));
}

TEST_CASE("temp passive expiring at exactly t=0", "[champion]") {
  Champion champ;
  // expires immediately (alive when time < 0, i.e. never alive for time >= 0)
  champ.addPassive(factory().make(
      [start = 0.0, duration = 0.0](const Stats &, const Stats &, Type time) {
        Stats bonus{};
        bonus[std::to_underlying(Stat::AD)] = 10.0;
        return Champion::PassiveResult{bonus, time - start < duration};
      }));
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  // at t=0: 0 < 0 is false → alive=false → bonus applied then removed
  Stats r = champ.applyPassives(base, base, 0.0);
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
  REQUIRE(champ.passives.empty());
}

TEST_CASE("temp passive with negative start time", "[champion]") {
  Champion champ;
  // start in the "past" at -5.0, duration 10 → expires at t=5.0
  champ.addPassive(factory().make(
      [start = -5.0, duration = 10.0](const Stats &, const Stats &, Type time) {
        Stats bonus{};
        bonus[std::to_underlying(Stat::AD)] = 10.0;
        return Champion::PassiveResult{bonus, time - start < duration};
      }));
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  // at t=0: 0 - (-5) = 5 < 10 → alive
  Stats r = champ.applyPassives(base, base, 0.0);
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
  REQUIRE(champ.passives.size() == 1);
  // at t=5.0: 5 - (-5) = 10, not < 10 → expires
  champ.applyPassives(base, base, 5.0);
  REQUIRE(champ.passives.empty());
}

TEST_CASE("many temp passives all expire independently", "[champion]") {
  Champion champ;
  // 5 temp passives, each expires at different time
  for (int i = 0; i < 5; ++i) {
    const Type dur = static_cast<Type>(i + 1); // 1,2,3,4,5
    champ.addPassive(factory().make(
        [start = 0.0, dur](const Stats &, const Stats &, Type time) {
          Stats bonus{};
          bonus[std::to_underlying(Stat::AD)] = 10.0;
          return Champion::PassiveResult{bonus, time - start < dur};
        }));
  }
  Stats base{};
  // at t=2.5: passives with dur 1,2 expired; 3,4,5 alive
  champ.applyPassives(base, base, 2.5);
  REQUIRE(champ.passives.size() == 3);
  // at t=3.5: dur 3 expires; 4,5 alive
  champ.applyPassives(base, base, 3.5);
  REQUIRE(champ.passives.size() == 2);
  // at t=5.5: all expired
  champ.applyPassives(base, base, 5.5);
  REQUIRE(champ.passives.empty());
}

TEST_CASE("applyPassives default time is 0.0", "[champion]") {
  Champion champ;
  // temp that is alive while time < 1.0
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type time) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, time < 1.0};
  }));
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  // no time arg → time=0.0 → alive (0 < 1), bonus applied, stays
  Stats r = champ.applyPassives(base, base);
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
  REQUIRE(champ.passives.size() == 1);
}

TEST_CASE("applyPassives temp passive can read final for cross-stat effect",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::MaxHP, ModType::Base, 1000.0, Source{"Base", ""});
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  // temp: AD += 1% of final HP, alive for 2s
  champ.addPassive(factory().make(
      [start = 0.0,
       duration = 2.0](const Stats &, const Stats &final, Type time) {
        Stats bonus{};
        bonus[std::to_underlying(Stat::AD)] =
            final[std::to_underlying(Stat::MaxHP)] * 0.01;
        return Champion::PassiveResult{bonus, time - start < duration};
      }));
  Stats base = champ.getBaseStats();
  // HP=1000 → AD bonus = 10 → AD = 60
  Stats r = champ.applyPassives(base, base, 1.0);
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
  REQUIRE(champ.passives.size() == 1);
  // at t=2.0: expires (bonus applied, then removed)
  Stats r2 = champ.applyPassives(base, base, 2.0);
  REQUIRE(r2[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
  REQUIRE(champ.passives.empty());
}

// --- mixed permanent + one-shot + temp in one queue ---

TEST_CASE("applyPassives applies permanent, one-shot, and temp together",
          "[champion]") {
  Champion champ;
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 5.0;
    return Champion::PassiveResult{bonus, true}; // permanent
  }));
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, false}; // one-shot
  }));
  // temp: self-managed 1.0s lifetime; expires at time >= 1.0
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type time) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 20.0;
    return Champion::PassiveResult{bonus, time < 1.0};
  }));
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;

  Stats first = champ.applyPassives(base, base, 1.0);
  // 50 + 5 (perm) + 10 (one-shot) + 20 (temp) = 85
  REQUIRE(first[std::to_underlying(Stat::AD)] == Catch::Approx(85.0));
  // one-shot and temp removed; only permanent remains
  REQUIRE(champ.passives.size() == 1);

  Stats second = champ.applyPassives(base, base, 1.0);
  // only permanent remains: 50 + 5 = 55
  REQUIRE(second[std::to_underlying(Stat::AD)] == Catch::Approx(55.0));
  REQUIRE(champ.passives.size() == 1);
}

TEST_CASE("full simulation: permanent + one-shot + temp across timesteps",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 5.0;
    return Champion::PassiveResult{bonus, true}; // permanent
  }));
  // burn temp: lasts 2.0s from t=0
  champ.addPassive(factory().make(
      [start = 0.0, duration = 2.0](const Stats &, const Stats &, Type time) {
        Stats bonus{};
        bonus[std::to_underlying(Stat::AD)] = 10.0;
        return Champion::PassiveResult{bonus, time - start < duration};
      }));
  // burst one-shot: +20 once
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 20.0;
    return Champion::PassiveResult{bonus, false};
  }));

  Stats base = champ.getBaseStats();
  // t=0: perm(5) + one-shot(20) + temp(10) = 50+35 = 85; one-shot consumed
  Stats t0 = champ.applyPassives(base, base, 0.0);
  REQUIRE(t0[std::to_underlying(Stat::AD)] == Catch::Approx(85.0));
  // perm + temp remain (2)
  REQUIRE(champ.passives.size() == 2);

  // t=1: perm(5) + temp(10) = 65; temp still alive
  Stats t1 = champ.applyPassives(base, base, 1.0);
  REQUIRE(t1[std::to_underlying(Stat::AD)] == Catch::Approx(65.0));
  REQUIRE(champ.passives.size() == 2);

  // t=2: temp expires this step (bonus applied, then removed):
  // perm(5)+temp(10)=65
  Stats t2 = champ.applyPassives(base, base, 2.0);
  REQUIRE(t2[std::to_underlying(Stat::AD)] == Catch::Approx(65.0));
  // only perm remains (1)
  REQUIRE(champ.passives.size() == 1);

  // t=3: only perm: 50+5 = 55
  Stats t3 = champ.applyPassives(base, base, 3.0);
  REQUIRE(t3[std::to_underlying(Stat::AD)] == Catch::Approx(55.0));
  REQUIRE(champ.passives.size() == 1);
}

// --- permanent passive edge cases ---

TEST_CASE("permanent passive can read base stats", "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  // bonus = 10% of base AD (not final)
  champ.addPassive(factory().make([](const Stats &base, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] =
        base[std::to_underlying(Stat::AD)] * 0.1;
    return Champion::PassiveResult{bonus, true};
  }));
  Stats base = champ.getBaseStats();
  Stats r = champ.applyPassives(base, base);
  // 50 + 5 = 55
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(55.0));
}

TEST_CASE("passives can affect multiple stats at once", "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.mod_db.add(Stat::MaxHP, ModType::Base, 1000.0, Source{"Base", ""});
  champ.addPassive(factory().make([](const Stats &base, const Stats &, Type) {
    Stats bonus{};
    // +5% of base HP as AD, +10% of base AD as HP
    bonus[std::to_underlying(Stat::AD)] =
        base[std::to_underlying(Stat::MaxHP)] * 0.05;
    bonus[std::to_underlying(Stat::MaxHP)] =
        base[std::to_underlying(Stat::AD)] * 0.10;
    return Champion::PassiveResult{bonus, true};
  }));
  Stats base = champ.getBaseStats();
  Stats r = champ.applyPassives(base, base);
  // AD: 50 + 1000*0.05 = 100; HP: 1000 + 50*0.10 = 1005
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(100.0));
  REQUIRE(r[std::to_underlying(Stat::MaxHP)] == Catch::Approx(1005.0));
}

TEST_CASE("applyPassives with passives returning negative bonus",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = -15.0;
    return Champion::PassiveResult{bonus, true};
  }));
  Stats base = champ.getBaseStats();
  Stats r = champ.applyPassives(base, base);
  // 50 - 15 = 35
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(35.0));
}

TEST_CASE("applyPassives with passive returning all-zero bonus", "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{Stats{}, true};
  }));
  Stats base = champ.getBaseStats();
  Stats r = champ.applyPassives(base, base);
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(50.0));
}

TEST_CASE("many permanent passives all contribute", "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  for (int i = 0; i < 10; ++i) {
    champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
      Stats bonus{};
      bonus[std::to_underlying(Stat::AD)] = 5.0;
      return Champion::PassiveResult{bonus, true};
    }));
  }
  Stats base = champ.getBaseStats();
  Stats r = champ.applyPassives(base, base);
  // 50 + 10*5 = 100
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(100.0));
}

TEST_CASE("applyPassives does not mutate permanent passives", "[champion]") {
  Champion champ;
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, true};
  }));
  Stats base{};
  base[std::to_underlying(Stat::AD)] = 50.0;
  champ.applyPassives(base, base, 1.0);
  champ.applyPassives(base, base, 2.0);
  champ.applyPassives(base, base, 3.0);
  REQUIRE(champ.passives.size() == 1);
}

TEST_CASE("applyPassives time argument is forwarded to passives",
          "[champion]") {
  Champion champ;
  // passive records the last time it was called with
  auto last_time = std::make_shared<Type>(-1.0);
  champ.addPassive(
      factory().make([last_time](const Stats &, const Stats &, Type time) {
        *last_time = time;
        return Champion::PassiveResult{Stats{}, true};
      }));
  Stats base{};
  champ.applyPassives(base, base, 42.5);
  REQUIRE(*last_time == Catch::Approx(42.5));
  champ.applyPassives(base, base, 100.0);
  REQUIRE(*last_time == Catch::Approx(100.0));
}

TEST_CASE("applyPassives with only permanent passive works", "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, true};
  }));
  Stats base = champ.getBaseStats();
  Stats r = champ.applyPassives(base, base, 1.0);
  // only perm: 50 + 10 = 60
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
  REQUIRE(champ.passives.size() == 1);
}

TEST_CASE("applyPassives with passive that reads both base and final",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.mod_db.add(Stat::MaxHP, ModType::Base, 1000.0, Source{"Base", ""});
  // bonus = (final AD - base AD) * 0.5 → amplifies the delta
  champ.addPassive(
      factory().make([](const Stats &base, const Stats &final, Type) {
        Stats bonus{};
        bonus[std::to_underlying(Stat::AD)] =
            (final[std::to_underlying(Stat::AD)] -
             base[std::to_underlying(Stat::AD)]) *
            0.5;
        return Champion::PassiveResult{bonus, true};
      }));
  // This is self-referential: final = 50 + 0.5*(final - 50) → final - 0.5*final
  // = 50 - 25
  //   0.5*final = 25 → final = 50 (converges to base, since bonus = 0 at fixed
  //   point)
  Stats r = champ.evaluateChampion(0.0001);
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(50.0));
}

TEST_CASE("applyPassives empty champion returns zeros", "[champion]") {
  Champion champ;
  Stats base = champ.getBaseStats();
  Stats r = champ.applyPassives(base, base, 0.0);
  for (std::size_t i = 0; i < std::to_underlying(Stat::Count); ++i) {
    REQUIRE(r[i] == Catch::Approx(0.0));
  }
}

// --- evaluateChampion tests ---

TEST_CASE("evaluateChampion throws ConvergenceError for non-converging passive",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  // weight 1.5 ≥ 1 → diverges
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] =
        final[std::to_underlying(Stat::AD)] * 1.5;
    return Champion::PassiveResult{bonus, true};
  }));
  REQUIRE_THROWS_AS(champ.evaluateChampion(0.01, 5), moba::ConvergenceError);
}

TEST_CASE("evaluateChampion with empty champion returns all zeros",
          "[champion]") {
  Champion champ;
  Stats r = champ.evaluateChampion();
  for (std::size_t i = 0; i < std::to_underlying(Stat::Count); ++i) {
    REQUIRE(r[i] == Catch::Approx(0.0));
  }
}

TEST_CASE("evaluateChampion with only mod_db and no passives returns base",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.mod_db.add(Stat::AD, ModType::Inc, 0.2, Source{"Item", ""});
  champ.mod_db.add(Stat::AD, ModType::More, 1.5, Source{"Item", ""});
  // 50 * 1.2 * 1.5 = 90
  Stats r = champ.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(90.0));
}

TEST_CASE("evaluateChampion includes one-shot passive bonus then consumes it",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  // one-shot gives +30 AD once
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 30.0;
    return Champion::PassiveResult{bonus, false};
  }));
  Stats r = champ.evaluateChampion();
  // 50 + 30 = 80
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(80.0));
  REQUIRE(champ.passives.empty());
}

TEST_CASE("evaluateChampion includes temp passive bonus; alive=true stays",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 25.0;
    return Champion::PassiveResult{bonus, true};
  }));
  Stats r = champ.evaluateChampion();
  // 50 + 25 = 75
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(75.0));
  // temp with alive=true stays
  REQUIRE(champ.passives.size() == 1);
}

TEST_CASE("evaluateChampion removes temp passives returning alive=false",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  // temp already expired (alive=false) — should be removed after evaluation
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 30.0;
    return Champion::PassiveResult{bonus, false};
  }));

  Stats result = champ.evaluateChampion();
  // bonus applied during iteration: 50 + 30 = 80
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(80.0));
  // temp removed because alive=false on final iteration
  REQUIRE(champ.passives.empty());
}

TEST_CASE("evaluateChampion with mixed queues prunes correctly", "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  // permanent: +10, alive=true
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, true};
  }));
  // one-shot: +100, alive=false
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 100.0;
    return Champion::PassiveResult{bonus, false};
  }));
  // temp: +200, alive=true
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 200.0;
    return Champion::PassiveResult{bonus, true};
  }));

  Stats result = champ.evaluateChampion();
  // all passives contribute: 50 + 10 (perm) + 100 (one-shot) + 200 (temp) = 360
  REQUIRE(result[std::to_underlying(Stat::AD)] == Catch::Approx(360.0));
  // one-shot removed (alive=false); perm and temp stay (alive=true)
  REQUIRE(champ.passives.size() == 2);
}

TEST_CASE(
    "evaluateChampion fixed-point includes one-shot in cross-stat dependency",
    "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.mod_db.add(Stat::MaxHP, ModType::Base, 1000.0, Source{"Base", ""});
  // one-shot gives +200 HP
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::MaxHP)] = 200.0;
    return Champion::PassiveResult{bonus, false};
  }));
  // permanent: AD += 1% of final HP
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] =
        final[std::to_underlying(Stat::MaxHP)] * 0.01;
    return Champion::PassiveResult{bonus, true};
  }));
  Stats r = champ.evaluateChampion();
  // HP: 1000 + 200 (one-shot) = 1200
  // AD: 50 + 0.01*1200 = 62
  REQUIRE(r[std::to_underlying(Stat::MaxHP)] == Catch::Approx(1200.0));
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(62.0));
  // one-shot consumed, permanent stays → 1 passive left
  REQUIRE(champ.passives.size() == 1);
}

TEST_CASE("evaluateChampion convergence with temp that depends on final",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  // temp passive: +20% of final AD, alive=true
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] =
        final[std::to_underlying(Stat::AD)] * 0.2;
    return Champion::PassiveResult{bonus, true};
  }));
  // fixed-point: final = 50 + 0.2*final → final = 50/0.8 = 62.5
  Stats r = champ.evaluateChampion(0.0001);
  REQUIRE(r[std::to_underlying(Stat::AD)] ==
          Catch::Approx(62.5).epsilon(0.001));
  // temp stays (alive=true)
  REQUIRE(champ.passives.size() == 1);
}

TEST_CASE("evaluateChampion with two cross-stat dependent passives",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.mod_db.add(Stat::MaxHP, ModType::Base, 1000.0, Source{"Base", ""});
  // passive 1: AD += 1% of final HP
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] =
        final[std::to_underlying(Stat::MaxHP)] * 0.01;
    return Champion::PassiveResult{bonus, true};
  }));
  // passive 2: HP += 0.5 * final AD
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::MaxHP)] =
        final[std::to_underlying(Stat::AD)] * 0.5;
    return Champion::PassiveResult{bonus, true};
  }));
  // System:
  //   AD = 50 + 0.01*HP
  //   HP = 1000 + 0.5*AD
  // Substitute: AD = 50 + 0.01*(1000 + 0.5*AD) = 50 + 10 + 0.005*AD
  //   AD*(1 - 0.005) = 60 → AD = 60/0.995 ≈ 60.30
  //   HP = 1000 + 0.5*60.30 ≈ 1030.15
  Stats r = champ.evaluateChampion(0.0001);
  REQUIRE(r[std::to_underlying(Stat::AD)] ==
          Catch::Approx(60.30).epsilon(0.01));
  REQUIRE(r[std::to_underlying(Stat::MaxHP)] ==
          Catch::Approx(1030.15).epsilon(0.01));
}

TEST_CASE("evaluateChampion with three dependent passives converges",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.mod_db.add(Stat::AP, ModType::Base, 50.0, Source{"Base", ""});
  champ.mod_db.add(Stat::MaxHP, ModType::Base, 1000.0, Source{"Base", ""});
  // passive 1: AD += 10% of final AP
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] =
        final[std::to_underlying(Stat::AP)] * 0.1;
    return Champion::PassiveResult{bonus, true};
  }));
  // passive 2: AP += 5% of final HP
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AP)] =
        final[std::to_underlying(Stat::MaxHP)] * 0.05;
    return Champion::PassiveResult{bonus, true};
  }));
  // passive 3: HP += 2 * final AD
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::MaxHP)] =
        final[std::to_underlying(Stat::AD)] * 2.0;
    return Champion::PassiveResult{bonus, true};
  }));
  // Should converge (weak coupling) — just verify it converges without throwing
  REQUIRE_NOTHROW(champ.evaluateChampion(0.0001, 10000));
}

TEST_CASE("evaluateChampion max_iter=1 throws if not converged in one step",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  // constant bonus: converges in 1 step (delta after step = |60 - 50| = 10 >
  // eps)
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, true};
  }));
  // one iteration: 50 + 10 = 60; delta(60, 50) = 10 > eps → ConvergenceError
  REQUIRE_THROWS_AS(champ.evaluateChampion(0.0001, 1), moba::ConvergenceError);
}

TEST_CASE("evaluateChampion max_iter large enough converges for weak passive",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make([](const Stats &, const Stats &final, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] =
        final[std::to_underlying(Stat::AD)] * 0.1;
    return Champion::PassiveResult{bonus, true};
  }));
  // fixed point 50/0.9 ≈ 55.5556
  REQUIRE_NOTHROW(champ.evaluateChampion(0.0001, 1000));
  Stats r = champ.evaluateChampion(0.0001, 1000);
  REQUIRE(r[std::to_underlying(Stat::AD)] ==
          Catch::Approx(55.5556).epsilon(0.001));
}

TEST_CASE("evaluateChampion repeated calls are stable for constant passive",
          "[champion]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AD)] = 10.0;
    return Champion::PassiveResult{bonus, true};
  }));
  Stats r1 = champ.evaluateChampion();
  Stats r2 = champ.evaluateChampion();
  REQUIRE(r1[std::to_underlying(Stat::AD)] ==
          Catch::Approx(r2[std::to_underlying(Stat::AD)]));
  REQUIRE(r1[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
}

// --- getDeltaStats tests ---

TEST_CASE("getDeltaStats is zero for two empty stats arrays", "[champion]") {
  Stats a{};
  Stats b{};
  REQUIRE(Champion::getDeltaStats(a, b) == Catch::Approx(0.0));
}

TEST_CASE("getDeltaStats with one differing element and rest same",
          "[champion]") {
  Stats a{};
  Stats b{};
  a[std::to_underlying(Stat::AD)] = 50.0;
  b[std::to_underlying(Stat::AD)] = 53.0;
  // all others zero → delta = 3
  REQUIRE(Champion::getDeltaStats(a, b) == Catch::Approx(3.0));
}

TEST_CASE("getDeltaStats is symmetric (order-independent)", "[champion]") {
  Stats a{};
  Stats b{};
  a[std::to_underlying(Stat::AD)] = 50.0;
  b[std::to_underlying(Stat::AD)] = 55.0;
  a[std::to_underlying(Stat::MaxHP)] = 1000.0;
  b[std::to_underlying(Stat::MaxHP)] = 990.0;
  REQUIRE(Champion::getDeltaStats(a, b) ==
          Catch::Approx(Champion::getDeltaStats(b, a)));
}

TEST_CASE("getDeltaStats with all stats differing returns the max",
          "[champion]") {
  Stats a{};
  Stats b{};
  a[std::to_underlying(Stat::MaxHP)] = 100.0;
  b[std::to_underlying(Stat::MaxHP)] = 110.0;
  a[std::to_underlying(Stat::AP)] = 50.0;
  b[std::to_underlying(Stat::AP)] = 56.0;
  a[std::to_underlying(Stat::AD)] = 80.0;
  b[std::to_underlying(Stat::AD)] = 85.0;
  a[std::to_underlying(Stat::MS)] = 350.0;
  b[std::to_underlying(Stat::MS)] = 365.0;
  // deltas: 10, 6, 5, 15 → max = 15
  REQUIRE(Champion::getDeltaStats(a, b) == Catch::Approx(15.0));
}

// --- getBaseStats full pipeline ---

TEST_CASE("getBaseStats with full mod_db pipeline", "[champion]") {
  Champion champ;
  Source src{"Item", ""};
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, src);
  champ.mod_db.add(Stat::AD, ModType::Base, 30.0, src);
  champ.mod_db.add(Stat::AD, ModType::Inc, 0.2, src);
  champ.mod_db.add(Stat::AD, ModType::More, 1.5, src);
  champ.mod_db.add(Stat::MaxHP, ModType::Base, 1000.0, src);
  champ.mod_db.add(Stat::AP, ModType::Base, 80.0, src);
  champ.mod_db.add(Stat::AP, ModType::Inc, 0.1, src);
  Stats base = champ.getBaseStats();
  // AD: (50+30) * 1.2 * 1.5 = 144
  REQUIRE(base[std::to_underlying(Stat::AD)] == Catch::Approx(144.0));
  // HP: 1000
  REQUIRE(base[std::to_underlying(Stat::MaxHP)] == Catch::Approx(1000.0));
  // AP: 80 * 1.1 = 88
  REQUIRE(base[std::to_underlying(Stat::AP)] == Catch::Approx(88.0));
}