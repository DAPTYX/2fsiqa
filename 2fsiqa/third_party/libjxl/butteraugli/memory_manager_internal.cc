#include "memory_manager.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "support.h"

namespace ba {

namespace {

constexpr size_t kAlign = 64;

}

StatusOr<size_t> BytesPerRow(size_t xsize, size_t sizeof_t) {
  size_t valid_bytes;
  if (!SafeMul(xsize, sizeof_t, valid_bytes)) {
    return JXL_FAILURE("Image dimensions are too large");
  }
  size_t aligned = (valid_bytes + kAlign - 1) & ~(kAlign - 1);
  if (aligned < valid_bytes) {
    return JXL_FAILURE("Image dimensions are too large");
  }
  return aligned;
}

StatusOr<AlignedMemory> AlignedMemory::Create(MemoryManager* memory_manager,
                                              size_t size, size_t pre_padding) {
  JXL_ENSURE(memory_manager);
  size_t allocation_size = size + pre_padding + kAlign;
  if (allocation_size < size) {
    return JXL_FAILURE("Requested allocation is too large");
  }
  void* allocated = memory_manager->alloc(allocation_size);
  if (allocated == nullptr) {
    return JXL_FAILURE("Allocation failed");
  }
  return AlignedMemory(memory_manager, allocated, pre_padding);
}

AlignedMemory::AlignedMemory(MemoryManager* memory_manager, void* allocation,
                             size_t pre_padding)
    : allocation_(allocation), memory_manager_(memory_manager) {
  uintptr_t min_addr =
      reinterpret_cast<uintptr_t>(allocation) + pre_padding;
  uintptr_t aligned = (min_addr + kAlign - 1) & ~(static_cast<uintptr_t>(kAlign - 1));
  address_ = reinterpret_cast<void*>(aligned);
}

AlignedMemory::AlignedMemory(AlignedMemory&& other) noexcept {
  allocation_ = other.allocation_;
  memory_manager_ = other.memory_manager_;
  address_ = other.address_;
  other.memory_manager_ = nullptr;
  other.allocation_ = nullptr;
  other.address_ = nullptr;
}

AlignedMemory& AlignedMemory::operator=(AlignedMemory&& other) noexcept {
  if (this == &other) return *this;
  if (memory_manager_ && allocation_) {
    memory_manager_->free(allocation_);
  }
  allocation_ = other.allocation_;
  memory_manager_ = other.memory_manager_;
  address_ = other.address_;
  other.memory_manager_ = nullptr;
  other.allocation_ = nullptr;
  other.address_ = nullptr;
  return *this;
}

AlignedMemory::~AlignedMemory() {
  if (memory_manager_ == nullptr || allocation_ == nullptr) return;
  memory_manager_->free(allocation_);
}

}
