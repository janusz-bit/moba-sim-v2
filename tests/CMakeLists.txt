find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(moba_sim_tests
    test_post_mitigation_damage.cpp
    test_mod_db.cpp
    test_champion.cpp
    test_scenarios.cpp
    test_combat.cpp
)

target_link_libraries(moba_sim_tests
    PRIVATE
        moba_sim
        Catch2::Catch2WithMain
)

catch_discover_tests(moba_sim_tests)