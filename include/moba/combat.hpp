#pragma once

#include "moba/champion.hpp"
#include "moba/types.hpp"

namespace moba {

// Obrażenia po redukcji przez resistance (armor/magic resist).
// Dodatni resistance: raw * 100/(100+res). Ujemny: raw * (2 - 100/(100-res)),
// max 200%.
Type post_mitigation_damage(const Type &raw_damage,
                            const Type &resistance) noexcept;

// Obrażzenia po penetracji (flat + %) i mitigacji. True damage omija resist.
[[nodiscard]] Type mitigated_damage(Type raw_damage, TypeDamage type,
                                    const Champion::Stats &target,
                                    Type flat_pen = 0.0,
                                    Type pct_pen = 0.0) noexcept;

// Wynik absorpcji: shield przyjmuje obrażenia przed HP.
struct DamageAfterShield {
  Type shield_remaining;
  Type hp_remaining;
};

// Shield absorbuje przed HP. damage ≤ 0 = no-op.
[[nodiscard]] DamageAfterShield
apply_damage_to_shield(Type shield, Type current_hp, Type mitigated) noexcept;

// Convenience: czytaj/zapisz stat z array po enum index.
[[nodiscard]] inline Type getStat(const Champion::Stats &stats, Stat stat) {
  return stats[std::to_underlying(stat)];
}

inline void setStat(Champion::Stats &stats, Stat stat, Type value) {
  stats[std::to_underlying(stat)] = value;
}

} // namespace moba