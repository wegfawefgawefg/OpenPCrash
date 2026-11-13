# The sound engine (src/audio, docs/audio.md) as a library of its own, linked into the game, and
# pc_audio_render, which plays sounds and music through it offline into a WAV and prints levels.
add_library(phantomcrash_audio STATIC
    src/audio/audio_banks.cpp src/audio/audio_device.cpp src/audio/audio_follow.cpp src/audio/audio_mix.cpp
    src/audio/audio_commands.cpp src/audio/audio_sounds.cpp src/audio/audio_streams.cpp src/audio/reverb.cpp src/audio/wav.cpp)
target_link_libraries(phantomcrash_audio PUBLIC SDL3::SDL3 PRIVATE gsexp::gsexp)
target_include_directories(phantomcrash_audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(NOT MSVC)
    target_compile_options(phantomcrash_audio PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(phantomcrash PRIVATE phantomcrash_audio)

add_executable(pc_audio_render tools/audio_render/main.cpp)
target_link_libraries(pc_audio_render PRIVATE phantomcrash_audio SDL3::SDL3)
if(NOT MSVC)
    target_compile_options(pc_audio_render PRIVATE -Wall -Wextra -Wpedantic)
endif()
