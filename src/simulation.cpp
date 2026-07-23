#include "moba/simulation.hpp"

#include "moba/combat.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <variant>

namespace moba {

namespace {

template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

} // namespace

Simulation::Simulation() = default;
Simulation::~Simulation() { clearSignals(); }

void Simulation::clearSignals() {
  onAttackHit.clear();
  onDamageDealt.clear();
  onDamageReceived.clear();
  onHealApplied.clear();
  onDeath.clear();
  event_queue_.clear();
}

void Simulation::evaluateAll(Type eps, std::size_t max_iter) {
  for (auto &champ : champions) {
    if (!champ.passives.empty()) {
      (void)champ.evaluateChampion(eps, max_iter, 0.0);
    }
  }
}

void Simulation::processInternalRules(const PassiveEvent &ev) {
  std::visit(
      overloaded{
          [](std::monostate) {},

          [&](const AttackHit &e) {
            if (e.target_id >= champions.size()) {
              return;
            }
            const auto &target_stats = champions[e.target_id].getBaseStats();
            Type flat_pen = 0.0;
            Type pct_pen = 0.0;
            if (e.actor_id < champions.size()) {
              const auto &atk = champions[e.actor_id].getBaseStats();
              if (e.damage_type == TypeDamage::Physical) {
                flat_pen = getStat(atk, Stat::ArmorPenFlat);
                pct_pen = getStat(atk, Stat::ArmorPenPct);
              } else if (e.damage_type == TypeDamage::Magic) {
                flat_pen = getStat(atk, Stat::MagicPenFlat);
                pct_pen = getStat(atk, Stat::MagicPenPct);
              }
            }
            Type mitigated = mitigated_damage(e.amount,
                                              e.damage_type,
                                              target_stats,
                                              flat_pen,
                                              pct_pen);
            Source dmg_src{"Damage", "", std::make_shared<Source>(e.source)};
            event_queue_.emplace_back(
                DamageReceived{.actor_id = e.actor_id,
                               .target_id = e.target_id,
                               .amount = mitigated,
                               .damage_type = e.damage_type,
                               .source = dmg_src,
                               .time = e.time});
            event_queue_.emplace_back(DamageDealt{.actor_id = e.actor_id,
                                                  .target_id = e.target_id,
                                                  .amount = mitigated,
                                                  .damage_type = e.damage_type,
                                                  .source = dmg_src,
                                                  .time = e.time});
          },

          [&](const DamageDealt &e) {
            if (e.actor_id >= champions.size() || e.amount <= 0.0) {
              return;
            }
            const auto &atk = champions[e.actor_id].getBaseStats();
            Type heal = 0.0;
            if (e.damage_type == TypeDamage::Physical) {
              heal += e.amount * getStat(atk, Stat::LifeSteal);
            }
            heal += e.amount * getStat(atk, Stat::Omnivamp);
            if (heal > 0.0) {
              event_queue_.emplace_back(
                  HealApplied{.target_id = e.actor_id,
                              .amount = heal,
                              .source = Source{"Lifesteal", ""},
                              .time = e.time});
            }
          },

          [&](const DamageReceived &e) {
            if (e.target_id >= champions.size()) {
              return;
            }
            auto &target = champions[e.target_id];
            auto base = target.getBaseStats();
            Type shield = getStat(base, Stat::ShieldHP);
            Type hp = getStat(base, Stat::CurrentHP);
            auto [sh_left, hp_left] =
                apply_damage_to_shield(shield, hp, e.amount);
            target.mod_db.replace(Stat::CurrentHP,
                                  ModType::Base,
                                  hp_left,
                                  Source{"Base", ""});
            target.mod_db.replace(Stat::ShieldHP,
                                  ModType::Base,
                                  sh_left,
                                  Source{"Base", ""});
            if (hp_left <= 0.0) {
              event_queue_.emplace_back(Death{.actor_id = e.actor_id,
                                              .target_id = e.target_id,
                                              .source = Source{"Death", ""},
                                              .time = e.time});
            }
          },

          [&](const HealApplied &e) {
            if (e.target_id >= champions.size()) {
              return;
            }
            auto &target = champions[e.target_id];
            auto base = target.getBaseStats();
            Type hp = getStat(base, Stat::CurrentHP);
            Type max_hp = getStat(base, Stat::MaxHP);
            Type new_hp = std::min(hp + e.amount, max_hp);
            target.mod_db.replace(Stat::CurrentHP,
                                  ModType::Base,
                                  new_hp,
                                  Source{"Base", ""});
          },

          [&](const Death &) {},
      },
      ev);
}

void Simulation::broadcastToPassives(const PassiveEvent &ev) {
  for (auto &champ : champions) {
    auto base = champ.getBaseStats();
    auto final = base;
    std::vector<PassiveEvent> new_events;

    for (auto it = champ.passives.begin(); it != champ.passives.end();) {
      auto result = it->passive(base, final, 0.0, ev);
      for (const auto &m : result.mods) {
        champ.mod_db.add(m.stat, m.type, m.value, m.source);
      }
      for (auto &ne : result.emitted_events) {
        new_events.push_back(std::move(ne));
      }
      if (!result.alive) {
        it = champ.passives.erase(it);
      } else {
        ++it;
      }
    }

    for (auto &ne : new_events) {
      event_queue_.push_back(std::move(ne));
    }
  }
}

void Simulation::dispatchEvent(const PassiveEvent &event, Type eps,
                               std::size_t max_iter) {
  event_queue_.push_back(event);

  std::size_t iter = 0;
  while (!event_queue_.empty() && iter < max_iter) {
    auto ev = std::move(event_queue_.front());
    event_queue_.pop_front();
    ++iter;

    processInternalRules(ev);

    std::visit(overloaded{
                   [](std::monostate) {},
                   [&](const AttackHit &e) { onAttackHit.emit(e); },
                   [&](const DamageDealt &e) { onDamageDealt.emit(e); },
                   [&](const DamageReceived &e) { onDamageReceived.emit(e); },
                   [&](const HealApplied &e) { onHealApplied.emit(e); },
                   [&](const Death &e) { onDeath.emit(e); },
               },
               ev);

    broadcastToPassives(ev);

    evaluateAll(eps, max_iter);
  }

  if (iter >= max_iter && !event_queue_.empty()) {
    throw ConvergenceError("Simulation::dispatchEvent did not converge after " +
                           std::to_string(max_iter) + " iterations");
  }
}

} // namespace moba