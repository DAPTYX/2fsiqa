// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "image.h"

#include "memory_manager.h"

#include <algorithm>  // swap
#include <cstddef>
#include <cstdint>

#include "support.h"
#include "memory_manager.h"

namespace ba {
namespace detail {

PlaneBase::PlaneBase(const uint32_t xsize, const uint32_t ysize,
                     const size_t sizeof_t)
    : xsize_(xsize),
      ysize_(ysize),
      orig_xsize_(xsize),
      orig_ysize_(ysize),
      bytes_per_row_(0),
      sizeof_t_(sizeof_t) {}

Status PlaneBase::Allocate(MemoryManager* memory_manager,
                           size_t pre_padding) {
  JXL_ENSURE(bytes_.address<void>() == nullptr);
  JXL_ENSURE(bytes_per_row_ == 0);

  JXL_ASSIGN_OR_RETURN(bytes_per_row_, BytesPerRow(xsize_, sizeof_t_));

  // Dimensions can be zero, e.g. for lazily-allocated images. Only allocate
  // if nonzero, because "zero" bytes still have padding/bookkeeping overhead.
  if (xsize_ == 0 || ysize_ == 0) {
    return true;
  }

  size_t total_bytes;
  if (!SafeMul(ysize_, bytes_per_row_, total_bytes)) {
    return JXL_FAILURE("Image dimensions are too large");
  }

  JXL_ASSIGN_OR_RETURN(bytes_,
                       AlignedMemory::Create(memory_manager, total_bytes,
                                             pre_padding * sizeof_t_));

  return true;
}

void PlaneBase::Swap(PlaneBase& other) {
  std::swap(xsize_, other.xsize_);
  std::swap(ysize_, other.ysize_);
  std::swap(orig_xsize_, other.orig_xsize_);
  std::swap(orig_ysize_, other.orig_ysize_);
  std::swap(bytes_per_row_, other.bytes_per_row_);
  std::swap(bytes_, other.bytes_);
}

}  // namespace detail
}  // namespace ba
