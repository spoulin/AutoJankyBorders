#include "auto_color.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#define HISTOGRAM_SIZE 4096

uint32_t auto_color_invert(uint32_t color) {
  return (color & 0xff000000) | ((~color) & 0x00ffffff);
}

static void sample_pixel(const uint8_t* pixel,
                         uint32_t histogram[HISTOGRAM_SIZE],
                         uint64_t red[HISTOGRAM_SIZE],
                         uint64_t green[HISTOGRAM_SIZE],
                         uint64_t blue[HISTOGRAM_SIZE]) {
  if (pixel[3] < 16) return;
  uint32_t bucket = ((uint32_t)(pixel[0] >> 4) << 8)
                  | ((uint32_t)(pixel[1] >> 4) << 4)
                  |  (uint32_t)(pixel[2] >> 4);
  histogram[bucket]++;
  red[bucket] += pixel[0];
  green[bucket] += pixel[1];
  blue[bucket] += pixel[2];
}

struct sample_result {
  uint32_t color;
  uint32_t count;
  uint32_t total;
};

static bool sample_side(const uint8_t* pixels,
                        size_t width,
                        size_t height,
                        size_t bytes_per_row,
                        size_t inset,
                        size_t band_width,
                        int side,
                        struct sample_result* result) {
  uint32_t histogram[HISTOGRAM_SIZE] = {0};
  uint64_t red[HISTOGRAM_SIZE] = {0};
  uint64_t green[HISTOGRAM_SIZE] = {0};
  uint64_t blue[HISTOGRAM_SIZE] = {0};
  if (2 * (inset + band_width) > width
      || 2 * (inset + band_width) > height) return false;

  bool horizontal = side < 2;
  size_t length = horizontal ? width - 2 * inset : height - 2 * inset;
  for (size_t along = 0; along < length; ++along) {
    for (size_t across = 0; across < band_width; ++across) {
      size_t x;
      size_t y;
      if (side == 0) { // top
        x = inset + along;
        y = inset + across;
      } else if (side == 1) { // bottom
        x = inset + along;
        y = height - inset - band_width + across;
      } else if (side == 2) { // left
        x = inset + across;
        y = inset + along;
      } else { // right
        x = width - inset - band_width + across;
        y = inset + along;
      }
      sample_pixel(pixels + y * bytes_per_row + x * 4,
                   histogram, red, green, blue);
    }
  }

  uint32_t best = 0;
  for (uint32_t i = 1; i < HISTOGRAM_SIZE; ++i) {
    if (histogram[i] > histogram[best]) best = i;
  }
  if (!histogram[best]) return false;
  result->count = histogram[best];
  result->total = (uint32_t)(length * band_width);
  result->color = ((uint32_t)(red[best] / result->count) << 16)
                | ((uint32_t)(green[best] / result->count) << 8)
                |  (uint32_t)(blue[best] / result->count);
  return true;
}

static bool sample_rgba(const uint8_t* pixels,
                        size_t width,
                        size_t height,
                        size_t bytes_per_row,
                        size_t inset,
                        size_t band_width,
                        struct sample_result* result) {
  if (!pixels || !result || width == 0 || height == 0 || band_width == 0
      || 2 * (inset + band_width) > width
      || 2 * (inset + band_width) > height) return false;

  uint32_t histogram[HISTOGRAM_SIZE] = {0};
  uint64_t red[HISTOGRAM_SIZE] = {0};
  uint64_t green[HISTOGRAM_SIZE] = {0};
  uint64_t blue[HISTOGRAM_SIZE] = {0};

  size_t left = inset;
  size_t right = width - inset - band_width;
  size_t top = inset;
  size_t bottom = height - inset - band_width;
  for (size_t y = inset; y < height - inset; ++y) {
    for (size_t x = 0; x < band_width; ++x) {
      sample_pixel(pixels + y * bytes_per_row + (left + x) * 4,
                   histogram, red, green, blue);
      sample_pixel(pixels + y * bytes_per_row + (right + x) * 4,
                   histogram, red, green, blue);
    }
  }
  for (size_t x = inset + band_width; x < width - inset - band_width; ++x) {
    for (size_t y = 0; y < band_width; ++y) {
      sample_pixel(pixels + (top + y) * bytes_per_row + x * 4,
                   histogram, red, green, blue);
      sample_pixel(pixels + (bottom + y) * bytes_per_row + x * 4,
                   histogram, red, green, blue);
    }
  }

  uint32_t best = 0;
  for (uint32_t i = 1; i < HISTOGRAM_SIZE; ++i) {
    if (histogram[i] > histogram[best]) best = i;
  }
  if (!histogram[best]) return false;
  uint32_t count = histogram[best];
  result->color = ((uint32_t)(red[best] / count) << 16)
                | ((uint32_t)(green[best] / count) << 8)
                |  (uint32_t)(blue[best] / count);
  result->count = count;
  result->total = (uint32_t)(2 * band_width
                             * ((height - 2 * inset)
                                + (width - 2 * inset - 2 * band_width)));
  return true;
}

