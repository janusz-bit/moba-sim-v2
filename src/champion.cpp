#include "moba/champion.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <variant>
#include <vector>

namespace moba {

namespace {

template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

Champion::Stats statsFromModDB(const ModDB &db) {
  Champion::Stats out{};
  for (std::size_t i = 0; i < std::to_underlying(Stat::Count); ++i) {
    out[i] = db.getStat(static_cast<Stat>(i));
  }
  return out;
}

std::pair<Champion::Stats, std::vector<bool>>
applyPassivesNoRemove(const ModDB &mod_db,
                      const std::vector<Champion::PassiveEntry> &passives,
                      const Champion::Stats &base, const Champion::Stats &final,
                      const Type &time, const PassiveEvent &event) {
  ModDB working = mod_db;
  std::vector<bool> alive_flags;
  for (const auto &entry : passives) {
    auto result = entry.passive(base, final, time, event);
    for (const auto &m : result.mods) {
      working.add(m.stat, m.type, m.value, m.source);
    }
    alive_flags.push_back(result.alive);
  }
  return {statsFromModDB(working), std::move(alive_flags)};
}

} // namespace

Champion::Champion(std::initializer_list<std::pair<Stat, Type>> stats) {
  Source src{"Base", ""};
  for (const auto &[stat, value] : stats) {
    mod_db.add(stat, ModType::Base, value, src);
  }
}

Champion::Stats Champion::getBaseStats() const {
  return statsFromModDB(mod_db);
}

void Champion::addPassive(PassiveEntry entry) {
  for (auto &e : passives) {
    if (e.id == entry.id) {
      e.passive = std::move(entry.passive);
      if (!entry.source.name.empty()) {
        e.source = std::move(entry.source);
      }
      return;
    }
  }
  passives.push_back(std::move(entry));
}

Champion::Stats Champion::applyPassives(const Stats &base, const Stats &final,
                                        const Type &time) {
  ModDB working = mod_db;
  for (auto it = passives.begin(); it != passives.end();) {
    auto result =
        it->passive(base, final, time, PassiveEvent{std::monostate{}});
    for (const auto &m : result.mods) {
      working.add(m.stat, m.type, m.value, m.source);
    }
    if (result.alive) {
      ++it;
    } else {
      it = passives.erase(it);
    }
  }
  return statsFromModDB(working);
}

Type Champion::getDeltaStats(const Stats &stats1, const Stats &stats2) {
  Type delta = 0;
  for (std::size_t i = 0; i < std::to_underlying(Stat::Count); ++i) {
    Type delta_now = std::abs(stats2[i] - stats1[i]);
    delta = std::max(delta_now, delta);
  }
  return delta;
}

Champion::Stats Champion::evaluateChampion(Type eps, std::size_t max_iter,
                                           Type time) {
  const Stats base = getBaseStats();
  Stats final = base;
  Stats prev = base;
  std::size_t iter = 0;
  std::vector<bool> alive_flags;
  PassiveEvent no_event{std::monostate{}};
  do {
    prev = final;
    auto [f, flags] =
        applyPassivesNoRemove(mod_db, passives, base, prev, time, no_event);
    final = f;
    alive_flags = std::move(flags);
    ++iter;
  } while (getDeltaStats(final, prev) > eps && iter < max_iter);
  if (iter >= max_iter && getDeltaStats(final, prev) > eps) {
    throw ConvergenceError(
        "evaluateChampion did not converge after " + std::to_string(max_iter) +
        " iterations (eps=" + std::to_string(eps) +
        ", delta=" + std::to_string(getDeltaStats(final, prev)) + ")");
  }
  if (alive_flags.size() == passives.size()) {
    std::size_t idx = 0;
    std::erase_if(passives, [&](const PassiveEntry &) {
      const bool dead = (idx < alive_flags.size()) && !alive_flags[idx];
      ++idx;
      return dead;
    });
  }
  return final;
}

} // namespace moba