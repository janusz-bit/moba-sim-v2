#pragma once

#include "moba/champion.hpp"
#include "moba/types.hpp"

namespace moba {

Type post_mitigation_damage(const Type &raw_damage,
                            const Type &resistance) noexcept;

[[nodiscard]] Type mitigated_damage(Type raw_damage, TypeDamage type,
                                    const Champion::Stats &target,
                                    Type flat_pen = 0.0,
                                    Type pct_pen = 0.0) noexcept;

struct DamageAfterShield {
  Type shield_remaining;
  Type hp_remaining;
};

[[nodiscard]] DamageAfterShield
apply_damage_to_shield(Type shield, Type current_hp, Type mitigated) noexcept;

[[nodiscard]] inline Type getStat(const Champion::Stats &stats, Stat stat) {
  return stats[std::to_underlying(stat)];
}

inline void setStat(Champion::Stats &stats, Stat stat, Type value) {
  stats[std::to_underlying(stat)] = value;
}

} // namespace moba