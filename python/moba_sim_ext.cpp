// High-level Python bindings for moba-sim (nanobind).
//
// Design: expose only Champion, Simulation, event structs, and stat/damage
// helpers.  Passives and signals accept Python callables.  ModDB, PassiveEntry,
// and PassiveFactory are internal — Python users add passives via
// `champion.add_passive(callback)`.
//
// "GIL": nanobind holds the GIL by default; Python callbacks invoked from C++
// (passive evaluation, signal handlers) also need the GIL.  Since all paths
// here are invoked while the GIL is held by the calling Python frame, no
// explicit acquire is needed.  (A future C++ thread that calls emit() will need
// to nb::gil_scoped_acquire.)

#include "moba/champion.hpp"
#include "moba/combat.hpp"
#include "moba/event.hpp"
#include "moba/mod_db.hpp"
#include "moba/signal.hpp"
#include "moba/simulation.hpp"
#include "moba/source.hpp"
#include "moba/types.hpp"

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/function.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>
#include <variant>

namespace nb = nanobind;
using namespace moba;

namespace {
// Helper for std::visit with overloaded lambdas
template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

// Construct a Champion from a Python dict {Stat: value} (or list of pairs).
// We build a std::initializer_list-like sequence by constructing the Champion
// and adding base modifiers directly via mod_db.
auto make_champion(nb::object stats_obj) {
  Champion c;
  Source src{"Base", ""};
  if (stats_obj.is_none()) {
    return c;
  }
  if (nb::isinstance<nb::dict>(stats_obj)) {
    for (auto [k, v] : nb::cast<nb::dict>(stats_obj)) {
      c.mod_db.add(nb::cast<Stat>(k), ModType::Base, nb::cast<Type>(v), src);
    }
  } else if (nb::isinstance<nb::list>(stats_obj) ||
             nb::isinstance<nb::tuple>(stats_obj)) {
    for (auto item : nb::cast<nb::sequence>(stats_obj)) {
      auto t = nb::cast<nb::tuple>(item);
      c.mod_db.add(nb::cast<Stat>(t[0]),
                   ModType::Base,
                   nb::cast<Type>(t[1]),
                   src);
    }
  }
  return c;
}

// Convert a Champion::Stats array to a numpy array (dtype=float64, shape=[25]).
// The returned array owns its data (copy), so it stays valid after the C++
// array goes out of scope.
auto stats_to_numpy(const Champion::Stats &s) {
  // Copy into a new numpy array: build from a std::vector, then wrap.
  auto *buf = new double[s.size()];
  std::copy(s.begin(), s.end(), buf);
  nb::capsule owner(buf, [](void *p) noexcept {
    delete[] static_cast<double *>(p);
  });
  return nb::ndarray<nb::numpy, double>(buf, {s.size()}, owner);
}

// Wrap a Python callable as a Champion::Passive (std::function).  The Python
// callable receives (base ndarray, final ndarray, time, event) and returns
// either:
//   - a list of (Stat, ModType, value[, source]) tuples, or
//   - a dict {"mods": [...], "alive": bool, "events": [...]} matching
//   PassiveResult.
// Returns a Champion::PassiveResult.
auto make_passive(nb::callable py_fn) {
  return [fn = nb::cast<std::function<nb::object(nb::ndarray<nb::numpy, double>,
                                                 nb::ndarray<nb::numpy, double>,
                                                 Type,
                                                 nb::object)>>(py_fn)](
             const Champion::Stats &base,
             const Champion::Stats &final,
             const Type &time,
             const PassiveEvent &event) -> Champion::PassiveResult {
    // Convert PassiveEvent variant to Python object
    nb::object py_event =
        std::visit(overloaded{
                       [](std::monostate) { return nb::none(); },
                       [](const auto &e) { return nb::cast(e); },
                   },
                   event);

    nb::object result =
        fn(stats_to_numpy(base), stats_to_numpy(final), time, py_event);
    if (result.is_none()) {
      return {};
    }
    // Accept dict {"mods": [...], "alive": bool, "events": [...]}
    if (nb::isinstance<nb::dict>(result)) {
      nb::dict d = nb::cast<nb::dict>(result);
      Champion::PassiveResult pr;
      if (d.contains("mods")) {
        for (auto item : nb::cast<nb::list>(d["mods"])) {
          auto t = nb::cast<nb::tuple>(item);
          auto stat = nb::cast<Stat>(t[0]);
          auto type = nb::cast<ModType>(t[1]);
          auto val = nb::cast<Type>(t[2]);
          Source src = t.size() > 3 ? nb::cast<Source>(t[3]) : Source{};
          pr.mods.push_back({stat, type, val, src});
        }
      }
      pr.alive = d.contains("alive") ? nb::cast<bool>(d["alive"]) : true;
      // TODO: parse "events" key for emitted_events
      return pr;
    }
    // Accept a bare list of tuples (alive defaults to true)
    if (nb::isinstance<nb::list>(result)) {
      Champion::PassiveResult pr;
      for (auto item : nb::cast<nb::list>(result)) {
        auto t = nb::cast<nb::tuple>(item);
        auto stat = nb::cast<Stat>(t[0]);
        auto type = nb::cast<ModType>(t[1]);
        auto val = nb::cast<Type>(t[2]);
        Source src = t.size() > 3 ? nb::cast<Source>(t[3]) : Source{};
        pr.mods.push_back({stat, type, val, src});
      }
      pr.alive = true;
      return pr;
    }
    throw std::runtime_error(
        "Passive callback must return a list of tuples or a dict "
        "{'mods': [...], 'alive': bool}");
  };
}

// Parse a keyword-args dict for an event into a Python dict for the signal
// callback.  Each event struct is exposed as an nb::class_ with read-only
// fields, so we just pass the struct through.
template <typename Ev> auto wrap_signal_emit(Signal<Ev> &sig) {
  return [&sig](const Ev &ev) { sig.emit(ev); };
}

} // namespace

