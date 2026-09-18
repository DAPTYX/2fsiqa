// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "yuv_allreal.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

#include "yuv_allreal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

HWY_BEFORE_NAMESPACE();
namespace yuv_allreal_detail {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

constexpr double KR = 0.348190307617;
constexpr double KG = 0.709976196289;
constexpr double KB = -0.058166503906;

constexpr double INV_TWO_ONE_MINUS_KB = 1.0 / (2.0 * (1.0 - KB));
constexpr double INV_TWO_ONE_MINUS_KR = 1.0 / (2.0 * (1.0 - KR));

constexpr double CODE_MAX_12 = 4095.0;

inline uint16_t quantize_full_12(double v) {
    const double scaled = v * CODE_MAX_12;
    const double clamped = std::min(CODE_MAX_12, std::max(0.0, scaled));
    return static_cast<uint16_t>(std::lround(clamped));
}

void convert_row_scalar(const float* rgb_row, uint16_t* y_row, uint16_t* cb_row,
                        uint16_t* cr_row, size_t width) {
    for (size_t x = 0; x < width; ++x) {
        const float* src = rgb_row + x * 3;
        const double r = static_cast<double>(src[0]);
        const double g = static_cast<double>(src[1]);
        const double b = static_cast<double>(src[2]);

        const double yv = KR * r + KG * g + KB * b;
        const double cbv = (b - yv) * INV_TWO_ONE_MINUS_KB;
        const double crv = (r - yv) * INV_TWO_ONE_MINUS_KR;

        y_row[x] = quantize_full_12(yv);
        cb_row[x] = quantize_full_12(cbv + 0.5);
        cr_row[x] = quantize_full_12(crv + 0.5);
    }
}

void convert_rows(const float* rgb, uint16_t* y, uint16_t* cb, uint16_t* cr,
                  uint32_t width, uint32_t stride, uint32_t row_begin,
                  uint32_t row_end) {
    const hn::ScalableTag<float> df;
    const size_t float_lanes = hn::Lanes(df);
    const size_t rgb_row_samples = static_cast<size_t>(width) * 3u;

    using DD = hn::Repartition<double, decltype(df)>;
    const DD dd;
    const size_t double_lanes = hn::Lanes(dd);

    const auto v_kr = hn::Set(dd, KR);
    const auto v_kg = hn::Set(dd, KG);
    const auto v_kb = hn::Set(dd, KB);
    const auto v_inv_kb = hn::Set(dd, INV_TWO_ONE_MINUS_KB);
    const auto v_inv_kr = hn::Set(dd, INV_TWO_ONE_MINUS_KR);
    const auto v_half = hn::Set(dd, 0.5);

    alignas(64) double y_tmp[64];
    alignas(64) double cb_tmp[64];
    alignas(64) double cr_tmp[64];

    for (uint32_t row = row_begin; row < row_end; ++row) {
        const float* rgb_row = rgb + static_cast<size_t>(row) * rgb_row_samples;
        uint16_t* y_row = y + static_cast<size_t>(row) * stride;
        uint16_t* cb_row = cb + static_cast<size_t>(row) * stride;
        uint16_t* cr_row = cr + static_cast<size_t>(row) * stride;

        if (float_lanes < 2) {
            convert_row_scalar(rgb_row, y_row, cb_row, cr_row, width);
            continue;
        }

        size_t x = 0;
        for (; x + float_lanes <= width; x += float_lanes) {
            hn::Vec<decltype(df)> rf, gf, bf;
            hn::LoadInterleaved3(df, rgb_row + x * 3, rf, gf, bf);

            const auto r_lo = hn::PromoteLowerTo(dd, rf);
            const auto g_lo = hn::PromoteLowerTo(dd, gf);
            const auto b_lo = hn::PromoteLowerTo(dd, bf);
            const auto r_hi = hn::PromoteUpperTo(dd, rf);
            const auto g_hi = hn::PromoteUpperTo(dd, gf);
            const auto b_hi = hn::PromoteUpperTo(dd, bf);

            const auto y_lo = hn::MulAdd(v_kb, b_lo, hn::MulAdd(v_kg, g_lo, hn::Mul(v_kr, r_lo)));
            const auto y_hi = hn::MulAdd(v_kb, b_hi, hn::MulAdd(v_kg, g_hi, hn::Mul(v_kr, r_hi)));
            const auto cb_lo = hn::Mul(hn::Sub(b_lo, y_lo), v_inv_kb);
            const auto cb_hi = hn::Mul(hn::Sub(b_hi, y_hi), v_inv_kb);
            const auto cr_lo = hn::Mul(hn::Sub(r_lo, y_lo), v_inv_kr);
            const auto cr_hi = hn::Mul(hn::Sub(r_hi, y_hi), v_inv_kr);

            hn::StoreU(y_lo, dd, y_tmp);
            hn::StoreU(y_hi, dd, y_tmp + double_lanes);
            hn::StoreU(hn::Add(cb_lo, v_half), dd, cb_tmp);
            hn::StoreU(hn::Add(cb_hi, v_half), dd, cb_tmp + double_lanes);
            hn::StoreU(hn::Add(cr_lo, v_half), dd, cr_tmp);
            hn::StoreU(hn::Add(cr_hi, v_half), dd, cr_tmp + double_lanes);

            for (size_t k = 0; k < float_lanes; ++k) {
                y_row[x + k] = quantize_full_12(y_tmp[k]);
                cb_row[x + k] = quantize_full_12(cb_tmp[k]);
                cr_row[x + k] = quantize_full_12(cr_tmp[k]);
            }
        }

        if (x < width) {
            convert_row_scalar(rgb_row + x * 3, y_row + x, cb_row + x, cr_row + x,
                               width - x);
        }
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace yuv_allreal_detail
HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace yuv_allreal_detail {
HWY_EXPORT(convert_rows);
}

Yuv444i12 working_rgb_to_yuv444_allreal_12bit_full(const WorkingImage& rgb) {
    if (rgb.channels != 3 || !rgb.data) {
        throw std::runtime_error("working_rgb_to_yuv444_allreal_12bit_full requires RGB working image");
    }

    constexpr uint32_t kAlign = 32;
    const uint32_t width = rgb.width;
    const uint32_t height = rgb.height;
    const uint32_t stride = (width + kAlign - 1u) & ~(kAlign - 1u);
    const size_t plane_samples = static_cast<size_t>(stride) * static_cast<size_t>(height);

    Yuv444i12 out;
    out.width = width;
    out.height = height;
    out.stride = stride;
    out.bpc = 12;
    out.y = std::make_unique_for_overwrite<uint16_t[]>(plane_samples);
    out.cb = std::make_unique_for_overwrite<uint16_t[]>(plane_samples);
    out.cr = std::make_unique_for_overwrite<uint16_t[]>(plane_samples);
    std::memset(out.y.get(), 0, plane_samples * sizeof(uint16_t));
    std::memset(out.cb.get(), 0, plane_samples * sizeof(uint16_t));
    std::memset(out.cr.get(), 0, plane_samples * sizeof(uint16_t));

    const float* src = rgb.data;
    uint16_t* y = out.y.get();
    uint16_t* cb = out.cb.get();
    uint16_t* cr = out.cr.get();

    const size_t kParallelMinRows = 64;
    unsigned hc = std::thread::hardware_concurrency();
    if (hc == 0) hc = 1;

    auto run = [&](uint32_t row_begin, uint32_t row_end) {
        HWY_DYNAMIC_DISPATCH(yuv_allreal_detail::convert_rows)(
            src, y, cb, cr, width, stride, row_begin, row_end);
    };

    if (height < kParallelMinRows || hc == 1) {
        run(0, height);
        return out;
    }

    const size_t nthreads = std::min<size_t>(hc, (height + kParallelMinRows - 1) / kParallelMinRows);
    const uint32_t chunk = static_cast<uint32_t>((height + nthreads - 1) / nthreads);
    std::vector<std::thread> workers;
    workers.reserve(nthreads);
    for (size_t t = 0; t < nthreads; ++t) {
        const uint32_t begin = static_cast<uint32_t>(t) * chunk;
        if (begin >= height) break;
        const uint32_t end = std::min(height, begin + chunk);
        workers.emplace_back(run, begin, end);
    }
    for (auto& w : workers) w.join();

    return out;
}

#endif
