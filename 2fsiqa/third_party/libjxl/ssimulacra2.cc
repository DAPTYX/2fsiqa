#include "ssimulacra2.h"

#include "gauss_blur.h"
#include "xyb.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <future>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

void MultiplyPlaneRange(const float* a, const float* b, float* out, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    out[i] = a[i] * b[i];
  }
}

void CopyPlaneRange(const float* src, float* dst, size_t n) {
  std::memcpy(dst, src, n * sizeof(float));
}


constexpr float kC2 = 0.0009f;
constexpr int kNumScales = 6;
constexpr float kDiffEps = 0.0009f;

void AttenuatePairDiffs(Image3F& a, Image3F& b) {
  for (size_t c = 0; c < 3; ++c) {
    for (size_t y = 0; y < a.ysize; ++y) {
      float* ra = a.PlaneRow(c, y);
      float* rb = b.PlaneRow(c, y);
      for (size_t x = 0; x < a.xsize; ++x) {
        const float delta = rb[x] - ra[x];
        const float ad = std::fabs(delta);
        const float t = ad / (ad + kDiffEps);
        const float shrunk = delta * t * t;
        const float mid = 0.5f * (ra[x] + rb[x]);
        ra[x] = mid - 0.5f * shrunk;
        rb[x] = mid + 0.5f * shrunk;
      }
    }
  }
}

Image3F Downsample(const Image3F& in, size_t fx, size_t fy) {
  const size_t out_xsize = (in.xsize + fx - 1) / fx;
  const size_t out_ysize = (in.ysize + fy - 1) / fy;
  Image3F out = Image3F::Create(out_xsize, out_ysize);
  const double normalize = 1.0 / static_cast<double>(fx * fy);

  auto downsample_plane = [&](size_t c) {
    for (size_t oy = 0; oy < out_ysize; ++oy) {
      float* row_out = out.PlaneRow(c, oy);
      for (size_t ox = 0; ox < out_xsize; ++ox) {
        double sum = 0.0;
        for (size_t iy = 0; iy < fy; ++iy) {
          for (size_t ix = 0; ix < fx; ++ix) {
            const size_t x = std::min(ox * fx + ix, in.xsize - 1);
            const size_t y = std::min(oy * fy + iy, in.ysize - 1);
            sum += static_cast<double>(in.PlaneRow(c, y)[x]);
          }
        }
        row_out[ox] = static_cast<float>(sum * normalize);
      }
    }
  };

  std::future<void> f0 = std::async(std::launch::async, downsample_plane, 0);
  std::future<void> f1 = std::async(std::launch::async, downsample_plane, 1);
  downsample_plane(2);
  f0.get();
  f1.get();
  return out;
}

class Blur {
 public:
  explicit Blur(size_t xsize, size_t ysize)
      : rg_(CreateRecursiveGaussian(1.5)),
        temp_(ImageF::Create(xsize, ysize)) {
    scratch_.EnsureRow(std::max(xsize, ysize));
    scratch_.EnsureCol(ysize);
  }

  void BlurPlane(const ImageF& in, ImageF& out) {
    FastGaussian(
        rg_, in.xsize, in.ysize,
        [&](size_t y) { return in.ConstRow(y); },
        [&](size_t y) { return temp_.Row(y); },
        [&](size_t y) { return out.Row(y); },
        scratch_);
  }

  void BlurPlaneFromImage3(const Image3F& in, size_t c, ImageF& out) {
    FastGaussian(
        rg_, in.xsize, in.ysize,
        [&](size_t y) { return in.PlaneRow(c, y); },
        [&](size_t y) { return temp_.Row(y); },
        [&](size_t y) { return out.Row(y); },
        scratch_);
  }

  void ShrinkTo(size_t xsize, size_t ysize) {
    temp_.ShrinkTo(xsize, ysize);
  }

 private:
  RecursiveGaussian rg_;
  ImageF temp_;
  GaussianScratch scratch_;
};

double quartic(double x) {
  x *= x;
  x *= x;
  return x;
}

