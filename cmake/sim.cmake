# pc_sim (src/sim_main.cpp): the world alone, without SDL, Vulkan or sound. It steps the stage
# viewer's fight from packs the game has cooked, for the crossplay hash check, the rollback check and
# the step timings (docs/numbers.md "Determinism", docs/multiplayer.md "Measured"). Built beside the
# game; -DPHANTOMCRASH_SIM_ONLY=ON builds it alone with only gfxp and gsexp (a cross build: ARM64).
# -DPHANTOMCRASH_FIGHTER_SLOTS=N sizes the world for N fighters (6 is the game's).
#
# The world itself is phantomcrash_world (cmake/world.cmake), the game's own library; pc_sim adds
# its main, the command line, the save's world part and the pack readers.
set(phantomcrash_sim_root "${CMAKE_CURRENT_LIST_DIR}/..")
if(NOT DEFINED PHANTOMCRASH_COOKED_DIR)
    set(PHANTOMCRASH_COOKED_DIR "${phantomcrash_sim_root}/cooked" CACHE PATH "The cook's output, cooked/")
endif()
set(PHANTOMCRASH_SIM_SOURCES
    src/sim_main.cpp src/cli.cpp src/world/game_state.cpp
    src/cook/pack.cpp src/cook/pack_library.cpp src/cook/cook_plan.cpp src/cook/sexp_read.cpp
    src/cook/read_manifest.cpp src/cook/read_tables.cpp src/cook/read_fighters.cpp
    src/cook/read_motion.cpp src/cook/data_hash.cpp)
list(TRANSFORM PHANTOMCRASH_SIM_SOURCES PREPEND "${phantomcrash_sim_root}/")

# a world of other than 6 fighters is a library of its own (the game's stays at 6)
set(PHANTOMCRASH_FIGHTER_SLOTS "" CACHE STRING "Fighter slots in pc_sim's world (empty: the game's 6)")
set(phantomcrash_sim_world phantomcrash_world)
if(PHANTOMCRASH_FIGHTER_SLOTS)
    set(phantomcrash_sim_world phantomcrash_world_sim)
    phantomcrash_world_library(phantomcrash_world_sim ${PHANTOMCRASH_FIGHTER_SLOTS})
endif()

add_executable(pc_sim ${PHANTOMCRASH_SIM_SOURCES})
target_link_libraries(pc_sim PRIVATE ${phantomcrash_sim_world})
target_compile_definitions(pc_sim PRIVATE PHANTOMCRASH_DATA_DIR="${PHANTOMCRASH_DATA_DIR}"
    PHANTOMCRASH_COOKED_DIR="${PHANTOMCRASH_COOKED_DIR}")
if(NOT MSVC)
    target_compile_options(pc_sim PRIVATE -Wall -Wextra -Wpedantic)
endif()
