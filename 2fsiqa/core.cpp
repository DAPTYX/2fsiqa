// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#include "core.h"
#include "batch.h"
#include "input/decode.h"
#include "cmm.h"
#include "psnr.h"
#include "ssim.h"
#include "dssim.h"
#include "ssimulacra2.h"
#include "butteraugli_2fs.h"
#include "image3f.h"
#include "yuv_allreal.h"
#include "vmaf_runner.h"
#include "toofsiqa.h"

#include <iostream>
#include <iomanip>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <memory>
#include <future>
#include <utility>
#include <algorithm>
#include <limits>
#include <optional>
#include <filesystem>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

static constexpr uint32_t kCoreMinSide = 8;
static constexpr uint32_t kVmafMinSide = 64;

static bool is_supported(const char* path) {
    return is_supported_path(path);
}

static bool stdin_is_redirected() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) == 0;
#else
    return isatty(fileno(stdin)) == 0;
#endif
}

static std::vector<uint8_t> load_file_into_memory(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) throw std::runtime_error(std::string("Cannot open: ") + path);
    fseek(file, 0, SEEK_END);
    const long file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        throw std::runtime_error("ftell failed");
    }
    rewind(file);
    std::vector<uint8_t> file_bytes(static_cast<size_t>(file_size));
    if (fread(file_bytes.data(), 1, file_bytes.size(), file) != file_bytes.size()) {
        fclose(file);
        throw std::runtime_error(std::string("Failed to read: ") + path);
    }
    fclose(file);
    return file_bytes;
}

std::vector<uint8_t> read_stdin_bytes() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    std::vector<uint8_t> file_bytes;
    uint8_t chunk[64 * 1024];
    for (;;) {
        const size_t n = fread(chunk, 1, sizeof(chunk), stdin);
        if (n > 0) {
            file_bytes.insert(file_bytes.end(), chunk, chunk + n);
        }
        if (n < sizeof(chunk)) {
            if (feof(stdin)) break;
            if (ferror(stdin)) {
                throw std::runtime_error("Failed to read distorted image from stdin");
            }
            break;
        }
    }
    if (file_bytes.empty()) {
        throw std::runtime_error("No data on stdin for distorted image");
    }
    return file_bytes;
}

struct OwnedDecodedImage {
    ImageInfo info;
    std::unique_ptr<uint8_t[]> pixels;
};

struct OwnedWorkingImage {
    std::unique_ptr<float[]> storage;
    WorkingImage view;
};

static OwnedDecodedImage load_and_decode(const char* path) {
    std::vector<uint8_t> file_bytes = load_file_into_memory(path);
    OwnedDecodedImage decoded;
    decoded.info = decode_image(file_bytes.data(), file_bytes.size(), decoded.pixels, path);
    return decoded;
}

static OwnedDecodedImage load_and_decode_bytes(std::vector<uint8_t> file_bytes,
                                               const char* label) {
    OwnedDecodedImage decoded;
    decoded.info = decode_image(file_bytes.data(), file_bytes.size(), decoded.pixels, label);
    return decoded;
}

static OwnedWorkingImage allocate_working(const ImageInfo& info, int force_channels = 0) {
    const int channels = force_channels > 0 ? force_channels : info.channels;
    const size_t pixel_count = static_cast<size_t>(info.width) * static_cast<size_t>(info.height);
    const size_t total_samples = pixel_count * static_cast<size_t>(channels);
    OwnedWorkingImage owned;
    owned.storage = std::make_unique_for_overwrite<float[]>(total_samples);
    owned.view.data = owned.storage.get();
    owned.view.width = info.width;
    owned.view.height = info.height;
    owned.view.channels = channels;
    owned.view.row_samples = static_cast<size_t>(info.width) * static_cast<size_t>(channels);
    owned.view.total_samples = total_samples;
    owned.view.peak = 1.0;
    return owned;
}

static std::unique_ptr<float[]> allocate_source_float(const ImageInfo& info) {
    const size_t pixel_count = static_cast<size_t>(info.width) * static_cast<size_t>(info.height);
    const size_t samples = pixel_count * source_samples_per_pixel(info);
    return std::make_unique_for_overwrite<float[]>(samples);
}

