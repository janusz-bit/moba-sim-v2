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
} // namespace

TEST_CASE("Combat: physical damage reduced by armor", "[combat]") {
  Champion target = Champion{{Stat::MaxHP, 1000},
                             {Stat::CurrentHP, 1000},
                             {Stat::AD, 50},
                             {Stat::AR, 100},
                             {Stat::MR, 50}};
  Stats target_base = target.getBaseStats();
  // 100 physical vs 100 armor → 50 post-mitigation
  Type dealt = moba::mitigated_damage(100.0,
                                      TypeDamage::Physical,
                                      target_base,
                                      0.0,
                                      0.0);
  target.addPassive(factory().make([dealt](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{
        {{Stat::CurrentHP, ModType::Base, -dealt, {}}},
        false};
  }));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::CurrentHP)] == Catch::Approx(950.0));
}

TEST_CASE("Combat: magic damage reduced by MR", "[combat]") {
  Champion target = Champion{{Stat::MaxHP, 1000},
                             {Stat::CurrentHP, 1000},
                             {Stat::AD, 50},
                             {Stat::AR, 100},
                             {Stat::MR, 50}};
  Stats target_base = target.getBaseStats();
  // 100 magic vs 50 MR → 100 * 100/150 ≈ 66.667
  Type dealt =
      moba::mitigated_damage(100.0, TypeDamage::Magic, target_base, 0.0, 0.0);
  target.addPassive(factory().make([dealt](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{
        {{Stat::CurrentHP, ModType::Base, -dealt, {}}},
        false};
  }));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(1000.0 - 58.8235).epsilon(0.01));
}

