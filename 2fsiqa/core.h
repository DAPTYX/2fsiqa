// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#pragma once

#include "toofsiqa.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct MetricSelection {
    bool psnr = true;
    bool ssim = true;
    bool dssim = true;
    bool lacra = true;
    bool butter = true;
    bool vmaf = true;
    bool any_explicit = false;
    bool showall = false;
    bool composite_only = true;
    bool showpairs = false;
    bool savestats = false;
    std::string savestats_path;

    bool any() const {
        return psnr || ssim || dssim || lacra || butter || vmaf;
    }

    void enable_only_named() {
        psnr = false;
        ssim = false;
        dssim = false;
        lacra = false;
        butter = false;
        vmaf = false;
        composite_only = false;
    }
};

struct PairMetrics {
    double psnr = 0.0;
    double ssim = 0.0;
    double dssim = 0.0;
    double ssimulacra2 = 0.0;
    double butteraugli = 0.0;
    double butteraugli_max_norm = 0.0;
    double vmaf = 0.0;
    bool psnr_infinite = false;
    bool psnr_all_infinite = false;
    int reported_bit_depth = 0;
    bool has_psnr = false;
    bool has_ssim = false;
    bool has_dssim = false;
    bool has_ssimulacra2 = false;
    bool has_butteraugli = false;
    bool has_vmaf = false;
};

std::vector<uint8_t> read_stdin_bytes();

PairMetrics compare_pair(const char* path_a, const char* path_b,
                         MetricSelection selection);

PairMetrics compare_pair_bytes_b(const char* path_a,
                                 const uint8_t* distorted_bytes,
                                 size_t distorted_byte_count,
                                 MetricSelection selection);

void print_metrics(const PairMetrics& metrics, const MetricSelection& selection);

ToofsiqaInputs metrics_to_toofsiqa_inputs(const PairMetrics& metrics);
