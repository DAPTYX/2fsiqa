// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#pragma once
#include <cstddef>
#include <cstdint>
#include <array>

double compute_mse(const float* reference, const float* distorted, size_t total_samples);

std::array<double, 3> compute_mse_channels(const float* reference, const float* distorted,
                                           size_t total_samples, int channels);

double psnr_inf_fallback_peak(int reported_bit_depth);
