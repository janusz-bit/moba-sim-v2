#pragma once

#include <cstdint>
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
  // Passive returns a *bonus* (delta): only stats it adds, others = 0.
  // Computed from (base, final); passives are independent within one
  // applyPassives call (order does not matter).
  using Passive = std::function<Stats(const Stats &base, const Stats &final)>;
  using Passives = std::vector<Passive>;
  ModDB mod_db;
  Passives passives;

  [[nodiscard]] Stats getBaseStats() const;

  [[nodiscard]] Stats applyPassives(const Stats &base,
                                    const Stats &final) const;

  [[nodiscard]] static Type getDeltaStats(const Stats &stats1,
                                          const Stats &stats2);
};

[[nodiscard]] Champion::Stats evaluateChampion(const Champion &champion,
                                               Type eps = 0.01);

} // namespace moba
