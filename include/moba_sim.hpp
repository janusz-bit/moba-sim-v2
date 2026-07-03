#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace moba {
using Type = double;

// Post-mitigation physical damage after armor reduction.
// See: https://wiki.leagueoflegends.com/en-us/Armor
Type post_mitigation_damage(const Type &raw_damage,
                            const Type &resistanse) noexcept;

enum class Stat : std::uint8_t { HP, AP, AD, MS, AR, MR, CDR, Count };
enum class ModType : std::uint8_t {
  Base, // 10 + 20 + 30
  Inc,  // 1.1 + 1.2 + 1.3
  More  // 1.1 * 1.2 * 1.3
};

enum class TypeDamage : std::uint8_t { Physical, Magic, True };
enum class KindDamage : std::uint8_t { AutoAttack, OnHit, Spell };

inline std::string statToString(Stat stat);

struct Source {
  std::string name;
  std::string description;

  bool operator==(const Source &) const = default;
};

struct Modifier {
  Stat stat{};
  ModType type{};
  Type value{};
  Source source{};
};

class ModDB {
  std::vector<Modifier> mods_;

public:
  [[nodiscard]] const std::vector<Modifier> &get_mods() const { return mods_; }

  void add(const Stat &stat, const ModType &type, const Type &value,
           const Source &source);

  void remove(const Stat &stat, const ModType &type, const Source &source);

  void remove(const std::function<bool(const Modifier &)> &predicate);

  // Insert or update a modifier matching (stat, type, source).
  void replace(const Stat &stat, const ModType &type, const Type &value,
               const Source &source);

  [[nodiscard]] Type getSumStat(
      const Stat &stat, const std::function<bool(const Modifier &)> &predicate =
                            [](const auto &) { return true; }) const;

  [[nodiscard]] Type getIncStat(
      const Stat &stat, const std::function<bool(const Modifier &)> &predicate =
                            [](const auto &) { return true; }) const;

  [[nodiscard]] Type getMoreStat(
      const Stat &stat, const std::function<bool(const Modifier &)> &predicate =
                            [](const auto &) { return true; }) const;

  [[nodiscard]] Type getStat(
      const Stat &stat, const std::function<bool(const Modifier &)> &predicate =
                            [](const auto &) { return true; }) const;
};

struct Champion {
  using Stats = std::array<Type, std::to_underlying(Stat::Count)>;
  using Passive = std::function<Stats(const Stats &base, const Stats &final)>;
  using Passives = std::vector<Passive>;
  ModDB mod_db;
  Stats stats{};
  Passives passives{};


  void getBaseStats();

  Stats applyPassives(const Stats &base, const Stats &final);

  [[nodiscard]] static Type getDeltaStats(const Stats &stats1,
                                          const Stats &stats2);

  void evaluateChampion() {
    getBaseStats();
    const Stats base = stats;
    Stats final = stats;
    final = applyPassives(base, final);

    Type delta = getDeltaStats(base, final);
    Stats final_now = final;
    while (delta > 0.01) {
        final_now = applyPassives(base, final_now);
        delta = getDeltaStats(final_now, final);
        final = final_now;
    }
    stats = final;
  }
};

} // namespace moba
