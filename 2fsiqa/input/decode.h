// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#pragma once
#include "image_info.h"
#include "png_decoder.h"
#include "tiff_decoder.h"
#include "jpeg_decoder.h"
#include "webp_decoder.h"
#include "avif_decoder.h"
#include "pnm_decoder.h"
#include "bmp_decoder.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <cctype>

enum class ImageFormat {
    Unknown,
    Png,
    Tiff,
    Jpeg,
    Webp,
    Avif,
    Pnm,
    Bmp
};

inline bool is_png_signature(const uint8_t* bytes, size_t byte_count) {
    static constexpr uint8_t PNG_MAGIC[8] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
    };
    if (byte_count < 8) return false;
    return std::memcmp(bytes, PNG_MAGIC, 8) == 0;
}

inline bool is_tiff_signature(const uint8_t* bytes, size_t byte_count) {
    if (byte_count < 4) return false;
    const bool le = bytes[0] == 'I' && bytes[1] == 'I' &&
                    bytes[2] == 42 && bytes[3] == 0;
    const bool be = bytes[0] == 'M' && bytes[1] == 'M' &&
                    bytes[2] == 0 && bytes[3] == 42;
    return le || be;
}

inline bool is_jpeg_signature(const uint8_t* bytes, size_t byte_count) {
    if (byte_count < 3) return false;
    return bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF;
}

inline ImageFormat detect_format_from_header(const uint8_t* bytes, size_t byte_count) {
    if (is_png_signature(bytes, byte_count)) return ImageFormat::Png;
    if (is_tiff_signature(bytes, byte_count)) return ImageFormat::Tiff;
    if (is_jpeg_signature(bytes, byte_count)) return ImageFormat::Jpeg;
    if (is_webp_signature(bytes, byte_count)) return ImageFormat::Webp;
    if (is_avif_signature(bytes, byte_count)) return ImageFormat::Avif;
    if (is_pnm_signature(bytes, byte_count)) return ImageFormat::Pnm;
    if (is_bmp_signature(bytes, byte_count)) return ImageFormat::Bmp;
    return ImageFormat::Unknown;
}

inline std::string extension_lower(const char* path) {
    if (!path) return {};
    const char* dot = std::strrchr(path, '.');
    if (!dot || dot == path) return {};
    std::string ext(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

inline ImageFormat detect_format_from_extension(const char* path) {
    const std::string ext = extension_lower(path);
    if (ext == "png") return ImageFormat::Png;
    if (ext == "tif" || ext == "tiff") return ImageFormat::Tiff;
    if (ext == "jpg" || ext == "jpeg" || ext == "jpe" ||
        ext == "jfif" || ext == "jfi") {
        return ImageFormat::Jpeg;
    }
    if (ext == "webp") return ImageFormat::Webp;
    if (ext == "avif") return ImageFormat::Avif;
    if (ext == "pnm" || ext == "pbm" || ext == "pgm" || ext == "ppm" ||
        ext == "pam" || ext == "pfm") {
        return ImageFormat::Pnm;
    }
    if (ext == "bmp" || ext == "dib") {
        return ImageFormat::Bmp;
    }
    return ImageFormat::Unknown;
}

inline bool is_supported_format(const uint8_t* bytes, size_t byte_count) {
    return detect_format_from_header(bytes, byte_count) != ImageFormat::Unknown;
}

inline bool is_supported_path(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    uint8_t header[32];
    const size_t read_count = fread(header, 1, 32, file);
    fclose(file);
    if (detect_format_from_header(header, read_count) != ImageFormat::Unknown) {
        return true;
    }
    return detect_format_from_extension(path) != ImageFormat::Unknown;
}

inline ImageFormat resolve_format(const uint8_t* file_bytes, size_t file_byte_count,
                                  const char* path_for_fallback) {
    ImageFormat format = detect_format_from_header(file_bytes, file_byte_count);
    if (format == ImageFormat::Unknown && path_for_fallback) {
        format = detect_format_from_extension(path_for_fallback);
    }
    return format;
}

inline bool probe_image(const uint8_t* file_bytes, size_t file_byte_count,
                        uint32_t& width, uint32_t& height,
                        const char* path_for_fallback = nullptr) {
    width = 0;
    height = 0;
    const ImageFormat format = resolve_format(file_bytes, file_byte_count, path_for_fallback);
    if (format == ImageFormat::Png) {
        return probe_png(file_bytes, file_byte_count, width, height);
    }
    if (format == ImageFormat::Tiff) {
        return probe_tiff(file_bytes, file_byte_count, width, height);
    }
    if (format == ImageFormat::Jpeg) {
        return probe_jpeg(file_bytes, file_byte_count, width, height);
    }
    if (format == ImageFormat::Webp) {
        return probe_webp(file_bytes, file_byte_count, width, height);
    }
    if (format == ImageFormat::Avif) {
        return probe_avif(file_bytes, file_byte_count, width, height);
    }
    if (format == ImageFormat::Pnm) {
        return probe_pnm(file_bytes, file_byte_count, width, height);
    }
    if (format == ImageFormat::Bmp) {
        return probe_bmp(file_bytes, file_byte_count, width, height);
    }
    return false;
}

inline bool probe_image_path(const char* path, uint32_t& width, uint32_t& height) {
    width = 0;
    height = 0;
    if (!path) {
        return false;
    }

    FILE* file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    const long size = ftell(file);
    if (size <= 0) {
        fclose(file);
        return false;
    }
    rewind(file);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) {
        fclose(file);
        return false;
    }
    fclose(file);
    return probe_image(bytes.data(), bytes.size(), width, height, path);
}

inline ImageInfo decode_image(const uint8_t* file_bytes, size_t file_byte_count,
                              std::unique_ptr<uint8_t[]>& pixel_buffer,
                              const char* path_for_fallback = nullptr) {
    ImageFormat format = detect_format_from_header(file_bytes, file_byte_count);

    if (format == ImageFormat::Unknown && path_for_fallback) {
        format = detect_format_from_extension(path_for_fallback);
        if (format != ImageFormat::Unknown) {
            std::cerr << "Warning: unrecognized header, trying decoder from extension ("
                      << path_for_fallback << ")\n";
        }
    }

    if (format == ImageFormat::Png) {
        return decode_png(file_bytes, file_byte_count, pixel_buffer);
    }
    if (format == ImageFormat::Tiff) {
        return decode_tiff(file_bytes, file_byte_count, pixel_buffer);
    }
    if (format == ImageFormat::Jpeg) {
        return decode_jpeg(file_bytes, file_byte_count, pixel_buffer);
    }
    if (format == ImageFormat::Webp) {
        return decode_webp(file_bytes, file_byte_count, pixel_buffer);
    }
    if (format == ImageFormat::Avif) {
        return decode_avif(file_bytes, file_byte_count, pixel_buffer);
    }
    if (format == ImageFormat::Pnm) {
        return decode_pnm(file_bytes, file_byte_count, pixel_buffer);
    }
    if (format == ImageFormat::Bmp) {
        return decode_bmp(file_bytes, file_byte_count, pixel_buffer);
    }

    throw std::runtime_error(
        path_for_fallback
            ? std::string("Format not supported: ") + path_for_fallback
            : std::string("Format not supported"));
}