static std::unique_ptr<double[]> allocate_source_double(const ImageInfo& info) {
    const size_t pixel_count = static_cast<size_t>(info.width) * static_cast<size_t>(info.height);
    const size_t samples = pixel_count * source_samples_per_pixel(info);
    return std::make_unique_for_overwrite<double[]>(samples);
}

static PairMetrics compute_psnr_ssim(const WorkingImage& working_a, const WorkingImage& working_b,
                                     bool want_psnr, bool want_ssim,
                                     int reported_bit_depth) {
    PairMetrics metrics;
    metrics.reported_bit_depth = reported_bit_depth;
    if (!want_psnr && !want_ssim) {
        return metrics;
    }

    double ssim = 0.0;
    if (working_a.channels == 1) {
        if (want_psnr) {
            const double mse = compute_mse(
                working_a.data, working_b.data, working_a.total_samples);
            if (mse == 0.0) {
                metrics.psnr_all_infinite = true;
                metrics.psnr_infinite = true;
                metrics.psnr = std::numeric_limits<double>::infinity();
            } else {
                metrics.psnr = 10.0 * std::log10(
                    (working_a.peak * working_a.peak) / mse);
            }
            metrics.has_psnr = true;
        }
        if (want_ssim) {
            ssim = compute_ssim(
                working_a.data, working_b.data,
                working_a.width, working_a.height,
                working_a.channels, working_a.row_samples);
        }
    } else {
        if (want_psnr) {
            const auto channel_mses = compute_mse_channels(
                working_a.data, working_b.data,
                working_a.total_samples, working_a.channels);

            const double peak2 = working_a.peak * working_a.peak;
            const double fallback = psnr_inf_fallback_peak(reported_bit_depth);

            double channel_psnr[3];
            bool channel_inf[3];
            int finite_count = 0;
            double max_finite = 0.0;

            for (int c = 0; c < 3; ++c) {
                if (channel_mses[c] == 0.0) {
                    channel_inf[c] = true;
                    channel_psnr[c] = 0.0;
                } else {
                    channel_inf[c] = false;
                    channel_psnr[c] = 10.0 * std::log10(peak2 / channel_mses[c]);
                    if (finite_count == 0 || channel_psnr[c] > max_finite) {
                        max_finite = channel_psnr[c];
                    }
                    ++finite_count;
                }
            }

            if (finite_count == 0) {
                metrics.psnr_all_infinite = true;
                metrics.psnr_infinite = true;
                metrics.psnr = std::numeric_limits<double>::infinity();
            } else {
                const double substitute =
                    (fallback > max_finite) ? fallback : max_finite;
                for (int c = 0; c < 3; ++c) {
                    if (channel_inf[c]) {
                        channel_psnr[c] = substitute;
                    }
                }
                metrics.psnr = 0.35 * channel_psnr[ChannelR]
                             + 0.40 * channel_psnr[ChannelG]
                             + 0.25 * channel_psnr[ChannelB];
            }
            metrics.has_psnr = true;
        }
        if (want_ssim) {
            const auto channel_ssims = compute_ssim_channels(
                working_a.data, working_b.data,
                working_a.width, working_a.height,
                working_a.channels, working_a.row_samples);
            ssim = 0.35 * channel_ssims[ChannelR]
                 + 0.40 * channel_ssims[ChannelG]
                 + 0.25 * channel_ssims[ChannelB];
        }
    }

    if (want_ssim) {
        metrics.ssim = ssim;
        metrics.has_ssim = true;
    }
    return metrics;
}

ToofsiqaInputs metrics_to_toofsiqa_inputs(const PairMetrics& metrics) {
    ToofsiqaInputs inputs;
    inputs.reported_bit_depth = metrics.reported_bit_depth;
    inputs.psnr_all_infinite = metrics.psnr_all_infinite;
    if (metrics.has_psnr && !metrics.psnr_all_infinite) {
        inputs.psnr = metrics.psnr;
    }
    if (metrics.has_ssim) {
        inputs.ssim = metrics.ssim;
    }
    if (metrics.has_dssim) {
        inputs.dssim = metrics.dssim;
    }
    if (metrics.has_ssimulacra2) {
        inputs.ssimulacra2 = metrics.ssimulacra2;
    }
    if (metrics.has_butteraugli) {
        inputs.butteraugli = metrics.butteraugli;
    }
    if (metrics.has_vmaf) {
        inputs.vmaf = metrics.vmaf;
    }
    return inputs;
}

