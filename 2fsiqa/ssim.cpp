// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#include "ssim.h"
#include "cmm.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <future>
#include <thread>

namespace {

int adaptive_block_side(uint32_t width, uint32_t height) {
    const int w = static_cast<int>(width);
    const int h = static_cast<int>(height);
    int side = static_cast<int>(std::lround(static_cast<double>(w + h) / 200.0));
    if (side < 4) {
        side = 4;
    }
    const int max_side = std::min(w, h) / 4;
    if (max_side < 2) {
        return std::max(1, std::min(w, h) / 2);
    }
    if (side > max_side) {
        side = max_side;
    }
    return side;
}

void build_origins(int dim, int block, int macro, std::vector<int>& origins) {
    origins.clear();
    if (dim < macro || block < 1) {
        return;
    }
    const int last = dim - macro;
    for (int pos = 0; pos < last; pos += block) {
        origins.push_back(pos);
    }
    if (origins.empty() || origins.back() != last) {
        origins.push_back(last);
    }
}

inline float ssim_window(double s1, double s2, double ss, double s12,
                         double peak, double window_n) {
    const double c1 = (0.01 * 0.01 * peak * peak) * window_n;
    const double c2 = (0.03 * 0.03 * peak * peak) * window_n * (window_n - 1.0);
    const double vars = ss * window_n - s1 * s1 - s2 * s2;
    const double covar = s12 * window_n - s1 * s2;
    const double num = (2.0 * s1 * s2 + c1) * (2.0 * covar + c2);
    const double den = (s1 * s1 + s2 * s2 + c1) * (vars + c2);
    return static_cast<float>(num / den);
}

void accumulate_block_gray(const float* ref, const float* dist,
                           size_t stride, int block, int origin_x, int origin_y,
                           double* out) {
    double s1 = 0.0, s2 = 0.0, ss = 0.0, s12 = 0.0;
    for (int y = 0; y < block; ++y) {
        const float* ref_row = ref + static_cast<size_t>(origin_y + y) * stride + origin_x;
        const float* dist_row = dist + static_cast<size_t>(origin_y + y) * stride + origin_x;
        for (int x = 0; x < block; ++x) {
            const double a = ref_row[x];
            const double b = dist_row[x];
            s1 += a;
            s2 += b;
            ss += a * a + b * b;
            s12 += a * b;
        }
    }
    out[0] = s1;
    out[1] = s2;
    out[2] = ss;
    out[3] = s12;
}

void accumulate_block_rgb(const float* ref, const float* dist,
                          size_t row_samples, int channel, int channels,
                          int block, int origin_x, int origin_y,
                          double* out) {
    double s1 = 0.0, s2 = 0.0, ss = 0.0, s12 = 0.0;
    for (int y = 0; y < block; ++y) {
        const float* ref_row = ref + static_cast<size_t>(origin_y + y) * row_samples + channel;
        const float* dist_row = dist + static_cast<size_t>(origin_y + y) * row_samples + channel;
        for (int x = 0; x < block; ++x) {
            const size_t idx = static_cast<size_t>(origin_x + x) * static_cast<size_t>(channels);
            const double a = ref_row[idx];
            const double b = dist_row[idx];
            s1 += a;
            s2 += b;
            ss += a * a + b * b;
            s12 += a * b;
        }
    }
    out[0] = s1;
    out[1] = s2;
    out[2] = ss;
    out[3] = s12;
}

double ssim_plane(const float* reference, const float* distorted,
                  uint32_t width, uint32_t height, size_t row_samples,
                  int channel, int channels, double peak) {
    const int w = static_cast<int>(width);
    const int h = static_cast<int>(height);
    const int block = adaptive_block_side(width, height);
    const int macro = block * 2;
    if (w < macro || h < macro || block < 1) {
        return 1.0;
    }

    std::vector<int> origin_x;
    std::vector<int> origin_y;
    build_origins(w, block, macro, origin_x);
    build_origins(h, block, macro, origin_y);

    const int blocks_x = static_cast<int>(origin_x.size());
    const int blocks_y = static_cast<int>(origin_y.size());
    if (blocks_x < 2 || blocks_y < 2) {
        return 1.0;
    }

    const int windows_x = blocks_x - 1;
    const int windows_y = blocks_y - 1;
    const double window_n = static_cast<double>(macro) * static_cast<double>(macro);

    unsigned int thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0) {
        thread_count = 4;
    }
    const int chunks = std::min(static_cast<int>(thread_count), windows_y);
    std::vector<std::future<double>> futures;
    futures.reserve(static_cast<size_t>(chunks));

