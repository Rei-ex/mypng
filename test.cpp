#include "src/imread.h"
#include <string>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
// #include <iostream>

// test images name:
// rover
// gray16
// grayscale_4bit
// 4bit3
// palette_4bit
// 10.4-MB


int main(void) {
    const char path[] = "test_images/gray16.png";
    const char new_path[] = "test_images/new.png";
    Image a = png_imread(path);
    int b = stbi_write_png(new_path, a.width, a.height, a.channels, a.buffer.data(), a.width * a.channels);
    // std::cout << b << std::endl;
    return 0;
}