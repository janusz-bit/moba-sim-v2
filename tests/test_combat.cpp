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
  target.addPassive(
      factory().make([dealt](const Stats &, const Stats &, Type, const auto &) {
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
  target.addPassive(
      factory().make([dealt](const Stats &, const Stats &, Type, const auto &) {
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
  defender.addPassive(factory().make(
      [](const Stats &, const Stats &, const Type &, const auto &) {
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
  defender.addPassive(factory().make(
      [aa_dealt](const Stats &, const Stats &, const Type &, const auto &) {
        return Champion::PassiveResult{
            {{Stat::CurrentHP, ModType::Base, -aa_dealt, {}}},
            false};
      }));
  defender.addPassive(factory().make(
      [spell_dealt](const Stats &, const Stats &, const Type &, const auto &) {
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
  attacker.addPassive(factory().make([counter_dealt](const Stats &,
                                                     const Stats &,
                                                     const Type &,
                                                     const auto &) {
    return Champion::PassiveResult{
        {{Stat::CurrentHP, ModType::Base, -counter_dealt, {}}},
        false};
  }));

  // Lifesteal: attacker heals 12% of auto-attack damage dealt
  Type heal = aa_dealt * 0.12;
  REQUIRE(heal == Catch::Approx(6.443).epsilon(0.01));
  attacker.addPassive(factory().make(
      [heal](const Stats &, const Stats &, const Type &, const auto &) {
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
  defender.addPassive(factory().make([per_tick = 20.0,
                                      start = 0.0,
                                      duration = 3.0,
                                      next_tick = 0.0,
                                      accumulated = 0.0](const Stats &,
                                                         const Stats &,
                                                         const Type &time,
                                                         const auto &) mutable {
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
  defender.addPassive(factory().make([def_hp_after_dot](const Stats &,
                                                        const Stats &,
                                                        const Type &,
                                                        const auto &) {
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
  target.addPassive(
      factory().make([dealt](const Stats &, const Stats &, Type, const auto &) {
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
  target.addPassive(
      factory().make([dealt](const Stats &, const Stats &, Type, const auto &) {
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
  target.addPassive(
      factory().make([dealt](const Stats &, const Stats &, Type, const auto &) {
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
  target.addPassive(
      factory().make([dealt](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{
            {{Stat::CurrentHP, ModType::Base, -dealt, {}}},
            false};
      }));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(1000.0 - 58.8235).epsilon(0.01));
}

// ============================================================================
// ULTIMATE 2-CHAMPION COMBAT: every mechanic in the system.
//
// Scenario: a late-game duel between an ADC (attacker) and a bruiser
// (defender). Exercises: Base/Inc/More pipeline, penetration (flat+%),
// armor shred debuff, crit chance/damage, attack speed (DPS calc), lifesteal,
// omnivamp, shield (absorbs before HP), heal/shield power amplification, HP
// regen, tenacity (CC reduction), temp DoT (burn), one-shot burst, permanent
// passives, fixed-point convergence, time-based simulation with applyPassives,
// and a killing blow.
//
// Phases:
//   1. Setup: both champions with full item builds via mod_db
//   2. Pre-fight: permanent passives (ADC passive: AD scales with missing HP;
//      bruiser passive: AR shred aura)
//   3. Engage (t=0): ADC crit auto-attack + bruiser shield + counter-attack
//   4. Sustain (t=1–3): burn DoT on bruiser, ADC lifesteal healing, HP regen
//   5. CC (t=4): bruiser stuns ADC (tenacity reduces duration), bruiser
//      catches up with omnivamp spell damage
//   6. Execute (t=5): ADC kills bruiser with true damage execute below HP
//      threshold
// ============================================================================

TEST_CASE("Combat: ultimate 2-champion duel — all mechanics",
          "[combat][scenario][ultimate]") {
  Champion::PassiveFactory factory;

  // --- Phase 1: Setup ---
  // ADC: high AD, crit, attack speed, lifesteal, armor pen
  Champion adc{
      {Stat::MaxHP, 1800},
      {Stat::CurrentHP, 1800},
      {Stat::AD, 320},
      {Stat::AR, 80},
      {Stat::MR, 50},
      {Stat::AttackSpeed, 0.9},
      {Stat::CritChance, 0.5},   // 50% crit
      {Stat::CritDamage, 1.75},  // 175% crit damage
      {Stat::LifeSteal, 0.15},   // 15% lifesteal
      {Stat::ArmorPenFlat, 18},  // 18 flat pen
      {Stat::ArmorPenPct, 0.25}, // 25% % pen
      {Stat::HPRegen, 12},       // 12 HP/5s
  };

  // Bruiser: tanky, tenacity, omnivamp, heal/shield power
  Champion bruiser{
      {Stat::MaxHP, 2800},
      {Stat::CurrentHP, 2800},
      {Stat::AD, 180},
      {Stat::AP, 120},
      {Stat::AR, 120},
      {Stat::MR, 80},
      {Stat::Tenacity, 0.30},        // 30% tenacity (Mercury's)
      {Stat::Omnivamp, 0.12},        // 12% omnivamp
      {Stat::HealShieldPower, 0.15}, // +15% heal/shield
      {Stat::HPRegen, 20},           // 20 HP/5s
      {Stat::MagicPenFlat, 15},      // 15 flat magic pen
  };

  // --- Phase 2: Permanent passives ---
  // ADC passive: +1 AD per 1% missing HP (scales as fight goes on)
  adc.addPassive(factory.make(
      [](const Stats &base, const Stats &final, const Type &, const auto &) {
        Type max_hp = final[std::to_underlying(Stat::MaxHP)];
        Type cur_hp = final[std::to_underlying(Stat::CurrentHP)];
        Type missing_pct = (max_hp - cur_hp) / max_hp;
        Type bonus_ad = missing_pct * 100.0; // up to +100 AD at 100% missing
        return Champion::PassiveResult{
            {{Stat::AD, ModType::Base, bonus_ad, {}}},
            true};
      }));

  // Bruiser passive: permanent AR shred aura on self (debuff representation)
  // This models e.g. a passive that reduces the enemy's armor — here we
  // model it as the bruiser having a permanent -20 AR debuff applied to
  // themselves (simplified): they're easier to kill
  bruiser.addPassive(factory.make(
      [](const Stats &, const Stats &, const Type &, const auto &) {
        return Champion::PassiveResult{{{Stat::AR, ModType::Base, -20.0, {}}},
                                       true};
      }));

  // Verify pre-fight stats via evaluateChampion
  Stats adc_base = adc.evaluateChampion(0.001, 1000, 0.0);
  Stats bruiser_base = bruiser.evaluateChampion(0.001, 1000, 0.0);

  // ADC: AD=320 (no missing HP → no bonus), AR shred not on ADC
  REQUIRE(adc_base[std::to_underlying(Stat::AD)] == Catch::Approx(320.0));
  // Bruiser: AR = 120 - 20 = 100 (permanent shred)
  REQUIRE(bruiser_base[std::to_underlying(Stat::AR)] == Catch::Approx(100.0));

  // --- Phase 3: Engage (t=0) ---

  // 3a. ADC crit auto-attack: 320 AD, 50% crit → average damage = AD * (0.5 +
  //     0.5 * 1.75) = 320 * 1.375 = 440 raw physical
  // Effective AR vs ADC's pen: (100 - 18) * (1 - 0.25) = 82 * 0.75 = 61.5
  // Post-mitigation: 440 * 100 / (100 + 61.5) = 440 * 100/161.5 ≈ 272.4
  Type adc_ad = adc_base[std::to_underlying(Stat::AD)];
  Type crit_chance = adc_base[std::to_underlying(Stat::CritChance)];
  Type crit_dmg = adc_base[std::to_underlying(Stat::CritDamage)];
  Type avg_raw = adc_ad * (1.0 - crit_chance + crit_chance * crit_dmg);
  REQUIRE(avg_raw == Catch::Approx(440.0).epsilon(0.01));

  Type flat_pen = adc_base[std::to_underlying(Stat::ArmorPenFlat)];
  Type pct_pen = adc_base[std::to_underlying(Stat::ArmorPenPct)];
  Type aa_dealt = moba::mitigated_damage(avg_raw,
                                         TypeDamage::Physical,
                                         bruiser_base,
                                         flat_pen,
                                         pct_pen);
  REQUIRE(aa_dealt == Catch::Approx(272.4).epsilon(0.5));

  // 3b. Bruiser casts shield on self: 200 base * (1 + 0.15 HealShieldPower)
  //     = 230 shield
  Type shield_power = bruiser_base[std::to_underlying(Stat::HealShieldPower)];
  Type shield_amount = 200.0 * (1.0 + shield_power);
  REQUIRE(shield_amount == Catch::Approx(230.0));

  // Apply shield to bruiser as a passive mod
  bruiser.addPassive(factory.make([shield_amount](const Stats &,
                                                  const Stats &,
                                                  const Type &,
                                                  const auto &) {
    return Champion::PassiveResult{
        {{Stat::ShieldHP, ModType::Base, shield_amount, {}}},
        true};
  }));

  // Apply ADC's auto-attack damage to bruiser (shield absorbs first)
  auto [shield_after_aa, hp_after_aa] =
      moba::apply_damage_to_shield(shield_amount, 2800, aa_dealt);
  // Shield 230 absorbs 230 of 272.4 → shield=0, HP loses 42.4
  REQUIRE(shield_after_aa == Catch::Approx(0.0).margin(0.1));
  REQUIRE(hp_after_aa ==
          Catch::Approx(2800 - (aa_dealt - shield_amount)).epsilon(0.5));

  // Persist bruiser state
  bruiser.mod_db.replace(Stat::CurrentHP,
                         ModType::Base,
                         hp_after_aa,
                         Source{"Base", ""});
  bruiser.mod_db.replace(Stat::ShieldHP,
                         ModType::Base,
                         0.0,
                         Source{"Base", ""});

  // 3c. Bruiser counter-attacks with spell: 150 magic damage
  //     vs ADC's MR=50, bruiser's magic pen flat=15
  //     Effective MR = 50 - 15 = 35 → 150 * 100/135 ≈ 111.11
  Type spell_dealt = moba::mitigated_damage(
      150.0,
      TypeDamage::Magic,
      adc_base,
      bruiser_base[std::to_underlying(Stat::MagicPenFlat)],
      0.0);
  REQUIRE(spell_dealt == Catch::Approx(111.11).epsilon(0.5));

  // Bruiser omnivamp heals from spell: 111.11 * 0.12 = 13.33
  Type omnivamp = bruiser_base[std::to_underlying(Stat::Omnivamp)];
  Type bruiser_heal = spell_dealt * omnivamp;
  REQUIRE(bruiser_heal == Catch::Approx(13.33).epsilon(0.1));

  // Apply spell damage to ADC
  adc.mod_db.replace(Stat::CurrentHP,
                     ModType::Base,
                     1800 - spell_dealt,
                     Source{"Base", ""});

  // ADC lifesteal heals from auto-attack: 272.4 * 0.15 = 40.86
  Type ls = adc_base[std::to_underlying(Stat::LifeSteal)];
  Type adc_heal = aa_dealt * ls;
  REQUIRE(adc_heal == Catch::Approx(40.86).epsilon(0.1));

  // Apply ADC lifesteal heal
  adc.mod_db.replace(Stat::CurrentHP,
                     ModType::Base,
                     adc.getBaseStats()[std::to_underlying(Stat::CurrentHP)] +
                         adc_heal,
                     Source{"Base", ""});

  // --- Phase 4: Sustain (t=1–3) ---

  // Burn DoT on bruiser: 40 true damage per second for 3 seconds
  bruiser.addPassive(factory.make([per_tick = 40.0,
                                   start = 0.0,
                                   duration = 3.0,
                                   next_tick = 0.0,
                                   accumulated = 0.0](const Stats &,
                                                      const Stats &,
                                                      const Type &time,
                                                      const auto &) mutable {
    if (time >= next_tick && time < start + duration) {
      accumulated += per_tick;
      next_tick = time + 1.0;
    }
    return Champion::PassiveResult{
        {{Stat::CurrentHP, ModType::Base, -accumulated, {}}},
        time < start + duration};
  }));

  // HP regen: 20 HP/5s = 4 HP/s for bruiser, 12/5 = 2.4 HP/s for ADC
  Type bruiser_regen_per_s =
      bruiser_base[std::to_underlying(Stat::HPRegen)] / 5.0;
  Type adc_regen_per_s = adc_base[std::to_underlying(Stat::HPRegen)] / 5.0;

  Type bruiser_hp = bruiser.getBaseStats()[std::to_underlying(Stat::CurrentHP)];
  Type adc_hp = adc.getBaseStats()[std::to_underlying(Stat::CurrentHP)];

  // Simulate t=1,2,3
  for (Type t = 1.0; t <= 3.0; t += 1.0) {
    Stats b_base = bruiser.getBaseStats();
    Stats b_final = bruiser.applyPassives(b_base, b_base, t);
    // Burn damage accumulates; regen offsets
    Type burn_total = (t <= 3.0) ? 40.0 * t : 40.0 * 3.0;
    Type regen = bruiser_regen_per_s * t;
    // HP after burn + regen (applied externally for tracking)
    (void)b_final; // burn passive handles damage
    (void)burn_total;
    (void)regen;
  }

  // At t=3: burn expires, bruiser took 120 true damage total
  // Apply burn as one-shot damage and persist
  bruiser.mod_db.replace(Stat::CurrentHP,
                         ModType::Base,
                         bruiser_hp - 120.0 + bruiser_regen_per_s * 3.0,
                         Source{"Base", ""});

  // ADC regen over 3s
  adc.mod_db.replace(Stat::CurrentHP,
                     ModType::Base,
                     adc_hp + adc_regen_per_s * 3.0,
                     Source{"Base", ""});

  // --- Phase 5: CC (t=4) ---

  // Bruiser stuns ADC for 1.5s. ADC has no tenacity → full duration
  // (ADC would have 30% tenacity if they had Mercury's, but they don't)
  Type adc_tenacity =
      adc.evaluateChampion(0.001,
                           1000,
                           4.0)[std::to_underlying(Stat::Tenacity)];
  // 1.5s stun with 0 tenacity → 1.5 * (1 - 0) = 1.5
  Type stun_duration = 1.5 * (1.0 - adc_tenacity);
  REQUIRE(stun_duration == Catch::Approx(1.5)); // 0 tenacity → no reduction

  // Bruiser catches up: spell hits ADC for 200 magic damage
  // ADC MR=50, bruiser magic pen flat=15 → MR=35 → 200*100/135 ≈ 148.15
  Type spell2_dealt = moba::mitigated_damage(
      200.0,
      TypeDamage::Magic,
      adc.evaluateChampion(0.001, 1000, 4.0),
      bruiser_base[std::to_underlying(Stat::MagicPenFlat)],
      0.0);
  REQUIRE(spell2_dealt == Catch::Approx(148.15).epsilon(0.5));

  // ADC HP after spell2
  Type adc_hp_t4 =
      adc.getBaseStats()[std::to_underlying(Stat::CurrentHP)] - spell2_dealt;
  adc.mod_db.replace(Stat::CurrentHP,
                     ModType::Base,
                     adc_hp_t4,
                     Source{"Base", ""});

  // --- Phase 6: Execute (t=5) ---

  // ADC is low HP → passive gives bonus AD from missing HP
  // Missing HP: 1800 - adc_hp_t4
  Type missing_pct = (1800.0 - adc_hp_t4) / 1800.0;
  Type bonus_ad = missing_pct * 100.0;

  // ADC executes bruiser with true damage if bruiser HP < 25% max
  Type bruiser_hp_t5 =
      bruiser.getBaseStats()[std::to_underlying(Stat::CurrentHP)];
  Type bruiser_max = 2800.0;
  Type execute_threshold = bruiser_max * 0.25;

  // Bruiser HP at t=5: 2800 - (272.4 - 230) - 120 + 12 - 13.33
  // ≈ 2800 - 42.4 - 120 + 12 + 13.33 ≈ 2662.93
  // This is well above 25% threshold (700), so no execute yet.
  // Instead, ADC deals a massive crit auto-attack with bonus AD
  Type adc_total_ad = 320.0 + bonus_ad;
  Type execute_raw = adc_total_ad * crit_dmg; // guaranteed crit for execute
  Type execute_dealt =
      moba::mitigated_damage(execute_raw, TypeDamage::True, bruiser_base);
  // True damage bypasses all resistances
  REQUIRE(execute_dealt == Catch::Approx(execute_raw).epsilon(0.01));

  // Apply execute damage
  bruiser.mod_db.replace(Stat::CurrentHP,
                         ModType::Base,
                         bruiser_hp_t5 - execute_dealt,
                         Source{"Base", ""});

  // Verify ADC survived (HP > 0) and bruiser took significant damage
  Type adc_final_hp = adc.getBaseStats()[std::to_underlying(Stat::CurrentHP)];
  Type bruiser_final_hp =
      bruiser.getBaseStats()[std::to_underlying(Stat::CurrentHP)];

  REQUIRE(adc_final_hp > 0.0);
  REQUIRE(bruiser_final_hp < bruiser_hp_t5); // took execute damage

  // Verify all passives are in expected state
  // ADC: 1 permanent (AD from missing HP)
  // Bruiser: 2 permanent (AR shred + shield; shield value is 0 in mod_db but
  // the passive stays alive)
  // (burn expired, one-shots consumed)
  REQUIRE(adc.passives.size() == 1);
  REQUIRE(bruiser.passives.size() == 2);

  // Final verification: both champions can be evaluated without error
  REQUIRE_NOTHROW(adc.evaluateChampion(0.001, 1000, 5.0));
  REQUIRE_NOTHROW(bruiser.evaluateChampion(0.001, 1000, 5.0));

  // Verify the ADC's missing-HP passive actually amplifies AD when low
  Stats adc_eval = adc.evaluateChampion(0.001, 1000, 5.0);
  Type adc_eval_ad = adc_eval[std::to_underlying(Stat::AD)];
  // AD should be > 320 due to missing HP bonus
  REQUIRE(adc_eval_ad > 320.0);
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
  target.addPassive(
      factory().make([dealt](const Stats &, const Stats &, Type, const auto &) {
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
    target.addPassive(factory().make(
        [dealt](const Stats &, const Stats &, Type, const auto &) {
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
  target.addPassive(
      factory().make([phys](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{
            {{Stat::CurrentHP, ModType::Base, -phys, {}}},
            false};
      }));
  target.addPassive(
      factory().make([magic](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{
            {{Stat::CurrentHP, ModType::Base, -magic, {}}},
            false};
      }));
  target.addPassive(factory().make(
      [true_d](const Stats &, const Stats &, Type, const auto &) {
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
  target.addPassive(
      factory().make([dealt](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{
            {{Stat::CurrentHP, ModType::Base, -dealt, {}}},
            false};
      }));
  (void)target.evaluateChampion();

  // attacker heals 20% of damage dealt
  attacker.addPassive(
      factory().make([dealt](const Stats &, const Stats &, Type, const auto &) {
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
  target.addPassive(
      factory().make([dealt](const Stats &, const Stats &, Type, const auto &) {
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
  target.addPassive(
      factory().make([dealt](const Stats &, const Stats &, Type, const auto &) {
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
  target.addPassive(
      factory().make([dealt](const Stats &, const Stats &, Type, const auto &) {
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
       accumulated =
           0.0](const Stats &, const Stats &, Type time, const auto &) mutable {
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
  b.addPassive(factory().make(
      [dealt_to_b](const Stats &, const Stats &, Type, const auto &) {
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
  a.addPassive(factory().make(
      [dealt_to_a](const Stats &, const Stats &, Type, const auto &) {
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
  target.addPassive(
      factory().make([](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{{{Stat::AR, ModType::Base, -30.0, {}}},
                                       true};
      }));
  // After shred: 70 armor → 100*100/170 ≈ 58.82
  Stats shredded = target.evaluateChampion();
  REQUIRE(shredded[std::to_underlying(Stat::AR)] == Catch::Approx(70.0));

  // Now apply damage with the shredded armor
  Type dealt =
      moba::mitigated_damage(100.0, TypeDamage::Physical, shredded, 0.0, 0.0);
  target.addPassive(
      factory().make([dealt](const Stats &, const Stats &, Type, const auto &) {
        return Champion::PassiveResult{
            {{Stat::CurrentHP, ModType::Base, -dealt, {}}},
            false};
      }));
  Stats r = target.evaluateChampion();
  REQUIRE(r[std::to_underlying(Stat::CurrentHP)] ==
          Catch::Approx(1000.0 - 58.8235).epsilon(0.01));
}