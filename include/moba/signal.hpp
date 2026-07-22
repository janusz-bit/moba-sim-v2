#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace moba {

// A typed signal: subscribers are notified when emit() is called.
// Safe for unsubscribe-during-emit (dead listeners are cleaned up after
// the outermost emit returns). New subscribers added during emit are
// not called until the next emit.
template <typename... Args> class Signal {
public:
  using ListenerID = std::size_t;
  using Callback = std::function<void(Args...)>;

  ListenerID subscribe(Callback cb) {
    ListenerID id = next_id_++;
    listeners_.push_back({id, std::move(cb), false});
    return id;
  }

  void unsubscribe(ListenerID id) {
    for (auto &l : listeners_) {
      if (l.id == id) {
        l.dead = true;
      }
    }
  }

  void emit(Args... args) {
    ++emit_depth_;
    const std::size_t n = listeners_.size();
    for (std::size_t i = 0; i < n; ++i) {
      if (!listeners_[i].dead && listeners_[i].callback) {
        listeners_[i].callback(args...);
      }
    }
    if (--emit_depth_ == 0) {
      std::erase_if(listeners_, [](const Listener &l) { return l.dead; });
    }
  }

  // Drop all subscribers. Call this before destruction to break reference
  // cycles (e.g. Python callbacks capturing the Simulation).
  void clear() { listeners_.clear(); }

private:
  struct Listener {
    ListenerID id;
    Callback callback;
    bool dead;
  };
  std::vector<Listener> listeners_;
  ListenerID next_id_ = 0;
  int emit_depth_ = 0;
};

} // namespace moba