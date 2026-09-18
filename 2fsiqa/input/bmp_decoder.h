// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#pragma once
#include "image_info.h"
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <cmath>

namespace bmp_detail {

inline uint16_t read_u16_le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

inline uint32_t read_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline int32_t read_i32_le(const uint8_t* p) {
    return static_cast<int32_t>(read_u32_le(p));
}

inline int mask_shift(uint32_t mask) {
    if (mask == 0) return 0;
    int shift = 0;
    while ((mask & 1u) == 0u) {
        mask >>= 1;
        ++shift;
    }
    return shift;
}

inline int mask_bits(uint32_t mask) {
    if (mask == 0) return 0;
    while ((mask & 1u) == 0u) mask >>= 1;
    int bits = 0;
    while ((mask & 1u) != 0u) {
        mask >>= 1;
        ++bits;
    }
    return bits;
}

inline uint8_t expand_channel(uint32_t pixel, uint32_t mask) {
    if (mask == 0) return 0;
    const int shift = mask_shift(mask);
    const int bits = mask_bits(mask);
    uint32_t v = (pixel & mask) >> shift;
    if (bits >= 8) {
        return static_cast<uint8_t>(v >> (bits - 8));
    }
    if (bits == 0) return 0;
    const uint32_t maxv = (1u << bits) - 1u;
    return static_cast<uint8_t>((v * 255u + maxv / 2u) / maxv);
}

enum class Compression : uint32_t {
    Rgb = 0,
    Rle8 = 1,
    Rle4 = 2,
    Bitfields = 3,
    Jpeg = 4,
    Png = 5,
    AlphaBitfields = 6
};

struct Header {
    uint32_t file_size = 0;
    uint32_t pixel_offset = 0;
    uint32_t dib_size = 0;
    int32_t width = 0;
    int32_t height_signed = 0;
    uint32_t height = 0;
    bool top_down = false;
    uint16_t planes = 1;
    uint16_t bpp = 0;
    Compression compression = Compression::Rgb;
    uint32_t image_size = 0;
    uint32_t colors_used = 0;
    uint32_t red_mask = 0;
    uint32_t green_mask = 0;
    uint32_t blue_mask = 0;
    uint32_t alpha_mask = 0;
    bool has_alpha = false;
    int32_t colorspace = 0;
    double red_x = 0, red_y = 0;
    double green_x = 0, green_y = 0;
    double blue_x = 0, blue_y = 0;
    double gamma = 0;
    bool has_chromaticities = false;
    bool has_gamma = false;
    uint32_t profile_offset = 0;
    uint32_t profile_size = 0;
};

inline size_t row_stride_bytes(uint32_t width, uint16_t bpp) {
    const uint64_t bits = static_cast<uint64_t>(width) * static_cast<uint64_t>(bpp);
    return static_cast<size_t>(((bits + 31u) / 32u) * 4u);
}

inline Header parse_header(const uint8_t* data, size_t size) {
    if (size < 14 + 12) {
        throw std::runtime_error("BMP: truncated file header");
    }
    if (data[0] != 'B' || data[1] != 'M') {
        throw std::runtime_error("BMP: not a Windows BM bitmap");
    }

    Header h;
    h.file_size = read_u32_le(data + 2);
    h.pixel_offset = read_u32_le(data + 10);
    h.dib_size = read_u32_le(data + 14);

    if (h.dib_size == 12) {
        if (size < 26) throw std::runtime_error("BMP: truncated OS/2 header");
        h.width = static_cast<int32_t>(read_u16_le(data + 18));
        h.height_signed = static_cast<int32_t>(read_u16_le(data + 20));
        h.planes = read_u16_le(data + 22);
        h.bpp = read_u16_le(data + 24);
        h.compression = Compression::Rgb;
    } else if (h.dib_size >= 40) {
        if (size < 14 + h.dib_size) {
            throw std::runtime_error("BMP: truncated DIB header");
        }
        h.width = read_i32_le(data + 18);
        h.height_signed = read_i32_le(data + 22);
        h.planes = read_u16_le(data + 26);
        h.bpp = read_u16_le(data + 28);
        h.compression = static_cast<Compression>(read_u32_le(data + 30));
        h.image_size = read_u32_le(data + 34);
        h.colors_used = read_u32_le(data + 46);

        const bool need_masks =
            h.dib_size >= 52 ||
            h.compression == Compression::Bitfields ||
            h.compression == Compression::AlphaBitfields;

        if (need_masks) {
            if (h.dib_size >= 52 && size >= 14 + 52) {
                h.red_mask = read_u32_le(data + 54);
                h.green_mask = read_u32_le(data + 58);
                h.blue_mask = read_u32_le(data + 62);
            } else if (size >= 14 + 40 + 12) {
                h.red_mask = read_u32_le(data + 54);
                h.green_mask = read_u32_le(data + 58);
                h.blue_mask = read_u32_le(data + 62);
            } else {
                throw std::runtime_error("BMP: missing bitfield masks");
            }
        }

        if ((h.dib_size >= 56 || h.compression == Compression::AlphaBitfields) &&
            size >= 14 + 56) {
            h.alpha_mask = read_u32_le(data + 66);
        }

        if (h.dib_size >= 108 && size >= 14 + 108) {
            if (h.alpha_mask == 0) {
                h.alpha_mask = read_u32_le(data + 66);
            }
            h.colorspace = read_i32_le(data + 70);

            constexpr double kDenom = 0x40000000;
            const double rx = static_cast<double>(read_u32_le(data + 74)) / kDenom;
            const double ry = static_cast<double>(read_u32_le(data + 78)) / kDenom;
            const double rz = static_cast<double>(read_u32_le(data + 82)) / kDenom;
            const double gx = static_cast<double>(read_u32_le(data + 86)) / kDenom;
            const double gy = static_cast<double>(read_u32_le(data + 90)) / kDenom;
            const double gz = static_cast<double>(read_u32_le(data + 94)) / kDenom;
            const double bx = static_cast<double>(read_u32_le(data + 98)) / kDenom;
            const double by = static_cast<double>(read_u32_le(data + 102)) / kDenom;
            const double bz = static_cast<double>(read_u32_le(data + 106)) / kDenom;

            const double gamma_r = static_cast<double>(read_u32_le(data + 110)) / 0x10000;
            const double gamma_g = static_cast<double>(read_u32_le(data + 114)) / 0x10000;
            const double gamma_b = static_cast<double>(read_u32_le(data + 118)) / 0x10000;

            if (h.colorspace == 0) {
                auto normalize = [](double x, double y, double z, double& ox, double& oy) {
                    const double s = x + y + z;
                    if (s > 0.0) {
                        ox = x / s;
                        oy = y / s;
                    }
                };
                normalize(rx, ry, rz, h.red_x, h.red_y);
                normalize(gx, gy, gz, h.green_x, h.green_y);
                normalize(bx, by, bz, h.blue_x, h.blue_y);
                h.has_chromaticities = (h.red_x > 0.0 && h.green_x > 0.0 && h.blue_x > 0.0);
                h.gamma = (gamma_r + gamma_g + gamma_b) / 3.0;
                h.has_gamma = (h.gamma > 0.0);
            }
        }

        if (h.dib_size >= 124 && size >= 14 + 124) {
            h.profile_offset = read_u32_le(data + 126);
            h.profile_size = read_u32_le(data + 130);
        }
    } else {
        throw std::runtime_error("BMP: unsupported DIB header size");
    }

    if (h.width <= 0) {
        throw std::runtime_error("BMP: invalid width");
    }
    if (h.height_signed == 0) {
        throw std::runtime_error("BMP: invalid height");
    }
    h.top_down = h.height_signed < 0;
    h.height = static_cast<uint32_t>(h.height_signed < 0 ? -h.height_signed
                                                          : h.height_signed);

    if (h.planes != 1) {
        throw std::runtime_error("BMP: planes must be 1");
    }

    switch (h.compression) {
        case Compression::Rgb:
        case Compression::Bitfields:
        case Compression::AlphaBitfields:
        case Compression::Rle8:
        case Compression::Rle4:
            break;
        case Compression::Jpeg:
            throw std::runtime_error("BMP: embedded JPEG is not supported");
        case Compression::Png:
            throw std::runtime_error("BMP: embedded PNG is not supported");
        default:
            throw std::runtime_error("BMP: unknown compression");
    }

    if (h.compression == Compression::Rle8 && h.bpp != 8) {
        throw std::runtime_error("BMP: RLE8 requires 8 bpp");
    }
    if (h.compression == Compression::Rle4 && h.bpp != 4) {
        throw std::runtime_error("BMP: RLE4 requires 4 bpp");
    }
    if ((h.compression == Compression::Rle8 || h.compression == Compression::Rle4) &&
        h.top_down) {
        throw std::runtime_error("BMP: RLE with top-down orientation is not supported");
    }

    if (h.bpp != 1 && h.bpp != 4 && h.bpp != 8 && h.bpp != 16 && h.bpp != 24 &&
        h.bpp != 32) {
        throw std::runtime_error("BMP: unsupported bits per pixel");
    }

    if (h.compression == Compression::Rgb) {
        if (h.bpp == 16) {
            h.red_mask = 0x7C00u;
            h.green_mask = 0x03E0u;
            h.blue_mask = 0x001Fu;
        } else if (h.bpp == 32) {
            h.red_mask = 0x00FF0000u;
            h.green_mask = 0x0000FF00u;
            h.blue_mask = 0x000000FFu;
            h.alpha_mask = 0;
        }
    }

    h.has_alpha =
        (h.alpha_mask != 0) &&
        (h.compression == Compression::Bitfields ||
         h.compression == Compression::AlphaBitfields);

    if (h.pixel_offset > size) {
        throw std::runtime_error("BMP: pixel offset past end of file");
    }

    const uint64_t pixels =
        static_cast<uint64_t>(h.width) * static_cast<uint64_t>(h.height);
    if (pixels > (static_cast<uint64_t>(1) << 28)) {
        throw std::runtime_error("BMP: image too large");
    }

    return h;
}

inline void detect_bi_rgb_32_alpha(const uint8_t* raster, size_t stride,
                                   Header& h) {
    if (h.compression != Compression::Rgb || h.bpp != 32 || h.dib_size != 40) {
        return;
    }
    if (h.has_alpha) return;

    for (uint32_t y = 0; y < h.height; ++y) {
        const uint8_t* row = raster + static_cast<size_t>(y) * stride;
        for (uint32_t x = 0; x < static_cast<uint32_t>(h.width); ++x) {
            if (row[static_cast<size_t>(x) * 4u + 3u] != 0) {
                h.alpha_mask = 0xFF000000u;
                h.has_alpha = true;
                return;
            }
        }
    }
}

inline std::vector<uint8_t> read_palette(const uint8_t* data, size_t size,
                                         const Header& h) {
    std::vector<uint8_t> palette;
    if (h.bpp > 8) return palette;

    uint32_t colors = h.colors_used;
    if (colors == 0) {
        colors = 1u << h.bpp;
    }
    if (colors > 256u) colors = 256u;

    const size_t entry_size = (h.dib_size == 12) ? 3u : 4u;
    size_t palette_offset = 14u + static_cast<size_t>(h.dib_size);
    if (h.dib_size == 40 &&
        (h.compression == Compression::Bitfields ||
         h.compression == Compression::AlphaBitfields)) {
        palette_offset += (h.compression == Compression::AlphaBitfields) ? 16u : 12u;
    }
    const size_t need = palette_offset + static_cast<size_t>(colors) * entry_size;
    if (need > size) {
        throw std::runtime_error("BMP: truncated palette");
    }

    palette.resize(static_cast<size_t>(colors) * 3u);
    for (uint32_t i = 0; i < colors; ++i) {
        const uint8_t* e = data + palette_offset + static_cast<size_t>(i) * entry_size;
        palette[i * 3u + 0] = e[2];
        palette[i * 3u + 1] = e[1];
        palette[i * 3u + 2] = e[0];
    }
    return palette;
}

inline std::vector<uint8_t> decode_rle(const uint8_t* data, size_t size,
                                       size_t offset, const Header& h) {
    const size_t pixel_count =
        static_cast<size_t>(h.width) * static_cast<size_t>(h.height);
    std::vector<uint8_t> indices(pixel_count, 0);

    size_t pos = offset;
    uint32_t x = 0;
    uint32_t y = 0;
    auto put = [&](uint8_t value) {
        if (y >= h.height) return;
        if (x < static_cast<uint32_t>(h.width)) {
            indices[static_cast<size_t>(y) * static_cast<size_t>(h.width) + x] = value;
        }
        ++x;
    };

    while (pos < size && y < h.height) {
        if (pos + 1 >= size) break;
        const int count = data[pos++];
        const int second = data[pos++];

        if (count > 0) {
            if (h.compression == Compression::Rle8) {
                for (int i = 0; i < count; ++i) put(static_cast<uint8_t>(second));
            } else {
                for (int i = 0; i < count; ++i) {
                    const uint8_t nibble = (i & 1)
                        ? static_cast<uint8_t>(second & 0x0f)
                        : static_cast<uint8_t>((second >> 4) & 0x0f);
                    put(nibble);
                }
            }
        } else {
            if (second == 0) {
                x = 0;
                ++y;
            } else if (second == 1) {
                break;
            } else if (second == 2) {
                if (pos + 1 >= size) break;
                x += data[pos++];
                y += data[pos++];
            } else {
                const int absolute = second;
                if (h.compression == Compression::Rle8) {
                    for (int i = 0; i < absolute; ++i) {
                        if (pos >= size) break;
                        put(data[pos++]);
                    }
                    if (absolute & 1) {
                        if (pos < size) ++pos;
                    }
                } else {
                    for (int i = 0; i < absolute; ++i) {
                        if ((i & 1) == 0) {
                            if (pos >= size) break;
                            const uint8_t byte = data[pos++];
                            put(static_cast<uint8_t>((byte >> 4) & 0x0f));
                            if (i + 1 < absolute) {
                                put(static_cast<uint8_t>(byte & 0x0f));
                                ++i;
                            }
                        }
                    }
                    const int bytes_read = (absolute + 1) / 2;
                    if ((bytes_read & 1) != 0 && pos < size) ++pos;
                }
            }
        }
    }
    return indices;
}

inline void expand_indices(const uint8_t* indices, const Header& h,
                           const std::vector<uint8_t>& palette, uint8_t* out_rgb) {
    const uint32_t colors = static_cast<uint32_t>(palette.size() / 3u);
    for (uint32_t y = 0; y < h.height; ++y) {
        const uint32_t src_y = h.top_down ? y : (h.height - 1u - y);
        const uint8_t* src =
            indices + static_cast<size_t>(src_y) * static_cast<size_t>(h.width);
        uint8_t* dst = out_rgb + static_cast<size_t>(y) * static_cast<size_t>(h.width) * 3u;
        for (uint32_t x = 0; x < static_cast<uint32_t>(h.width); ++x) {
            uint32_t idx = src[x];
            if (idx >= colors) idx = 0;
            dst[x * 3u + 0] = palette[idx * 3u + 0];
            dst[x * 3u + 1] = palette[idx * 3u + 1];
            dst[x * 3u + 2] = palette[idx * 3u + 2];
        }
    }
}

inline void decode_paletted_raw(const uint8_t* src_rows, size_t stride,
                                const Header& h, const std::vector<uint8_t>& palette,
                                uint8_t* out_rgb) {
    const uint32_t colors = static_cast<uint32_t>(palette.size() / 3u);
    for (uint32_t y = 0; y < h.height; ++y) {
        const uint32_t src_y = h.top_down ? y : (h.height - 1u - y);
        const uint8_t* row = src_rows + static_cast<size_t>(src_y) * stride;
        uint8_t* dst = out_rgb + static_cast<size_t>(y) * static_cast<size_t>(h.width) * 3u;

        if (h.bpp == 8) {
            for (uint32_t x = 0; x < static_cast<uint32_t>(h.width); ++x) {
                uint32_t idx = row[x];
                if (idx >= colors) idx = 0;
                dst[x * 3u + 0] = palette[idx * 3u + 0];
                dst[x * 3u + 1] = palette[idx * 3u + 1];
                dst[x * 3u + 2] = palette[idx * 3u + 2];
            }
        } else if (h.bpp == 4) {
            for (uint32_t x = 0; x < static_cast<uint32_t>(h.width); ++x) {
                const uint8_t packed = row[x / 2u];
                uint32_t idx = (x & 1u) ? (packed & 0x0Fu) : ((packed >> 4) & 0x0Fu);
                if (idx >= colors) idx = 0;
                dst[x * 3u + 0] = palette[idx * 3u + 0];
                dst[x * 3u + 1] = palette[idx * 3u + 1];
                dst[x * 3u + 2] = palette[idx * 3u + 2];
            }
        } else {
            for (uint32_t x = 0; x < static_cast<uint32_t>(h.width); ++x) {
                const uint8_t packed = row[x / 8u];
                const int bit = 7 - static_cast<int>(x % 8u);
                uint32_t idx = (packed >> bit) & 1u;
                if (idx >= colors) idx = 0;
                dst[x * 3u + 0] = palette[idx * 3u + 0];
                dst[x * 3u + 1] = palette[idx * 3u + 1];
                dst[x * 3u + 2] = palette[idx * 3u + 2];
            }
        }
    }
}

inline void decode_truecolor(const uint8_t* src_rows, size_t stride,
                             const Header& h, uint8_t* out, bool with_alpha) {
    const size_t spp = with_alpha ? 4u : 3u;
    for (uint32_t y = 0; y < h.height; ++y) {
        const uint32_t src_y = h.top_down ? y : (h.height - 1u - y);
        const uint8_t* row = src_rows + static_cast<size_t>(src_y) * stride;
        uint8_t* dst = out + static_cast<size_t>(y) * static_cast<size_t>(h.width) * spp;

        if (h.bpp == 24) {
            for (uint32_t x = 0; x < static_cast<uint32_t>(h.width); ++x) {
                const uint8_t* p = row + static_cast<size_t>(x) * 3u;
                dst[x * 3u + 0] = p[2];
                dst[x * 3u + 1] = p[1];
                dst[x * 3u + 2] = p[0];
            }
        } else if (h.bpp == 32) {
            for (uint32_t x = 0; x < static_cast<uint32_t>(h.width); ++x) {
                const uint32_t pixel = read_u32_le(row + static_cast<size_t>(x) * 4u);
                dst[x * spp + 0] = expand_channel(pixel, h.red_mask);
                dst[x * spp + 1] = expand_channel(pixel, h.green_mask);
                dst[x * spp + 2] = expand_channel(pixel, h.blue_mask);
                if (with_alpha) {
                    dst[x * spp + 3] = expand_channel(pixel, h.alpha_mask);
                }
            }
        } else if (h.bpp == 16) {
            for (uint32_t x = 0; x < static_cast<uint32_t>(h.width); ++x) {
                const uint32_t pixel = read_u16_le(row + static_cast<size_t>(x) * 2u);
                dst[x * spp + 0] = expand_channel(pixel, h.red_mask);
                dst[x * spp + 1] = expand_channel(pixel, h.green_mask);
                dst[x * spp + 2] = expand_channel(pixel, h.blue_mask);
                if (with_alpha) {
                    dst[x * spp + 3] = expand_channel(pixel, h.alpha_mask);
                }
            }
        }
    }
}

inline void collect_icc(const uint8_t* data, size_t size, const Header& h,
                        std::vector<uint8_t>& out) {
    out.clear();
    if (h.colorspace != 0x4D424544 || h.profile_offset == 0 || h.profile_size == 0) {
        return;
    }
    const size_t start = 14u + static_cast<size_t>(h.profile_offset);
    if (start + h.profile_size > size) return;

    size_t profile_len = h.profile_size;
    if (profile_len >= 4) {
        const uint32_t declared =
            (static_cast<uint32_t>(data[start]) << 24) |
            (static_cast<uint32_t>(data[start + 1]) << 16) |
            (static_cast<uint32_t>(data[start + 2]) << 8) |
            static_cast<uint32_t>(data[start + 3]);
        if (declared > 0 && declared < profile_len) {
            profile_len = declared;
        }
    }
    out.assign(data + start, data + start + profile_len);
}

}  // namespace bmp_detail

