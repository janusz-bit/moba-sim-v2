#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace moba {

/// Typed signal (publish-subscribe). Subscribers are notified when `emit()`
/// is called with matching arguments.
///
/// Safe for unsubscribe-during-emit: dead listeners are cleaned up after the
/// outermost `emit()` returns. New subscribers added during emit are not called
/// until the next emit.
///
/// Used by `Simulation` to dispatch typed events (`AttackHit`,
/// `DamageReceived`, etc.). User code subscribes callbacks to react to game
/// events.
///
/// @tparam Args Event argument types (typically a single event struct).
template <typename... Args> class Signal {
public:
  /// Opaque identifier returned by `subscribe()`, used for `unsubscribe()`.
  using ListenerID = std::size_t;

  /// Callback type: `void(const Args&...)` or `void(Args...)`.
  using Callback = std::function<void(Args...)>;

  /// Register a callback to be called on `emit()`.
  /// @param cb The callback function / lambda.
  /// @return Listener ID for later `unsubscribe()`.
  ListenerID subscribe(Callback cb) {
    ListenerID id = next_id_++;
    listeners_.push_back({id, std::move(cb), false});
    return id;
  }

  /// Mark a subscriber as dead. The callback is removed after the current
  /// emit chain completes (deferred cleanup).
  /// @param id Listener ID returned by `subscribe()`.
  void unsubscribe(ListenerID id) {
    for (auto &l : listeners_) {
      if (l.id == id) {
        l.dead = true;
      }
    }
  }

  /// Notify all live subscribers with the given arguments.
  /// Calls are synchronous and in subscription order.
  /// @param args Arguments forwarded to each callback.
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

  /// Drop all subscribers immediately. Call this to break reference cycles
  /// (e.g. Python callbacks capturing the `Simulation`) before destruction.
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