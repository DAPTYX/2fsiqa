// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#pragma once

#include "yuv_allreal.h"

#include <string>

struct VmafScore {
    double score = 0.0;
    bool ok = false;
    std::string error;
};

VmafScore compute_2fs_vmaf(const Yuv444i12& ref, const Yuv444i12& dist);
