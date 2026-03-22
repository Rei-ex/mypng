#include "src/imread.hpp"
#include "src/imsave.hpp"
#include <cstdio>

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
    Image a = png_imread(path, 16);
    printf("%d\t%d\t%d\t%d\n", a.width, a.height, a.channels, a.bit_depth);
    int b = png_imsave(new_path, a.buffer.data(), a.width, a.height, a.channels, a.bit_depth);
    printf("%d\n", b);
    Image c = png_imread(new_path);
    printf("%d\n", c.state);
    return 0;
}