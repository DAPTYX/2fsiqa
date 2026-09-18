// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#include "cmm.h"
#include "embedded_assets.h"
#include <lcms2.h>
#if defined(TOOFSIQA_HAS_LCMS2_FAST_FLOAT)
#include <lcms2_fast_float.h>
#endif
#if defined(TOOFSIQA_HAS_LCMS2_THREADED)
#include <lcms2_threaded.h>
#endif
#include <stdexcept>
#include <vector>
#include <array>
#include <algorithm>
#include <mutex>
#include <thread>
#include <future>
#include <cmath>
#include <cstring>

namespace {

std::once_flag g_lcms2_plugins_once;

void register_lcms2_plugins() {
#if defined(TOOFSIQA_HAS_LCMS2_FAST_FLOAT)
    cmsPlugin(cmsFastFloatExtensions());
#endif
#if defined(TOOFSIQA_HAS_LCMS2_THREADED)
    cmsPlugin(cmsThreadedExtensions(CMS_THREADED_GUESS_MAX_THREADS, 0));
#endif
}

void ensure_lcms2_plugins() {
    std::call_once(g_lcms2_plugins_once, register_lcms2_plugins);
}

cmsHPROFILE g_destination_rgb_g22 = nullptr;
cmsHPROFILE g_destination_rgb_g24 = nullptr;
cmsHPROFILE g_destination_rgb_linear = nullptr;
cmsHPROFILE g_destination_gray = nullptr;
cmsHPROFILE g_destination_gray_linear = nullptr;
cmsHPROFILE g_fallback_srgb = nullptr;
cmsHPROFILE g_fallback_srgb_gray = nullptr;
std::once_flag g_destination_rgb_g22_once;
std::once_flag g_destination_rgb_g24_once;
std::once_flag g_destination_rgb_linear_once;
std::once_flag g_destination_gray_once;
std::once_flag g_destination_gray_linear_once;
std::once_flag g_fallback_srgb_once;
std::once_flag g_fallback_srgb_gray_once;

void init_destination_rgb_g22() {
    g_destination_rgb_g22 = cmsOpenProfileFromMem(
        embedded::_2FSCP_26_AllReal_g2_2_v4_data,
        static_cast<cmsUInt32Number>(embedded::_2FSCP_26_AllReal_g2_2_v4_size));
    if (!g_destination_rgb_g22) {
        throw std::runtime_error("Failed to open embedded destination ICC: 2FSCP-26_AllReal_g2.2_v4");
    }
}

void init_destination_rgb_g24() {
    g_destination_rgb_g24 = cmsOpenProfileFromMem(
        embedded::_2FSCP_26_AllReal_g2_4_v4_data,
        static_cast<cmsUInt32Number>(embedded::_2FSCP_26_AllReal_g2_4_v4_size));
    if (!g_destination_rgb_g24) {
        throw std::runtime_error("Failed to open embedded destination ICC: 2FSCP-26_AllReal_g2.4_v4");
    }
}

void init_destination_rgb_linear() {
    g_destination_rgb_linear = cmsOpenProfileFromMem(
        embedded::_2FSCP_26_AllReal_linear_v4_data,
        static_cast<cmsUInt32Number>(embedded::_2FSCP_26_AllReal_linear_v4_size));
    if (!g_destination_rgb_linear) {
        throw std::runtime_error("Failed to open embedded destination ICC: 2FSCP-26_AllReal_linear_v4");
    }
}

void init_destination_gray() {
    g_destination_gray = cmsOpenProfileFromMem(
        embedded::_2FSCP_26_Grayscale_g2_2_v4_data,
        static_cast<cmsUInt32Number>(embedded::_2FSCP_26_Grayscale_g2_2_v4_size));
    if (!g_destination_gray) {
        throw std::runtime_error("Failed to open embedded destination ICC: 2FSCP-26_Grayscale_g2.2_v4");
    }
}

void init_destination_gray_linear() {
    g_destination_gray_linear = cmsOpenProfileFromMem(
        embedded::_2FSCP_26_Grayscale_linear_v4_data,
        static_cast<cmsUInt32Number>(embedded::_2FSCP_26_Grayscale_linear_v4_size));
    if (!g_destination_gray_linear) {
        throw std::runtime_error("Failed to open embedded destination ICC: 2FSCP-26_Grayscale_linear_v4");
    }
}

void init_fallback_srgb() {
    g_fallback_srgb = cmsOpenProfileFromMem(
        embedded::_2FSCP_26_sRGB_v4_data,
        static_cast<cmsUInt32Number>(embedded::_2FSCP_26_sRGB_v4_size));
    if (!g_fallback_srgb) {
        throw std::runtime_error("Failed to open embedded fallback sRGB ICC: 2FSCP-26_sRGB_v4");
    }
}

void init_fallback_srgb_gray() {
    g_fallback_srgb_gray = cmsOpenProfileFromMem(
        embedded::_2FSCP_26_sRGB_gray_v4_data,
        static_cast<cmsUInt32Number>(embedded::_2FSCP_26_sRGB_gray_v4_size));
    if (!g_fallback_srgb_gray) {
        throw std::runtime_error("Failed to open embedded fallback sRGB gray ICC: 2FSCP-26_sRGB-gray_v4");
    }
}

cmsHPROFILE destination_profile(int channels, WorkingTransfer transfer) {
    if (transfer == WorkingTransfer::Linear) {
        if (channels == 1) {
            std::call_once(g_destination_gray_linear_once, init_destination_gray_linear);
            return g_destination_gray_linear;
        }
        std::call_once(g_destination_rgb_linear_once, init_destination_rgb_linear);
        return g_destination_rgb_linear;
    }
    if (transfer == WorkingTransfer::Gamma24) {
        if (channels == 1) {
            std::call_once(g_destination_gray_once, init_destination_gray);
            return g_destination_gray;
        }
        std::call_once(g_destination_rgb_g24_once, init_destination_rgb_g24);
        return g_destination_rgb_g24;
    }
    if (channels == 1) {
        std::call_once(g_destination_gray_once, init_destination_gray);
        return g_destination_gray;
    }
    std::call_once(g_destination_rgb_g22_once, init_destination_rgb_g22);
    return g_destination_rgb_g22;
}

cmsHPROFILE fallback_srgb(int channels) {
    if (channels == 1) {
        std::call_once(g_fallback_srgb_gray_once, init_fallback_srgb_gray);
        return g_fallback_srgb_gray;
    }
    std::call_once(g_fallback_srgb_once, init_fallback_srgb);
    return g_fallback_srgb;
}

cmsHPROFILE create_profile_from_chrm(const ImageInfo& info) {
    cmsCIExyY white;
    white.x = info.white_x;
    white.y = info.white_y;
    white.Y = 1.0;

    cmsCIExyYTRIPLE primaries;
    primaries.Red.x   = info.red_x;
    primaries.Red.y   = info.red_y;
    primaries.Red.Y   = 1.0;
    primaries.Green.x = info.green_x;
    primaries.Green.y = info.green_y;
    primaries.Green.Y = 1.0;
    primaries.Blue.x  = info.blue_x;
    primaries.Blue.y  = info.blue_y;
    primaries.Blue.Y  = 1.0;

    cmsToneCurve* curve = cmsBuildGamma(nullptr, 1.0 / info.gamma);
    if (!curve) {
        throw std::runtime_error("Failed to build gamma curve");
    }
    cmsToneCurve* curves[3] = {curve, curve, curve};

    cmsHPROFILE h = cmsCreateRGBProfile(&white, &primaries, curves);
    cmsFreeToneCurve(curve);
    if (!h) {
        throw std::runtime_error("Failed to create RGB profile from cHRM");
    }
    return h;
}

cmsHPROFILE create_gray_profile_from_gama(const ImageInfo& info) {
    cmsCIExyY white;
    white.x = 0.3127;
    white.y = 0.3290;
    white.Y = 1.0;

    cmsToneCurve* curve = cmsBuildGamma(nullptr, 1.0 / info.gamma);
    if (!curve) {
        throw std::runtime_error("Failed to build gray gamma curve");
    }

    cmsHPROFILE h = cmsCreateGrayProfile(&white, curve);
    cmsFreeToneCurve(curve);
    if (!h) {
        throw std::runtime_error("Failed to create gray profile from gAMA");
    }
    return h;
}

struct OwnedProfile {
    cmsHPROFILE handle = nullptr;
    bool owned = false;
};

bool usable_gamma(const ImageInfo& info) {
    return info.has_gamma && info.gamma > 0.0;
}

OwnedProfile resolve_source_profile(const ImageInfo& info) {
    if (!info.icc_profile.empty()) {
        cmsHPROFILE h = cmsOpenProfileFromMem(
            info.icc_profile.data(),
            static_cast<cmsUInt32Number>(info.icc_profile.size()));
        if (h) {
            return {h, true};
        }
    }

    if (info.channels == 1) {
        if (usable_gamma(info)) {
            return {create_gray_profile_from_gama(info), true};
        }
        return {fallback_srgb(1), false};
    }

    if (info.has_chromaticities && usable_gamma(info)) {
        return {create_profile_from_chrm(info), true};
    }

    return {fallback_srgb(3), false};
}

size_t sample_bytes(int bit_depth) {
    if (bit_depth <= 0) {
        return 1u;
    }
    return static_cast<size_t>((bit_depth + 7) / 8);
}

double sample_peak(int bit_depth) {
    if (bit_depth <= 0) {
        return 1.0;
    }
    if (bit_depth >= 32) {
        return 4294967295.0;
    }
    return static_cast<double>((1u << bit_depth) - 1u);
}

cmsUInt32Number select_double_format(int channels) {
    return (channels == 1) ? TYPE_GRAY_DBL : TYPE_RGB_DBL;
}


uint32_t read_integer_sample(const uint8_t* bytes, size_t nbytes) {
    uint32_t value = 0;
    for (size_t i = 0; i < nbytes; ++i) {
        value |= static_cast<uint32_t>(bytes[i]) << (8u * static_cast<uint32_t>(i));
    }
    return value;
}

cmsUInt32Number select_float_format(int channels) {
    return (channels == 1) ? TYPE_GRAY_FLT : TYPE_RGB_FLT;
}

void expand_integer_range_to_float(const uint8_t* pixels, const ImageInfo& info,
                                   float* out, size_t begin, size_t end) {
    const size_t nbytes = sample_bytes(info.bit_depth);
    const size_t spp = source_samples_per_pixel(info);
    const size_t bytes_per_pixel = spp * nbytes;
    const double inv_peak = 1.0 / sample_peak(info.bit_depth);

    for (size_t i = begin; i < end; ++i) {
        const uint8_t* src = pixels + i * bytes_per_pixel;
        float* dst = out + i * spp;
        for (size_t s = 0; s < spp; ++s) {
            const uint32_t raw = read_integer_sample(src + s * nbytes, nbytes);
            dst[s] = static_cast<float>(static_cast<double>(raw) * inv_peak);
        }
    }
}

void expand_float32_source_to_float(const uint8_t* pixels, const ImageInfo& info,
                                    float* out, size_t begin, size_t end) {
    const size_t spp = source_samples_per_pixel(info);
    const float* src = reinterpret_cast<const float*>(pixels);
    for (size_t i = begin; i < end; ++i) {
        for (size_t s = 0; s < spp; ++s) {
            out[i * spp + s] = src[i * spp + s];
        }
    }
}

void expand_float64_source_to_float(const uint8_t* pixels, const ImageInfo& info,
                                    float* out, size_t begin, size_t end) {
    const size_t spp = source_samples_per_pixel(info);
    const double* src = reinterpret_cast<const double*>(pixels);
    for (size_t i = begin; i < end; ++i) {
        for (size_t s = 0; s < spp; ++s) {
            out[i * spp + s] = static_cast<float>(src[i * spp + s]);
        }
    }
}


bool alpha_plane_present(const float* alpha, size_t pixel_count) {
    if (!alpha || pixel_count == 0) {
        return false;
    }
    float max_a = 0.0f;
    for (size_t i = 0; i < pixel_count; ++i) {
        if (alpha[i] > max_a) {
            max_a = alpha[i];
        }
    }
    return max_a >= (1.0f / 255.0f);
}

void extract_alpha_from_float(const float* pixels, size_t pixel_count, int channels,
                              std::unique_ptr<float[]>& alpha_out) {
    const size_t spp = static_cast<size_t>(channels) + 1u;
    alpha_out = std::make_unique_for_overwrite<float[]>(pixel_count);
    for (size_t i = 0; i < pixel_count; ++i) {
        alpha_out[i] = pixels[i * spp + static_cast<size_t>(channels)];
    }
}

std::array<float, 3> compute_background_from_float(
    const float* pixels, size_t pixel_count, int channels, bool has_alpha,
    const float* alpha_or_null) {
    std::array<float, 3> bg = {0.0f, 0.0f, 0.0f};
    if (!pixels || pixel_count == 0) {
        return bg;
    }

    const size_t spp = static_cast<size_t>(channels) + (has_alpha ? 1u : 0u);
    const int ch = channels;

    unsigned thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0) {
        thread_count = 4;
    }
    if (pixel_count < 262144) {
        thread_count = 1;
    }

