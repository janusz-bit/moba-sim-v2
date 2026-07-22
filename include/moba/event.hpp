#pragma once

#include "moba/signal.hpp"
#include "moba/source.hpp"
#include "moba/types.hpp"

#include <cstddef>

namespace moba {

// --- Typed event structs ---
//
// Each event carries the data its subscribers need.  A Simulation owns one
// Signal per event type; interested parties call `sim.onFoo.subscribe(...)`.
// Internal handlers (damage application, death detection, ...) are wired in
// the Simulation constructor, so the framework provides the default rules
// and user passives hook in alongside them.

struct AttackHit {
  std::size_t actor_id{};
  std::size_t target_id{};
  Type amount{};
  TypeDamage damage_type;
  Source source;
  Type time{};
};

struct DamageDealt {
  std::size_t actor_id{};
  std::size_t target_id{};
  Type amount{}; // post-mitigation
  TypeDamage damage_type;
  Source source;
  Type time{};
};

struct DamageReceived {
  std::size_t actor_id{};
  std::size_t target_id{};
  Type amount{}; // post-mitigation
  TypeDamage damage_type;
  Source source;
  Type time{};
};

struct HealApplied {
  std::size_t target_id{};
  Type amount{};
  Source source;
  Type time{};
};

struct Death {
  std::size_t actor_id{};  // killer
  std::size_t target_id{}; // victim
  Source source;
  Type time{};
};

} // namespace moba