#include "moba_sim.hpp"
#include <algorithm>
namespace moba {

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
  }
  return "Unknown";
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

[[nodiscard]] Type ModDB::getSumStat(
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
[[nodiscard]] Type ModDB::getIncStat(
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
[[nodiscard]] Type ModDB::getMoreStat(
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
[[nodiscard]] Type
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

[[nodiscard]] Champion::Stats Champion::getBaseStats() const {
  Stats stats{};
  for (std::size_t i = 0; i < std::to_underlying(Stat::Count); ++i) {
    stats[i] = mod_db.getStat(static_cast<Stat>(i));
  }
  return stats;
}
Champion::Stats Champion::applyPassives(const Stats &base, const Stats &final) {
  Stats result = final;
  for (const auto &passive : passives) {
    result = passive(base, final);
  }
  return result;
}
[[nodiscard]] Type Champion::getDeltaStats(const Stats &stats1,
                                           const Stats &stats2) {
  Type delta = 0;
  for (std::size_t i = 0; i < std::to_underlying(Stat::Count); ++i) {
    Type delta_now = std::abs(stats2[i] - stats1[i]);
    delta = std::max(delta_now, delta);
  }
  return delta;
}
Champion::Stats Champion::evaluateChampion() {
  Stats stats = getBaseStats();
  const Stats base = stats;
  Stats final = stats;
  final = applyPassives(base, final);

  Type delta = getDeltaStats(base, final);
  Stats final_now = final;
  while (delta > 0.01) {
    final_now = applyPassives(base, final_now);
    delta = getDeltaStats(final_now, final);
    final = final_now;
  }
  return final;
}

} // namespace moba
