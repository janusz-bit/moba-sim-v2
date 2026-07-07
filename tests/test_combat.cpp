#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "moba_sim.hpp"

using moba::Champion;
using moba::ModType;
using moba::Source;
using moba::Stat;
using moba::TypeDamage;
using Stats = Champion::Stats;
using moba::Type;

namespace {
Champion::PassiveFactory &factory() {
  static Champion::PassiveFactory f;
  return f;
}

// Build a one-shot damage passive that reduces target HP by raw*mitigation.
// `target_final` is captured by value so the lambda reads the target's final
// stats at the time the passive was created.
Champion::PassiveEntry makeDamage(Type raw, TypeDamage type, Type flat_pen,
                                  Type pct_pen, Stats target_final) {
  return factory().make([raw, type, flat_pen, pct_pen, target_final](
                            const Stats &,
                            const Stats &,
                            Type) -> Champion::PassiveResult {
    Stats bonus{};
    if (type == TypeDamage::True) {
      bonus[std::to_underlying(Stat::HP)] = -raw;
      return {bonus, false};
    }
    const Stat resist_stat =
        (type == TypeDamage::Physical) ? Stat::AR : Stat::MR;
    Type res = target_final[std::to_underlying(resist_stat)];
    res = (res - flat_pen) * (1.0 - pct_pen);
    bonus[std::to_underlying(Stat::HP)] =
        -moba::post_mitigation_damage(raw, res);
    return {bonus, false};
  });
}

// Convenience: build a champion with base stats and armor/MR.
Champion makeChampion(Type hp, Type ad, Type ar, Type mr) {
  Champion c;
  Source src{"Base", ""};
  c.mod_db.add(Stat::HP, ModType::Base, hp, src);
  c.mod_db.add(Stat::AD, ModType::Base, ad, src);
  c.mod_db.add(Stat::AR, ModType::Base, ar, src);
  c.mod_db.add(Stat::MR, ModType::Base, mr, src);
  return c;
}
} // namespace

TEST_CASE("Combat: physical damage reduced by armor", "[combat]") {
  Champion target = makeChampion(1000.0, 50.0, 100.0, 50.0);
  Stats target_base = target.getBaseStats();
  // 100 physical vs 100 armor → 50 post-mitigation
  target.addPassive(
      makeDamage(100.0, TypeDamage::Physical, 0.0, 0.0, target_base));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::HP)] == Catch::Approx(950.0));
}

TEST_CASE("Combat: magic damage reduced by MR", "[combat]") {
  Champion target = makeChampion(1000.0, 50.0, 100.0, 50.0);
  Stats target_base = target.getBaseStats();
  // 100 magic vs 50 MR → 100 * 100/150 ≈ 66.667
  target.addPassive(
      makeDamage(100.0, TypeDamage::Magic, 0.0, 0.0, target_base));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::HP)] ==
          Catch::Approx(1000.0 - 66.6667).epsilon(0.01));
}

TEST_CASE("Combat: true damage ignores resistances", "[combat]") {
  Champion target = makeChampion(1000.0, 50.0, 1000.0, 1000.0);
  Stats target_base = target.getBaseStats();
  // 100 true damage → 100 HP loss regardless of AR/MR
  target.addPassive(makeDamage(100.0, TypeDamage::True, 0.0, 0.0, target_base));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::HP)] == Catch::Approx(900.0));
}

TEST_CASE("Combat: flat armor penetration", "[combat]") {
  Champion target = makeChampion(1000.0, 50.0, 100.0, 50.0);
  Stats target_base = target.getBaseStats();
  // 100 physical, 30 flat pen → effective armor = 70 → 100*100/170 ≈ 58.82
  target.addPassive(
      makeDamage(100.0, TypeDamage::Physical, 30.0, 0.0, target_base));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::HP)] ==
          Catch::Approx(1000.0 - 58.8235).epsilon(0.01));
}

TEST_CASE("Combat: percentage armor penetration", "[combat]") {
  Champion target = makeChampion(1000.0, 50.0, 100.0, 50.0);
  Stats target_base = target.getBaseStats();
  // 100 physical, 30% pen → effective armor = 100*0.7 = 70 → 58.82
  target.addPassive(
      makeDamage(100.0, TypeDamage::Physical, 0.0, 0.3, target_base));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::HP)] ==
          Catch::Approx(1000.0 - 58.8235).epsilon(0.01));
}

