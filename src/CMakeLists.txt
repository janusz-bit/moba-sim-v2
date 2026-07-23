add_library(moba_sim STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/mod_db.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/combat.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/champion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/simulation.cpp
)

target_include_directories(moba_sim PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

install(TARGETS moba_sim)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/
        DESTINATION include)