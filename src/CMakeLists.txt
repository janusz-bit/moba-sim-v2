add_library(moba_sim STATIC
    moba_sim.cpp
)

target_include_directories(moba_sim PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

install(TARGETS moba_sim)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/
        DESTINATION include)