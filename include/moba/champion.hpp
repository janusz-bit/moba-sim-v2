#pragma once

#include "moba/event.hpp"
#include "moba/mod_db.hpp"
#include "moba/source.hpp"
#include "moba/types.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

namespace moba {

struct Champion {
  using Stats = std::array<Type, std::to_underlying(Stat::Count)>;

  // Construct a champion with base stats from an initializer list of
  // (Stat, value) pairs. Example: Champion c{{Stat::MaxHP, 1000}, {Stat::AD,
  // 50}};
  Champion(std::initializer_list<std::pair<Stat, Type>> stats);
  Champion() = default;

  // A Passive receives (base, final, time) and returns a list of typed
  // modifiers (Base/Inc/More) plus an `alive` flag. `time` is the absolute
  // simulation time (starts at 0, only increases). The passive is the sole
  // authority on its lifetime:
  //   - permanent: always returns `alive=true`
  //   - one-shot: returns `alive=false` after its single application
  //   - temp: returns `alive=false` once it decides to expire (e.g. by
  //     capturing a start time and checking `time - start < duration`)
  // Passives returning `alive=false` are removed after their mods are applied.
  // Mods are folded into a copy of mod_db, so a passive can express additive
  // (Base), percent-increase (Inc), or multiplicative (More) effects via the
  // standard pipeline.
  struct PassiveResult {
    std::vector<Modifier> mods;
    std::vector<GameEvent> emitted_events;
    bool alive = true;

    // Backward-compatible: PassiveResult{mods, alive}
    PassiveResult(std::vector<Modifier> m = {}, bool a = true,
                  std::vector<GameEvent> ev = {})
        : mods(std::move(m)), emitted_events(std::move(ev)), alive(a) {}
  };

  using Passive = std::function<PassiveResult(
      const Stats &base, const Stats &final, const Type &time)>;

  // An EventPassive reacts to a GameEvent and can emit new events + mods.
  using EventPassive =
      std::function<PassiveResult(const Stats &base, const Stats &final,
                                  const Type &time, const GameEvent &event)>;

  // A PassiveEntry pairs a passive with a numeric id, a source, an optional
  // event handler, and the passive itself. The id is used by
  // Champion::addPassive to deduplicate: inserting an entry whose id already
  // exists replaces the existing passive (refresh), otherwise a new entry is
  // appended.
  struct PassiveEntry {
    std::size_t id = 0;
    Source source;
    Passive passive;
    std::optional<EventPassive> on_event;

    PassiveEntry(std::size_t id_, Passive p, Source src = {},
                 std::optional<EventPassive> ev = {})
        : id(id_), source(std::move(src)), passive(std::move(p)),
          on_event(std::move(ev)) {}
  };

  using Passives = std::vector<PassiveEntry>;

  // PassiveFactory creates PassiveEntry instances with auto-incremented ids.
  class PassiveFactory {
    std::size_t next_id_ = 0;

  public:
    [[nodiscard]] PassiveEntry make(Passive p, Source src = {}) {
      return PassiveEntry{next_id_++, std::move(p), std::move(src)};
    }
    [[nodiscard]] PassiveEntry make(Passive p, Source src, EventPassive ev) {
      return PassiveEntry{next_id_++,
                          std::move(p),
                          std::move(src),
                          std::move(ev)};
    }
  };

  ModDB mod_db;
  Passives passives;

  [[nodiscard]] Stats getBaseStats() const;

  // Insert a passive entry, deduplicating by id: if an entry with the same id
  // already exists, its passive is replaced (refresh); otherwise the entry is
  // appended.
  void addPassive(PassiveEntry entry);

  // Applies all passives in a single step. Passives returning alive=false are
  // removed after their bonus is applied. Not const: mutates passives.
  Stats applyPassives(const Stats &base, const Stats &final,
                      const Type &time = 0.0);

  [[nodiscard]] static Type getDeltaStats(const Stats &stats1,
                                          const Stats &stats2);

  [[nodiscard]] Stats evaluateChampion(Type eps = 0.01,
                                       std::size_t max_iter = 1000,
                                       Type time = 0.0);
};

} // namespace moba