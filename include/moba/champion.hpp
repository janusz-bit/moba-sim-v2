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

struct Champion {
  using Stats = std::array<Type, std::to_underlying(Stat::Count)>;

  Champion(std::initializer_list<std::pair<Stat, Type>> stats);
  Champion() = default;

  struct PassiveResult {
    std::vector<Modifier> mods;
    std::vector<PassiveEvent> emitted_events;
    bool alive = true;

    PassiveResult(std::vector<Modifier> m = {}, bool a = true,
                  std::vector<PassiveEvent> ev = {})
        : mods(std::move(m)), emitted_events(std::move(ev)), alive(a) {}
  };

  using Passive =
      std::function<PassiveResult(const Stats &base, const Stats &final,
                                  const Type &time, const PassiveEvent &event)>;

  struct PassiveEntry {
    std::size_t id = 0;
    Source source;
    Passive passive;

    PassiveEntry(std::size_t id_, Passive p, Source src = {})
        : id(id_), source(std::move(src)), passive(std::move(p)) {}
  };

  using Passives = std::vector<PassiveEntry>;

  class PassiveFactory {
    std::size_t next_id_ = 0;

  public:
    [[nodiscard]] PassiveEntry make(Passive p, Source src = {}) {
      return PassiveEntry{next_id_++, std::move(p), std::move(src)};
    }
  };

  ModDB mod_db;
  Passives passives;

  [[nodiscard]] Stats getBaseStats() const;
  void addPassive(PassiveEntry entry);
  Stats applyPassives(const Stats &base, const Stats &final,
                      const Type &time = 0.0);
  [[nodiscard]] static Type getDeltaStats(const Stats &stats1,
                                          const Stats &stats2);
  [[nodiscard]] Stats evaluateChampion(Type eps = 0.01,
                                       std::size_t max_iter = 1000,
                                       Type time = 0.0);
};

} // namespace moba