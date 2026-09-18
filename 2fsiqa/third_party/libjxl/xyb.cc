#include "xyb.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <cmath>
#include <future>
#include <thread>
#include <vector>

namespace {

constexpr float kOpsinM00 = 0.30f;
constexpr float kOpsinM01 = 0.622f;
constexpr float kOpsinM02 = 0.078f;
constexpr float kOpsinM10 = 0.23f;
constexpr float kOpsinM11 = 0.692f;
constexpr float kOpsinM12 = 0.078f;
constexpr float kOpsinM20 = 0.24342268924547819f;
constexpr float kOpsinM21 = 0.20476744424496821f;
constexpr float kOpsinM22 = 0.5518098665095536f;

constexpr float kOpsinBias = 0.0037930732552754493f;
constexpr float kNegCbrtBias = -0.15595420054924863f;

constexpr float kMapAllRealToOpsin00 = 2.458617915659094f;
constexpr float kMapAllRealToOpsin01 = -1.1479488864297331f;
constexpr float kMapAllRealToOpsin02 = -0.3106688755154754f;
constexpr float kMapAllRealToOpsin10 = -0.2765451413492818f;
constexpr float kMapAllRealToOpsin11 = 1.360414093465418f;
constexpr float kMapAllRealToOpsin12 = -0.0838691397172298f;
constexpr float kMapAllRealToOpsin20 = -0.0103543044597107f;
constexpr float kMapAllRealToOpsin21 = -0.1626034365635163f;
constexpr float kMapAllRealToOpsin22 = 1.1729577614258124f;

inline float CubeRootPositive(float v) {
  if (v <= 0.0f) {
    return 0.0f;
  }
  uint32_t bits = 0;
  std::memcpy(&bits, &v, sizeof(bits));
  bits = bits / 3u + 0x2a5137a0u;
  float y = 0.0f;
  std::memcpy(&y, &bits, sizeof(y));
  double yd = static_cast<double>(y);
  const double vd = static_cast<double>(v);
  for (int i = 0; i < 4; ++i) {
    yd = (2.0 * yd + vd / (yd * yd)) / 3.0;
  }
  return static_cast<float>(yd);
}

void OpsinAndStoreXyb(float r, float g, float b,
                      float& out_x, float& out_y, float& out_b) {
  float mixed0 = kOpsinM00 * r + kOpsinM01 * g + kOpsinM02 * b + kOpsinBias;
  float mixed1 = kOpsinM10 * r + kOpsinM11 * g + kOpsinM12 * b + kOpsinBias;
  float mixed2 = kOpsinM20 * r + kOpsinM21 * g + kOpsinM22 * b + kOpsinBias;

  mixed0 = CubeRootPositive(mixed0) + kNegCbrtBias;
  mixed1 = CubeRootPositive(mixed1) + kNegCbrtBias;
  mixed2 = CubeRootPositive(mixed2) + kNegCbrtBias;

  out_x = 0.5f * (mixed0 - mixed1);
  out_y = 0.5f * (mixed0 + mixed1);
  out_b = mixed2;
}

void MapAllRealToOpsin(float ar, float ag, float ab,
                       float& r, float& g, float& b) {
  r = kMapAllRealToOpsin00 * ar + kMapAllRealToOpsin01 * ag +
      kMapAllRealToOpsin02 * ab;
  g = kMapAllRealToOpsin10 * ar + kMapAllRealToOpsin11 * ag +
      kMapAllRealToOpsin12 * ab;
  b = kMapAllRealToOpsin20 * ar + kMapAllRealToOpsin21 * ag +
      kMapAllRealToOpsin22 * ab;
}

unsigned WorkerCount(size_t rows) {
  unsigned n = std::thread::hardware_concurrency();
  if (n == 0) {
    n = 4;
  }
  if (n > 8) {
    n = 8;
  }
  if (rows < 64) {
    n = 1;
  }
  return static_cast<unsigned>(std::min<size_t>(n, rows));
}

void ForRowsParallel(size_t ysize, const std::function<void(size_t, size_t)>& fn) {
  const unsigned workers = WorkerCount(ysize);
  if (workers == 1) {
    fn(0, ysize);
    return;
  }
  std::vector<std::future<void>> futures;
  futures.reserve(workers);
  const size_t chunk = (ysize + workers - 1) / workers;
  for (unsigned t = 0; t < workers; ++t) {
    const size_t y0 = t * chunk;
    if (y0 >= ysize) {
      break;
    }
    const size_t y1 = std::min(y0 + chunk, ysize);
    futures.push_back(std::async(std::launch::async, [&fn, y0, y1]() {
      fn(y0, y1);
    }));
  }
  for (auto& f : futures) {
    f.get();
  }
}

}

void LinearAllRealToXyb(Image3F& image) {
  ForRowsParallel(image.ysize, [&](size_t y0, size_t y1) {
    for (size_t y = y0; y < y1; ++y) {
      float* row0 = image.PlaneRow(0, y);
      float* row1 = image.PlaneRow(1, y);
      float* row2 = image.PlaneRow(2, y);
      for (size_t x = 0; x < image.xsize; ++x) {
        float r, g, b;
        MapAllRealToOpsin(row0[x], row1[x], row2[x], r, g, b);
        OpsinAndStoreXyb(r, g, b, row0[x], row1[x], row2[x]);
      }
    }
  });
}

void MakePositiveXyb(Image3F& image) {
  ForRowsParallel(image.ysize, [&](size_t y0, size_t y1) {
    for (size_t y = y0; y < y1; ++y) {
      float* rowY = image.PlaneRow(1, y);
      float* rowB = image.PlaneRow(2, y);
      float* rowX = image.PlaneRow(0, y);
      for (size_t x = 0; x < image.xsize; ++x) {
        rowB[x] = (rowB[x] - rowY[x]) + 0.55f;
        rowX[x] = rowX[x] * 14.f + 0.42f;
        rowY[x] += 0.01f;
      }
    }
  });
}