static void print_individual_metrics(const PairMetrics& metrics) {
    if (metrics.has_psnr) {
        if (metrics.psnr_infinite) {
            std::cout << "2FS-psnr: inf\n";
        } else {
            std::cout << "2FS-psnr: " << std::fixed << std::setprecision(11) << metrics.psnr << "\n";
        }
    }
    if (metrics.has_ssim) {
        std::cout << "2FS-ssim: " << std::fixed << std::setprecision(11) << metrics.ssim << "\n";
    }
    if (metrics.has_dssim) {
        std::cout << "2FS-dssim: " << std::fixed << std::setprecision(11) << metrics.dssim << "\n";
    }
    if (metrics.has_ssimulacra2) {
        std::cout << "2FS-ssimulacra2: " << std::fixed << std::setprecision(11)
                  << metrics.ssimulacra2 << "\n";
    }
    if (metrics.has_butteraugli) {
        std::cout << "2FS-butteraugli: " << std::fixed << std::setprecision(11)
                  << metrics.butteraugli << "\n";
    }
    if (metrics.has_vmaf) {
        std::cout << "2FS-vmaf: " << std::fixed << std::setprecision(11)
                  << metrics.vmaf << "\n";
    }
}

void print_metrics(const PairMetrics& metrics, const MetricSelection& selection) {
    const bool want_composite = selection.composite_only || selection.showall;
    const bool want_individuals = selection.any_explicit || selection.showall;

    if (metrics.psnr_all_infinite) {
        if (want_individuals && metrics.has_psnr) {
            std::cout << "2FS-psnr: inf\n";
        }
        if (want_composite) {
            std::cout << "2fsiqa: " << std::fixed << std::setprecision(11) << 1.0 << "\n";
        }
        return;
    }

    if (want_individuals) {
        print_individual_metrics(metrics);
    }

    if (want_composite) {
        const auto score = compute_toofsiqa(metrics_to_toofsiqa_inputs(metrics));
        if (score.has_value()) {
            std::cout << "2fsiqa: " << std::fixed << std::setprecision(11) << *score << "\n";
        } else {
            std::cout << "2fsiqa: unavailable\n";
        }
    }
}

static Image3F working_to_image3f(const WorkingImage& working) {
    if (working.channels != 3) {
        throw std::runtime_error("working_to_image3f requires RGB");
    }
    Image3F img = Image3F::Create(working.width, working.height);
    for (uint32_t y = 0; y < working.height; ++y) {
        const float* src = working.data + static_cast<size_t>(y) * working.row_samples;
        float* r = img.PlaneRow(0, y);
        float* g = img.PlaneRow(1, y);
        float* b = img.PlaneRow(2, y);
        for (uint32_t x = 0; x < working.width; ++x) {
            r[x] = src[x * 3 + 0];
            g[x] = src[x * 3 + 1];
            b[x] = src[x * 3 + 2];
        }
    }
    return img;
}

static void compute_linear_dssim(const WorkingImage& a, const WorkingImage& b,
                                 PairMetrics& metrics) {
    metrics.dssim = compute_dssim(a, b);
    metrics.has_dssim = true;
}

static void compute_linear_jxl_scores(OwnedWorkingImage& a,
                                      OwnedWorkingImage& b,
                                      PairMetrics& metrics,
                                      bool want_lacra, bool want_butter) {
    Image3F img_a = working_to_image3f(a.view);
    Image3F img_b = working_to_image3f(b.view);
    a.storage.reset();
    b.storage.reset();
    a.view.data = nullptr;
    b.view.data = nullptr;

    if (want_lacra) {
        metrics.ssimulacra2 = Compute2FSssimulacra2(img_a, img_b).Score();
        metrics.has_ssimulacra2 = true;
    }
    if (want_butter) {
        const ButteraugliScores ba = Compute2FSbutteraugli(img_a, img_b);
        metrics.butteraugli = ba.pnorm3;
        metrics.butteraugli_max_norm = ba.max_norm;
        metrics.has_butteraugli = true;
    }
}

