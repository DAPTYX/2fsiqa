// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#pragma once

#include "cmm.h"
#include <cstdint>
#include <cstddef>
#include <memory>

struct Yuv444i12 {
    std::unique_ptr<uint16_t[]> y;
    std::unique_ptr<uint16_t[]> cb;
    std::unique_ptr<uint16_t[]> cr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    unsigned bpc = 12;
};

Yuv444i12 working_rgb_to_yuv444_allreal_12bit_full(const WorkingImage& rgb);
