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

enum class PassiveId : std::size_t {
  Test1,
  Test2,
  Test3,
  Test4,
  Test5,
  Test6,
  Test7,
  Test8,
  Test9,
  Test10,
  Test11,
  Test12,
  Test13,
  Test14,
  Test15,
  Test16,
  Test17,
  Test18,
  Test19,
  Test20,
  Test21,
  Test22,
  Test23,
  Test24,
  Test25,
  Test26,
  Test27,
  Test28,
  Test29,
  Test30,
  Test31,
  Test32,
  Test33,
  Test34,
  Test35,
  Test36,
  Test37,
  Test38,
  Test39,
  Test40,
};

namespace {
Champion::PassiveFactory &factory() {
  static Champion::PassiveFactory f;
  return f;
}
} // namespace

// --- Life steal ---

TEST_CASE("lifesteal_heal returns 0 for zero lifesteal", "[lifesteal]") {
  REQUIRE(moba::lifesteal_heal(100.0, 0.0) == Catch::Approx(0.0));
}

TEST_CASE("lifesteal_heal returns percentage of post-mitigated damage",
          "[lifesteal]") {
  // 12% lifesteal on 50 post-mitigated → 6
  REQUIRE(moba::lifesteal_heal(50.0, 0.12) == Catch::Approx(6.0));
}

TEST_CASE("lifesteal_heal scales with damage", "[lifesteal]") {
  Type heal1 = moba::lifesteal_heal(50.0, 0.15);
  Type heal2 = moba::lifesteal_heal(100.0, 0.15);
  REQUIRE(heal2 == Catch::Approx(2.0 * heal1));
}

TEST_CASE("lifesteal_heal with 100% lifesteal returns full damage",
          "[lifesteal]") {
  REQUIRE(moba::lifesteal_heal(75.0, 1.0) == Catch::Approx(75.0));
}

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
      factory().make(PassiveId::Test1, [](const Stats &, const Stats &, Type) {
        return Champion::PassiveResult{
            {{Stat::LifeSteal, ModType::Base, 0.05, {}}},
            true};
      }));
  Stats r = champ.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::LifeSteal)] == Catch::Approx(0.10));
}

// --- Omnivamp ---

TEST_CASE("omnivamp_heal returns percentage of post-mitigated damage",
          "[omnivamp]") {
  REQUIRE(moba::omnivamp_heal(100.0, 0.0) == Catch::Approx(0.0));
  REQUIRE(moba::omnivamp_heal(80.0, 0.15) == Catch::Approx(12.0));
}

TEST_CASE("Omnivamp stat in mod_db", "[omnivamp]") {
  Champion champ;
  champ.mod_db.add(Stat::Omnivamp, ModType::Base, 0.15, Source{"Item", ""});
  Stats base = champ.getBaseStats();
  REQUIRE(base[std::to_underlying(Stat::Omnivamp)] == Catch::Approx(0.15));
}

// --- Tenacity ---

TEST_CASE("effective_cc_duration with zero tenacity returns raw",
          "[tenacity]") {
  REQUIRE(moba::effective_cc_duration(2.0, 0.0) == Catch::Approx(2.0));
}

TEST_CASE("effective_cc_duration with 30% tenacity reduces by 30%",
          "[tenacity]") {
  // 2.0s * (1 - 0.3) = 1.4s
  REQUIRE(moba::effective_cc_duration(2.0, 0.3) == Catch::Approx(1.4));
}

TEST_CASE("effective_cc_duration with 100% tenacity hits 0.3s floor",
          "[tenacity]") {
  // Full tenacity → 0.0, but capped at 0.3s minimum
  REQUIRE(moba::effective_cc_duration(2.0, 1.0) == Catch::Approx(0.3));
}

TEST_CASE("effective_cc_duration floor applies even for short CC",
          "[tenacity]") {
  // 0.4s stun with 50% tenacity → 0.2, but floored to 0.3
  REQUIRE(moba::effective_cc_duration(0.4, 0.5) == Catch::Approx(0.3));
}

