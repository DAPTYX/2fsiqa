// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#include "dssim.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

constexpr double kScaleWeights[5] = {0.028, 0.197, 0.322, 0.298, 0.155};
constexpr int kScaleCount = 5;

constexpr float kD50x = 0.964202880859f;
constexpr float kD50y = 1.0f;
constexpr float kD50z = 0.824905395508f;

constexpr float kRx = 0.964202880859f;
constexpr float kRy = 0.348190307617f;
constexpr float kRz = 0.0f;
constexpr float kGx = 0.0f;
constexpr float kGy = 0.709976196289f;
constexpr float kGz = 0.0f;
constexpr float kBx = 0.0f;
constexpr float kBy = -0.058166503906f;
constexpr float kBz = 0.824905395508f;

constexpr float kEpsilon = 216.0f / 24389.0f;
constexpr float kK = 24389.0f / (27.0f * 116.0f);

constexpr float kSide = 0.30875886f;
constexpr float kCenter = 0.3824828f;
constexpr float k5Outer = kSide * kSide;
constexpr float k5Inner = 2.0f * kSide * kCenter;
constexpr float k5Mid = 2.0f * kSide * kSide + kCenter * kCenter;
constexpr float k5EdgeCenter = k5Mid + k5Inner;
constexpr float k5EdgeNear = k5Outer + k5Inner;
constexpr float k5EdgeFar = k5Outer;

unsigned worker_count_for(size_t work_units) {
    unsigned n = std::thread::hardware_concurrency();
    if (n == 0) {
        n = 4;
    }
    if (work_units < 65536) {
        n = 1;
    }
    return n;
}

float cbrt_poly(float x) {
    float y = (-0.5f * x + 1.51f) * x + 0.2f;
    float y3 = y * y * y;
    y = y * (2.0f * x + y3) / (2.0f * y3 + x);
    y3 = y * y * y;
    y = y * (2.0f * x + y3) / (2.0f * y3 + x);
    return y;
}

float lab_f(float t) {
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > kEpsilon) {
        return cbrt_poly(t) - 16.0f / 116.0f;
    }
    return kK * t;
}

void rgb_to_lab_pixel(float r, float g, float b, float& L, float& a, float& bb) {
    const float X = r * kRx + g * kGx + b * kBx;
    const float Y = r * kRy + g * kGy + b * kBy;
    const float Z = r * kRz + g * kGz + b * kBz;
    const float fx = lab_f(X / kD50x);
    const float fy = lab_f(Y / kD50y);
    const float fz = lab_f(Z / kD50z);
    L = fy * 1.05f;
    a = (500.0f / 220.0f) * (fx - fy) + 86.2f / 220.0f;
    bb = (200.0f / 220.0f) * (fy - fz) + 107.9f / 220.0f;
}

float gray_to_L(float y) {
    if (y < 0.0f) {
        y = 0.0f;
    }
    if (y > kEpsilon) {
        return (cbrt_poly(y) - 16.0f / 116.0f) * 1.16f;
    }
    return (kK * 1.16f) * y;
}

void blur_h5_row(const float* row, float* out, uint32_t width) {
    const uint32_t last = width - 1;
    const float p0 = row[0];
    const float p1 = row[std::min(1u, last)];
    const float p2 = row[std::min(2u, last)];
    out[0] = k5EdgeCenter * p0 + k5EdgeNear * p1 + k5EdgeFar * p2;
    if (width >= 2) {
        out[last] = k5EdgeFar * row[last >= 2 ? last - 2 : 0]
                  + k5EdgeNear * row[last - 1]
                  + k5EdgeCenter * row[last];
    }
    if (width >= 3) {
        out[1] = (row[0] + row[std::min(3u, last)]) * k5Outer
               + (row[0] + row[std::min(2u, last)]) * k5Inner
               + row[1] * k5Mid;
    }
    if (width >= 4) {
        const uint32_t i = last - 1;
        out[i] = (row[i - 2] + row[std::min(i + 2, last)]) * k5Outer
               + (row[i - 1] + row[i + 1]) * k5Inner
               + row[i] * k5Mid;
    }
    for (uint32_t x = 2; x + 2 < width; ++x) {
        out[x] = (row[x - 2] + row[x + 2]) * k5Outer
               + (row[x - 1] + row[x + 1]) * k5Inner
               + row[x] * k5Mid;
    }
}