static void compute_gamma24_vmaf(OwnedWorkingImage& a,
                                 OwnedWorkingImage& b,
                                 PairMetrics& metrics) {
    Yuv444i12 yuv_a = working_rgb_to_yuv444_allreal_12bit_full(a.view);
    Yuv444i12 yuv_b = working_rgb_to_yuv444_allreal_12bit_full(b.view);
    a.storage.reset();
    b.storage.reset();
    a.view.data = nullptr;
    b.view.data = nullptr;

    const VmafScore score = compute_2fs_vmaf(yuv_a, yuv_b);
    if (score.ok) {
        metrics.vmaf = score.score;
        metrics.has_vmaf = true;
    }
}

template <typename Sample>
static void build_working_images(
    Sample* stage_a, Sample* stage_b,
    ImageInfo& info_a, ImageInfo& info_b,
    bool want_g22, bool want_linear, bool need_rgb_working,
    bool want_vmaf, bool want_dssim,
    OwnedWorkingImage& working_g22_a, OwnedWorkingImage& working_g22_b,
    OwnedWorkingImage& working_linear_a, OwnedWorkingImage& working_linear_b,
    OwnedWorkingImage& working_g24_a, OwnedWorkingImage& working_g24_b,
    bool& run_linear_rgb, bool& run_linear_gray, bool& run_vmaf) {
    if (want_g22) {
        auto future_convert_a = std::async(std::launch::async, [&]() {
            working_g22_a = allocate_working(info_a);
            convert_to_working(stage_a, info_a, working_g22_a.view,
                               WorkingTransfer::Gamma22);
        });
        auto future_convert_b = std::async(std::launch::async, [&]() {
            working_g22_b = allocate_working(info_b);
            convert_to_working(stage_b, info_b, working_g22_b.view,
                               WorkingTransfer::Gamma22);
        });
        future_convert_a.get();
        future_convert_b.get();
    }

    if (info_a.channels == 3 && info_b.channels == 3) {
        run_linear_rgb = want_linear;
        run_vmaf = want_vmaf;
        std::vector<std::future<void>> converts;
        if (want_linear) {
            converts.push_back(std::async(std::launch::async, [&]() {
                working_linear_a = allocate_working(info_a);
                convert_to_working(stage_a, info_a, working_linear_a.view,
                                   WorkingTransfer::Linear);
            }));
            converts.push_back(std::async(std::launch::async, [&]() {
                working_linear_b = allocate_working(info_b);
                convert_to_working(stage_b, info_b, working_linear_b.view,
                                   WorkingTransfer::Linear);
            }));
        }
        if (want_vmaf) {
            converts.push_back(std::async(std::launch::async, [&]() {
                working_g24_a = allocate_working(info_a);
                convert_to_working(stage_a, info_a, working_g24_a.view,
                                   WorkingTransfer::Gamma24);
            }));
            converts.push_back(std::async(std::launch::async, [&]() {
                working_g24_b = allocate_working(info_b);
                convert_to_working(stage_b, info_b, working_g24_b.view,
                                   WorkingTransfer::Gamma24);
            }));
        }
        for (auto& f : converts) f.get();
    } else if (info_a.channels == 1 && info_b.channels == 1) {
        if (need_rgb_working) {
            run_linear_rgb = want_linear;
            run_vmaf = want_vmaf;
            std::vector<std::future<void>> converts;
            if (want_linear) {
                converts.push_back(std::async(std::launch::async, [&]() {
                    working_linear_a = allocate_working(info_a, 3);
                    convert_to_working(stage_a, info_a, working_linear_a.view,
                                       WorkingTransfer::Linear);
                }));
                converts.push_back(std::async(std::launch::async, [&]() {
                    working_linear_b = allocate_working(info_b, 3);
                    convert_to_working(stage_b, info_b, working_linear_b.view,
                                       WorkingTransfer::Linear);
                }));
            }
            if (want_vmaf) {
                converts.push_back(std::async(std::launch::async, [&]() {
                    working_g24_a = allocate_working(info_a, 3);
                    convert_to_working(stage_a, info_a, working_g24_a.view,
                                       WorkingTransfer::Gamma24);
                }));
                converts.push_back(std::async(std::launch::async, [&]() {
                    working_g24_b = allocate_working(info_b, 3);
                    convert_to_working(stage_b, info_b, working_g24_b.view,
                                       WorkingTransfer::Gamma24);
                }));
            }
            for (auto& f : converts) f.get();
        } else if (want_dssim) {
            run_linear_gray = true;
            auto future_convert_a = std::async(std::launch::async, [&]() {
                working_linear_a = allocate_working(info_a);
                convert_to_working(stage_a, info_a, working_linear_a.view,
                                   WorkingTransfer::Linear);
            });
            auto future_convert_b = std::async(std::launch::async, [&]() {
                working_linear_b = allocate_working(info_b);
                convert_to_working(stage_b, info_b, working_linear_b.view,
                                   WorkingTransfer::Linear);
            });
            future_convert_a.get();
            future_convert_b.get();
        }
    }
}

