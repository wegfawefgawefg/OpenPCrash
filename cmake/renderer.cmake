# The Vulkan renderer (src/render) and its shaders, on Mans In My House's cmake/renderer.cmake
if(MINGW)
    include(cmake/mingw.cmake) # the headers and import library from SDL's Khronos copy
endif()
find_package(Vulkan REQUIRED)
# glslangValidator (apt glslang-tools, brew glslang) or glslc (brew shaderc); the Vulkan SDK ships
# both
find_program(GLSLANG_VALIDATOR glslangValidator HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin")
find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin")
if(GLSLANG_VALIDATOR)
    set(PHANTOMCRASH_SHADER_COMPILER "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.1)
elseif(GLSLC)
    set(PHANTOMCRASH_SHADER_COMPILER "${GLSLC}" --target-env=vulkan1.1)
else()
    message(FATAL_ERROR "No GLSL compiler: install glslangValidator (glslang) or glslc (shaderc)")
endif()
add_library(phantomcrash_renderer STATIC
    src/render/vk_device.cpp src/render/vk_target.cpp src/render/vk_pipeline.cpp src/render/vk_resources.cpp
    src/render/vk_frame.cpp src/render/vk_composite.cpp src/render/vk_sprites.cpp
    src/render/vk_sprite_pipeline.cpp
    src/render/vk_backdrop.cpp src/render/vk_stream.cpp src/perf_log.cpp)
target_link_libraries(phantomcrash_renderer PUBLIC SDL3::SDL3 SDL3_image::SDL3_image Vulkan::Vulkan)
target_include_directories(phantomcrash_renderer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(phantomcrash_renderer PRIVATE PHANTOMCRASH_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")
if(NOT MSVC)
    # Vulkan structs are started from their sType alone
    target_compile_options(phantomcrash_renderer PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()
# the original's programs (assets/shaders/original, docs/shaders.md) and the overlay's sprite pair;
# every original program depends on the shared includes
file(GLOB PHANTOMCRASH_ORIGINAL_SHADERS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/original"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/original/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/original/*.frag")
file(GLOB PHANTOMCRASH_SHADER_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/original/include/*.glsl")
foreach(shader ${PHANTOMCRASH_ORIGINAL_SHADERS})
    set(output "${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader}.spv")
    add_custom_command(OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND ${PHANTOMCRASH_SHADER_COMPILER} "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/original/${shader}" -o "${output}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/original/${shader}" ${PHANTOMCRASH_SHADER_INCLUDES} VERBATIM)
    list(APPEND PHANTOMCRASH_SHADER_OUTPUTS "${output}")
endforeach()
foreach(shader sprite.vert sprite.frag)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader}.spv")
    add_custom_command(OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND ${PHANTOMCRASH_SHADER_COMPILER} "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/${shader}" -o "${output}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/${shader}" VERBATIM)
    list(APPEND PHANTOMCRASH_SHADER_OUTPUTS "${output}")
endforeach()
add_custom_target(phantomcrash_shaders DEPENDS ${PHANTOMCRASH_SHADER_OUTPUTS})
add_dependencies(phantomcrash_renderer phantomcrash_shaders)
target_link_libraries(phantomcrash PRIVATE phantomcrash_renderer)
