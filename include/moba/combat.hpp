#pragma once

#include "moba/champion.hpp"
#include "moba/types.hpp"

namespace moba {

/// @defgroup combat Combat Helpers
/// @brief Damage calculation and shield absorption utilities.
/// @{

/// Compute post-mitigation damage after resistance reduction.
///
/// For positive resistance:
/// `post_mitigation = raw * 100 / (100 + resistance)`
///
/// For negative resistance (amplification, capped at 200%):
/// `post_mitigation = raw * (2 - 100 / (100 - resistance))`
///
/// @param raw_damage Raw damage amount.
/// @param resistance Effective resistance (Armor or Magic Resist).
/// @return Damage after mitigation.
/// @see https://wiki.leagueoflegends.com/en-us/Armor
Type post_mitigation_damage(const Type &raw_damage,
                            const Type &resistance) noexcept;

/// Compute damage after applying penetration and resistance mitigation.
///
/// Effective resistance = `(resistance - flat_pen) * (1 - pct_pen)`,
/// then `post_mitigation_damage()` is applied. True damage bypasses
/// mitigation entirely.
///
/// @param raw_damage Raw damage amount.
/// @param type       Damage type (Physical → AR, Magic → MR, True → bypass).
/// @param target     Target's stats (resistance read from AR/MR).
/// @param flat_pen   Flat penetration (default 0.0).
/// @param pct_pen    Percentage penetration, 0.0–1.0 (default 0.0).
/// @return Post-mitigation damage amount.
[[nodiscard]] Type mitigated_damage(Type raw_damage, TypeDamage type,
                                    const Champion::Stats &target,
                                    Type flat_pen = 0.0,
                                    Type pct_pen = 0.0) noexcept;

/// Result of `apply_damage_to_shield()`: remaining shield and HP.
struct DamageAfterShield {
  Type shield_remaining; ///< Shield HP left after absorption
  Type hp_remaining;     ///< Current HP left after absorption
};

/// Apply damage to shield first, then CurrentHP.
///
/// - If `shield >= damage`: shield absorbs all, HP untouched.
/// - If `shield < damage`: shield = 0, HP loses the remainder.
/// - If `damage <= 0`: nothing happens (negative damage doesn't heal).
///
/// Resistance mitigation is applied BEFORE the shield absorbs.
///
/// @param shield    Current shield amount.
/// @param current_hp Current HP.
/// @param mitigated  Post-mitigation damage to apply.
/// @return Remaining shield and HP.
/// @see https://wiki.leagueoflegends.com/en-us/Shield
[[nodiscard]] DamageAfterShield
apply_damage_to_shield(Type shield, Type current_hp, Type mitigated) noexcept;

/// @}

/// @defgroup stat_accessors Stat Accessors
/// @brief Convenience functions for reading/writing `Champion::Stats`.
/// @{

/// Read a stat value from a `Stats` array by enum index.
/// @param stats The stats array.
/// @param stat  Which stat to read.
/// @return The stat's value.
[[nodiscard]] inline Type getStat(const Champion::Stats &stats, Stat stat) {
  return stats[std::to_underlying(stat)];
}

/// Write a stat value into a `Stats` array by enum index.
/// @param stats The stats array (modified in place).
/// @param stat  Which stat to write.
/// @param value The value to set.
inline void setStat(Champion::Stats &stats, Stat stat, Type value) {
  stats[std::to_underlying(stat)] = value;
}

/// @}

} // namespace moba