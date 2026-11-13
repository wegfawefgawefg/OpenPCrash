# The first-run screen (src/setup): with no data/ yet the game asks for the player's own disc image
# or game folder and extracts it with the extractor's families (pc_extract_lib, tools/extract), then
# cooks it. Its words are SDL_ttf in Jost (assets/fonts, SIL Open Font License 1.1).
target_sources(phantomcrash PRIVATE
    src/setup/setup_screen.cpp src/setup/setup_job.cpp src/setup/setup_draw.cpp)
if(TARGET SDL3_ttf::SDL3_ttf)
    set(phantomcrash_ttf SDL3_ttf::SDL3_ttf)
elseif(TARGET SDL3_ttf::SDL3_ttf-shared)
    set(phantomcrash_ttf SDL3_ttf::SDL3_ttf-shared)
else()
    set(phantomcrash_ttf SDL3_ttf::SDL3_ttf-static)
endif()
target_link_libraries(phantomcrash PRIVATE pc_extract_lib ${phantomcrash_ttf})
