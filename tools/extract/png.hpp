#pragma once
// RGBA8 images to PNG files (SDL3_image), for every family that writes pictures.
#include <cstdint>
#include <string>
#include <vector>

bool write_png(const std::string& path, int width, int height,
               const std::vector<std::uint8_t>& rgba);