    const size_t chunk = (pixel_count + thread_count - 1) / thread_count;
    std::vector<std::future<std::array<double, 4>>> futures;
    futures.reserve(thread_count);

    for (unsigned t = 0; t < thread_count; ++t) {
        const size_t begin = t * chunk;
        const size_t end = std::min(begin + chunk, pixel_count);
        if (begin >= pixel_count) {
            break;
        }
        futures.push_back(std::async(std::launch::async, [=]() {
            double local_c[3] = {0.0, 0.0, 0.0};
            double local_w = 0.0;
            for (size_t i = begin; i < end; ++i) {
                const float w = alpha_or_null ? alpha_or_null[i] : 1.0f;
                local_w += static_cast<double>(w);
                const float* src = pixels + i * spp;
                for (int c = 0; c < ch; ++c) {
                    local_c[c] += static_cast<double>(w) * static_cast<double>(src[c]);
                }
            }
            return std::array<double, 4>{local_c[0], local_c[1], local_c[2], local_w};
        }));
    }

    double sum_c[3] = {0.0, 0.0, 0.0};
    double sum_w = 0.0;
    for (auto& fut : futures) {
        const auto partial = fut.get();
        sum_c[0] += partial[0];
        sum_c[1] += partial[1];
        sum_c[2] += partial[2];
        sum_w += partial[3];
    }

