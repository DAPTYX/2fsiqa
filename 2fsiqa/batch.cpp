// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#include "batch.h"

#include "input/decode.h"
#include "toofsiqa.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct ImageProbe {
    fs::path path;
    std::string stem;
    std::string filename;
    uint32_t width = 0;
    uint32_t height = 0;
    bool ok = false;
};

struct PairJob {
    std::string ref_name;
    std::string dist_name;
    fs::path path_a;
    fs::path path_b;
    uint32_t width = 0;
    uint32_t height = 0;
    double work = 0.0;
};

struct NamedMetrics {
    std::string ref_name;
    std::string dist_name;
    PairMetrics metrics;
    std::optional<double> toofsiqa_score;
};

std::string pair_vs_label(const std::string& ref_name, const std::string& dist_name) {
    return ref_name + " vs " + dist_name;
}

bool natural_char_less(char a, char b) {
    const unsigned char ua = static_cast<unsigned char>(a);
    const unsigned char ub = static_cast<unsigned char>(b);
    const char la = static_cast<char>(std::tolower(ua));
    const char lb = static_cast<char>(std::tolower(ub));
    if (la != lb) return la < lb;
    return ua < ub;
}

bool natural_less(const std::string& a, const std::string& b) {
    size_t i = 0;
    size_t j = 0;
    while (i < a.size() && j < b.size()) {
        if (std::isdigit(static_cast<unsigned char>(a[i])) &&
            std::isdigit(static_cast<unsigned char>(b[j]))) {
            while (i < a.size() && a[i] == '0') ++i;
            while (j < b.size() && b[j] == '0') ++j;
            size_t i_digits = i;
            size_t j_digits = j;
            while (i_digits < a.size() &&
                   std::isdigit(static_cast<unsigned char>(a[i_digits]))) {
                ++i_digits;
            }
            while (j_digits < b.size() &&
                   std::isdigit(static_cast<unsigned char>(b[j_digits]))) {
                ++j_digits;
            }
            const size_t len_a = i_digits - i;
            const size_t len_b = j_digits - j;
            if (len_a != len_b) return len_a < len_b;
            for (size_t k = 0; k < len_a; ++k) {
                if (a[i + k] != b[j + k]) return a[i + k] < b[j + k];
            }
            i = i_digits;
            j = j_digits;
        } else {
            if (a[i] != b[j] && natural_char_less(a[i], b[j]) != natural_char_less(b[j], a[i])) {
                return natural_char_less(a[i], b[j]);
            }
            if (a[i] != b[j]) return natural_char_less(a[i], b[j]);
            ++i;
            ++j;
        }
    }
    return (a.size() - i) < (b.size() - j);
}

bool pair_job_natural_less(const PairJob& a, const PairJob& b) {
    if (a.ref_name != b.ref_name) return natural_less(a.ref_name, b.ref_name);
    return natural_less(a.dist_name, b.dist_name);
}

constexpr int kMinPartialMatch = 3;
constexpr double kWorkSizeBoost = 0.64;

double image_work_units(uint32_t width, uint32_t height) {
    return std::sqrt(static_cast<double>(width) * static_cast<double>(height));
}

void apply_relative_work_weights(std::vector<PairJob>& pairs) {
    if (pairs.empty()) return;
    double base_min = pairs[0].work;
    for (const auto& job : pairs) {
        base_min = std::min(base_min, job.work);
    }
    if (base_min <= 0.0) return;
    for (auto& job : pairs) {
        const double ratio = job.work / base_min;
        job.work *= 1.0 + kWorkSizeBoost * std::log2(ratio);
    }
}

