#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "moba_sim.hpp"

using moba::Champion;
using moba::EventKind;
using moba::GameEvent;
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

TEST_CASE("Simulation enqueue and empty queue", "[events]") {
  Simulation sim;
  REQUIRE(sim.event_queue.empty());
  sim.enqueue({EventKind::AttackHit, 0, 1, 100.0, TypeDamage::Physical});
  REQUIRE(sim.event_queue.size() == 1);
}

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

  sim.enqueue({EventKind::AttackHit,
               0,
               1,
               100.0,
               TypeDamage::Physical,
               Source{"Basic attack", ""},
               0.0});
  sim.processEvents();

  // 100 physical vs 100 AR → 50 post-mitigation
  Stats t = sim.champions[1].getBaseStats();
  REQUIRE(getStat(t, Stat::CurrentHP) == Catch::Approx(950.0).epsilon(0.01));
}

TEST_CASE("EventPassive on DamageReceived grants shield", "[events]") {
  Simulation sim;
  Champion attacker{{Stat::MaxHP, 1000},
                    {Stat::CurrentHP, 1000},
                    {Stat::AD, 100}};
  Champion target{{Stat::MaxHP, 1000},
                  {Stat::CurrentHP, 1000},
                  {Stat::AR, 100}};
  sim.champions.push_back(std::move(attacker));
  sim.champions.push_back(std::move(target));

  // Shield proc: on DamageReceived → +200 ShieldHP, one-shot
  sim.champions[1].addPassive(factory().make(
      [](const Stats &, const Stats &, Type) {
        return Champion::PassiveResult{{}, true};
      },
      Source{"Sterak's Gage", "shield proc"},
      [](const Stats &, const Stats &, Type, const GameEvent &ev)
          -> Champion::PassiveResult {
        if (ev.kind != EventKind::DamageReceived)
          return {};
        return {{{Stat::ShieldHP, ModType::Base, 200.0, {}}}, false};
      }));

  sim.enqueue({EventKind::AttackHit,
               0,
               1,
               100.0,
               TypeDamage::Physical,
               Source{"Basic attack", ""},
               0.0});
  sim.processEvents();

  // Damage was 50 (mitigated), shield absorbed it, then shield proc added 200
  // but damage already applied first. Shield should be 200, HP should be 1000
  // (shield absorbs before HP, but damage was already applied before event
  // handler fired). Actually: DamageReceived applies HP loss first, THEN
  // event handlers fire. So HP = 950, shield = 200.
  Stats t = sim.champions[1].getBaseStats();
  REQUIRE(getStat(t, Stat::CurrentHP) == Catch::Approx(950.0).epsilon(0.01));
  REQUIRE(getStat(t, Stat::ShieldHP) == Catch::Approx(200.0).epsilon(0.01));
}

TEST_CASE("Lifesteal passive emits HealApplied on DamageDealt", "[events]") {
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

  // Lifesteal: on DamageDealt → heal attacker by dealt * lifesteal_pct
  sim.champions[0].addPassive(factory().make(
      [](const Stats &, const Stats &, Type) {
        return Champion::PassiveResult{{}, true};
      },
      Source{"Bloodthirster", "lifesteal"},
      [](const Stats &, const Stats &final, Type, const GameEvent &ev)
          -> Champion::PassiveResult {
        if (ev.kind != EventKind::DamageDealt)
          return {};
        Type heal = ev.amount * getStat(final, Stat::LifeSteal);
        return {{},
                true,
                {{EventKind::HealApplied,
                  ev.actor_id,
                  ev.actor_id,
                  heal,
                  TypeDamage::True,
                  Source{"Lifesteal", ""},
                  ev.time}}};
      }));

  sim.enqueue({EventKind::AttackHit,
               0,
               1,
               100.0,
               TypeDamage::Physical,
               Source{"Basic attack", ""},
               0.0});
  sim.processEvents();

  // 100 phys vs 100 AR → 50 mitigated. Lifesteal: 50 * 0.12 = 6 HP heal
  Stats a = sim.champions[0].getBaseStats();
  REQUIRE(getStat(a, Stat::CurrentHP) == Catch::Approx(806.0).epsilon(0.01));
  // Target took 50 damage
  Stats t = sim.champions[1].getBaseStats();
  REQUIRE(getStat(t, Stat::CurrentHP) == Catch::Approx(950.0).epsilon(0.01));
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
  sim.champions[1].addPassive(factory().make(
      [](const Stats &, const Stats &, Type) {
        return Champion::PassiveResult{{}, true};
      },
      Source{"Tracker", ""},
      [&captured_source](const Stats &,
                         const Stats &,
                         Type,
                         const GameEvent &ev) -> Champion::PassiveResult {
        if (ev.kind == EventKind::DamageReceived) {
          captured_source = ev.source;
        }
        return {};
      }));

  auto jinx = std::make_shared<Source>("Jinx", "champion");
  Source attack_src{"Basic attack", "auto", jinx};
  sim.enqueue({EventKind::AttackHit,
               0,
               1,
               100.0,
               TypeDamage::Physical,
               attack_src,
               0.0});
  sim.processEvents();

  // DamageReceived source should have parent "Basic attack",
  // grandparent "Jinx"
  REQUIRE(captured_source.name == "Damage");
  REQUIRE(captured_source.parent != nullptr);
  REQUIRE(captured_source.parent->name == "Basic attack");
  REQUIRE(captured_source.parent->parent != nullptr);
  REQUIRE(captured_source.parent->parent->name == "Jinx");
}

