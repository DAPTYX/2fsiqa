// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#pragma once

#include "core.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

int run_batch_dirs(const std::filesystem::path& dir_a,
                   const std::filesystem::path& dir_b,
                   MetricSelection selection,
                   const std::string& ref_arg,
                   const std::string& dist_arg);

int run_batch_size_only(const std::filesystem::path& path_a,
                        const std::filesystem::path& path_b,
                        MetricSelection selection,
                        const std::string& ref_arg,
                        const std::string& dist_arg);

int run_batch_refs_vs_bytes(const std::filesystem::path& path_a,
                            const std::vector<uint8_t>& distorted_bytes,
                            MetricSelection selection,
                            const std::string& ref_arg);
