#pragma once

#include "moba/event.hpp"
#include "moba/mod_db.hpp"
#include "moba/source.hpp"
#include "moba/types.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <utility>
#include <vector>

namespace moba {

/// A champion with base stats (via `mod_db`) and passive effects.
///
/// The champion pipeline:
/// ```
/// mod_db (modifiers) -> getBaseStats() -> passives -> final stats
/// ```
///
/// Passives are `std::function` callbacks that receive `(base, final, time)`
/// and return typed modifiers + an `alive` flag. The passive is the sole
/// authority on its lifetime (permanent, one-shot, or temp).
///
/// @see PassiveResult, Passive, PassiveEntry, PassiveFactory
struct Champion {
  /// Fixed-size array of all stats, indexed by `Stat` enum.
  using Stats = std::array<Type, std::to_underlying(Stat::Count)>;

  /// Construct a champion with base stats from an initializer list.
  /// @param stats List of (Stat, value) pairs. Each pair adds a `Base`
  ///              modifier to `mod_db` with source `"Base"`.
  /// @code
  /// Champion c{{Stat::MaxHP, 1000}, {Stat::AD, 50}, {Stat::AR, 100}};
  /// @endcode
  Champion(std::initializer_list<std::pair<Stat, Type>> stats);
  Champion() = default; ///< Default-constructed: all stats zero, no passives.

  /// Result returned by a `Passive` callback.
  ///
  /// - `mods` are folded into a copy of `mod_db` and run through the full
  ///   Base/Inc/More pipeline, so a passive can express additive (Base),
  ///   percent-increase (Inc), or multiplicative (More) effects.
  /// - `emitted_events` are new events to be dispatched by the Simulation
  ///   after the current evaluation step (enables reactive chaining).
  /// - `alive` controls the passive's lifetime:
  ///   - `true`  — passive stays in the queue (permanent / temp still active)
  ///   - `false` — passive is removed after its mods are applied (one-shot /
  ///     expired)
  struct PassiveResult {
    std::vector<Modifier> mods; ///< Modifiers to fold into the pipeline
    std::vector<PassiveEvent> emitted_events; ///< Events to dispatch after
    bool alive = true; ///< Whether this passive stays alive

    /// @param m Modifiers (defaults to empty).
    /// @param a Alive flag (defaults to `true`).
    /// @param ev Emitted events (defaults to empty).
    PassiveResult(std::vector<Modifier> m = {}, bool a = true,
                  std::vector<PassiveEvent> ev = {})
        : mods(std::move(m)), emitted_events(std::move(ev)), alive(a) {}
  };

  /// Passive callback signature.
  ///
  /// @param base  Stats from `mod_db` (without passives) — informational.
  /// @param final Current result from the previous iteration.
  /// @param time  Absolute simulation time (starts at 0, only increases).
  /// @param event The event being dispatched, or `std::monostate` for normal
  ///              stat evaluation. Passives can inspect the event to react
  ///              (e.g. grant shield on `DamageReceived`).
  /// @return `PassiveResult` with mods, emitted events, and alive flag.
  using Passive =
      std::function<PassiveResult(const Stats &base, const Stats &final,
                                  const Type &time, const PassiveEvent &event)>;

  /// A passive paired with an id and source. The id is used by
  /// `addPassive()` to deduplicate: inserting an entry whose id already
  /// exists replaces the existing passive (refresh), otherwise a new entry
  /// is appended.
  struct PassiveEntry {
    std::size_t id = 0; ///< Deduplication id
    Source source;      ///< Provenance of this passive
    Passive passive;    ///< The callback

    /// @param id_ Unique id for deduplication.
    /// @param p   The passive callback.
    /// @param src Source provenance (defaults to empty).
    PassiveEntry(std::size_t id_, Passive p, Source src = {})
        : id(id_), source(std::move(src)), passive(std::move(p)) {}
  };

  /// Vector of `PassiveEntry`.
  using Passives = std::vector<PassiveEntry>;

  /// Factory that creates `PassiveEntry` instances with auto-incremented ids.
  /// Each call to `make()` returns an entry with a unique id.
  class PassiveFactory {
    std::size_t next_id_ = 0;

  public:
    /// Create a `PassiveEntry` with an auto-incremented id.
    /// @param p   The passive callback.
    /// @param src Source provenance (defaults to empty).
    /// @return A new `PassiveEntry` with a unique id.
    [[nodiscard]] PassiveEntry make(Passive p, Source src = {}) {
      return PassiveEntry{next_id_++, std::move(p), std::move(src)};
    }
  };

  ModDB mod_db;      ///< Modifier database (base stats, items, runes)
  Passives passives; ///< Queue of passive effects

  /// Compute base stats from `mod_db` (without passives).
  /// @return `Stats` array after the full Base/Inc/More pipeline.
  [[nodiscard]] Stats getBaseStats() const;

  /// Insert a passive entry, deduplicating by id.
  /// If an entry with the same id exists, its passive is replaced (refresh);
  /// otherwise the entry is appended.
  /// @param entry The passive entry to insert or refresh.
  void addPassive(PassiveEntry entry);

  /// Apply all passives in a single step.
  ///
  /// 1. Copy `mod_db` into a working set.
  /// 2. For each passive: call it, fold its mods into the working set.
  /// 3. If `alive=false`, remove the passive from the queue.
  /// 4. Run the full Base/Inc/More pipeline on the working set.
  ///
  /// @param base  Base stats (passed to passives as informational input).
  /// @param final Current result (passed to passives).
  /// @param time  Simulation time (default 0.0).
  /// @return Stats after the pipeline. Mutates `passives` (removes expired).
  Stats applyPassives(const Stats &base, const Stats &final,
                      const Type &time = 0.0);

  /// Maximum absolute difference between two stats arrays (per-element).
  /// Used for convergence checking in `evaluateChampion()`.
  /// @param stats1 First stats array.
  /// @param stats2 Second stats array.
  /// @return Max `|stats2[i] - stats1[i]|` over all `i`.
  [[nodiscard]] static Type getDeltaStats(const Stats &stats1,
                                          const Stats &stats2);

  /// Iteratively resolve all passives to a fixed-point.
  ///
  /// Repeatedly applies all passives until stats converge (delta <= eps)
  /// or `max_iter` is exceeded. Passives are NOT removed during iteration;
  /// after convergence, passives that returned `alive=false` on the final
  /// iteration are removed.
  ///
  /// @param eps      Convergence threshold (default 0.01).
  /// @param max_iter Maximum iterations (default 1000).
  /// @param time     Simulation time passed to passives (default 0.0).
  /// @return Final stats after convergence.
  /// @throws ConvergenceError if not converged within `max_iter`.
  [[nodiscard]] Stats evaluateChampion(Type eps = 0.01,
                                       std::size_t max_iter = 1000,
                                       Type time = 0.0);
};

} // namespace moba