std::vector<ImageProbe> collect_probes(const fs::path& directory) {
    std::vector<fs::path> candidates;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        candidates.push_back(entry.path());
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const fs::path& x, const fs::path& y) {
                  return natural_less(x.filename().string(), y.filename().string());
              });

    std::vector<std::future<ImageProbe>> futures;
    futures.reserve(candidates.size());
    for (const auto& path : candidates) {
        futures.push_back(std::async(std::launch::async, [path]() {
            ImageProbe probe;
            probe.path = path;
            probe.stem = path.stem().string();
            probe.filename = path.filename().string();
            if (probe.stem.empty()) return probe;
            if (!is_supported_path(path.string().c_str())) return probe;
            uint32_t w = 0;
            uint32_t h = 0;
            if (probe_image_path(path.string().c_str(), w, h)) {
                probe.width = w;
                probe.height = h;
                probe.ok = true;
            }
            return probe;
        }));
    }

    std::vector<ImageProbe> probes;
    probes.reserve(futures.size());
    for (auto& f : futures) {
        ImageProbe p = f.get();
        if (p.ok) probes.push_back(std::move(p));
    }
    return probes;
}

bool same_dimensions(const ImageProbe& a, const ImageProbe& b) {
    return a.width == b.width && a.height == b.height;
}

size_t common_prefix_len(const std::string& a, const std::string& b) {
    const size_t n = std::min(a.size(), b.size());
    size_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    return i;
}

size_t common_suffix_len(const std::string& a, const std::string& b) {
    const size_t n = std::min(a.size(), b.size());
    size_t i = 0;
    while (i < n && a[a.size() - 1 - i] == b[b.size() - 1 - i]) ++i;
    return i;
}

bool partial_name_match(const std::string& stem_a, const std::string& stem_b) {
    if (common_prefix_len(stem_a, stem_b) >= static_cast<size_t>(kMinPartialMatch)) {
        return true;
    }
    if (common_suffix_len(stem_a, stem_b) >= static_cast<size_t>(kMinPartialMatch)) {
        return true;
    }
    return false;
}

std::vector<PairJob> form_pairs(std::vector<ImageProbe> probes_a,
                                std::vector<ImageProbe> probes_b) {
    std::vector<PairJob> pairs;
    std::vector<bool> used_a(probes_a.size(), false);
    std::vector<bool> used_b(probes_b.size(), false);

    std::map<std::string, std::vector<size_t>> stem_a;
    std::map<std::string, std::vector<size_t>> stem_b;
    for (size_t i = 0; i < probes_a.size(); ++i) {
        stem_a[probes_a[i].stem].push_back(i);
    }
    for (size_t i = 0; i < probes_b.size(); ++i) {
        stem_b[probes_b[i].stem].push_back(i);
    }

    for (const auto& [stem, indices_a] : stem_a) {
        auto it = stem_b.find(stem);
        if (it == stem_b.end()) continue;
        const auto& indices_b = it->second;
        for (size_t ia : indices_a) {
            for (size_t ib : indices_b) {
                if (!same_dimensions(probes_a[ia], probes_b[ib])) continue;
                PairJob job;
                job.ref_name = probes_a[ia].filename;
                job.dist_name = probes_b[ib].filename;
                job.path_a = probes_a[ia].path;
                job.path_b = probes_b[ib].path;
                job.width = probes_a[ia].width;
                job.height = probes_a[ia].height;
                job.work = image_work_units(job.width, job.height) * 2.0;
                pairs.push_back(std::move(job));
                used_a[ia] = true;
                used_b[ib] = true;
            }
        }
    }

    std::vector<size_t> free_a;
    std::vector<size_t> free_b;
    for (size_t i = 0; i < probes_a.size(); ++i) {
        if (!used_a[i]) free_a.push_back(i);
    }
    for (size_t i = 0; i < probes_b.size(); ++i) {
        if (!used_b[i]) free_b.push_back(i);
    }

    for (size_t ia : free_a) {
        if (used_a[ia]) continue;
        for (size_t ib : free_b) {
            if (used_b[ib]) continue;
            if (!same_dimensions(probes_a[ia], probes_b[ib])) continue;
            if (!partial_name_match(probes_a[ia].stem, probes_b[ib].stem)) continue;
            PairJob job;
            job.ref_name = probes_a[ia].filename;
            job.dist_name = probes_b[ib].filename;
            job.path_a = probes_a[ia].path;
            job.path_b = probes_b[ib].path;
            job.width = probes_a[ia].width;
            job.height = probes_a[ia].height;
            job.work = image_work_units(job.width, job.height) * 2.0;
            pairs.push_back(std::move(job));
            used_a[ia] = true;
            used_b[ib] = true;
            break;
        }
    }

    return pairs;
}

