// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#pragma once
#include "input/image_info.h"
#include <memory>
#include <cstdint>
#include <cstddef>

struct WorkingImage {
    float* data = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    int channels = 0;
    size_t row_samples = 0;
    size_t total_samples = 0;
    double peak = 1.0;
};

enum Channel : int {
    ChannelR = 0,
    ChannelG = 1,
    ChannelB = 2
};

size_t source_samples_per_pixel(const ImageInfo& info);
bool needs_double_staging(const ImageInfo& info);

void expand_source_to_float(const uint8_t* pixels, const ImageInfo& info, float* out);
void expand_source_to_double(const uint8_t* pixels, const ImageInfo& info, double* out);

void prepare_alpha_float(
    float* ref, ImageInfo& ref_info,
    float* dist, ImageInfo& dist_info);

void prepare_alpha_double(
    double* ref, ImageInfo& ref_info,
    double* dist, ImageInfo& dist_info);

void promote_gray_to_rgb_float(std::unique_ptr<float[]>& pixels, ImageInfo& info);
void promote_gray_to_rgb_double(std::unique_ptr<double[]>& pixels, ImageInfo& info);

enum class WorkingTransfer {
    Gamma22,
    Gamma24,
    Linear
};

void convert_to_working(const float* pixels, const ImageInfo& info, WorkingImage& out,
                        WorkingTransfer transfer);
void convert_to_working(const double* pixels, const ImageInfo& info, WorkingImage& out,
                        WorkingTransfer transfer);
