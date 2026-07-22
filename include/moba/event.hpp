#pragma once

#include "moba/source.hpp"
#include "moba/types.hpp"

#include <cstddef>

namespace moba {

enum class EventKind : std::uint8_t {
  AttackHit,
  DamageDealt,
  DamageReceived,
  HealApplied,
  ShieldBroken,
  CCApplied,
  Kill,
  Death,
};

struct GameEvent {
  EventKind kind{};
  std::size_t actor_id = 0;  // index in Simulation::champions
  std::size_t target_id = 0; // index (may equal actor_id for self-heal)
  Type amount = 0.0;         // raw damage / heal / etc.
  TypeDamage damage_type = TypeDamage::True;
  Source source;   // provenance chain
  Type time = 0.0; // when it happened

  bool operator==(const GameEvent &) const = default;
};

} // namespace moba