    if (sum_w <= 0.0) {
        return bg;
    }
    for (int c = 0; c < ch; ++c) {
        bg[c] = static_cast<float>(1.0 - (sum_c[c] / sum_w));
    }
    if (ch == 1) {
        bg[1] = bg[0];
        bg[2] = bg[0];
    }
    return bg;
}

void bake_alpha_float_and_compact(float* pixels, size_t pixel_count, int channels,
                                  const float* alpha, const std::array<float, 3>& bg) {
    const size_t spp_in = static_cast<size_t>(channels) + 1u;
    const size_t spp_out = static_cast<size_t>(channels);

    unsigned thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0) {
        thread_count = 4;
    }
    if (pixel_count < 262144) {
        thread_count = 1;
    }

    const size_t chunk = (pixel_count + thread_count - 1) / thread_count;
    std::vector<std::future<void>> futures;
    futures.reserve(thread_count);

    for (unsigned t = 0; t < thread_count; ++t) {
        const size_t begin = t * chunk;
        const size_t end = std::min(begin + chunk, pixel_count);
        if (begin >= pixel_count) {
            break;
        }
        futures.push_back(std::async(std::launch::async, [=]() {
            for (size_t i = begin; i < end; ++i) {
                const float a = alpha[i];
                const float inv = 1.0f - a;
                float* px = pixels + i * spp_in;
                for (int c = 0; c < channels; ++c) {
                    px[c] = a * px[c] + inv * bg[c];
                }
            }
        }));
    }
    for (auto& fut : futures) {
        fut.get();
    }

    for (size_t i = 0; i < pixel_count; ++i) {
        const float* src = pixels + i * spp_in;
        float* dst = pixels + i * spp_out;
        for (size_t c = 0; c < spp_out; ++c) {
            dst[c] = src[c];
        }
    }
}

