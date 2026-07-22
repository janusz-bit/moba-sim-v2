#pragma once

#include "moba/signal.hpp"
#include "moba/source.hpp"
#include "moba/types.hpp"

#include <cstddef>

namespace moba {

/// @defgroup events Typed Events
/// @brief Event structs dispatched by `Simulation` via `Signal`s.
///
/// Each event carries the data its subscribers need. A `Simulation` owns one
/// `Signal` per event type. Internal handlers (damage application, death
/// detection, lifesteal, healing) are wired in the `Simulation` constructor;
/// user code subscribes to the same signals to react or chain new events.
///
/// @{

/// Emitted when an attack hits a target. The `Simulation` internally
/// converts this into `DamageReceived` + `DamageDealt` after computing
/// mitigation (armor/magic resist/penetration).
struct AttackHit {
  std::size_t actor_id{};  ///< Index of the attacking champion
  std::size_t target_id{}; ///< Index of the target champion
  Type amount{};           ///< Raw (pre-mitigation) damage amount
  TypeDamage damage_type;  ///< Physical, Magic, or True
  Source source; ///< Provenance chain (e.g. "Basic attack" → champion)
  Type time{};   ///< Simulation time of the hit
};

/// Emitted alongside `DamageReceived` when damage is dealt. Used by the
/// internal lifesteal/omnivamp handler and by user reactions
/// (e.g. on-hit effects).
struct DamageDealt {
  std::size_t actor_id{};  ///< Index of the dealing champion
  std::size_t target_id{}; ///< Index of the receiving champion
  Type amount{};           ///< Post-mitigation damage amount
  TypeDamage damage_type;  ///< Physical, Magic, or True
  Source source;           ///< Provenance chain
  Type time{};             ///< Simulation time
};

/// Emitted when a champion receives damage. The `Simulation` internally
/// applies HP loss (shield absorbs first) and checks for death.
struct DamageReceived {
  std::size_t actor_id{};  ///< Index of the dealing champion
  std::size_t target_id{}; ///< Index of the receiving champion
  Type amount{};           ///< Post-mitigation damage amount
  TypeDamage damage_type;  ///< Physical, Magic, or True
  Source source;           ///< Provenance chain
  Type time{};             ///< Simulation time
};

/// Emitted when a heal is applied. The `Simulation` internally applies HP
/// gain (capped at MaxHP). User code can emit this to heal champions.
struct HealApplied {
  std::size_t target_id{}; ///< Index of the champion being healed
  Type amount{};           ///< Heal amount (before MaxHP cap)
  Source source;           ///< Provenance chain (e.g. "Lifesteal")
  Type time{};             ///< Simulation time
};

/// Emitted when a champion's HP reaches zero. The `Simulation` emits this
/// internally from the `DamageReceived` handler.
struct Death {
  std::size_t actor_id{};  ///< Index of the killer
  std::size_t target_id{}; ///< Index of the victim
  Source source;           ///< Provenance chain
  Type time{};             ///< Simulation time
};

/// @}

} // namespace moba