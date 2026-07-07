#include "moba_sim.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace moba {

namespace {
[[nodiscard]] Champion::Stats addStats(const Champion::Stats &a,
                                       const Champion::Stats &b) {
  Champion::Stats out{};
  for (std::size_t i = 0; i < std::to_underlying(Stat::Count); ++i) {
    out[i] = a[i] + b[i];
  }
  return out;
}

// Applies all passives in a single step without removing any. Returns the
// resulting stats and the `alive` flag of each passive (aligned with passives).
[[nodiscard]] std::pair<Champion::Stats, std::vector<bool>>
applyPassivesNoRemove(const std::vector<Champion::PassiveEntry> &passives,
                      const Champion::Stats &base, const Champion::Stats &final,
                      const Type &time) {
  Champion::Stats out = base;
  std::vector<bool> alive_flags;
  for (const auto &entry : passives) {
    auto [bonus, alive] = entry.passive(base, final, time);
    out = addStats(out, bonus);
    alive_flags.push_back(alive);
  }
  return {out, std::move(alive_flags)};
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
  Stats stats{};
  for (std::size_t i = 0; i < std::to_underlying(Stat::Count); ++i) {
    stats[i] = mod_db.getStat(static_cast<Stat>(i));
  }
  return stats;
}

void Champion::addPassive(PassiveEntry entry) {
  for (auto &e : passives) {
    if (e.id == entry.id) {
      e.passive = std::move(entry.passive);
      return;
    }
  }
  passives.push_back(std::move(entry));
}

Champion::Stats Champion::applyPassives(const Stats &base, const Stats &final,
                                        const Type &time) {
  Stats bonus{};
  for (auto it = passives.begin(); it != passives.end();) {
    auto [b, alive] = it->passive(base, final, time);
    bonus = addStats(bonus, b);
    if (alive) {
      ++it;
    } else {
      it = passives.erase(it);
    }
  }
  return addStats(base, bonus);
}

Type Champion::getDeltaStats(const Stats &stats1, const Stats &stats2) {
  Type delta = 0;
  for (std::size_t i = 0; i < std::to_underlying(Stat::Count); ++i) {
    Type delta_now = std::abs(stats2[i] - stats1[i]);
    delta = std::max(delta_now, delta);
  }
  return delta;
}

Champion::Stats Champion::evaluateChampion(Type eps, std::size_t max_iter) {
  const Stats base = getBaseStats();
  Stats final = base;
  Stats prev = base;
  std::size_t iter = 0;
  std::vector<bool> alive_flags;
  do {
    prev = final;
    auto [f, flags] = applyPassivesNoRemove(passives, base, prev, 0.0);
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

} // namespace moba