std::vector<PairJob> form_pairs_size_only(std::vector<ImageProbe> probes_a,
                                          std::vector<ImageProbe> probes_b) {
    std::vector<PairJob> pairs;
    for (const auto& a : probes_a) {
        for (const auto& b : probes_b) {
            if (!same_dimensions(a, b)) continue;
            PairJob job;
            job.ref_name = a.filename;
            job.dist_name = b.filename.empty() ? "stdin" : b.filename;
            job.path_a = a.path;
            job.path_b = b.path;
            job.width = a.width;
            job.height = a.height;
            job.work = image_work_units(job.width, job.height) * 2.0;
            pairs.push_back(std::move(job));
        }
    }
    return pairs;
}

ImageProbe probe_one_file(const fs::path& path) {
    ImageProbe probe;
    probe.path = path;
    probe.stem = path.stem().string();
    probe.filename = path.filename().string();
    if (!is_supported_path(path.string().c_str())) return probe;
    uint32_t w = 0;
    uint32_t h = 0;
    if (probe_image_path(path.string().c_str(), w, h)) {
        probe.width = w;
        probe.height = h;
        probe.ok = true;
    }
    return probe;
}

std::vector<ImageProbe> collect_side(const fs::path& path) {
    if (fs::is_directory(path)) {
        return collect_probes(path);
    }
    ImageProbe one = probe_one_file(path);
    if (!one.ok) {
        return {};
    }
    return {std::move(one)};
}

std::string format_duration(double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    const auto total = static_cast<long long>(seconds + 0.5);
    const long long days = total / 86400;
    const long long hours = (total % 86400) / 3600;
    const long long mins = (total % 3600) / 60;
    const long long secs = total % 60;

    char buf[32];
    if (days > 0) {
        std::snprintf(buf, sizeof(buf), "%lldd %02lld:%02lld:%02lld",
                      days, hours, mins, secs);
    } else {
        std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld", hours, mins, secs);
    }
    return buf;
}

struct ProgressState {
    std::mutex mu;
    std::chrono::steady_clock::time_point start{};
    std::chrono::steady_clock::time_point last_rate_recalc{};
    std::chrono::steady_clock::time_point last_tick{};
    double eta_remaining_s = -1.0;
    double done_work = 0.0;
    double total_work = 0.0;
    size_t done_pairs = 0;
    size_t total_pairs = 0;
    std::atomic<bool> stop{false};

    void print_line(bool final_line) {
        if (total_work <= 0.0 || total_pairs == 0) return;

        const auto now = std::chrono::steady_clock::now();
        const double elapsed_s =
            std::chrono::duration_cast<std::chrono::duration<double>>(now - start).count();
        const double fraction = std::min(1.0, done_work / total_work);
        const double percent = fraction * 100.0;
        const bool finished = final_line;

        if (!finished && fraction >= 0.05 && elapsed_s > 0.05) {
            const double since_rate =
                std::chrono::duration_cast<std::chrono::duration<double>>(
                    now - last_rate_recalc).count();
            if (eta_remaining_s < 0.0 || since_rate >= 5.0) {
                eta_remaining_s = elapsed_s * (1.0 - fraction) / fraction;
                last_rate_recalc = now;
                last_tick = now;
            } else {
                const double since_tick =
                    std::chrono::duration_cast<std::chrono::duration<double>>(
                        now - last_tick).count();
                eta_remaining_s = std::max(0.0, eta_remaining_s - since_tick);
                last_tick = now;
            }
        }

        std::cerr << "\r[" << done_pairs << "/" << total_pairs << "] "
                  << std::fixed << std::setprecision(1) << percent << "%"
                  << "  elapsed " << format_duration(elapsed_s);

        if (finished) {
            std::cerr << "  done" << std::string(16, ' ') << "\n";
        } else if (eta_remaining_s >= 0.0) {
            std::cerr << "  ETA " << format_duration(eta_remaining_s)
                      << std::string(8, ' ');
        } else {
            std::cerr << "  ETA --:--:--" << std::string(8, ' ');
        }
        std::cerr << std::flush;
    }
};

