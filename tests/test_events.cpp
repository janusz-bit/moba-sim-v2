#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "moba_sim.hpp"

using moba::Champion;
using moba::DamageDealt;
using moba::DamageReceived;
using moba::Death;
using moba::HealApplied;
using moba::ModType;
using moba::Simulation;
using moba::Source;
using moba::Stat;
using moba::Type;
using moba::TypeDamage;
using Stats = Champion::Stats;

namespace {
Champion::PassiveFactory &factory() {
  static Champion::PassiveFactory f;
  return f;
}
} // namespace

// --- Source chain ---

TEST_CASE("Source parent chain is walkable", "[source][chain]") {
  auto jinx = std::make_shared<Source>("Jinx", "champion");
  auto attack = std::make_shared<Source>("Basic attack", "auto hit", jinx);
  Source heal_src{"Bloodthirster", "lifesteal proc", attack};

  REQUIRE(heal_src.name == "Bloodthirster");
  REQUIRE(heal_src.origin() == "Basic attack");
  REQUIRE(heal_src.parent->parent->name == "Jinx");
  REQUIRE(heal_src.parent->parent->parent == nullptr);
}

TEST_CASE("Source default has no parent", "[source][chain]") {
  Source s;
  REQUIRE(s.parent == nullptr);
  REQUIRE(s.origin().empty());
}

TEST_CASE("Source string-origin creates parent", "[source][chain]") {
  Source s{"Item", "Bloodthirster", "attacker"};
  REQUIRE(s.parent != nullptr);
  REQUIRE(s.parent->name == "attacker");
  REQUIRE(s.parent->parent == nullptr);
}

TEST_CASE("Source equality compares chain recursively", "[source][chain]") {
  auto a = std::make_shared<Source>("Jinx");
  Source s1{"Attack", "", a};
  Source s2{"Attack", "", std::make_shared<Source>("Jinx")};
  Source s3{"Attack", "", std::make_shared<Source>("Lux")};
  REQUIRE(s1 == s2);
  REQUIRE(s1 != s3);
}

// --- Event system basics ---

TEST_CASE("AttackHit produces DamageReceived with mitigated amount",
          "[events]") {
  Simulation sim;
  Champion attacker{{Stat::MaxHP, 1000},
                    {Stat::CurrentHP, 1000},
                    {Stat::AD, 100},
                    {Stat::AR, 50}};
  Champion target{{Stat::MaxHP, 1000},
                  {Stat::CurrentHP, 1000},
                  {Stat::AR, 100}};
  sim.champions.push_back(std::move(attacker));
  sim.champions.push_back(std::move(target));

  sim.onAttackHit.emit(
      {0, 1, 100.0, TypeDamage::Physical, Source{"Basic attack", ""}, 0.0});

  // 100 physical vs 100 AR -> 50 post-mitigation
  Stats t = sim.champions[1].getBaseStats();
  REQUIRE(getStat(t, Stat::CurrentHP) == Catch::Approx(950.0).epsilon(0.01));
}

TEST_CASE("Shield proc on DamageReceived grants shield", "[events]") {
  Simulation sim;
  Champion attacker{{Stat::MaxHP, 1000},
                    {Stat::CurrentHP, 1000},
                    {Stat::AD, 100}};
  Champion target{{Stat::MaxHP, 1000},
                  {Stat::CurrentHP, 1000},
                  {Stat::AR, 100}};
  sim.champions.push_back(std::move(attacker));
  sim.champions.push_back(std::move(target));

  // Shield proc: on DamageReceived -> +200 ShieldHP, one-shot
  sim.onDamageReceived.subscribe([&sim](const DamageReceived &ev) {
    if (ev.target_id != 1) {
      return;
    }
    sim.champions[1].mod_db.add(Stat::ShieldHP,
                                ModType::Base,
                                200.0,
                                Source{"Sterak's Gage", ""});
  });

  sim.onAttackHit.emit(
      {0, 1, 100.0, TypeDamage::Physical, Source{"Basic attack", ""}, 0.0});

  // Damage was 50 (mitigated), HP = 950. Shield proc adds 200.
  Stats t = sim.champions[1].getBaseStats();
  REQUIRE(getStat(t, Stat::CurrentHP) == Catch::Approx(950.0).epsilon(0.01));
  REQUIRE(getStat(t, Stat::ShieldHP) == Catch::Approx(200.0).epsilon(0.01));
}

