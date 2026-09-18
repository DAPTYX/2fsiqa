// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#include "psnr.h"
#include "cmm.h"

#include <future>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <thread>
#include <array>

namespace {

double mse_float32_range(const float* reference, const float* distorted,
                         size_t range_begin, size_t range_end) {
    double total = 0.0;
    for (size_t i = range_begin; i < range_end; ++i) {
        const double diff = static_cast<double>(reference[i]) - static_cast<double>(distorted[i]);
        total += diff * diff;
    }
    return total;
}

void mse_float32_channels_range(const float* reference, const float* distorted,
                                size_t range_begin, size_t range_end,
                                int channels, double* out_sums) {
    if (channels == 1) {
        out_sums[0] = mse_float32_range(reference, distorted, range_begin, range_end);
        out_sums[1] = 0.0;
        out_sums[2] = 0.0;
        return;
    }

    double sum0 = 0.0;
    double sum1 = 0.0;
    double sum2 = 0.0;

    size_t i = range_begin;
    for (; i + 3 <= range_end; i += 3) {
        const double dr = static_cast<double>(reference[i + 0]) - static_cast<double>(distorted[i + 0]);
        const double dg = static_cast<double>(reference[i + 1]) - static_cast<double>(distorted[i + 1]);
        const double db = static_cast<double>(reference[i + 2]) - static_cast<double>(distorted[i + 2]);
        sum0 += dr * dr;
        sum1 += dg * dg;
        sum2 += db * db;
    }

    out_sums[0] = sum0;
    out_sums[1] = sum1;
    out_sums[2] = sum2;
}

}  // namespace

double compute_mse(const float* reference, const float* distorted, size_t total_samples) {
    if (total_samples == 0) {
        return 0.0;
    }

    unsigned thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0) {
        thread_count = 4;
    }
    if (total_samples < 262144) {
        thread_count = 1;
    }

    const size_t chunk_samples = (total_samples + thread_count - 1) / thread_count;
    std::vector<std::future<double>> futures;
    futures.reserve(thread_count);

    for (unsigned thread_index = 0; thread_index < thread_count; ++thread_index) {
        const size_t range_begin = thread_index * chunk_samples;
        const size_t range_end = std::min(range_begin + chunk_samples, total_samples);
        if (range_begin >= total_samples) {
            break;
        }

        futures.emplace_back(std::async(std::launch::async, [=]() {
            return mse_float32_range(reference, distorted, range_begin, range_end);
        }));
    }

    double total = 0.0;
    for (auto& fut : futures) {
        total += fut.get();
    }
    return total / static_cast<double>(total_samples);
}

std::array<double, 3> compute_mse_channels(const float* reference, const float* distorted,
                                           size_t total_samples, int channels) {
    std::array<double, 3> result = {0.0, 0.0, 0.0};
    if (total_samples == 0 || channels < 1) {
        return result;
    }

    if (channels == 1) {
        result[0] = compute_mse(reference, distorted, total_samples);
        return result;
    }

    const size_t pixel_count = total_samples / static_cast<size_t>(channels);

    unsigned thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0) {
        thread_count = 4;
    }
    if (pixel_count < 262144) {
        thread_count = 1;
    }

    const size_t chunk_samples = ((pixel_count + thread_count - 1) / thread_count) * static_cast<size_t>(channels);
    std::vector<std::future<std::array<double, 3>>> futures;
    futures.reserve(thread_count);

    for (unsigned thread_index = 0; thread_index < thread_count; ++thread_index) {
        const size_t range_begin = thread_index * chunk_samples;
        const size_t range_end = std::min(range_begin + chunk_samples, total_samples);
        if (range_begin >= total_samples) {
            break;
        }

        futures.emplace_back(std::async(std::launch::async, [=]() {
            double local[3] = {0.0, 0.0, 0.0};
            mse_float32_channels_range(reference, distorted, range_begin, range_end, channels, local);
            return std::array<double, 3>{local[0], local[1], local[2]};
        }));
    }

    for (auto& fut : futures) {
        const auto partial = fut.get();
        result[0] += partial[0];
        result[1] += partial[1];
        result[2] += partial[2];
    }

    const double inv = 1.0 / static_cast<double>(pixel_count);
    result[0] *= inv;
    result[1] *= inv;
    result[2] *= inv;
    return result;
}

double psnr_inf_fallback_peak(int reported_bit_depth) {
    if (reported_bit_depth <= 1) return 20.52743879660661719;
    if (reported_bit_depth == 2) return 24.36265906048794250;
    if (reported_bit_depth == 3) return 32.71917969101289003;
    if (reported_bit_depth == 4) return 39.38691762192354417;
    if (reported_bit_depth == 5) return 45.33571334135487518;
    if (reported_bit_depth == 6) return 51.89406430624571698;
    if (reported_bit_depth == 7) return 57.47154941911067283;
    if (reported_bit_depth == 8) return 64.17501352899263622;
    if (reported_bit_depth == 9) return 70.14780319047090984;
    if (reported_bit_depth == 10) return 76.13639401939916240;
    if (reported_bit_depth == 11) return 82.14104159915294190;
    if (reported_bit_depth == 12) return 88.15141572085119037;
    if (reported_bit_depth == 13) return 94.11150058205750213;
    return 144.737197309002645795;
}
