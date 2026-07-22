#include "moba_sim.hpp"
#include <algorithm>
#include <cmath>
#include <ranges>
namespace moba {

namespace {
// Computes all stats from a ModDB as a Stats array (full Base/Inc/More pipeline
// per stat).
[[nodiscard]] Champion::Stats statsFromModDB(const ModDB &db) {
  Champion::Stats out{};
  for (std::size_t i = 0; i < std::to_underlying(Stat::Count); ++i) {
    out[i] = db.getStat(static_cast<Stat>(i));
  }
  return out;
}

// Applies all passives in a single step without removing any. Folds passive
// mods into a copy of mod_db, then runs the full Base/Inc/More pipeline.
// Returns the resulting stats and the `alive` flag of each passive (aligned
// with passives by index).
[[nodiscard]] std::pair<Champion::Stats, std::vector<bool>>
applyPassivesNoRemove(const ModDB &mod_db,
                      const std::vector<Champion::PassiveEntry> &passives,
                      const Champion::Stats &base, const Champion::Stats &final,
                      const Type &time) {
  ModDB working = mod_db;
  std::vector<bool> alive_flags;
  for (const auto &entry : passives) {
    auto result = entry.passive(base, final, time);
    for (const auto &m : result.mods) {
      working.add(m.stat, m.type, m.value, m.source);
    }
    alive_flags.push_back(result.alive);
  }
  return {statsFromModDB(working), std::move(alive_flags)};
}
} // namespace

Type post_mitigation_damage(const Type &raw_damage,
                            const Type &resistance) noexcept {
  if (resistance >= 0) {
    return raw_damage * 100.0 / (100.0 + resistance);
  }
  return raw_damage * (2.0 - (100.0 / (100.0 - resistance)));
}

void ModDB::add(const Stat &stat, const ModType &type, const Type &value,
                const Source &source) {
  mods_.push_back(
      {.stat = stat, .type = type, .value = value, .source = source});
}

Champion::Champion(std::initializer_list<std::pair<Stat, Type>> stats) {
  Source src{"Base", ""};
  for (const auto &[stat, value] : stats) {
    mod_db.add(stat, ModType::Base, value, src);
  }
}

void ModDB::remove(const Stat &stat, const ModType &type,
                   const Source &source) {
  auto it = std::ranges::find_if(mods_, [&](const Modifier &m) {
    return m.stat == stat && m.type == type && m.source == source;
  });
  if (it != mods_.end()) {
    mods_.erase(it);
  }
}

void ModDB::remove(const std::function<bool(const Modifier &)> &predicate) {
  auto [it, end] = std::ranges::remove_if(mods_, predicate);
  mods_.erase(it, end);
}

Type ModDB::getSumStat(
    const Stat &stat,
    const std::function<bool(const Modifier &)> &predicate) const {
  Type total = 0.0;
  for (const auto &m : mods_) {
    if (predicate(m) && m.stat == stat && m.type == ModType::Base) {
      total += m.value;
    }
  }
  return total;
}

Type ModDB::getIncStat(
    const Stat &stat,
    const std::function<bool(const Modifier &)> &predicate) const {
  Type total = 1.0;
  for (const auto &m : mods_) {
    if (predicate(m) && m.stat == stat && m.type == ModType::Inc) {
      total += m.value;
    }
  }
  return total;
}

Type ModDB::getMoreStat(
    const Stat &stat,
    const std::function<bool(const Modifier &)> &predicate) const {
  Type total = 1.0;
  for (const auto &m : mods_) {
    if (predicate(m) && m.stat == stat && m.type == ModType::More) {
      total *= m.value;
    }
  }
  return total;
}

Type ModDB::getStat(
    const Stat &stat,
    const std::function<bool(const Modifier &)> &predicate) const {
  return getSumStat(stat, predicate) * getIncStat(stat, predicate) *
         getMoreStat(stat, predicate);
}

void ModDB::replace(const Stat &stat, const ModType &type, const Type &value,
                    const Source &source) {
  auto it = std::ranges::find_if(mods_, [&](const Modifier &m) {
    return m.stat == stat && m.type == type && m.source == source;
  });
  if (it != mods_.end()) {
    it->value = value;
  } else {
    mods_.push_back(
        {.stat = stat, .type = type, .value = value, .source = source});
  }
}

Champion::Stats Champion::getBaseStats() const {
  return statsFromModDB(mod_db);
}

void Champion::addPassive(PassiveEntry entry) {
  for (auto &e : passives) {
    if (e.id == entry.id) {
      e.passive = std::move(entry.passive);
      // Update source if the new entry provides one; otherwise keep existing
      if (!entry.source.name.empty()) {
        e.source = std::move(entry.source);
      }
      return;
    }
  }
  passives.push_back(std::move(entry));
}

Champion::Stats Champion::applyPassives(const Stats &base, const Stats &final,
                                        const Type &time) {
  ModDB working = mod_db;
  for (auto it = passives.begin(); it != passives.end();) {
    auto result = it->passive(base, final, time);
    for (const auto &m : result.mods) {
      working.add(m.stat, m.type, m.value, m.source);
    }
    if (result.alive) {
      ++it;
    } else {
      it = passives.erase(it);
    }
  }
  return statsFromModDB(working);
}

Type Champion::getDeltaStats(const Stats &stats1, const Stats &stats2) {
  Type delta = 0;
  for (std::size_t i = 0; i < std::to_underlying(Stat::Count); ++i) {
    Type delta_now = std::abs(stats2[i] - stats1[i]);
    delta = std::max(delta_now, delta);
  }
  return delta;
}

Champion::Stats Champion::evaluateChampion(Type eps, std::size_t max_iter,
                                           Type time) {
  const Stats base = getBaseStats();
  Stats final = base;
  Stats prev = base;
  std::size_t iter = 0;
  std::vector<bool> alive_flags;
  do {
    prev = final;
    auto [f, flags] = applyPassivesNoRemove(mod_db, passives, base, prev, time);
    final = f;
    alive_flags = std::move(flags);
    ++iter;
  } while (getDeltaStats(final, prev) > eps && iter < max_iter);
  if (iter >= max_iter && getDeltaStats(final, prev) > eps) {
    throw ConvergenceError(
        "evaluateChampion did not converge after " + std::to_string(max_iter) +
        " iterations (eps=" + std::to_string(eps) +
        ", delta=" + std::to_string(getDeltaStats(final, prev)) + ")");
  }
  // Remove passives that reported alive=false on the final iteration. Flags
  // are aligned with passives by index (applyPassivesNoRemove appends one flag
  // per passive, in order).
  if (alive_flags.size() == passives.size()) {
    std::size_t idx = 0;
    std::erase_if(passives, [&](const PassiveEntry &) {
      const bool dead = (idx < alive_flags.size()) && !alive_flags[idx];
      ++idx;
      return dead;
    });
  }
  return final;
}

Type mitigated_damage(Type raw_damage, TypeDamage type,
                      const Champion::Stats &target, Type flat_pen,
                      Type pct_pen) noexcept {
  if (type == TypeDamage::True) {
    return raw_damage;
  }
  const Stat resist_stat = (type == TypeDamage::Physical) ? Stat::AR : Stat::MR;
  Type res = target[std::to_underlying(resist_stat)];
  res = (res - flat_pen) * (1.0 - pct_pen);
  return post_mitigation_damage(raw_damage, res);
}

DamageAfterShield apply_damage_to_shield(Type shield, Type current_hp,
                                         Type mitigated) noexcept {
  if (mitigated <= 0.0) {
    return {.shield_remaining = shield, .hp_remaining = current_hp};
  }
  if (shield >= mitigated) {
    return {.shield_remaining = shield - mitigated, .hp_remaining = current_hp};
  }
  return {.shield_remaining = 0.0,
          .hp_remaining = current_hp - (mitigated - shield)};
}

void Simulation::evaluateAll(Type eps, std::size_t max_iter) {
  for (auto &champ : champions) {
    if (!champ.passives.empty()) {
      (void)champ.evaluateChampion(eps, max_iter, 0.0);
    }
  }
}

Simulation::~Simulation() { clearSignals(); }

void Simulation::clearSignals() {
  onAttackHit.clear();
  onDamageDealt.clear();
  onDamageReceived.clear();
  onHealApplied.clear();
  onDeath.clear();
}

Simulation::Simulation() {
  // AttackHit -> compute mitigated damage -> emit DamageReceived + DamageDealt
  onAttackHit.subscribe([this](const AttackHit &ev) {
    if (ev.target_id >= champions.size()) {
      return;
    }
    const auto &target_stats = champions[ev.target_id].getBaseStats();
    Type flat_pen = 0.0;
    Type pct_pen = 0.0;
    if (ev.actor_id < champions.size()) {
      const auto &atk = champions[ev.actor_id].getBaseStats();
      if (ev.damage_type == TypeDamage::Physical) {
        flat_pen = getStat(atk, Stat::ArmorPenFlat);
        pct_pen = getStat(atk, Stat::ArmorPenPct);
      } else if (ev.damage_type == TypeDamage::Magic) {
        flat_pen = getStat(atk, Stat::MagicPenFlat);
        pct_pen = getStat(atk, Stat::MagicPenPct);
      }
    }
    Type mitigated = mitigated_damage(ev.amount,
                                      ev.damage_type,
                                      target_stats,
                                      flat_pen,
                                      pct_pen);
    Source dmg_src{"Damage", "", std::make_shared<Source>(ev.source)};
    onDamageReceived.emit({.actor_id = ev.actor_id,
                           .target_id = ev.target_id,
                           .amount = mitigated,
                           .damage_type = ev.damage_type,
                           .source = dmg_src,
                           .time = ev.time});
    onDamageDealt.emit({.actor_id = ev.actor_id,
                        .target_id = ev.target_id,
                        .amount = mitigated,
                        .damage_type = ev.damage_type,
                        .source = dmg_src,
                        .time = ev.time});
  });

  // DamageReceived -> apply HP loss (shield absorbs) -> emit Death if HP <= 0
  onDamageReceived.subscribe([this](const DamageReceived &ev) {
    if (ev.target_id >= champions.size()) {
      return;
    }
    auto &target = champions[ev.target_id];
    auto base = target.getBaseStats();
    Type shield = getStat(base, Stat::ShieldHP);
    Type hp = getStat(base, Stat::CurrentHP);
    auto [sh_left, hp_left] = apply_damage_to_shield(shield, hp, ev.amount);
    target.mod_db.replace(Stat::CurrentHP,
                          ModType::Base,
                          hp_left,
                          Source{"Base", ""});
    target.mod_db.replace(Stat::ShieldHP,
                          ModType::Base,
                          sh_left,
                          Source{"Base", ""});
    if (hp_left <= 0.0) {
      onDeath.emit({.actor_id = ev.actor_id,
                    .target_id = ev.target_id,
                    .source = Source{"Death", ""},
                    .time = ev.time});
    }
  });

  // HealApplied -> apply HP gain (cap MaxHP)
  onHealApplied.subscribe([this](const HealApplied &ev) {
    if (ev.target_id >= champions.size()) {
      return;
    }
    auto &target = champions[ev.target_id];
    auto base = target.getBaseStats();
    Type hp = getStat(base, Stat::CurrentHP);
    Type max_hp = getStat(base, Stat::MaxHP);
    Type new_hp = std::min(hp + ev.amount, max_hp);
    target.mod_db.replace(Stat::CurrentHP,
                          ModType::Base,
                          new_hp,
                          Source{"Base", ""});
  });
}

} // namespace moba
