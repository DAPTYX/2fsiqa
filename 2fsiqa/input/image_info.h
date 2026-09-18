// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

enum class SampleType : int {
    Integer = 0,
    Float32 = 1,
    Float64 = 2
};

struct ImageInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    int bit_depth = 0;
    int reported_bit_depth = 0;
    int channels = 0;
    bool has_alpha = false;
    SampleType sample_type = SampleType::Integer;
    size_t row_bytes = 0;
    size_t pixel_bytes = 0;

    std::vector<uint8_t> icc_profile;
    bool has_chromaticities = false;
    double white_x = 0.0;
    double white_y = 0.0;
    double red_x = 0.0;
    double red_y = 0.0;
    double green_x = 0.0;
    double green_y = 0.0;
    double blue_x = 0.0;
    double blue_y = 0.0;
    bool has_gamma = false;
    double gamma = 0.0;
};
