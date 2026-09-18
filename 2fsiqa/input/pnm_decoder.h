// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#pragma once
#include "image_info.h"
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace pnm_detail {

struct Cursor {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
};

inline bool at_end(const Cursor& c) {
    return c.pos >= c.size;
}

inline void skip_whitespace_and_comments(Cursor& c) {
    for (;;) {
        while (!at_end(c) && std::isspace(static_cast<unsigned char>(c.data[c.pos]))) {
            ++c.pos;
        }
        if (at_end(c) || c.data[c.pos] != '#') {
            return;
        }
        while (!at_end(c) && c.data[c.pos] != '\n' && c.data[c.pos] != '\r') {
            ++c.pos;
        }
    }
}

inline uint32_t read_uint(Cursor& c) {
    skip_whitespace_and_comments(c);
    if (at_end(c) || !std::isdigit(static_cast<unsigned char>(c.data[c.pos]))) {
        throw std::runtime_error("PNM: expected unsigned integer");
    }
    uint64_t value = 0;
    while (!at_end(c) && std::isdigit(static_cast<unsigned char>(c.data[c.pos]))) {
        value = value * 10u + static_cast<uint64_t>(c.data[c.pos] - '0');
        if (value > 0xFFFFFFFFull) {
            throw std::runtime_error("PNM: integer overflow");
        }
        ++c.pos;
    }
    return static_cast<uint32_t>(value);
}

inline std::string read_token(Cursor& c) {
    skip_whitespace_and_comments(c);
    if (at_end(c)) {
        throw std::runtime_error("PNM: unexpected end while reading token");
    }
    const size_t begin = c.pos;
    while (!at_end(c) && !std::isspace(static_cast<unsigned char>(c.data[c.pos])) &&
           c.data[c.pos] != '#') {
        ++c.pos;
    }
    return std::string(reinterpret_cast<const char*>(c.data + begin), c.pos - begin);
}

inline float read_ascii_float(Cursor& c) {
    skip_whitespace_and_comments(c);
    if (at_end(c)) {
        throw std::runtime_error("PNM: expected float");
    }
    char* end = nullptr;
    const char* start = reinterpret_cast<const char*>(c.data + c.pos);
    const float value = std::strtof(start, &end);
    if (end == start) {
        throw std::runtime_error("PNM: invalid float");
    }
    c.pos += static_cast<size_t>(end - start);
    return value;
}

inline int bits_for_maxval(uint32_t maxval) {
    if (maxval <= 1u) return 1;
    int bits = 0;
    uint32_t v = maxval;
    while (v > 0u) {
        ++bits;
        v >>= 1;
    }
    return bits;
}

inline int storage_depth_for_maxval(uint32_t maxval) {
    if (maxval <= 255u) return 8;
    if (maxval <= 65535u) return 16;
    return 32;
}

inline uint32_t storage_peak(int depth) {
    if (depth >= 32) return 0xFFFFFFFFu;
    if (depth <= 0) return 1u;
    return (1u << depth) - 1u;
}

inline uint32_t scale_sample(uint32_t sample, uint32_t maxval, uint32_t peak) {
    if (maxval == 0u) return 0u;
    if (maxval == peak) return std::min(sample, peak);
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(sample) * static_cast<uint64_t>(peak) +
         static_cast<uint64_t>(maxval) / 2u) /
        static_cast<uint64_t>(maxval));
}

inline void store_sample(uint8_t* dst, uint32_t value, int depth) {
    if (depth <= 8) {
        *dst = static_cast<uint8_t>(value > 255u ? 255u : value);
    } else if (depth <= 16) {
        const uint16_t v = static_cast<uint16_t>(value > 65535u ? 65535u : value);
        dst[0] = static_cast<uint8_t>(v & 0xffu);
        dst[1] = static_cast<uint8_t>((v >> 8) & 0xffu);
    } else {
        dst[0] = static_cast<uint8_t>(value & 0xffu);
        dst[1] = static_cast<uint8_t>((value >> 8) & 0xffu);
        dst[2] = static_cast<uint8_t>((value >> 16) & 0xffu);
        dst[3] = static_cast<uint8_t>((value >> 24) & 0xffu);
    }
}

inline uint32_t read_binary_sample_be(Cursor& c, int bytes) {
    if (c.pos + static_cast<size_t>(bytes) > c.size) {
        throw std::runtime_error("PNM: truncated binary sample data");
    }
    uint32_t value = 0;
    for (int i = 0; i < bytes; ++i) {
        value = (value << 8) | c.data[c.pos++];
    }
    return value;
}

