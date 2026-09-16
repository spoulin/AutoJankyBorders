#pragma once
#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>
#include <stdint.h>

struct auto_colors {
  uint32_t top_left;
  uint32_t top_right;
  uint32_t bottom_left;
  uint32_t bottom_right;
};

// Returns an opaque RGB sample from a narrow band just inside each image edge.
// Pixels must be RGBA8888 with an explicit row stride.
bool auto_color_sample_rgba(const uint8_t* pixels,
                            size_t width,
                            size_t height,
                            size_t bytes_per_row,
                            size_t inset,
                            size_t band_width,
                            uint32_t* color);

bool auto_color_sample_header_rgba(const uint8_t* pixels,
                                   size_t width,
                                   size_t height,
                                   size_t bytes_per_row,
                                   uint32_t* color);

bool auto_color_sample_window(uint32_t window_id,
                              uint8_t alpha,
                              uint32_t* color);

bool auto_color_sample_window_corners(uint32_t window_id,
                                      uint8_t alpha,
                                      struct auto_colors* colors);

CGImageRef auto_color_create_bilinear_gradient(struct auto_colors colors);

uint32_t auto_color_invert(uint32_t color);
