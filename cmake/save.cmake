# The game save (src/save, docs/saves.md) as a library the game links, with the world's saved state
# beside the story (src/world/game_state.cpp); and pc_import_xbox_save, which turns a retail
# phantomcrash.sav into the port's save and runs the format's round trip by hand.
add_library(phantomcrash_save STATIC
    src/save/save_text.cpp src/save/save_parse.cpp src/save/save_parse_rumble.cpp src/save/save_files.cpp
    src/save/xbox_save.cpp
    src/world/game_state.cpp)
target_link_libraries(phantomcrash_save PUBLIC phantomcrash_fx gsexp::gsexp SDL3::SDL3)
target_include_directories(phantomcrash_save PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(NOT MSVC)
    target_compile_options(phantomcrash_save PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(phantomcrash PRIVATE phantomcrash_save)

# the story code the round trip's new game needs, and the table reader the font goes through
add_executable(pc_import_xbox_save tools/import_xbox_save/main.cpp
    src/story/story_tables.cpp src/story/progress.cpp src/story/days.cpp src/story/triggers.cpp
    src/cook/sexp_read.cpp)
target_link_libraries(pc_import_xbox_save PRIVATE phantomcrash_save)
target_compile_definitions(pc_import_xbox_save PRIVATE PHANTOMCRASH_DATA_DIR_DEFAULT="${PHANTOMCRASH_DATA_DIR}")
if(NOT MSVC)
    target_compile_options(pc_import_xbox_save PRIVATE -Wall -Wextra -Wpedantic)
endif()
