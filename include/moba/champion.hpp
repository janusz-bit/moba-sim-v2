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

// Champion: mod_db (statystyki bazowe + przedmioty) + passives (efekty).
// Potok: mod_db → getBaseStats() → passives → final stats.
struct Champion {
  using Stats = std::array<Type, std::to_underlying(Stat::Count)>;

  // Konstruktor z listą (Stat, wartość) par → dodaje jako Base do mod_db.
  Champion(std::initializer_list<std::pair<Stat, Type>> stats);
  Champion() = default;

  // Wynik pasywki: modyfikatory do potoku + opcjonalne nowe eventy + alive
  // flag.
  struct PassiveResult {
    std::vector<Modifier> mods;
    std::vector<PassiveEvent>
        emitted_events; // eventy do dispatchu po tym kroku
    bool alive = true;  // false = usuń pasywkę po aplikacji

    PassiveResult(std::vector<Modifier> m = {}, bool a = true,
                  std::vector<PassiveEvent> ev = {})
        : mods(std::move(m)), emitted_events(std::move(ev)), alive(a) {}
  };

  // Sygnatura pasywki. event = monostate (normalna ewaluacja) lub konkretny
  // event.
  using Passive =
      std::function<PassiveResult(const Stats &base, const Stats &final,
                                  const Type &time, const PassiveEvent &event)>;

  // Pasywka + ID (do deduplikacji) + source (provenance).
  struct PassiveEntry {
    std::size_t id = 0;
    Source source;
    Passive passive;

    PassiveEntry(std::size_t id_, Passive p, Source src = {})
        : id(id_), source(std::move(src)), passive(std::move(p)) {}
  };

  using Passives = std::vector<PassiveEntry>;

  // Fabryka z auto-increment ID. make() → PassiveEntry z unikalnym ID.
  class PassiveFactory {
    std::size_t next_id_ = 0;

  public:
    [[nodiscard]] PassiveEntry make(Passive p, Source src = {}) {
      return PassiveEntry{next_id_++, std::move(p), std::move(src)};
    }
  };

  ModDB mod_db;
  Passives passives;

  // Statystyki z mod_db (bez passyw), przez pełny potok.
  [[nodiscard]] Stats getBaseStats() const;

  // Dodaj pasywkę. Ten sam ID = refresh (zastąp), nowy = dodaj.
  void addPassive(PassiveEntry entry);

  // Jeden krok symulacji: fold mods od wszystkich pasyw, usuń alive=false.
  Stats applyPassives(const Stats &base, const Stats &final,
                      const Type &time = 0.0);

  // Max |różnica| per-element między dwoma Stats. Do convergence check.
  [[nodiscard]] static Type getDeltaStats(const Stats &stats1,
                                          const Stats &stats2);

  // Fixed-point: iteruj applyPassives do zbieżności (delta ≤ eps).
  // Passywki usuwane dopiero po zbieżności. Rzuca ConvergenceError.
  [[nodiscard]] Stats evaluateChampion(Type eps = 0.01,
                                       std::size_t max_iter = 1000,
                                       Type time = 0.0);
};

} // namespace moba