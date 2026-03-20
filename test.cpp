#include "src/imread.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// test images name:
// rover
// gray16
// grayscale_4bit
// 4bit3
// palette_4bit
// 10.4-MB


int main(void) {
    const char path[] = "test_images/rover.png";
    const char new_path[] = "test_images/new.png";
    Image a = png_imread(path);
    stbi_write_png(new_path, a.width, a.height, a.channels, a.buffer.data(), a.width * a.channels);
    return 0;
}