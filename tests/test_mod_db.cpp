#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "moba_sim.hpp"

using moba::ModDB;
using moba::Modifier;
using moba::ModType;
using moba::Source;
using moba::Stat;
using moba::Type;

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

TEST_CASE("ModDB remove by stat/type/source is no-op when not found", "[mod_db]") {
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

TEST_CASE("ModDB getSumStat sums Base modifiers for matching stat", "[mod_db]") {
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
  REQUIRE(db.getSumStat(Stat::AD,
                        [](const Modifier &m) { return m.source.name == "Item"; }) ==
          Catch::Approx(10.0));
  REQUIRE(db.getSumStat(Stat::AD,
                        [](const Modifier &m) { return m.source.name == "Buff"; }) ==
          Catch::Approx(20.0));
}

TEST_CASE("ModDB multiple modifiers of same stat and type aggregate", "[mod_db]") {
  ModDB db;
  Source src{"Item", ""};
  db.add(Stat::HP, ModType::Base, 100.0, src);
  db.add(Stat::HP, ModType::Base, 200.0, src);
  db.add(Stat::HP, ModType::Inc, 0.5, src);
  db.add(Stat::HP, ModType::More, 2.0, src);
  // sum=300, inc=1.5, more=2.0 → 300*1.5*2.0=900
  REQUIRE(db.getStat(Stat::HP) == Catch::Approx(900.0));
}