#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace moba {

using Type = double;

enum class Stat : std::uint8_t {
  MaxHP,
  CurrentHP,
  Mana,
  CurrentMana,
  AP,
  AD,
  MS,
  AR,
  MR,
  CDR,
  ArmorPenFlat,
  ArmorPenPct,
  MagicPenFlat,
  MagicPenPct,
  AttackSpeed,
  CritChance,
  CritDamage,
  LifeSteal,
  Omnivamp,
  Tenacity,
  SlowResist,
  HealShieldPower,
  HPRegen,
  MPRegen,
  ShieldHP,
  Count
};

enum class ModType : std::uint8_t { Base, Inc, More };
enum class TypeDamage : std::uint8_t { Physical, Magic, True };

class ConvergenceError : public std::runtime_error {
public:
  explicit ConvergenceError(const std::string &msg) : std::runtime_error(msg) {}
};

} // namespace moba