std::string format_score(double v) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(11) << v;
    return os.str();
}

std::string format_pair_bracket(const NamedMetrics& example, size_t tie_count,
                                bool avg_example) {
    const std::string vs = pair_vs_label(example.ref_name, example.dist_name);
    if (avg_example) {
        return "[close to avg e.g. " + vs + "]";
    }
    if (tie_count > 1) {
        return "[" + std::to_string(tie_count) + " pairs, e.g. " + vs + "]";
    }
    return "[" + vs + "]";
}

template <typename GetScore, typename IsEqual>
void extremum_stats(const std::vector<NamedMetrics>& results, GetScore get,
                    IsEqual equal, bool prefer_higher_as_max,
                    const NamedMetrics*& out_min, const NamedMetrics*& out_max,
                    size_t& min_ties, size_t& max_ties) {
    out_min = nullptr;
    out_max = nullptr;
    min_ties = 0;
    max_ties = 0;
    for (const auto& item : results) {
        if (!get(item).has_value()) continue;
        const double v = *get(item);
        if (!out_min) {
            out_min = &item;
            out_max = &item;
            min_ties = 1;
            max_ties = 1;
            continue;
        }
        const double vmin = *get(*out_min);
        const double vmax = *get(*out_max);
        if (prefer_higher_as_max) {
            if (v < vmin) {
                out_min = &item;
                min_ties = 1;
            } else if (equal(v, vmin)) {
                ++min_ties;
            }
            if (v > vmax) {
                out_max = &item;
                max_ties = 1;
            } else if (equal(v, vmax)) {
                ++max_ties;
            }
        } else {
            if (v > vmin) {
                out_min = &item;
                min_ties = 1;
            } else if (equal(v, vmin)) {
                ++min_ties;
            }
            if (v < vmax) {
                out_max = &item;
                max_ties = 1;
            } else if (equal(v, vmax)) {
                ++max_ties;
            }
        }
    }
}

size_t count_equal_score(const std::vector<NamedMetrics>& results,
                         const NamedMetrics* anchor,
                         const std::function<std::optional<double>(const NamedMetrics&)>& get) {
    if (!anchor || !get(*anchor).has_value()) return 0;
    const double target = *get(*anchor);
    size_t n = 0;
    for (const auto& item : results) {
        auto v = get(item);
        if (v.has_value() && *v == target) ++n;
    }
    return n;
}

const NamedMetrics* closest_to_avg(
    const std::vector<NamedMetrics>& results, double avg,
    const std::function<std::optional<double>(const NamedMetrics&)>& get) {
    const NamedMetrics* best = nullptr;
    double best_dist = 0.0;
    for (const auto& item : results) {
        auto v = get(item);
        if (!v.has_value()) continue;
        const double d = std::fabs(*v - avg);
        if (!best || d < best_dist) {
            best = &item;
            best_dist = d;
        }
    }
    return best;
}

void append_metric_block(
    std::ostream& out, const char* label, double avg, bool avg_inf,
    const NamedMetrics* min_item, size_t min_ties,
    const NamedMetrics* max_item, size_t max_ties,
    const NamedMetrics* avg_example,
    const std::function<std::optional<double>(const NamedMetrics&)>& get,
    size_t exclude_inf_note) {
    if (avg_inf) {
        out << label << " avg: inf\n";
    } else {
        out << label << " avg: " << format_score(avg);
        if (avg_example) {
            out << " " << format_pair_bracket(*avg_example, 1, true);
        }
        if (exclude_inf_note > 0) {
            out << " (excluding " << exclude_inf_note << " infinite)";
        }
        out << "\n";
    }
    if (min_item) {
        auto v = get(*min_item);
        out << label << " min: ";
        if (v.has_value()) out << format_score(*v);
        else out << "inf";
        out << " " << format_pair_bracket(*min_item, min_ties, false) << "\n";
    }
    if (max_item) {
        auto v = get(*max_item);
        out << label << " max: ";
        if (v.has_value()) out << format_score(*v);
        else out << "inf";
        out << " " << format_pair_bracket(*max_item, max_ties, false) << "\n";
    }
}

