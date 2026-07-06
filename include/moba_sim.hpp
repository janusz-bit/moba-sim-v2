#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <stdexcept>
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

  Source(std::initializer_list<std::string> list)
      : name(list.begin()[0]), description(list.begin()[1]) {}

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

class ConvergenceError : public std::runtime_error {
public:
  explicit ConvergenceError(const std::string &msg) : std::runtime_error(msg) {}
};

struct Champion {
  using Stats = std::array<Type, std::to_underlying(Stat::Count)>;
  struct PassiveResult {
    Stats bonus{};
    bool alive = true;
  };
  using Passive = std::function<PassiveResult(const Stats &base,
                                              const Stats &final, Type time)>;
  using Passives = std::vector<Passive>;
  // One-shot: removed after a single application.
  using OneShotPassives = std::vector<Passive>;
  // Temp: self-managed lifetime via time; removed when returning alive=false.
  using TempPassives = std::vector<Passive>;
  ModDB mod_db;
  Passives passives;
  OneShotPassives one_shot_passives;
  TempPassives temp_passives;

  [[nodiscard]] Stats getBaseStats() const;

  // Applies permanent, one-shot (then consumed), and temp (self-managed via
  // time) passives in a single step. Passives returning alive=false are
  // removed. Not const: mutates one_shot_/temp_ passives.
  Stats applyPassives(const Stats &base, const Stats &final, Type time = 0.0);

  [[nodiscard]] static Type getDeltaStats(const Stats &stats1,
                                          const Stats &stats2);

  [[nodiscard]] Stats evaluateChampion(Type eps = 0.01,
                                       std::size_t max_iter = 1000);
};

} // namespace moba
