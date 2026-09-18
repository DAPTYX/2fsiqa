#pragma once

#include <cstddef>
#include <functional>
#include <vector>

struct RecursiveGaussian {
  float n2[3];
  float d1[3];
  size_t radius;
};

RecursiveGaussian CreateRecursiveGaussian(double sigma);

struct GaussianScratch {
  std::vector<float> forward;
  std::vector<float> col_in;
  std::vector<float> col_out;

  void EnsureRow(size_t n) {
    if (forward.size() < n) {
      forward.resize(n);
    }
  }

  void EnsureCol(size_t n) {
    if (col_in.size() < n) {
      col_in.resize(n);
      col_out.resize(n);
    }
  }
};

void FastGaussian1D(const RecursiveGaussian& rg, size_t xsize,
                    const float* in, float* out, float* forward_scratch);

using GetConstRow = std::function<const float*(size_t)>;
using GetRow = std::function<float*(size_t)>;

void FastGaussian(const RecursiveGaussian& rg, size_t xsize, size_t ysize,
                  const GetConstRow& in, const GetRow& temp, const GetRow& out,
                  GaussianScratch& scratch);
