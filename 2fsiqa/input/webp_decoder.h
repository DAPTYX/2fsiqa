// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#pragma once
#include "image_info.h"
#include <webp/decode.h>
#include <webp/mux.h>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

inline bool is_webp_signature(const uint8_t* bytes, size_t byte_count) {
    if (byte_count < 12) return false;
    return bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' &&
           bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P';
}

inline void collect_webp_icc(const uint8_t* file_bytes, size_t file_byte_count,
                             std::vector<uint8_t>& out) {
    out.clear();
    WebPData content;
    content.bytes = file_bytes;
    content.size = file_byte_count;
    WebPMux* mux = WebPMuxCreate(&content, 0);
    if (!mux) return;

    uint32_t flags = 0;
    if (WebPMuxGetFeatures(mux, &flags) == WEBP_MUX_OK && (flags & ICCP_FLAG)) {
        WebPData chunk;
        std::memset(&chunk, 0, sizeof(chunk));
        if (WebPMuxGetChunk(mux, "ICCP", &chunk) == WEBP_MUX_OK &&
            chunk.bytes != nullptr && chunk.size > 0) {
            out.assign(chunk.bytes, chunk.bytes + chunk.size);
        }
    }
    WebPMuxDelete(mux);
}

inline ImageInfo decode_webp(const uint8_t* file_bytes, size_t file_byte_count,
                             std::unique_ptr<uint8_t[]>& pixel_buffer) {
    if (!file_bytes || file_byte_count < 12) {
        throw std::runtime_error("Empty or truncated WebP buffer");
    }
    if (!is_webp_signature(file_bytes, file_byte_count)) {
        throw std::runtime_error("Not a WebP file");
    }

    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config)) {
        throw std::runtime_error("WebPInitDecoderConfig failed (library version mismatch)");
    }

    const VP8StatusCode feature_status =
        WebPGetFeatures(file_bytes, file_byte_count, &config.input);
    if (feature_status != VP8_STATUS_OK) {
        throw std::runtime_error("WebPGetFeatures failed");
    }

    if (config.input.has_animation) {
        throw std::runtime_error("Animated WebP is not supported");
    }

    const int width = config.input.width;
    const int height = config.input.height;
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid WebP dimensions");
    }

    const bool has_alpha = config.input.has_alpha != 0;
    config.output.colorspace = has_alpha ? MODE_RGBA : MODE_RGB;

    const VP8StatusCode decode_status =
        WebPDecode(file_bytes, file_byte_count, &config);
    if (decode_status != VP8_STATUS_OK) {
        WebPFreeDecBuffer(&config.output);
        throw std::runtime_error("WebPDecode failed");
    }

    const WebPDecBuffer& out = config.output;
    if (!out.u.RGBA.rgba || out.u.RGBA.size == 0) {
        WebPFreeDecBuffer(&config.output);
        throw std::runtime_error("WebPDecode produced empty buffer");
    }

    ImageInfo image;
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.bit_depth = 8;
    image.reported_bit_depth = 8;
    image.channels = 3;
    image.has_alpha = has_alpha;
    image.sample_type = SampleType::Integer;

    const size_t bytes_per_pixel = has_alpha ? 4u : 3u;
    image.row_bytes = static_cast<size_t>(width) * bytes_per_pixel;
    image.pixel_bytes = image.row_bytes * static_cast<size_t>(height);

    pixel_buffer = std::make_unique_for_overwrite<uint8_t[]>(image.pixel_bytes);

    const uint8_t* src = out.u.RGBA.rgba;
    const size_t src_stride = static_cast<size_t>(out.u.RGBA.stride);
    if (src_stride == image.row_bytes) {
        std::memcpy(pixel_buffer.get(), src, image.pixel_bytes);
    } else {
        for (uint32_t y = 0; y < image.height; ++y) {
            std::memcpy(pixel_buffer.get() + y * image.row_bytes,
                        src + y * src_stride,
                        image.row_bytes);
        }
    }

    WebPFreeDecBuffer(&config.output);
    collect_webp_icc(file_bytes, file_byte_count, image.icc_profile);
    return image;
}

inline bool probe_webp(const uint8_t* file_bytes, size_t file_byte_count,
                       uint32_t& width, uint32_t& height) {
    width = 0;
    height = 0;
    if (!file_bytes || file_byte_count < 12) {
        return false;
    }
    if (!is_webp_signature(file_bytes, file_byte_count)) {
        return false;
    }

    WebPBitstreamFeatures features;
    std::memset(&features, 0, sizeof(features));
    if (WebPGetFeatures(file_bytes, file_byte_count, &features) != VP8_STATUS_OK) {
        return false;
    }
    if (features.has_animation) {
        return false;
    }
    if (features.width <= 0 || features.height <= 0) {
        return false;
    }

    width = static_cast<uint32_t>(features.width);
    height = static_cast<uint32_t>(features.height);
    return true;
}