void blur_h5(const float* src, float* dst, uint32_t width, uint32_t height, size_t src_stride) {
    if (width == 0 || height == 0) {
        return;
    }
    const unsigned workers = worker_count_for(static_cast<size_t>(width) * height);
    if (workers == 1) {
        for (uint32_t y = 0; y < height; ++y) {
            blur_h5_row(src + static_cast<size_t>(y) * src_stride,
                        dst + static_cast<size_t>(y) * width, width);
        }
        return;
    }
    std::vector<std::future<void>> futures;
    futures.reserve(workers);
    for (unsigned t = 0; t < workers; ++t) {
        const uint32_t y0 = height * t / workers;
        const uint32_t y1 = height * (t + 1) / workers;
        futures.push_back(std::async(std::launch::async, [=]() {
            for (uint32_t y = y0; y < y1; ++y) {
                blur_h5_row(src + static_cast<size_t>(y) * src_stride,
                            dst + static_cast<size_t>(y) * width, width);
            }
        }));
    }
    for (auto& f : futures) {
        f.get();
    }
}

void blur_v5(const float* src, float* dst, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return;
    }
    const uint32_t last_y = height - 1;
    auto row = [&](uint32_t y) {
        return src + static_cast<size_t>(y) * width;
    };

    {
        const float* r0 = row(0);
        const float* r1 = row(std::min(1u, last_y));
        const float* r2 = row(std::min(2u, last_y));
        float* out = dst;
        for (uint32_t x = 0; x < width; ++x) {
            out[x] = k5EdgeCenter * r0[x] + k5EdgeNear * r1[x] + k5EdgeFar * r2[x];
        }
    }
    if (height >= 2) {
        const float* rl = row(last_y);
        const float* rl1 = row(last_y - 1);
        const float* rl2 = row(last_y >= 2 ? last_y - 2 : 0);
        float* out = dst + static_cast<size_t>(last_y) * width;
        for (uint32_t x = 0; x < width; ++x) {
            out[x] = k5EdgeFar * rl2[x] + k5EdgeNear * rl1[x] + k5EdgeCenter * rl[x];
        }
    }
    if (height >= 3) {
        const float* rm = row(0);
        const float* rc = row(1);
        const float* rp1 = row(std::min(2u, last_y));
        const float* rp2 = row(std::min(3u, last_y));
        float* out = dst + width;
        for (uint32_t x = 0; x < width; ++x) {
            out[x] = (rm[x] + rp2[x]) * k5Outer + (rm[x] + rp1[x]) * k5Inner + rc[x] * k5Mid;
        }
    }
    if (height >= 4) {
        const uint32_t y = last_y - 1;
        const float* rm2 = row(y - 2);
        const float* rm1 = row(y - 1);
        const float* rc = row(y);
        const float* rp1 = row(y + 1);
        const float* rp2 = row(std::min(y + 2, last_y));
        float* out = dst + static_cast<size_t>(y) * width;
        for (uint32_t x = 0; x < width; ++x) {
            out[x] = (rm2[x] + rp2[x]) * k5Outer + (rm1[x] + rp1[x]) * k5Inner + rc[x] * k5Mid;
        }
    }

    const unsigned workers = worker_count_for(static_cast<size_t>(width) * height);
    if (height >= 5) {
        if (workers == 1) {
            for (uint32_t y = 2; y + 2 < height; ++y) {
                const float* rm2 = row(y - 2);
                const float* rm1 = row(y - 1);
                const float* rc = row(y);
                const float* rp1 = row(y + 1);
                const float* rp2 = row(y + 2);
                float* out = dst + static_cast<size_t>(y) * width;
                for (uint32_t x = 0; x < width; ++x) {
                    out[x] = (rm2[x] + rp2[x]) * k5Outer
                           + (rm1[x] + rp1[x]) * k5Inner
                           + rc[x] * k5Mid;
                }
            }
        } else {
            std::vector<std::future<void>> futures;
            futures.reserve(workers);
            const uint32_t y_begin = 2;
            const uint32_t y_end = height - 2;
            for (unsigned t = 0; t < workers; ++t) {
                const uint32_t span = y_end - y_begin;
                const uint32_t y0 = y_begin + span * t / workers;
                const uint32_t y1 = y_begin + span * (t + 1) / workers;
                futures.push_back(std::async(std::launch::async, [=]() {
                    for (uint32_t y = y0; y < y1; ++y) {
                        const float* rm2 = src + static_cast<size_t>(y - 2) * width;
                        const float* rm1 = src + static_cast<size_t>(y - 1) * width;
                        const float* rc = src + static_cast<size_t>(y) * width;
                        const float* rp1 = src + static_cast<size_t>(y + 1) * width;
                        const float* rp2 = src + static_cast<size_t>(y + 2) * width;
                        float* out = dst + static_cast<size_t>(y) * width;
                        for (uint32_t x = 0; x < width; ++x) {
                            out[x] = (rm2[x] + rp2[x]) * k5Outer
                                   + (rm1[x] + rp1[x]) * k5Inner
                                   + rc[x] * k5Mid;
                        }
                    }
                }));
            }
            for (auto& f : futures) {
                f.get();
            }
        }
    }
}

