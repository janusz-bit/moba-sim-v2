#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace moba {
using Type = double;

// Post-mitigation damage after resistance reduction.
// See: https://wiki.leagueoflegends.com/en-us/Armor
Type post_mitigation_damage(const Type &raw_damage,
                            const Type &resistance) noexcept;

enum class Stat : std::uint8_t {
  MaxHP,
  CurrentHP,
  Mana,
  CurrentMana,
  AP,
  AD,
  MS,
  AR,
  MR,
  CDR,
  ArmorPenFlat,
  ArmorPenPct,
  MagicPenFlat,
  MagicPenPct,
  // --- Offensive stats (LoL Wiki: Champion statistic) ---
  AttackSpeed, // attacks per second (e.g. 0.651)
  CritChance,  // 0.0–1.0 (e.g. 0.25 = 25%)
  CritDamage,  // multiplier (e.g. 1.75 = 175%)
  LifeSteal,   // 0.0–1.0, heals % of post-mitigation basic damage dealt
  Omnivamp,    // 0.0–1.0, heals % of all damage dealt (physical/magic/true)
  // --- Defensive stats ---
  Tenacity,   // 0.0–1.0, reduces incoming CC duration (capped at 1.0)
  SlowResist, // 0.0–1.0, reduces incoming slow duration
  // --- Utility stats ---
  HealShieldPower, // 0.0–1.0, amplifies heals and shields
  HPRegen,         // HP per 5 seconds
  MPRegen,         // Mana per 5 seconds
  ShieldHP,        // active shield amount; absorbs damage before CurrentHP
  Count
};
enum class ModType : std::uint8_t {
  Base, // 10 + 20 + 30
  Inc,  // 1.1 + 1.2 + 1.3
  More  // 1.1 * 1.2 * 1.3
};

enum class TypeDamage : std::uint8_t { Physical, Magic, True };

struct Source;
using SourcePtr = std::shared_ptr<Source>;

struct Source {
  std::string name;
  std::string description;
  SourcePtr parent; // who/what added this (chainable provenance).
                    // nullptr = root source.

  Source(std::string n = {}, std::string d = {}, SourcePtr p = {})
      : name(std::move(n)), description(std::move(d)), parent(std::move(p)) {}

  // Convenience: 3rd arg as string → creates a root parent with that name.
  // Enables backward-compatible Source{"Item", "Bloodthirster", "attacker"}.
  Source(std::string n, std::string d, std::string origin_name)
      : name(std::move(n)), description(std::move(d)),
        parent(origin_name.empty()
                   ? SourcePtr{}
                   : std::make_shared<Source>(std::move(origin_name))) {}

  // Backward-compatible initializer_list constructor.
  // {"name", "desc", "origin"} → parent = make_shared<Source>("origin")
  Source(std::initializer_list<std::string> list) {
    const auto *it = list.begin();
    name = (it != list.end()) ? *it++ : std::string{};
    description = (it != list.end()) ? *it++ : std::string{};
    if (it != list.end()) {
      std::string origin_name = *it++;
      if (!origin_name.empty()) {
        parent = std::make_shared<Source>(std::move(origin_name));
      }
    }
  }

  // Walk the chain: returns the origin (root parent's name), or "" if none.
  [[nodiscard]] std::string origin() const {
    return parent ? parent->name : std::string{};
  }

  bool operator==(const Source &o) const {
    if (name != o.name || description != o.description) {
      return false;
    }
    if (!parent && !o.parent) {
      return true;
    }
    if (!parent || !o.parent) {
      return false;
    }
    return *parent == *o.parent;
  }
};

struct Modifier {
  Stat stat{};
  ModType type{};
  Type value{};
  Source source;
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

// --- Event system ---

enum class EventKind : std::uint8_t {
  AttackHit,
  DamageDealt,
  DamageReceived,
  HealApplied,
  ShieldBroken,
  CCApplied,
  Kill,
  Death,
};

struct GameEvent {
  EventKind kind{};
  std::size_t actor_id = 0;  // index in Simulation::champions
  std::size_t target_id = 0; // index (may equal actor_id for self-heal)
  Type amount = 0.0;         // raw damage / heal / etc.
  TypeDamage damage_type = TypeDamage::True;
  Source source;   // provenance chain
  Type time = 0.0; // when it happened

  bool operator==(const GameEvent &) const = default;
};

struct Champion {
  using Stats = std::array<Type, std::to_underlying(Stat::Count)>;
  // Construct a champion with base stats from an initializer list of
  // (Stat, value) pairs. Example: Champion c{{Stat::MaxHP, 1000}, {Stat::AD,
  // 50}};
  Champion(std::initializer_list<std::pair<Stat, Type>> stats);
  Champion() = default;
  // A Passive receives (base, final, time) and returns a list of typed
  // modifiers (Base/Inc/More) plus an `alive` flag. `time` is the absolute
  // simulation time (starts at 0, only increases). The passive is the sole
  // authority on its lifetime:
  //   - permanent: always returns `alive=true`
  //   - one-shot: returns `alive=false` after its single application
  //   - temp: returns `alive=false` once it decides to expire (e.g. by
  //     capturing a start time and checking `time - start < duration`)
  // Passives returning `alive=false` are removed after their mods are applied.
  // Mods are folded into a copy of mod_db, so a passive can express additive
  // (Base), percent-increase (Inc), or multiplicative (More) effects via the
  // standard pipeline.
  struct PassiveResult {
    std::vector<Modifier> mods;
    std::vector<GameEvent> emitted_events;
    bool alive = true;

