#pragma once

#include "moba/champion.hpp"
#include "moba/event.hpp"
#include "moba/signal.hpp"

#include <cstddef>
#include <vector>

namespace moba {

/// Signal-based event dispatch for multi-champion combat.
///
/// Each event type is a `Signal<EventType>`. The constructor wires internal
/// handlers that implement the core game rules:
///
/// - **AttackHit** → compute mitigated damage → emit `DamageReceived` +
/// `DamageDealt`
/// - **DamageDealt** → lifesteal (physical only) + omnivamp (all types) → emit
/// `HealApplied`
/// - **DamageReceived** → apply HP loss (shield absorbs) → emit `Death` if HP ≤
/// 0
/// - **HealApplied** → apply HP gain (cap at MaxHP)
///
/// User code subscribes to the same signals to react or chain new events.
///
/// @par Example
/// ```cpp
/// Simulation sim;
/// sim.champions.push_back(attacker);
/// sim.champions.push_back(target);
/// sim.onDamageReceived.subscribe([&](const DamageReceived &ev) {
///   // shield proc, counter-attack, etc.
/// });
/// sim.onAttackHit.emit({0, 1, 100.0, TypeDamage::Physical, src, 0.0});
/// ```
struct Simulation {
  /// List of all champions in the simulation, indexed by
  /// `actor_id`/`target_id`.
  std::vector<Champion> champions;

  // --- Signals (one per event type) ---

  Signal<AttackHit> onAttackHit;     ///< Emitted by user to trigger an attack
  Signal<DamageDealt> onDamageDealt; ///< Emitted internally from AttackHit
  Signal<DamageReceived>
      onDamageReceived; ///< Emitted internally from AttackHit
  Signal<HealApplied>
      onHealApplied;     ///< Emitted by user or internally (lifesteal)
  Signal<Death> onDeath; ///< Emitted internally when HP ≤ 0

  /// Constructor — wires all internal handlers to the signals.
  Simulation();

  /// Destructor — calls `clearSignals()` to break reference cycles.
  ~Simulation();

  /// Drop all signal subscribers (internal handlers + user callbacks).
  /// Call this to break reference cycles before destruction or when
  /// resetting the simulation state.
  void clearSignals();

  /// Re-evaluate all champions that have passives (fixed-point).
  /// @param eps      Convergence threshold (default 0.01).
  /// @param max_iter Maximum iterations (default 10000).
  void evaluateAll(Type eps = 0.01, std::size_t max_iter = 10000);
};

} // namespace moba