TEST_CASE("Passive without on_event ignores events", "[events]") {
  Simulation sim;
  Champion attacker{{Stat::MaxHP, 1000},
                    {Stat::CurrentHP, 1000},
                    {Stat::AD, 100}};
  Champion target{{Stat::MaxHP, 1000},
                  {Stat::CurrentHP, 1000},
                  {Stat::AR, 100}};
  sim.champions.push_back(std::move(attacker));
  sim.champions.push_back(std::move(target));

  // Regular passive (no on_event) — should not react to events
  sim.champions[1].addPassive(factory().make(
      [](const Stats &, const Stats &, Type) {
        return Champion::PassiveResult{{{Stat::AR, ModType::Base, 10.0, {}}},
                                       true};
      },
      Source{"Buff", ""}));

  sim.enqueue({EventKind::AttackHit,
               0,
               1,
               100.0,
               TypeDamage::Physical,
               Source{"Basic attack", ""},
               0.0});
  sim.processEvents();

  // Target should still have +10 AR from permanent passive (via
  // evaluateChampion)
  Stats t = sim.champions[1].evaluateChampion();
  REQUIRE(getStat(t, Stat::AR) == Catch::Approx(110.0).epsilon(0.01));
  // And took some damage (mitigated with 110 AR)
  REQUIRE(getStat(t, Stat::CurrentHP) < 1000.0);
}

TEST_CASE("Emitted events are appended to queue (feedback chain)", "[events]") {
  Simulation sim;
  Champion attacker{{Stat::MaxHP, 1000},
                    {Stat::CurrentHP, 1000},
                    {Stat::AD, 100}};
  Champion target{{Stat::MaxHP, 1000},
                  {Stat::CurrentHP, 1000},
                  {Stat::AR, 100}};
  sim.champions.push_back(std::move(attacker));
  sim.champions.push_back(std::move(target));

  // Passive A: on DamageReceived → emit a custom HealApplied to self
  sim.champions[1].addPassive(factory().make(
      [](const Stats &, const Stats &, Type) {
        return Champion::PassiveResult{{}, true};
      },
      Source{"Counter", ""},
      [](const Stats &, const Stats &, Type, const GameEvent &ev)
          -> Champion::PassiveResult {
        if (ev.kind != EventKind::DamageReceived)
          return {};
        return {{},
                true,
                {{EventKind::HealApplied,
                  ev.target_id,
                  ev.target_id,
                  10.0,
                  TypeDamage::True,
                  Source{"Counter heal", ""},
                  ev.time}}};
      }));

  sim.enqueue({EventKind::AttackHit,
               0,
               1,
               100.0,
               TypeDamage::Physical,
               Source{"Basic attack", ""},
               0.0});
  sim.processEvents();

  // Target took 50 damage, then healed 10 from counter → 960
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

  sim.enqueue({EventKind::AttackHit,
               0,
               1,
               100.0,
               TypeDamage::True,
               Source{"True damage", ""},
               0.0});
  sim.processEvents();

  // True damage bypasses mitigation → 100 damage directly
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
  sim.champions[1].addPassive(factory().make(
      [](const Stats &, const Stats &, Type) {
        return Champion::PassiveResult{{}, true};
      },
      Source{"Death tracker", ""},
      [&death_seen](const Stats &, const Stats &, Type, const GameEvent &ev)
          -> Champion::PassiveResult {
        if (ev.kind == EventKind::Death)
          death_seen = true;
        return {};
      }));

  // 999 true damage vs 100 HP → death
  sim.enqueue({EventKind::AttackHit,
               0,
               1,
               999.0,
               TypeDamage::True,
               Source{"Execute", ""},
               0.0});
  sim.processEvents();

  REQUIRE(death_seen);
  Stats t = sim.champions[1].getBaseStats();
  REQUIRE(getStat(t, Stat::CurrentHP) <= 0.0);
}