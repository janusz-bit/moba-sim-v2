#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
  champ.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{{{Stat::LifeSteal, ModType::Base, 0.05, {}}},
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
  target.addPassive(
      factory().make([dealt](const Stats &, const Stats &, const Type &) {
        return Champion::PassiveResult{
            {{moba::Stat::CurrentHP, moba::ModType::Base, -dealt, {}}},
            false};
      }));

  // Apply heal to attacker
  attacker.addPassive(
      factory().make([heal](const Stats &, const Stats &, const Type &) {
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