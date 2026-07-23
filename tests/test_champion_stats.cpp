#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "moba_sim.hpp"

using moba::Champion;
using moba::ModType;
using moba::Source;
using moba::Stat;
using moba::Type;
using Stats = Champion::Stats;

namespace {
Champion::PassiveFactory &factory() {
  static Champion::PassiveFactory f;
  return f;
}
} // namespace

// --- Life steal ---

TEST_CASE("LifeSteal stat in mod_db flows through pipeline", "[lifesteal]") {
  Champion champ;
  champ.mod_db.add(Stat::LifeSteal, ModType::Base, 0.12, Source{"Item", ""});
  Stats base = champ.getBaseStats();
  REQUIRE(base[std::to_underlying(Stat::LifeSteal)] == Catch::Approx(0.12));
}

TEST_CASE("LifeSteal stacks additively from multiple sources", "[lifesteal]") {
  Champion champ;
  Source item{"Item", ""};
  champ.mod_db.add(Stat::LifeSteal, ModType::Base, 0.07, item);
  champ.mod_db.add(Stat::LifeSteal, ModType::Base, 0.05, Source{"Rune", ""});
  Stats base = champ.getBaseStats();
  REQUIRE(base[std::to_underlying(Stat::LifeSteal)] == Catch::Approx(0.12));
}

TEST_CASE("LifeSteal via passive Inc mod", "[lifesteal]") {
  Champion champ;
  champ.mod_db.add(Stat::LifeSteal, ModType::Base, 0.05, Source{"Item", ""});
  champ.addPassive(
      factory().make([](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{
            {{Stat::LifeSteal, ModType::Base, 0.05, {}}},
            true};
      }));
  Stats r = champ.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::LifeSteal)] == Catch::Approx(0.10));
}

// --- Omnivamp ---

TEST_CASE("Omnivamp stat in mod_db", "[omnivamp]") {
  Champion champ;
  champ.mod_db.add(Stat::Omnivamp, ModType::Base, 0.15, Source{"Item", ""});
  Stats base = champ.getBaseStats();
  REQUIRE(base[std::to_underlying(Stat::Omnivamp)] == Catch::Approx(0.15));
}

// --- Tenacity ---

TEST_CASE("Tenacity stat in mod_db", "[tenacity]") {
  Champion champ;
  champ.mod_db.add(Stat::Tenacity, ModType::Base, 0.30, Source{"Item", ""});
  Stats base = champ.getBaseStats();
  REQUIRE(base[std::to_underlying(Stat::Tenacity)] == Catch::Approx(0.30));
}

TEST_CASE(
    "Tenacity from Mercury's Treads + Sterak's Gage stacks multiplicatively",
    "[tenacity]") {
  // Group A: Mercury's (30%) * Sterak's (20%) = 1 - (0.7 * 0.8) = 0.44
  Champion champ;
  champ.mod_db.add(Stat::Tenacity, ModType::Base, 0.30, Source{"Mercury", ""});
  champ.mod_db.add(Stat::Tenacity,
                   ModType::More,
                   0.80,
                   Source{"Sterak", ""}); // *0.8 as More
  // This models: tenacity = 0.30 * 0.80 (multiplicative within group)
  // via Base + More pipeline: 0.30 * 1.0 * 0.80 = 0.24
  // NOTE: real LoL uses group system; this is a simplified model
  Stats base = champ.getBaseStats();
  REQUIRE(base[std::to_underlying(Stat::Tenacity)] == Catch::Approx(0.24));
}

// --- Slow resist ---

TEST_CASE("SlowResist stat in mod_db", "[slowresist]") {
  Champion champ;
  champ.mod_db.add(Stat::SlowResist, ModType::Base, 0.20, Source{"Rune", ""});
  Stats base = champ.getBaseStats();
  REQUIRE(base[std::to_underlying(Stat::SlowResist)] == Catch::Approx(0.20));
}

// --- Attack speed ---

