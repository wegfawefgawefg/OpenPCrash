cmake_minimum_required(VERSION 3.20)
project(phantomcrash LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
# no fused multiply-adds: the few floats the world and the cook still compute (repair costs,
# destruction values, curve weights, converted tables) round the same on every machine. Clang
# fuses a * b + c within an expression by default and does on ARM64 (docs/numbers.md
# "Determinism"); x86-64 without -mfma has none to fuse. MSVC's /fp:precise doesn't fuse either.
if(NOT MSVC)
    add_compile_options(-ffp-contract=off)
endif()
# pc_sim alone, the headless world without SDL, Vulkan or sound (cmake/sim.cmake): for cross builds
option(PHANTOMCRASH_SIM_ONLY "Build only pc_sim (gfxp and gsexp, no SDL)" OFF)
include(cmake/platform.cmake)
include(cmake/deps.cmake)
# the world's fixed-point maths (src/fx, docs/numbers.md)
include(cmake/fx.cmake)

# where the original lives (read only) and where its converted copy goes (docs/assets.md); the
# extractor and the game take --disc, --xbe and --data/--out instead
set(PHANTOMCRASH_DISC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../phantom-crash-archive/assets/raw" CACHE PATH
    "The extracted disc (the folder holding Media/)")
set(PHANTOMCRASH_XBE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../phantom-crash-decomp/orig/default.xbe" CACHE FILEPATH
    "The retail default.xbe")
set(PHANTOMCRASH_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data" CACHE PATH "The extractor's output, data/")

if(PHANTOMCRASH_SIM_ONLY)
    include(cmake/sim.cmake)
    return()
endif()

# the extractor: the original to data/
add_subdirectory(tools/extract)

# the game
add_executable(phantomcrash
    src/main.cpp src/run_log.cpp src/cli.cpp src/viewer_battle.cpp src/viewer_world.cpp src/battle_world.cpp src/battle/battle_spawn.cpp src/battle/battle_ending.cpp
    src/world/world.cpp src/world/world_hash.cpp src/world/world_hash_save.cpp src/save/vs_saves.cpp src/world/control_route.cpp src/world/world_random.cpp
    src/story/story_tables.cpp src/story/progress.cpp src/story/days.cpp src/story/triggers.cpp
    src/talk/talk_script.cpp src/talk/sprite_registry.cpp src/talk/text_draw.cpp
    src/talk/talk_interp.cpp src/talk/talk_task.cpp src/talk/talk_canvas.cpp src/talk/talk_player.cpp
    src/talk/talk_skip.cpp
    src/input/pad_read.cpp src/input/menu_input.cpp src/input/bindings.cpp src/input/device_codes.cpp
    src/input/devices.cpp src/input/dev_route.cpp src/input/script_input.cpp src/input/pad_calibration.cpp
    src/screens/ui/ui.cpp src/screens/ui/ui_data.cpp src/screens/ui/ui_anchors.cpp
    src/screens/ui/ui_anchor_shapes.cpp src/screens/ui/menu_pointer.cpp src/screens/ui/screen_text.cpp
    src/screens/ui/screen_widgets.cpp src/screens/flow/front_end.cpp src/screens/flow/dev_seed.cpp src/screens/flow/talk_skips.cpp
    src/screens/title/boot_screens.cpp
    src/screens/title/title_screens.cpp src/screens/system/options_screens.cpp src/screens/system/save_screens.cpp
    src/screens/flow/story_screens.cpp src/screens/garage/part_tables.cpp src/screens/garage/garage_stats.cpp
    src/screens/garage/garage_rules.cpp src/screens/ui/screen_kit.cpp src/screens/garage/model_stand.cpp src/screens/garage/stand_parts.cpp
    src/screens/garage/stand_loader.cpp src/screens/garage/stand_turn.cpp
    src/screens/title/name_entry.cpp src/screens/garage/garage_hub.cpp src/screens/garage/assemble_screen.cpp
    src/screens/garage/assemble_draw.cpp src/screens/garage/part_card.cpp src/screens/garage/spec_screen.cpp
    src/screens/shops/plus_tech.cpp src/screens/shops/plus_tech_draw.cpp src/screens/shops/plus_tech_chips.cpp
    src/screens/garage/chip_carousel.cpp src/screens/shops/wild_machines.cpp src/screens/shops/wild_steps.cpp src/screens/shops/wild_draw.cpp
    src/screens/garage/stock_screen.cpp
    src/screens/shops/music_player.cpp src/screens/ui/screen_header.cpp src/screens/system/save_slots.cpp
    src/screens/flow/menu_dispatch.cpp src/screens/system/system_screens.cpp src/screens/system/key_config_screens.cpp
    src/screens/system/record_screens.cpp src/screens/system/record_grids.cpp src/screens/flow/help_screens.cpp
    src/screens/flow/pad_watch.cpp src/screens/shops/sonic_amp.cpp src/screens/shops/sonic_amp_draw.cpp
    src/screens/shops/media_player.cpp src/screens/shops/media_edit.cpp src/screens/shops/media_draw.cpp
    src/screens/shops/dealer.cpp src/screens/shops/dealer_buy.cpp src/screens/shops/dealer_draw.cpp
    src/screens/title/ending_screens.cpp
    src/screens/campaign/campaign_dispatch.cpp src/screens/garage/hangar.cpp src/screens/garage/hangar_draw.cpp
    src/screens/campaign/schedule_screen.cpp src/screens/campaign/job_cards.cpp src/screens/campaign/job_wanderers.cpp src/screens/campaign/scene_text.cpp
    src/screens/pc/pc_settings.cpp src/screens/pc/pc_display.cpp src/screens/pc/pc_settings_art.cpp src/screens/pc/pc_settings_rows.cpp
    src/screens/pc/pc_settings_binds.cpp src/screens/pc/pc_settings_axes.cpp src/screens/pc/pc_settings_screen.cpp
    src/screens/pc/pc_settings_calibrate.cpp
    src/view/view.cpp src/view/input.cpp src/view/combat_viewer.cpp src/view/combat_sounds.cpp
    src/view/fighter_sounds.cpp
    src/view/footsteps.cpp
    src/render/d3dx_math.cpp
    src/assets/pack_assets.cpp src/assets/motion_sets.cpp
    src/stage/stage_scene.cpp src/stage/stage_data.cpp src/stage/stage_world.cpp src/stage/stage_props.cpp
    src/stage/supply_craft.cpp src/stage/supply_crates.cpp src/stage/stage_animate.cpp
    src/stage/stage_view.cpp src/stage/sky_craft.cpp
    src/stage/stage_audio.cpp
    src/mech/mech_pose.cpp src/mech/mech_ik.cpp src/mech/mech_parts.cpp src/mech/mech_load.cpp
    src/mech/fighter_spawn.cpp src/mech/fighter_motion.cpp src/mech/fighter_root.cpp src/mech/fighter_air.cpp
    src/mech/fighter_contact.cpp src/mech/fighter_hurt.cpp src/mech/rig_data.cpp
    src/mech/fighter_camo.cpp src/stage/pickup_apply.cpp
    src/mech/pose_ik.cpp src/mech/pose_walk.cpp src/mech/pose_weapons.cpp src/mech/pose_compare.cpp
    src/weapons/combat_tables.cpp src/weapons/combat_spawn.cpp src/weapons/weapon_fire.cpp
    src/weapons/shot_movers.cpp src/weapons/shot_pass.cpp src/weapons/shot_hits.cpp
    src/weapons/damage_settle.cpp src/weapons/hud_aim.cpp src/weapons/weapon_clip.cpp
    src/hud/hud_data.cpp src/hud/hud_core.cpp src/hud/hud_readout.cpp src/hud/hud_gauges.cpp src/hud/hud_meters.cpp
    src/hud/hud_reticle.cpp src/hud/hud_radar.cpp src/hud/hud_scenes.cpp src/hud/hud_barks.cpp
    src/hud/hud_world.cpp
    src/battle/part_records.cpp src/battle/battle_data.cpp src/battle/stage_collision.cpp
    src/battle/battle_camera.cpp src/battle/hud_senses.cpp src/battle/fighters_separate.cpp
    src/battle/camera_views.cpp src/view/cockpit_view.cpp src/input/pad_rumble.cpp
    src/ai/ai_data.cpp src/ai/ai_links.cpp src/ai/ai_update.cpp src/ai/ai_target.cpp src/ai/ai_aim.cpp
    src/ai/ai_route.cpp src/ai/ai_weapons.cpp src/ai/ai_states.cpp src/ai/ai_ranker.cpp
    src/render/scene_draw.cpp src/render/scenery.cpp src/render/mesh_queue.cpp src/render/mesh_draw.cpp
    src/render/mesh_constants.cpp src/render/stage_lighting.cpp src/render/camo_draw.cpp
    src/effects/effect_data.cpp src/effects/particle_pool.cpp src/effects/particle_start.cpp
    src/effects/particle_step.cpp src/effects/billboards.cpp src/effects/effects.cpp
    src/effects/sprite_fx.cpp src/effects/trails.cpp src/effects/stage_effects.cpp
    src/effects/combat_effects.cpp src/effects/screen_effects.cpp src/effects/fighter_effects.cpp
    src/effects/shot_looks.cpp src/effects/casings.cpp src/effects/contact_decals.cpp
    src/effects/shield_bubble.cpp src/effects/weapon_break.cpp src/effects/lamp_trails.cpp
    src/effects/cockpit_draw.cpp)
# local multiplayer: seats and devices, split-screen VS (docs/multiplayer.md)
target_sources(phantomcrash PRIVATE
    src/input/seats.cpp src/input/virtual_pads.cpp src/battle/vs_battle.cpp
    src/view/split_views.cpp src/battle_vs.cpp src/battle_replay.cpp src/screens/vs/vs_setup.cpp src/screens/vs/vs_draw.cpp
    src/screens/vs/vs_area.cpp)
# the campaign: the opponent queues and rosters, the battle's flow and entry, the hub screens
target_sources(phantomcrash PRIVATE
    src/story/roster_tables.cpp src/story/rumble.cpp src/story/rumble_roster.cpp
    src/battle/battle_flow.cpp src/battle/battle_dev.cpp src/battle/battle_scenes.cpp src/battle/battle_scene_ends.cpp src/battle/battle_settle.cpp
    src/battle/battle_tutorial.cpp
    src/screens/ui/hub_widgets.cpp src/screens/campaign/town_screens.cpp
    src/screens/campaign/club_wired.cpp src/screens/campaign/area_select.cpp src/screens/campaign/area_select_draw.cpp src/screens/campaign/area_plate.cpp
    src/screens/campaign/battle_screen.cpp src/screens/campaign/replay_screen.cpp src/screens/campaign/keep_sv.cpp src/screens/campaign/keep_sv_draw.cpp src/screens/campaign/result_screen.cpp src/battle_run.cpp)
# the battle's scene cameras, the walk out of the gate and the barks
target_sources(phantomcrash PRIVATE
    src/battle/scene_curves.cpp src/battle/battle_barks.cpp src/view/scene_camera.cpp
    src/screens/system/trophy_models.cpp)
# the stage viewer's loop (main runs it for --viewer); the AppState's own states
target_sources(phantomcrash PRIVATE src/viewer_run.cpp src/screens/flow/state_dispatch.cpp
    src/screens/garage/garage_stock.cpp src/screens/title/name_entry_draw.cpp src/screens/vs/vs_machines.cpp)
target_link_libraries(phantomcrash PRIVATE phantomcrash_fx gfxp::gfxp gsexp::gsexp gubsy::engine SDL3::SDL3 SDL3_image::SDL3_image phantomcrash_cgltf)
if(MSVC)
    target_compile_options(phantomcrash PRIVATE /W3)
else()
    target_compile_options(phantomcrash PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_include_directories(phantomcrash PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# gubsy's engine state ("src/engine_state.hpp"), for its settings pools, as MIMH reads them
if(PHANTOMCRASH_USE_LOCAL_DEPS)
    target_include_directories(phantomcrash PRIVATE "${PHANTOMCRASH_GUBSY_DIR}")
else()
    target_include_directories(phantomcrash PRIVATE "${gubsy_SOURCE_DIR}")
endif()
target_compile_definitions(phantomcrash PRIVATE PHANTOMCRASH_DATA_DIR="${PHANTOMCRASH_DATA_DIR}")
# the port's own data kept in git (assets/ui/anchors.sexp)
target_compile_definitions(phantomcrash PRIVATE PHANTOMCRASH_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

# the cook (src/cook): data/ to cooked/, in the game and as pc_cook
include(cmake/cook.cmake)
# the renderer (src/render/vk_*) and its shaders
include(cmake/renderer.cmake)
# the sound engine (src/audio)
include(cmake/audio.cmake)
# the movie player (src/movie)
include(cmake/movie.cmake)
# the game save (src/save) and pc_import_xbox_save
include(cmake/save.cmake)
# the headless world (src/sim_main.cpp): crossplay hashes, rollback check, timings
include(cmake/sim.cmake)
# the first-run screen (src/setup): the player's disc to data/ when there's none
include(cmake/setup.cmake)
# the version, a release's stamp and its paths (scripts/release.sh, docs/release.md)
include(cmake/release.cmake)
