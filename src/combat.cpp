#include "moba/combat.hpp"

namespace moba {

Type post_mitigation_damage(const Type &raw_damage,
                            const Type &resistance) noexcept {
  if (resistance >= 0) {
    return raw_damage * 100.0 / (100.0 + resistance);
  }
  return raw_damage * (2.0 - (100.0 / (100.0 - resistance)));
}

Type mitigated_damage(Type raw_damage, TypeDamage type,
                      const Champion::Stats &target, Type flat_pen,
                      Type pct_pen) noexcept {
  if (type == TypeDamage::True) {
    return raw_damage;
  }
  const Stat resist_stat = (type == TypeDamage::Physical) ? Stat::AR : Stat::MR;
  Type res = target[std::to_underlying(resist_stat)];
  res = (res - flat_pen) * (1.0 - pct_pen);
  return post_mitigation_damage(raw_damage, res);
}

DamageAfterShield apply_damage_to_shield(Type shield, Type current_hp,
                                         Type mitigated) noexcept {
  if (mitigated <= 0.0) {
    return {.shield_remaining = shield, .hp_remaining = current_hp};
  }
  if (shield >= mitigated) {
    return {.shield_remaining = shield - mitigated, .hp_remaining = current_hp};
  }
  return {.shield_remaining = 0.0,
          .hp_remaining = current_hp - (mitigated - shield)};
}

} // namespace moba