TEST_CASE("AttackSpeed stat in mod_db", "[attackspeed]") {
  Champion champ;
  champ.mod_db.add(Stat::AttackSpeed, ModType::Base, 0.651, Source{"Base", ""});
  champ.mod_db.add(Stat::AttackSpeed,
                   ModType::Inc,
                   0.5,
                   Source{"Item", ""}); // +50%
  Stats base = champ.getBaseStats();
  // 0.651 * (1 + 0.5) * 1.0 = 0.9765
  REQUIRE(base[std::to_underlying(Stat::AttackSpeed)] ==
          Catch::Approx(0.9765).epsilon(0.001));
}

// --- Critical strike ---

TEST_CASE("CritChance stat in mod_db", "[crit]") {
  Champion champ;
  champ.mod_db.add(Stat::CritChance, ModType::Base, 0.25, Source{"Item", ""});
  Stats base = champ.getBaseStats();
  REQUIRE(base[std::to_underlying(Stat::CritChance)] == Catch::Approx(0.25));
}

TEST_CASE("CritDamage stat in mod_db", "[crit]") {
  Champion champ;
  champ.mod_db.add(Stat::CritDamage, ModType::Base, 1.75, Source{"Base", ""});
  champ.mod_db.add(Stat::CritDamage,
                   ModType::Inc,
                   0.40,
                   Source{"Item", ""}); // +40% Inc → 1.75 * 1.40 = 2.45
  Stats base = champ.getBaseStats();
  REQUIRE(base[std::to_underlying(Stat::CritDamage)] ==
          Catch::Approx(2.45).epsilon(0.001));
}

// --- Heal and shield power ---

TEST_CASE("HealShieldPower stat in mod_db", "[healshield]") {
  Champion champ;
  champ.mod_db.add(Stat::HealShieldPower,
                   ModType::Base,
                   0.15,
                   Source{"Item", ""});
  Stats base = champ.getBaseStats();
  REQUIRE(base[std::to_underlying(Stat::HealShieldPower)] ==
          Catch::Approx(0.15));
}

// --- HP/MP regen ---

TEST_CASE("HPRegen stat in mod_db", "[regen]") {
  Champion champ;
  champ.mod_db.add(Stat::HPRegen, ModType::Base, 8.5, Source{"Base", ""});
  Stats base = champ.getBaseStats();
  REQUIRE(base[std::to_underlying(Stat::HPRegen)] == Catch::Approx(8.5));
}

TEST_CASE("MPRegen stat in mod_db", "[regen]") {
  Champion champ;
  champ.mod_db.add(Stat::MPRegen, ModType::Base, 11.0, Source{"Base", ""});
  Stats base = champ.getBaseStats();
  REQUIRE(base[std::to_underlying(Stat::MPRegen)] == Catch::Approx(11.0));
}

// --- Integration: lifesteal in a combat passive ---