void strip_alpha_float_compact(float* pixels, size_t pixel_count, int channels) {
    const size_t spp_in = static_cast<size_t>(channels) + 1u;
    const size_t spp_out = static_cast<size_t>(channels);
    for (size_t i = 0; i < pixel_count; ++i) {
        const float* src = pixels + i * spp_in;
        float* dst = pixels + i * spp_out;
        for (size_t c = 0; c < spp_out; ++c) {
            dst[c] = src[c];
        }
    }
}

}

size_t source_samples_per_pixel(const ImageInfo& info) {
    return static_cast<size_t>(info.channels) + (info.has_alpha ? 1u : 0u);
}

bool needs_double_staging(const ImageInfo& info) {
    if (info.sample_type == SampleType::Float64) {
        return true;
    }
    if (info.sample_type == SampleType::Integer && info.bit_depth >= 32) {
        return true;
    }
    return false;
}

void expand_source_to_float(const uint8_t* pixels, const ImageInfo& info, float* out) {
    const size_t pixel_count = static_cast<size_t>(info.width) * static_cast<size_t>(info.height);

    unsigned thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0) {
        thread_count = 4;
    }
    if (pixel_count < 262144) {
        thread_count = 1;
    }

    const size_t chunk = (pixel_count + thread_count - 1) / thread_count;
    std::vector<std::future<void>> futures;
    futures.reserve(thread_count);

    for (unsigned t = 0; t < thread_count; ++t) {
        const size_t begin = t * chunk;
        const size_t end = std::min(begin + chunk, pixel_count);
        if (begin >= pixel_count) {
            break;
        }
        futures.push_back(std::async(std::launch::async, [=]() {
            if (info.sample_type == SampleType::Float32) {
                expand_float32_source_to_float(pixels, info, out, begin, end);
            } else if (info.sample_type == SampleType::Float64) {
                expand_float64_source_to_float(pixels, info, out, begin, end);
            } else {
                expand_integer_range_to_float(pixels, info, out, begin, end);
            }
        }));
    }
    for (auto& fut : futures) {
        fut.get();
    }
}

void prepare_alpha_float(
    float* ref, ImageInfo& ref_info,
    float* dist, ImageInfo& dist_info) {
    const size_t pixel_count =
        static_cast<size_t>(ref_info.width) * static_cast<size_t>(ref_info.height);

    std::unique_ptr<float[]> alpha_ref;
    std::unique_ptr<float[]> alpha_dist;
    bool ref_has_alpha = false;
    bool dist_has_alpha = false;

    if (ref_info.has_alpha) {
        extract_alpha_from_float(ref, pixel_count, ref_info.channels, alpha_ref);
        ref_has_alpha = alpha_plane_present(alpha_ref.get(), pixel_count);
        if (!ref_has_alpha) {
            alpha_ref.reset();
            strip_alpha_float_compact(ref, pixel_count, ref_info.channels);
            ref_info.has_alpha = false;
        }
    }

    if (dist_info.has_alpha) {
        extract_alpha_from_float(dist, pixel_count, dist_info.channels, alpha_dist);
        dist_has_alpha = alpha_plane_present(alpha_dist.get(), pixel_count);
        if (!dist_has_alpha) {
            alpha_dist.reset();
            strip_alpha_float_compact(dist, pixel_count, dist_info.channels);
            dist_info.has_alpha = false;
        }
    }

    if (ref_has_alpha || dist_has_alpha) {
        const float* ref_alpha_for_bg = ref_has_alpha ? alpha_ref.get() : nullptr;
        const auto bg = compute_background_from_float(
            ref, pixel_count, ref_info.channels, ref_info.has_alpha, ref_alpha_for_bg);

        if (ref_has_alpha) {
            bake_alpha_float_and_compact(ref, pixel_count, ref_info.channels, alpha_ref.get(), bg);
            alpha_ref.reset();
            ref_info.has_alpha = false;
        }

        if (dist_has_alpha) {
            bake_alpha_float_and_compact(dist, pixel_count, dist_info.channels, alpha_dist.get(), bg);
            alpha_dist.reset();
            dist_info.has_alpha = false;
        }
    }
}

