#include "moba_sim.hpp"
#include <algorithm>
#include <stdexcept>
namespace moba {

namespace {
[[nodiscard]] Champion::Stats addStats(const Champion::Stats &a, const Champion::Stats &b) {
  Champion::Stats out{};
  for (std::size_t i = 0; i < std::to_underlying(Stat::Count); ++i) {
    out[i] = a[i] + b[i];
  }
  return out;
}

// Applies only permanent passives; used by evaluateChampion for fixed-point
// iteration. Does not touch one_shot_/temp_ passives.
[[nodiscard]] Champion::Stats
applyPermanentPassives(const std::vector<Champion::Passive> &passives,
                       const Champion::Stats &base, const Champion::Stats &final,
                       Type time) {
  Champion::Stats out = base;
  for (const auto &passive : passives) {
    out = addStats(out, passive(base, final, time).bonus);
  }
  return out;
}
} // namespace

Type post_mitigation_damage(const Type &raw_damage,
                            const Type &resistanse) noexcept {
  if (resistanse >= 0) {
    return raw_damage * 100.0 / (100.0 + resistanse);
  }
  return raw_damage * (2.0 - (100.0 / (100.0 - resistanse)));
}

inline std::string statToString(Stat stat) {
  switch (stat) {
  case Stat::HP:
    return "HP";
  case Stat::AP:
    return "AP";
  case Stat::AD:
    return "AD";
  case Stat::MS:
    return "MS";
  case Stat::AR:
    return "Armor";
  case Stat::MR:
    return "MR";
  case Stat::CDR:
    return "CDR";
  case Stat::Count:
    throw std::invalid_argument("Invalid stat");
  }
  throw std::invalid_argument("Invalid stat");
}

void ModDB::add(const Stat &stat, const ModType &type, const Type &value,
                const Source &source) {
  mods_.push_back(
      {.stat = stat, .type = type, .value = value, .source = source});
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

Type
ModDB::getStat(const Stat &stat,
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

Champion::Stats Champion::applyPassives(const Stats &base,
                                         const Stats &final, Type time) {
  Stats bonus{};
  for (const auto &passive : passives) {
    bonus = addStats(bonus, passive(base, final, time).bonus);
  }
  for (auto it = one_shot_passives.begin(); it != one_shot_passives.end();) {
    bonus = addStats(bonus, (*it)(base, final, time).bonus);
    it = one_shot_passives.erase(it);
  }
  for (auto it = temp_passives.begin(); it != temp_passives.end();) {
    auto result = (*it)(base, final, time);
    bonus = addStats(bonus, result.bonus);
    if (result.alive) {
      ++it;
    } else {
      it = temp_passives.erase(it);
    }
  }
  return addStats(base, bonus);
}

Type Champion::getDeltaStats(const Stats &stats1,
                                            const Stats &stats2) {
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
  do {
    prev = final;
    final = applyPermanentPassives(passives, base, prev, 0.0);
    ++iter;
  } while (getDeltaStats(final, prev) > eps && iter < max_iter);
  if (iter >= max_iter && getDeltaStats(final, prev) > eps) {
    throw ConvergenceError(
        "evaluateChampion did not converge after " + std::to_string(max_iter) +
        " iterations (eps=" + std::to_string(eps) + ", delta=" +
        std::to_string(getDeltaStats(final, prev)) + ")");
  }
  return final;
}

} // namespace moba