TEST_CASE("Combat: attacker heals via LifeSteal stat", "[lifesteal][combat]") {
  Champion attacker{{Stat::MaxHP, 800},
                    {Stat::CurrentHP, 800},
                    {Stat::AD, 60},
                    {Stat::AR, 50},
                    {Stat::MR, 50},
                    {Stat::LifeSteal, 0.12}};
  Champion target{{Stat::MaxHP, 1000},
                  {Stat::CurrentHP, 1000},
                  {Stat::AD, 50},
                  {Stat::AR, 100},
                  {Stat::MR, 50}};

  Stats atk_base = attacker.getBaseStats();
  Stats tgt_base = target.getBaseStats();

  // Auto-attack: 60 physical vs 100 AR → 30 post-mitigation
  Type dealt = moba::mitigated_damage(atk_base[std::to_underlying(Stat::AD)],
                                      moba::TypeDamage::Physical,
                                      tgt_base);

  // Lifesteal heal: 30 * 0.12 = 3.6
  Type heal = dealt * atk_base[std::to_underlying(Stat::LifeSteal)];

  REQUIRE(dealt == Catch::Approx(30.0));
  REQUIRE(heal == Catch::Approx(3.6));

  // Apply damage to target
  target.addPassive(factory().make(
      [dealt](const Stats &, const Stats &, const Type &, const auto &) {
        return Champion::PassiveResult{
            {{moba::Stat::CurrentHP, moba::ModType::Base, -dealt, {}}},
            false};
      }));

  // Apply heal to attacker
  attacker.addPassive(factory().make(
      [heal](const Stats &, const Stats &, const Type &, const auto &) {
        return Champion::PassiveResult{
            {{moba::Stat::CurrentHP, moba::ModType::Base, heal, {}}},
            false};
      }));

  Stats tgt_after = target.evaluateChampion();
  Stats atk_after = attacker.evaluateChampion();

  REQUIRE(tgt_after[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(970.0));
  REQUIRE(atk_after[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(803.6));
}

// --- Integration: tenacity reduces CC in simulation ---

TEST_CASE("Combat: tenacity reduces stun duration in simulation",
          "[tenacity][combat]") {
  Champion champ;
  champ.mod_db.add(Stat::Tenacity, ModType::Base, 0.30, Source{"Mercury", ""});

  Stats base = champ.getBaseStats();
  Type tenacity = base[std::to_underlying(Stat::Tenacity)];

  // 2.0s stun reduced by 30% tenacity → 1.4s
  Type reduced = 2.0 * (1.0 - tenacity);
  REQUIRE(reduced == Catch::Approx(1.4));
}

// ===========================================================================
// EDGE CASE TESTS — exercising extreme/corner inputs
// ===========================================================================

// --- mitigated_damage: extreme penetration ---

TEST_CASE("mitigated_damage with 100% penetration ignores all resistance",
          "[mitigated][edge]") {
  Champion::Stats target{};
  target[std::to_underlying(Stat::AR)] = 1000.0;
  // 100% pen → effective AR = (1000 - 0) * (1 - 1.0) = 0 → full damage
  REQUIRE(moba::mitigated_damage(100.0,
                                 moba::TypeDamage::Physical,
                                 target,
                                 0.0,
                                 1.0) == Catch::Approx(100.0));
}

TEST_CASE("mitigated_damage with flat pen exceeding resistance → negative AR",
          "[mitigated][edge]") {
  Champion::Stats target{};
  target[std::to_underlying(Stat::AR)] = 50.0;
  // flat pen=100 → effective AR = (50 - 100) * 1.0 = -50 → amplified damage
  // 100 * (2 - 100/150) = 100 * 1.333 = 133.33
  REQUIRE(moba::mitigated_damage(100.0,
                                 moba::TypeDamage::Physical,
                                 target,
                                 100.0,
                                 0.0) == Catch::Approx(133.3333).epsilon(0.01));
}

TEST_CASE("mitigated_damage with both pen types stacking",
          "[mitigated][edge]") {
  Champion::Stats target{};
  target[std::to_underlying(Stat::AR)] = 200.0;
  // flat=50, pct=0.5 → AR = (200-50) * 0.5 = 75 → 100*100/175 ≈ 57.14
  REQUIRE(moba::mitigated_damage(100.0,
                                 moba::TypeDamage::Physical,
                                 target,
                                 50.0,
                                 0.5) == Catch::Approx(57.1429).epsilon(0.01));
}

TEST_CASE("mitigated_damage true damage ignores penetration too",
          "[mitigated][edge]") {
  Champion::Stats target{};
  target[std::to_underlying(Stat::AR)] = 10000.0;
  // True damage: penetration values are irrelevant
  REQUIRE(moba::mitigated_damage(100.0,
                                 moba::TypeDamage::True,
                                 target,
                                 9999.0,
                                 0.99) == Catch::Approx(100.0));
}

TEST_CASE("mitigated_damage with zero raw returns zero", "[mitigated][edge]") {
  Champion::Stats target{};
  target[std::to_underlying(Stat::AR)] = 100.0;
  REQUIRE(moba::mitigated_damage(0.0, moba::TypeDamage::Physical, target) ==
          Catch::Approx(0.0));
}

TEST_CASE("mitigated_damage with negative raw stays negative",
          "[mitigated][edge]") {
  Champion::Stats target{};
  target[std::to_underlying(Stat::AR)] = 100.0;
  REQUIRE(moba::mitigated_damage(-100.0, moba::TypeDamage::Physical, target) ==
          Catch::Approx(-50.0));
}

// --- ModDB: extreme stacking ---

TEST_CASE("ModDB: 1000 Base modifiers stack correctly", "[mod_db][edge]") {
  moba::ModDB db;
  moba::Source src{"Test", ""};
  for (int i = 0; i < 1000; ++i) {
    db.add(moba::Stat::AD, moba::ModType::Base, 1.0, src);
  }
  REQUIRE(db.getSumStat(moba::Stat::AD) == Catch::Approx(1000.0));
  REQUIRE(db.getStat(moba::Stat::AD) == Catch::Approx(1000.0));
}

TEST_CASE("ModDB: 1000 Inc modifiers compound", "[mod_db][edge]") {
  moba::ModDB db;
  moba::Source src{"Test", ""};
  for (int i = 0; i < 1000; ++i) {
    db.add(moba::Stat::AD, moba::ModType::Inc, 0.001, src);
  }
  // inc = 1.0 + 1000 * 0.001 = 2.0
  REQUIRE(db.getIncStat(moba::Stat::AD) == Catch::Approx(2.0));
}

TEST_CASE("ModDB: 1000 More modifiers multiply", "[mod_db][edge]") {
  moba::ModDB db;
  moba::Source src{"Test", ""};
  for (int i = 0; i < 1000; ++i) {
    db.add(moba::Stat::AD, moba::ModType::More, 1.001, src);
  }
  // more = 1.001^1000 ≈ 2.7169 (e^(1000*ln(1.001)))
  REQUIRE(db.getMoreStat(moba::Stat::AD) ==
          Catch::Approx(2.7169).epsilon(0.01));
}

TEST_CASE("ModDB: More modifier of 0.0 zeroes the stat", "[mod_db][edge]") {
  moba::ModDB db;
  moba::Source src{"Test", ""};
  db.add(moba::Stat::AD, moba::ModType::Base, 1000.0, src);
  db.add(moba::Stat::AD, moba::ModType::More, 0.0, src);
  db.add(moba::Stat::AD, moba::ModType::More, 2.0, src);
  // 0.0 * 2.0 = 0.0 → stat is zeroed
  REQUIRE(db.getMoreStat(moba::Stat::AD) == Catch::Approx(0.0));
  REQUIRE(db.getStat(moba::Stat::AD) == Catch::Approx(0.0));
}

TEST_CASE("ModDB: Inc modifier of -1.0 zeroes the stat", "[mod_db][edge]") {
  moba::ModDB db;
  moba::Source src{"Test", ""};
  db.add(moba::Stat::AD, moba::ModType::Base, 1000.0, src);
  db.add(moba::Stat::AD, moba::ModType::Inc, -1.0, src);
  // inc = 1.0 - 1.0 = 0.0 → stat is zeroed
  REQUIRE(db.getIncStat(moba::Stat::AD) == Catch::Approx(0.0));
  REQUIRE(db.getStat(moba::Stat::AD) == Catch::Approx(0.0));
}

TEST_CASE("ModDB: Inc < -1.0 makes stat negative (inversion)",
          "[mod_db][edge]") {
  moba::ModDB db;
  moba::Source src{"Test", ""};
  db.add(moba::Stat::AD, moba::ModType::Base, 100.0, src);
  db.add(moba::Stat::AD, moba::ModType::Inc, -1.5, src);
  // inc = 1.0 - 1.5 = -0.5 → stat = 100 * -0.5 = -50
  REQUIRE(db.getIncStat(moba::Stat::AD) == Catch::Approx(-0.5));
  REQUIRE(db.getStat(moba::Stat::AD) == Catch::Approx(-50.0));
}

// --- evaluateChampion: convergence edge cases ---

TEST_CASE("evaluateChampion converges with oscillating passive",
          "[champion][edge]") {
  // Passive that oscillates: +10 on even iterations, -10 on odd
  // Uses mutable counter to track iteration
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make(
      [iter = 0](const Stats &, const Stats &, Type, const auto &) mutable {
        Stats bonus{};
        Type val = (iter % 2 == 0) ? 10.0 : -10.0;
        bonus[std::to_underlying(Stat::AD)] = val;
        ++iter;
        return Champion::PassiveResult{{{Stat::AD, ModType::Base, val, {}}},
                                       true};
      }));
  // This won't converge (oscillates between 60 and 40) → should throw
  REQUIRE_THROWS_AS(champ.evaluateChampion(0.001, 100), moba::ConvergenceError);
}

TEST_CASE("evaluateChampion with passive returning empty mods",
          "[champion][edge]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(
      factory().make([](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{{}, true}; // empty mods, permanent
      }));
  Stats r = champ.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(50.0));
  REQUIRE(champ.passives.size() == 1); // permanent stays
}