void write_batch_summary(std::ostream& out, const std::vector<NamedMetrics>& results,
                         const MetricSelection& selection) {
    const bool want_individuals = selection.any_explicit || selection.showall;
    const bool want_composite = selection.composite_only || selection.showall;

    out << "Pairs: " << results.size() << "\n";

    auto eq = [](double a, double b) { return a == b; };

    if (want_individuals) {
        {
            double sum = 0.0;
            size_t finite = 0;
            size_t infinite = 0;
            const NamedMetrics* pmin = nullptr;
            const NamedMetrics* pmax = nullptr;
            for (const auto& item : results) {
                if (!item.metrics.has_psnr) continue;
                if (item.metrics.psnr_infinite) {
                    ++infinite;
                    if (!pmax || !pmax->metrics.psnr_infinite) pmax = &item;
                } else {
                    sum += item.metrics.psnr;
                    ++finite;
                    if (!pmin || item.metrics.psnr < pmin->metrics.psnr) pmin = &item;
                    if (!pmax || pmax->metrics.psnr_infinite ||
                        item.metrics.psnr > pmax->metrics.psnr) {
                        pmax = &item;
                    }
                }
            }
            if (finite > 0 || infinite > 0) {
                auto get = [](const NamedMetrics& m) -> std::optional<double> {
                    if (!m.metrics.has_psnr) return std::nullopt;
                    if (m.metrics.psnr_infinite) return std::nullopt;
                    return m.metrics.psnr;
                };
                size_t min_ties = pmin ? count_equal_score(results, pmin, get) : 0;
                size_t max_ties = 0;
                if (pmax) {
                    if (pmax->metrics.psnr_infinite) {
                        for (const auto& item : results) {
                            if (item.metrics.has_psnr && item.metrics.psnr_infinite) ++max_ties;
                        }
                    } else {
                        max_ties = count_equal_score(results, pmax, get);
                    }
                }
                const double avg = finite > 0 ? sum / static_cast<double>(finite) : 0.0;
                const NamedMetrics* avg_ex =
                    finite > 0 ? closest_to_avg(results, avg, get) : nullptr;
                if (infinite > 0 && finite == 0) {
                    out << "2FS-psnr avg: inf\n";
                } else if (finite > 0) {
                    out << "2FS-psnr avg: " << format_score(avg);
                    if (avg_ex) out << " " << format_pair_bracket(*avg_ex, 1, true);
                    if (infinite > 0) out << " (excluding " << infinite << " infinite)";
                    out << "\n";
                }
                if (pmin) {
                    out << "2FS-psnr min: " << format_score(pmin->metrics.psnr) << " "
                        << format_pair_bracket(*pmin, min_ties, false) << "\n";
                }
                if (pmax) {
                    out << "2FS-psnr max: ";
                    if (pmax->metrics.psnr_infinite) out << "inf";
                    else out << format_score(pmax->metrics.psnr);
                    out << " " << format_pair_bracket(*pmax, max_ties, false) << "\n";
                }
            }
        }

        auto emit_simple = [&](const char* label, bool higher_is_better,
                               auto has_fn, auto score_fn) {
            std::function<std::optional<double>(const NamedMetrics&)> get =
                [&](const NamedMetrics& m) -> std::optional<double> {
                    if (!has_fn(m)) return std::nullopt;
                    return score_fn(m);
                };
            double sum = 0.0;
            size_t count = 0;
            for (const auto& item : results) {
                auto v = get(item);
                if (!v) continue;
                sum += *v;
                ++count;
            }
            if (count == 0) return;
            const NamedMetrics* mn = nullptr;
            const NamedMetrics* mx = nullptr;
            size_t min_ties = 0;
            size_t max_ties = 0;
            extremum_stats(results, get, eq, higher_is_better, mn, mx, min_ties, max_ties);
            min_ties = count_equal_score(results, mn, get);
            max_ties = count_equal_score(results, mx, get);
            const double avg = sum / static_cast<double>(count);
            const NamedMetrics* avg_ex = closest_to_avg(results, avg, get);
            append_metric_block(out, label, avg, false, mn, min_ties, mx, max_ties,
                                avg_ex, get, 0);
        };

        emit_simple("2FS-ssim", true,
                    [](const NamedMetrics& m) { return m.metrics.has_ssim; },
                    [](const NamedMetrics& m) { return m.metrics.ssim; });
        emit_simple("2FS-dssim", false,
                    [](const NamedMetrics& m) { return m.metrics.has_dssim; },
                    [](const NamedMetrics& m) { return m.metrics.dssim; });
        emit_simple("2FS-ssimulacra2", true,
                    [](const NamedMetrics& m) { return m.metrics.has_ssimulacra2; },
                    [](const NamedMetrics& m) { return m.metrics.ssimulacra2; });
        emit_simple("2FS-butteraugli", false,
                    [](const NamedMetrics& m) { return m.metrics.has_butteraugli; },
                    [](const NamedMetrics& m) { return m.metrics.butteraugli; });
        emit_simple("2FS-vmaf", true,
                    [](const NamedMetrics& m) { return m.metrics.has_vmaf; },
                    [](const NamedMetrics& m) { return m.metrics.vmaf; });
    }

    if (want_composite) {
        std::function<std::optional<double>(const NamedMetrics&)> get =
            [](const NamedMetrics& m) -> std::optional<double> {
                return m.toofsiqa_score;
            };
        double sum = 0.0;
        size_t count = 0;
        for (const auto& item : results) {
            if (!item.toofsiqa_score) continue;
            sum += *item.toofsiqa_score;
            ++count;
        }
        if (count > 0) {
            const NamedMetrics* mn = nullptr;
            const NamedMetrics* mx = nullptr;
            size_t min_ties = 0;
            size_t max_ties = 0;
            extremum_stats(results, get, eq, true, mn, mx, min_ties, max_ties);
            min_ties = count_equal_score(results, mn, get);
            max_ties = count_equal_score(results, mx, get);
            const double avg = sum / static_cast<double>(count);
            const NamedMetrics* avg_ex = closest_to_avg(results, avg, get);
            append_metric_block(out, "2fsiqa", avg, false, mn, min_ties, mx, max_ties,
                                avg_ex, get, 0);
        }
    }
}