inline float half_bits_to_float(uint16_t bits) {
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

enum class PnmKind {
    PbmAscii = 1,
    PgmAscii = 2,
    PpmAscii = 3,
    PbmBinary = 4,
    PgmBinary = 5,
    PpmBinary = 6,
    Pam = 7,
    PfmGray = 8,
    PfmRgb = 9,
    PhmGray = 10,
    PhmRgb = 11
};

struct Header {
    PnmKind kind = PnmKind::PgmBinary;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t maxval = 255;
    int channels = 1;
    bool has_alpha = false;
    bool is_float = false;
    bool is_half = false;
    float pfm_scale = 1.0f;
    bool pfm_little_endian = true;
};

inline Header parse_header(Cursor& c) {
    skip_whitespace_and_comments(c);
    if (c.pos + 2 > c.size || c.data[c.pos] != 'P') {
        throw std::runtime_error("PNM: missing P magic");
    }
    const char type_char = static_cast<char>(c.data[c.pos + 1]);
    c.pos += 2;

    Header h;
    switch (type_char) {
        case '1': h.kind = PnmKind::PbmAscii;  h.channels = 1; h.maxval = 1; break;
        case '2': h.kind = PnmKind::PgmAscii;  h.channels = 1; break;
        case '3': h.kind = PnmKind::PpmAscii;  h.channels = 3; break;
        case '4': h.kind = PnmKind::PbmBinary; h.channels = 1; h.maxval = 1; break;
        case '5': h.kind = PnmKind::PgmBinary; h.channels = 1; break;
        case '6': h.kind = PnmKind::PpmBinary; h.channels = 3; break;
        case '7': h.kind = PnmKind::Pam; break;
        case 'f': h.kind = PnmKind::PfmGray; h.channels = 1; h.is_float = true; break;
        case 'F': h.kind = PnmKind::PfmRgb;  h.channels = 3; h.is_float = true; break;
        case 'h': h.kind = PnmKind::PhmGray; h.channels = 1; h.is_float = true; h.is_half = true; break;
        case 'H': h.kind = PnmKind::PhmRgb;  h.channels = 3; h.is_float = true; h.is_half = true; break;
        default:
            throw std::runtime_error("PNM: unsupported type");
    }

    if (h.kind == PnmKind::Pam) {
        h.width = 0;
        h.height = 0;
        h.maxval = 0;
        int depth = 0;
        std::string tupltype = "BLACKANDWHITE";
        for (;;) {
            const std::string key = read_token(c);
            if (key == "ENDHDR") break;
            if (key == "WIDTH") {
                h.width = read_uint(c);
            } else if (key == "HEIGHT") {
                h.height = read_uint(c);
            } else if (key == "DEPTH") {
                depth = static_cast<int>(read_uint(c));
            } else if (key == "MAXVAL") {
                h.maxval = read_uint(c);
            } else if (key == "TUPLTYPE") {
                tupltype = read_token(c);
            } else {
                (void)read_token(c);
            }
        }
        if (h.width == 0 || h.height == 0 || h.maxval == 0 || depth <= 0) {
            throw std::runtime_error("PNM: incomplete PAM header");
        }
        if (tupltype == "BLACKANDWHITE" || tupltype == "GRAYSCALE") {
            h.channels = 1;
            h.has_alpha = (depth >= 2);
        } else if (tupltype == "RGB") {
            h.channels = 3;
            h.has_alpha = (depth >= 4);
        } else if (tupltype == "BLACKANDWHITE_ALPHA" || tupltype == "GRAYSCALE_ALPHA") {
            h.channels = 1;
            h.has_alpha = true;
        } else if (tupltype == "RGB_ALPHA") {
            h.channels = 3;
            h.has_alpha = true;
        } else {
            if (depth == 1) {
                h.channels = 1;
            } else if (depth == 2) {
                h.channels = 1;
                h.has_alpha = true;
            } else if (depth == 3) {
                h.channels = 3;
            } else if (depth >= 4) {
                h.channels = 3;
                h.has_alpha = true;
            } else {
                throw std::runtime_error("PNM: unsupported PAM depth");
            }
        }
        const int expected = h.channels + (h.has_alpha ? 1 : 0);
        if (depth < expected) {
            throw std::runtime_error("PNM: PAM DEPTH smaller than TUPLTYPE requires");
        }
    } else if (h.is_float) {
        h.width = read_uint(c);
        h.height = read_uint(c);
        h.pfm_scale = read_ascii_float(c);
        h.pfm_little_endian = (h.pfm_scale < 0.0f);
        if (h.pfm_scale < 0.0f) h.pfm_scale = -h.pfm_scale;
        if (h.pfm_scale == 0.0f) h.pfm_scale = 1.0f;
    } else {
        h.width = read_uint(c);
        h.height = read_uint(c);
        if (h.kind != PnmKind::PbmAscii && h.kind != PnmKind::PbmBinary) {
            h.maxval = read_uint(c);
        }
    }

    if (h.width == 0 || h.height == 0) {
        throw std::runtime_error("PNM: invalid dimensions");
    }
    if (!h.is_float && h.maxval == 0) {
        throw std::runtime_error("PNM: maxval out of supported range");
    }

    const uint64_t pixels = static_cast<uint64_t>(h.width) * static_cast<uint64_t>(h.height);
    if (pixels > (static_cast<uint64_t>(1) << 28)) {
        throw std::runtime_error("PNM: image too large");
    }

    if (!h.is_float) {
        skip_whitespace_and_comments(c);
    } else {
        if (!at_end(c) && (c.data[c.pos] == '\n' || c.data[c.pos] == '\r')) {
            if (c.data[c.pos] == '\r') {
                ++c.pos;
                if (!at_end(c) && c.data[c.pos] == '\n') ++c.pos;
            } else {
                ++c.pos;
            }
        }
    }
    return h;
}

inline void decode_pbm_binary(Cursor& c, const Header& h, uint8_t* out) {
    const size_t row_bytes = (static_cast<size_t>(h.width) + 7u) / 8u;
    for (uint32_t y = 0; y < h.height; ++y) {
        if (c.pos + row_bytes > c.size) {
            throw std::runtime_error("PNM: truncated PBM data");
        }
        for (uint32_t x = 0; x < h.width; ++x) {
            const size_t byte_index = c.pos + static_cast<size_t>(x / 8u);
            const int bit = 7 - static_cast<int>(x % 8u);
            const bool black = ((c.data[byte_index] >> bit) & 1u) != 0;
            out[static_cast<size_t>(y) * h.width + x] = black ? 0u : 255u;
        }
        c.pos += row_bytes;
    }
}

inline void decode_pbm_ascii(Cursor& c, const Header& h, uint8_t* out) {
    const size_t count = static_cast<size_t>(h.width) * static_cast<size_t>(h.height);
    for (size_t i = 0; i < count; ++i) {
        const uint32_t bit = read_uint(c);
        out[i] = (bit != 0u) ? 0u : 255u;
    }
}

inline void decode_integer_raster(Cursor& c, const Header& h, bool ascii,
                                  int out_depth, uint8_t* out) {
    const uint32_t peak = storage_peak(out_depth);
    const size_t bytes_per_sample = static_cast<size_t>((out_depth + 7) / 8);
    const int file_bytes = (h.maxval <= 255u) ? 1 : (h.maxval <= 65535u ? 2 : 4);
    const size_t spp = static_cast<size_t>(h.channels) + (h.has_alpha ? 1u : 0u);
    const size_t sample_count =
        static_cast<size_t>(h.width) * static_cast<size_t>(h.height) * spp;

    for (size_t i = 0; i < sample_count; ++i) {
        uint32_t raw = 0;
        if (ascii) {
            raw = read_uint(c);
        } else {
            raw = read_binary_sample_be(c, file_bytes);
        }
        if (raw > h.maxval) raw = h.maxval;
        const uint32_t scaled = scale_sample(raw, h.maxval, peak);
        store_sample(out + i * bytes_per_sample, scaled, out_depth);
    }
}

inline void decode_pfm_or_phm(Cursor& c, const Header& h, float* out) {
    const size_t spp = static_cast<size_t>(h.channels);
    const size_t sample_bytes = h.is_half ? 2u : 4u;
    const size_t need =
        static_cast<size_t>(h.width) * static_cast<size_t>(h.height) * spp * sample_bytes;
    if (c.pos + need > c.size) {
        throw std::runtime_error("PNM: truncated float raster");
    }

    for (uint32_t y = 0; y < h.height; ++y) {
        const uint32_t src_y = h.height - 1u - y;
        float* row = out + static_cast<size_t>(src_y) * static_cast<size_t>(h.width) * spp;
        for (size_t s = 0; s < static_cast<size_t>(h.width) * spp; ++s) {
            if (h.is_half) {
                uint8_t b[2] = {c.data[c.pos], c.data[c.pos + 1]};
                c.pos += 2;
                if (!h.pfm_little_endian) std::swap(b[0], b[1]);
                const uint16_t bits = static_cast<uint16_t>(b[0] | (static_cast<uint16_t>(b[1]) << 8));
                row[s] = half_bits_to_float(bits) * h.pfm_scale;
            } else {
                uint8_t b[4];
                std::memcpy(b, c.data + c.pos, 4);
                c.pos += 4;
                if (!h.pfm_little_endian) {
                    std::swap(b[0], b[3]);
                    std::swap(b[1], b[2]);
                }
                float v = 0.0f;
                std::memcpy(&v, b, 4);
                row[s] = v * h.pfm_scale;
            }
        }
    }
}

}  // namespace pnm_detail