TEST_CASE("evaluateChampion with 100 passives all adding +1 AD",
          "[champion][edge]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  for (int i = 0; i < 100; ++i) {
    champ.addPassive(
        factory().make([](const Stats &, const Stats &, Type, const auto &) {
          return Champion::PassiveResult{{{Stat::AD, ModType::Base, 1.0, {}}},
                                         true};
        }));
  }
  Stats r = champ.evaluateChampion();
  // 50 + 100 * 1 = 150
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(150.0));
  REQUIRE(champ.passives.size() == 100);
}

TEST_CASE("evaluateChampion max_iter=0 throws immediately",
          "[champion][edge]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(
      factory().make([](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{{{Stat::AD, ModType::Base, 10.0, {}}},
                                       true};
      }));
  // max_iter=0 → do-while runs once, then iter >= max_iter → check delta
  // delta = |60 - 50| = 10 > eps → throw
  REQUIRE_THROWS_AS(champ.evaluateChampion(0.001, 0), moba::ConvergenceError);
}

TEST_CASE("evaluateChampion with eps larger than delta converges",
          "[champion][edge]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(
      factory().make([](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{{{Stat::AD, ModType::Base, 10.0, {}}},
                                       true};
      }));
  // delta after 1 iter = 10; eps=20 → 10 < 20 → converges immediately
  Stats r = champ.evaluateChampion(20.0, 100);
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
}

