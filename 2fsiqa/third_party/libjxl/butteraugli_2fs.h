// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#pragma once

#include "image3f.h"

struct ButteraugliScores {
  double max_norm = 0.0;
  double pnorm3 = 0.0;
};

ButteraugliScores Compute2FSbutteraugli(Image3F& linear_orig,
                                        Image3F& linear_dist);