// Advanced 2-champion combat: permanent shred, one-shot burst (physical +
// magic), lifesteal, counter-attack, burn DoT over time, and a killing blow.
// Exercises the full pipeline: mod_db, permanent/temp/one-shot passives,
// mitigated_damage with penetration, evaluateChampion fixed-point, and
// applyPassives time-based simulation.
TEST_CASE("Combat: full 2-champion trade — shred, burst, DoT, lifesteal, death",
          "[combat][scenario]") {
  // Attacker: burst ADC with armor penetration
  Champion attacker{{Stat::MaxHP, 800},
                    {Stat::CurrentHP, 800},
                    {Stat::AD, 80},
                    {Stat::AR, 60},
                    {Stat::MR, 40},
                    {Stat::ArmorPenFlat, 20},
                    {Stat::ArmorPenPct, 0.3}};
  // Defender: tanky bruiser (low enough HP to die in the scenario)
  Champion defender{{Stat::MaxHP, 600},
                    {Stat::CurrentHP, 600},
                    {Stat::AD, 60},
                    {Stat::AR, 120},
                    {Stat::MR, 50}};

  // --- Phase 1: permanent armor shred on defender (-30 AR) ---
  defender.addPassive(
      factory().make([](const Stats &, const Stats &, const Type &) {
        return Champion::PassiveResult{{{Stat::AR, ModType::Base, -30.0, {}}},
                                       true};
      }));
  Stats def_eval = defender.evaluateChampion();
  REQUIRE(def_eval[std::to_underlying(Stat::AR)] == Catch::Approx(90.0));
  REQUIRE(defender.passives.size() == 1); // shred stays

  // --- Phase 2: burst exchange ---
  Stats atk_base = attacker.getBaseStats();
  // Auto-attack: 80 physical, pen flat=20 pct=0.3 vs effective
  // AR=(90-20)*0.7=49 → 80 * 100/149 ≈ 53.691
  Type aa_dealt =
      moba::mitigated_damage(atk_base[std::to_underlying(Stat::AD)],
                             TypeDamage::Physical,
                             def_eval,
                             atk_base[std::to_underlying(Stat::ArmorPenFlat)],
                             atk_base[std::to_underlying(Stat::ArmorPenPct)]);
  REQUIRE(aa_dealt == Catch::Approx(53.691).epsilon(0.01));

  // Spell: 150 magic vs 50 MR → 150 * 100/150 = 100
  Type spell_dealt = moba::mitigated_damage(150.0, TypeDamage::Magic, def_eval);
  REQUIRE(spell_dealt == Catch::Approx(100.0).epsilon(0.01));

  // Stack both as one-shot damage on defender
  defender.addPassive(
      factory().make([aa_dealt](const Stats &, const Stats &, const Type &) {
        return Champion::PassiveResult{
            {{Stat::CurrentHP, ModType::Base, -aa_dealt, {}}},
            false};
      }));
  defender.addPassive(
      factory().make([spell_dealt](const Stats &, const Stats &, const Type &) {
        return Champion::PassiveResult{
            {{Stat::CurrentHP, ModType::Base, -spell_dealt, {}}},
            false};
      }));

  // Counter-attack: 60 physical vs 60 AR → 60*100/160 = 37.5
  Type counter_dealt =
      moba::mitigated_damage(def_eval[std::to_underlying(Stat::AD)],
                             TypeDamage::Physical,
                             atk_base);
  REQUIRE(counter_dealt == Catch::Approx(37.5).epsilon(0.01));
  attacker.addPassive(factory().make(
      [counter_dealt](const Stats &, const Stats &, const Type &) {
        return Champion::PassiveResult{
            {{Stat::CurrentHP, ModType::Base, -counter_dealt, {}}},
            false};
      }));

  // Lifesteal: attacker heals 12% of auto-attack damage dealt
  Type heal = aa_dealt * 0.12;
  REQUIRE(heal == Catch::Approx(6.443).epsilon(0.01));
  attacker.addPassive(
      factory().make([heal](const Stats &, const Stats &, const Type &) {
        return Champion::PassiveResult{
            {{Stat::CurrentHP, ModType::Base, heal, {}}},
            false};
      }));

  // Evaluate both (fixed-point, time=0)
  Stats def_burst = defender.evaluateChampion();
  Stats atk_burst = attacker.evaluateChampion();

  // Defender: 600 - 53.691 - 100 = 446.309
  REQUIRE(def_burst[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(600.0 - aa_dealt - spell_dealt).epsilon(0.01));
  // Attacker: 800 - 37.5 + 6.443 = 768.943
  REQUIRE(atk_burst[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(800.0 - counter_dealt + heal).epsilon(0.01));
  // One-shots consumed; shred permanent stays
  REQUIRE(defender.passives.size() == 1);
  REQUIRE(attacker.passives.empty());

  // Persist post-burst HP into mod_db so future evaluations build on it
  defender.mod_db.replace(Stat::CurrentHP,
                          ModType::Base,
                          def_burst[std::to_underlying(Stat::CurrentHP)],
                          Source{"Base", ""});
  attacker.mod_db.replace(Stat::CurrentHP,
                          ModType::Base,
                          atk_burst[std::to_underlying(Stat::CurrentHP)],
                          Source{"Base", ""});

  // --- Phase 3: burn DoT (20 true damage/tick for 3 ticks at t=0,1,2) ---
  defender.addPassive(factory().make(
      [per_tick = 20.0,
       start = 0.0,
       duration = 3.0,
       next_tick = 0.0,
       accumulated =
           0.0](const Stats &, const Stats &, const Type &time) mutable {
        if (time >= next_tick && time < start + duration) {
          accumulated += per_tick;
          next_tick = time + 1.0;
        }
        return Champion::PassiveResult{
            {{Stat::CurrentHP, ModType::Base, -accumulated, {}}},
            time < start + duration};
      }));

  Type def_hp_after_burst =
      defender.getBaseStats()[std::to_underlying(Stat::CurrentHP)];
  Stats base = defender.getBaseStats();
  Stats final = base;

  // t=0: burn ticks → accumulated=20
  final = defender.applyPassives(base, final, 0.0);
  REQUIRE(final[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(def_hp_after_burst - 20.0).epsilon(0.01));
  REQUIRE(defender.passives.size() == 2); // shred + burn

  // t=1: accumulated=40
  final = defender.applyPassives(base, final, 1.0);
  REQUIRE(final[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(def_hp_after_burst - 40.0).epsilon(0.01));

  // t=2: accumulated=60
  final = defender.applyPassives(base, final, 2.0);
  REQUIRE(final[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(def_hp_after_burst - 60.0).epsilon(0.01));

  // t=3: burn expires (bonus applied then removed)
  final = defender.applyPassives(base, final, 3.0);
  REQUIRE(final[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(def_hp_after_burst - 60.0).epsilon(0.01));
  REQUIRE(defender.passives.size() == 1); // shred only

  // Persist post-DoT HP
  Type def_hp_after_dot = final[std::to_underlying(Stat::CurrentHP)];
  defender.mod_db.replace(Stat::CurrentHP,
                          ModType::Base,
                          def_hp_after_dot,
                          Source{"Base", ""});

  // --- Phase 4: killing blow (true damage exceeding remaining HP) ---
  defender.addPassive(factory().make(
      [def_hp_after_dot](const Stats &, const Stats &, const Type &) {
        return Champion::PassiveResult{
            {{Stat::CurrentHP, ModType::Base, -(def_hp_after_dot + 100.0), {}}},
            false};
      }));
  Stats def_dead = defender.evaluateChampion();
  REQUIRE(def_dead[std::to_underlying(Stat::CurrentHP)] <= 0.0);
  REQUIRE(def_dead[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(-100.0).epsilon(0.01));
  // Shred permanent stays; kill one-shot consumed
  REQUIRE(defender.passives.size() == 1);

  // Attacker survived the encounter with lifesteal-healed HP
  Stats atk_final = attacker.evaluateChampion();
  REQUIRE(atk_final[std::to_underlying(Stat::CurrentHP)] > 0.0);
  REQUIRE(atk_final[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(atk_burst[std::to_underlying(Stat::CurrentHP)])
              .epsilon(0.01));
}

TEST_CASE("Combat: true damage ignores resistances", "[combat]") {
  Champion target = Champion{{Stat::MaxHP, 1000},
                             {Stat::CurrentHP, 1000},
                             {Stat::AD, 50},
                             {Stat::AR, 1000},
                             {Stat::MR, 1000}};
  Stats target_base = target.getBaseStats();
  // 100 true damage → 100 HP loss regardless of AR/MR
  Type dealt =
      moba::mitigated_damage(100.0, TypeDamage::True, target_base, 0.0, 0.0);
  target.addPassive(factory().make([dealt](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{
        {{Stat::CurrentHP, ModType::Base, -dealt, {}}},
        false};
  }));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::CurrentHP)] == Catch::Approx(900.0));
}

TEST_CASE("Combat: flat armor penetration as stat", "[combat]") {
  Champion attacker = Champion{{Stat::MaxHP, 800},
                               {Stat::CurrentHP, 800},
                               {Stat::AD, 100},
                               {Stat::AR, 50},
                               {Stat::MR, 50},
                               {Stat::ArmorPenFlat, 30}};
  Champion target = Champion{{Stat::MaxHP, 1000},
                             {Stat::CurrentHP, 1000},
                             {Stat::AD, 50},
                             {Stat::AR, 100},
                             {Stat::MR, 50}};
  Stats attacker_base = attacker.getBaseStats();
  Stats target_base = target.getBaseStats();
  // 100 physical, 30 flat pen → effective armor = 70 → 100*100/170 ≈ 58.82
  Type flat_pen = attacker_base[std::to_underlying(Stat::ArmorPenFlat)];
  Type dealt = moba::mitigated_damage(100.0,
                                      TypeDamage::Physical,
                                      target_base,
                                      flat_pen,
                                      0.0);
  target.addPassive(factory().make([dealt](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{
        {{Stat::CurrentHP, ModType::Base, -dealt, {}}},
        false};
  }));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(1000.0 - 58.8235).epsilon(0.01));
}

TEST_CASE("Combat: percentage armor penetration as stat", "[combat]") {
  Champion attacker = Champion{{Stat::MaxHP, 800},
                               {Stat::CurrentHP, 800},
                               {Stat::AD, 100},
                               {Stat::AR, 50},
                               {Stat::MR, 50},
                               {Stat::ArmorPenPct, 0.3}};
  Champion target = Champion{{Stat::MaxHP, 1000},
                             {Stat::CurrentHP, 1000},
                             {Stat::AD, 50},
                             {Stat::AR, 100},
                             {Stat::MR, 50}};
  Stats attacker_base = attacker.getBaseStats();
  Stats target_base = target.getBaseStats();
  // 100 physical, 30% pen → effective armor = 100*0.7 = 70 → 58.82
  Type pct_pen = attacker_base[std::to_underlying(Stat::ArmorPenPct)];
  Type dealt = moba::mitigated_damage(100.0,
                                      TypeDamage::Physical,
                                      target_base,
                                      0.0,
                                      pct_pen);
  target.addPassive(factory().make([dealt](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{
        {{Stat::CurrentHP, ModType::Base, -dealt, {}}},
        false};
  }));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(1000.0 - 58.8235).epsilon(0.01));
}

TEST_CASE("Combat: flat and percentage penetration stack", "[combat]") {
  Champion attacker = Champion{{Stat::MaxHP, 800},
                               {Stat::CurrentHP, 800},
                               {Stat::AD, 100},
                               {Stat::AR, 50},
                               {Stat::MR, 50},
                               {Stat::ArmorPenFlat, 30},
                               {Stat::ArmorPenPct, 0.3}};
  Champion target = Champion{{Stat::MaxHP, 1000},
                             {Stat::CurrentHP, 1000},
                             {Stat::AD, 50},
                             {Stat::AR, 100},
                             {Stat::MR, 50}};
  Stats attacker_base = attacker.getBaseStats();
  Stats target_base = target.getBaseStats();
  // 100 physical, 30 flat + 30% pen → armor = (100-30)*0.7 = 49 → 100*100/149
  // ≈ 67.11
  Type flat_pen = attacker_base[std::to_underlying(Stat::ArmorPenFlat)];
  Type pct_pen = attacker_base[std::to_underlying(Stat::ArmorPenPct)];
  Type dealt = moba::mitigated_damage(100.0,
                                      TypeDamage::Physical,
                                      target_base,
                                      flat_pen,
                                      pct_pen);
  target.addPassive(factory().make([dealt](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{
        {{Stat::CurrentHP, ModType::Base, -dealt, {}}},
        false};
  }));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(1000.0 - 67.1141).epsilon(0.01));
}

TEST_CASE("Combat: attacker deals damage to target via passive", "[combat]") {
  Champion attacker = Champion{{Stat::MaxHP, 800},
                               {Stat::CurrentHP, 800},
                               {Stat::AD, 60},
                               {Stat::AR, 50},
                               {Stat::MR, 50}};
  Champion target = Champion{{Stat::MaxHP, 1000},
                             {Stat::CurrentHP, 1000},
                             {Stat::AD, 50},
                             {Stat::AR, 100},
                             {Stat::MR, 50}};

  Stats attacker_base = attacker.getBaseStats();
  Stats target_base = target.getBaseStats();
  // attacker auto-attacks: 60 physical vs target's 100 armor → 30 damage
  Type dealt =
      moba::mitigated_damage(attacker_base[std::to_underlying(Stat::AD)],
                             TypeDamage::Physical,
                             target_base,
                             0.0,
                             0.0);
  target.addPassive(factory().make([dealt](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{
        {{Stat::CurrentHP, ModType::Base, -dealt, {}}},
        false};
  }));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::CurrentHP)] == Catch::Approx(970.0));
}

