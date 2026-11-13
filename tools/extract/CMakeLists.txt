# pc_extract: the original disc and XBE to data/ (docs/assets.md). One source pair per family; add a
# family's .cpp to the library and its row in extract_run.cpp's families[]. The families are a
# static library the game links too, for its first-run screen (src/setup); the build's default
# paths go only into pc_extract's main.cpp, never into the game.
add_library(pc_extract_lib STATIC
    xbe.cpp disc.cpp disc_image.cpp extract_run.cpp manifest.cpp png.cpp
    textures.cpp systemdata.cpp
    model_decode.cpp gltf_write.cpp models.cpp
    sjis.cpp stages.cpp stage_lighting.cpp stage_props.cpp fighters.cpp skeletons.cpp motion.cpp
    tables.cpp tables_parts.cpp tables_roster.cpp tables_tournaments.cpp tables_music.cpp
    tables_flow.cpp tables_ai.cpp effects.cpp
    audio.cpp audio_streams.cpp audio_codec.cpp
    glyphs.cpp event_scripts.cpp ui_strings.cpp text.cpp scripts.cpp
    collision.cpp nav.cpp scenes.cpp
    ui.cpp ui_layouts.cpp ui_timeline.cpp ui_images.cpp)
target_link_libraries(pc_extract_lib PUBLIC gsexp::gsexp SDL3::SDL3 SDL3_image::SDL3_image)
target_include_directories(pc_extract_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
if(APPLE)
    target_link_libraries(pc_extract_lib PRIVATE iconv) # sjis.cpp; Linux's is in libc, Windows uses code page 932
endif()
if(NOT MSVC)
    target_compile_options(pc_extract_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(pc_extract main.cpp)
target_link_libraries(pc_extract PRIVATE pc_extract_lib)
if(NOT MSVC)
    target_compile_options(pc_extract PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_compile_definitions(pc_extract PRIVATE
    PHANTOMCRASH_DISC_DIR="${PHANTOMCRASH_DISC_DIR}"
    PHANTOMCRASH_XBE_PATH="${PHANTOMCRASH_XBE_PATH}"
    PHANTOMCRASH_DATA_DIR="${PHANTOMCRASH_DATA_DIR}")