void promote_gray_to_rgb_float(std::unique_ptr<float[]>& pixels, ImageInfo& info) {
    if (info.channels != 1 || !pixels) {
        return;
    }

    const size_t pixel_count = static_cast<size_t>(info.width) * static_cast<size_t>(info.height);
    const size_t old_spp = 1u + (info.has_alpha ? 1u : 0u);
    const size_t new_spp = 3u + (info.has_alpha ? 1u : 0u);
    auto promoted = std::make_unique_for_overwrite<float[]>(pixel_count * new_spp);

    for (size_t i = 0; i < pixel_count; ++i) {
        const float g = pixels[i * old_spp];
        float* dst = promoted.get() + i * new_spp;
        dst[0] = g;
        dst[1] = g;
        dst[2] = g;
        if (info.has_alpha) {
            dst[3] = pixels[i * old_spp + 1u];
        }
    }

    pixels = std::move(promoted);
    info.channels = 3;
}

void promote_gray_to_rgb_double(std::unique_ptr<double[]>& pixels, ImageInfo& info) {
    if (info.channels != 1 || !pixels) {
        return;
    }

    const size_t pixel_count = static_cast<size_t>(info.width) * static_cast<size_t>(info.height);
    const size_t old_spp = 1u + (info.has_alpha ? 1u : 0u);
    const size_t new_spp = 3u + (info.has_alpha ? 1u : 0u);
    auto promoted = std::make_unique_for_overwrite<double[]>(pixel_count * new_spp);

    for (size_t i = 0; i < pixel_count; ++i) {
        const double g = pixels[i * old_spp];
        double* dst = promoted.get() + i * new_spp;
        dst[0] = g;
        dst[1] = g;
        dst[2] = g;
        if (info.has_alpha) {
            dst[3] = pixels[i * old_spp + 1u];
        }
    }

    pixels = std::move(promoted);
    info.channels = 3;
}

void convert_to_working(const float* pixels, const ImageInfo& info, WorkingImage& out,
                        WorkingTransfer transfer) {
    ensure_lcms2_plugins();
    if (!pixels || info.width == 0 || info.height == 0) {
        throw std::runtime_error("Invalid image for color conversion");
    }
    if (info.has_alpha) {
        throw std::runtime_error("convert_to_working expects alpha already handled");
    }
    if (!out.data || out.total_samples == 0) {
        throw std::runtime_error("convert_to_working requires core-allocated output buffer");
    }

    if (out.channels != 1 && out.channels != 3) {
        throw std::runtime_error("convert_to_working output channels must be 1 or 3");
    }
    if (info.channels != 1 && info.channels != 3) {
        throw std::runtime_error("convert_to_working source channels must be 1 or 3");
    }

    OwnedProfile src = resolve_source_profile(info);
    cmsHPROFILE h_dst = destination_profile(out.channels, transfer);

    const size_t pixel_count = static_cast<size_t>(info.width) * static_cast<size_t>(info.height);
    const size_t src_spp = static_cast<size_t>(info.channels);
    const size_t dst_spp = static_cast<size_t>(out.channels);
    const cmsUInt32Number in_fmt = select_float_format(info.channels);
    const cmsUInt32Number out_fmt = select_float_format(out.channels);
    const cmsUInt32Number xform_flags = cmsFLAGS_NOOPTIMIZE | cmsFLAGS_NOCACHE;

#if defined(TOOFSIQA_HAS_LCMS2_THREADED)
    cmsHTRANSFORM xform = cmsCreateTransform(
        src.handle, in_fmt, h_dst, out_fmt, INTENT_PERCEPTUAL, xform_flags);
    if (!xform) {
        if (src.owned) {
            cmsCloseProfile(src.handle);
        }
        throw std::runtime_error("cmsCreateTransform failed");
    }
    const cmsUInt32Number bytes_per_line_in =
        static_cast<cmsUInt32Number>(info.width * src_spp * sizeof(float));
    const cmsUInt32Number bytes_per_line_out =
        static_cast<cmsUInt32Number>(info.width * dst_spp * sizeof(float));
    cmsDoTransformLineStride(
        xform, pixels, out.data,
        static_cast<cmsUInt32Number>(info.width),
        static_cast<cmsUInt32Number>(info.height),
        bytes_per_line_in, bytes_per_line_out, 0, 0);
    cmsDeleteTransform(xform);
#else
    unsigned cores = std::thread::hardware_concurrency();
    if (cores == 0) {
        cores = 4;
    }
    const unsigned slices = (cores >= 4 && pixel_count >= 2097152) ? 2u : 1u;

    std::vector<cmsHTRANSFORM> transforms(slices, nullptr);
    for (unsigned s = 0; s < slices; ++s) {
        transforms[s] = cmsCreateTransform(
            src.handle, in_fmt, h_dst, out_fmt, INTENT_PERCEPTUAL, xform_flags);
        if (!transforms[s]) {
            for (unsigned k = 0; k < s; ++k) {
                cmsDeleteTransform(transforms[k]);
            }
            if (src.owned) {
                cmsCloseProfile(src.handle);
            }
            throw std::runtime_error("cmsCreateTransform failed");
        }
    }

    if (slices == 1) {
        cmsDoTransform(transforms[0], pixels, out.data,
                       static_cast<cmsUInt32Number>(pixel_count));
    } else {
        const size_t pixels_per_slice = (pixel_count + slices - 1) / slices;
        std::vector<std::future<void>> futures;
        futures.reserve(slices);

        for (unsigned s = 0; s < slices; ++s) {
            const size_t start = s * pixels_per_slice;
            if (start >= pixel_count) {
                break;
            }
            const size_t count = std::min(pixels_per_slice, pixel_count - start);
            const float* src_slice = pixels + start * src_spp;
            float* dst_slice = out.data + start * dst_spp;
            cmsHTRANSFORM xform = transforms[s];

            futures.push_back(std::async(std::launch::async, [xform, src_slice, dst_slice, count]() {
                cmsDoTransform(xform, src_slice, dst_slice, static_cast<cmsUInt32Number>(count));
            }));
        }

        for (auto& future : futures) {
            future.get();
        }
    }

    for (cmsHTRANSFORM xform : transforms) {
        if (xform) {
            cmsDeleteTransform(xform);
        }
    }
#endif

    if (src.owned) {
        cmsCloseProfile(src.handle);
    }
}