TEST_CASE("Combat: multiple hits in one evaluation", "[combat]") {
  Champion target = Champion{{Stat::MaxHP, 1000},
                             {Stat::CurrentHP, 1000},
                             {Stat::AD, 50},
                             {Stat::AR, 100},
                             {Stat::MR, 50}};
  Stats target_base = target.getBaseStats();
  // 3 separate one-shot damage passives: 100 physical each → 50 each → 150
  // total
  Type dealt = moba::mitigated_damage(100.0,
                                      TypeDamage::Physical,
                                      target_base,
                                      0.0,
                                      0.0);
  for (int i = 0; i < 3; ++i) {
    target.addPassive(
        factory().make([dealt](const Stats &, const Stats &, Type) {
          return Champion::PassiveResult{
              {{Stat::CurrentHP, ModType::Base, -dealt, {}}},
              false};
        }));
  }
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::CurrentHP)] == Catch::Approx(850.0));
  REQUIRE(target.passives.empty());
}

TEST_CASE("Combat: mixed damage types in one evaluation", "[combat]") {
  Champion target = Champion{{Stat::MaxHP, 1000},
                             {Stat::CurrentHP, 1000},
                             {Stat::AD, 50},
                             {Stat::AR, 100},
                             {Stat::MR, 50}};
  Stats target_base = target.getBaseStats();
  // 100 physical (→50), 100 magic (→66.67), 100 true (→100) = 216.67 total
  Type phys = moba::mitigated_damage(100.0,
                                     TypeDamage::Physical,
                                     target_base,
                                     0.0,
                                     0.0);
  Type magic =
      moba::mitigated_damage(100.0, TypeDamage::Magic, target_base, 0.0, 0.0);
  Type true_d =
      moba::mitigated_damage(100.0, TypeDamage::True, target_base, 0.0, 0.0);
  target.addPassive(factory().make([phys](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{
        {{Stat::CurrentHP, ModType::Base, -phys, {}}},
        false};
  }));
  target.addPassive(factory().make([magic](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{
        {{Stat::CurrentHP, ModType::Base, -magic, {}}},
        false};
  }));
  target.addPassive(
      factory().make([true_d](const Stats &, const Stats &, Type) {
        return Champion::PassiveResult{
            {{Stat::CurrentHP, ModType::Base, -true_d, {}}},
            false};
      }));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(1000.0 - 50.0 - 66.6667 - 100.0).epsilon(0.01));
}