TEST_CASE("evaluateChampion with NaN-producing passive throws or is caught",
          "[champion][edge]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  // Passive that returns NaN — should cause convergence issues
  champ.addPassive(
      factory().make([](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{{{Stat::AD,
                                         ModType::Base,
                                         std::numeric_limits<Type>::quiet_NaN(),
                                         {}}},
                                       true};
      }));
  // NaN comparisons are always false, so delta check (NaN > eps) is false
  // → "converges" immediately with NaN result. Just verify no crash.
  REQUIRE_NOTHROW(champ.evaluateChampion(0.001, 100));
}

// --- applyPassives: edge cases ---

TEST_CASE("applyPassives with 100 one-shot passives all consumed in one call",
          "[champion][edge]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  for (int i = 0; i < 100; ++i) {
    champ.addPassive(
        factory().make([](const Stats &, const Stats &, Type, const auto &) {
          return Champion::PassiveResult{{{Stat::AD, ModType::Base, 1.0, {}}},
                                         false};
        }));
  }
  Stats base = champ.getBaseStats();
  Stats r = champ.applyPassives(base, base);
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(150.0));
  REQUIRE(champ.passives.empty());
}

TEST_CASE(
    "applyPassives with passive returning multiple mods for different stats",
    "[champion][edge]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.mod_db.add(Stat::MaxHP, ModType::Base, 1000.0, Source{"Base", ""});
  champ.addPassive(
      factory().make([](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{{{Stat::AD, ModType::Base, 10.0, {}},
                                        {Stat::MaxHP, ModType::Base, 200.0, {}},
                                        {Stat::AR, ModType::Base, 30.0, {}}},
                                       true};
      }));
  Stats base = champ.getBaseStats();
  Stats r = champ.applyPassives(base, base);
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
  REQUIRE(r[std::to_underlying(Stat::MaxHP)] == Catch::Approx(1200.0));
  REQUIRE(r[std::to_underlying(Stat::AR)] == Catch::Approx(30.0));
}

