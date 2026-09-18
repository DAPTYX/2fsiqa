// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#pragma once
#include "image_info.h"
#include <tiffio.h>
#include <stdexcept>
#include <cstdint>
#include <cstring>
#include <vector>
#include <memory>
#include <algorithm>
#include <string>

namespace tiff_detail {

struct MemoryTiffSource {
    const uint8_t* bytes = nullptr;
    size_t length = 0;
    toff_t position = 0;
};

static tsize_t memory_read(thandle_t handle, tdata_t buffer, tsize_t size) {
    auto* src = static_cast<MemoryTiffSource*>(handle);
    if (!src || !src->bytes || size <= 0) {
        return 0;
    }
    const toff_t remaining = static_cast<toff_t>(src->length) - src->position;
    if (remaining <= 0) {
        return 0;
    }
    const tsize_t to_copy = static_cast<tsize_t>(
        std::min(static_cast<toff_t>(size), remaining));
    std::memcpy(buffer, src->bytes + src->position, static_cast<size_t>(to_copy));
    src->position += static_cast<toff_t>(to_copy);
    return to_copy;
}

static tsize_t memory_write(thandle_t, tdata_t, tsize_t) {
    return 0;
}

static toff_t memory_seek(thandle_t handle, toff_t offset, int whence) {
    auto* src = static_cast<MemoryTiffSource*>(handle);
    if (!src) {
        return static_cast<toff_t>(-1);
    }
    toff_t next = 0;
    if (whence == SEEK_SET) {
        next = offset;
    } else if (whence == SEEK_CUR) {
        next = src->position + offset;
    } else if (whence == SEEK_END) {
        next = static_cast<toff_t>(src->length) + offset;
    } else {
        return static_cast<toff_t>(-1);
    }
    if (next > static_cast<toff_t>(src->length)) {
        return static_cast<toff_t>(-1);
    }
    src->position = next;
    return src->position;
}

static int memory_close(thandle_t) {
    return 0;
}

static toff_t memory_size(thandle_t handle) {
    auto* src = static_cast<MemoryTiffSource*>(handle);
    return src ? static_cast<toff_t>(src->length) : 0;
}

static void swap_bytes_16(uint8_t* data, size_t sample_count) {
    for (size_t i = 0; i < sample_count; ++i) {
        uint8_t* p = data + i * 2;
        std::swap(p[0], p[1]);
    }
}

static void swap_bytes_32(uint8_t* data, size_t sample_count) {
    for (size_t i = 0; i < sample_count; ++i) {
        uint8_t* p = data + i * 4;
        std::swap(p[0], p[3]);
        std::swap(p[1], p[2]);
    }
}

static void swap_bytes_64(uint8_t* data, size_t sample_count) {
    for (size_t i = 0; i < sample_count; ++i) {
        uint8_t* p = data + i * 8;
        std::swap(p[0], p[7]);
        std::swap(p[1], p[6]);
        std::swap(p[2], p[5]);
        std::swap(p[3], p[4]);
    }
}

static bool host_is_little_endian() {
    const uint16_t probe = 1;
    return *reinterpret_cast<const uint8_t*>(&probe) == 1;
}

static float half_bits_to_float(uint16_t bits) {
    const uint32_t sign = (bits >> 15) & 1u;
    const uint32_t exp = (bits >> 10) & 0x1Fu;
    const uint32_t mant = bits & 0x3FFu;
    uint32_t fbits = 0;
    if (exp == 0) {
        if (mant == 0) {
            fbits = sign << 31;
        } else {
            uint32_t m = mant;
            uint32_t e = 127 - 15 + 1;
            while ((m & 0x400u) == 0u) {
                m <<= 1;
                --e;
            }
            m &= 0x3FFu;
            fbits = (sign << 31) | (e << 23) | (m << 13);
        }
    } else if (exp == 31) {
        fbits = (sign << 31) | 0x7F800000u | (mant << 13);
    } else {
        fbits = (sign << 31) | ((exp + (127 - 15)) << 23) | (mant << 13);
    }
    float out = 0.0f;
    std::memcpy(&out, &fbits, 4);
    return out;
}

static void convert_half_buffer_to_float32(const uint8_t* src, float* dst,
                                           size_t sample_count) {
    const uint16_t* halfs = reinterpret_cast<const uint16_t*>(src);
    for (size_t i = 0; i < sample_count; ++i) {
        dst[i] = half_bits_to_float(halfs[i]);
    }
}

}

