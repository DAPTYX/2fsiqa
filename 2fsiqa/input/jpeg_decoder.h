// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#pragma once
#include "image_info.h"
#include <jpeglib.h>
#include <jerror.h>
#include <csetjmp>
#include <stdexcept>
#include <cstdint>
#include <cstring>
#include <vector>
#include <memory>
#include <algorithm>

namespace jpeg_detail {

struct JpegErrorState {
    jpeg_error_mgr pub;
    jmp_buf escape;
    char message[JMSG_LENGTH_MAX];
};

struct JpegMemorySource {
    const uint8_t* bytes = nullptr;
    size_t length = 0;
    size_t position = 0;
};

static void jpeg_error_exit(j_common_ptr cinfo) {
    auto* state = reinterpret_cast<JpegErrorState*>(cinfo->err);
    (*cinfo->err->format_message)(cinfo, state->message);
    longjmp(state->escape, 1);
}

static void jpeg_emit_message(j_common_ptr, int) {
}

static void init_memory_source(j_decompress_ptr) {
}

static boolean fill_memory_source(j_decompress_ptr cinfo) {
    static const JOCTET eoi[2] = {0xFF, JPEG_EOI};
    cinfo->src->next_input_byte = eoi;
    cinfo->src->bytes_in_buffer = 2;
    return TRUE;
}

static void skip_memory_source(j_decompress_ptr cinfo, long num_bytes) {
    if (num_bytes <= 0) {
        return;
    }
    auto* src = cinfo->src;
    while (num_bytes > static_cast<long>(src->bytes_in_buffer)) {
        num_bytes -= static_cast<long>(src->bytes_in_buffer);
        (void)fill_memory_source(cinfo);
    }
    src->next_input_byte += num_bytes;
    src->bytes_in_buffer -= static_cast<size_t>(num_bytes);
}

static void term_memory_source(j_decompress_ptr) {
}

static void attach_memory_source(j_decompress_ptr cinfo, JpegMemorySource* mem) {
    if (cinfo->src == nullptr) {
        cinfo->src = static_cast<jpeg_source_mgr*>(
            (*cinfo->mem->alloc_small)(
                reinterpret_cast<j_common_ptr>(cinfo),
                JPOOL_PERMANENT,
                sizeof(jpeg_source_mgr)));
    }
    jpeg_source_mgr* src = cinfo->src;
    src->init_source = init_memory_source;
    src->fill_input_buffer = fill_memory_source;
    src->skip_input_data = skip_memory_source;
    src->resync_to_restart = jpeg_resync_to_restart;
    src->term_source = term_memory_source;
    src->next_input_byte = mem->bytes;
    src->bytes_in_buffer = mem->length;
}

static void collect_icc_from_markers(j_decompress_ptr cinfo, std::vector<uint8_t>& out) {
    JOCTET* profile = nullptr;
    unsigned int profile_length = 0;
    if (jpeg_read_icc_profile(cinfo, &profile, &profile_length) &&
        profile != nullptr && profile_length > 0) {
        out.assign(profile, profile + profile_length);
        free(profile);
    }
}

static int choose_output_precision(int file_precision) {
    if (file_precision >= 13) {
        return file_precision;
    }
    if (file_precision >= 9) {
        return file_precision;
    }
    return 12;
}

static void copy_scanline_8(
    const JSAMPLE* src, uint8_t* dst, size_t sample_count) {
    std::memcpy(dst, src, sample_count);
}

static void copy_scanline_12(
    const J12SAMPLE* src, uint8_t* dst, size_t sample_count) {
    auto* out = reinterpret_cast<uint16_t*>(dst);
    for (size_t i = 0; i < sample_count; ++i) {
        out[i] = static_cast<uint16_t>(src[i]);
    }
}

static void copy_scanline_16(
    const J16SAMPLE* src, uint8_t* dst, size_t sample_count) {
    auto* out = reinterpret_cast<uint16_t*>(dst);
    for (size_t i = 0; i < sample_count; ++i) {
        out[i] = static_cast<uint16_t>(src[i]);
    }
}

}  // namespace jpeg_detail

