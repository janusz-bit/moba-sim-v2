#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace moba {

// Wszystkie wartości w bibliotece są double.
using Type = double;

// 25 statystyk championa. Indeksuje Stats (std::array<double, 25>).
// Count jest sentinel do array sizing.
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

// Typ modyfikatora w potoku Base/Inc/More:
// Base = addytywny (10+20+30), Inc = % od 1.0 (1+0.1+0.2), More = mnożnik
// od 1.0 (1.1*1.2)
enum class ModType : std::uint8_t { Base, Inc, More };

// Typ obrażeń: Physical (AR), Magic (MR), True (omija resist)
enum class TypeDamage : std::uint8_t { Physical, Magic, True };

// Rzucane gdy evaluateChampion nie zbiegnie się w max_iter iteracjach.
class ConvergenceError : public std::runtime_error {
public:
  explicit ConvergenceError(const std::string &msg) : std::runtime_error(msg) {}
};

} // namespace moba