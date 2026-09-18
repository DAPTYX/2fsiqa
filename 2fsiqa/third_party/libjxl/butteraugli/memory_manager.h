#ifndef MEMORY_MANAGER_H_
#define MEMORY_MANAGER_H_

#include <cstddef>
#include <cstdlib>
#include "support.h"

namespace ba {

struct MemoryManager {
  void* (*alloc)(size_t size);
  void (*free)(void* address);
};

inline void* DefaultAlloc(size_t size) { return std::malloc(size); }
inline void DefaultFree(void* address) { std::free(address); }

inline MemoryManager* DefaultMemoryManager() {
  static MemoryManager manager = {&DefaultAlloc, &DefaultFree};
  return &manager;
}

}  // namespace ba


// --- 2FS folded memory_manager_internal ---


#include <cstddef>
#include <utility>


namespace ba {

namespace memory_manager_internal {
static constexpr size_t kAlignment = 64;
static constexpr size_t kNumAlignmentGroups = 1;
static constexpr size_t kAlias = kAlignment;
}  // namespace memory_manager_internal

StatusOr<size_t> BytesPerRow(size_t xsize, size_t sizeof_t);

class AlignedMemory {
 public:
  AlignedMemory()
      : allocation_(nullptr), memory_manager_(nullptr), address_(nullptr) {}

  AlignedMemory(const AlignedMemory& other) = delete;
  AlignedMemory& operator=(const AlignedMemory& other) = delete;

  AlignedMemory(AlignedMemory&& other) noexcept;
  AlignedMemory& operator=(AlignedMemory&& other) noexcept;

  ~AlignedMemory();

  static StatusOr<AlignedMemory> Create(MemoryManager* memory_manager,
                                        size_t size, size_t pre_padding = 0);

  explicit operator bool() const noexcept { return (address_ != nullptr); }

  template <typename T>
  T* address() const {
    return reinterpret_cast<T*>(address_);
  }
  MemoryManager* memory_manager() const { return memory_manager_; }

 private:
  AlignedMemory(MemoryManager* memory_manager, void* allocation,
                size_t pre_padding);

  void* allocation_;
  MemoryManager* memory_manager_;
  void* address_;
};

}  // namespace ba


#endif  // MEMORY_MANAGER_H_
