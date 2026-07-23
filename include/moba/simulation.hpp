#pragma once

#include "moba/champion.hpp"
#include "moba/event.hpp"
#include "moba/signal.hpp"

#include <cstddef>
#include <deque>
#include <vector>

namespace moba {

/// Signal-based event dispatch for multi-champion combat.
///
/// Two mechanisms coexist:
///
/// 1. **Passive dispatch** — `dispatchEvent(event)` broadcasts the event to
///    all passives of all champions. Passives receive the event as their 4th
///    argument, can return modifiers (folded into the pipeline) and emit new
///    events (chained via the event queue). Internal game rules
///    (AttackHit→DamageReceived, lifesteal, death, heal) are applied first
///    via `std::visit`.
///
/// 2. **Observer signals** — `Signal<EventType>` for each event type. User
///    code subscribes for side-effect-free reactions (logging, UI, meta).
///    Signals fire synchronously after the internal rules and before passive
///    dispatch.
///
/// @par Example
/// ```cpp
/// Simulation sim;
/// sim.add_champion(attacker);
/// sim.add_champion(target);
/// // Observer: log deaths
/// sim.onDeath.subscribe([](const Death &ev) { log(ev); });
/// // Passive: shield on damage (via dispatchEvent, not signals)
/// target.add_passive(factory.make([](auto&, auto&, auto t, const auto& ev) {
///   if (std::holds_alternative<DamageReceived>(ev))
///     return Champion::PassiveResult{{{Stat::ShieldHP, ModType::Base, 200.0,
///     {}}}, false};
///   return Champion::PassiveResult{};
/// }));
/// sim.onAttackHit.emit({0, 1, 100.0, TypeDamage::Physical, src, 0.0});
/// ```
struct Simulation {
  /// List of all champions in the simulation, indexed by
  /// `actor_id`/`target_id`.
  std::vector<Champion> champions;

  // --- Observer signals (side-effect-free reactions) ---

  Signal<AttackHit> onAttackHit;
  Signal<DamageDealt> onDamageDealt;
  Signal<DamageReceived> onDamageReceived;
  Signal<HealApplied> onHealApplied;
  Signal<Death> onDeath;

  Simulation();
  ~Simulation();

  /// Drop all signal subscribers and event queue.
  /// Call this to break reference cycles before destruction.
  void clearSignals();

  /// Dispatch an event through the full pipeline:
  /// 1. Internal game rules (AttackHit→mitigation→DamageReceived+DamageDealt,
  ///    DamageReceived→HP loss→Death, HealApplied→HP gain, lifesteal/omnivamp)
  /// 2. Observer signals (synchronous, side-effect-free)
  /// 3. Broadcast to all passives of all champions (stat mods + new events)
  /// 4. Re-evaluate affected champions (fixed-point)
  /// 5. Flush event queue (chained events, up to max_iter)
  ///
  /// @param event The event to dispatch.
  /// @param eps Fixed-point convergence threshold.
  /// @param max_iter Maximum iterations for the event queue flush.
  void dispatchEvent(const PassiveEvent &event, Type eps = 0.01,
                     std::size_t max_iter = 10000);

  /// Convenience: re-evaluate all champions with passives (fixed-point).
  void evaluateAll(Type eps = 0.01, std::size_t max_iter = 10000);

private:
  std::deque<PassiveEvent> event_queue_;

  void processInternalRules(const PassiveEvent &ev);
  void broadcastToPassives(const PassiveEvent &ev);
};

} // namespace moba