TEST_CASE("applyPassives temp passive with huge duration stays alive",
          "[champion][edge]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make(
      [duration = 1e9](const Stats &, const Stats &, Type time, const auto &) {
        return Champion::PassiveResult{{{Stat::AD, ModType::Base, 10.0, {}}},
                                       time < duration};
      }));
  Stats base = champ.getBaseStats();
  champ.applyPassives(base, base, 1e6);
  REQUIRE(champ.passives.size() == 1);
}

TEST_CASE("applyPassives temp passive with zero duration expires immediately",
          "[champion][edge]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make(
      [start = 0.0,
       duration = 0.0](const Stats &, const Stats &, Type time, const auto &) {
        return Champion::PassiveResult{{{Stat::AD, ModType::Base, 10.0, {}}},
                                       time - start < duration};
      }));
  Stats base = champ.getBaseStats();
  // At t=0: 0 < 0 is false → alive=false → applied then removed
  Stats r = champ.applyPassives(base, base, 0.0);
  REQUIRE(r[std::to_underlying(Stat::AD)] == Catch::Approx(60.0));
  REQUIRE(champ.passives.empty());
}

// --- Passive factory: edge cases ---

TEST_CASE("PassiveFactory auto-increments ids", "[champion][edge]") {
  Champion::PassiveFactory f;
  auto e1 = f.make([](const Stats &, const Stats &, Type, const auto &) {
    return Champion::PassiveResult{{}, true};
  });
  auto e2 = f.make([](const Stats &, const Stats &, Type, const auto &) {
    return Champion::PassiveResult{{}, true};
  });
  REQUIRE(e1.id != e2.id);
  REQUIRE(e1.id == 0);
  REQUIRE(e2.id == 1);
}

TEST_CASE("addPassive refresh with same id replaces passive",
          "[champion][edge]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  auto passive1 = [](const Stats &, const Stats &, Type, const auto &) {
    return Champion::PassiveResult{{{Stat::AD, ModType::Base, 10.0, {}}}, true};
  };
  auto passive2 = [](const Stats &, const Stats &, Type, const auto &) {
    return Champion::PassiveResult{{{Stat::AD, ModType::Base, 20.0, {}}}, true};
  };
  champ.addPassive(Champion::PassiveEntry{42, passive1});
  REQUIRE(champ.passives.size() == 1);

  // Refresh with +20 instead of +10 (same id = 42)
  champ.addPassive(Champion::PassiveEntry{42, passive2}); // refresh
  REQUIRE(champ.passives.size() == 1);                    // still 1, replaced
  Stats base = champ.getBaseStats();
  Stats r = champ.applyPassives(base, base);
  REQUIRE(r[std::to_underlying(Stat::AD)] ==
          Catch::Approx(70.0)); // 50 + 20, not 50 + 10
}

// --- Champion: all stats at once ---

