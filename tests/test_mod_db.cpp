#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "moba_sim.hpp"

using moba::ModDB;
using moba::Modifier;
using moba::ModType;
using moba::Source;
using moba::Stat;

TEST_CASE("ModDB add stores modifiers", "[mod_db]") {
  ModDB db;
  Source src{"Item", "desc"};
  db.add(Stat::AD, ModType::Base, 10.0, src);
  REQUIRE(db.get_mods().size() == 1);
  REQUIRE(db.get_mods()[0].stat == Stat::AD);
  REQUIRE(db.get_mods()[0].type == ModType::Base);
  REQUIRE(db.get_mods()[0].value == 10.0);
  REQUIRE(db.get_mods()[0].source == src);
}

TEST_CASE("ModDB remove by stat/type/source erases first match", "[mod_db]") {
  ModDB db;
  Source src{"Item", "desc"};
  db.add(Stat::AD, ModType::Base, 10.0, src);
  db.add(Stat::HP, ModType::Base, 20.0, src);
  db.remove(Stat::AD, ModType::Base, src);
  REQUIRE(db.get_mods().size() == 1);
  REQUIRE(db.get_mods()[0].stat == Stat::HP);
}

TEST_CASE("ModDB remove by stat/type/source is no-op when not found",
          "[mod_db]") {
  ModDB db;
  Source src{"Item", "desc"};
  db.add(Stat::AD, ModType::Base, 10.0, src);
  db.remove(Stat::HP, ModType::Base, src);
  REQUIRE(db.get_mods().size() == 1);
}

TEST_CASE("ModDB remove by predicate erases all matching", "[mod_db]") {
  ModDB db;
  Source src1{"Item", "desc"};
  Source src2{"Buff", "desc"};
  db.add(Stat::AD, ModType::Base, 10.0, src1);
  db.add(Stat::AD, ModType::Base, 20.0, src2);
  db.add(Stat::HP, ModType::Base, 30.0, src1);
  db.remove([](const Modifier &m) { return m.stat == Stat::AD; });
  REQUIRE(db.get_mods().size() == 1);
  REQUIRE(db.get_mods()[0].stat == Stat::HP);
}

TEST_CASE("ModDB replace inserts when not present", "[mod_db]") {
  ModDB db;
  Source src{"Item", "desc"};
  db.replace(Stat::AD, ModType::Base, 10.0, src);
  REQUIRE(db.get_mods().size() == 1);
  REQUIRE(db.get_mods()[0].value == 10.0);
}

TEST_CASE("ModDB replace updates value when present", "[mod_db]") {
  ModDB db;
  Source src{"Item", "desc"};
  db.replace(Stat::AD, ModType::Base, 10.0, src);
  db.replace(Stat::AD, ModType::Base, 25.0, src);
  REQUIRE(db.get_mods().size() == 1);
  REQUIRE(db.get_mods()[0].value == 25.0);
}

TEST_CASE("ModDB getSumStat sums Base modifiers for matching stat",
          "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::Base, 10.0, src);
  db.add(Stat::AD, ModType::Base, 20.0, src);
  db.add(Stat::HP, ModType::Base, 30.0, src);
  db.add(Stat::AD, ModType::Inc, 0.1, src);
  REQUIRE(db.getSumStat(Stat::AD) == Catch::Approx(30.0));
  REQUIRE(db.getSumStat(Stat::HP) == Catch::Approx(30.0));
  REQUIRE(db.getSumStat(Stat::AP) == Catch::Approx(0.0));
}

TEST_CASE("ModDB getIncStat sums Inc modifiers starting from 1.0", "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::Inc, 0.1, src);
  db.add(Stat::AD, ModType::Inc, 0.2, src);
  db.add(Stat::AD, ModType::Inc, 0.3, src);
  REQUIRE(db.getIncStat(Stat::AD) == Catch::Approx(1.6));
}

TEST_CASE("ModDB getMoreStat multiplies More modifiers starting from 1.0",
          "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::More, 1.1, src);
  db.add(Stat::AD, ModType::More, 1.2, src);
  db.add(Stat::AD, ModType::More, 1.3, src);
  REQUIRE(db.getMoreStat(Stat::AD) == Catch::Approx(1.716).epsilon(0.001));
}

TEST_CASE("ModDB getStat combines sum * inc * more", "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::Base, 50.0, src);
  db.add(Stat::AD, ModType::Inc, 0.1, src);
  db.add(Stat::AD, ModType::More, 1.5, src);
  // 50 * 1.1 * 1.5 = 82.5
  REQUIRE(db.getStat(Stat::AD) == Catch::Approx(82.5));
}

TEST_CASE("ModDB empty getStat returns zero", "[mod_db]") {
  ModDB db;
  // getSumStat=0, getIncStat=1, getMoreStat=1 → 0*1*1=0
  REQUIRE(db.getStat(Stat::AD) == Catch::Approx(0.0));
}

