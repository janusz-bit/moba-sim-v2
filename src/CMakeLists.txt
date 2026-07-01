add_library(moba_sim STATIC
    moba_sim.cpp
)

target_include_directories(moba_sim PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)