void write_pair_metrics(std::ostream& out, const NamedMetrics& item,
                        const MetricSelection& selection) {
    const bool want_composite = selection.composite_only || selection.showall;
    const bool want_individuals = selection.any_explicit || selection.showall;
    const auto& m = item.metrics;

    if (m.psnr_all_infinite) {
        if (want_individuals && m.has_psnr) {
            out << "2FS-psnr: inf\n";
        }
        if (want_composite) {
            out << "2fsiqa: " << format_score(1.0) << "\n";
        }
        return;
    }
    if (want_individuals) {
        if (m.has_psnr) {
            if (m.psnr_infinite) out << "2FS-psnr: inf\n";
            else out << "2FS-psnr: " << format_score(m.psnr) << "\n";
        }
        if (m.has_ssim) out << "2FS-ssim: " << format_score(m.ssim) << "\n";
        if (m.has_dssim) out << "2FS-dssim: " << format_score(m.dssim) << "\n";
        if (m.has_ssimulacra2) {
            out << "2FS-ssimulacra2: " << format_score(m.ssimulacra2) << "\n";
        }
        if (m.has_butteraugli) {
            out << "2FS-butteraugli: " << format_score(m.butteraugli) << "\n";
        }
        if (m.has_vmaf) out << "2FS-vmaf: " << format_score(m.vmaf) << "\n";
    }
    if (want_composite) {
        if (item.toofsiqa_score.has_value()) {
            out << "2fsiqa: " << format_score(*item.toofsiqa_score) << "\n";
        } else {
            out << "2fsiqa: unavailable\n";
        }
    }
}

