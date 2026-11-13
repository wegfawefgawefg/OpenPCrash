# pc_sim (src/sim_main.cpp): the world alone, without SDL, Vulkan or sound. It steps the stage
# viewer's fight from packs the game has cooked, for the crossplay hash check, the rollback check and
# the step timings (docs/numbers.md "Determinism", docs/multiplayer.md "Measured"). Built beside the
# game; -DPHANTOMCRASH_SIM_ONLY=ON builds it alone with only gfxp and gsexp (a cross build: ARM64).
# -DPHANTOMCRASH_FIGHTER_SLOTS=N sizes the world for N fighters (6 is the game's).
#
# Its sources are the world's and the loaders the battle data needs; a new world .cpp goes here too
# (the link says so).
set(phantomcrash_sim_root "${CMAKE_CURRENT_LIST_DIR}/..")
if(NOT DEFINED PHANTOMCRASH_COOKED_DIR)
    set(PHANTOMCRASH_COOKED_DIR "${phantomcrash_sim_root}/cooked" CACHE PATH "The cook's output, cooked/")
endif()
set(PHANTOMCRASH_SIM_SOURCES
    src/sim_main.cpp src/cli.cpp src/viewer_world.cpp src/battle_world.cpp src/input/script_input.cpp
    src/input/pad_read.cpp src/input/menu_input.cpp
    src/world/world.cpp src/world/world_hash.cpp src/world/world_hash_save.cpp
    src/world/control_route.cpp src/world/world_random.cpp src/world/game_state.cpp
    src/story/story_tables.cpp src/story/progress.cpp src/story/days.cpp src/story/triggers.cpp
    src/story/roster_tables.cpp src/story/rumble.cpp src/story/rumble_roster.cpp
    src/battle/battle_spawn.cpp src/battle/battle_ending.cpp src/battle/part_records.cpp
    src/battle/battle_data.cpp src/battle/stage_collision.cpp src/battle/battle_camera.cpp
    src/battle/camera_views.cpp
    src/battle/hud_senses.cpp src/battle/fighters_separate.cpp src/battle/vs_battle.cpp
    src/battle/battle_flow.cpp src/battle/battle_dev.cpp src/battle/battle_scenes.cpp src/battle/battle_scene_ends.cpp
    src/battle/battle_settle.cpp src/battle/battle_tutorial.cpp src/battle/scene_curves.cpp
    src/battle/battle_barks.cpp
    src/stage/stage_data.cpp src/stage/stage_world.cpp src/stage/stage_props.cpp
    src/stage/supply_craft.cpp src/stage/supply_crates.cpp src/stage/pickup_apply.cpp
    src/mech/fighter_spawn.cpp src/mech/fighter_motion.cpp src/mech/fighter_root.cpp src/mech/fighter_air.cpp
    src/mech/fighter_contact.cpp src/mech/fighter_hurt.cpp src/mech/rig_data.cpp
    src/mech/fighter_camo.cpp src/mech/pose_ik.cpp src/mech/pose_walk.cpp src/mech/pose_weapons.cpp
    src/weapons/combat_tables.cpp src/weapons/combat_spawn.cpp src/weapons/weapon_fire.cpp
    src/weapons/shot_movers.cpp src/weapons/shot_pass.cpp src/weapons/shot_hits.cpp
    src/weapons/damage_settle.cpp src/weapons/hud_aim.cpp src/weapons/weapon_clip.cpp
    src/ai/ai_data.cpp src/ai/ai_links.cpp src/ai/ai_update.cpp src/ai/ai_target.cpp
    src/ai/ai_aim.cpp src/ai/ai_route.cpp src/ai/ai_weapons.cpp src/ai/ai_states.cpp
    src/ai/ai_ranker.cpp
    src/hud/hud_world.cpp
    src/assets/pack_assets.cpp src/assets/motion_sets.cpp
    src/cook/pack.cpp src/cook/pack_library.cpp src/cook/cook_plan.cpp src/cook/sexp_read.cpp
    src/cook/read_manifest.cpp src/cook/read_tables.cpp src/cook/read_fighters.cpp
    src/cook/read_motion.cpp src/cook/data_hash.cpp)
list(TRANSFORM PHANTOMCRASH_SIM_SOURCES PREPEND "${phantomcrash_sim_root}/")

add_executable(pc_sim ${PHANTOMCRASH_SIM_SOURCES})
target_link_libraries(pc_sim PRIVATE phantomcrash_fx gfxp::gfxp gsexp::gsexp)
target_include_directories(pc_sim PRIVATE "${phantomcrash_sim_root}/src")
target_compile_definitions(pc_sim PRIVATE PHANTOMCRASH_DATA_DIR="${PHANTOMCRASH_DATA_DIR}"
    PHANTOMCRASH_COOKED_DIR="${PHANTOMCRASH_COOKED_DIR}")
set(PHANTOMCRASH_FIGHTER_SLOTS "" CACHE STRING "Fighter slots in pc_sim's world (empty: the game's 6)")
if(PHANTOMCRASH_FIGHTER_SLOTS)
    target_compile_definitions(pc_sim PRIVATE PHANTOMCRASH_FIGHTER_SLOTS=${PHANTOMCRASH_FIGHTER_SLOTS})
endif()
if(NOT MSVC)
    target_compile_options(pc_sim PRIVATE -Wall -Wextra -Wpedantic)
endif()
