# Desktop platform settings (Linux, macOS, Windows), set before any dependency is added (Mans In My
# House's cmake/platform.cmake).

# SDL's sub-projects cache BUILD_SHARED_LIBS=ON part way through the first configure, so a
# library could come out static on the first configure and shared on every later one. Decide it
# once.
set(BUILD_SHARED_LIBS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Windows has no symbol interposition and looks for DLLs beside the executable: there everything,
# SDL too, is linked into the one executable (cmake/mingw.cmake)
if(WIN32)
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()

# a build for players (scripts/release.sh, docs/release.md): the game's files beside the
# executable and its data in the per-user folder (src/game_paths.hpp)
option(PHANTOMCRASH_RELEASE "A release build: files beside the executable, data per user" OFF)
if(PHANTOMCRASH_RELEASE AND NOT MSVC)
    # no path of the machine it was built on in what ships (__FILE__, asserts, debug info): the
    # checkout becomes ".", the build folder (and every fetched dependency in it) "build". The
    # last match wins, so the build folder, inside the checkout, goes second
    add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
                        "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build")
endif()
if(PHANTOMCRASH_RELEASE)
    # Gubsy compiles its source folder in as its project root; a release has none (the game hands
    # it the per-user folder as its data root, src/main.cpp)
    set(GUB_RUNTIME_PROJECT_SOURCE_DIR "." CACHE STRING "Gubsy's project root" FORCE)
endif()

if(APPLE)
    # Homebrew's prefix, for the Vulkan loader, headers and glslang
    foreach(prefix /opt/homebrew /usr/local)
        if(EXISTS "${prefix}")
            list(APPEND CMAKE_PREFIX_PATH "${prefix}")
        endif()
    endforeach()
    # a release is an app bundle (scripts/release-mac.sh): the game and every library it loads
    # find each other in its Contents/Frameworks
    if(PHANTOMCRASH_RELEASE)
        set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
        set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks")
    endif()
endif()

if(MSVC)
    add_compile_options(/utf-8 "$<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus;/bigobj>")
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()