static PairMetrics compare_decoded(OwnedDecodedImage image_a,
                                   OwnedDecodedImage image_b,
                                   MetricSelection selection);

PairMetrics compare_pair(const char* path_a, const char* path_b,
                         MetricSelection selection) {
    auto future_a = std::async(std::launch::async, load_and_decode, path_a);
    OwnedDecodedImage image_b;
    if (path_b) {
        auto future_b = std::async(std::launch::async, load_and_decode, path_b);
        image_b = future_b.get();
    } else {
        image_b = load_and_decode_bytes(read_stdin_bytes(), "stdin");
    }
    OwnedDecodedImage image_a = future_a.get();
    return compare_decoded(std::move(image_a), std::move(image_b), selection);
}

PairMetrics compare_pair_bytes_b(const char* path_a,
                                 const uint8_t* distorted_bytes,
                                 size_t distorted_byte_count,
                                 MetricSelection selection) {
    auto future_a = std::async(std::launch::async, load_and_decode, path_a);
    std::vector<uint8_t> copy(distorted_bytes,
                              distorted_bytes + distorted_byte_count);
    OwnedDecodedImage image_b =
        load_and_decode_bytes(std::move(copy), "stdin");
    OwnedDecodedImage image_a = future_a.get();
    return compare_decoded(std::move(image_a), std::move(image_b), selection);
}