TEST_CASE("Champion with all stats set evaluates correctly",
          "[champion][edge]") {
  Champion champ{{Stat::MaxHP, 2000},      {Stat::CurrentHP, 2000},
                 {Stat::Mana, 500},        {Stat::CurrentMana, 500},
                 {Stat::AP, 100},          {Stat::AD, 80},
                 {Stat::MS, 340},          {Stat::AR, 60},
                 {Stat::MR, 40},           {Stat::CDR, 20},
                 {Stat::ArmorPenFlat, 10}, {Stat::ArmorPenPct, 0.2},
                 {Stat::MagicPenFlat, 15}, {Stat::MagicPenPct, 0.1},
                 {Stat::AttackSpeed, 0.8}, {Stat::CritChance, 0.25},
                 {Stat::CritDamage, 1.75}, {Stat::LifeSteal, 0.12},
                 {Stat::Omnivamp, 0.08},   {Stat::Tenacity, 0.30},
                 {Stat::SlowResist, 0.15}, {Stat::HealShieldPower, 0.10},
                 {Stat::HPRegen, 15},      {Stat::MPRegen, 10},
                 {Stat::ShieldHP, 200}};

  Stats base = champ.getBaseStats();
  // Verify every stat individually
  REQUIRE(base[std::to_underlying(Stat::MaxHP)] == Catch::Approx(2000.0));
  REQUIRE(base[std::to_underlying(Stat::CurrentHP)] == Catch::Approx(2000.0));
  REQUIRE(base[std::to_underlying(Stat::Mana)] == Catch::Approx(500.0));
  REQUIRE(base[std::to_underlying(Stat::CurrentMana)] == Catch::Approx(500.0));
  REQUIRE(base[std::to_underlying(Stat::AP)] == Catch::Approx(100.0));
  REQUIRE(base[std::to_underlying(Stat::AD)] == Catch::Approx(80.0));
  REQUIRE(base[std::to_underlying(Stat::MS)] == Catch::Approx(340.0));
  REQUIRE(base[std::to_underlying(Stat::AR)] == Catch::Approx(60.0));
  REQUIRE(base[std::to_underlying(Stat::MR)] == Catch::Approx(40.0));
  REQUIRE(base[std::to_underlying(Stat::CDR)] == Catch::Approx(20.0));
  REQUIRE(base[std::to_underlying(Stat::ArmorPenFlat)] == Catch::Approx(10.0));
  REQUIRE(base[std::to_underlying(Stat::ArmorPenPct)] == Catch::Approx(0.2));
  REQUIRE(base[std::to_underlying(Stat::MagicPenFlat)] == Catch::Approx(15.0));
  REQUIRE(base[std::to_underlying(Stat::MagicPenPct)] == Catch::Approx(0.1));
  REQUIRE(base[std::to_underlying(Stat::AttackSpeed)] == Catch::Approx(0.8));
  REQUIRE(base[std::to_underlying(Stat::CritChance)] == Catch::Approx(0.25));
  REQUIRE(base[std::to_underlying(Stat::CritDamage)] == Catch::Approx(1.75));
  REQUIRE(base[std::to_underlying(Stat::LifeSteal)] == Catch::Approx(0.12));
  REQUIRE(base[std::to_underlying(Stat::Omnivamp)] == Catch::Approx(0.08));
  REQUIRE(base[std::to_underlying(Stat::Tenacity)] == Catch::Approx(0.30));
  REQUIRE(base[std::to_underlying(Stat::SlowResist)] == Catch::Approx(0.15));
  REQUIRE(base[std::to_underlying(Stat::HealShieldPower)] ==
          Catch::Approx(0.10));
  REQUIRE(base[std::to_underlying(Stat::HPRegen)] == Catch::Approx(15.0));
  REQUIRE(base[std::to_underlying(Stat::MPRegen)] == Catch::Approx(10.0));
  REQUIRE(base[std::to_underlying(Stat::ShieldHP)] == Catch::Approx(200.0));
}

// --- Numeric stability ---

TEST_CASE("evaluateChampion is numerically stable across repeated calls",
          "[champion][edge]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(
      factory().make([](const Stats &, const Stats &final, Type, const auto &) {
        return Champion::PassiveResult{
            {{Stat::AD,
              ModType::Base,
              final[std::to_underlying(Stat::AD)] * 0.1,
              {}}},
            true};
      }));
  // Fixed point: 50/0.9 ≈ 55.5556
  Stats r1 = champ.evaluateChampion(0.0001);
  for (int i = 0; i < 100; ++i) {
    Stats r = champ.evaluateChampion(0.0001);
    REQUIRE(r[std::to_underlying(Stat::AD)] ==
            Catch::Approx(r1[std::to_underlying(Stat::AD)]).epsilon(0.001));
  }
}

TEST_CASE("post_mitigation_damage near armor=0 is continuous", "[edge]") {
  // At armor=0, both branches should give ~100
  REQUIRE(moba::post_mitigation_damage(100.0, 0.001) ==
          Catch::Approx(99.9).epsilon(0.01));
  REQUIRE(moba::post_mitigation_damage(100.0, -0.001) ==
          Catch::Approx(100.1).epsilon(0.01));
}

TEST_CASE("post_mitigation_damage at armor=-100 boundary is exactly 150",
          "[edge]") {
  // armor=-100: 100 * (2 - 100/(100-(-100))) = 100 * (2 - 0.5) = 150
  REQUIRE(moba::post_mitigation_damage(100.0, -100.0) == Catch::Approx(150.0));
}