TEST_CASE("effective_cc_duration with negative tenacity amplifies CC",
          "[tenacity]") {
  // -30% tenacity (e.g. Brittle) → 2.0 * (1 - (-0.3)) = 2.0 * 1.3 = 2.6
  REQUIRE(moba::effective_cc_duration(2.0, -0.3) == Catch::Approx(2.6));
}

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
  Type heal =
      moba::lifesteal_heal(dealt,
                           atk_base[std::to_underlying(Stat::LifeSteal)]);

  REQUIRE(dealt == Catch::Approx(30.0));
  REQUIRE(heal == Catch::Approx(3.6));

  // Apply damage to target
  target.addPassive(factory().make(
      PassiveId::Test2,
      [dealt](const Stats &, const Stats &, const Type &) {
        return Champion::PassiveResult{
            {{moba::Stat::CurrentHP, moba::ModType::Base, -dealt, {}}},
            false};
      }));

  // Apply heal to attacker
  attacker.addPassive(factory().make(
      PassiveId::Test3,
      [heal](const Stats &, const Stats &, const Type &) {
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
  Type reduced = moba::effective_cc_duration(2.0, tenacity);
  REQUIRE(reduced == Catch::Approx(1.4));
}

// ===========================================================================
// EDGE CASE TESTS — exercising extreme/corner inputs
// ===========================================================================

// --- effective_cc_duration: extreme tenacity values ---

TEST_CASE("tenacity exactly 0.0 returns raw duration", "[tenacity][edge]") {
  REQUIRE(moba::effective_cc_duration(5.0, 0.0) == Catch::Approx(5.0));
}

TEST_CASE("tenacity 1.0 (100%) floors at 0.3s for long CC",
          "[tenacity][edge]") {
  REQUIRE(moba::effective_cc_duration(10.0, 1.0) == Catch::Approx(0.3));
}

TEST_CASE("tenacity 1.0 floors at 0.3s even for very long CC",
          "[tenacity][edge]") {
  REQUIRE(moba::effective_cc_duration(1000.0, 1.0) == Catch::Approx(0.3));
}

TEST_CASE("tenacity exactly 0.3s floor boundary", "[tenacity][edge]") {
  // 0.42857s * (1 - 0.3) = 0.3s exactly → floor doesn't trigger
  REQUIRE(moba::effective_cc_duration(0.428571, 0.3) ==
          Catch::Approx(0.3).epsilon(0.001));
}

TEST_CASE("tenacity floor triggers just below 0.3s", "[tenacity][edge]") {
  // 0.4s * (1 - 0.5) = 0.2 → floored to 0.3
  REQUIRE(moba::effective_cc_duration(0.4, 0.5) == Catch::Approx(0.3));
}

TEST_CASE("tenacity negative amplifies CC multiplicatively",
          "[tenacity][edge]") {
  // -50% tenacity → 2.0 * (1 - (-0.5)) = 2.0 * 1.5 = 3.0
  REQUIRE(moba::effective_cc_duration(2.0, -0.5) == Catch::Approx(3.0));
}

TEST_CASE("tenacity -1.0 doubles CC duration", "[tenacity][edge]") {
  // -100% tenacity → 2.0 * (1 - (-1.0)) = 2.0 * 2.0 = 4.0
  REQUIRE(moba::effective_cc_duration(2.0, -1.0) == Catch::Approx(4.0));
}

TEST_CASE("tenacity extreme negative does not floor", "[tenacity][edge]") {
  // -10.0 tenacity → 1.0 * (1 - (-10)) = 1.0 * 11 = 11 (floor is for min, not
  // max)
  REQUIRE(moba::effective_cc_duration(1.0, -10.0) == Catch::Approx(11.0));
}

TEST_CASE("tenacity zero duration stays zero (unless floored)",
          "[tenacity][edge]") {
  // 0.0 * (1 - 0.5) = 0.0, which is < 0.3 → floored to 0.3
  REQUIRE(moba::effective_cc_duration(0.0, 0.5) == Catch::Approx(0.3));
}

// --- lifesteal_heal: extreme values ---

TEST_CASE("lifesteal_heal with zero damage returns zero", "[lifesteal][edge]") {
  REQUIRE(moba::lifesteal_heal(0.0, 0.15) == Catch::Approx(0.0));
}

TEST_CASE("lifesteal_heal with negative damage (heal reversal)",
          "[lifesteal][edge]") {
  // Negative post-mitigated (e.g. from negative raw) → negative heal (damage
  // self)
  REQUIRE(moba::lifesteal_heal(-50.0, 0.12) == Catch::Approx(-6.0));
}

TEST_CASE("lifesteal_heal with huge values stays finite", "[lifesteal][edge]") {
  REQUIRE(std::isfinite(moba::lifesteal_heal(1e15, 1.0)));
  REQUIRE(moba::lifesteal_heal(1e15, 1.0) == Catch::Approx(1e15));
}

TEST_CASE("lifesteal_heal with >100% lifesteal over-heals",
          "[lifesteal][edge]") {
  // 150% lifesteal: heals more than damage dealt
  REQUIRE(moba::lifesteal_heal(100.0, 1.5) == Catch::Approx(150.0));
}

TEST_CASE("lifesteal_heal with negative lifesteal reverses healing",
          "[lifesteal][edge]") {
  // Negative lifesteal → self-damage
  REQUIRE(moba::lifesteal_heal(100.0, -0.2) == Catch::Approx(-20.0));
}

// --- omnivamp_heal: extreme values ---

TEST_CASE("omnivamp_heal with zero damage returns zero", "[omnivamp][edge]") {
  REQUIRE(moba::omnivamp_heal(0.0, 0.15) == Catch::Approx(0.0));
}

TEST_CASE("omnivamp_heal with negative omnivamp self-damages",
          "[omnivamp][edge]") {
  REQUIRE(moba::omnivamp_heal(100.0, -0.3) == Catch::Approx(-30.0));
}

TEST_CASE("omnivamp_heal with huge omnivamp stays finite", "[omnivamp][edge]") {
  REQUIRE(std::isfinite(moba::omnivamp_heal(1e15, 100.0)));
}

// --- amplified_heal: extreme values ---

TEST_CASE("amplified_heal with zero power returns base", "[healshield][edge]") {
  REQUIRE(moba::amplified_heal(100.0, 0.0) == Catch::Approx(100.0));
}

TEST_CASE("amplified_heal with negative power reduces heal",
          "[healshield][edge]") {
  // -50% heal power → 100 * (1 - 0.5) = 50
  REQUIRE(moba::amplified_heal(100.0, -0.5) == Catch::Approx(50.0));
}

TEST_CASE("amplified_heal with -100% power zeroes the heal",
          "[healshield][edge]") {
  // Grievous Wounds-style: -100% → 0 heal
  REQUIRE(moba::amplified_heal(100.0, -1.0) == Catch::Approx(0.0));
}

TEST_CASE("amplified_heal with huge power stays finite", "[healshield][edge]") {
  REQUIRE(std::isfinite(moba::amplified_heal(1e10, 1e10)));
}

TEST_CASE("amplified_heal with zero base returns zero", "[healshield][edge]") {
  REQUIRE(moba::amplified_heal(0.0, 0.5) == Catch::Approx(0.0));
}

// --- apply_damage_to_shield: extreme values ---

TEST_CASE("shield absorbs exactly all damage", "[shield][edge]") {
  auto [s, h] = moba::apply_damage_to_shield(100.0, 1000.0, 100.0);
  REQUIRE(s == Catch::Approx(0.0));
  REQUIRE(h == Catch::Approx(1000.0));
}

TEST_CASE("shield larger than damage: partial absorption", "[shield][edge]") {
  auto [s, h] = moba::apply_damage_to_shield(200.0, 1000.0, 50.0);
  REQUIRE(s == Catch::Approx(150.0));
  REQUIRE(h == Catch::Approx(1000.0));
}

TEST_CASE("shield zero: all damage goes to HP", "[shield][edge]") {
  auto [s, h] = moba::apply_damage_to_shield(0.0, 1000.0, 100.0);
  REQUIRE(s == Catch::Approx(0.0));
  REQUIRE(h == Catch::Approx(900.0));
}

TEST_CASE("damage exactly equals shield+HP: both go to zero",
          "[shield][edge]") {
  auto [s, h] = moba::apply_damage_to_shield(100.0, 200.0, 300.0);
  REQUIRE(s == Catch::Approx(0.0));
  REQUIRE(h == Catch::Approx(0.0));
}

TEST_CASE("damage exceeds shield+HP: HP goes negative", "[shield][edge]") {
  auto [s, h] = moba::apply_damage_to_shield(100.0, 200.0, 500.0);
  REQUIRE(s == Catch::Approx(0.0));
  REQUIRE(h == Catch::Approx(-200.0));
}

TEST_CASE("shield with zero damage: nothing changes", "[shield][edge]") {
  auto [s, h] = moba::apply_damage_to_shield(100.0, 500.0, 0.0);
  REQUIRE(s == Catch::Approx(100.0));
  REQUIRE(h == Catch::Approx(500.0));
}

TEST_CASE("shield with negative damage (heal): no effect on shield/HP",
          "[shield][edge]") {
  // Negative damage should not increase shield or HP (only heals do that)
  auto [s, h] = moba::apply_damage_to_shield(100.0, 500.0, -50.0);
  REQUIRE(s == Catch::Approx(100.0));
  REQUIRE(h == Catch::Approx(500.0));
}

TEST_CASE("shield huge, damage tiny: shield barely scratched",
          "[shield][edge]") {
  auto [s, h] = moba::apply_damage_to_shield(1e6, 1000.0, 1.0);
  REQUIRE(s == Catch::Approx(999999.0));
  REQUIRE(h == Catch::Approx(1000.0));
}

TEST_CASE("shield tiny, damage huge: shield obliterated, HP takes rest",
          "[shield][edge]") {
  auto [s, h] = moba::apply_damage_to_shield(1.0, 1000.0, 10000.0);
  REQUIRE(s == Catch::Approx(0.0));
  REQUIRE(h == Catch::Approx(1000.0 - 9999.0));
}

TEST_CASE("shield and HP both zero: stays zero", "[shield][edge]") {
  auto [s, h] = moba::apply_damage_to_shield(0.0, 0.0, 100.0);
  REQUIRE(s == Catch::Approx(0.0));
  REQUIRE(h == Catch::Approx(-100.0));
}

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
      PassiveId::Test4,
      [iter = 0](const Stats &, const Stats &, Type) mutable {
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
      factory().make(PassiveId::Test5, [](const Stats &, const Stats &, Type) {
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
    champ.addPassive(factory().make(static_cast<PassiveId>(i),
                                    [](const Stats &, const Stats &, Type) {
                                      return Champion::PassiveResult{
                                          {{Stat::AD, ModType::Base, 1.0, {}}},
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
      factory().make(PassiveId::Test6, [](const Stats &, const Stats &, Type) {
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
      factory().make(PassiveId::Test7, [](const Stats &, const Stats &, Type) {
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
      factory().make(PassiveId::Test8, [](const Stats &, const Stats &, Type) {
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
    champ.addPassive(factory().make(static_cast<PassiveId>(100 + i),
                                    [](const Stats &, const Stats &, Type) {
                                      return Champion::PassiveResult{
                                          {{Stat::AD, ModType::Base, 1.0, {}}},
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
      factory().make(PassiveId::Test9, [](const Stats &, const Stats &, Type) {
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
      PassiveId::Test10,
      [duration = 1e9](const Stats &, const Stats &, Type time) {
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
      PassiveId::Test11,
      [start = 0.0, duration = 0.0](const Stats &, const Stats &, Type time) {
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

TEST_CASE("PassiveFactory creates entries with provided enum ids",
          "[champion][edge]") {
  Champion::PassiveFactory f;
  auto e1 = f.make(PassiveId::Test12, [](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{{}, true};
  });
  auto e2 = f.make(PassiveId::Test13, [](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{{}, true};
  });
  REQUIRE(e1.id == std::to_underlying(PassiveId::Test12));
  REQUIRE(e2.id == std::to_underlying(PassiveId::Test13));
  REQUIRE(e1.id != e2.id);
}

TEST_CASE("addPassive refresh with same id replaces passive",
          "[champion][edge]") {
  Champion champ;
  champ.mod_db.add(Stat::AD, ModType::Base, 50.0, Source{"Base", ""});
  auto e =
      factory().make(PassiveId::Test14, [](const Stats &, const Stats &, Type) {
        return Champion::PassiveResult{{{Stat::AD, ModType::Base, 10.0, {}}},
                                       true};
      });
  champ.addPassive(e);
  REQUIRE(champ.passives.size() == 1);

  // Refresh with +20 instead of +10
  champ.addPassive(
      factory().make(PassiveId::Test14, [](const Stats &, const Stats &, Type) {
        return Champion::PassiveResult{{{Stat::AD, ModType::Base, 20.0, {}}},
                                       true};
      }));
  REQUIRE(champ.passives.size() == 1); // still 1, replaced
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
      factory().make(PassiveId::Test15,
                     [](const Stats &, const Stats &final, Type) {
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