/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "xtils/system/paged_memory.h"

#include <sys/mman.h>

#include <cstddef>

#include "xtils/logging/logger.h"

namespace xtils {

namespace {
size_t GetSysPageSize() { return 4096; }
size_t RoundUpToSysPageSize(size_t req_size) {
  const size_t page_size = GetSysPageSize();
  return (req_size + page_size - 1) & ~(page_size - 1);
}

size_t GuardSize() { return GetSysPageSize(); }

}  // namespace

// static
PagedMemory PagedMemory::Allocate(size_t req_size, int flags) {
  size_t rounded_up_size = RoundUpToSysPageSize(req_size);
  CHECK(rounded_up_size >= req_size);
  size_t outer_size = rounded_up_size + GuardSize() * 2;
  void* ptr = mmap(nullptr, outer_size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ptr == MAP_FAILED && (flags & kMayFail)) return PagedMemory();
  CHECK(ptr && ptr != MAP_FAILED);
  char* usable_region = reinterpret_cast<char*>(ptr) + GuardSize();
  int res = mprotect(ptr, GuardSize(), PROT_NONE);
  res |= mprotect(usable_region + rounded_up_size, GuardSize(), PROT_NONE);
  CHECK(res == 0);

  auto memory = PagedMemory(usable_region, req_size);
  return memory;
}

PagedMemory::PagedMemory() {}

// clang-format off
PagedMemory::PagedMemory(char* p, size_t size) : p_(p), size_(size) {
}

PagedMemory::PagedMemory(PagedMemory&& other) noexcept {
  *this = other;
  other.p_ = nullptr;
}
// clang-format on

PagedMemory& PagedMemory::operator=(PagedMemory&& other) {
  this->~PagedMemory();
  new (this) PagedMemory(std::move(other));
  return *this;
}

PagedMemory::~PagedMemory() {
  if (!p_) return;
  CHECK(size_);
  char* start = p_ - GuardSize();
  const size_t outer_size = RoundUpToSysPageSize(size_) + GuardSize() * 2;
  int res = munmap(start, outer_size);
  CHECK(res == 0);
}

bool PagedMemory::AdviseDontNeed(void* p, size_t size) {
  DCHECK(p_);
  DCHECK(p >= p_);
  DCHECK(static_cast<char*>(p) + size <= p_ + size_);
  int res = madvise(p, size, MADV_DONTNEED);
  DCHECK(res == 0);
  return true;
}

}  // namespace xtils