void expand_source_to_double(const uint8_t* pixels, const ImageInfo& info, double* out) {
    const size_t pixel_count = static_cast<size_t>(info.width) * static_cast<size_t>(info.height);
    const size_t spp = source_samples_per_pixel(info);

    unsigned thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0) {
        thread_count = 4;
    }
    if (pixel_count < 262144) {
        thread_count = 1;
    }

    const size_t chunk = (pixel_count + thread_count - 1) / thread_count;
    std::vector<std::future<void>> futures;
    futures.reserve(thread_count);

    for (unsigned t = 0; t < thread_count; ++t) {
        const size_t begin = t * chunk;
        const size_t end = std::min(begin + chunk, pixel_count);
        if (begin >= pixel_count) {
            break;
        }
        futures.push_back(std::async(std::launch::async, [=]() {
            if (info.sample_type == SampleType::Float64) {
                const double* src = reinterpret_cast<const double*>(pixels);
                for (size_t i = begin; i < end; ++i) {
                    for (size_t s = 0; s < spp; ++s) {
                        out[i * spp + s] = src[i * spp + s];
                    }
                }
            } else if (info.sample_type == SampleType::Float32) {
                const float* src = reinterpret_cast<const float*>(pixels);
                for (size_t i = begin; i < end; ++i) {
                    for (size_t s = 0; s < spp; ++s) {
                        out[i * spp + s] = static_cast<double>(src[i * spp + s]);
                    }
                }
            } else {
                const size_t nbytes = sample_bytes(info.bit_depth);
                const size_t bytes_per_pixel = spp * nbytes;
                const double inv_peak = 1.0 / sample_peak(info.bit_depth);
                for (size_t i = begin; i < end; ++i) {
                    const uint8_t* src = pixels + i * bytes_per_pixel;
                    double* dst = out + i * spp;
                    for (size_t s = 0; s < spp; ++s) {
                        const uint32_t raw = read_integer_sample(src + s * nbytes, nbytes);
                        dst[s] = static_cast<double>(raw) * inv_peak;
                    }
                }
            }
        }));
    }
    for (auto& fut : futures) {
        fut.get();
    }
}

namespace {

bool alpha_plane_present_d(const double* alpha, size_t pixel_count) {
    if (!alpha || pixel_count == 0) {
        return false;
    }
    double max_a = 0.0;
    for (size_t i = 0; i < pixel_count; ++i) {
        if (alpha[i] > max_a) {
            max_a = alpha[i];
        }
    }
    return max_a >= (1.0 / 255.0);
}

void extract_alpha_from_double(const double* pixels, size_t pixel_count, int channels,
                               std::unique_ptr<double[]>& alpha_out) {
    const size_t spp = static_cast<size_t>(channels) + 1u;
    alpha_out = std::make_unique_for_overwrite<double[]>(pixel_count);
    for (size_t i = 0; i < pixel_count; ++i) {
        alpha_out[i] = pixels[i * spp + static_cast<size_t>(channels)];
    }
}

std::array<double, 3> compute_background_from_double(
    const double* pixels, size_t pixel_count, int channels, bool has_alpha,
    const double* alpha_or_null) {
    std::array<double, 3> bg = {0.0, 0.0, 0.0};
    if (!pixels || pixel_count == 0) {
        return bg;
    }

    const size_t spp = static_cast<size_t>(channels) + (has_alpha ? 1u : 0u);
    const int ch = channels;

    unsigned thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0) {
        thread_count = 4;
    }
    if (pixel_count < 262144) {
        thread_count = 1;
    }

