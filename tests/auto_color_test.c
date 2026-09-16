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

  struct auto_colors gradient_colors = {
    .top_left = 0xff000000,
    .top_right = 0xffff0000,
    .bottom_left = 0xff00ff00,
    .bottom_right = 0xff0000ff
  };
  CGImageRef gradient = auto_color_create_bilinear_gradient(gradient_colors);
  assert(gradient);
  CFDataRef gradient_data = CGDataProviderCopyData(CGImageGetDataProvider(gradient));
  const uint8_t* gradient_pixels = CFDataGetBytePtr(gradient_data);
  size_t gradient_row = CGImageGetBytesPerRow(gradient);
  assert(gradient_pixels[0] == 0 && gradient_pixels[1] == 0
         && gradient_pixels[2] == 0 && gradient_pixels[3] == 255);
  const uint8_t* top_right = gradient_pixels + (63 * 4);
  assert(top_right[0] == 255 && top_right[1] == 0 && top_right[2] == 0);
  const uint8_t* bottom_left = gradient_pixels + (63 * gradient_row);
  assert(bottom_left[0] == 0 && bottom_left[1] == 255 && bottom_left[2] == 0);
  CFRelease(gradient_data);
  CGImageRelease(gradient);
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

  enum { ACCENT_WIDTH = 80, ACCENT_HEIGHT = 40 };
  uint8_t accent_pixels[ACCENT_WIDTH * ACCENT_HEIGHT * 4];
  for (size_t y = 0; y < ACCENT_HEIGHT; ++y) {
    for (size_t x = 0; x < ACCENT_WIDTH; ++x) {
      set_pixel(accent_pixels, ACCENT_WIDTH, x, y, 20, 24, 28);
    }
  }
  // A Dia-like two-pixel frame spans most of the top and should replace the
  // dark upper-corner samples without changing the lower corners.
  for (size_t y = 8; y < 10; ++y) {
    for (size_t x = 8; x < ACCENT_WIDTH - 8; ++x) {
      set_pixel(accent_pixels, ACCENT_WIDTH, x, y, 142, 174, 194);
    }
  }
  struct auto_colors colors;
  assert(auto_color_sample_corners_rgba(accent_pixels,
                                        ACCENT_WIDTH, ACCENT_HEIGHT,
                                        ACCENT_WIDTH * 4, &colors));
  assert(colors.top_left == 0x8eaec2);
  assert(colors.top_right == 0x8eaec2);
  assert(colors.bottom_left == 0x14181c);
  assert(colors.bottom_right == 0x14181c);

  // A short control or decoration must not be mistaken for a window-wide
  // accent line.
  for (size_t y = 8; y < 10; ++y) {
    for (size_t x = 8; x < ACCENT_WIDTH - 8; ++x) {
      set_pixel(accent_pixels, ACCENT_WIDTH, x, y, 20, 24, 28);
    }
    for (size_t x = 20; x < 38; ++x) {
      set_pixel(accent_pixels, ACCENT_WIDTH, x, y, 142, 174, 194);
    }
  }
  assert(auto_color_sample_corners_rgba(accent_pixels,
                                        ACCENT_WIDTH, ACCENT_HEIGHT,
                                        ACCENT_WIDTH * 4, &colors));
  assert(colors.top_left == 0x14181c);
  assert(colors.top_right == 0x14181c);
  puts("auto_color_test: ok");
  return 0;
}
