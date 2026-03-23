#include "src/imread.hpp"
#include "src/imsave.hpp"

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
    png_imsave(new_path, a.buffer.data(), a.width, a.height, a.channels, a.bit_depth);
    return 0;
}