NB_MODULE(moba_ext, m) {
  m.doc() = "MOBA-style champion stat aggregation and combat simulation";

  // --- Stat enum ---
  nb::enum_<Stat>(m, "Stat")
      .value("MaxHP", Stat::MaxHP)
      .value("CurrentHP", Stat::CurrentHP)
      .value("Mana", Stat::Mana)
      .value("CurrentMana", Stat::CurrentMana)
      .value("AP", Stat::AP)
      .value("AD", Stat::AD)
      .value("MS", Stat::MS)
      .value("AR", Stat::AR)
      .value("MR", Stat::MR)
      .value("CDR", Stat::CDR)
      .value("ArmorPenFlat", Stat::ArmorPenFlat)
      .value("ArmorPenPct", Stat::ArmorPenPct)
      .value("MagicPenFlat", Stat::MagicPenFlat)
      .value("MagicPenPct", Stat::MagicPenPct)
      .value("AttackSpeed", Stat::AttackSpeed)
      .value("CritChance", Stat::CritChance)
      .value("CritDamage", Stat::CritDamage)
      .value("LifeSteal", Stat::LifeSteal)
      .value("Omnivamp", Stat::Omnivamp)
      .value("Tenacity", Stat::Tenacity)
      .value("SlowResist", Stat::SlowResist)
      .value("HealShieldPower", Stat::HealShieldPower)
      .value("HPRegen", Stat::HPRegen)
      .value("MPRegen", Stat::MPRegen)
      .value("ShieldHP", Stat::ShieldHP)
      .def("__index__", [](Stat s) { return std::to_underlying(s); })
      .def("__int__", [](Stat s) { return std::to_underlying(s); })
      .export_values();

  // --- ModType enum ---
  nb::enum_<ModType>(m, "ModType")
      .value("Base", ModType::Base)
      .value("Inc", ModType::Inc)
      .value("More", ModType::More);

  // --- TypeDamage enum ---
  nb::enum_<TypeDamage>(m, "TypeDamage")
      .value("Physical", TypeDamage::Physical)
      .value("Magic", TypeDamage::Magic)
      .value("True", TypeDamage::True)
      // "True" is a Python keyword, so add a "True_" alias for ergonomic use
      .value("True_", TypeDamage::True)
      .def("__index__", [](TypeDamage t) { return std::to_underlying(t); })
      .def("__int__", [](TypeDamage t) { return std::to_underlying(t); })
      .export_values();

  // --- Source ---
  nb::class_<Source>(m, "Source")
      .def(nb::init<std::string, std::string, SourcePtr>(),
           nb::arg("name") = "",
           nb::arg("description") = "",
           nb::arg("parent") = SourcePtr{})
      .def(nb::init<std::string, std::string, std::string>(),
           nb::arg("name"),
           nb::arg("description"),
           nb::arg("origin_name"))
      .def_rw("name", &Source::name)
      .def_rw("description", &Source::description)
      .def_rw("parent", &Source::parent)
      .def("origin", &Source::origin)
      .def("__eq__", [](const Source &a, const Source &b) { return a == b; })
      .def("__repr__",
           [](const Source &s) { return "<Source '" + s.name + "'>"; });

  // --- Event structs ---
  nb::class_<AttackHit>(m, "AttackHit")
      .def_ro("actor_id", &AttackHit::actor_id)
      .def_ro("target_id", &AttackHit::target_id)
      .def_ro("amount", &AttackHit::amount)
      .def_ro("damage_type", &AttackHit::damage_type)
      .def_ro("source", &AttackHit::source)
      .def_ro("time", &AttackHit::time);

  nb::class_<DamageDealt>(m, "DamageDealt")
      .def_ro("actor_id", &DamageDealt::actor_id)
      .def_ro("target_id", &DamageDealt::target_id)
      .def_ro("amount", &DamageDealt::amount)
      .def_ro("damage_type", &DamageDealt::damage_type)
      .def_ro("source", &DamageDealt::source)
      .def_ro("time", &DamageDealt::time);

  nb::class_<DamageReceived>(m, "DamageReceived")
      .def_ro("actor_id", &DamageReceived::actor_id)
      .def_ro("target_id", &DamageReceived::target_id)
      .def_ro("amount", &DamageReceived::amount)
      .def_ro("damage_type", &DamageReceived::damage_type)
      .def_ro("source", &DamageReceived::source)
      .def_ro("time", &DamageReceived::time);

  nb::class_<HealApplied>(m, "HealApplied")
      .def_ro("target_id", &HealApplied::target_id)
      .def_ro("amount", &HealApplied::amount)
      .def_ro("source", &HealApplied::source)
      .def_ro("time", &HealApplied::time);

  nb::class_<Death>(m, "Death")
      .def_ro("actor_id", &Death::actor_id)
      .def_ro("target_id", &Death::target_id)
      .def_ro("source", &Death::source)
      .def_ro("time", &Death::time);

  // --- Champion ---
  nb::class_<Champion>(m, "Champion")
      .def(
          "__init__",
          [](Champion *c, nb::object stats) {
            new (c) Champion(make_champion(stats));
          },
          nb::arg("stats") = nb::none())
      .def("get_base_stats",
           [](const Champion &c) { return stats_to_numpy(c.getBaseStats()); })
      .def(
          "add_passive",
          [](Champion &c, nb::callable fn) {
            c.addPassive(Champion::PassiveEntry{0, make_passive(fn)});
          },
          nb::arg("callback"),
          "Add a passive.  callback(base, final, time) -> list[(stat, "
          "type, value[, source])] | {'mods': [...], 'alive': bool}")
      .def(
          "apply_passives",
          [](Champion &c,
             const Champion::Stats &base,
             const Champion::Stats &final,
             Type time) {
            return stats_to_numpy(c.applyPassives(base, final, time));
          },
          nb::arg("base"),
          nb::arg("final"),
          nb::arg("time") = 0.0)
      .def(
          "apply_passives",
          [](Champion &c, Type time) {
            auto base = c.getBaseStats();
            auto r = c.applyPassives(base, base, time);
            return stats_to_numpy(r);
          },
          nb::arg("time") = 0.0)
      .def(
          "evaluate_champion",
          [](Champion &c, Type eps, std::size_t max_iter, Type time) {
            return stats_to_numpy(c.evaluateChampion(eps, max_iter, time));
          },
          nb::arg("eps") = 0.01,
          nb::arg("max_iter") = 1000,
          nb::arg("time") = 0.0)
      .def_rw("mod_db", &Champion::mod_db)
      .def_prop_ro("passives_count",
                   [](const Champion &c) { return c.passives.size(); });

  // --- ModDB (low-level access; champion.mod_db) ---
  nb::class_<ModDB>(m, "ModDB")
      .def("add",
           &ModDB::add,
           nb::arg("stat"),
           nb::arg("type"),
           nb::arg("value"),
           nb::arg("source") = Source{})
      .def("replace",
           &ModDB::replace,
           nb::arg("stat"),
           nb::arg("type"),
           nb::arg("value"),
           nb::arg("source") = Source{})
      .def("get_stat",
           &ModDB::getStat,
           nb::arg("stat"),
           nb::arg("predicate") = std::function<bool(const Modifier &)>{
               [](const Modifier &) { return true; }});

  // --- Combat helpers ---
  m.def(
      "mitigated_damage",
      [](Type raw,
         TypeDamage type,
         const Champion::Stats &target,
         Type flat_pen,
         Type pct_pen) {
        return mitigated_damage(raw, type, target, flat_pen, pct_pen);
      },
      nb::arg("raw"),
      nb::arg("type"),
      nb::arg("target"),
      nb::arg("flat_pen") = 0.0,
      nb::arg("pct_pen") = 0.0);

  m.def("post_mitigation_damage",
        &post_mitigation_damage,
        nb::arg("raw"),
        nb::arg("resistance"));

  m.def(
      "apply_damage_to_shield",
      [](Type shield, Type hp, Type mitigated) {
        auto r = apply_damage_to_shield(shield, hp, mitigated);
        return std::make_pair(r.shield_remaining, r.hp_remaining);
      },
      nb::arg("shield"),
      nb::arg("hp"),
      nb::arg("mitigated"));

  m.def(
      "get_stat",
      [](const Champion::Stats &s, Stat stat) { return getStat(s, stat); },
      nb::arg("stats"),
      nb::arg("stat"));
  m.def(
      "set_stat",
      [](Champion::Stats &s, Stat stat, Type v) { setStat(s, stat, v); },
      nb::arg("stats"),
      nb::arg("stat"),
      nb::arg("value"));

  // --- ConvergenceError ---
  // Expose as a Python exception inheriting from RuntimeError.  When C++
  // throws a ConvergenceError, nanobind translates it to this Python type
  // automatically.
  nb::exception<ConvergenceError> conv_err(m,
                                           "ConvergenceError",
                                           PyExc_RuntimeError);

  // --- Simulation ---
  nb::class_<Simulation>(m, "Simulation")
      .def(nb::init<>())
      .def(
          "__del__",
          [](Simulation &sim) { sim.clearSignals(); },
          "Break reference cycles before destruction")
      .def(
          "add_champion",
          [](Simulation &sim, const Champion &c) {
            sim.champions.push_back(c);
          },
          nb::arg("champion"))
      .def("champion_count",
           [](const Simulation &sim) { return sim.champions.size(); })
      .def(
          "get_champion",
          [](Simulation &sim, std::size_t i) -> Champion & {
            if (i >= sim.champions.size()) {
              throw std::out_of_range("champion index out of range");
            }
            return sim.champions[i];
          },
          nb::rv_policy::reference,
          nb::arg("index"))
      .def("on_attack_hit_subscribe",
           [](Simulation &sim, nb::callable fn) {
             sim.onAttackHit.subscribe(
                 [f = nb::cast<std::function<void(AttackHit)>>(fn)](
                     const AttackHit &ev) { f(ev); });
           })
      .def("on_damage_dealt_subscribe",
           [](Simulation &sim, nb::callable fn) {
             sim.onDamageDealt.subscribe(
                 [f = nb::cast<std::function<void(DamageDealt)>>(fn)](
                     const DamageDealt &ev) { f(ev); });
           })
      .def("on_damage_received_subscribe",
           [](Simulation &sim, nb::callable fn) {
             sim.onDamageReceived.subscribe(
                 [f = nb::cast<std::function<void(DamageReceived)>>(fn)](
                     const DamageReceived &ev) { f(ev); });
           })
      .def("on_heal_applied_subscribe",
           [](Simulation &sim, nb::callable fn) {
             sim.onHealApplied.subscribe(
                 [f = nb::cast<std::function<void(HealApplied)>>(fn)](
                     const HealApplied &ev) { f(ev); });
           })
      .def("on_death_subscribe",
           [](Simulation &sim, nb::callable fn) {
             sim.onDeath.subscribe([f = nb::cast<std::function<void(Death)>>(
                                        fn)](const Death &ev) { f(ev); });
           })
      .def(
          "emit_attack_hit",
          [](Simulation &sim,
             std::size_t actor,
             std::size_t target,
             Type amount,
             TypeDamage type,
             const Source &src,
             Type t) {
            sim.dispatchEvent(AttackHit{actor, target, amount, type, src, t});
          },
          nb::arg("actor"),
          nb::arg("target"),
          nb::arg("amount"),
          nb::arg("type"),
          nb::arg("source") = Source{},
          nb::arg("time") = 0.0)
      .def(
          "emit_heal_applied",
          [](Simulation &sim,
             std::size_t target,
             Type amount,
             const Source &src,
             Type t) {
            sim.dispatchEvent(HealApplied{target, amount, src, t});
          },
          nb::arg("target"),
          nb::arg("amount"),
          nb::arg("source") = Source{},
          nb::arg("time") = 0.0)
      .def("clear_signals",
           &Simulation::clearSignals,
           "Drop all signal subscribers to break reference cycles")
      .def("evaluate_all",
           &Simulation::evaluateAll,
           nb::arg("eps") = 0.01,
           nb::arg("max_iter") = 10000);
}