void blur_into(const float* src, float* dst, float* tmp,
               uint32_t width, uint32_t height, size_t src_stride) {
    blur_h5(src, tmp, width, height, src_stride);
    blur_v5(tmp, dst, width, height);
}

void blur_mul_into(const float* a, const float* b, float* dst, float* tmp, float* product,
                   uint32_t width, uint32_t height) {
    const size_t n = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < n; ++i) {
        product[i] = a[i] * b[i];
    }
    blur_into(product, dst, tmp, width, height, width);
}

void downsample_gray(const float* src, uint32_t width, uint32_t height,
                     float* dst, uint32_t& out_w, uint32_t& out_h) {
    if (width < 8 || height < 8) {
        out_w = 0;
        out_h = 0;
        return;
    }
    out_w = width / 2;
    out_h = height / 2;
    for (uint32_t y = 0; y < out_h; ++y) {
        const float* top = src + static_cast<size_t>(y * 2) * width;
        const float* bot = src + static_cast<size_t>(y * 2 + 1) * width;
        float* row = dst + static_cast<size_t>(y) * out_w;
        for (uint32_t x = 0; x < out_w; ++x) {
            row[x] = (top[x * 2] + top[x * 2 + 1] + bot[x * 2] + bot[x * 2 + 1]) * 0.25f;
        }
    }
}

void downsample_rgb(const float* src, uint32_t width, uint32_t height, size_t row_samples,
                    float* dst, uint32_t& out_w, uint32_t& out_h) {
    if (width < 8 || height < 8) {
        out_w = 0;
        out_h = 0;
        return;
    }
    out_w = width / 2;
    out_h = height / 2;
    for (uint32_t y = 0; y < out_h; ++y) {
        const float* top = src + static_cast<size_t>(y * 2) * row_samples;
        const float* bot = src + static_cast<size_t>(y * 2 + 1) * row_samples;
        float* row = dst + static_cast<size_t>(y) * out_w * 3;
        for (uint32_t x = 0; x < out_w; ++x) {
            const float* t0 = top + x * 6;
            const float* t1 = t0 + 3;
            const float* b0 = bot + x * 6;
            const float* b1 = b0 + 3;
            row[x * 3 + 0] = (t0[0] + t1[0] + b0[0] + b1[0]) * 0.25f;
            row[x * 3 + 1] = (t0[1] + t1[1] + b0[1] + b1[1]) * 0.25f;
            row[x * 3 + 2] = (t0[2] + t1[2] + b0[2] + b1[2]) * 0.25f;
        }
    }
}

