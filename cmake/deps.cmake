# The owner's libraries: Gubsy (SDL host, input, settings, saves), GView and GLayout (menus), gfxp
# (fixed point) and gsexp (s-expression data). Pinned like Mans In My House's cmake/deps.cmake, at
# the same commits it builds with. A machine with the siblings checked out beside this repository
# (../gubsy, ../gview, ../glayout, ../gfxp, ../gsexp) can build against those instead with
# -DPHANTOMCRASH_USE_LOCAL_DEPS=ON (to work on a library and the game together). Every pin must be
# a commit pushed to its GitHub remote, or fresh clones fail to check it out.
#
# The gubsy and gview pins get MIMH's two fixes for building on Windows as they are fetched
# (cmake/patches/README.txt).
set(PHANTOMCRASH_GUBSY_REV c4d5408c74e61914f3886099db799919a2637696)
set(PHANTOMCRASH_GVIEW_REV 71956a3f3884a7e542b65cadd413637b9b3cff0e)
set(PHANTOMCRASH_GLAYOUT_REV 50861b524e58195f937e48984b33c1ca65e9631d)
set(PHANTOMCRASH_GFXP_REV 9b52499e5cf32bffdc1ce5b2f5e946646840e085)
set(PHANTOMCRASH_GSEXP_REV a095211f2ea61ec3824fdd783f6b8ad5fe099d13)
set(PHANTOMCRASH_GUBSY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../gubsy" CACHE PATH "Local Gubsy checkout")
set(PHANTOMCRASH_GVIEW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../gview" CACHE PATH "Local GView checkout")
set(PHANTOMCRASH_GLAYOUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../glayout" CACHE PATH "Local GLayout checkout")
set(PHANTOMCRASH_GFXP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../gfxp" CACHE PATH "Local gfxp checkout")
set(PHANTOMCRASH_GSEXP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../gsexp" CACHE PATH "Local gsexp checkout")
set(local_sources "${PHANTOMCRASH_GUBSY_DIR}" "${PHANTOMCRASH_GVIEW_DIR}" "${PHANTOMCRASH_GLAYOUT_DIR}"
    "${PHANTOMCRASH_GFXP_DIR}" "${PHANTOMCRASH_GSEXP_DIR}")
# the pinned commits by default, even with the siblings checked out beside us (they move on; the
# pins are what the game is known to build with)
option(PHANTOMCRASH_USE_LOCAL_DEPS "Build against local sibling checkouts instead of the pinned commits" OFF)

set(GFXP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GFXP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GFXP_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(GSEXP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GSEXP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GUB_MODE consumer CACHE STRING "" FORCE)
set(GUB_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
set(GUB_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(GUB_ENABLE_LUA_MOD_HOST OFF CACHE BOOL "" FORCE)
set(GUB_ENABLE_STEAM OFF CACHE BOOL "" FORCE)
set(GUB_FETCH_DEPS ON CACHE BOOL "" FORCE)
set(GUB_WARN_AS_ERROR OFF CACHE BOOL "" FORCE) # its own -Werror trips on clang (Windows, the Mac)
set(GLAYOUT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLAYOUT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLAYOUT_BUILD_SDL_DEMO OFF CACHE BOOL "" FORCE)
set(GVIEW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GVIEW_BUILD_AUTHORING OFF CACHE BOOL "" FORCE)
set(GVIEW_WITH_SDL3_RENDERER ON CACHE BOOL "" FORCE)
set(GVIEW_SDL3_TARGET SDL3::SDL3 CACHE STRING "" FORCE)

# SDL itself, release-3.4.18 as in MIMH, declared here first (FetchContent keeps the first
# declaration; Gubsy's own is then passed over)
set(phantomcrash_patches "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches")
set(phantomcrash_apply -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply_patch.cmake")
FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG 829a65d769d935c4852f8159e964312c0957260a)
if(PHANTOMCRASH_USE_LOCAL_DEPS)
    foreach(source IN LISTS local_sources)
        if(NOT EXISTS "${source}/CMakeLists.txt")
            message(FATAL_ERROR "Missing local dependency: ${source}. "
                "Configure with -DPHANTOMCRASH_USE_LOCAL_DEPS=OFF to fetch the pinned commits.")
        endif()
    endforeach()
    message(STATUS "Phantom Crash: using local sibling checkouts")
    add_subdirectory("${PHANTOMCRASH_GFXP_DIR}" gfxp)
    add_subdirectory("${PHANTOMCRASH_GSEXP_DIR}" gsexp)
    if(NOT PHANTOMCRASH_SIM_ONLY)
        add_subdirectory("${PHANTOMCRASH_GUBSY_DIR}" gubsy)
        add_subdirectory("${PHANTOMCRASH_GLAYOUT_DIR}" glayout)
        add_subdirectory("${PHANTOMCRASH_GVIEW_DIR}" gview)
    endif()
else()
    message(STATUS "Phantom Crash: fetching the pinned Gubsy/GView/GLayout/gfxp/gsexp commits")
    FetchContent_Declare(gfxp GIT_REPOSITORY https://github.com/wegfawefgawefg/gfxp.git GIT_TAG ${PHANTOMCRASH_GFXP_REV})
    FetchContent_Declare(gsexp GIT_REPOSITORY https://github.com/wegfawefgawefg/gsexp.git GIT_TAG ${PHANTOMCRASH_GSEXP_REV})
    FetchContent_MakeAvailable(gfxp gsexp)
    if(NOT PHANTOMCRASH_SIM_ONLY) # pc_sim alone needs only gfxp and gsexp (cmake/sim.cmake)
        FetchContent_Declare(gubsy GIT_REPOSITORY https://github.com/wegfawefgawefg/gubsy.git GIT_TAG ${PHANTOMCRASH_GUBSY_REV}
            PATCH_COMMAND "${CMAKE_COMMAND}" "-DPATCH=${phantomcrash_patches}/gubsy-windows.patch" ${phantomcrash_apply})
        FetchContent_MakeAvailable(gubsy)
        FetchContent_Declare(glayout GIT_REPOSITORY https://github.com/wegfawefgawefg/glayout.git GIT_TAG ${PHANTOMCRASH_GLAYOUT_REV})
        FetchContent_MakeAvailable(glayout)
        FetchContent_Declare(gview GIT_REPOSITORY https://github.com/wegfawefgawefg/gview.git GIT_TAG ${PHANTOMCRASH_GVIEW_REV}
            PATCH_COMMAND "${CMAKE_COMMAND}" "-DPATCH=${phantomcrash_patches}/gview-windows.patch" ${phantomcrash_apply})
        FetchContent_MakeAvailable(gview)
    endif()
endif()

# cgltf (glTF 2.0 reader, header only), v1.15, for the converted models in data/models
FetchContent_Declare(cgltf GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG 360db1a95480fe102ae9c69b27c5d101167ff5ba)
FetchContent_GetProperties(cgltf)
if(NOT cgltf_POPULATED)
    FetchContent_Populate(cgltf)
endif()
add_library(phantomcrash_cgltf INTERFACE)
target_include_directories(phantomcrash_cgltf SYSTEM INTERFACE "${cgltf_SOURCE_DIR}")
