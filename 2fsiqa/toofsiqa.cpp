// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#include "toofsiqa.h"

#include <algorithm>
#include <cmath>

namespace {

double linear_scale_higher_better(double value, double peak, double zero) {
    const double range = peak - zero;
    if (range == 0.0) {
        return 0.0;
    }
    return (value - zero) / range;
}

double linear_scale_lower_better(double value, double peak, double zero) {
    const double range = zero - peak;
    if (range == 0.0) {
        return 0.0;
    }
    return (zero - value) / range;
}

double apply_gamma(double norm, double gamma) {
    const double capped = std::min(norm, 1.0);
    const double exp_gamma = std::exp(gamma);
    return (std::exp(gamma * capped) - 1.0) / (exp_gamma - 1.0);
}

}  // namespace

std::optional<double> compute_toofsiqa(const ToofsiqaInputs& inputs,
                                       const ToofsiqaConstants& constants) {
    if (inputs.psnr_all_infinite) {
        return 1.0;
    }

    double total_weight = 0.0;
    double quality_score = 0.0;

    auto accumulate = [&](std::optional<double> raw, double peak, double zero,
                          double weight, double gamma, bool lower_is_better) {
        if (!raw.has_value()) {
            return;
        }
        double norm = lower_is_better
            ? linear_scale_lower_better(*raw, peak, zero)
            : linear_scale_higher_better(*raw, peak, zero);
        norm = apply_gamma(norm, gamma);
        total_weight += weight;
        quality_score += norm * weight;
    };

    if (inputs.psnr.has_value() && include_psnr_in_toofsiqa(inputs.reported_bit_depth)) {
        accumulate(inputs.psnr, psnr_toofsiqa_peak(inputs.reported_bit_depth),
                   constants.psnr_zero, constants.weight_psnr,
                   constants.gamma_psnr, false);
    }

    accumulate(inputs.ssim, constants.ssim_peak, constants.ssim_zero,
               constants.weight_ssim, constants.gamma_ssim, false);

    accumulate(inputs.dssim, constants.dssim_peak, constants.dssim_zero,
               constants.weight_dssim, constants.gamma_dssim, true);

    accumulate(inputs.ssimulacra2, constants.ssimulacra2_peak, constants.ssimulacra2_zero,
               constants.weight_ssimulacra2, constants.gamma_ssimulacra2, false);

    accumulate(inputs.butteraugli, constants.butteraugli_peak, constants.butteraugli_zero,
               constants.weight_butteraugli, constants.gamma_butteraugli, true);

    accumulate(inputs.vmaf, constants.vmaf_peak, constants.vmaf_zero,
               constants.weight_vmaf, constants.gamma_vmaf, false);

    if (total_weight == 0.0) {
        return std::nullopt;
    }

    return quality_score / total_weight;
}