void rgb_to_lab_planes(const float* rgb, uint32_t width, uint32_t height, size_t row_samples,
                       float* L, float* a, float* b) {
    const unsigned workers = worker_count_for(static_cast<size_t>(width) * height);
    auto body = [&](uint32_t y0, uint32_t y1) {
        for (uint32_t y = y0; y < y1; ++y) {
            const float* row = rgb + static_cast<size_t>(y) * row_samples;
            float* Lp = L + static_cast<size_t>(y) * width;
            float* ap = a + static_cast<size_t>(y) * width;
            float* bp = b + static_cast<size_t>(y) * width;
            for (uint32_t x = 0; x < width; ++x) {
                rgb_to_lab_pixel(row[x * 3], row[x * 3 + 1], row[x * 3 + 2], Lp[x], ap[x], bp[x]);
            }
        }
    };
    if (workers == 1) {
        body(0, height);
        return;
    }
    std::vector<std::future<void>> futures;
    futures.reserve(workers);
    for (unsigned t = 0; t < workers; ++t) {
        const uint32_t y0 = height * t / workers;
        const uint32_t y1 = height * (t + 1) / workers;
        futures.push_back(std::async(std::launch::async, body, y0, y1));
    }
    for (auto& f : futures) {
        f.get();
    }
}

void gray_to_L_plane(const float* gray, uint32_t width, uint32_t height, size_t row_samples,
                     float* L) {
    const unsigned workers = worker_count_for(static_cast<size_t>(width) * height);
    auto body = [&](uint32_t y0, uint32_t y1) {
        for (uint32_t y = y0; y < y1; ++y) {
            const float* row = gray + static_cast<size_t>(y) * row_samples;
            float* Lp = L + static_cast<size_t>(y) * width;
            for (uint32_t x = 0; x < width; ++x) {
                Lp[x] = gray_to_L(row[x]);
            }
        }
    };
    if (workers == 1) {
        body(0, height);
        return;
    }
    std::vector<std::future<void>> futures;
    futures.reserve(workers);
    for (unsigned t = 0; t < workers; ++t) {
        const uint32_t y0 = height * t / workers;
        const uint32_t y1 = height * (t + 1) / workers;
        futures.push_back(std::async(std::launch::async, body, y0, y1));
    }
    for (auto& f : futures) {
        f.get();
    }
}

double ssim_sample_1ch(double m1, double m2, double sq1, double sq2, double cross) {
    constexpr double c1 = 0.01 * 0.01;
    constexpr double c2 = 0.03 * 0.03;
    const double m1sq = m1 * m1;
    const double m2sq = m2 * m2;
    const double m12 = m1 * m2;
    const double s1 = sq1 - m1sq;
    const double s2 = sq2 - m2sq;
    const double s12 = cross - m12;
    return (2.0 * m12 + c1) * (2.0 * s12 + c2)
         / ((m1sq + m2sq + c1) * (s1 + s2 + c2));
}

double ssim_sample_3ch(const float* mu1[3], const float* mu2[3],
                       const float* sq1[3], const float* sq2[3], const float* cross[3],
                       size_t i) {
    constexpr double c1 = 0.01 * 0.01;
    constexpr double c2 = 0.03 * 0.03;
    constexpr double inv3 = 1.0 / 3.0;
    double m1sq = 0.0, m2sq = 0.0, m12 = 0.0;
    double s1 = 0.0, s2 = 0.0, s12 = 0.0;
    for (int c = 0; c < 3; ++c) {
        const double a = static_cast<double>(mu1[c][i]);
        const double b = static_cast<double>(mu2[c][i]);
        const double aa = a * a;
        const double bb = b * b;
        const double ab = a * b;
        m1sq += aa;
        m2sq += bb;
        m12 += ab;
        s1 += static_cast<double>(sq1[c][i]) - aa;
        s2 += static_cast<double>(sq2[c][i]) - bb;
        s12 += static_cast<double>(cross[c][i]) - ab;
    }
    m1sq *= inv3;
    m2sq *= inv3;
    m12 *= inv3;
    s1 *= inv3;
    s2 *= inv3;
    s12 *= inv3;
    return (2.0 * m12 + c1) * (2.0 * s12 + c2)
         / ((m1sq + m2sq + c1) * (s1 + s2 + c2));
}