TEST_CASE("ModDB getters respect predicate filtering by source", "[mod_db]") {
  ModDB db;
  Source item{"Item", ""};
  Source buff{"Buff", ""};
  db.add(Stat::AD, ModType::Base, 10.0, item);
  db.add(Stat::AD, ModType::Base, 20.0, buff);
  REQUIRE(db.getSumStat(Stat::AD, [](const Modifier &m) {
    return m.source.name == "Item";
  }) == Catch::Approx(10.0));
  REQUIRE(db.getSumStat(Stat::AD, [](const Modifier &m) {
    return m.source.name == "Buff";
  }) == Catch::Approx(20.0));
}

TEST_CASE("ModDB multiple modifiers of same stat and type aggregate",
          "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::HP, ModType::Base, 100.0, src);
  db.add(Stat::HP, ModType::Base, 200.0, src);
  db.add(Stat::HP, ModType::Inc, 0.5, src);
  db.add(Stat::HP, ModType::More, 2.0, src);
  // sum=300, inc=1.5, more=2.0 → 300*1.5*2.0=900
  REQUIRE(db.getStat(Stat::HP) == Catch::Approx(900.0));
}

// --- additional coverage tests ---

TEST_CASE("ModDB getSumStat handles negative Base values", "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::Base, 50.0, src);
  db.add(Stat::AD, ModType::Base, -20.0, src);
  db.add(Stat::AD, ModType::Base, -10.0, src);
  // 50 - 20 - 10 = 20
  REQUIRE(db.getSumStat(Stat::AD) == Catch::Approx(20.0));
}

TEST_CASE("ModDB getIncStat handles negative Inc values (can go below 1.0)",
          "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::Inc, 0.5, src);
  db.add(Stat::AD, ModType::Inc, -0.3, src);
  // 1.0 + 0.5 - 0.3 = 1.2
  REQUIRE(db.getIncStat(Stat::AD) == Catch::Approx(1.2));
}

TEST_CASE("ModDB getIncStat can reduce to zero with negative Inc", "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::Inc, -1.0, src);
  // 1.0 - 1.0 = 0 → getStat becomes 0
  REQUIRE(db.getIncStat(Stat::AD) == Catch::Approx(0.0));
  REQUIRE(db.getStat(Stat::AD) == Catch::Approx(0.0));
}

TEST_CASE("ModDB getMoreStat with value < 1 reduces", "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::More, 0.8, src);
  db.add(Stat::AD, ModType::More, 0.5, src);
  // 0.8 * 0.5 = 0.4
  REQUIRE(db.getMoreStat(Stat::AD) == Catch::Approx(0.4));
}

TEST_CASE("ModDB getMoreStat with zero value zeroes the stat", "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::Base, 100.0, src);
  db.add(Stat::AD, ModType::More, 0.0, src);
  REQUIRE(db.getMoreStat(Stat::AD) == Catch::Approx(0.0));
  REQUIRE(db.getStat(Stat::AD) == Catch::Approx(0.0));
}

TEST_CASE("ModDB getStat for each Stat enum value independently", "[mod_db]") {
  ModDB db;
  Source src{"Base", ""};
  db.add(Stat::HP, ModType::Base, 100.0, src);
  db.add(Stat::AP, ModType::Base, 50.0, src);
  db.add(Stat::AD, ModType::Base, 70.0, src);
  db.add(Stat::MS, ModType::Base, 350.0, src);
  db.add(Stat::AR, ModType::Base, 30.0, src);
  db.add(Stat::MR, ModType::Base, 40.0, src);
  db.add(Stat::CDR, ModType::Base, 20.0, src);
  REQUIRE(db.getStat(Stat::HP) == Catch::Approx(100.0));
  REQUIRE(db.getStat(Stat::AP) == Catch::Approx(50.0));
  REQUIRE(db.getStat(Stat::AD) == Catch::Approx(70.0));
  REQUIRE(db.getStat(Stat::MS) == Catch::Approx(350.0));
  REQUIRE(db.getStat(Stat::AR) == Catch::Approx(30.0));
  REQUIRE(db.getStat(Stat::MR) == Catch::Approx(40.0));
  REQUIRE(db.getStat(Stat::CDR) == Catch::Approx(20.0));
}

TEST_CASE("ModDB getSumStat ignores Inc and More modifiers", "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::Base, 10.0, src);
  db.add(Stat::AD, ModType::Inc, 0.5, src);
  db.add(Stat::AD, ModType::More, 2.0, src);
  REQUIRE(db.getSumStat(Stat::AD) == Catch::Approx(10.0));
}

TEST_CASE("ModDB getIncStat ignores Base and More modifiers", "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::Base, 100.0, src);
  db.add(Stat::AD, ModType::Inc, 0.2, src);
  db.add(Stat::AD, ModType::More, 3.0, src);
  REQUIRE(db.getIncStat(Stat::AD) == Catch::Approx(1.2));
}