    for (int t = 0; t < chunks; ++t) {
        const int start_y = windows_y * t / chunks;
        const int end_y = windows_y * (t + 1) / chunks;

        futures.push_back(std::async(std::launch::async, [=, &origin_x, &origin_y]() {
            const int sum_width = blocks_x + 2;
            std::vector<double> sum0_storage(static_cast<size_t>(sum_width) * 4);
            std::vector<double> sum1_storage(static_cast<size_t>(sum_width) * 4);
            double (*sum0)[4] = reinterpret_cast<double (*)[4]>(sum0_storage.data());
            double (*sum1)[4] = reinterpret_cast<double (*)[4]>(sum1_storage.data());

            double local_total = 0.0;
            int z = start_y;

            for (int y = start_y; y < end_y; ++y) {
                for (; z <= y + 1; ++z) {
                    std::swap(sum0, sum1);
                    const int oy = origin_y[static_cast<size_t>(z)];
                    for (int bx = 0; bx < blocks_x; ++bx) {
                        const int ox = origin_x[static_cast<size_t>(bx)];
                        if (channels == 1) {
                            accumulate_block_gray(reference, distorted, row_samples,
                                                  block, ox, oy, sum0[bx]);
                        } else {
                            accumulate_block_rgb(reference, distorted, row_samples,
                                                 channel, channels, block, ox, oy, sum0[bx]);
                        }
                    }
                }
                for (int x = 0; x < windows_x; ++x) {
                    const double s1 = sum0[x][0] + sum0[x + 1][0]
                                    + sum1[x][0] + sum1[x + 1][0];
                    const double s2 = sum0[x][1] + sum0[x + 1][1]
                                    + sum1[x][1] + sum1[x + 1][1];
                    const double ss = sum0[x][2] + sum0[x + 1][2]
                                    + sum1[x][2] + sum1[x + 1][2];
                    const double s12 = sum0[x][3] + sum0[x + 1][3]
                                     + sum1[x][3] + sum1[x + 1][3];
                    local_total += ssim_window(s1, s2, ss, s12, peak, window_n);
                }
            }
            return local_total;
        }));
    }

    double total = 0.0;
    for (auto& fut : futures) {
        total += fut.get();
    }
    return total / (static_cast<double>(windows_x) * static_cast<double>(windows_y));
}

}

double compute_ssim(const float* reference, const float* distorted,
                    uint32_t width, uint32_t height, int channels,
                    size_t row_samples) {
    if (width < 8 || height < 8 || channels < 1) {
        return 1.0;
    }

    const double peak = 1.0;
    return ssim_plane(reference, distorted, width, height,
                      row_samples, 0, channels, peak);
}

std::array<double, 3> compute_ssim_channels(const float* reference, const float* distorted,
                                            uint32_t width, uint32_t height, int channels,
                                            size_t row_samples) {
    std::array<double, 3> result = {1.0, 1.0, 1.0};
    if (width < 8 || height < 8 || channels < 3) {
        return result;
    }

    const double peak = 1.0;
    result[ChannelR] = ssim_plane(reference, distorted, width, height,
                                  row_samples, ChannelR, channels, peak);
    result[ChannelG] = ssim_plane(reference, distorted, width, height,
                                  row_samples, ChannelG, channels, peak);
    result[ChannelB] = ssim_plane(reference, distorted, width, height,
                                  row_samples, ChannelB, channels, peak);
    return result;
}
