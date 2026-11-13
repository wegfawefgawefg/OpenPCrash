# The cook (src/cook, docs/assets.md "Packs"): data/ to cooked/ packs. A library the game links (it
# cooks on first run and re-cooks stale packs as it opens them) and the standalone pc_cook.
set(PHANTOMCRASH_COOKED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cooked" CACHE PATH "The cook's output, cooked/")

# BC1/BC3 (rgbcx) and BC7 (bc7enc) encoders, MIT, at a pinned commit. Only its two encoder
# sources are compiled, its own project isn't.
FetchContent_Declare(bc7enc_rdo GIT_REPOSITORY https://github.com/richgel999/bc7enc_rdo.git
    GIT_TAG b9438627eef73a1157e84201b6fa6eb2ffd6d9f0 SOURCE_SUBDIR not-its-cmake)
FetchContent_MakeAvailable(bc7enc_rdo)
add_library(phantomcrash_bc STATIC "${bc7enc_rdo_SOURCE_DIR}/bc7enc.cpp" "${bc7enc_rdo_SOURCE_DIR}/rgbcx.cpp")
target_include_directories(phantomcrash_bc SYSTEM PUBLIC "${bc7enc_rdo_SOURCE_DIR}")
target_compile_options(phantomcrash_bc PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2 -w>)

add_library(phantomcrash_cook STATIC
    src/cook/pack.cpp src/cook/pack_write.cpp src/cook/pack_library.cpp
    src/cook/cook.cpp src/cook/cook_plan.cpp src/cook/cook_textures.cpp
    src/cook/sexp_read.cpp src/cook/read_manifest.cpp src/cook/read_gltf.cpp src/cook/read_motion.cpp
    src/cook/read_tables.cpp src/cook/read_fighters.cpp src/cook/cook_pc_art.cpp src/cook/data_hash.cpp)
target_include_directories(phantomcrash_cook PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(phantomcrash_cook PUBLIC gsexp::gsexp SDL3::SDL3 SDL3_image::SDL3_image
    PRIVATE phantomcrash_cgltf phantomcrash_bc)
if(NOT MSVC)
    target_compile_options(phantomcrash_cook PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(pc_cook src/cook/cook_main.cpp)
target_link_libraries(pc_cook PRIVATE phantomcrash_cook)
target_compile_definitions(pc_cook PRIVATE PHANTOMCRASH_DATA_DIR="${PHANTOMCRASH_DATA_DIR}"
    PHANTOMCRASH_COOKED_DIR="${PHANTOMCRASH_COOKED_DIR}")

target_link_libraries(phantomcrash PRIVATE phantomcrash_cook)
target_compile_definitions(phantomcrash PRIVATE PHANTOMCRASH_COOKED_DIR="${PHANTOMCRASH_COOKED_DIR}")
