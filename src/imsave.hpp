#pragma once
#include <cstdint>
#include <string>

// not support interlace and color type 3 imsave

// return -1, wrong input
// return  1, file is not open
int png_imsave(const std::string path, void *data, const uint32_t width, const uint32_t height, const uint32_t channels, const uint8_t bit_depth = 8) noexcept;