// --- Source origin field ---

TEST_CASE("Source default-constructed has empty origin", "[source]") {
  Source s;
  REQUIRE(s.name.empty());
  REQUIRE(s.description.empty());
  REQUIRE(s.origin().empty());
}

TEST_CASE("Source three-arg ctor sets origin", "[source]") {
  Source s{"Item", "Bloodthirster", "attacker"};
  REQUIRE(s.name == "Item");
  REQUIRE(s.description == "Bloodthirster");
  REQUIRE(s.origin() == "attacker");
}

TEST_CASE("Source equality compares all three fields", "[source]") {
  Source a{"Item", "desc", "attacker"};
  Source b{"Item", "desc", "attacker"};
  Source c{"Item", "desc", "defender"};
  REQUIRE(a == b);
  REQUIRE(a != c);
}

// --- PassiveEntry source ---

TEST_CASE("PassiveEntry default source is empty", "[passive][source]") {
  Champion::PassiveFactory f;
  auto e = f.make([](const Stats &, const Stats &, Type, const auto &) {
    return Champion::PassiveResult{{}, true};
  });
  REQUIRE(e.source.name.empty());
  REQUIRE(e.source.origin().empty());
}

TEST_CASE("PassiveEntry with source stores it", "[passive][source]") {
  Champion::PassiveFactory f;
  auto e = f.make(
      [](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{{}, true};
      },
      Source{"Item", "Bloodthirster", "attacker"});
  REQUIRE(e.source.name == "Item");
  REQUIRE(e.source.description == "Bloodthirster");
  REQUIRE(e.source.origin() == "attacker");
}

TEST_CASE("addPassive stores source on passive entry", "[passive][source]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make(
      [](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{{{Stat::AD, ModType::Base, 10.0, {}}},
                                       true};
      },
      Source{"Ability", "Courage", "attacker"}));
  REQUIRE(champ.passives.size() == 1);
  REQUIRE(champ.passives[0].source.name == "Ability");
  REQUIRE(champ.passives[0].source.origin() == "attacker");
}

TEST_CASE("addPassive refresh without source keeps old source",
          "[passive][source]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(Champion::PassiveEntry{
      100,
      [](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{{{Stat::AD, ModType::Base, 10.0, {}}},
                                       true};
      },
      Source{"Item", "Black Cleaver", "attacker"}});
  REQUIRE(champ.passives[0].source.name == "Item");

  // Refresh without source → keeps old source
  champ.addPassive(Champion::PassiveEntry{
      100,
      [](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{{{Stat::AD, ModType::Base, 20.0, {}}},
                                       true};
      }});
  REQUIRE(champ.passives[0].source.name == "Item");
  REQUIRE(champ.passives[0].source.origin() == "attacker");
}

TEST_CASE("addPassive refresh with new source updates source",
          "[passive][source]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(Champion::PassiveEntry{
      101,
      [](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{{{Stat::AD, ModType::Base, 10.0, {}}},
                                       true};
      },
      Source{"Item", "Black Cleaver", "attacker"}});

  // Refresh with new source
  champ.addPassive(Champion::PassiveEntry{
      101,
      [](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{{{Stat::AD, ModType::Base, 20.0, {}}},
                                       true};
      },
      Source{"Rune", "Bloodline", "defender"}});
  REQUIRE(champ.passives[0].source.name == "Rune");
  REQUIRE(champ.passives[0].source.origin() == "defender");
}

TEST_CASE("two passives from different origins coexist", "[passive][source]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  champ.addPassive(factory().make(
      [](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{{{Stat::AD, ModType::Base, 10.0, {}}},
                                       true};
      },
      Source{"Item", "Bloodthirster", "attacker"}));
  champ.addPassive(factory().make(
      [](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{{{Stat::AD, ModType::Base, 5.0, {}}},
                                       true};
      },
      Source{"Rune", "Bloodline", "defender"}));
  REQUIRE(champ.passives.size() == 2);
  REQUIRE(champ.passives[0].source.origin() == "attacker");
  REQUIRE(champ.passives[1].source.origin() == "defender");
}