double score_from_stats_1ch(const float* mu1, const float* mu2,
                            const float* sq1, const float* sq2, const float* cross,
                            size_t n, int scale_index) {
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sum += ssim_sample_1ch(mu1[i], mu2[i], sq1[i], sq2[i], cross[i]);
    }
    const double avg = std::pow(std::max(0.0, sum / static_cast<double>(n)),
                                std::pow(0.5, static_cast<double>(scale_index)));
    double mad = 0.0;
    for (size_t i = 0; i < n; ++i) {
        mad += std::abs(avg - ssim_sample_1ch(mu1[i], mu2[i], sq1[i], sq2[i], cross[i]));
    }
    mad /= static_cast<double>(n);
    return 1.0 - mad;
}

double score_from_stats_3ch(const float* mu1[3], const float* mu2[3],
                            const float* sq1[3], const float* sq2[3], const float* cross[3],
                            size_t n, int scale_index) {
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sum += ssim_sample_3ch(mu1, mu2, sq1, sq2, cross, i);
    }
    const double avg = std::pow(std::max(0.0, sum / static_cast<double>(n)),
                                std::pow(0.5, static_cast<double>(scale_index)));
    double mad = 0.0;
    for (size_t i = 0; i < n; ++i) {
        mad += std::abs(avg - ssim_sample_3ch(mu1, mu2, sq1, sq2, cross, i));
    }
    mad /= static_cast<double>(n);
    return 1.0 - mad;
}

double dssim_from_ssim(double ssim) {
    return 1.0 / std::max(ssim, 1e-15) - 1.0;
}

struct PlaneBuf {
    std::vector<float> data;
    uint32_t width = 0;
    uint32_t height = 0;

    void ensure(uint32_t w, uint32_t h) {
        width = w;
        height = h;
        const size_t need = static_cast<size_t>(w) * h;
        if (data.size() < need) {
            data.resize(need);
        }
    }

    float* ptr() { return data.data(); }
    const float* ptr() const { return data.data(); }

    void release() {
        data.clear();
        data.shrink_to_fit();
        width = 0;
        height = 0;
    }
};

double compare_gray_scale(PlaneBuf& ref_L, PlaneBuf& dist_L,
                          uint32_t width, uint32_t height, int scale_index,
                          PlaneBuf& mu1, PlaneBuf& mu2,
                          PlaneBuf& sq1, PlaneBuf& sq2, PlaneBuf& cross,
                          PlaneBuf& tmp, PlaneBuf& product) {
    const size_t n = static_cast<size_t>(width) * height;
    mu1.ensure(width, height);
    mu2.ensure(width, height);
    sq1.ensure(width, height);
    sq2.ensure(width, height);
    cross.ensure(width, height);
    tmp.ensure(width, height);
    product.ensure(width, height);

    blur_into(ref_L.ptr(), mu1.ptr(), tmp.ptr(), width, height, width);
    blur_into(dist_L.ptr(), mu2.ptr(), tmp.ptr(), width, height, width);
    blur_mul_into(ref_L.ptr(), ref_L.ptr(), sq1.ptr(), tmp.ptr(), product.ptr(), width, height);
    blur_mul_into(dist_L.ptr(), dist_L.ptr(), sq2.ptr(), tmp.ptr(), product.ptr(), width, height);
    blur_mul_into(ref_L.ptr(), dist_L.ptr(), cross.ptr(), tmp.ptr(), product.ptr(), width, height);

    ref_L.release();
    dist_L.release();
    tmp.release();
    product.release();

    return score_from_stats_1ch(mu1.ptr(), mu2.ptr(), sq1.ptr(), sq2.ptr(), cross.ptr(),
                                n, scale_index);
}

