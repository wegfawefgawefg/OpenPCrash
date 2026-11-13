# The fixed-point maths of the deterministic world (src/fx, docs/numbers.md): a library the game
# links. Include after deps.cmake (it needs gfxp::gfxp), then
#   target_link_libraries(phantomcrash PRIVATE phantomcrash_fx)
# -DPHANTOMCRASH_FX_ACCURACY=ON also builds fx_accuracy, which prints src/fx's worst errors against
# double precision (the numbers in docs/numbers.md). It is a tool, not a test.
set(phantomcrash_fx_root "${CMAKE_CURRENT_LIST_DIR}/..")

if(NOT TARGET gfxp::gfxp)
    message(FATAL_ERROR "cmake/fx.cmake needs gfxp::gfxp: include cmake/deps.cmake first")
endif()

add_library(phantomcrash_fx STATIC "${phantomcrash_fx_root}/src/fx/hit.cpp")
target_include_directories(phantomcrash_fx PUBLIC "${phantomcrash_fx_root}/src")
target_link_libraries(phantomcrash_fx PUBLIC gfxp::gfxp)
target_compile_features(phantomcrash_fx PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(phantomcrash_fx PRIVATE /W3)
else()
    target_compile_options(phantomcrash_fx PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

option(PHANTOMCRASH_FX_ACCURACY "Build fx_accuracy (src/fx against double precision)" OFF)
if(PHANTOMCRASH_FX_ACCURACY)
    add_executable(fx_accuracy "${phantomcrash_fx_root}/tools/fx_accuracy.cpp")
    target_link_libraries(fx_accuracy PRIVATE phantomcrash_fx)
endif()