inline ImageInfo decode_jpeg(const uint8_t* file_bytes, size_t file_byte_count,
                             std::unique_ptr<uint8_t[]>& pixel_buffer) {
    if (!file_bytes || file_byte_count < 4) {
        throw std::runtime_error("Empty or truncated JPEG buffer");
    }

    jpeg_decompress_struct cinfo;
    jpeg_detail::JpegErrorState error_state;
    std::memset(&cinfo, 0, sizeof(cinfo));
    std::memset(&error_state, 0, sizeof(error_state));

    cinfo.err = jpeg_std_error(&error_state.pub);
    error_state.pub.error_exit = jpeg_detail::jpeg_error_exit;
    error_state.pub.emit_message = jpeg_detail::jpeg_emit_message;

    if (setjmp(error_state.escape)) {
        jpeg_destroy_decompress(&cinfo);
        throw std::runtime_error(
            error_state.message[0]
                ? std::string("JPEG decode failed: ") + error_state.message
                : std::string("JPEG decode failed"));
    }

    jpeg_create_decompress(&cinfo);

    jpeg_detail::JpegMemorySource memory_source{file_bytes, file_byte_count, 0};
    jpeg_detail::attach_memory_source(&cinfo, &memory_source);

    jpeg_save_markers(&cinfo, JPEG_APP0 + 2, 0xFFFF);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        throw std::runtime_error("JPEG header read failed");
    }

    const int file_precision = cinfo.data_precision;
    const int output_precision = jpeg_detail::choose_output_precision(file_precision);
    cinfo.data_precision = output_precision;

    if (cinfo.num_components == 1) {
        cinfo.out_color_space = JCS_GRAYSCALE;
    } else if (cinfo.num_components == 3 || cinfo.num_components == 4) {
        cinfo.out_color_space = JCS_RGB;
    } else {
        jpeg_destroy_decompress(&cinfo);
        throw std::runtime_error("Unsupported JPEG component count");
    }

    cinfo.dct_method = JDCT_ISLOW;
    cinfo.do_fancy_upsampling = TRUE;
    cinfo.do_block_smoothing = TRUE;

    jpeg_start_decompress(&cinfo);

    ImageInfo image;
    image.width = cinfo.output_width;
    image.height = cinfo.output_height;
    image.bit_depth = output_precision;
    image.reported_bit_depth = file_precision;
    image.sample_type = SampleType::Integer;
    image.has_alpha = false;

    if (cinfo.out_color_space == JCS_GRAYSCALE) {
        image.channels = 1;
    } else {
        image.channels = 3;
    }

    if (image.width == 0 || image.height == 0) {
        jpeg_destroy_decompress(&cinfo);
        throw std::runtime_error("Invalid JPEG dimensions");
    }

    const size_t samples_per_pixel = static_cast<size_t>(image.channels);
    const size_t bytes_per_sample = (output_precision <= 8) ? 1u : 2u;
    const size_t samples_per_row = static_cast<size_t>(image.width) * samples_per_pixel;
    image.row_bytes = samples_per_row * bytes_per_sample;
    image.pixel_bytes = image.row_bytes * static_cast<size_t>(image.height);

    pixel_buffer = std::make_unique_for_overwrite<uint8_t[]>(image.pixel_bytes);

    jpeg_detail::collect_icc_from_markers(&cinfo, image.icc_profile);

    if (output_precision <= 8) {
        std::vector<JSAMPLE> row(samples_per_row);
        JSAMPROW row_ptr = row.data();
        for (uint32_t y = 0; y < image.height; ++y) {
            if (jpeg_read_scanlines(&cinfo, &row_ptr, 1) != 1) {
                jpeg_destroy_decompress(&cinfo);
                throw std::runtime_error("JPEG scanline read failed");
            }
            jpeg_detail::copy_scanline_8(
                row.data(),
                pixel_buffer.get() + static_cast<size_t>(y) * image.row_bytes,
                samples_per_row);
        }
    } else if (output_precision <= 12) {
        std::vector<J12SAMPLE> row(samples_per_row);
        J12SAMPROW row_ptr = row.data();
        for (uint32_t y = 0; y < image.height; ++y) {
            if (jpeg12_read_scanlines(&cinfo, &row_ptr, 1) != 1) {
                jpeg_destroy_decompress(&cinfo);
                throw std::runtime_error("JPEG 12-bit scanline read failed");
            }
            jpeg_detail::copy_scanline_12(
                row.data(),
                pixel_buffer.get() + static_cast<size_t>(y) * image.row_bytes,
                samples_per_row);
        }
    } else {
        std::vector<J16SAMPLE> row(samples_per_row);
        J16SAMPROW row_ptr = row.data();
        for (uint32_t y = 0; y < image.height; ++y) {
            if (jpeg16_read_scanlines(&cinfo, &row_ptr, 1) != 1) {
                jpeg_destroy_decompress(&cinfo);
                throw std::runtime_error("JPEG 16-bit scanline read failed");
            }
            jpeg_detail::copy_scanline_16(
                row.data(),
                pixel_buffer.get() + static_cast<size_t>(y) * image.row_bytes,
                samples_per_row);
        }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return image;
}

inline bool probe_jpeg(const uint8_t* file_bytes, size_t file_byte_count,
                       uint32_t& width, uint32_t& height) {
    if (!file_bytes || file_byte_count < 3) {
        return false;
    }

    jpeg_decompress_struct cinfo;
    jpeg_detail::JpegErrorState error_state;
    std::memset(&cinfo, 0, sizeof(cinfo));
    std::memset(&error_state, 0, sizeof(error_state));

    cinfo.err = jpeg_std_error(&error_state.pub);
    error_state.pub.error_exit = jpeg_detail::jpeg_error_exit;
    error_state.pub.emit_message = jpeg_detail::jpeg_emit_message;

    if (setjmp(error_state.escape)) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_detail::JpegMemorySource memory_source{file_bytes, file_byte_count, 0};
    jpeg_detail::attach_memory_source(&cinfo, &memory_source);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    width = static_cast<uint32_t>(cinfo.image_width);
    height = static_cast<uint32_t>(cinfo.image_height);
    jpeg_destroy_decompress(&cinfo);
    return width > 0 && height > 0;
}
