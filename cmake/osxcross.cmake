# Cross-compiling for macOS on Apple Silicon (arm64) from Linux with osxcross
# (https://github.com/tpoechtrager/osxcross, its "llvm" flavor: the host's
# Clang, ld64.lld and LLVM tools, with Apple's SDK). scripts/release-mac.sh
# builds it into build-release/tools/osxcross/ once the SDK is there. Pass
# -DCMAKE_TOOLCHAIN_FILE=cmake/osxcross.cmake, and OSXCROSS_ROOT (cache or
# environment) to use another copy. See docs/release.md, "The Mac build".
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR arm64)

if(NOT OSXCROSS_ROOT AND DEFINED ENV{OSXCROSS_ROOT})
    set(OSXCROSS_ROOT "$ENV{OSXCROSS_ROOT}")
endif()
if(NOT OSXCROSS_ROOT)
    set(OSXCROSS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../build-release/tools/osxcross")
endif()
get_filename_component(OSXCROSS_ROOT "${OSXCROSS_ROOT}" ABSOLUTE)
set(OSXCROSS_ROOT "${OSXCROSS_ROOT}" CACHE PATH "osxcross checkout, built")

# the wrappers are named for the SDK's Darwin version: arm64-apple-darwin24.5-clang
file(GLOB osxcross_compilers "${OSXCROSS_ROOT}/target/bin/arm64-apple-darwin*-clang++")
list(FILTER osxcross_compilers EXCLUDE REGEX "clang\\+\\+-")
file(GLOB osxcross_sdks "${OSXCROSS_ROOT}/target/SDK/MacOSX*.sdk")
if(NOT osxcross_compilers OR NOT osxcross_sdks)
    message(FATAL_ERROR "No osxcross in ${OSXCROSS_ROOT}/target: scripts/release.sh --only mac builds it "
        "once the macOS SDK is in ${OSXCROSS_ROOT}/tarballs/ (docs/release.md, \"The Mac build\")")
endif()
list(GET osxcross_compilers 0 osxcross_cxx)
list(GET osxcross_sdks 0 osxcross_sdk)
string(REGEX REPLACE "-clang\\+\\+$" "" osxcross_prefix "${osxcross_cxx}")
# the Darwin version the SDK is for, and osxcross' sw_vers on the path:
# CMake asks it for the macOS version, and without one (past 10.5) it makes
# no -rpath or @rpath install names, so libraries are named by the paths
# they were built at
string(REGEX MATCH "darwin([0-9.]+)$" osxcross_darwin "${osxcross_prefix}")
set(CMAKE_SYSTEM_VERSION "${CMAKE_MATCH_1}")
set(ENV{PATH} "${OSXCROSS_ROOT}/target/bin:$ENV{PATH}")

set(CMAKE_C_COMPILER "${osxcross_prefix}-clang")
set(CMAKE_CXX_COMPILER "${osxcross_prefix}-clang++")
# SDL's Cocoa code is Objective-C
set(CMAKE_OBJC_COMPILER "${osxcross_prefix}-clang")
set(CMAKE_OBJCXX_COMPILER "${osxcross_prefix}-clang++")
set(CMAKE_AR "${osxcross_prefix}-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${osxcross_prefix}-ranlib" CACHE FILEPATH "")
set(CMAKE_INSTALL_NAME_TOOL "${osxcross_prefix}-install_name_tool" CACHE FILEPATH "")
set(CMAKE_OSX_SYSROOT "${osxcross_sdk}" CACHE PATH "")
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "")
# the first macOS for Apple Silicon; MoltenVK itself wants 12, and on 11 the
# game draws with OpenGL instead
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "")

# libraries and headers from the SDK only; programs (glslang, SPIRV-Cross)
# from the host
set(CMAKE_FIND_ROOT_PATH "${osxcross_sdk}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# frameworks (Cocoa, Metal) from the SDK too
set(CMAKE_FIND_FRAMEWORK FIRST)
# and none of the host's pkg-config packages
set(ENV{PKG_CONFIG_LIBDIR} "${osxcross_sdk}/usr/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
