#pragma once
#include <cstdint>
#include <string>
#include <vector>

// the Image
// remember to check the Image state if imread have success or not
struct Image {
    std::vector<uint8_t> buffer; // data of the image
    uint32_t height;
    uint32_t width;
    uint32_t channels;
    uint8_t bit_depth;
    int state; // 0 = success
               // 1 = ifstream failure ( file is not open, false file size )
               // 2 = invalid file format ( file does not follow the PNG specification or Interlaced 1)
               // 3 = inflate failure ( zlib-ng inflate failure )
}; // the image buffer is an object declare from the sruct so it BELONGS to the STRUCT <=> clear ownership



Image png_imread(const std::string path, const int cvtbitdepth = 8) noexcept;