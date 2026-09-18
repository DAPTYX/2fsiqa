// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#pragma once
#include <cstdint>
#include <cstddef>
#include <array>

double compute_ssim(const float* reference, const float* distorted,
                    uint32_t width, uint32_t height, int channels,
                    size_t row_samples);

std::array<double, 3> compute_ssim_channels(const float* reference, const float* distorted,
                                            uint32_t width, uint32_t height, int channels,
                                            size_t row_samples);