TEST_CASE("Lifesteal is built into Simulation", "[events]") {
  Simulation sim;
  Champion attacker{{Stat::MaxHP, 1000},
                    {Stat::CurrentHP, 800},
                    {Stat::AD, 100},
                    {Stat::LifeSteal, 0.12}};
  Champion target{{Stat::MaxHP, 1000},
                  {Stat::CurrentHP, 1000},
                  {Stat::AR, 100}};
  sim.champions.push_back(std::move(attacker));
  sim.champions.push_back(std::move(target));

  // No manual subscription needed — lifesteal is automatic via internal handler
  sim.onAttackHit.emit(
      {0, 1, 100.0, TypeDamage::Physical, Source{"Basic attack", ""}, 0.0});

  // 100 phys vs 100 AR -> 50 mitigated. Lifesteal: 50 * 0.12 = 6 HP heal
  Stats a = sim.champions[0].getBaseStats();
  REQUIRE(getStat(a, Stat::CurrentHP) == Catch::Approx(806.0).epsilon(0.01));
  // Target took 50 damage
  Stats t = sim.champions[1].getBaseStats();
  REQUIRE(getStat(t, Stat::CurrentHP) == Catch::Approx(950.0).epsilon(0.01));
}

TEST_CASE("Omnivamp heals from all damage types", "[events]") {
  Simulation sim;
  Champion attacker{{Stat::MaxHP, 1000},
                    {Stat::CurrentHP, 800},
                    {Stat::AD, 100},
                    {Stat::Omnivamp, 0.10}};
  Champion target{{Stat::MaxHP, 1000},
                  {Stat::CurrentHP, 1000},
                  {Stat::MR, 100}};
  sim.champions.push_back(std::move(attacker));
  sim.champions.push_back(std::move(target));

  // Magic damage — omnivamp applies, lifesteal doesn't
  sim.onAttackHit.emit(
      {0, 1, 100.0, TypeDamage::Magic, Source{"Spell", ""}, 0.0});

  // 100 magic vs 100 MR -> 50 post-mitigation. Omnivamp: 50 * 0.10 = 5 heal
  Stats a = sim.champions[0].getBaseStats();
  REQUIRE(getStat(a, Stat::CurrentHP) == Catch::Approx(805.0).epsilon(0.01));
}

TEST_CASE("LifeSteal does not trigger on magic damage", "[events]") {
  Simulation sim;
  Champion attacker{{Stat::MaxHP, 1000},
                    {Stat::CurrentHP, 800},
                    {Stat::AD, 100},
                    {Stat::LifeSteal, 0.20}};
  Champion target{{Stat::MaxHP, 1000},
                  {Stat::CurrentHP, 1000},
                  {Stat::MR, 100}};
  sim.champions.push_back(std::move(attacker));
  sim.champions.push_back(std::move(target));

  // Magic damage — lifesteal should NOT trigger
  sim.onAttackHit.emit(
      {0, 1, 100.0, TypeDamage::Magic, Source{"Spell", ""}, 0.0});

  // 100 magic vs 100 MR -> 50 post-mitigation, no heal
  Stats a = sim.champions[0].getBaseStats();
  REQUIRE(getStat(a, Stat::CurrentHP) == Catch::Approx(800.0).epsilon(0.01));
}

TEST_CASE("Event source chain tracks provenance", "[events][source]") {
  Simulation sim;
  Champion attacker{{Stat::MaxHP, 1000},
                    {Stat::CurrentHP, 1000},
                    {Stat::AD, 100}};
  Champion target{{Stat::MaxHP, 1000},
                  {Stat::CurrentHP, 1000},
                  {Stat::AR, 100}};
  sim.champions.push_back(std::move(attacker));
  sim.champions.push_back(std::move(target));

  // Track source chain from event
  Source captured_source;
  sim.onDamageReceived.subscribe([&captured_source](const DamageReceived &ev) {
    captured_source = ev.source;
  });

  auto jinx = std::make_shared<Source>("Jinx", "champion");
  Source attack_src{"Basic attack", "auto", jinx};
  sim.onAttackHit.emit({0, 1, 100.0, TypeDamage::Physical, attack_src, 0.0});

  // DamageReceived source should have parent "Basic attack",
  // grandparent "Jinx"
  REQUIRE(captured_source.name == "Damage");
  REQUIRE(captured_source.parent != nullptr);
  REQUIRE(captured_source.parent->name == "Basic attack");
  REQUIRE(captured_source.parent->parent != nullptr);
  REQUIRE(captured_source.parent->parent->name == "Jinx");
}