std::string stats_filename_timestamped() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "2fsiqa_stats_%02d.%02d.%02d_%02d%02d.txt",
                  (local.tm_year + 1900) % 100, local.tm_mon + 1, local.tm_mday,
                  local.tm_hour, local.tm_min);
    return buf;
}

fs::path default_stats_path(const fs::path& ref_path, const fs::path& dist_path,
                            bool dist_is_stdin) {
    const std::string name = stats_filename_timestamped();
    if (dist_is_stdin) {
        if (fs::is_directory(ref_path)) {
            return ref_path / name;
        }
        fs::path parent = ref_path.parent_path();
        if (parent.empty()) parent = ".";
        return parent / name;
    }
    if (fs::is_directory(dist_path)) {
        return dist_path / name;
    }
    fs::path parent = dist_path.parent_path();
    if (parent.empty()) parent = ".";
    return parent / name;
}

}  // namespace

int run_pair_jobs(std::vector<PairJob> pairs,
                  MetricSelection selection,
                  const std::vector<uint8_t>* shared_distorted_bytes,
                  const std::string& ref_arg,
                  const std::string& dist_arg) {
    if (pairs.empty()) {
        std::cerr << "Error: no matching supported pairs found\n";
        return 1;
    }

    std::sort(pairs.begin(), pairs.end(), pair_job_natural_less);
    apply_relative_work_weights(pairs);

    double total_work = 0.0;
    for (const auto& job : pairs) {
        total_work += job.work;
    }

    std::vector<NamedMetrics> results;
    results.reserve(pairs.size());

    ProgressState progress;
    progress.start = std::chrono::steady_clock::now();
    progress.last_rate_recalc = progress.start;
    progress.last_tick = progress.start;
    progress.total_work = total_work;
    progress.total_pairs = pairs.size();
    progress.done_pairs = 0;
    progress.done_work = 0.0;

    std::thread ticker([&progress]() {
        while (!progress.stop.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (progress.stop.load(std::memory_order_relaxed)) break;
            std::lock_guard<std::mutex> lock(progress.mu);
            progress.print_line(false);
        }
    });

    for (const auto& job : pairs) {
        try {
            PairMetrics metrics;
            if (shared_distorted_bytes) {
                metrics = compare_pair_bytes_b(
                    job.path_a.string().c_str(),
                    shared_distorted_bytes->data(),
                    shared_distorted_bytes->size(),
                    selection);
            } else {
                metrics = compare_pair(job.path_a.string().c_str(),
                                       job.path_b.string().c_str(),
                                       selection);
            }
            NamedMetrics entry;
            entry.ref_name = job.ref_name;
            entry.dist_name = job.dist_name;
            entry.metrics = metrics;
            entry.toofsiqa_score = compute_toofsiqa(metrics_to_toofsiqa_inputs(metrics));
            if (selection.showpairs) {
                {
                    std::lock_guard<std::mutex> lock(progress.mu);
                    std::cerr << "\n";
                }
                std::cout << "---\n";
                std::cout << pair_vs_label(entry.ref_name, entry.dist_name) << "\n";
                write_pair_metrics(std::cout, entry, selection);
            }
            results.push_back(std::move(entry));
        } catch (const std::exception& error) {
            std::cerr << "\nWarning: skipping \""
                      << pair_vs_label(job.ref_name, job.dist_name) << "\": "
                      << error.what() << "\n";
        }
        {
            std::lock_guard<std::mutex> lock(progress.mu);
            progress.done_work += job.work;
            ++progress.done_pairs;
            progress.print_line(false);
        }
    }

    progress.stop.store(true, std::memory_order_relaxed);
    ticker.join();
    {
        std::lock_guard<std::mutex> lock(progress.mu);
        progress.print_line(true);
    }

    if (results.empty()) {
        std::cerr << "Error: no pairs completed successfully\n";
        return 1;
    }

    if (selection.showpairs) {
        std::cout << "---\n";
    }
    write_batch_summary(std::cout, results, selection);

    if (selection.savestats) {
        const bool dist_is_stdin = (dist_arg == "stdin" || dist_arg.empty());
        fs::path out_path = selection.savestats_path.empty()
            ? default_stats_path(fs::path(ref_arg),
                                 fs::path(dist_arg.empty() ? "." : dist_arg),
                                 dist_is_stdin)
            : fs::path(selection.savestats_path);
        std::ofstream file(out_path, std::ios::out | std::ios::binary);
        if (!file) {
            std::cerr << "Error: cannot write stats file: " << out_path << "\n";
            return 1;
        }
        file << "reference: " << ref_arg << "\n";
        file << "distorted: " << (dist_is_stdin ? "stdin" : dist_arg) << "\n";
        file << "\n";
        write_batch_summary(file, results, selection);
        file << "\n";
        for (size_t i = 0; i < results.size(); ++i) {
            file << "---\n";
            file << pair_vs_label(results[i].ref_name, results[i].dist_name) << "\n";
            write_pair_metrics(file, results[i], selection);
        }
        file << "---\n";
        std::cerr << "Stats written: " << out_path << "\n";
    }
    return 0;
}

