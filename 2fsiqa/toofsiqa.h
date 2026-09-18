// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#pragma once

#include <cmath>
#include <optional>

struct ToofsiqaInputs {
    std::optional<double> psnr;
    std::optional<double> ssim;
    std::optional<double> dssim;
    std::optional<double> ssimulacra2;
    std::optional<double> butteraugli;
    std::optional<double> vmaf;
    bool psnr_all_infinite = false;
    int reported_bit_depth = 0;
};

struct ToofsiqaConstants {
    double gamma_psnr = 9.36171837885394353;
    double gamma_ssim = 1.18192682654902703;
    double gamma_dssim = 0.62340643859208122;
    double gamma_ssimulacra2 = 3.13359136645195857;
    double gamma_butteraugli = 5.07657791664660607;
    double gamma_vmaf = 0.1834947685744179;

    double ssim_peak = 0.99998977677354295;
    double dssim_peak = 0.0004725741092786;
    double ssimulacra2_peak = 96.12100924227171106;
    double butteraugli_peak = 0.00097344081834298;
    double vmaf_peak = 97.05573772108768082;

    double psnr_zero = 27.94916313800175089;
    double ssim_zero = 0.75590529490661496;
    double dssim_zero = 0.03348857182409740;
    double ssimulacra2_zero = 0.0;
    double butteraugli_zero = 6.51104090026518456;
    double vmaf_zero = 55.08180736540971623;

    double weight_psnr = 0.03765;
    double weight_ssim = 0.1685;
    double weight_dssim = 0.2315;
    double weight_ssimulacra2 = 0.1985;
    double weight_butteraugli = 0.1315;
    double weight_vmaf = 0.23235;
};

inline constexpr int kPsnrMinBitDepth = 4;

inline double psnr_toofsiqa_peak(int reported_bit_depth) {
    if (reported_bit_depth <= 4) return 33.99026746406606492;
    if (reported_bit_depth == 5) return 40.32093696889131706;
    if (reported_bit_depth == 6) return 46.30522133742691437;
    if (reported_bit_depth == 7) return 51.87900995976647778;
    if (reported_bit_depth == 8) return 59.29273437200676966;
    if (reported_bit_depth == 9) return 64.74288369915451824;
    if (reported_bit_depth == 10) return 70.76565379276246404;
    if (reported_bit_depth == 11) return 76.78918145445553023;
    if (reported_bit_depth == 12) return 82.80607136437981808;
    if (reported_bit_depth == 13) return 88.76607550208761666;
    return 98.49715991730304267;
}

inline bool include_psnr_in_toofsiqa(int reported_bit_depth) {
    return reported_bit_depth >= kPsnrMinBitDepth;
}

std::optional<double> compute_toofsiqa(const ToofsiqaInputs& inputs,
                                       const ToofsiqaConstants& constants = ToofsiqaConstants{});