    // Backward-compatible: PassiveResult{mods, alive}
    PassiveResult(std::vector<Modifier> m = {}, bool a = true,
                  std::vector<GameEvent> ev = {})
        : mods(std::move(m)), emitted_events(std::move(ev)), alive(a) {}
  };
  using Passive = std::function<PassiveResult(
      const Stats &base, const Stats &final, const Type &time)>;
  // An EventPassive reacts to a GameEvent and can emit new events + mods.
  using EventPassive =
      std::function<PassiveResult(const Stats &base, const Stats &final,
                                  const Type &time, const GameEvent &event)>;
  // A PassiveEntry pairs a passive with a numeric id, a source, an optional
  // event handler, and the passive itself. The id is used by
  // Champion::addPassive to deduplicate: inserting an entry whose id already
  // exists replaces the existing passive (refresh), otherwise a new entry is
  // appended.
  struct PassiveEntry {
    std::size_t id = 0;
    Source source;
    Passive passive;
    std::optional<EventPassive> on_event;

    PassiveEntry(std::size_t id_, Passive p, Source src = {},
                 std::optional<EventPassive> ev = {})
        : id(id_), source(std::move(src)), passive(std::move(p)),
          on_event(std::move(ev)) {}
  };
  using Passives = std::vector<PassiveEntry>;

  // PassiveFactory creates PassiveEntry instances with auto-incremented ids.
  class PassiveFactory {
    std::size_t next_id_ = 0;

  public:
    [[nodiscard]] PassiveEntry make(Passive p, Source src = {}) {
      return PassiveEntry{next_id_++, std::move(p), std::move(src)};
    }
    [[nodiscard]] PassiveEntry make(Passive p, Source src, EventPassive ev) {
      return PassiveEntry{next_id_++,
                          std::move(p),
                          std::move(src),
                          std::move(ev)};
    }
  };

  ModDB mod_db;
  Passives passives;

  [[nodiscard]] Stats getBaseStats() const;

  // Insert a passive entry, deduplicating by id: if an entry with the same id
  // already exists, its passive is replaced (refresh); otherwise the entry is
  // appended.
  void addPassive(PassiveEntry entry);

  // Applies all passives in a single step. Passives returning alive=false are
  // removed after their bonus is applied. Not const: mutates passives.
  Stats applyPassives(const Stats &base, const Stats &final,
                      const Type &time = 0.0);

  [[nodiscard]] static Type getDeltaStats(const Stats &stats1,
                                          const Stats &stats2);

  [[nodiscard]] Stats evaluateChampion(Type eps = 0.01,
                                       std::size_t max_iter = 1000,
                                       Type time = 0.0);
};

// Damage after applying flat and percentage penetration, then resistance
// mitigation. True damage bypasses mitigation. `target` is the target's
// final stats (resistance read from AR/MR depending on `type`).
[[nodiscard]] Type mitigated_damage(Type raw_damage, TypeDamage type,
                                    const Champion::Stats &target,
                                    Type flat_pen = 0.0,
                                    Type pct_pen = 0.0) noexcept;

// Lifesteal healing from post-mitigation damage. Life steal applies to basic
// damage; omnivamp applies to all damage types. Returns the heal amount.
// See: https://wiki.leagueoflegends.com/en-us/Life_steal
[[nodiscard]] Type lifesteal_heal(Type post_mitigated,
                                  Type lifesteal_pct) noexcept;
[[nodiscard]] Type omnivamp_heal(Type post_mitigated,
                                 Type omnivamp_pct) noexcept;

// Effective crowd control duration after tenacity reduction. Capped at 0.3s
// minimum (per LoL Wiki). Tenacity is 0.0–1.0.
// See: https://wiki.leagueoflegends.com/en-us/Tenacity
[[nodiscard]] Type effective_cc_duration(Type raw_duration,
                                         Type tenacity) noexcept;

// Apply damage to shield first, then CurrentHP. Returns remaining shield and
// HP after absorption. Shield absorbs all damage types (normal shield per LoL
// Wiki). Resistance mitigation is applied BEFORE the shield absorbs.
// See: https://wiki.leagueoflegends.com/en-us/Shield
struct DamageAfterShield {
  Type shield_remaining;
  Type hp_remaining;
};
[[nodiscard]] DamageAfterShield
apply_damage_to_shield(Type shield, Type current_hp, Type mitigated) noexcept;

// Amplify a heal or shield amount by HealShieldPower. Returns the amplified
// value. HealShieldPower is 0.0–1.0 (e.g. 0.15 = +15%).
[[nodiscard]] Type amplified_heal(Type base_heal,
                                  Type heal_shield_power) noexcept;

// Convenience accessors for Stats indexed by Stat enum.
[[nodiscard]] inline Type getStat(const Champion::Stats &stats, Stat stat) {
  return stats[std::to_underlying(stat)];
}
inline void setStat(Champion::Stats &stats, Stat stat, Type value) {
  stats[std::to_underlying(stat)] = value;
}

// --- Simulation: event queue + cross-champion dispatch ---

struct Simulation {
  std::vector<Champion> champions;
  std::deque<GameEvent> event_queue;

  void enqueue(GameEvent ev) { event_queue.push_back(std::move(ev)); }

  // Process the event queue FIFO. For each event:
  //   1. AttackHit → compute mitigated damage → enqueue DamageReceived
  //   2. Broadcast event to all passives of all champions with on_event
  //   3. Collect emitted events → append to queue
  //   4. Re-evaluate affected champions (fixed-point)
  // Repeats until queue empty or max_iter exceeded.
  void processEvents(Type eps = 0.01, std::size_t max_iter = 10000);
};

} // namespace moba
