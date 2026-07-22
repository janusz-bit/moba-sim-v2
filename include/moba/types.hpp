#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace moba {

/// Numeric type used throughout the library (double precision).
using Type = double;

/// Champion statistic identifiers. Used to index into `Champion::Stats`.
///
/// The final value of a stat is computed via the Base/Inc/More pipeline:
/// `getStat(stat) = sum(stat) * inc(stat) * more(stat)`.
///
/// @see ModDB, ModType
enum class Stat : std::uint8_t {
  MaxHP,        ///< Maximum health pool
  CurrentHP,    ///< Current health (reduced by damage, restored by heals)
  Mana,         ///< Maximum mana pool
  CurrentMana,  ///< Current mana
  AP,           ///< Ability Power
  AD,           ///< Attack Damage
  MS,           ///< Movement Speed
  AR,           ///< Armor (reduces physical damage)
  MR,           ///< Magic Resist (reduces magic damage)
  CDR,          ///< Cooldown Reduction
  ArmorPenFlat, ///< Flat armor penetration
  ArmorPenPct,  ///< Percentage armor penetration (0.0–1.0)
  MagicPenFlat, ///< Flat magic penetration
  MagicPenPct,  ///< Percentage magic penetration (0.0–1.0)
  AttackSpeed,  ///< Attacks per second (e.g. 0.651)
  CritChance,   ///< Critical strike chance (0.0–1.0)
  CritDamage,   ///< Critical strike damage multiplier (e.g. 1.75 = 175%)
  LifeSteal,    ///< Heals % of post-mitigation physical damage dealt (0.0–1.0)
  Omnivamp,     ///< Heals % of all damage dealt — physical/magic/true (0.0–1.0)
  Tenacity,     ///< Reduces incoming CC duration (0.0–1.0)
  SlowResist,   ///< Reduces incoming slow duration (0.0–1.0)
  HealShieldPower, ///< Amplifies heals and shields (0.0–1.0)
  HPRegen,         ///< HP regenerated per 5 seconds
  MPRegen,         ///< Mana regenerated per 5 seconds
  ShieldHP,        ///< Active shield amount; absorbs damage before CurrentHP
  Count            ///< Sentinel — number of stats (for array sizing)
};

/// Modifier type in the Base/Inc/More pipeline.
///
/// - **Base** — additive bonuses summed: `10 + 20 + 30 = 60`
/// - **Inc**  — multiplicative increases summed from 1.0: `1.0 + 0.1 + 0.2
/// = 1.3`
/// - **More** — multiplicative multipliers from 1.0: `1.1 * 1.2 * 1.3 = 1.716`
///
/// Final stat = `sum(Base) * (1 + sum(Inc)) * product(More)`.
enum class ModType : std::uint8_t {
  Base, ///< Additive: `10 + 20 + 30`
  Inc,  ///< Percent increase: `1.0 + 0.1 + 0.2`
  More  ///< Multiplicative: `1.1 * 1.2 * 1.3`
};

/// Damage type classification.
enum class TypeDamage : std::uint8_t {
  Physical, ///< Reduced by Armor (AR)
  Magic,    ///< Reduced by Magic Resist (MR)
  True      ///< Bypasses all resistance mitigation
};

/// Thrown when `Champion::evaluateChampion()` fails to converge
/// within the specified iteration limit.
class ConvergenceError : public std::runtime_error {
public:
  /// @param msg Error message describing the convergence failure.
  explicit ConvergenceError(const std::string &msg) : std::runtime_error(msg) {}
};

} // namespace moba