TEST_CASE("Combat: flat and percentage penetration stack", "[combat]") {
  Champion target = makeChampion(1000.0, 50.0, 100.0, 50.0);
  Stats target_base = target.getBaseStats();
  // 100 physical, 30 flat + 30% pen → armor = (100-30)*0.7 = 49 → 100*100/149
  // ≈ 67.11
  target.addPassive(
      makeDamage(100.0, TypeDamage::Physical, 30.0, 0.3, target_base));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::HP)] ==
          Catch::Approx(1000.0 - 67.1141).epsilon(0.01));
}

TEST_CASE("Combat: attacker deals damage to target via passive", "[combat]") {
  Champion attacker = makeChampion(800.0, 60.0, 50.0, 50.0);
  Champion target = makeChampion(1000.0, 50.0, 100.0, 50.0);

  Stats target_final = target.evaluateChampion();
  // attacker auto-attacks: 60 physical vs target's 100 armor → 30 damage
  target.addPassive(
      makeDamage(attacker.getBaseStats()[std::to_underlying(Stat::AD)],
                 TypeDamage::Physical,
                 0.0,
                 0.0,
                 target_final));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::HP)] == Catch::Approx(970.0));
}

TEST_CASE("Combat: multiple hits in one evaluation", "[combat]") {
  Champion target = makeChampion(1000.0, 50.0, 100.0, 50.0);
  Stats target_base = target.getBaseStats();
  // 3 separate one-shot damage passives: 100 physical each → 50 each → 150
  // total
  for (int i = 0; i < 3; ++i) {
    target.addPassive(
        makeDamage(100.0, TypeDamage::Physical, 0.0, 0.0, target_base));
  }
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::HP)] == Catch::Approx(850.0));
  REQUIRE(target.passives.empty());
}

TEST_CASE("Combat: mixed damage types in one evaluation", "[combat]") {
  Champion target = makeChampion(1000.0, 50.0, 100.0, 50.0);
  Stats target_base = target.getBaseStats();
  // 100 physical (→50), 100 magic (→66.67), 100 true (→100) = 216.67 total
  target.addPassive(
      makeDamage(100.0, TypeDamage::Physical, 0.0, 0.0, target_base));
  target.addPassive(
      makeDamage(100.0, TypeDamage::Magic, 0.0, 0.0, target_base));
  target.addPassive(makeDamage(100.0, TypeDamage::True, 0.0, 0.0, target_base));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::HP)] ==
          Catch::Approx(1000.0 - 50.0 - 66.6667 - 100.0).epsilon(0.01));
}

TEST_CASE("Combat: lifesteal heals attacker for fraction of damage",
          "[combat]") {
  Champion attacker = makeChampion(800.0, 60.0, 50.0, 50.0);
  Champion target = makeChampion(1000.0, 50.0, 100.0, 50.0);

  Stats target_final = target.evaluateChampion();
  Type dealt = moba::post_mitigation_damage(
      attacker.getBaseStats()[std::to_underlying(Stat::AD)],
      100.0);

  // attacker deals damage to target
  target.addPassive(
      makeDamage(attacker.getBaseStats()[std::to_underlying(Stat::AD)],
                 TypeDamage::Physical,
                 0.0,
                 0.0,
                 target_final));
  (void)target.evaluateChampion();

  // attacker heals 20% of damage dealt
  attacker.addPassive(
      factory().make([dealt](const Stats &, const Stats &, Type) {
        Stats bonus{};
        bonus[std::to_underlying(Stat::HP)] = dealt * 0.2;
        return Champion::PassiveResult{bonus, false};
      }));
  Stats a = attacker.evaluateChampion();
  REQUIRE(a[std::to_underlying(Stat::HP)] ==
          Catch::Approx(800.0 + dealt * 0.2));
}

TEST_CASE("Combat: damage passive is one-shot and consumed", "[combat]") {
  Champion target = makeChampion(1000.0, 50.0, 100.0, 50.0);
  Stats target_base = target.getBaseStats();
  target.addPassive(
      makeDamage(100.0, TypeDamage::Physical, 0.0, 0.0, target_base));
  Stats first = target.evaluateChampion();
  REQUIRE(first[std::to_underlying(Stat::HP)] == Catch::Approx(950.0));
  REQUIRE(target.passives.empty());
  // re-evaluate: no more damage passive
  Stats second = target.evaluateChampion();
  REQUIRE(second[std::to_underlying(Stat::HP)] == Catch::Approx(1000.0));
}

TEST_CASE("Combat: negative armor amplifies damage", "[combat]") {
  Champion target = makeChampion(1000.0, 50.0, -50.0, 50.0);
  Stats target_base = target.getBaseStats();
  // 100 physical vs -50 armor → 100 * (2 - 100/150) ≈ 133.33
  target.addPassive(
      makeDamage(100.0, TypeDamage::Physical, 0.0, 0.0, target_base));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::HP)] ==
          Catch::Approx(1000.0 - 133.3333).epsilon(0.01));
}

