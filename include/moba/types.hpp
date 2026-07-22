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
  // --- Offensive stats (LoL Wiki: Champion statistic) ---
  AttackSpeed, // attacks per second (e.g. 0.651)
  CritChance,  // 0.0-1.0 (e.g. 0.25 = 25%)
  CritDamage,  // multiplier (e.g. 1.75 = 175%)
  LifeSteal,   // 0.0-1.0, heals % of post-mitigation basic damage dealt
  Omnivamp,    // 0.0-1.0, heals % of all damage dealt (physical/magic/true)
  // --- Defensive stats ---
  Tenacity,   // 0.0-1.0, reduces incoming CC duration (capped at 1.0)
  SlowResist, // 0.0-1.0, reduces incoming slow duration
  // --- Utility stats ---
  HealShieldPower, // 0.0-1.0, amplifies heals and shields
  HPRegen,         // HP per 5 seconds
  MPRegen,         // Mana per 5 seconds
  ShieldHP,        // active shield amount; absorbs damage before CurrentHP
  Count
};

enum class ModType : std::uint8_t {
  Base, // 10 + 20 + 30
  Inc,  // 1.1 + 1.2 + 1.3
  More  // 1.1 * 1.2 * 1.3
};

enum class TypeDamage : std::uint8_t { Physical, Magic, True };

class ConvergenceError : public std::runtime_error {
public:
  explicit ConvergenceError(const std::string &msg) : std::runtime_error(msg) {}
};

} // namespace moba