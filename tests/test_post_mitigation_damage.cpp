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
  REQUIRE(post_mitigation_damage(100.0, 200.0) == Catch::Approx(33.3333).epsilon(0.01));
  REQUIRE(post_mitigation_damage(100.0, 300.0) == Catch::Approx(25.0));
}

TEST_CASE("post_mitigation_damage with negative armor amplifies damage",
          "[post_mitigation_damage]") {
  REQUIRE(post_mitigation_damage(100.0, -50.0) == Catch::Approx(133.3333).epsilon(0.01));
  REQUIRE(post_mitigation_damage(100.0, -100.0) == Catch::Approx(150.0));
  REQUIRE(post_mitigation_damage(100.0, -200.0) == Catch::Approx(166.6667).epsilon(0.01));
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