TEST_CASE("Regular passive ignores signals", "[events]") {
  Simulation sim;
  Champion attacker{{Stat::MaxHP, 1000},
                    {Stat::CurrentHP, 1000},
                    {Stat::AD, 100}};
  Champion target{{Stat::MaxHP, 1000},
                  {Stat::CurrentHP, 1000},
                  {Stat::AR, 100}};
  sim.champions.push_back(std::move(attacker));
  sim.champions.push_back(std::move(target));

  // Regular passive (no signal subscription) - should not react to events
  sim.champions[1].addPassive(factory().make(
      [](const Stats &, const Stats &, Type) {
        return Champion::PassiveResult{{{Stat::AR, ModType::Base, 10.0, {}}},
                                       true};
      },
      Source{"Buff", ""}));

  sim.onAttackHit.emit(
      {0, 1, 100.0, TypeDamage::Physical, Source{"Basic attack", ""}, 0.0});

  // Target should still have +10 AR from permanent passive (via
  // evaluateChampion)
  Stats t = sim.champions[1].evaluateChampion();
  REQUIRE(getStat(t, Stat::AR) == Catch::Approx(110.0).epsilon(0.01));
  // And took some damage (mitigated with 110 AR)
  REQUIRE(getStat(t, Stat::CurrentHP) < 1000.0);
}

TEST_CASE("Counter heal on DamageReceived emits HealApplied", "[events]") {
  Simulation sim;
  Champion attacker{{Stat::MaxHP, 1000},
                    {Stat::CurrentHP, 1000},
                    {Stat::AD, 100}};
  Champion target{{Stat::MaxHP, 1000},
                  {Stat::CurrentHP, 1000},
                  {Stat::AR, 100}};
  sim.champions.push_back(std::move(attacker));
  sim.champions.push_back(std::move(target));

  // Counter heal: on DamageReceived -> heal self for 10
  sim.onDamageReceived.subscribe([&sim](const DamageReceived &ev) {
    sim.onHealApplied.emit(
        {ev.target_id, 10.0, Source{"Counter heal", ""}, ev.time});
  });

  sim.onAttackHit.emit(
      {0, 1, 100.0, TypeDamage::Physical, Source{"Basic attack", ""}, 0.0});

  // Target took 50 damage, then healed 10 from counter -> 960
  Stats t = sim.champions[1].getBaseStats();
  REQUIRE(getStat(t, Stat::CurrentHP) == Catch::Approx(960.0).epsilon(0.01));
}

TEST_CASE("True damage attack bypasses mitigation", "[events]") {
  Simulation sim;
  Champion attacker{{Stat::MaxHP, 1000},
                    {Stat::CurrentHP, 1000},
                    {Stat::AD, 100}};
  Champion target{{Stat::MaxHP, 1000},
                  {Stat::CurrentHP, 1000},
                  {Stat::AR, 500}};
  sim.champions.push_back(std::move(attacker));
  sim.champions.push_back(std::move(target));

  sim.onAttackHit.emit(
      {0, 1, 100.0, TypeDamage::True, Source{"True damage", ""}, 0.0});

  // True damage bypasses mitigation -> 100 damage directly
  Stats t = sim.champions[1].getBaseStats();
  REQUIRE(getStat(t, Stat::CurrentHP) == Catch::Approx(900.0).epsilon(0.01));
}

TEST_CASE("Death event fires when HP reaches zero", "[events]") {
  Simulation sim;
  Champion attacker{{Stat::MaxHP, 1000},
                    {Stat::CurrentHP, 1000},
                    {Stat::AD, 999}};
  Champion target{{Stat::MaxHP, 1000}, {Stat::CurrentHP, 100}, {Stat::AR, 0}};
  sim.champions.push_back(std::move(attacker));
  sim.champions.push_back(std::move(target));

  bool death_seen = false;
  sim.onDeath.subscribe([&death_seen](const Death &) { death_seen = true; });

  // 999 true damage vs 100 HP -> death
  sim.onAttackHit.emit(
      {0, 1, 999.0, TypeDamage::True, Source{"Execute", ""}, 0.0});

  REQUIRE(death_seen);
  Stats t = sim.champions[1].getBaseStats();
  REQUIRE(getStat(t, Stat::CurrentHP) <= 0.0);
}