inline bool is_pnm_signature(const uint8_t* bytes, size_t byte_count) {
    if (byte_count < 2) return false;
    if (bytes[0] != 'P') return false;
    const char t = static_cast<char>(bytes[1]);
    return t == '1' || t == '2' || t == '3' || t == '4' || t == '5' || t == '6' ||
           t == '7' || t == 'f' || t == 'F' || t == 'h' || t == 'H';
}

inline ImageInfo decode_pnm(const uint8_t* file_bytes, size_t file_byte_count,
                            std::unique_ptr<uint8_t[]>& pixel_buffer) {
    if (!file_bytes || file_byte_count < 3) {
        throw std::runtime_error("Empty or truncated PNM buffer");
    }
    if (!is_pnm_signature(file_bytes, file_byte_count)) {
        throw std::runtime_error("Not a PNM file");
    }

    pnm_detail::Cursor cursor{file_bytes, file_byte_count, 0};
    const pnm_detail::Header header = pnm_detail::parse_header(cursor);

    ImageInfo info;
    info.width = header.width;
    info.height = header.height;
    info.channels = header.channels;
    info.has_alpha = header.has_alpha;

    if (header.is_float) {
        info.sample_type = SampleType::Float32;
        info.bit_depth = 32;
        info.reported_bit_depth = header.is_half ? 10 : 23;
        const size_t spp =
            static_cast<size_t>(info.channels) + (info.has_alpha ? 1u : 0u);
        info.row_bytes = static_cast<size_t>(info.width) * spp * sizeof(float);
        info.pixel_bytes = info.row_bytes * static_cast<size_t>(info.height);
        pixel_buffer = std::make_unique_for_overwrite<uint8_t[]>(info.pixel_bytes);
        pnm_detail::decode_pfm_or_phm(cursor, header,
                                      reinterpret_cast<float*>(pixel_buffer.get()));
        return info;
    }

    const int out_depth = pnm_detail::storage_depth_for_maxval(header.maxval);
    info.sample_type = SampleType::Integer;
    info.bit_depth = out_depth;
    info.reported_bit_depth = pnm_detail::bits_for_maxval(header.maxval);
    if (info.reported_bit_depth < 1) info.reported_bit_depth = 1;

    const size_t spp =
        static_cast<size_t>(info.channels) + (info.has_alpha ? 1u : 0u);
    const size_t bytes_per_sample = static_cast<size_t>((out_depth + 7) / 8);
    info.row_bytes = static_cast<size_t>(info.width) * spp * bytes_per_sample;
    info.pixel_bytes = info.row_bytes * static_cast<size_t>(info.height);
    pixel_buffer = std::make_unique_for_overwrite<uint8_t[]>(info.pixel_bytes);

    switch (header.kind) {
        case pnm_detail::PnmKind::PbmAscii:
            pnm_detail::decode_pbm_ascii(cursor, header, pixel_buffer.get());
            break;
        case pnm_detail::PnmKind::PbmBinary:
            pnm_detail::decode_pbm_binary(cursor, header, pixel_buffer.get());
            break;
        case pnm_detail::PnmKind::PgmAscii:
        case pnm_detail::PnmKind::PpmAscii:
            pnm_detail::decode_integer_raster(cursor, header, true, out_depth,
                                              pixel_buffer.get());
            break;
        case pnm_detail::PnmKind::PgmBinary:
        case pnm_detail::PnmKind::PpmBinary:
        case pnm_detail::PnmKind::Pam:
            pnm_detail::decode_integer_raster(cursor, header, false, out_depth,
                                              pixel_buffer.get());
            break;
        default:
            throw std::runtime_error("PNM: internal kind error");
    }
    return info;
}

inline bool probe_pnm(const uint8_t* file_bytes, size_t file_byte_count,
                      uint32_t& width, uint32_t& height) {
    width = 0;
    height = 0;
    if (!file_bytes || file_byte_count < 3 || !is_pnm_signature(file_bytes, file_byte_count)) {
        return false;
    }
    try {
        pnm_detail::Cursor cursor{file_bytes, file_byte_count, 0};
        const pnm_detail::Header header = pnm_detail::parse_header(cursor);
        width = header.width;
        height = header.height;
        return width > 0 && height > 0;
    } catch (...) {
        return false;
    }
}