TEST_CASE("Combat: target dies when damage exceeds HP", "[combat]") {
  Champion target = makeChampion(100.0, 50.0, 100.0, 50.0);
  Stats target_base = target.getBaseStats();
  // 1000 true damage → HP goes negative
  target.addPassive(
      makeDamage(1000.0, TypeDamage::True, 0.0, 0.0, target_base));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::HP)] <= 0.0);
  REQUIRE(r[std::to_underlying(Stat::HP)] == Catch::Approx(-900.0));
}

TEST_CASE("Combat: DoT via temp passive over time", "[combat]") {
  Champion target = makeChampion(1000.0, 50.0, 100.0, 50.0);
  Type per_tick = 50.0; // 50 true damage per tick

  // burn: 50 true damage per second for 3 seconds (ticks at t=0,1,2).
  // The passive accumulates total damage in its captured state and returns
  // the full accumulated amount as bonus each call, so result = base - total.
  target.addPassive(factory().make(
      [per_tick,
       start = 0.0,
       duration = 3.0,
       next_tick = 0.0,
       accumulated = 0.0](const Stats &, const Stats &, Type time) mutable {
        if (time >= next_tick && time < start + duration) {
          accumulated += per_tick;
          next_tick = time + 1.0;
        }
        Stats bonus{};
        bonus[std::to_underlying(Stat::HP)] = -accumulated;
        return Champion::PassiveResult{bonus, time < start + duration};
      }));

  Stats base = target.getBaseStats();
  // t=0: -50
  Stats t0 = target.applyPassives(base, base, 0.0);
  REQUIRE(t0[std::to_underlying(Stat::HP)] == Catch::Approx(950.0));
  // t=1: -100 total
  Stats t1 = target.applyPassives(base, base, 1.0);
  REQUIRE(t1[std::to_underlying(Stat::HP)] == Catch::Approx(900.0));
  // t=2: -150 total (still alive at t=2 since 2 < 3)
  Stats t2 = target.applyPassives(base, base, 2.0);
  REQUIRE(t2[std::to_underlying(Stat::HP)] == Catch::Approx(850.0));
  // t=3: expires (3 < 3 false), bonus still applied then removed
  Stats t3 = target.applyPassives(base, base, 3.0);
  REQUIRE(t3[std::to_underlying(Stat::HP)] == Catch::Approx(850.0));
  REQUIRE(target.passives.empty());
}

TEST_CASE("Combat: two champions trade damage", "[combat]") {
  Champion a = makeChampion(800.0, 60.0, 50.0, 50.0);
  Champion b = makeChampion(1000.0, 40.0, 80.0, 60.0);

  Stats a_final = a.evaluateChampion();
  Stats b_final = b.evaluateChampion();

  // a hits b: 60 physical vs 80 armor → 60*100/180 = 33.33
  b.addPassive(makeDamage(a_final[std::to_underlying(Stat::AD)],
                          TypeDamage::Physical,
                          0.0,
                          0.0,
                          b_final));
  // b hits a: 40 physical vs 50 armor → 40*100/150 = 26.67
  a.addPassive(makeDamage(b_final[std::to_underlying(Stat::AD)],
                          TypeDamage::Physical,
                          0.0,
                          0.0,
                          a_final));

  Stats a_after = a.evaluateChampion();
  Stats b_after = b.evaluateChampion();

  REQUIRE(a_after[std::to_underlying(Stat::HP)] ==
          Catch::Approx(800.0 - 26.6667).epsilon(0.01));
  REQUIRE(b_after[std::to_underlying(Stat::HP)] ==
          Catch::Approx(1000.0 - 33.3333).epsilon(0.01));
}

TEST_CASE("Combat: armor shred debuff then damage", "[combat]") {
  Champion target = makeChampion(1000.0, 50.0, 100.0, 50.0);
  Stats target_base = target.getBaseStats();

  // First: armor shred (-30 armor) as permanent debuff
  target.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    Stats bonus{};
    bonus[std::to_underlying(Stat::AR)] = -30.0;
    return Champion::PassiveResult{bonus, true};
  }));
  // After shred: 70 armor → 100*100/170 ≈ 58.82
  Stats shredded = target.evaluateChampion();
  REQUIRE(shredded[std::to_underlying(Stat::AR)] == Catch::Approx(70.0));

  // Now apply damage with the shredded armor
  target.addPassive(
      makeDamage(100.0, TypeDamage::Physical, 0.0, 0.0, shredded));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::HP)] ==
          Catch::Approx(1000.0 - 58.8235).epsilon(0.01));
}