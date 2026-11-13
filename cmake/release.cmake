# The game's version and a release's stamp (scripts/release.sh, docs/release.md). Every build knows
# its version (src/build_info.hpp); a release is stamped too with its commit and day.
# PHANTOMCRASH_RELEASE (cmake/platform.cmake) reads the game's own files from beside the executable
# and keeps the data made from the player's disc in the per-user folder (src/game_paths.hpp).

# the game's version: bump it by hand
set(PHANTOMCRASH_GAME_VERSION 0.2.0)
set(PHANTOMCRASH_COMMIT "" CACHE STRING "The commit a release is built from")
set(PHANTOMCRASH_BUILD_DATE "" CACHE STRING "The day a release is built")
foreach(stamp PHANTOMCRASH_COMMIT PHANTOMCRASH_BUILD_DATE)
    if(NOT ${stamp} MATCHES "^[A-Za-z0-9._+-]*$")
        message(FATAL_ERROR "${stamp} takes letters, digits and ._+- only")
    endif()
endforeach()
# written only when it changes, so a new stamp rebuilds build_info.cpp alone
set(stamp_header "")
foreach(stamp PHANTOMCRASH_GAME_VERSION PHANTOMCRASH_COMMIT PHANTOMCRASH_BUILD_DATE)
    string(APPEND stamp_header "#define ${stamp} \"${${stamp}}\"\n")
endforeach()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/stamp/build_stamp.h" CONTENT "${stamp_header}")
target_sources(phantomcrash PRIVATE src/build_info.cpp)
target_include_directories(phantomcrash PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/stamp")

if(WIN32)
    # the version in the file's properties, and a manifest: UTF-8 as the code page, so a path
    # through a user folder named in any language opens with the plain char APIs
    enable_language(RC)
    target_sources(phantomcrash PRIVATE src/windows/phantomcrash.rc)
    set_property(SOURCE src/windows/phantomcrash.rc APPEND PROPERTY OBJECT_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/windows/phantomcrash.manifest"
        "${CMAKE_CURRENT_BINARY_DIR}/stamp/build_stamp.h")
    # 8 MB of stack for the main thread and every thread it starts, as Linux gives them
    if(MSVC)
        target_link_options(phantomcrash PRIVATE /STACK:8388608)
    else()
        target_link_options(phantomcrash PRIVATE -Wl,--stack,8388608)
    endif()
endif()

if(PHANTOMCRASH_RELEASE)
    # the checkout's and the build folder's paths out, the bundle's folders in
    get_target_property(release_definitions phantomcrash COMPILE_DEFINITIONS)
    list(FILTER release_definitions EXCLUDE REGEX "^PHANTOMCRASH_(ASSET|DATA|COOKED)_DIR=")
    list(APPEND release_definitions PHANTOMCRASH_RELEASE=1 PHANTOMCRASH_ASSET_DIR="assets"
         PHANTOMCRASH_DATA_DIR="data" PHANTOMCRASH_COOKED_DIR="cooked")
    set_property(TARGET phantomcrash PROPERTY COMPILE_DEFINITIONS ${release_definitions})
    get_target_property(release_definitions phantomcrash_renderer COMPILE_DEFINITIONS)
    list(FILTER release_definitions EXCLUDE REGEX "^PHANTOMCRASH_SHADER_DIR=")
    list(APPEND release_definitions PHANTOMCRASH_RELEASE=1 PHANTOMCRASH_SHADER_DIR="shaders")
    set_property(TARGET phantomcrash_renderer PROPERTY COMPILE_DEFINITIONS ${release_definitions})
    if(MINGW)
        # no console window behind the game's
        target_link_options(phantomcrash PRIVATE -mwindows)
    endif()
endif()