double compare_rgb_scale(PlaneBuf& L1, PlaneBuf& a1, PlaneBuf& b1,
                         PlaneBuf& L2, PlaneBuf& a2, PlaneBuf& b2,
                         uint32_t width, uint32_t height, int scale_index,
                         PlaneBuf mu1[3], PlaneBuf mu2[3],
                         PlaneBuf sq1[3], PlaneBuf sq2[3], PlaneBuf cross[3],
                         PlaneBuf& tmp, PlaneBuf& product) {
    const size_t n = static_cast<size_t>(width) * height;
    PlaneBuf* planes1[3] = {&L1, &a1, &b1};
    PlaneBuf* planes2[3] = {&L2, &a2, &b2};

    for (int c = 0; c < 3; ++c) {
        mu1[c].ensure(width, height);
        mu2[c].ensure(width, height);
        sq1[c].ensure(width, height);
        sq2[c].ensure(width, height);
        cross[c].ensure(width, height);
    }
    tmp.ensure(width, height);
    product.ensure(width, height);

    for (int c = 0; c < 3; ++c) {
        if (c > 0) {
            blur_into(planes1[c]->ptr(), planes1[c]->ptr(), tmp.ptr(), width, height, width);
            blur_into(planes2[c]->ptr(), planes2[c]->ptr(), tmp.ptr(), width, height, width);
        }
        blur_into(planes1[c]->ptr(), mu1[c].ptr(), tmp.ptr(), width, height, width);
        blur_into(planes2[c]->ptr(), mu2[c].ptr(), tmp.ptr(), width, height, width);
        blur_mul_into(planes1[c]->ptr(), planes1[c]->ptr(), sq1[c].ptr(),
                      tmp.ptr(), product.ptr(), width, height);
        blur_mul_into(planes2[c]->ptr(), planes2[c]->ptr(), sq2[c].ptr(),
                      tmp.ptr(), product.ptr(), width, height);
        blur_mul_into(planes1[c]->ptr(), planes2[c]->ptr(), cross[c].ptr(),
                      tmp.ptr(), product.ptr(), width, height);
        planes1[c]->release();
        planes2[c]->release();
    }

    tmp.release();
    product.release();

    const float* mu1p[3] = {mu1[0].ptr(), mu1[1].ptr(), mu1[2].ptr()};
    const float* mu2p[3] = {mu2[0].ptr(), mu2[1].ptr(), mu2[2].ptr()};
    const float* sq1p[3] = {sq1[0].ptr(), sq1[1].ptr(), sq1[2].ptr()};
    const float* sq2p[3] = {sq2[0].ptr(), sq2[1].ptr(), sq2[2].ptr()};
    const float* crossp[3] = {cross[0].ptr(), cross[1].ptr(), cross[2].ptr()};
    return score_from_stats_3ch(mu1p, mu2p, sq1p, sq2p, crossp, n, scale_index);
}

double compute_dssim_gray(const WorkingImage& reference, const WorkingImage& distorted) {
    PlaneBuf L1;
    PlaneBuf L2;
    PlaneBuf hold_a;
    PlaneBuf hold_b;
    PlaneBuf next1;
    PlaneBuf next2;
    PlaneBuf mu1;
    PlaneBuf mu2;
    PlaneBuf sq1;
    PlaneBuf sq2;
    PlaneBuf cross;
    PlaneBuf tmp;
    PlaneBuf product;

    uint32_t width = reference.width;
    uint32_t height = reference.height;
    size_t row_a = reference.row_samples;
    size_t row_b = distorted.row_samples;
    const float* src_a = reference.data;
    const float* src_b = distorted.data;

    double ssim_sum = 0.0;
    double weight_sum = 0.0;

    for (int s = 0; s < kScaleCount; ++s) {
        L1.ensure(width, height);
        L2.ensure(width, height);
        gray_to_L_plane(src_a, width, height, row_a, L1.ptr());
        gray_to_L_plane(src_b, width, height, row_b, L2.ptr());

        const double score = compare_gray_scale(
            L1, L2, width, height, s,
            mu1, mu2, sq1, sq2, cross, tmp, product);

        ssim_sum += score * kScaleWeights[s];
        weight_sum += kScaleWeights[s];

        if (s + 1 >= kScaleCount) {
            break;
        }
        uint32_t nw = 0;
        uint32_t nh = 0;
        next1.ensure(width / 2, height / 2);
        next2.ensure(width / 2, height / 2);
        downsample_gray(src_a, width, height, next1.ptr(), nw, nh);
        if (nw == 0) {
            break;
        }
        uint32_t nw2 = 0;
        uint32_t nh2 = 0;
        downsample_gray(src_b, width, height, next2.ptr(), nw2, nh2);

        width = nw;
        height = nh;
        row_a = nw;
        row_b = nw2;
        std::swap(hold_a.data, next1.data);
        std::swap(hold_b.data, next2.data);
        hold_a.width = width;
        hold_a.height = height;
        hold_b.width = width;
        hold_b.height = height;
        src_a = hold_a.ptr();
        src_b = hold_b.ptr();
    }

    if (weight_sum <= 0.0) {
        return 0.0;
    }
    return dssim_from_ssim(ssim_sum / weight_sum);
}

