# Bug reports (src/report, docs/release.md "Bug reports"): the consent, the crash handler
# (Windows' or POSIX's), the watchdog, the fight's record and the worker that sends them over HTTPS
# with libcurl, as Mans In My House does on its three builds: the system's libcurl on Linux (a
# release links libcurl.so.4, scripts/release.sh), the macOS SDK's on the Mac, and a pinned static
# curl over Schannel on Windows (cmake/curl_windows.cmake).
#
# Where they go is PHANTOMCRASH_REPORT_URL, empty in the public source: with none, the reports are
# compiled in but the question and the menu items are hidden and nothing is sent. scripts/release.sh
# reads it from ~/.config/openpcrash/report_url when that file is there (docs/release.md).
set(PHANTOMCRASH_REPORT_URL "" CACHE STRING
    "Where bug reports go (https://.../openpcrash-reports); empty: none, nothing shown or sent")
if(NOT PHANTOMCRASH_REPORT_URL MATCHES "^(https?://[A-Za-z0-9._:/-]+)?$")
    message(FATAL_ERROR "PHANTOMCRASH_REPORT_URL is an http(s) address or empty")
endif()

target_sources(phantomcrash PRIVATE
    src/report/reports.cpp src/report/report_send.cpp src/report/report_http.cpp
    src/report/report_shot.cpp src/report/report_crash.cpp src/report/report_watchdog.cpp
    src/report/report_fight.cpp
    src/screens/pc/report_panel.cpp src/screens/pc/report_panel_draw.cpp)
if(WIN32)
    target_sources(phantomcrash PRIVATE src/report/report_crash_windows.cpp)
else()
    target_sources(phantomcrash PRIVATE src/report/report_crash_posix.cpp)
    # dladdr and backtrace: in libc on new glibc, libdl on old
    target_link_libraries(phantomcrash PRIVATE ${CMAKE_DL_LIBS})
endif()
# the address in one file, so a new one rebuilds only that
set_property(SOURCE src/report/reports.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    PHANTOMCRASH_REPORT_URL="${PHANTOMCRASH_REPORT_URL}")

# LZ4 (third_party/lz4, BSD 2-Clause) packs the fight's record; pc_sim reads it (cmake/sim.cmake)
if(NOT TARGET phantomcrash_lz4)
    add_library(phantomcrash_lz4 STATIC "${CMAKE_CURRENT_LIST_DIR}/../third_party/lz4/lz4.c")
    target_include_directories(phantomcrash_lz4 SYSTEM PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/../third_party/lz4")
endif()
target_link_libraries(phantomcrash PRIVATE phantomcrash_lz4)

# without curl the rest still builds: the reports say they can't go
find_package(CURL QUIET)
if(WIN32 AND NOT CURL_FOUND)
    include(cmake/curl_windows.cmake)
    target_link_libraries(phantomcrash PRIVATE CURL::libcurl)
elseif(CURL_FOUND)
    target_link_libraries(phantomcrash PRIVATE CURL::libcurl)
else()
    message(STATUS "libcurl not found: bug reports can't be sent from this build")
    target_compile_definitions(phantomcrash PRIVATE PHANTOMCRASH_NO_CURL=1)
endif()
