#pragma once

#include "moba/source.hpp"
#include "moba/types.hpp"

#include <functional>
#include <vector>

namespace moba {

/// A single modifier entry in the Base/Inc/More pipeline.
///
/// Each modifier targets one `Stat`, has a `ModType` (Base/Inc/More),
/// a numeric `value`, and a `Source` for provenance tracking.
struct Modifier {
  Stat stat{};    ///< Which stat this modifier affects
  ModType type{}; ///< Pipeline stage: Base, Inc, or More
  Type value{};   ///< The modifier's numeric value
  Source source;  ///< Provenance — who/what added this modifier
};

/// Database of modifiers. Stores all Base/Inc/More modifiers for a champion
/// and computes final stat values via the pipeline:
///
/// `getStat(stat) = getSumStat(stat) * getIncStat(stat) * getMoreStat(stat)`
///
/// - `getSumStat` — sum of all `Base` modifiers for the stat
/// - `getIncStat` — `1.0 + sum(Inc)` (percent increases)
/// - `getMoreStat` — `product(More)` (multiplicative)
///
/// All getters accept an optional predicate to filter modifiers by source.
class ModDB {
  std::vector<Modifier> mods_;

public:
  /// Read-only access to the raw modifier vector.
  [[nodiscard]] const std::vector<Modifier> &get_mods() const { return mods_; }

  /// Add a modifier to the database.
  /// @param stat Target stat.
  /// @param type Modifier type (Base/Inc/More).
  /// @param value Numeric value.
  /// @param source Provenance of this modifier.
  void add(const Stat &stat, const ModType &type, const Type &value,
           const Source &source);

  /// Remove the first modifier matching (stat, type, source).
  void remove(const Stat &stat, const ModType &type, const Source &source);

  /// Remove all modifiers matching the given predicate.
  void remove(const std::function<bool(const Modifier &)> &predicate);

  /// Insert or update a modifier matching (stat, type, source).
  /// If a matching modifier exists, its value is replaced; otherwise a new
  /// modifier is appended.
  void replace(const Stat &stat, const ModType &type, const Type &value,
               const Source &source);

  /// Sum of all `Base` modifiers for the given stat (filtered by predicate).
  /// @param stat Target stat.
  /// @param predicate Filter function; defaults to "accept all".
  /// @return Sum of Base values.
  [[nodiscard]] Type getSumStat(
      const Stat &stat, const std::function<bool(const Modifier &)> &predicate =
                            [](const auto &) { return true; }) const;

  /// Percent increase factor: `1.0 + sum(Inc)` for the given stat.
  /// @param stat Target stat.
  /// @param predicate Filter function; defaults to "accept all".
  /// @return Multiplier starting from 1.0.
  [[nodiscard]] Type getIncStat(
      const Stat &stat, const std::function<bool(const Modifier &)> &predicate =
                            [](const auto &) { return true; }) const;

  /// Multiplicative factor: `product(More)` for the given stat.
  /// @param stat Target stat.
  /// @param predicate Filter function; defaults to "accept all".
  /// @return Multiplier starting from 1.0.
  [[nodiscard]] Type getMoreStat(
      const Stat &stat, const std::function<bool(const Modifier &)> &predicate =
                            [](const auto &) { return true; }) const;

  /// Full pipeline result: `getSumStat * getIncStat * getMoreStat`.
  /// @param stat Target stat.
  /// @param predicate Filter function; defaults to "accept all".
  /// @return Final computed stat value.
  [[nodiscard]] Type getStat(
      const Stat &stat, const std::function<bool(const Modifier &)> &predicate =
                            [](const auto &) { return true; }) const;
};

} // namespace moba