TEST_CASE("ModDB getMoreStat ignores Base and Inc modifiers", "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::Base, 100.0, src);
  db.add(Stat::AD, ModType::Inc, 0.5, src);
  db.add(Stat::AD, ModType::More, 2.5, src);
  REQUIRE(db.getMoreStat(Stat::AD) == Catch::Approx(2.5));
}

TEST_CASE("ModDB remove only erases first matching modifier", "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::Base, 10.0, src);
  db.add(Stat::AD, ModType::Base, 20.0, src);
  db.add(Stat::AD, ModType::Base, 30.0, src);
  db.remove(Stat::AD, ModType::Base, src);
  REQUIRE(db.get_mods().size() == 2);
  // first (10.0) removed, remaining: 20 + 30 = 50
  REQUIRE(db.getSumStat(Stat::AD) == Catch::Approx(50.0));
}

TEST_CASE(
    "ModDB remove by predicate removes nothing when predicate false for all",
    "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::Base, 10.0, src);
  db.add(Stat::HP, ModType::Base, 20.0, src);
  db.remove([](const Modifier &m) { return m.stat == Stat::AP; });
  REQUIRE(db.get_mods().size() == 2);
}

TEST_CASE(
    "ModDB remove by predicate removes everything with always-true predicate",
    "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::Base, 10.0, src);
  db.add(Stat::HP, ModType::Base, 20.0, src);
  db.add(Stat::AP, ModType::Inc, 0.1, src);
  db.remove([](const Modifier &) { return true; });
  REQUIRE(db.get_mods().empty());
}

TEST_CASE("ModDB replace preserves other modifiers", "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::HP, ModType::Base, 100.0, src);
  db.add(Stat::AD, ModType::Base, 50.0, src);
  db.replace(Stat::AD, ModType::Base, 75.0, src);
  REQUIRE(db.get_mods().size() == 2);
  REQUIRE(db.getStat(Stat::AD) == Catch::Approx(75.0));
  REQUIRE(db.getStat(Stat::HP) == Catch::Approx(100.0));
}

TEST_CASE("ModDB replace with different source inserts new, keeps old",
          "[mod_db]") {
  ModDB db;
  Source item{"Item", ""};
  Source buff{"Buff", ""};
  db.add(Stat::AD, ModType::Base, 50.0, item);
  db.replace(Stat::AD, ModType::Base, 30.0, buff);
  // different (stat, type, source) → insert, not update
  REQUIRE(db.get_mods().size() == 2);
  REQUIRE(db.getSumStat(Stat::AD) == Catch::Approx(80.0));
}

TEST_CASE("ModDB getSumStat predicate filters by type", "[mod_db]") {
  ModDB db;
  Source item{"Item", ""};
  Source buff{"Buff", ""};
  db.add(Stat::AD, ModType::Base, 10.0, item);
  db.add(Stat::AD, ModType::Base, 20.0, buff);
  db.add(Stat::AD, ModType::Inc, 0.5, item);
  // predicate matches only item; but getSumStat only sums Base, so Inc is
  // excluded anyway
  REQUIRE(db.getSumStat(Stat::AD, [](const Modifier &m) {
    return m.source.name == "Item";
  }) == Catch::Approx(10.0));
}

TEST_CASE("ModDB full pipeline with mixed stats and sources", "[mod_db]") {
  ModDB db;
  Source item{"Item", ""};
  Source buff{"Buff", ""};
  Source rune{"Rune", ""};
  // AD: 50 base + 30 base, +0.2 inc, *1.5 more
  db.add(Stat::AD, ModType::Base, 50.0, item);
  db.add(Stat::AD, ModType::Base, 30.0, buff);
  db.add(Stat::AD, ModType::Inc, 0.2, rune);
  db.add(Stat::AD, ModType::More, 1.5, item);
  // sum=80, inc=1.2, more=1.5 → 80*1.2*1.5=144
  REQUIRE(db.getStat(Stat::AD) == Catch::Approx(144.0));
}

TEST_CASE("ModDB empty ModDB all getters return identity", "[mod_db]") {
  ModDB db;
  REQUIRE(db.getSumStat(Stat::AD) == Catch::Approx(0.0));
  REQUIRE(db.getIncStat(Stat::AD) == Catch::Approx(1.0));
  REQUIRE(db.getMoreStat(Stat::AD) == Catch::Approx(1.0));
  REQUIRE(db.getStat(Stat::AD) == Catch::Approx(0.0));
}

TEST_CASE("ModDB add then remove leaves empty", "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::AD, ModType::Base, 10.0, src);
  REQUIRE(db.get_mods().size() == 1);
  db.remove(Stat::AD, ModType::Base, src);
  REQUIRE(db.get_mods().empty());
  REQUIRE(db.getStat(Stat::AD) == Catch::Approx(0.0));
}