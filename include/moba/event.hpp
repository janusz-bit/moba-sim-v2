#pragma once

#include "moba/signal.hpp"
#include "moba/source.hpp"
#include "moba/types.hpp"

#include <cstddef>
#include <variant>

namespace moba {

/// @defgroup events Typed Events
/// @brief Event structs dispatched by `Simulation` via `Signal`s and
/// `PassiveEvent`.
///
/// Each event carries the data its subscribers need. A `Simulation` owns
/// observer `Signal`s for each event type (logging, UI, meta-systems) and
/// dispatches events to passives via `PassiveEvent` (stat modification,
/// fixed-point participation).
///
/// @{

/// Emitted when an attack hits a target. The `Simulation` internally
/// converts this into `DamageReceived` + `DamageDealt` after computing
/// mitigation (armor/magic resist/penetration).
struct AttackHit {
  std::size_t actor_id{};
  std::size_t target_id{};
  Type amount{};
  TypeDamage damage_type{};
  Source source;
  Type time{};
};

/// Emitted alongside `DamageReceived` when damage is dealt. Used by the
/// internal lifesteal/omnivamp handler and by user reactions
/// (e.g. on-hit effects).
struct DamageDealt {
  std::size_t actor_id{};
  std::size_t target_id{};
  Type amount{};
  TypeDamage damage_type{};
  Source source;
  Type time{};
};

/// Emitted when a champion receives damage. The `Simulation` internally
/// applies HP loss (shield absorbs first) and checks for death.
struct DamageReceived {
  std::size_t actor_id{};
  std::size_t target_id{};
  Type amount{};
  TypeDamage damage_type{};
  Source source;
  Type time{};
};

/// Emitted when a heal is applied. The `Simulation` internally applies HP
/// gain (capped at MaxHP). User code can emit this to heal champions.
struct HealApplied {
  std::size_t target_id{};
  Type amount{};
  Source source;
  Type time{};
};

/// Emitted when a champion's HP reaches zero. The `Simulation` emits this
/// internally from the `DamageReceived` handler.
struct Death {
  std::size_t actor_id{};
  std::size_t target_id{};
  Source source;
  Type time{};
};

/// @}

/// Variant of all event types, plus `std::monostate` for "no event" (normal
/// stat evaluation). Passives receive this as their 4th argument.
///
/// - `std::monostate` — normal evaluation (evaluateChampion / applyPassives)
/// - Concrete event — the passive can react (e.g. shield on DamageReceived)
///
/// @see Champion::Passive
using PassiveEvent = std::variant<std::monostate, AttackHit, DamageDealt,
                                  DamageReceived, HealApplied, Death>;

} // namespace moba