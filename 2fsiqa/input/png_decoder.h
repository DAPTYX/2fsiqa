// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#pragma once
#include "image_info.h"
#include <png.h>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <cstdint>
#include <vector>
#include <memory>

struct PngBufferCursor {
    const uint8_t* bytes;
    size_t length;
    size_t position;
};

static void png_read_from_memory(png_structp png, png_bytep destination, png_size_t byte_count) {
    PngBufferCursor* cursor = static_cast<PngBufferCursor*>(png_get_io_ptr(png));
    if (cursor->position + byte_count > cursor->length) {
        png_error(png, "Read Error: out of bounds");
    }
    std::memcpy(destination, cursor->bytes + cursor->position, byte_count);
    cursor->position += byte_count;
}

static bool host_is_little_endian() {
    const uint16_t probe = 1;
    return *reinterpret_cast<const uint8_t*>(&probe) == 1;
}

static void png_error_handler(png_structp png, png_const_charp) {
    longjmp(png_jmpbuf(png), 1);
}

static void png_warning_handler(png_structp, png_const_charp message) {
    if (message && std::strstr(message, "RGB color space not permitted")) {
        return;
    }
    if (message) {
        std::fprintf(stderr, "libpng warning: %s\n", message);
    }
}

inline ImageInfo decode_png(const uint8_t* file_bytes, size_t file_byte_count,
                            std::unique_ptr<uint8_t[]>& pixel_buffer) {
    if (!file_bytes || file_byte_count == 0) {
        throw std::runtime_error("Empty PNG buffer");
    }

    PngBufferCursor cursor = { file_bytes, file_byte_count, 0 };

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        throw std::runtime_error("png_create_read_struct failed");
    }

    png_infop png_info = png_create_info_struct(png);
    if (!png_info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        throw std::runtime_error("png_create_info_struct failed");
    }

    png_set_error_fn(png, nullptr, png_error_handler, png_warning_handler);

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &png_info, nullptr);
        throw std::runtime_error("libpng read failed");
    }

    png_set_read_fn(png, &cursor, png_read_from_memory);
    png_read_info(png, png_info);

    ImageInfo image;
    image.width     = png_get_image_width(png, png_info);
    image.height    = png_get_image_height(png, png_info);
    image.bit_depth = png_get_bit_depth(png, png_info);
    image.reported_bit_depth = image.bit_depth;
    const int color_type = png_get_color_type(png, png_info);

    if (image.width == 0 || image.height == 0) {
        png_destroy_read_struct(&png, &png_info, nullptr);
        throw std::runtime_error("Invalid image dimensions");
    }
    if (image.bit_depth != 1 && image.bit_depth != 2 && image.bit_depth != 4 &&
        image.bit_depth != 8 && image.bit_depth != 16) {
        png_destroy_read_struct(&png, &png_info, nullptr);
        throw std::runtime_error("Unsupported PNG bit depth");
    }

    const bool has_plte = png_get_valid(png, png_info, PNG_INFO_PLTE) != 0;
    const bool expand_palette =
        color_type == PNG_COLOR_TYPE_PALETTE ||
        ((color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) &&
         has_plte);

    if (expand_palette) {
        if (image.bit_depth < 8) {
            png_set_packing(png);
        }
        png_set_palette_to_rgb(png);
        image.bit_depth = 8;
    } else {
        if (image.bit_depth < 8) {
            png_set_packing(png);
            image.bit_depth = 8;
        }
        if (image.bit_depth == 16 && host_is_little_endian()) {
            png_set_swap(png);
        }
    }

    if (png_get_valid(png, png_info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png);
    }

    png_set_expand(png);
    png_read_update_info(png, png_info);

    const int final_color_type = png_get_color_type(png, png_info);
    image.bit_depth = png_get_bit_depth(png, png_info);

    if (final_color_type == PNG_COLOR_TYPE_GRAY) {
        image.channels = 1;
        image.has_alpha = false;
    } else if (final_color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        image.channels = 1;
        image.has_alpha = true;
    } else if (final_color_type == PNG_COLOR_TYPE_RGB) {
        image.channels = 3;
        image.has_alpha = false;
    } else if (final_color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
        image.channels = 3;
        image.has_alpha = true;
    } else {
        png_destroy_read_struct(&png, &png_info, nullptr);
        throw std::runtime_error("Unsupported final color type after expand");
    }

    png_charp iccp_name = nullptr;
    int iccp_compression = 0;
    png_bytep iccp_data = nullptr;
    png_uint_32 iccp_len = 0;
    if (png_get_iCCP(png, png_info, &iccp_name, &iccp_compression, &iccp_data, &iccp_len) &&
        iccp_data && iccp_len > 0) {
        image.icc_profile.assign(iccp_data, iccp_data + iccp_len);
    }

    double wx, wy, rx, ry, gx, gy, bx, by;
    if (png_get_cHRM(png, png_info, &wx, &wy, &rx, &ry, &gx, &gy, &bx, &by)) {
        image.has_chromaticities = true;
        image.white_x = wx;
        image.white_y = wy;
        image.red_x = rx;
        image.red_y = ry;
        image.green_x = gx;
        image.green_y = gy;
        image.blue_x = bx;
        image.blue_y = by;
    }

    double gama = 0.0;
    if (png_get_gAMA(png, png_info, &gama)) {
        image.has_gamma = true;
        image.gamma = gama;
    }

    image.sample_type = SampleType::Integer;
    image.row_bytes   = png_get_rowbytes(png, png_info);
    image.pixel_bytes = image.row_bytes * static_cast<size_t>(image.height);

    pixel_buffer = std::make_unique_for_overwrite<uint8_t[]>(image.pixel_bytes);

    std::vector<png_bytep> row_pointers;
    row_pointers.reserve(image.height);
    for (uint32_t row = 0; row < image.height; ++row) {
        row_pointers.push_back(pixel_buffer.get() + row * image.row_bytes);
    }

    png_read_image(png, row_pointers.data());
    png_destroy_read_struct(&png, &png_info, nullptr);
    return image;
}

inline bool probe_png(const uint8_t* file_bytes, size_t file_byte_count,
                      uint32_t& width, uint32_t& height) {
    if (!file_bytes || file_byte_count < 8) {
        return false;
    }

    PngBufferCursor cursor = { file_bytes, file_byte_count, 0 };
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        return false;
    }

    png_infop png_info = png_create_info_struct(png);
    if (!png_info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        return false;
    }

    png_set_error_fn(png, nullptr, png_error_handler, png_warning_handler);
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &png_info, nullptr);
        return false;
    }

    png_set_read_fn(png, &cursor, png_read_from_memory);
    png_read_info(png, png_info);
    width = png_get_image_width(png, png_info);
    height = png_get_image_height(png, png_info);
    png_destroy_read_struct(&png, &png_info, nullptr);
    return width > 0 && height > 0;
}