double compute_dssim_rgb(const WorkingImage& reference, const WorkingImage& distorted) {
    PlaneBuf rgb_a;
    PlaneBuf rgb_b;
    PlaneBuf next_a;
    PlaneBuf next_b;
    PlaneBuf L1;
    PlaneBuf a1;
    PlaneBuf b1;
    PlaneBuf L2;
    PlaneBuf a2;
    PlaneBuf b2;
    PlaneBuf mu1[3];
    PlaneBuf mu2[3];
    PlaneBuf sq1[3];
    PlaneBuf sq2[3];
    PlaneBuf cross[3];
    PlaneBuf tmp;
    PlaneBuf product;

    uint32_t width = reference.width;
    uint32_t height = reference.height;
    size_t row_a = reference.row_samples;
    size_t row_b = distorted.row_samples;
    const float* src_a = reference.data;
    const float* src_b = distorted.data;

    double ssim_sum = 0.0;
    double weight_sum = 0.0;

    for (int s = 0; s < kScaleCount; ++s) {
        L1.ensure(width, height);
        a1.ensure(width, height);
        b1.ensure(width, height);
        L2.ensure(width, height);
        a2.ensure(width, height);
        b2.ensure(width, height);

        rgb_to_lab_planes(src_a, width, height, row_a, L1.ptr(), a1.ptr(), b1.ptr());
        rgb_to_lab_planes(src_b, width, height, row_b, L2.ptr(), a2.ptr(), b2.ptr());

        const double score = compare_rgb_scale(
            L1, a1, b1, L2, a2, b2,
            width, height, s,
            mu1, mu2, sq1, sq2, cross, tmp, product);

        ssim_sum += score * kScaleWeights[s];
        weight_sum += kScaleWeights[s];

        if (s + 1 >= kScaleCount) {
            break;
        }

        uint32_t nw = 0;
        uint32_t nh = 0;
        const size_t next_samples = static_cast<size_t>(width / 2) * (height / 2) * 3;
        if (next_a.data.size() < next_samples) {
            next_a.data.resize(next_samples);
        }
        if (next_b.data.size() < next_samples) {
            next_b.data.resize(next_samples);
        }
        downsample_rgb(src_a, width, height, row_a, next_a.data.data(), nw, nh);
        if (nw == 0) {
            break;
        }
        uint32_t nw2 = 0;
        uint32_t nh2 = 0;
        downsample_rgb(src_b, width, height, row_b, next_b.data.data(), nw2, nh2);

        width = nw;
        height = nh;
        row_a = static_cast<size_t>(nw) * 3;
        row_b = static_cast<size_t>(nw2) * 3;
        std::swap(rgb_a.data, next_a.data);
        std::swap(rgb_b.data, next_b.data);
        src_a = rgb_a.data.data();
        src_b = rgb_b.data.data();
    }

    if (weight_sum <= 0.0) {
        return 0.0;
    }
    return dssim_from_ssim(ssim_sum / weight_sum);
}

}

double compute_dssim(const WorkingImage& reference, const WorkingImage& distorted) {
    if (reference.width != distorted.width || reference.height != distorted.height) {
        throw std::runtime_error("2FSdssim: dimension mismatch");
    }
    if (reference.channels != distorted.channels) {
        throw std::runtime_error("2FSdssim: channel mismatch");
    }
    if (reference.width < 8 || reference.height < 8) {
        throw std::runtime_error("2FSdssim: image too small");
    }
    if (reference.channels == 1) {
        return compute_dssim_gray(reference, distorted);
    }
    if (reference.channels == 3) {
        return compute_dssim_rgb(reference, distorted);
    }
    throw std::runtime_error("2FSdssim: unsupported channel count");
}