TEST_CASE("Combat: lifesteal heals attacker for fraction of damage",
          "[combat]") {
  Champion attacker = Champion{{Stat::MaxHP, 800},
                               {Stat::CurrentHP, 800},
                               {Stat::AD, 60},
                               {Stat::AR, 50},
                               {Stat::MR, 50}};
  Champion target = Champion{{Stat::MaxHP, 1000},
                             {Stat::CurrentHP, 1000},
                             {Stat::AD, 50},
                             {Stat::AR, 100},
                             {Stat::MR, 50}};

  Stats attacker_base = attacker.getBaseStats();
  Stats target_base = target.getBaseStats();
  Type dealt =
      moba::mitigated_damage(attacker_base[std::to_underlying(Stat::AD)],
                             TypeDamage::Physical,
                             target_base,
                             0.0,
                             0.0);

  // attacker deals damage to target
  target.addPassive(factory().make([dealt](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{
        {{Stat::CurrentHP, ModType::Base, -dealt, {}}},
        false};
  }));
  (void)target.evaluateChampion();

  // attacker heals 20% of damage dealt
  attacker.addPassive(
      factory().make([dealt](const Stats &, const Stats &, Type) {
        return Champion::PassiveResult{
            {{Stat::CurrentHP, ModType::Base, dealt * 0.2, {}}},
            false};
      }));
  Stats a = attacker.evaluateChampion();
  REQUIRE(a[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(800.0 + dealt * 0.2));
}

TEST_CASE("Combat: damage passive is one-shot and consumed", "[combat]") {
  Champion target = Champion{{Stat::MaxHP, 1000},
                             {Stat::CurrentHP, 1000},
                             {Stat::AD, 50},
                             {Stat::AR, 100},
                             {Stat::MR, 50}};
  Stats target_base = target.getBaseStats();
  Type dealt = moba::mitigated_damage(100.0,
                                      TypeDamage::Physical,
                                      target_base,
                                      0.0,
                                      0.0);
  target.addPassive(factory().make([dealt](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{
        {{Stat::CurrentHP, ModType::Base, -dealt, {}}},
        false};
  }));
  Stats first = target.evaluateChampion();
  REQUIRE(first[std::to_underlying(Stat::CurrentHP)] == Catch::Approx(950.0));
  REQUIRE(target.passives.empty());
  // re-evaluate: no more damage passive
  Stats second = target.evaluateChampion();
  REQUIRE(second[std::to_underlying(Stat::CurrentHP)] == Catch::Approx(1000.0));
}

TEST_CASE("Combat: negative armor amplifies damage", "[combat]") {
  Champion target = Champion{{Stat::MaxHP, 1000},
                             {Stat::CurrentHP, 1000},
                             {Stat::AD, 50},
                             {Stat::AR, -50},
                             {Stat::MR, 50}};
  Stats target_base = target.getBaseStats();
  // 100 physical vs -50 armor → 100 * (2 - 100/150) ≈ 133.33
  Type dealt = moba::mitigated_damage(100.0,
                                      TypeDamage::Physical,
                                      target_base,
                                      0.0,
                                      0.0);
  target.addPassive(factory().make([dealt](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{
        {{Stat::CurrentHP, ModType::Base, -dealt, {}}},
        false};
  }));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(1000.0 - 133.3333).epsilon(0.01));
}

