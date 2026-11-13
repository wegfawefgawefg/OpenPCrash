# Cross-compiling for 64-bit Windows from Linux with llvm-mingw (Clang, lld,
# libc++, the UCRT): scripts/release.sh fetches a release into
# build-release/tools/. Pass -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake, and
# LLVM_MINGW_ROOT (cache or environment) to use another copy.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT LLVM_MINGW_ROOT AND DEFINED ENV{LLVM_MINGW_ROOT})
    set(LLVM_MINGW_ROOT "$ENV{LLVM_MINGW_ROOT}")
endif()
if(NOT LLVM_MINGW_ROOT)
    file(GLOB llvm_mingw_found "${CMAKE_CURRENT_LIST_DIR}/../build-release/tools/llvm-mingw-*")
    list(SORT llvm_mingw_found)
    list(POP_BACK llvm_mingw_found LLVM_MINGW_ROOT)
endif()
if(NOT EXISTS "${LLVM_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang++")
    message(FATAL_ERROR "No llvm-mingw: run scripts/release.sh once, or set LLVM_MINGW_ROOT")
endif()
set(LLVM_MINGW_ROOT "${LLVM_MINGW_ROOT}" CACHE PATH "llvm-mingw toolchain")

set(triple x86_64-w64-mingw32)
set(CMAKE_C_COMPILER "${LLVM_MINGW_ROOT}/bin/${triple}-clang")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW_ROOT}/bin/${triple}-clang++")
set(CMAKE_RC_COMPILER "${LLVM_MINGW_ROOT}/bin/${triple}-windres")
set(CMAKE_AR "${LLVM_MINGW_ROOT}/bin/llvm-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${LLVM_MINGW_ROOT}/bin/llvm-ranlib" CACHE FILEPATH "")
set(CMAKE_DLLTOOL "${LLVM_MINGW_ROOT}/bin/llvm-dlltool" CACHE FILEPATH "")

# libraries and headers from the toolchain only; programs (glslang,
# SPIRV-Cross) from the host
set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_ROOT}/${triple}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# and none of the host's pkg-config packages (Gubsy looks for glm that way)
set(ENV{PKG_CONFIG_LIBDIR} "${LLVM_MINGW_ROOT}/${triple}/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