static PairMetrics compare_decoded(OwnedDecodedImage image_a,
                                   OwnedDecodedImage image_b,
                                   MetricSelection selection) {
    if (image_a.info.width != image_b.info.width ||
        image_a.info.height != image_b.info.height) {
        throw std::runtime_error("Mismatch in dimensions");
    }

    if (image_a.info.width < kCoreMinSide || image_a.info.height < kCoreMinSide) {
        throw std::runtime_error("too small image");
    }

    if (selection.vmaf &&
        (image_a.info.width < kVmafMinSide || image_a.info.height < kVmafMinSide)) {
        std::cerr << "Warning: too small for vmaf\n";
        selection.vmaf = false;
        if (!selection.any()) {
            throw std::runtime_error("too small for vmaf");
        }
    }

    const bool channel_mismatch = image_a.info.channels != image_b.info.channels;
    if (channel_mismatch) {
        std::cerr << "Warning: Channel count mismatch ("
                  << image_a.info.channels << " vs " << image_b.info.channels
                  << "); promoting gray to RGB.\n";
    }

    const bool use_double =
        needs_double_staging(image_a.info) || needs_double_staging(image_b.info);

    OwnedWorkingImage working_g22_a;
    OwnedWorkingImage working_g22_b;
    OwnedWorkingImage working_linear_a;
    OwnedWorkingImage working_linear_b;
    OwnedWorkingImage working_g24_a;
    OwnedWorkingImage working_g24_b;
    const bool want_g22 = selection.psnr || selection.ssim;
    const bool want_linear = selection.dssim || selection.lacra || selection.butter;
    const bool want_jxl = selection.lacra || selection.butter;
    const bool need_rgb_working = selection.lacra || selection.butter || selection.vmaf;
    bool run_linear_rgb = false;
    bool run_linear_gray = false;
    bool run_vmaf = false;

    if (use_double) {
        std::unique_ptr<double[]> stage_a = allocate_source_double(image_a.info);
        std::unique_ptr<double[]> stage_b = allocate_source_double(image_b.info);

        auto future_expand_a = std::async(std::launch::async, [&]() {
            expand_source_to_double(image_a.pixels.get(), image_a.info, stage_a.get());
        });
        auto future_expand_b = std::async(std::launch::async, [&]() {
            expand_source_to_double(image_b.pixels.get(), image_b.info, stage_b.get());
        });
        future_expand_a.get();
        future_expand_b.get();

        image_a.pixels.reset();
        image_b.pixels.reset();

        if (channel_mismatch) {
            if (image_a.info.channels == 1) {
                promote_gray_to_rgb_double(stage_a, image_a.info);
            }
            if (image_b.info.channels == 1) {
                promote_gray_to_rgb_double(stage_b, image_b.info);
            }
        }

        prepare_alpha_double(stage_a.get(), image_a.info, stage_b.get(), image_b.info);

        build_working_images(
            stage_a.get(), stage_b.get(),
            image_a.info, image_b.info,
            want_g22, want_linear, need_rgb_working,
            selection.vmaf, selection.dssim,
            working_g22_a, working_g22_b,
            working_linear_a, working_linear_b,
            working_g24_a, working_g24_b,
            run_linear_rgb, run_linear_gray, run_vmaf);
    } else {
        std::unique_ptr<float[]> stage_a = allocate_source_float(image_a.info);
        std::unique_ptr<float[]> stage_b = allocate_source_float(image_b.info);

        auto future_expand_a = std::async(std::launch::async, [&]() {
            expand_source_to_float(image_a.pixels.get(), image_a.info, stage_a.get());
        });
        auto future_expand_b = std::async(std::launch::async, [&]() {
            expand_source_to_float(image_b.pixels.get(), image_b.info, stage_b.get());
        });
        future_expand_a.get();
        future_expand_b.get();

        image_a.pixels.reset();
        image_b.pixels.reset();

        if (channel_mismatch) {
            if (image_a.info.channels == 1) {
                promote_gray_to_rgb_float(stage_a, image_a.info);
            }
            if (image_b.info.channels == 1) {
                promote_gray_to_rgb_float(stage_b, image_b.info);
            }
        }

        prepare_alpha_float(stage_a.get(), image_a.info, stage_b.get(), image_b.info);

        build_working_images(
            stage_a.get(), stage_b.get(),
            image_a.info, image_b.info,
            want_g22, want_linear, need_rgb_working,
            selection.vmaf, selection.dssim,
            working_g22_a, working_g22_b,
            working_linear_a, working_linear_b,
            working_g24_a, working_g24_b,
            run_linear_rgb, run_linear_gray, run_vmaf);
    }

    PairMetrics metrics;
    metrics.reported_bit_depth = image_a.info.reported_bit_depth;
    if (want_g22) {
        metrics = compute_psnr_ssim(working_g22_a.view, working_g22_b.view,
                                    selection.psnr, selection.ssim,
                                    image_a.info.reported_bit_depth);
        working_g22_a.storage.reset();
        working_g22_b.storage.reset();
        working_g22_a.view.data = nullptr;
        working_g22_b.view.data = nullptr;
        if (metrics.psnr_all_infinite) {
            return metrics;
        }
    }

    if (run_linear_rgb) {
        if (selection.dssim) {
            compute_linear_dssim(working_linear_a.view, working_linear_b.view, metrics);
        }
        if (want_jxl) {
            compute_linear_jxl_scores(working_linear_a, working_linear_b, metrics,
                                      selection.lacra, selection.butter);
        } else {
            working_linear_a.storage.reset();
            working_linear_b.storage.reset();
            working_linear_a.view.data = nullptr;
            working_linear_b.view.data = nullptr;
        }
    } else if (run_linear_gray) {
        compute_linear_dssim(working_linear_a.view, working_linear_b.view, metrics);
        working_linear_a.storage.reset();
        working_linear_b.storage.reset();
        working_linear_a.view.data = nullptr;
        working_linear_b.view.data = nullptr;
    }

    if (run_vmaf) {
        compute_gamma24_vmaf(working_g24_a, working_g24_b, metrics);
    }

    return metrics;
}

