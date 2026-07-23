#pragma once

#include "moba/champion.hpp"
#include "moba/event.hpp"
#include "moba/signal.hpp"

#include <cstddef>
#include <deque>
#include <vector>

namespace moba {

// Symulacja walki wielu championów. Dwa mechanizmy:
//
// 1. dispatchEvent(event) — główny entry point:
//    a) Wewnętrzne reguły (std::visit):
//    AttackHit→mitigation→DamageReceived+DamageDealt,
//       DamageDealt→lifesteal+omnivamp→HealApplied, DamageReceived→HP
//       loss→Death, HealApplied→HP gain (cap MaxHP)
//    b) Observer signals (synchroniczne, side-effect-free: logging, UI)
//    c) Broadcast eventu do wszystkich pasyw wszystkich championów
//    d) Re-ewaluacja (fixed-point)
//    e) Flush kolejki (chained events z emitted_events, aż do max_iter)
//
// 2. Observer signals (onAttackHit, onDeath, ...) — dla reakcji bez modyfikacji
// statów.
struct Simulation {
  std::vector<Champion> champions;

  // Observer signals — side-effect-free (logging, UI, meta).
  Signal<AttackHit> onAttackHit;
  Signal<DamageDealt> onDamageDealt;
  Signal<DamageReceived> onDamageReceived;
  Signal<HealApplied> onHealApplied;
  Signal<Death> onDeath;

  Simulation();
  ~Simulation(); // wywołuje clearSignals()

  // Usuń wszystkich subskrybentów i wyczyść kolejkę. Rozerwij cykle
  // referencyjne.
  void clearSignals();

  // Główny entry point: dispatch event przez reguły + pasywy + re-ewaluację.
  void dispatchEvent(const PassiveEvent &event, Type eps = 0.01,
                     std::size_t max_iter = 10000);

  // Re-ewaluj wszystkich championów z pasywkami (fixed-point).
  void evaluateAll(Type eps = 0.01, std::size_t max_iter = 10000);

private:
  std::deque<PassiveEvent> event_queue_;
  void processInternalRules(const PassiveEvent &ev); // reguły gry (std::visit)
  void broadcastToPassives(const PassiveEvent &ev);  // dispatch do pasyw
};

} // namespace moba