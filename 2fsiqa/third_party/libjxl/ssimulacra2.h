#pragma once

#include "image3f.h"

#include <vector>

struct MsssimScale {
  double avg_ssim[3 * 2];
  double avg_edgediff[3 * 4];
};

struct Msssim {
  std::vector<MsssimScale> scales;

  double Score() const;
};

Msssim Compute2FSssimulacra2(const Image3F& linear_orig,
                             const Image3F& linear_dist);
