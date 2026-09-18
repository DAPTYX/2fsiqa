#pragma once

#include "support.h"
#include "image.h"

namespace ba {

struct WeightsSeparable5 {
  float horz[3 * 4];
  float vert[3 * 4];
};

Status SlowSeparable5(const ImageF& in, const Rect& in_rect,
                      const WeightsSeparable5& weights, ImageF* out,
                      const Rect& out_rect);

Status Separable5(const ImageF& in, const Rect& rect,
                  const WeightsSeparable5& weights, ImageF* out);

}  // namespace ba
