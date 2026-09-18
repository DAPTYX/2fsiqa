// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#include "butteraugli_2fs.h"

#include <array>
#include <cmath>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

#include "butteraugli.h"
#include "image.h"
#include "memory_manager.h"

namespace {

double DistanceP3(const ba::ImageF& distmap) {
  const size_t xsize = distmap.xsize();
  const size_t ysize = distmap.ysize();
  if (xsize == 0 || ysize == 0) return 0.0;

  const double one_per_pixels =
      1.0 / (static_cast<double>(ysize) * static_cast<double>(xsize));
  constexpr double p = 3.0;

  unsigned worker_count = std::thread::hardware_concurrency();
  if (worker_count == 0) worker_count = 4;
  worker_count = std::min(worker_count, static_cast<unsigned>(ysize));
  if (worker_count < 1) worker_count = 1;

  std::vector<std::array<double, 3>> partial(worker_count);
  for (auto& sums : partial) sums = {0.0, 0.0, 0.0};

  std::vector<std::future<void>> futures;
  futures.reserve(worker_count);

  for (unsigned t = 0; t < worker_count; ++t) {
    const size_t y0 = ysize * t / worker_count;
    const size_t y1 = ysize * (t + 1) / worker_count;
    futures.push_back(std::async(std::launch::async, [&, t, y0, y1]() {
      double sum0 = 0.0;
      double sum1 = 0.0;
      double sum2 = 0.0;
      for (size_t y = y0; y < y1; ++y) {
        const float* row = distmap.ConstRow(y);
        for (size_t x = 0; x < xsize; ++x) {
          const double d = static_cast<double>(row[x]);
          double d2 = d * d * d;
          sum0 += d2;
          d2 *= d2;
          sum1 += d2;
          d2 *= d2;
          sum2 += d2;
        }
      }
      partial[t][0] = sum0;
      partial[t][1] = sum1;
      partial[t][2] = sum2;
    }));
  }

  for (auto& f : futures) f.get();

  double sum1[3] = {0.0, 0.0, 0.0};
  for (const auto& part : partial) {
    sum1[0] += part[0];
    sum1[1] += part[1];
    sum1[2] += part[2];
  }

  double v = 0.0;
  for (int i = 0; i < 3; ++i) {
    v += std::pow(one_per_pixels * sum1[i], 1.0 / (p * (1 << i)));
  }
  return v / 3.0;
}

}

ButteraugliScores Compute2FSbutteraugli(Image3F& linear_orig,
                                        Image3F& linear_dist) {
  if (linear_orig.xsize != linear_dist.xsize ||
      linear_orig.ysize != linear_dist.ysize) {
    throw std::runtime_error("2FSbutteraugli: dimension mismatch");
  }
  if (linear_orig.xsize < 8 || linear_orig.ysize < 8) {
    throw std::runtime_error("2FSbutteraugli: image too small");
  }

  ba::Image3F rgb0 = std::move(linear_orig.impl);
  ba::Image3F rgb1 = std::move(linear_dist.impl);
  linear_orig.xsize = linear_orig.ysize = 0;
  linear_dist.xsize = linear_dist.ysize = 0;

  ba::ButteraugliParams params;
  params.hf_asymmetry = 1.0f;
  params.xmul = 1.0f;
  params.intensity_target = 80.0f;

  ba::MemoryManager* memory_manager = ba::DefaultMemoryManager();
  auto diff_or =
      ba::ImageF::Create(memory_manager, rgb0.xsize(), rgb0.ysize());
  if (!diff_or.ok()) {
    throw std::runtime_error("2FSbutteraugli: diffmap alloc failed");
  }
  ba::ImageF diffmap = std::move(diff_or).value_();

  double diffvalue = 0.0;
  ba::Status st = ba::ButteraugliInterfaceInPlace(
      std::move(rgb0), std::move(rgb1), params, diffmap, diffvalue);
  if (!st) {
    throw std::runtime_error("2FSbutteraugli: Diffmap failed");
  }

  ButteraugliScores scores;
  scores.max_norm = diffvalue;
  scores.pnorm3 = DistanceP3(diffmap);
  return scores;
}