bool auto_color_sample_rgba(const uint8_t* pixels,
                            size_t width,
                            size_t height,
                            size_t bytes_per_row,
                            size_t inset,
                            size_t band_width,
                            uint32_t* color) {
  struct sample_result result;
  if (!color || !sample_rgba(pixels, width, height, bytes_per_row,
                             inset, band_width, &result)) return false;
  *color = result.color;
  return true;
}

bool auto_color_sample_header_rgba(const uint8_t* pixels,
                                   size_t width,
                                   size_t height,
                                   size_t bytes_per_row,
                                   uint32_t* color) {
  struct sample_result header;
  if (!color || !sample_side(pixels, width, height, bytes_per_row,
                             3, 2, 0, &header)) return false;
  *color = header.color;
  return true;
}

static bool sample_patch(const uint8_t* pixels,
                         size_t width,
                         size_t height,
                         size_t bytes_per_row,
                         size_t x_origin,
                         size_t y_origin,
                         size_t patch_size,
                         uint32_t* color) {
  uint32_t histogram[HISTOGRAM_SIZE] = {0};
  uint64_t red[HISTOGRAM_SIZE] = {0};
  uint64_t green[HISTOGRAM_SIZE] = {0};
  uint64_t blue[HISTOGRAM_SIZE] = {0};
  if (!pixels || !color || patch_size == 0
      || x_origin + patch_size > width
      || y_origin + patch_size > height) return false;
  for (size_t y = y_origin; y < y_origin + patch_size; ++y) {
    for (size_t x = x_origin; x < x_origin + patch_size; ++x) {
      sample_pixel(pixels + y * bytes_per_row + x * 4,
                   histogram, red, green, blue);
    }
  }
  uint32_t best = 0;
  for (uint32_t i = 1; i < HISTOGRAM_SIZE; ++i) {
    if (histogram[i] > histogram[best]) best = i;
  }
  if (!histogram[best]) return false;
  uint32_t count = histogram[best];
  *color = ((uint32_t)(red[best] / count) << 16)
         | ((uint32_t)(green[best] / count) << 8)
         |  (uint32_t)(blue[best] / count);
  return true;
}

static bool sample_corners_rgba(const uint8_t* pixels,
                                size_t width,
                                size_t height,
                                size_t bytes_per_row,
                                struct auto_colors* colors) {
  if (!colors || width < 16 || height < 16) return false;
  size_t minimum = width < height ? width : height;
  size_t patch = minimum / 10;
  if (patch < 8) patch = 8;
  if (patch > 64) patch = 64;
  size_t inset = 3;
  if (2 * (patch + inset) > width || 2 * (patch + inset) > height) return false;
  return sample_patch(pixels, width, height, bytes_per_row,
                      inset, inset, patch, &colors->top_left)
      && sample_patch(pixels, width, height, bytes_per_row,
                      width - inset - patch, inset, patch, &colors->top_right)
      && sample_patch(pixels, width, height, bytes_per_row,
                      inset, height - inset - patch, patch, &colors->bottom_left)
      && sample_patch(pixels, width, height, bytes_per_row,
                      width - inset - patch, height - inset - patch,
                      patch, &colors->bottom_right);
}

bool auto_color_sample_window(uint32_t window_id,
                              uint8_t alpha,
                              uint32_t* color) {
  static bool did_request_permission = false;
  static bool did_report_permission_error = false;
  if (!CGPreflightScreenCaptureAccess()) {
    if (!did_request_permission) {
      did_request_permission = true;
      CGRequestScreenCaptureAccess();
    }
    if (!CGPreflightScreenCaptureAccess()) {
      if (!did_report_permission_error) {
        fprintf(stderr,
                "[?] Borders: Screen Recording permission is required for "
                "color=auto; using default_color.\n");
        did_report_permission_error = true;
      }
      return false;
    }
  }

  typedef CGImageRef (*window_list_create_image_fn)(CGRect,
                                                     CGWindowListOption,
                                                     CGWindowID,
                                                     CGWindowImageOption);
  static window_list_create_image_fn create_image = NULL;
  static bool did_resolve = false;
  if (!did_resolve) {
    create_image = (window_list_create_image_fn)dlsym(RTLD_DEFAULT,
                                                       "CGWindowListCreateImage");
    did_resolve = true;
  }
  if (!create_image) {
    fprintf(stderr,
            "[?] Borders: Window capture is unavailable; using "
            "default_color.\n");
    return false;
  }

  CGImageRef image = create_image(CGRectNull,
                                  kCGWindowListOptionIncludingWindow,
                                  window_id,
                                  kCGWindowImageBoundsIgnoreFraming
                                  | kCGWindowImageBestResolution);
  if (!image) return false;

  size_t width = CGImageGetWidth(image);
  size_t height = CGImageGetHeight(image);
  size_t bytes_per_row = width * 4;
  uint8_t* pixels = calloc(height, bytes_per_row);
  CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
  CGContextRef context = pixels && color_space
                         ? CGBitmapContextCreate(pixels, width, height, 8,
                                                bytes_per_row, color_space,
                                                kCGImageAlphaPremultipliedLast
                                                | kCGBitmapByteOrder32Big)
                         : NULL;
  bool success = false;
  if (context) {
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
    uint32_t edge_rgb = 0;
    success = auto_color_sample_header_rgba(pixels, width, height,
                                            bytes_per_row, &edge_rgb);
    if (success) {
      *color = ((uint32_t)alpha << 24) | edge_rgb;
    }
  }

  if (context) CGContextRelease(context);
  if (color_space) CGColorSpaceRelease(color_space);
  free(pixels);
  CGImageRelease(image);
  return success;
}

