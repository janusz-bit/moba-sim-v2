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
  // A Passive receives (base, final, time) and returns a bonus (delta) plus an
  // `alive` flag. `time` is the absolute simulation time (starts at 0, only
  // increases). The passive is the sole authority on its lifetime:
  //   - permanent: always returns `alive=true`
  //   - one-shot: returns `alive=false` after its single application
  //   - temp: returns `alive=false` once it decides to expire (e.g. by
  //     capturing a start time and checking `time - start < duration`)
  // Passives returning `alive=false` are removed after their bonus is applied.
  struct PassiveResult {
    Stats bonus{};
    bool alive = true;
  };
  using Passive = std::function<PassiveResult(const Stats &base,
                                              const Stats &final, const Type &time)>;
  using Passives = std::vector<Passive>;
  ModDB mod_db;
  Passives passives;

  [[nodiscard]] Stats getBaseStats() const;

  // Applies all passives in a single step. Passives returning alive=false are
  // removed after their bonus is applied. Not const: mutates passives.
  Stats applyPassives(const Stats &base, const Stats &final, const Type &time = 0.0);

  [[nodiscard]] static Type getDeltaStats(const Stats &stats1,
                                           const Stats &stats2);

  [[nodiscard]] Stats evaluateChampion(Type eps = 0.01,
                                        std::size_t max_iter = 1000);
};

} // namespace moba
