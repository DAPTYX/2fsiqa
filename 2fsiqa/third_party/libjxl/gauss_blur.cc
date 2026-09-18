#include "gauss_blur.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <future>
#include <thread>
#include <vector>

namespace {

void ZeroRange(float* p, size_t n) {
  std::memset(p, 0, n * sizeof(float));
}


constexpr double kPi = 3.141592653589793238;

void Invert3x3(const double m[3][3], double inv[3][3]) {
  const double det =
      m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
      m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
      m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
  const double inv_det = 1.0 / det;
  inv[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * inv_det;
  inv[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * inv_det;
  inv[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * inv_det;
  inv[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * inv_det;
  inv[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * inv_det;
  inv[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * inv_det;
  inv[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * inv_det;
  inv[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * inv_det;
  inv[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * inv_det;
}

void RecursiveGaussian1D(const RecursiveGaussian& rg, size_t xsize,
                         const float* in, float* out, float* forward) {
  const ptrdiff_t N = static_cast<ptrdiff_t>(rg.radius);
  const ptrdiff_t xs = static_cast<ptrdiff_t>(xsize);

  ZeroRange(forward, xsize);

  for (int pass = 0; pass < 3; ++pass) {
    const float n2 = rg.n2[pass];
    const float d1 = rg.d1[pass];
    float prev = 0.0f;
    float prev2 = 0.0f;
    for (ptrdiff_t n = -N + 1; n < xs; ++n) {
      const ptrdiff_t left = n - N - 1;
      const ptrdiff_t right = n + N - 1;
      const float left_val = (left >= 0) ? in[left] : 0.0f;
      const float right_val = (right < xs) ? in[right] : 0.0f;
      const float sum = left_val + right_val;
      const float cur = n2 * sum - d1 * prev - prev2;
      prev2 = prev;
      prev = cur;
      if (n >= 0) {
        forward[static_cast<size_t>(n)] += cur;
      }
    }
  }

  ZeroRange(out, xsize);
  for (int pass = 0; pass < 3; ++pass) {
    const float n2 = rg.n2[pass];
    const float d1 = rg.d1[pass];
    float prev = 0.0f;
    float prev2 = 0.0f;
    for (ptrdiff_t n = xs + N - 2; n >= -N + 1; --n) {
      const ptrdiff_t left = n - N + 1;
      const ptrdiff_t right = n + N + 1;
      const float left_val =
          (left >= 0 && left < xs) ? forward[static_cast<size_t>(left)] : 0.0f;
      const float right_val =
          (right >= 0 && right < xs) ? forward[static_cast<size_t>(right)]
                                     : 0.0f;
      const float sum = left_val + right_val;
      const float cur = n2 * sum - d1 * prev - prev2;
      prev2 = prev;
      prev = cur;
      if (n >= 0 && n < xs) {
        out[static_cast<size_t>(n)] += cur;
      }
    }
  }
}

unsigned WorkerCount(size_t work_items) {
  unsigned n = std::thread::hardware_concurrency();
  if (n == 0) {
    n = 4;
  }
  if (n > 8) {
    n = 8;
  }
  if (work_items < 64) {
    n = 1;
  }
  return static_cast<unsigned>(std::min<size_t>(n, work_items));
}

}  // namespace

RecursiveGaussian CreateRecursiveGaussian(double sigma) {
  RecursiveGaussian rg;
  const double radius = std::round(3.2795 * sigma + 0.2546);

  const double pi_div_2r = kPi / (2.0 * radius);
  const double omega[3] = {pi_div_2r, 3.0 * pi_div_2r, 5.0 * pi_div_2r};

  const double p_1 = +1.0 / std::tan(0.5 * omega[0]);
  const double p_3 = -1.0 / std::tan(0.5 * omega[1]);
  const double p_5 = +1.0 / std::tan(0.5 * omega[2]);

  const double r_1 = +p_1 * p_1 / std::sin(omega[0]);
  const double r_3 = -p_3 * p_3 / std::sin(omega[1]);
  const double r_5 = +p_5 * p_5 / std::sin(omega[2]);

  const double neg_half_sigma2 = -0.5 * sigma * sigma;
  const double recip_radius = 1.0 / radius;
  double rho[3];
  for (size_t i = 0; i < 3; ++i) {
    rho[i] = std::exp(neg_half_sigma2 * omega[i] * omega[i]) * recip_radius;
  }

  const double D_13 = p_1 * r_3 - r_1 * p_3;
  const double D_35 = p_3 * r_5 - r_3 * p_5;
  const double D_51 = p_5 * r_1 - r_5 * p_1;
  const double recip_d13 = 1.0 / D_13;
  const double zeta_15 = D_35 * recip_d13;
  const double zeta_35 = D_51 * recip_d13;

  double A[3][3] = {
      {p_1, p_3, p_5},
      {r_1, r_3, r_5},
      {zeta_15, zeta_35, 1.0},
  };
  double invA[3][3];
  Invert3x3(A, invA);

  const double gamma[3] = {
      1.0,
      radius * radius - sigma * sigma,
      zeta_15 * rho[0] + zeta_35 * rho[1] + rho[2],
  };

  double beta[3];
  for (size_t i = 0; i < 3; ++i) {
    beta[i] = invA[i][0] * gamma[0] + invA[i][1] * gamma[1] + invA[i][2] * gamma[2];
  }

  rg.radius = static_cast<size_t>(radius);
  for (size_t i = 0; i < 3; ++i) {
    rg.n2[i] =
        static_cast<float>(-beta[i] * std::cos(omega[i] * (radius + 1.0)));
    rg.d1[i] = static_cast<float>(-2.0 * std::cos(omega[i]));
  }
  return rg;
}

void FastGaussian1D(const RecursiveGaussian& rg, size_t xsize, const float* in,
                    float* out, float* forward_scratch) {
  RecursiveGaussian1D(rg, xsize, in, out, forward_scratch);
}

void FastGaussian(const RecursiveGaussian& rg, size_t xsize, size_t ysize,
                  const GetConstRow& in, const GetRow& temp, const GetRow& out,
                  GaussianScratch& scratch) {
  scratch.EnsureRow(std::max(xsize, ysize));
  scratch.EnsureCol(ysize);

  const unsigned row_workers = WorkerCount(ysize);
  if (row_workers == 1) {
    for (size_t y = 0; y < ysize; ++y) {
      FastGaussian1D(rg, xsize, in(y), temp(y), scratch.forward.data());
    }
  } else {
    std::vector<std::future<void>> futures;
    futures.reserve(row_workers);
    const size_t chunk = (ysize + row_workers - 1) / row_workers;
    for (unsigned t = 0; t < row_workers; ++t) {
      const size_t y0 = t * chunk;
      if (y0 >= ysize) {
        break;
      }
      const size_t y1 = std::min(y0 + chunk, ysize);
      futures.push_back(std::async(std::launch::async, [&, y0, y1]() {
        std::vector<float> local_forward(std::max(xsize, ysize));
        for (size_t y = y0; y < y1; ++y) {
          FastGaussian1D(rg, xsize, in(y), temp(y), local_forward.data());
        }
      }));
    }
    for (auto& f : futures) {
      f.get();
    }
  }

  const unsigned col_workers = WorkerCount(xsize);
  if (col_workers == 1) {
    for (size_t x = 0; x < xsize; ++x) {
      for (size_t y = 0; y < ysize; ++y) {
        scratch.col_in[y] = temp(y)[x];
      }
      FastGaussian1D(rg, ysize, scratch.col_in.data(), scratch.col_out.data(),
                     scratch.forward.data());
      for (size_t y = 0; y < ysize; ++y) {
        out(y)[x] = scratch.col_out[y];
      }
    }
  } else {
    std::vector<std::future<void>> futures;
    futures.reserve(col_workers);
    const size_t chunk = (xsize + col_workers - 1) / col_workers;
    for (unsigned t = 0; t < col_workers; ++t) {
      const size_t x0 = t * chunk;
      if (x0 >= xsize) {
        break;
      }
      const size_t x1 = std::min(x0 + chunk, xsize);
      futures.push_back(std::async(std::launch::async, [&, x0, x1]() {
        std::vector<float> col_in(ysize);
        std::vector<float> col_out(ysize);
        std::vector<float> local_forward(std::max(xsize, ysize));
        for (size_t x = x0; x < x1; ++x) {
          for (size_t y = 0; y < ysize; ++y) {
            col_in[y] = temp(y)[x];
          }
          FastGaussian1D(rg, ysize, col_in.data(), col_out.data(),
                         local_forward.data());
          for (size_t y = 0; y < ysize; ++y) {
            out(y)[x] = col_out[y];
          }
        }
      }));
    }
    for (auto& f : futures) {
      f.get();
    }
  }
}
