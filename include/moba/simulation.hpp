#pragma once

#include "moba/champion.hpp"
#include "moba/event.hpp"

#include <cstddef>
#include <deque>
#include <utility>
#include <vector>

namespace moba {

// Simulation: event queue + cross-champion dispatch.
struct Simulation {
  std::vector<Champion> champions;
  std::deque<GameEvent> event_queue;

  void enqueue(GameEvent ev) { event_queue.push_back(std::move(ev)); }

  // Process the event queue FIFO. For each event:
  //   1. AttackHit -> compute mitigated damage -> enqueue DamageReceived
  //   2. Broadcast event to all passives of all champions with on_event
  //   3. Collect emitted events -> append to queue
  //   4. Re-evaluate affected champions (fixed-point)
  // Repeats until queue empty or max_iter exceeded.
  void processEvents(Type eps = 0.01, std::size_t max_iter = 10000);
};

} // namespace moba