static void print_usage(const char* program) {
    std::cerr
        << "  2fsiqa - Composite Image Quality Assessment Metric\n"
        << "\n"
        << "  This tool can help you estimate the perceptual difference between pairs of images\n"
		<< "\n"
		<< "  2fsiqa score range is ~-0.6 - 1.0, where:\n"
		<< "\n"
		<< "  1.0  — pair is identical\n"
		<< "  Score > 0.0 - similar, with some differences (closer to 1.0 means more similar)\n"
		<< "  Score < 0.0 - questionable that the images match\n"
		<< "\n"
		<< "\n"
        << "  Usage:\n"
        << "  toofsiqa <reference> <distorted> [options]\n"
        << "  or\n"
        << "  toofsiqa [options] <reference> <distorted>\n"
        << "\n"
        << "\n"
		<< "  Distorted image can be passed through a pipe, e.g.:\n"
        << "    magick <distorted> png:- | toofsiqa <reference>\n"
		<< "\n"
        << "  Inputs may be files or directories\n"
        << "  A directory as an input enables batch mode\n"
        << "\n"
		<< "\n"
        << "  Default run will return 2fsiqa score for a single pair,\n"
        << "  or average/minimum/maximum summary in batch mode\n"
        << "\n"
		<< "\n"
        << "  Options:\n"
        << "    --showall\n"
        << "        Print every metric together with 2fsiqa (default: composite 2fsiqa only)\n"
        << "    --psnr --ssim --dssim --lacra --butter --vmaf\n"
        << "        Run only the named metric\n"
        << "\n"
        << "  Batch options:\n"
        << "    --showpairs\n"
        << "        Print each pair score as it completes\n"
        << "    --savestats [path]\n"
        << "        Write summary and per-pair stats to a text file\n"
        << "        Default path: inside the distorted directory, or next to the distorted file\n"
		<< "\n"
        << "\n"
        << "  Only pairs of still images with identical dimensions (width x height) are supported.\n"
        << "  Supported image formats: avif, bmp, jpeg, png, pnm, tiff, webp";
}

int main(int argc, char** argv) {
    MetricSelection selection;
    std::vector<const char*> positionals;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--showall") {
            selection.showall = true;
            selection.composite_only = false;
        } else if (arg == "--showpairs") {
            selection.showpairs = true;
        } else if (arg == "--savestats") {
            selection.savestats = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                selection.savestats_path = argv[++i];
            }
        } else if (arg == "--psnr" || arg == "--ssim" || arg == "--dssim" ||
                   arg == "--lacra" || arg == "--butter" || arg == "--vmaf") {
            if (!selection.any_explicit) {
                selection.enable_only_named();
                selection.any_explicit = true;
            }
            if (arg == "--psnr") selection.psnr = true;
            else if (arg == "--ssim") selection.ssim = true;
            else if (arg == "--dssim") selection.dssim = true;
            else if (arg == "--lacra") selection.lacra = true;
            else if (arg == "--butter") selection.butter = true;
            else if (arg == "--vmaf") selection.vmaf = true;
        } else {
            positionals.push_back(argv[i]);
        }
    }

    const bool stdin_distorted = (positionals.size() == 1 && stdin_is_redirected());
    if (positionals.size() != 2 && !stdin_distorted) {
        print_usage(argv[0]);
        return 1;
    }

    if (!selection.any()) {
        std::cerr << "Error: no metrics selected\n";
        return 1;
    }

    try {
        namespace fs = std::filesystem;
        const fs::path path_a(positionals[0]);
        const bool a_dir = fs::is_directory(path_a);

        if (stdin_distorted) {
            if (a_dir) {
                std::vector<uint8_t> bytes = read_stdin_bytes();
                return run_batch_refs_vs_bytes(path_a, bytes, selection, positionals[0]);
            }
            if (!is_supported(positionals[0])) {
                std::cerr << "Error: format not supported: " << positionals[0] << "\n";
                return 1;
            }
            PairMetrics metrics = compare_pair(positionals[0], nullptr, selection);
            print_metrics(metrics, selection);
            return 0;
        }

        const fs::path path_b(positionals[1]);
        const bool b_dir = fs::is_directory(path_b);

        if (a_dir && b_dir) {
            return run_batch_dirs(path_a, path_b, selection, positionals[0], positionals[1]);
        }
        if (a_dir || b_dir) {
            return run_batch_size_only(path_a, path_b, selection,
                                       positionals[0], positionals[1]);
        }

        if (!is_supported(positionals[0])) {
            std::cerr << "Error: format not supported: " << positionals[0] << "\n";
            return 1;
        }
        if (!is_supported(positionals[1])) {
            std::cerr << "Error: format not supported: " << positionals[1] << "\n";
            return 1;
        }
        PairMetrics metrics = compare_pair(positionals[0], positionals[1], selection);
        print_metrics(metrics, selection);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