TEST_CASE("Combat: target dies when damage exceeds HP", "[combat]") {
  Champion target = Champion{{Stat::MaxHP, 100},
                             {Stat::CurrentHP, 100},
                             {Stat::AD, 50},
                             {Stat::AR, 100},
                             {Stat::MR, 50}};
  Stats target_base = target.getBaseStats();
  // 1000 true damage → HP goes negative
  Type dealt =
      moba::mitigated_damage(1000.0, TypeDamage::True, target_base, 0.0, 0.0);
  target.addPassive(factory().make([dealt](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{
        {{Stat::CurrentHP, ModType::Base, -dealt, {}}},
        false};
  }));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::CurrentHP)] <= 0.0);
  REQUIRE(r[std::to_underlying(Stat::CurrentHP)] == Catch::Approx(-900.0));
}

TEST_CASE("Combat: DoT via temp passive over time", "[combat]") {
  Champion target = Champion{{Stat::MaxHP, 1000},
                             {Stat::CurrentHP, 1000},
                             {Stat::AD, 50},
                             {Stat::AR, 100},
                             {Stat::MR, 50}};
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
        return Champion::PassiveResult{
            {{Stat::CurrentHP, ModType::Base, -accumulated, {}}},
            time < start + duration};
      }));

  Stats base = target.getBaseStats();
  // t=0: -50
  Stats t0 = target.applyPassives(base, base, 0.0);
  REQUIRE(t0[std::to_underlying(Stat::CurrentHP)] == Catch::Approx(950.0));
  // t=1: -100 total
  Stats t1 = target.applyPassives(base, base, 1.0);
  REQUIRE(t1[std::to_underlying(Stat::CurrentHP)] == Catch::Approx(900.0));
  // t=2: -150 total (still alive at t=2 since 2 < 3)
  Stats t2 = target.applyPassives(base, base, 2.0);
  REQUIRE(t2[std::to_underlying(Stat::CurrentHP)] == Catch::Approx(850.0));
  // t=3: expires (3 < 3 false), bonus still applied then removed
  Stats t3 = target.applyPassives(base, base, 3.0);
  REQUIRE(t3[std::to_underlying(Stat::CurrentHP)] == Catch::Approx(850.0));
  REQUIRE(target.passives.empty());
}

