#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "moba_sim.hpp"

using moba::post_mitigation_damage;
using moba::Type;

TEST_CASE("post_mitigation_damage with zero armor returns raw damage",
          "[post_mitigation_damage]") {
  REQUIRE(post_mitigation_damage(100.0, 0.0) == Catch::Approx(100.0));
}

TEST_CASE("post_mitigation_damage with positive armor reduces damage",
          "[post_mitigation_damage]") {
  REQUIRE(post_mitigation_damage(100.0, 100.0) == Catch::Approx(50.0));
  REQUIRE(post_mitigation_damage(100.0, 200.0) ==
          Catch::Approx(33.3333).epsilon(0.01));
  REQUIRE(post_mitigation_damage(100.0, 300.0) == Catch::Approx(25.0));
}

TEST_CASE("post_mitigation_damage with negative armor amplifies damage",
          "[post_mitigation_damage]") {
  REQUIRE(post_mitigation_damage(100.0, -50.0) ==
          Catch::Approx(133.3333).epsilon(0.01));
  REQUIRE(post_mitigation_damage(100.0, -100.0) == Catch::Approx(150.0));
  REQUIRE(post_mitigation_damage(100.0, -200.0) ==
          Catch::Approx(166.6667).epsilon(0.01));
}

TEST_CASE("post_mitigation_damage approaches 200% for very negative armor",
          "[post_mitigation_damage]") {
  Type result = post_mitigation_damage(100.0, -10000.0);
  REQUIRE(std::isfinite(result));
  REQUIRE(result == Catch::Approx(199.0).epsilon(0.01));
}

TEST_CASE("post_mitigation_damage with zero raw damage returns zero",
          "[post_mitigation_damage]") {
  REQUIRE(post_mitigation_damage(0.0, 100.0) == 0.0);
  REQUIRE(post_mitigation_damage(0.0, -50.0) == 0.0);
  REQUIRE(post_mitigation_damage(0.0, 0.0) == 0.0);
}

TEST_CASE("post_mitigation_damage with negative raw damage stays negative",
          "[post_mitigation_damage]") {
  // Negative raw damage should be reduced/amplified in the same proportion,
  // not clamped to zero.
  REQUIRE(post_mitigation_damage(-100.0, 100.0) == Catch::Approx(-50.0));
  REQUIRE(post_mitigation_damage(-100.0, -100.0) == Catch::Approx(-150.0));
}

TEST_CASE("post_mitigation_damage is linear in raw damage",
          "[post_mitigation_damage]") {
  const Type armor = 75.0;
  const Type factor = 100.0 / (100.0 + armor);
  REQUIRE(post_mitigation_damage(1.0, armor) ==
          Catch::Approx(factor).epsilon(0.001));
  REQUIRE(post_mitigation_damage(10.0, armor) ==
          Catch::Approx(10.0 * factor).epsilon(0.001));
  REQUIRE(post_mitigation_damage(1000.0, armor) ==
          Catch::Approx(1000.0 * factor).epsilon(0.001));
}

TEST_CASE("post_mitigation_damage is symmetric in armor sign for small values",
          "[post_mitigation_damage]") {
  // For armor a, positive: 100 * 100/(100+a); for -a: 100 * (2 - 100/(100-a))
  // At a=0 both branches equal 100.0; check continuity near zero.
  REQUIRE(post_mitigation_damage(100.0, 1.0) ==
          Catch::Approx(99.0099).epsilon(0.001));
  REQUIRE(post_mitigation_damage(100.0, -1.0) ==
          Catch::Approx(101.0101).epsilon(0.001));
}

TEST_CASE("post_mitigation_damage halves at armor=100",
          "[post_mitigation_damage]") {
  REQUIRE(post_mitigation_damage(100.0, 100.0) == Catch::Approx(50.0));
}

TEST_CASE("post_mitigation_damage at armor=300 is 25%",
          "[post_mitigation_damage]") {
  // 100 * 100/(100+300) = 25
  REQUIRE(post_mitigation_damage(100.0, 300.0) == Catch::Approx(25.0));
}

TEST_CASE("post_mitigation_damage never exceeds 200% for any negative armor",
          "[post_mitigation_damage]") {
  // As armor → -∞, damage → 200% but never reaches it.
  REQUIRE(post_mitigation_damage(100.0, -1000.0) < 200.0);
  REQUIRE(post_mitigation_damage(100.0, -1000000.0) < 200.0);
  REQUIRE(post_mitigation_damage(100.0, -1000000.0) > 199.0);
}

TEST_CASE("post_mitigation_damage is finite for huge armor magnitudes",
          "[post_mitigation_damage]") {
  REQUIRE(std::isfinite(post_mitigation_damage(1e9, 1e9)));
  REQUIRE(std::isfinite(post_mitigation_damage(1e9, -1e9)));
  REQUIRE(std::isfinite(post_mitigation_damage(1e-9, 1e9)));
}

TEST_CASE("post_mitigation_damage at armor=-100 hits the 150% branch point",
          "[post_mitigation_damage]") {
  // armor=-100: 100 * (2 - 100/(100 - (-100))) = 100 * (2 - 0.5) = 150
  REQUIRE(post_mitigation_damage(100.0, -100.0) == Catch::Approx(150.0));
}

TEST_CASE("post_mitigation_damage tiny raw damage with huge armor is tiny",
          "[post_mitigation_damage]") {
  REQUIRE(post_mitigation_damage(0.001, 10000.0) ==
          Catch::Approx(0.0000099).epsilon(0.01));
}