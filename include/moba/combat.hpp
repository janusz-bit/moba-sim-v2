#pragma once

#include "moba/champion.hpp"
#include "moba/types.hpp"

namespace moba {

// Post-mitigation damage after resistance reduction.
// See: https://wiki.leagueoflegends.com/en-us/Armor
Type post_mitigation_damage(const Type &raw_damage,
                            const Type &resistance) noexcept;

// Damage after applying flat and percentage penetration, then resistance
// mitigation. True damage bypasses mitigation. `target` is the target's
// final stats (resistance read from AR/MR depending on `type`).
[[nodiscard]] Type mitigated_damage(Type raw_damage, TypeDamage type,
                                    const Champion::Stats &target,
                                    Type flat_pen = 0.0,
                                    Type pct_pen = 0.0) noexcept;

// Apply damage to shield first, then CurrentHP. Returns remaining shield and
// HP after absorption. Shield absorbs all damage types (normal shield per LoL
// Wiki). Resistance mitigation is applied BEFORE the shield absorbs.
// See: https://wiki.leagueoflegends.com/en-us/Shield
struct DamageAfterShield {
  Type shield_remaining;
  Type hp_remaining;
};
[[nodiscard]] DamageAfterShield
apply_damage_to_shield(Type shield, Type current_hp, Type mitigated) noexcept;

// Convenience accessors for Stats indexed by Stat enum.
[[nodiscard]] inline Type getStat(const Champion::Stats &stats, Stat stat) {
  return stats[std::to_underlying(stat)];
}
inline void setStat(Champion::Stats &stats, Stat stat, Type value) {
  stats[std::to_underlying(stat)] = value;
}

} // namespace moba