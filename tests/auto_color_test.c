#include "../src/auto_color.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void set_pixel(uint8_t* pixels, size_t width, size_t x, size_t y,
                      uint8_t red, uint8_t green, uint8_t blue) {
  uint8_t* pixel = pixels + (y * width + x) * 4;
  pixel[0] = red;
  pixel[1] = green;
  pixel[2] = blue;
  pixel[3] = 255;
}

int main(void) {
  enum { WIDTH = 20, HEIGHT = 20 };
  uint8_t pixels[WIDTH * HEIGHT * 4];
  memset(pixels, 0, sizeof(pixels));

  // Match the production inset/band and make blue the dominant edge color.
  for (size_t y = 3; y < HEIGHT - 3; ++y) {
    for (size_t x = 0; x < 2; ++x) {
      set_pixel(pixels, WIDTH, 3 + x, y, 32, 64, 224);
      set_pixel(pixels, WIDTH, WIDTH - 5 + x, y, 32, 64, 224);
    }
  }
  for (size_t x = 5; x < WIDTH - 5; ++x) {
    for (size_t y = 0; y < 2; ++y) {
      set_pixel(pixels, WIDTH, x, 3 + y, 224, 32, 32);
      set_pixel(pixels, WIDTH, x, HEIGHT - 5 + y, 32, 64, 224);
    }
  }

  uint32_t color = 0;
  assert(auto_color_invert(0xff000000) == 0xffffffff);
  assert(auto_color_invert(0x7fffffff) == 0x7f000000);
  assert(auto_color_invert(0xff123456) == 0xffedcba9);
  assert(auto_color_sample_rgba(pixels, WIDTH, HEIGHT, WIDTH * 4,
                                3, 2, &color));
  assert(color == 0x2040e0);

  // Chrome-like fixture: a colored theme occupies the top edge while neutral
  // application chrome dominates the other three sides.
  for (size_t y = 0; y < HEIGHT; ++y) {
    for (size_t x = 0; x < WIDTH; ++x) {
      set_pixel(pixels, WIDTH, x, y, 240, 238, 232);
    }
  }
  for (size_t y = 3; y < 5; ++y) {
    for (size_t x = 3; x < WIDTH - 3; ++x) {
      set_pixel(pixels, WIDTH, x, y, 128, 48, 48);
    }
  }
  assert(auto_color_sample_header_rgba(pixels, WIDTH, HEIGHT, WIDTH * 4,
                                       &color));
  assert(color == 0x803030);
  assert(!auto_color_sample_rgba(NULL, WIDTH, HEIGHT, WIDTH * 4,
                                 3, 2, &color));
  assert(!auto_color_sample_rgba(pixels, 4, 4, WIDTH * 4,
                                 3, 2, &color));
  puts("auto_color_test: ok");
  return 0;
}
