#pragma once

#include "moba/champion.hpp"
#include "moba/event.hpp"
#include "moba/signal.hpp"

#include <cstddef>
#include <vector>

namespace moba {

// Simulation: signal-based event dispatch.
//
// Each event type is a Signal<EventType>.  The Simulation constructor wires
// internal handlers that implement the core rules (damage application, death
// detection, healing).  User passives subscribe to the same signals to react
// or chain new events.
//
// Usage:
//   Simulation sim;
//   sim.champions.push_back(attacker);
//   sim.champions.push_back(target);
//   sim.onDamageReceived.subscribe([&](const DamageReceived &ev) { ... });
//   sim.onAttackHit.emit({0, 1, 100.0, TypeDamage::Physical, src, 0.0});
//
struct Simulation {
  std::vector<Champion> champions;

  // --- Signals (one per event type) ---
  Signal<AttackHit> onAttackHit;
  Signal<DamageDealt> onDamageDealt;
  Signal<DamageReceived> onDamageReceived;
  Signal<HealApplied> onHealApplied;
  Signal<Death> onDeath;

  Simulation();
  ~Simulation();

  // Drop all signal subscribers (internal handlers + user callbacks).
  // Call this to break reference cycles before destruction.
  void clearSignals();

  // Convenience: re-evaluate all champions with passives (fixed-point).
  void evaluateAll(Type eps = 0.01, std::size_t max_iter = 10000);
};

} // namespace moba