TEST_CASE("Combat: two champions trade damage", "[combat]") {
  Champion a = Champion{{Stat::MaxHP, 800},
                        {Stat::CurrentHP, 800},
                        {Stat::AD, 60},
                        {Stat::AR, 50},
                        {Stat::MR, 50}};
  Champion b = Champion{{Stat::MaxHP, 1000},
                        {Stat::CurrentHP, 1000},
                        {Stat::AD, 40},
                        {Stat::AR, 80},
                        {Stat::MR, 60}};

  Stats a_base = a.getBaseStats();
  Stats b_base = b.getBaseStats();

  // a hits b: 60 physical vs 80 armor → 60*100/180 = 33.33
  Type dealt_to_b = moba::mitigated_damage(a_base[std::to_underlying(Stat::AD)],
                                           TypeDamage::Physical,
                                           b_base,
                                           0.0,
                                           0.0);
  b.addPassive(factory().make([dealt_to_b](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{
        {{Stat::CurrentHP, ModType::Base, -dealt_to_b, {}}},
        false};
  }));
  // b hits a: 40 physical vs 50 armor → 40*100/150 = 26.67
  Type dealt_to_a = moba::mitigated_damage(b_base[std::to_underlying(Stat::AD)],
                                           TypeDamage::Physical,
                                           a_base,
                                           0.0,
                                           0.0);
  a.addPassive(factory().make([dealt_to_a](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{
        {{Stat::CurrentHP, ModType::Base, -dealt_to_a, {}}},
        false};
  }));

  Stats a_after = a.evaluateChampion();
  Stats b_after = b.evaluateChampion();

  REQUIRE(a_after[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(800.0 - 26.6667).epsilon(0.01));
  REQUIRE(b_after[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(1000.0 - 33.3333).epsilon(0.01));
}

TEST_CASE("Combat: armor shred debuff then damage", "[combat]") {
  Champion target = Champion{{Stat::MaxHP, 1000},
                             {Stat::CurrentHP, 1000},
                             {Stat::AD, 50},
                             {Stat::AR, 100},
                             {Stat::MR, 50}};

  // First: armor shred (-30 armor) as permanent debuff
  target.addPassive(factory().make([](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{{{Stat::AR, ModType::Base, -30.0, {}}},
                                   true};
  }));
  // After shred: 70 armor → 100*100/170 ≈ 58.82
  Stats shredded = target.evaluateChampion();
  REQUIRE(shredded[std::to_underlying(Stat::AR)] == Catch::Approx(70.0));

  // Now apply damage with the shredded armor
  Type dealt =
      moba::mitigated_damage(100.0, TypeDamage::Physical, shredded, 0.0, 0.0);
  target.addPassive(factory().make([dealt](const Stats &, const Stats &, Type) {
    return Champion::PassiveResult{
        {{Stat::CurrentHP, ModType::Base, -dealt, {}}},
        false};
  }));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(1000.0 - 58.8235).epsilon(0.01));
}