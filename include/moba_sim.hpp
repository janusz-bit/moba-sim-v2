#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace moba {
using Type = double;

// Post-mitigation physical damage after armor reduction.
// See: https://wiki.leagueoflegends.com/en-us/Armor
Type post_mitigation_damage(const Type &raw_damage,
                            const Type &resistanse) noexcept;

enum class Stat : std::uint8_t { HP, AP, AD, MS, Armor, MR };
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
  Stat stat;
  ModType type;
  Type value;
  Source source;
};

class ModDB {
  std::vector<Modifier> mods_;

public:
  void add(const Stat &stat, const ModType &type, const Type &value,
           const Source &source) {
    mods_.push_back(
        {.stat = stat, .type = type, .value = value, .source = source});
  }

  [[nodiscard]] Type getSumStat(
      const Stat &stat, const std::function<bool(const Modifier &)> &predicate =
                            [](const auto &) { return true; }) const;

  [[nodiscard]] Type getIncStat(
      const Stat &stat, const std::function<bool(const Modifier &)> &predicate =
                            [](const auto &) { return true; }) const;

  [[nodiscard]] Type getMoreStat(
      const Stat &stat, const std::function<bool(const Modifier &)> &predicate =
                            [](const auto &) { return true; }) const;

  // Insert or update a modifier matching (stat, type, source).
  void replace(const Stat &stat, const ModType &type, const Type &value,
               const Source &source);

  // Remove all modifiers from a given source (e.g. when unequipping an item).
  [[nodiscard]] bool removeBySource(const Source &source) {
    auto [first, last] = std::ranges::remove_if(mods_, [&](const Modifier &m) {
      return m.source == source;
    });
    if (first == last) {
      return false;
    }

    mods_.erase(first, last);
    return true;
  }

  // Get the BASE modifier value for stat that comes specifically from source
  // "Base". Used to separate natural base stats from bonus stats (e.g. bonus HP
  // for conversions).
  [[nodiscard]] double getBaseFromSource(Stat stat) const {
    for (const auto &m : mods_) {
      if (m.stat == stat && m.type == ModType::Base &&
          m.source.name == "Base") {
        return m.value;
      }
    }
    return 0.0;
  }

  [[nodiscard]] const std::vector<Modifier> &mods() const { return mods_; }
};

} // namespace moba
