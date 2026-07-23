#pragma once

#include "moba/source.hpp"
#include "moba/types.hpp"

#include <functional>
#include <vector>

namespace moba {

// Pojedynczy modyfikator: cel (stat), typ (Base/Inc/More), wartość, źródło.
struct Modifier {
  Stat stat{};
  ModType type{};
  Type value{};
  Source source;
};

// Baza modyfikatorów championa. Potok: getStat = sum(Base) * (1+sum(Inc)) *
// prod(More) Wszystkie gettery przyjmują opcjonalny predykat do filtrowania po
// source.
class ModDB {
  std::vector<Modifier> mods_;

public:
  [[nodiscard]] const std::vector<Modifier> &get_mods() const { return mods_; }

  void add(const Stat &stat, const ModType &type, const Type &value,
           const Source &source);
  void remove(const Stat &stat, const ModType &type, const Source &source);
  void remove(const std::function<bool(const Modifier &)> &predicate);
  // Insert-or-update: jeśli (stat,type,source) istnieje → zastąp wartość.
  void replace(const Stat &stat, const ModType &type, const Type &value,
               const Source &source);

  // Suma Base modyfikatorów dla statu.
  [[nodiscard]] Type getSumStat(
      const Stat &stat, const std::function<bool(const Modifier &)> &predicate =
                            [](const auto &) { return true; }) const;
  // Mnożnik Inc: 1.0 + sum(Inc).
  [[nodiscard]] Type getIncStat(
      const Stat &stat, const std::function<bool(const Modifier &)> &predicate =
                            [](const auto &) { return true; }) const;
  // Mnożnik More: product(More).
  [[nodiscard]] Type getMoreStat(
      const Stat &stat, const std::function<bool(const Modifier &)> &predicate =
                            [](const auto &) { return true; }) const;
  // Pełny potok: sum * inc * more.
  [[nodiscard]] Type getStat(
      const Stat &stat, const std::function<bool(const Modifier &)> &predicate =
                            [](const auto &) { return true; }) const;
};

} // namespace moba