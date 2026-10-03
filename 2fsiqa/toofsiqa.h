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
};

struct ToofsiqaConstants {
    double gamma_psnr = 11.5516255123160736;
    double gamma_ssim = 2.50804960469373883;
    double gamma_dssim = 0.30425822850646822;
    double gamma_ssimulacra2 = 4.50336182359004678;
    double gamma_butteraugli = 4.16911964975726335;
    double gamma_vmaf = 0.11474717339130573;

    double psnr_peak = 105.67429235572350876;
    double ssim_peak = 0.99411856119380337;
    double dssim_peak = 0.00054186963634625;
    double ssimulacra2_peak = 99.99634029098663746;
    double butteraugli_peak = 0.00000196303671376;
    double vmaf_peak = 97.4269502677698398;

    double psnr_zero = 27.94776035194346875;
    double ssim_zero = 0.43759992597380704;
    double dssim_zero = 0.01312436630032327;
    double ssimulacra2_zero = 0.010536871675854538;
    double butteraugli_zero = 5.37465129822075749;
    double vmaf_zero = 63.27964704498809567;

    double weight_psnr = 0.0353;
    double weight_ssim = 0.2221;
    double weight_dssim = 0.1816;
    double weight_ssimulacra2 = 0.2102;
    double weight_butteraugli = 0.1376;
    double weight_vmaf = 0.2132;
};

std::optional<double> compute_toofsiqa(const ToofsiqaInputs& inputs,
                                       const ToofsiqaConstants& constants = ToofsiqaConstants{});
