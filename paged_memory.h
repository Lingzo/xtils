#pragma once

#include <cstddef>
#include <memory>

namespace base {

class PagedMemory {
 public:
  // Initializes an invalid PagedMemory pointing to nullptr.
  PagedMemory();

  ~PagedMemory();

  PagedMemory(PagedMemory&& other) noexcept;
  PagedMemory& operator=(PagedMemory&& other);

  enum AllocationFlags {
    // By default, Allocate() crashes if the underlying mmap fails (e.g., if out
    // of virtual address space). When this flag is provided, an invalid
    // PagedMemory pointing to nullptr is returned in this case instead.
    kMayFail = 1 << 0,

    // By default, Allocate() commits the allocated memory immediately. When
    // this flag is provided, the memory virtual address space may only be
    // reserved and the user should call EnsureCommitted() before writing to
    // memory addresses.
    kDontCommit = 1 << 1,
  };

  // Allocates |size| bytes using mmap(MAP_ANONYMOUS). The returned memory is
  // guaranteed to be page-aligned and guaranteed to be zeroed.
  // For |flags|, see the AllocationFlags enum above.
  static PagedMemory Allocate(size_t size, int flags = 0);

  // Hint to the OS that the memory range is not needed and can be discarded.
  // The memory remains accessible and its contents may be retained, or they
  // may be zeroed. This function may be a NOP on some platforms. Returns true
  // if implemented.
  bool AdviseDontNeed(void* p, size_t size);

  // Ensures that at least the first |committed_size| bytes of the allocated
  // memory region are committed. The implementation may commit memory in larger
  // chunks above |committed_size|. Crashes if the memory couldn't be committed.

  inline void* Get() const noexcept { return p_; }
  inline bool IsValid() const noexcept { return !!p_; }
  inline size_t size() const noexcept { return size_; }

 private:
  PagedMemory(char* p, size_t size);

  PagedMemory(const PagedMemory&) = delete;
  // Defaulted for implementation of move constructor + assignment.
  PagedMemory& operator=(const PagedMemory&) = default;

  char* p_ = nullptr;

  // The size originally passed to Allocate(). The actual virtual memory
  // reservation will be larger due to: (i) guard pages; (ii) rounding up to
  // the system page size.
  size_t size_ = 0;
};

}  // namespace base
