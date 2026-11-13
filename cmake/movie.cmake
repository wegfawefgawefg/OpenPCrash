# The movie player (src/movie, docs/movies.md) as a library of its own, on pl_mpeg (vendored in
# third_party/pl_mpeg, MIT) and the sound engine, linked into the game; and pc_movie_check, which
# decodes the movies offline and runs the movie screens without a window or a sound device.
add_library(phantomcrash_pl_mpeg STATIC third_party/pl_mpeg/pl_mpeg.c)
target_include_directories(phantomcrash_pl_mpeg SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pl_mpeg")

add_library(phantomcrash_movie STATIC
    src/movie/movie_decode.cpp src/movie/movie_player.cpp src/movie/movie_screens.cpp)
target_link_libraries(phantomcrash_movie PUBLIC phantomcrash_audio PRIVATE phantomcrash_pl_mpeg gsexp::gsexp)
target_include_directories(phantomcrash_movie PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(NOT MSVC)
    target_compile_options(phantomcrash_movie PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(phantomcrash PRIVATE phantomcrash_movie)

add_executable(pc_movie_check tools/movie_check/main.cpp tools/movie_check/screens_run.cpp)
target_link_libraries(pc_movie_check PRIVATE phantomcrash_movie SDL3::SDL3 SDL3_image::SDL3_image)
target_compile_definitions(pc_movie_check PRIVATE PHANTOMCRASH_DATA_DIR_DEFAULT="${PHANTOMCRASH_DATA_DIR}")
if(NOT MSVC)
    target_compile_options(pc_movie_check PRIVATE -Wall -Wextra -Wpedantic)
endif()
