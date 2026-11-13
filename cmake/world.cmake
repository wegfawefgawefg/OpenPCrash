# The deterministic world (AGENTS.md "Simulation"): step_world, the battle, the fighters, the AI,
# the story and the loaders the battle data needs, without SDL, Vulkan or sound. One library the
# game and pc_sim (cmake/sim.cmake) both link, so a new world .cpp goes here once. Include after
# fx.cmake (it needs phantomcrash_fx, gfxp and gsexp).
#
# Not here: src/cli.cpp (its default paths differ by target), src/world/game_state.cpp (the game
# has it in phantomcrash_save, cmake/save.cmake) and the pack readers (the game's phantomcrash_cook
# links SDL; pc_sim compiles the readers itself).
get_filename_component(phantomcrash_world_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(PHANTOMCRASH_WORLD_SOURCES
    src/viewer_world.cpp src/battle_world.cpp
    src/input/script_input.cpp src/input/pad_read.cpp src/input/menu_input.cpp
    src/world/world.cpp src/world/world_hash.cpp src/world/world_hash_save.cpp
    src/world/control_route.cpp src/world/world_random.cpp
    src/story/story_tables.cpp src/story/progress.cpp src/story/days.cpp src/story/triggers.cpp
    src/story/roster_tables.cpp src/story/rumble.cpp src/story/rumble_roster.cpp
    src/battle/battle_spawn.cpp src/battle/battle_ending.cpp src/battle/part_records.cpp
    src/battle/battle_data.cpp src/battle/stage_collision.cpp src/battle/battle_camera.cpp
    src/battle/camera_views.cpp
    src/battle/hud_senses.cpp src/battle/fighters_separate.cpp src/battle/vs_battle.cpp
    src/battle/battle_flow.cpp src/battle/battle_dev.cpp src/battle/battle_scenes.cpp
    src/battle/battle_scene_ends.cpp src/battle/battle_settle.cpp src/battle/battle_tutorial.cpp
    src/battle/scene_curves.cpp src/battle/battle_barks.cpp
    src/stage/stage_data.cpp src/stage/stage_world.cpp src/stage/stage_props.cpp
    src/stage/supply_craft.cpp src/stage/supply_crates.cpp src/stage/pickup_apply.cpp
    src/mech/fighter_spawn.cpp src/mech/fighter_motion.cpp src/mech/fighter_root.cpp
    src/mech/fighter_air.cpp src/mech/fighter_contact.cpp src/mech/fighter_hurt.cpp
    src/mech/rig_data.cpp src/mech/fighter_camo.cpp
    src/mech/pose_ik.cpp src/mech/pose_walk.cpp src/mech/pose_weapons.cpp
    src/weapons/combat_tables.cpp src/weapons/combat_spawn.cpp src/weapons/weapon_fire.cpp
    src/weapons/shot_movers.cpp src/weapons/shot_pass.cpp src/weapons/shot_hits.cpp
    src/weapons/damage_settle.cpp src/weapons/hud_aim.cpp src/weapons/weapon_clip.cpp
    src/ai/ai_data.cpp src/ai/ai_links.cpp src/ai/ai_update.cpp src/ai/ai_target.cpp
    src/ai/ai_aim.cpp src/ai/ai_route.cpp src/ai/ai_weapons.cpp src/ai/ai_states.cpp
    src/ai/ai_ranker.cpp
    src/hud/hud_world.cpp
    src/assets/pack_assets.cpp src/assets/motion_sets.cpp)
list(TRANSFORM PHANTOMCRASH_WORLD_SOURCES PREPEND "${phantomcrash_world_root}/")

# the world as a library named name; slots: its fighter slots (empty: the game's 6)
function(phantomcrash_world_library name slots)
    add_library(${name} STATIC ${PHANTOMCRASH_WORLD_SOURCES})
    target_include_directories(${name} PUBLIC "${phantomcrash_world_root}/src")
    target_link_libraries(${name} PUBLIC phantomcrash_fx gfxp::gfxp gsexp::gsexp)
    # a few world files reach SDL's headers through shared ones (render/renderer.hpp's types);
    # nothing links SDL. pc_sim alone (PHANTOMCRASH_SIM_ONLY) takes the system's headers
    if(TARGET SDL3::Headers)
        target_link_libraries(${name} PRIVATE SDL3::Headers)
    endif()
    if(slots)
        target_compile_definitions(${name} PUBLIC PHANTOMCRASH_FIGHTER_SLOTS=${slots})
    endif()
    if(MSVC)
        target_compile_options(${name} PRIVATE /W3)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

phantomcrash_world_library(phantomcrash_world "")