inline bool is_bmp_signature(const uint8_t* bytes, size_t byte_count) {
    if (byte_count < 2) return false;
    return bytes[0] == 'B' && bytes[1] == 'M';
}

inline ImageInfo decode_bmp(const uint8_t* file_bytes, size_t file_byte_count,
                            std::unique_ptr<uint8_t[]>& pixel_buffer) {
    if (!file_bytes || file_byte_count < 26) {
        throw std::runtime_error("Empty or truncated BMP buffer");
    }
    if (!is_bmp_signature(file_bytes, file_byte_count)) {
        throw std::runtime_error("Not a BMP file");
    }

    bmp_detail::Header header =
        bmp_detail::parse_header(file_bytes, file_byte_count);

    const std::vector<uint8_t> palette =
        bmp_detail::read_palette(file_bytes, file_byte_count, header);

    ImageInfo info;
    info.width = static_cast<uint32_t>(header.width);
    info.height = header.height;
    info.bit_depth = 8;
    info.reported_bit_depth = 8;
    info.sample_type = SampleType::Integer;
    info.channels = 3;

    if (header.compression == bmp_detail::Compression::Rle8 ||
        header.compression == bmp_detail::Compression::Rle4) {
        if (palette.empty()) {
            throw std::runtime_error("BMP: RLE requires palette");
        }
        const std::vector<uint8_t> indices = bmp_detail::decode_rle(
            file_bytes, file_byte_count, header.pixel_offset, header);
        info.has_alpha = false;
        info.row_bytes = static_cast<size_t>(info.width) * 3u;
        info.pixel_bytes = info.row_bytes * static_cast<size_t>(info.height);
        pixel_buffer = std::make_unique_for_overwrite<uint8_t[]>(info.pixel_bytes);
        bmp_detail::expand_indices(indices.data(), header, palette, pixel_buffer.get());
    } else {
        const size_t stride =
            bmp_detail::row_stride_bytes(static_cast<uint32_t>(header.width), header.bpp);
        const size_t raster_bytes = stride * static_cast<size_t>(header.height);
        if (header.pixel_offset + raster_bytes > file_byte_count) {
            throw std::runtime_error("BMP: truncated pixel data");
        }
        const uint8_t* raster = file_bytes + header.pixel_offset;

        if (header.bpp <= 8) {
            info.has_alpha = false;
            if (palette.empty()) {
                throw std::runtime_error("BMP: missing palette");
            }
            info.row_bytes = static_cast<size_t>(info.width) * 3u;
            info.pixel_bytes = info.row_bytes * static_cast<size_t>(info.height);
            pixel_buffer = std::make_unique_for_overwrite<uint8_t[]>(info.pixel_bytes);
            bmp_detail::decode_paletted_raw(raster, stride, header, palette,
                                            pixel_buffer.get());
        } else {
            bmp_detail::detect_bi_rgb_32_alpha(raster, stride, header);
            info.has_alpha = header.has_alpha;
            const size_t spp = 3u + (info.has_alpha ? 1u : 0u);
            info.row_bytes = static_cast<size_t>(info.width) * spp;
            info.pixel_bytes = info.row_bytes * static_cast<size_t>(info.height);
            pixel_buffer = std::make_unique_for_overwrite<uint8_t[]>(info.pixel_bytes);
            bmp_detail::decode_truecolor(raster, stride, header, pixel_buffer.get(),
                                         info.has_alpha);
        }
    }

    if (header.has_chromaticities) {
        info.has_chromaticities = true;
        info.red_x = header.red_x;
        info.red_y = header.red_y;
        info.green_x = header.green_x;
        info.green_y = header.green_y;
        info.blue_x = header.blue_x;
        info.blue_y = header.blue_y;
        info.white_x = 0.3127;
        info.white_y = 0.3290;
    }
    if (header.has_gamma) {
        info.has_gamma = true;
        info.gamma = header.gamma;
    }
    bmp_detail::collect_icc(file_bytes, file_byte_count, header, info.icc_profile);
    return info;
}

inline bool probe_bmp(const uint8_t* file_bytes, size_t file_byte_count,
                      uint32_t& width, uint32_t& height) {
    width = 0;
    height = 0;
    if (!file_bytes || file_byte_count < 26 || !is_bmp_signature(file_bytes, file_byte_count)) {
        return false;
    }
    try {
        const bmp_detail::Header header =
            bmp_detail::parse_header(file_bytes, file_byte_count);
        width = static_cast<uint32_t>(header.width);
        height = header.height;
        return width > 0 && height > 0;
    } catch (...) {
        return false;
    }
}