    const size_t chunk = (pixel_count + thread_count - 1) / thread_count;
    std::vector<std::future<std::array<double, 4>>> futures;
    futures.reserve(thread_count);

    for (unsigned t = 0; t < thread_count; ++t) {
        const size_t begin = t * chunk;
        const size_t end = std::min(begin + chunk, pixel_count);
        if (begin >= pixel_count) {
            break;
        }
        futures.push_back(std::async(std::launch::async, [=]() {
            double local_c[3] = {0.0, 0.0, 0.0};
            double local_w = 0.0;
            for (size_t i = begin; i < end; ++i) {
                const double w = alpha_or_null ? alpha_or_null[i] : 1.0;
                local_w += w;
                const double* src = pixels + i * spp;
                for (int c = 0; c < ch; ++c) {
                    local_c[c] += w * src[c];
                }
            }
            return std::array<double, 4>{local_c[0], local_c[1], local_c[2], local_w};
        }));
    }

    double sum_c[3] = {0.0, 0.0, 0.0};
    double sum_w = 0.0;
    for (auto& fut : futures) {
        const auto partial = fut.get();
        sum_c[0] += partial[0];
        sum_c[1] += partial[1];
        sum_c[2] += partial[2];
        sum_w += partial[3];
    }

    if (sum_w <= 0.0) {
        return bg;
    }
    for (int c = 0; c < ch; ++c) {
        bg[c] = 1.0 - (sum_c[c] / sum_w);
    }
    if (ch == 1) {
        bg[1] = bg[0];
        bg[2] = bg[0];
    }
    return bg;
}

void bake_alpha_double_and_compact(double* pixels, size_t pixel_count, int channels,
                                   const double* alpha, const std::array<double, 3>& bg) {
    const size_t spp_in = static_cast<size_t>(channels) + 1u;
    const size_t spp_out = static_cast<size_t>(channels);

    unsigned thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0) {
        thread_count = 4;
    }
    if (pixel_count < 262144) {
        thread_count = 1;
    }

    const size_t chunk = (pixel_count + thread_count - 1) / thread_count;
    std::vector<std::future<void>> futures;
    futures.reserve(thread_count);

    for (unsigned t = 0; t < thread_count; ++t) {
        const size_t begin = t * chunk;
        const size_t end = std::min(begin + chunk, pixel_count);
        if (begin >= pixel_count) {
            break;
        }
        futures.push_back(std::async(std::launch::async, [=]() {
            for (size_t i = begin; i < end; ++i) {
                const double a = alpha[i];
                const double inv = 1.0 - a;
                double* px = pixels + i * spp_in;
                for (int c = 0; c < channels; ++c) {
                    px[c] = a * px[c] + inv * bg[c];
                }
            }
        }));
    }
    for (auto& fut : futures) {
        fut.get();
    }

    for (size_t i = 0; i < pixel_count; ++i) {
        const double* src = pixels + i * spp_in;
        double* dst = pixels + i * spp_out;
        for (size_t c = 0; c < spp_out; ++c) {
            dst[c] = src[c];
        }
    }
}

void strip_alpha_double_compact(double* pixels, size_t pixel_count, int channels) {
    const size_t spp_in = static_cast<size_t>(channels) + 1u;
    const size_t spp_out = static_cast<size_t>(channels);
    for (size_t i = 0; i < pixel_count; ++i) {
        const double* src = pixels + i * spp_in;
        double* dst = pixels + i * spp_out;
        for (size_t c = 0; c < spp_out; ++c) {
            dst[c] = src[c];
        }
    }
}

}

void prepare_alpha_double(
    double* ref, ImageInfo& ref_info,
    double* dist, ImageInfo& dist_info) {
    const size_t pixel_count =
        static_cast<size_t>(ref_info.width) * static_cast<size_t>(ref_info.height);

    std::unique_ptr<double[]> alpha_ref;
    std::unique_ptr<double[]> alpha_dist;
    bool ref_has_alpha = false;
    bool dist_has_alpha = false;

    if (ref_info.has_alpha) {
        extract_alpha_from_double(ref, pixel_count, ref_info.channels, alpha_ref);
        ref_has_alpha = alpha_plane_present_d(alpha_ref.get(), pixel_count);
        if (!ref_has_alpha) {
            alpha_ref.reset();
            strip_alpha_double_compact(ref, pixel_count, ref_info.channels);
            ref_info.has_alpha = false;
        }
    }

    if (dist_info.has_alpha) {
        extract_alpha_from_double(dist, pixel_count, dist_info.channels, alpha_dist);
        dist_has_alpha = alpha_plane_present_d(alpha_dist.get(), pixel_count);
        if (!dist_has_alpha) {
            alpha_dist.reset();
            strip_alpha_double_compact(dist, pixel_count, dist_info.channels);
            dist_info.has_alpha = false;
        }
    }

    if (ref_has_alpha || dist_has_alpha) {
        const double* ref_alpha_for_bg = ref_has_alpha ? alpha_ref.get() : nullptr;
        const auto bg = compute_background_from_double(
            ref, pixel_count, ref_info.channels, ref_info.has_alpha, ref_alpha_for_bg);

        if (ref_has_alpha) {
            bake_alpha_double_and_compact(ref, pixel_count, ref_info.channels, alpha_ref.get(), bg);
            alpha_ref.reset();
            ref_info.has_alpha = false;
        }

        if (dist_has_alpha) {
            bake_alpha_double_and_compact(dist, pixel_count, dist_info.channels, alpha_dist.get(), bg);
            alpha_dist.reset();
            dist_info.has_alpha = false;
        }
    }
}

