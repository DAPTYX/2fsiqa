// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#pragma once
#include "image_info.h"
#include <avif/avif.h>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

inline bool is_avif_signature(const uint8_t* bytes, size_t byte_count) {
    if (byte_count < 12) return false;
    if (bytes[4] != 'f' || bytes[5] != 't' || bytes[6] != 'y' || bytes[7] != 'p') {
        return false;
    }
    const size_t limit = byte_count < 32 ? byte_count : 32;
    for (size_t i = 8; i + 4 <= limit; ++i) {
        if ((bytes[i] == 'a' && bytes[i + 1] == 'v' && bytes[i + 2] == 'i' && bytes[i + 3] == 'f') ||
            (bytes[i] == 'a' && bytes[i + 1] == 'v' && bytes[i + 2] == 'i' && bytes[i + 3] == 's')) {
            return true;
        }
    }
    return false;
}

inline ImageInfo decode_avif(const uint8_t* file_bytes, size_t file_byte_count,
                             std::unique_ptr<uint8_t[]>& pixel_buffer) {
    if (!file_bytes || file_byte_count < 12) {
        throw std::runtime_error("Empty or truncated AVIF buffer");
    }

    avifDecoder* decoder = avifDecoderCreate();
    if (!decoder) {
        throw std::runtime_error("avifDecoderCreate failed");
    }

    avifResult result = avifDecoderSetIOMemory(decoder, file_bytes, file_byte_count);
    if (result != AVIF_RESULT_OK) {
        avifDecoderDestroy(decoder);
        throw std::runtime_error(std::string("avifDecoderSetIOMemory failed: ") +
                                 avifResultToString(result));
    }

    result = avifDecoderParse(decoder);
    if (result != AVIF_RESULT_OK) {
        avifDecoderDestroy(decoder);
        throw std::runtime_error(std::string("avifDecoderParse failed: ") +
                                 avifResultToString(result));
    }

    if (decoder->imageSequenceTrackPresent) {
        avifDecoderDestroy(decoder);
        throw std::runtime_error("Animated AVIF is not supported");
    }

    result = avifDecoderNextImage(decoder);
    if (result != AVIF_RESULT_OK) {
        avifDecoderDestroy(decoder);
        throw std::runtime_error(std::string("avifDecoderNextImage failed: ") +
                                 avifResultToString(result));
    }

    const avifImage* image = decoder->image;
    if (!image || image->width == 0 || image->height == 0) {
        avifDecoderDestroy(decoder);
        throw std::runtime_error("Invalid AVIF dimensions");
    }

    const uint32_t file_depth = image->depth;
    if (file_depth != 8 && file_depth != 10 && file_depth != 12) {
        avifDecoderDestroy(decoder);
        throw std::runtime_error("Unsupported AVIF bit depth");
    }

    const uint32_t out_depth = (file_depth > 8) ? 16u : 8u;
    const bool is_gray = (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV400);
    const bool has_alpha = (image->alphaPlane != nullptr) || (decoder->alphaPresent != AVIF_FALSE);

    avifRGBImage rgb;
    std::memset(&rgb, 0, sizeof(rgb));
    avifRGBImageSetDefaults(&rgb, image);
    rgb.depth = out_depth;
    rgb.isFloat = AVIF_FALSE;
    if (is_gray) {
        rgb.format = has_alpha ? AVIF_RGB_FORMAT_GRAYA : AVIF_RGB_FORMAT_GRAY;
    } else {
        rgb.format = has_alpha ? AVIF_RGB_FORMAT_RGBA : AVIF_RGB_FORMAT_RGB;
    }

    result = avifRGBImageAllocatePixels(&rgb);
    if (result != AVIF_RESULT_OK) {
        avifDecoderDestroy(decoder);
        throw std::runtime_error("avifRGBImageAllocatePixels failed");
    }

    result = avifImageYUVToRGB(image, &rgb);
    if (result != AVIF_RESULT_OK) {
        avifRGBImageFreePixels(&rgb);
        avifDecoderDestroy(decoder);
        throw std::runtime_error(std::string("avifImageYUVToRGB failed: ") +
                                 avifResultToString(result));
    }

    ImageInfo info;
    info.width = image->width;
    info.height = image->height;
    info.bit_depth = static_cast<int>(out_depth);
    info.reported_bit_depth = static_cast<int>(file_depth);
    info.sample_type = SampleType::Integer;
    info.has_alpha = has_alpha;
    info.channels = is_gray ? 1 : 3;

    const size_t bytes_per_sample = (out_depth > 8) ? 2u : 1u;
    const size_t samples_per_pixel =
        static_cast<size_t>(info.channels) + (has_alpha ? 1u : 0u);
    info.row_bytes = static_cast<size_t>(info.width) * samples_per_pixel * bytes_per_sample;
    info.pixel_bytes = info.row_bytes * static_cast<size_t>(info.height);

    if (image->icc.data && image->icc.size > 0) {
        info.icc_profile.assign(image->icc.data, image->icc.data + image->icc.size);
    }

    pixel_buffer = std::make_unique_for_overwrite<uint8_t[]>(info.pixel_bytes);

    const size_t src_stride = static_cast<size_t>(rgb.rowBytes);
    if (src_stride == info.row_bytes) {
        std::memcpy(pixel_buffer.get(), rgb.pixels, info.pixel_bytes);
    } else {
        for (uint32_t y = 0; y < info.height; ++y) {
            std::memcpy(pixel_buffer.get() + y * info.row_bytes,
                        rgb.pixels + y * src_stride,
                        info.row_bytes);
        }
    }

    avifRGBImageFreePixels(&rgb);
    avifDecoderDestroy(decoder);
    return info;
}

inline bool probe_avif(const uint8_t* file_bytes, size_t file_byte_count,
                       uint32_t& width, uint32_t& height) {
    width = 0;
    height = 0;
    if (!file_bytes || file_byte_count < 12) {
        return false;
    }
    if (!is_avif_signature(file_bytes, file_byte_count)) {
        return false;
    }

    avifDecoder* decoder = avifDecoderCreate();
    if (!decoder) {
        return false;
    }

    if (avifDecoderSetIOMemory(decoder, file_bytes, file_byte_count) != AVIF_RESULT_OK) {
        avifDecoderDestroy(decoder);
        return false;
    }
    if (avifDecoderParse(decoder) != AVIF_RESULT_OK) {
        avifDecoderDestroy(decoder);
        return false;
    }
    if (decoder->imageSequenceTrackPresent) {
        avifDecoderDestroy(decoder);
        return false;
    }

    if (decoder->image && decoder->image->width > 0 && decoder->image->height > 0) {
        width = decoder->image->width;
        height = decoder->image->height;
        avifDecoderDestroy(decoder);
        return true;
    }

    avifDecoderDestroy(decoder);
    return false;
}
