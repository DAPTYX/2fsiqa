#pragma once

#include "image.h"
#include "memory_manager.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

struct Image3F {
  ba::Image3F impl;
  size_t xsize = 0;
  size_t ysize = 0;
  size_t row_stride = 0;

  static Image3F Create(size_t width, size_t height) {
    if (width == 0 || height == 0) {
      throw std::runtime_error("Image3F::Create: zero dimension");
    }
    Image3F img;
    ba::MemoryManager* mm = ba::DefaultMemoryManager();
    auto created = ba::Image3F::Create(mm, width, height);
    if (!created.ok()) {
      throw std::runtime_error("Image3F::Create: alloc failed");
    }
    img.impl = std::move(created).value_();
    img.xsize = width;
    img.ysize = height;
    img.row_stride = static_cast<size_t>(img.impl.PixelsPerRow());
    return img;
  }

  float* PlaneRow(size_t c, size_t y) { return impl.PlaneRow(c, y); }
  const float* PlaneRow(size_t c, size_t y) const {
    return impl.ConstPlaneRow(c, y);
  }
  float* Row(size_t y) { return impl.PlaneRow(0, y); }
  const float* ConstRow(size_t y) const { return impl.ConstPlaneRow(0, y); }

  void ShrinkTo(size_t new_x, size_t new_y) {
    if (new_x > xsize || new_y > ysize) {
      throw std::runtime_error("Image3F::ShrinkTo: larger than current");
    }
    if (!impl.ShrinkTo(new_x, new_y)) {
      throw std::runtime_error("Image3F::ShrinkTo: impl failed");
    }
    xsize = new_x;
    ysize = new_y;
  }
};

struct ImageF {
  ba::ImageF impl;
  float* data = nullptr;
  size_t xsize = 0;
  size_t ysize = 0;
  size_t row_stride = 0;

  static ImageF Create(size_t width, size_t height) {
    if (width == 0 || height == 0) {
      throw std::runtime_error("ImageF::Create: zero dimension");
    }
    ImageF img;
    ba::MemoryManager* mm = ba::DefaultMemoryManager();
    auto created = ba::ImageF::Create(mm, width, height);
    if (!created.ok()) {
      throw std::runtime_error("ImageF::Create: alloc failed");
    }
    img.impl = std::move(created).value_();
    img.xsize = width;
    img.ysize = height;
    img.row_stride = static_cast<size_t>(img.impl.PixelsPerRow());
    img.data = img.impl.Row(0);
    return img;
  }

  float* Row(size_t y) { return impl.Row(y); }
  const float* ConstRow(size_t y) const { return impl.ConstRow(y); }

  void ShrinkTo(size_t new_x, size_t new_y) {
    if (new_x > xsize || new_y > ysize) {
      throw std::runtime_error("ImageF::ShrinkTo: larger than current");
    }
    if (!impl.ShrinkTo(new_x, new_y)) {
      throw std::runtime_error("ImageF::ShrinkTo: impl failed");
    }
    xsize = new_x;
    ysize = new_y;
  }
};