void SSIMMapPlane(const ImageF& m1, const ImageF& m2, const ImageF& s11,
                  const ImageF& s22, const ImageF& s12, double* avg_out) {
  const double onePerPixels =
      1.0 / static_cast<double>(m1.ysize * m1.xsize);
  double sum1[2] = {0.0, 0.0};
  for (size_t y = 0; y < m1.ysize; ++y) {
    const float* row_m1 = m1.ConstRow(y);
    const float* row_m2 = m2.ConstRow(y);
    const float* row_s11 = s11.ConstRow(y);
    const float* row_s22 = s22.ConstRow(y);
    const float* row_s12 = s12.ConstRow(y);
    for (size_t x = 0; x < m1.xsize; ++x) {
      const float mu1 = row_m1[x];
      const float mu2 = row_m2[x];
      const float mu11 = mu1 * mu1;
      const float mu22 = mu2 * mu2;
      const float mu12 = mu1 * mu2;
      const float num_m = 1.0f - (mu1 - mu2) * (mu1 - mu2);
      const float num_s = 2.0f * (row_s12[x] - mu12) + kC2;
      const float denom_s = (row_s11[x] - mu11) + (row_s22[x] - mu22) + kC2;
      double d = 1.0 - static_cast<double>(num_m * num_s / denom_s);
      d = std::max(d, 0.0);
      sum1[0] += d;
      sum1[1] += quartic(d);
    }
  }
  avg_out[0] = onePerPixels * sum1[0];
  avg_out[1] = std::sqrt(std::sqrt(onePerPixels * sum1[1]));
}

void EdgeDiffMapPlane(const Image3F& img1, size_t c1, const ImageF& mu1,
                      const Image3F& img2, size_t c2, const ImageF& mu2,
                      double* avg_out) {
  const double onePerPixels =
      1.0 / static_cast<double>(img1.ysize * img1.xsize);
  double sum1[4] = {0.0, 0.0, 0.0, 0.0};
  for (size_t y = 0; y < img1.ysize; ++y) {
    const float* row1 = img1.PlaneRow(c1, y);
    const float* row2 = img2.PlaneRow(c2, y);
    const float* rowm1 = mu1.ConstRow(y);
    const float* rowm2 = mu2.ConstRow(y);
    for (size_t x = 0; x < img1.xsize; ++x) {
      const double d1 =
          (1.0 + std::abs(static_cast<double>(row2[x] - rowm2[x]))) /
              (1.0 + std::abs(static_cast<double>(row1[x] - rowm1[x]))) -
          1.0;
      const double artifact = std::max(d1, 0.0);
      sum1[0] += artifact;
      sum1[1] += quartic(artifact);
      const double detail_lost = std::max(-d1, 0.0);
      sum1[2] += detail_lost;
      sum1[3] += quartic(detail_lost);
    }
  }
  avg_out[0] = onePerPixels * sum1[0];
  avg_out[1] = std::sqrt(std::sqrt(onePerPixels * sum1[1]));
  avg_out[2] = onePerPixels * sum1[2];
  avg_out[3] = std::sqrt(std::sqrt(onePerPixels * sum1[3]));
}

void MultiplyPlanes(const Image3F& a, size_t ca, const Image3F& b, size_t cb,
                    ImageF& out) {
  for (size_t y = 0; y < a.ysize; ++y) {
    MultiplyPlaneRange(a.PlaneRow(ca, y), b.PlaneRow(cb, y), out.Row(y), a.xsize);
  }
}

Image3F CopyImage3F(const Image3F& src) {
  Image3F dst = Image3F::Create(src.xsize, src.ysize);
  auto copy_c = [&](size_t c) {
    for (size_t y = 0; y < src.ysize; ++y) {
      CopyPlaneRange(src.PlaneRow(c, y), dst.PlaneRow(c, y), src.xsize);
    }
  };
  std::future<void> f0 = std::async(std::launch::async, copy_c, 0);
  std::future<void> f1 = std::async(std::launch::async, copy_c, 1);
  copy_c(2);
  f0.get();
  f1.get();
  return dst;
}

}  // namespace

