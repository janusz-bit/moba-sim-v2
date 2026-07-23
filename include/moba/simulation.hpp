#pragma once

#include "moba/champion.hpp"
#include "moba/event.hpp"
#include "moba/signal.hpp"

#include <cstddef>
#include <deque>
#include <vector>

namespace moba {

struct Simulation {
  std::vector<Champion> champions;

  Signal<AttackHit> onAttackHit;
  Signal<DamageDealt> onDamageDealt;
  Signal<DamageReceived> onDamageReceived;
  Signal<HealApplied> onHealApplied;
  Signal<Death> onDeath;

  Simulation();
  ~Simulation();

  void clearSignals();
  void dispatchEvent(const PassiveEvent &event, Type eps = 0.01,
                     std::size_t max_iter = 10000);
  void evaluateAll(Type eps = 0.01, std::size_t max_iter = 10000);

private:
  std::deque<PassiveEvent> event_queue_;
  void processInternalRules(const PassiveEvent &ev);
  void broadcastToPassives(const PassiveEvent &ev);
};

} // namespace moba