void convert_to_working(const double* pixels, const ImageInfo& info, WorkingImage& out,
                        WorkingTransfer transfer) {
    ensure_lcms2_plugins();
    if (!pixels || info.width == 0 || info.height == 0) {
        throw std::runtime_error("Invalid image for color conversion");
    }
    if (info.has_alpha) {
        throw std::runtime_error("convert_to_working expects alpha already handled");
    }
    if (!out.data || out.total_samples == 0) {
        throw std::runtime_error("convert_to_working requires core-allocated output buffer");
    }

    if (out.channels != 1 && out.channels != 3) {
        throw std::runtime_error("convert_to_working output channels must be 1 or 3");
    }
    if (info.channels != 1 && info.channels != 3) {
        throw std::runtime_error("convert_to_working source channels must be 1 or 3");
    }

    OwnedProfile src = resolve_source_profile(info);
    cmsHPROFILE h_dst = destination_profile(out.channels, transfer);

    const size_t pixel_count = static_cast<size_t>(info.width) * static_cast<size_t>(info.height);
    const size_t src_spp = static_cast<size_t>(info.channels);
    const size_t dst_spp = static_cast<size_t>(out.channels);
    const cmsUInt32Number in_fmt = select_double_format(info.channels);
    const cmsUInt32Number out_fmt = select_float_format(out.channels);
    const cmsUInt32Number xform_flags = cmsFLAGS_NOOPTIMIZE | cmsFLAGS_NOCACHE;

#if defined(TOOFSIQA_HAS_LCMS2_THREADED)
    cmsHTRANSFORM xform = cmsCreateTransform(
        src.handle, in_fmt, h_dst, out_fmt, INTENT_PERCEPTUAL, xform_flags);
    if (!xform) {
        if (src.owned) {
            cmsCloseProfile(src.handle);
        }
        throw std::runtime_error("cmsCreateTransform failed");
    }
    const cmsUInt32Number bytes_per_line_in =
        static_cast<cmsUInt32Number>(info.width * src_spp * sizeof(double));
    const cmsUInt32Number bytes_per_line_out =
        static_cast<cmsUInt32Number>(info.width * dst_spp * sizeof(float));
    cmsDoTransformLineStride(
        xform, pixels, out.data,
        static_cast<cmsUInt32Number>(info.width),
        static_cast<cmsUInt32Number>(info.height),
        bytes_per_line_in, bytes_per_line_out, 0, 0);
    cmsDeleteTransform(xform);
#else
    unsigned cores = std::thread::hardware_concurrency();
    if (cores == 0) {
        cores = 4;
    }
    const unsigned slices = (cores >= 4 && pixel_count >= 2097152) ? 2u : 1u;

    std::vector<cmsHTRANSFORM> transforms(slices, nullptr);
    for (unsigned s = 0; s < slices; ++s) {
        transforms[s] = cmsCreateTransform(
            src.handle, in_fmt, h_dst, out_fmt, INTENT_PERCEPTUAL, xform_flags);
        if (!transforms[s]) {
            for (unsigned k = 0; k < s; ++k) {
                cmsDeleteTransform(transforms[k]);
            }
            if (src.owned) {
                cmsCloseProfile(src.handle);
            }
            throw std::runtime_error("cmsCreateTransform failed");
        }
    }

    if (slices == 1) {
        cmsDoTransform(transforms[0], pixels, out.data,
                       static_cast<cmsUInt32Number>(pixel_count));
    } else {
        const size_t pixels_per_slice = (pixel_count + slices - 1) / slices;
        std::vector<std::future<void>> futures;
        futures.reserve(slices);

        for (unsigned s = 0; s < slices; ++s) {
            const size_t start = s * pixels_per_slice;
            if (start >= pixel_count) {
                break;
            }
            const size_t count = std::min(pixels_per_slice, pixel_count - start);
            const double* src_slice = pixels + start * src_spp;
            float* dst_slice = out.data + start * dst_spp;
            cmsHTRANSFORM xform = transforms[s];

            futures.push_back(std::async(std::launch::async, [xform, src_slice, dst_slice, count]() {
                cmsDoTransform(xform, src_slice, dst_slice, static_cast<cmsUInt32Number>(count));
            }));
        }

        for (auto& future : futures) {
            future.get();
        }
    }

    for (cmsHTRANSFORM xform : transforms) {
        if (xform) {
            cmsDeleteTransform(xform);
        }
    }
#endif

    if (src.owned) {
        cmsCloseProfile(src.handle);
    }
}