double Msssim::Score() const {
  double ssim = 0.0;
  constexpr double weight[108] = {
      0.0,
      0.0007376606707406586,
      0.0,
      0.0,
      0.0007793481682867309,
      0.0,
      0.0,
      0.0004371155730107379,
      0.0,
      1.1041726426657346,
      0.00066284834129271,
      0.00015231632783718752,
      0.0,
      0.0016406437456599754,
      0.0,
      1.8422455520539298,
      11.441172603757666,
      0.0,
      0.0007989109436015163,
      0.000176816438078653,
      0.0,
      1.8787594979546387,
      10.94906990605142,
      0.0,
      0.0007289346991508072,
      0.9677937080626833,
      0.0,
      0.00014003424285435884,
      0.9981766977854967,
      0.00031949755934435053,
      0.0004550992113792063,
      0.0,
      0.0,
      0.0013648766163243398,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0,
      7.466890328078848,
      0.0,
      17.445833984131262,
      0.0006235601634041466,
      0.0,
      0.0,
      6.683678146179332,
      0.00037724407979611296,
      1.027889937768264,
      225.20515300849274,
      0.0,
      0.0,
      19.213238186143016,
      0.0011401524586618361,
      0.001237755635509985,
      176.39317598450694,
      0.0,
      0.0,
      24.43300999870476,
      0.28520802612117757,
      0.0004485436923833408,
      0.0,
      0.0,
      0.0,
      34.77906344483772,
      44.835625328877896,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0008680556573291698,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0005313191874358747,
      0.0,
      0.00016533814161379112,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0,
      0.0004179171803251336,
      0.0017290828234722833,
      0.0,
      0.0020827005846636437,
      0.0,
      0.0,
      8.826982764996862,
      23.19243343998926,
      0.0,
      95.1080498811086,
      0.9863978034400682,
      0.9834382792465353,
      0.0012286405048278493,
      171.2667255897307,
      0.9807858872435379,
      0.0,
      0.0,
      0.0,
      0.0005130064588990679,
      0.0,
      0.00010854057858411537};

  size_t i = 0;
  for (size_t c = 0; c < 3; ++c) {
    for (size_t scale = 0; scale < scales.size(); ++scale) {
      for (size_t n = 0; n < 2; ++n) {
        ssim += weight[i++] * std::abs(scales[scale].avg_ssim[c * 2 + n]);
        ssim += weight[i++] * std::abs(scales[scale].avg_edgediff[c * 4 + n]);
        ssim +=
            weight[i++] * std::abs(scales[scale].avg_edgediff[c * 4 + n + 2]);
      }
    }
  }

  ssim = ssim * 0.9562382616834844;
  ssim = 2.326765642916932 * ssim - 0.020884521182843837 * ssim * ssim +
         6.248496625763138e-05 * ssim * ssim * ssim;
  if (ssim > 0) {
    ssim = 100.0 - 10.0 * std::pow(ssim, 0.6276336467831387);
  } else {
    ssim = 100.0;
  }
  return ssim;
}

Msssim Compute2FSssimulacra2(const Image3F& linear_orig,
                             const Image3F& linear_dist) {
  if (linear_orig.xsize != linear_dist.xsize ||
      linear_orig.ysize != linear_dist.ysize) {
    throw std::runtime_error("2FSssimulacra2: dimension mismatch");
  }
  if (linear_orig.xsize < 8 || linear_orig.ysize < 8) {
    throw std::runtime_error("2FSssimulacra2: image too small");
  }

  Msssim msssim;

  Image3F orig_linear = CopyImage3F(linear_orig);
  Image3F dist_linear = CopyImage3F(linear_dist);
  Image3F img1 = CopyImage3F(orig_linear);
  Image3F img2 = CopyImage3F(dist_linear);
  {
    auto f = std::async(std::launch::async, [&]() {
      LinearAllRealToXyb(img1);
      MakePositiveXyb(img1);
    });
    LinearAllRealToXyb(img2);
    MakePositiveXyb(img2);
    f.get();
  }
  AttenuatePairDiffs(img1, img2);

  for (int scale = 0; scale < kNumScales; ++scale) {
    if (img1.xsize < 8 || img1.ysize < 8) {
      break;
    }
    if (scale) {
      Image3F next_orig = Downsample(orig_linear, 2, 2);
      Image3F next_dist = Downsample(dist_linear, 2, 2);
      orig_linear = std::move(next_orig);
      dist_linear = std::move(next_dist);
      img1 = CopyImage3F(orig_linear);
      img2 = CopyImage3F(dist_linear);
      auto f = std::async(std::launch::async, [&]() {
        LinearAllRealToXyb(img1);
        MakePositiveXyb(img1);
      });
      LinearAllRealToXyb(img2);
      MakePositiveXyb(img2);
      f.get();
      AttenuatePairDiffs(img1, img2);
    }

    const size_t w = img1.xsize;
    const size_t h = img1.ysize;

    ImageF mul = ImageF::Create(w, h);
    ImageF sigma1 = ImageF::Create(w, h);
    ImageF sigma2 = ImageF::Create(w, h);
    ImageF sigma12 = ImageF::Create(w, h);
    ImageF mu1 = ImageF::Create(w, h);
    ImageF mu2 = ImageF::Create(w, h);
    Blur blur(w, h);

    MsssimScale sscale;
    for (size_t c = 0; c < 3; ++c) {
      MultiplyPlanes(img1, c, img1, c, mul);
      blur.BlurPlane(mul, sigma1);

      MultiplyPlanes(img2, c, img2, c, mul);
      blur.BlurPlane(mul, sigma2);

      MultiplyPlanes(img1, c, img2, c, mul);
      blur.BlurPlane(mul, sigma12);

      blur.BlurPlaneFromImage3(img1, c, mu1);
      blur.BlurPlaneFromImage3(img2, c, mu2);

      SSIMMapPlane(mu1, mu2, sigma1, sigma2, sigma12, &sscale.avg_ssim[c * 2]);
      EdgeDiffMapPlane(img1, c, mu1, img2, c, mu2, &sscale.avg_edgediff[c * 4]);
    }
    msssim.scales.push_back(sscale);
  }
  return msssim;
}