bool auto_color_sample_window_corners(uint32_t window_id,
                                      uint8_t alpha,
                                      struct auto_colors* colors) {
  if (!colors || !CGPreflightScreenCaptureAccess()) return false;
  typedef CGImageRef (*window_list_create_image_fn)(CGRect,
                                                     CGWindowListOption,
                                                     CGWindowID,
                                                     CGWindowImageOption);
  static window_list_create_image_fn create_image = NULL;
  static bool did_resolve = false;
  if (!did_resolve) {
    create_image = (window_list_create_image_fn)dlsym(RTLD_DEFAULT,
                                                       "CGWindowListCreateImage");
    did_resolve = true;
  }
  if (!create_image) return false;
  CGImageRef image = create_image(CGRectNull,
                                  kCGWindowListOptionIncludingWindow,
                                  window_id,
                                  kCGWindowImageBoundsIgnoreFraming
                                  | kCGWindowImageBestResolution);
  if (!image) return false;
  size_t width = CGImageGetWidth(image);
  size_t height = CGImageGetHeight(image);
  size_t bytes_per_row = width * 4;
  uint8_t* pixels = calloc(height, bytes_per_row);
  CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
  CGContextRef context = pixels && color_space
                         ? CGBitmapContextCreate(pixels, width, height, 8,
                                                bytes_per_row, color_space,
                                                kCGImageAlphaPremultipliedLast
                                                | kCGBitmapByteOrder32Big)
                         : NULL;
  bool success = false;
  if (context) {
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
    success = sample_corners_rgba(pixels, width, height, bytes_per_row, colors);
    if (success) {
      uint32_t alpha_mask = (uint32_t)alpha << 24;
      colors->top_left = alpha_mask | colors->top_left;
      colors->top_right = alpha_mask | colors->top_right;
      colors->bottom_left = alpha_mask | colors->bottom_left;
      colors->bottom_right = alpha_mask | colors->bottom_right;
    }
  }
  if (context) CGContextRelease(context);
  if (color_space) CGColorSpaceRelease(color_space);
  free(pixels);
  CGImageRelease(image);
  return success;
}

static uint8_t channel(uint32_t color, int shift) {
  return (color >> shift) & 0xff;
}

CGImageRef auto_color_create_bilinear_gradient(struct auto_colors colors) {
  enum { SIZE = 64 };
  uint8_t* pixels = malloc(SIZE * SIZE * 4);
  if (!pixels) return NULL;
  for (int y = 0; y < SIZE; ++y) {
    float fy = (float)y / (SIZE - 1);
    for (int x = 0; x < SIZE; ++x) {
      float fx = (float)x / (SIZE - 1);
      uint32_t corners[] = { colors.top_left, colors.top_right,
                             colors.bottom_left, colors.bottom_right };
      float weights[] = { (1.f - fx) * (1.f - fy), fx * (1.f - fy),
                          (1.f - fx) * fy, fx * fy };
      uint8_t* pixel = pixels + (y * SIZE + x) * 4;
      for (int component = 0; component < 4; ++component) {
        int shift = component == 0 ? 16 : component == 1 ? 8 : component == 2 ? 0 : 24;
        float value = 0.f;
        for (int corner = 0; corner < 4; ++corner) {
          value += channel(corners[corner], shift) * weights[corner];
        }
        pixel[component] = (uint8_t)(value + 0.5f);
      }
    }
  }
  CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
  CGContextRef context = CGBitmapContextCreate(pixels, SIZE, SIZE, 8,
                                                SIZE * 4, space,
                                                kCGImageAlphaPremultipliedLast
                                                | kCGBitmapByteOrder32Big);
  CGImageRef image = context ? CGBitmapContextCreateImage(context) : NULL;
  if (context) CGContextRelease(context);
  if (space) CGColorSpaceRelease(space);
  free(pixels);
  return image;
}