inline ImageInfo decode_tiff(const uint8_t* file_bytes, size_t file_byte_count,
                             std::unique_ptr<uint8_t[]>& pixel_buffer) {
    if (!file_bytes || file_byte_count < 8) {
        throw std::runtime_error("Empty or truncated TIFF buffer");
    }

    tiff_detail::MemoryTiffSource source{file_bytes, file_byte_count, 0};

    TIFF* tif = TIFFClientOpen(
        "memory.tif", "rm",
        &source,
        tiff_detail::memory_read,
        tiff_detail::memory_write,
        tiff_detail::memory_seek,
        tiff_detail::memory_close,
        tiff_detail::memory_size,
        nullptr,
        nullptr);

    if (!tif) {
        throw std::runtime_error("TIFFClientOpen failed");
    }

    ImageInfo image;
    try {
        uint32_t width = 0;
        uint32_t height = 0;
        uint16_t samples_per_pixel = 0;
        uint16_t bits_per_sample = 0;
        uint16_t sample_format = SAMPLEFORMAT_UINT;
        uint16_t photometric = 0;
        uint16_t planar_config = PLANARCONFIG_CONTIG;
        uint16_t extra_samples_count = 0;
        uint16_t* extra_samples = nullptr;
        uint16_t orientation = ORIENTATION_TOPLEFT;

        if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width) ||
            !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height)) {
            throw std::runtime_error("TIFF missing ImageWidth/ImageLength");
        }
        if (width == 0 || height == 0) {
            throw std::runtime_error("Invalid TIFF dimensions");
        }

        if (!TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samples_per_pixel)) {
            samples_per_pixel = 1;
        }

        TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bits_per_sample);

        if (!TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sample_format)) {
            sample_format = SAMPLEFORMAT_UINT;
        }
        if (!TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photometric)) {
            photometric = PHOTOMETRIC_RGB;
        }
        TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &planar_config);
        TIFFGetFieldDefaulted(tif, TIFFTAG_ORIENTATION, &orientation);

        if (planar_config != PLANARCONFIG_CONTIG) {
            throw std::runtime_error("TIFF planar (separate) configuration is not supported");
        }

        if (orientation != ORIENTATION_TOPLEFT &&
            orientation != ORIENTATION_BOTLEFT &&
            orientation != ORIENTATION_TOPRIGHT &&
            orientation != ORIENTATION_BOTRIGHT) {
            throw std::runtime_error("Unsupported TIFF orientation");
        }

        TIFFGetFieldDefaulted(tif, TIFFTAG_EXTRASAMPLES, &extra_samples_count, &extra_samples);

        bool has_alpha = false;
        int color_channels = 0;

        if (photometric == PHOTOMETRIC_MINISBLACK ||
            photometric == PHOTOMETRIC_MINISWHITE) {
            color_channels = 1;
        } else if (photometric == PHOTOMETRIC_RGB) {
            color_channels = 3;
        } else {
            throw std::runtime_error(
                "Unsupported TIFF PhotometricInterpretation (only MinIsBlack/MinIsWhite/RGB)");
        }

        if (extra_samples_count > 0 && extra_samples != nullptr) {
            for (uint16_t i = 0; i < extra_samples_count; ++i) {
                if (extra_samples[i] == EXTRASAMPLE_ASSOCALPHA ||
                    extra_samples[i] == EXTRASAMPLE_UNASSALPHA ||
                    extra_samples[i] == EXTRASAMPLE_UNSPECIFIED) {
                    has_alpha = true;
                    break;
                }
            }
        }
        if (!has_alpha) {
            if (photometric == PHOTOMETRIC_RGB && samples_per_pixel == 4) {
                has_alpha = true;
            } else if ((photometric == PHOTOMETRIC_MINISBLACK ||
                        photometric == PHOTOMETRIC_MINISWHITE) &&
                       samples_per_pixel == 2) {
                has_alpha = true;
            }
        }

        const uint16_t expected_spp =
            static_cast<uint16_t>(color_channels + (has_alpha ? 1 : 0));
        if (samples_per_pixel < expected_spp) {
            throw std::runtime_error("TIFF SamplesPerPixel too small for photometric/alpha");
        }
        if (samples_per_pixel > expected_spp + 4) {
            throw std::runtime_error("TIFF SamplesPerPixel has unsupported extra channels");
        }

        const bool is_half_float =
            (sample_format == SAMPLEFORMAT_IEEEFP && bits_per_sample == 16);

        if (sample_format == SAMPLEFORMAT_IEEEFP) {
            if (bits_per_sample == 16) {
                image.sample_type = SampleType::Float32;
            } else if (bits_per_sample == 32) {
                image.sample_type = SampleType::Float32;
            } else if (bits_per_sample == 64) {
                image.sample_type = SampleType::Float64;
            } else {
                throw std::runtime_error("Unsupported IEEE float BitsPerSample");
            }
        } else if (sample_format == SAMPLEFORMAT_UINT) {
            if (bits_per_sample != 8 && bits_per_sample != 16 &&
                bits_per_sample != 32) {
                throw std::runtime_error("Unsupported integer BitsPerSample (need 8/16/32)");
            }
            image.sample_type = SampleType::Integer;
        } else {
            throw std::runtime_error("Unsupported TIFF SampleFormat (need UINT or IEEEFP)");
        }

        image.width = width;
        image.height = height;
        if (is_half_float) {
            image.bit_depth = 32;
            image.reported_bit_depth = 10;
        } else if (sample_format == SAMPLEFORMAT_IEEEFP && bits_per_sample == 32) {
            image.bit_depth = 32;
            image.reported_bit_depth = 23;
        } else if (sample_format == SAMPLEFORMAT_IEEEFP && bits_per_sample == 64) {
            image.bit_depth = 64;
            image.reported_bit_depth = 52;
        } else {
            image.bit_depth = static_cast<int>(bits_per_sample);
            image.reported_bit_depth = image.bit_depth;
        }
        image.channels = color_channels;
        image.has_alpha = has_alpha;

        uint32_t icc_len = 0;
        void* icc_data = nullptr;
        if (TIFFGetField(tif, TIFFTAG_ICCPROFILE, &icc_len, &icc_data) &&
            icc_data && icc_len > 0) {
            image.icc_profile.assign(
                static_cast<const uint8_t*>(icc_data),
                static_cast<const uint8_t*>(icc_data) + icc_len);
        }

        float* white_point = nullptr;
        if (TIFFGetField(tif, TIFFTAG_WHITEPOINT, &white_point) && white_point) {
            image.white_x = static_cast<double>(white_point[0]);
            image.white_y = static_cast<double>(white_point[1]);
        }

        float* primaries = nullptr;
        if (TIFFGetField(tif, TIFFTAG_PRIMARYCHROMATICITIES, &primaries) && primaries) {
            image.has_chromaticities = true;
            image.red_x = static_cast<double>(primaries[0]);
            image.red_y = static_cast<double>(primaries[1]);
            image.green_x = static_cast<double>(primaries[2]);
            image.green_y = static_cast<double>(primaries[3]);
            image.blue_x = static_cast<double>(primaries[4]);
            image.blue_y = static_cast<double>(primaries[5]);
            if (!white_point) {
                image.white_x = 0.3127;
                image.white_y = 0.3290;
            }
        } else if (white_point) {
            image.has_chromaticities = false;
        }

        const size_t spp = static_cast<size_t>(samples_per_pixel);
        const size_t file_bytes_per_sample = static_cast<size_t>((bits_per_sample + 7) / 8);
        const size_t file_bytes_per_pixel = spp * file_bytes_per_sample;
        const size_t file_row_bytes = static_cast<size_t>(width) * file_bytes_per_pixel;

        const size_t out_bytes_per_sample = is_half_float
            ? sizeof(float)
            : file_bytes_per_sample;
        const size_t out_bytes_per_pixel = spp * out_bytes_per_sample;
        const size_t calculated_row_bytes = static_cast<size_t>(width) * out_bytes_per_pixel;

        const tsize_t scanline_size_ts = TIFFScanlineSize(tif);
        if (scanline_size_ts <= 0) {
            throw std::runtime_error("Invalid TIFF scanline size");
        }
        const size_t scanline_size = static_cast<size_t>(scanline_size_ts);

        if (scanline_size < file_row_bytes) {
            throw std::runtime_error(
                "TIFF scanline smaller than expected tightly-packed row "
                "(BitsPerSample / SamplesPerPixel mismatch?)");
        }

        image.row_bytes = calculated_row_bytes;
        image.pixel_bytes = image.row_bytes * static_cast<size_t>(height);

        pixel_buffer = std::make_unique_for_overwrite<uint8_t[]>(image.pixel_bytes);

        std::vector<uint8_t> scanline(scanline_size);

        const bool need_vertical_flip =
            (orientation == ORIENTATION_BOTLEFT || orientation == ORIENTATION_BOTRIGHT);
        const bool need_horizontal_flip =
            (orientation == ORIENTATION_TOPRIGHT || orientation == ORIENTATION_BOTRIGHT);

        const bool file_is_big_endian = TIFFIsBigEndian(tif) != 0;
        const bool host_le = tiff_detail::host_is_little_endian();
        const bool host_is_big_endian = !host_le;
        const bool need_byte_swap = file_is_big_endian != host_is_big_endian;

        for (uint32_t y = 0; y < height; ++y) {
            if (TIFFReadScanline(tif, scanline.data(), y, 0) < 0) {
                throw std::runtime_error("TIFFReadScanline failed");
            }

            const uint32_t dst_y = need_vertical_flip ? (height - 1u - y) : y;
            uint8_t* dst_row = pixel_buffer.get() + static_cast<size_t>(dst_y) * image.row_bytes;

            if (is_half_float) {
                std::vector<uint8_t> ordered(file_row_bytes);
                if (!need_horizontal_flip) {
                    std::memcpy(ordered.data(), scanline.data(), file_row_bytes);
                } else {
                    for (uint32_t x = 0; x < width; ++x) {
                        const uint8_t* src_px =
                            scanline.data() + static_cast<size_t>(x) * file_bytes_per_pixel;
                        uint8_t* dst_px =
                            ordered.data() +
                            static_cast<size_t>(width - 1u - x) * file_bytes_per_pixel;
                        std::memcpy(dst_px, src_px, file_bytes_per_pixel);
                    }
                }
                if (need_byte_swap) {
                    tiff_detail::swap_bytes_16(ordered.data(),
                                              static_cast<size_t>(width) * spp);
                }
                tiff_detail::convert_half_buffer_to_float32(
                    ordered.data(), reinterpret_cast<float*>(dst_row),
                    static_cast<size_t>(width) * spp);
            } else if (!need_horizontal_flip) {
                std::memcpy(dst_row, scanline.data(), image.row_bytes);
            } else {
                for (uint32_t x = 0; x < width; ++x) {
                    const uint8_t* src_px =
                        scanline.data() + static_cast<size_t>(x) * file_bytes_per_pixel;
                    uint8_t* dst_px =
                        dst_row + static_cast<size_t>(width - 1u - x) * out_bytes_per_pixel;
                    std::memcpy(dst_px, src_px, out_bytes_per_pixel);
                }
            }
        }

        if (!is_half_float) {
            if (need_byte_swap && image.sample_type == SampleType::Integer) {
                const size_t total_samples =
                    static_cast<size_t>(width) * static_cast<size_t>(height) * spp;
                if (bits_per_sample == 16) {
                    tiff_detail::swap_bytes_16(pixel_buffer.get(), total_samples);
                } else if (bits_per_sample == 32) {
                    tiff_detail::swap_bytes_32(pixel_buffer.get(), total_samples);
                }
            } else if (need_byte_swap && image.sample_type == SampleType::Float32) {
                const size_t total_samples =
                    static_cast<size_t>(width) * static_cast<size_t>(height) * spp;
                tiff_detail::swap_bytes_32(pixel_buffer.get(), total_samples);
            } else if (need_byte_swap && image.sample_type == SampleType::Float64) {
                const size_t total_samples =
                    static_cast<size_t>(width) * static_cast<size_t>(height) * spp;
                tiff_detail::swap_bytes_64(pixel_buffer.get(), total_samples);
            }
        }

        if (photometric == PHOTOMETRIC_MINISWHITE &&
            image.sample_type == SampleType::Integer) {
            const size_t total_samples =
                static_cast<size_t>(width) * static_cast<size_t>(height) * spp;
            if (bits_per_sample == 8) {
                for (size_t i = 0; i < total_samples; ++i) {
                    pixel_buffer[i] = static_cast<uint8_t>(255u - pixel_buffer[i]);
                }
            } else if (bits_per_sample == 16) {
                uint16_t* samples = reinterpret_cast<uint16_t*>(pixel_buffer.get());
                for (size_t i = 0; i < total_samples; ++i) {
                    samples[i] = static_cast<uint16_t>(65535u - samples[i]);
                }
            } else if (bits_per_sample == 32) {
                uint32_t* samples = reinterpret_cast<uint32_t*>(pixel_buffer.get());
                for (size_t i = 0; i < total_samples; ++i) {
                    samples[i] = 0xFFFFFFFFu - samples[i];
                }
            }
        }

    } catch (...) {
        TIFFClose(tif);
        throw;
    }

    TIFFClose(tif);
    return image;
}

inline bool probe_tiff(const uint8_t* file_bytes, size_t file_byte_count,
                       uint32_t& width, uint32_t& height) {
    if (!file_bytes || file_byte_count == 0) {
        return false;
    }

    tiff_detail::MemoryTiffSource source{file_bytes, file_byte_count, 0};
    TIFF* tif = TIFFClientOpen(
        "mem", "rm", &source,
        tiff_detail::memory_read,
        tiff_detail::memory_write,
        tiff_detail::memory_seek,
        tiff_detail::memory_close,
        tiff_detail::memory_size,
        nullptr, nullptr);
    if (!tif) {
        return false;
    }

    uint32_t w = 0;
    uint32_t h = 0;
    const int ok_w = TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
    const int ok_h = TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
    TIFFClose(tif);
    if (!ok_w || !ok_h || w == 0 || h == 0) {
        return false;
    }
    width = w;
    height = h;
    return true;
}
