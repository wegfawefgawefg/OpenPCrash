# Windows built with MinGW (llvm-mingw, cross-compiled from Linux by scripts/release.sh;
# cmake/mingw-w64.cmake is the toolchain), as Mans In My House's cmake/mingw.cmake. What MSVC users
# get from the Vulkan SDK comes from SDL's own copy of the Khronos headers, and the Vulkan loader's
# import library is written from them. Included by cmake/renderer.cmake before it looks for Vulkan.

FetchContent_GetProperties(SDL3)
set(mingw_khronos "${sdl3_SOURCE_DIR}/src/video/khronos")
if(NOT EXISTS "${mingw_khronos}/vulkan/vulkan_core.h")
    message(FATAL_ERROR "No Khronos headers in SDL's sources (${mingw_khronos})")
endif()

# vulkan-1.dll's import library, from every vk function the headers declare. The loader is never
# bundled: it comes with the GPU drivers.
set(mingw_vulkan_def "${CMAKE_CURRENT_BINARY_DIR}/vulkan-1.def")
set(mingw_vulkan_lib "${CMAKE_CURRENT_BINARY_DIR}/libvulkan-1.a")
if(NOT EXISTS "${mingw_vulkan_lib}")
    set(def "LIBRARY vulkan-1.dll\nEXPORTS\n")
    foreach(header vulkan_core.h vulkan_win32.h)
        file(STRINGS "${mingw_khronos}/vulkan/${header}" lines REGEX "^VKAPI_ATTR .* VKAPI_CALL vk[A-Za-z0-9]+\\(")
        foreach(line IN LISTS lines)
            string(REGEX MATCH "vk[A-Za-z0-9]+\\(" name "${line}")
            string(REPLACE "(" "" name "${name}")
            string(APPEND def "    ${name}\n")
        endforeach()
    endforeach()
    file(WRITE "${mingw_vulkan_def}" "${def}")
    execute_process(COMMAND "${CMAKE_DLLTOOL}" -m i386:x86-64 -d "${mingw_vulkan_def}"
        -D vulkan-1.dll -l "${mingw_vulkan_lib}" RESULT_VARIABLE dlltool_failed)
    if(dlltool_failed)
        message(FATAL_ERROR "llvm-dlltool could not write ${mingw_vulkan_lib}")
    endif()
endif()
set(Vulkan_INCLUDE_DIR "${mingw_khronos}" CACHE PATH "" FORCE)
set(Vulkan_LIBRARY "${mingw_vulkan_lib}" CACHE FILEPATH "" FORCE)

# vulkan-1.dll is only there with Vulkan drivers, so it is loaded late, on the first Vulkan call,
# after main has looked for it (vulkan_loader_present): a machine without it gets the game's own
# plain message instead of the system's missing-DLL box (as Mans In My House's cmake/mingw.cmake)
target_link_options(phantomcrash PRIVATE -Wl,--delayload=vulkan-1.dll)
target_link_libraries(phantomcrash PRIVATE delayimp)

# one executable needing nothing but Windows: libc++ and libunwind linked in (every library is
# static on Windows, platform.cmake)
target_link_options(phantomcrash PRIVATE -static)