int run_batch_dirs(const fs::path& dir_a, const fs::path& dir_b,
                   MetricSelection selection,
                   const std::string& ref_arg,
                   const std::string& dist_arg) {
    auto future_a = std::async(std::launch::async, collect_probes, dir_a);
    auto future_b = std::async(std::launch::async, collect_probes, dir_b);
    std::vector<ImageProbe> probes_a = future_a.get();
    std::vector<ImageProbe> probes_b = future_b.get();
    return run_pair_jobs(form_pairs(std::move(probes_a), std::move(probes_b)),
                         selection, nullptr, ref_arg, dist_arg);
}

int run_batch_size_only(const fs::path& path_a, const fs::path& path_b,
                        MetricSelection selection,
                        const std::string& ref_arg,
                        const std::string& dist_arg) {
    auto future_a = std::async(std::launch::async, collect_side, path_a);
    auto future_b = std::async(std::launch::async, collect_side, path_b);
    std::vector<ImageProbe> probes_a = future_a.get();
    std::vector<ImageProbe> probes_b = future_b.get();
    if (probes_a.empty()) {
        std::cerr << "Error: no supported reference image(s) in " << path_a << "\n";
        return 1;
    }
    if (probes_b.empty()) {
        std::cerr << "Error: no supported distorted image(s) in " << path_b << "\n";
        return 1;
    }
    return run_pair_jobs(form_pairs_size_only(std::move(probes_a), std::move(probes_b)),
                         selection, nullptr, ref_arg, dist_arg);
}

int run_batch_refs_vs_bytes(const fs::path& path_a,
                            const std::vector<uint8_t>& distorted_bytes,
                            MetricSelection selection,
                            const std::string& ref_arg) {
    std::vector<ImageProbe> probes_a = collect_side(path_a);
    if (probes_a.empty()) {
        std::cerr << "Error: no supported reference image(s) in " << path_a << "\n";
        return 1;
    }

    uint32_t bw = 0;
    uint32_t bh = 0;
    if (!probe_image(distorted_bytes.data(), distorted_bytes.size(), bw, bh, "stdin")) {
        std::cerr << "Error: unsupported or unreadable distorted image on stdin\n";
        return 1;
    }

    ImageProbe probe_b;
    probe_b.filename = "stdin";
    probe_b.stem = "stdin";
    probe_b.width = bw;
    probe_b.height = bh;
    probe_b.ok = true;

    std::vector<PairJob> pairs =
        form_pairs_size_only(std::move(probes_a), {std::move(probe_b)});
    return run_pair_jobs(std::move(pairs), selection, &distorted_bytes, ref_arg, "stdin");
}
