#include "moba_sim.hpp"

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
  case Stat::Armor:
    return "Armor";
  case Stat::MR:
    return "MR";
  }
  return "Unknown";
}

[[nodiscard]] Type ModDB::getSumStat(
    Stat stat, const std::function<bool(const Modifier &)> &predicate) const {
  Type total = 0.0;
  for (const auto &m : mods_) {
    if (predicate(m) && m.stat == stat && m.type == ModType::Base) {
      total += m.value;
    }
  }
  return total;
}
[[nodiscard]] Type ModDB::getIncStat(
    Stat stat, const std::function<bool(const Modifier &)> &predicate) const {
  Type total = 1.0;
  for (const auto &m : mods_) {
    if (predicate(m) && m.stat == stat && m.type == ModType::Inc) {
      total += m.value;
    }
  }
  return total;
}
[[nodiscard]] Type ModDB::getMoreStat(
    Stat stat, const std::function<bool(const Modifier &)> &predicate) const {
  Type total = 1.0;
  for (const auto &m : mods_) {
    if (predicate(m) && m.stat == stat && m.type == ModType::Inc) {
      total *= m.value;
    }
  }
  return total;
}

} // namespace moba
