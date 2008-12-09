// Copyright (c) 2005, Google Inc.
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// 
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// ---
// Author: Sanjay Ghemawat

#include "config.h"
#if defined HAVE_STDINT_H
#include <stdint.h>
#elif defined HAVE_INTTYPES_H
#include <inttypes.h>
#else
#include <sys/types.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fcntl.h>    // for open()
#ifdef HAVE_MMAP
#include <sys/mman.h>
#endif
#include <errno.h>
#include "system-alloc.h"
#include "internal_logging.h"
#include "base/logging.h"
#include "base/commandlineflags.h"
#include "base/spinlock.h"

// On systems (like freebsd) that don't define MAP_ANONYMOUS, use the old
// form of the name instead.
#ifndef MAP_ANONYMOUS
# define MAP_ANONYMOUS MAP_ANON
#endif

// Solaris has a bug where it doesn't declare madvise() for C++.
//    http://www.opensolaris.org/jive/thread.jspa?threadID=21035&tstart=0
#if defined(__sun) && defined(__SVR4)
# include <sys/types.h>    // for caddr_t
  extern "C" { extern int madvise(caddr_t, size_t, int); }
#endif

// Set kDebugMode mode so that we can have use C++ conditionals
// instead of preprocessor conditionals.
#ifdef NDEBUG
static const bool kDebugMode = false;
#else
static const bool kDebugMode = true;
#endif

// Structure for discovering alignment
union MemoryAligner {
  void*  p;
  double d;
  size_t s;
};

static SpinLock spinlock(SpinLock::LINKER_INITIALIZED);

#if defined(HAVE_MMAP) || defined(MADV_DONTNEED)
// Page size is initialized on demand (only needed for mmap-based allocators)
static size_t pagesize = 0;
#endif

// Configuration parameters.

DEFINE_int32(malloc_devmem_start,
             EnvToInt("TCMALLOC_DEVMEM_START", 0),
             "Physical memory starting location in MB for /dev/mem allocation."
             "  Setting this to 0 disables /dev/mem allocation");
DEFINE_int32(malloc_devmem_limit,
             EnvToInt("TCMALLOC_DEVMEM_LIMIT", 0),
             "Physical memory limit location in MB for /dev/mem allocation."
             "  Setting this to 0 means no limit.");
DEFINE_bool(malloc_skip_mmap,
            EnvToBool("TCMALLOC_SKIP_MMAP", false),
            "Whether mmap can be used to obtain memory.");

// static allocators
class SbrkSysAllocator : public SysAllocator {
public:
  SbrkSysAllocator() : SysAllocator() {
  }
  void* Alloc(size_t size, size_t *actual_size, size_t alignment);
  void DumpStats(TCMalloc_Printer* printer);
};
static char sbrk_space[sizeof(SbrkSysAllocator)];

class MmapSysAllocator : public SysAllocator {
public:
  MmapSysAllocator() : SysAllocator() {
  }
  void* Alloc(size_t size, size_t *actual_size, size_t alignment);
  void DumpStats(TCMalloc_Printer* printer);
};
static char mmap_space[sizeof(MmapSysAllocator)];

class DevMemSysAllocator : public SysAllocator {
public:
  DevMemSysAllocator() : SysAllocator() {
  }
  void* Alloc(size_t size, size_t *actual_size, size_t alignment);
  void DumpStats(TCMalloc_Printer* printer);
};
static char devmem_space[sizeof(DevMemSysAllocator)];

static const int kStaticAllocators = 3;
// kMaxDynamicAllocators + kStaticAllocators;
static const int kMaxAllocators = 5;
static SysAllocator *allocators[kMaxAllocators];

bool RegisterSystemAllocator(SysAllocator *a, int priority) {
  SpinLockHolder lock_holder(&spinlock);

  // No two allocators should have a priority conflict, since the order
  // is determined at compile time.
  CHECK_CONDITION(allocators[priority] == NULL);
  allocators[priority] = a;
  return true;
}


void* SbrkSysAllocator::Alloc(size_t size, size_t *actual_size,
                              size_t alignment) {
  // sbrk will release memory if passed a negative number, so we do
  // a strict check here
  if (static_cast<ptrdiff_t>(size + alignment) < 0) return NULL;

  // could theoretically return the "extra" bytes here, but this
  // is simple and correct.
  if (actual_size) {
    *actual_size = size;
  }

  // This doesn't overflow because TCMalloc_SystemAlloc has already
  // tested for overflow at the alignment boundary.
  size = ((size + alignment - 1) / alignment) * alignment;

  // Check that we we're not asking for so much more memory that we'd
  // wrap around the end of the virtual address space.  (This seems
  // like something sbrk() should check for us, and indeed opensolaris
  // does, but glibc does not:
  //    http://src.opensolaris.org/source/xref/onnv/onnv-gate/usr/src/lib/libc/port/sys/sbrk.c?a=true
  //    http://sourceware.org/cgi-bin/cvsweb.cgi/~checkout~/libc/misc/sbrk.c?rev=1.1.2.1&content-type=text/plain&cvsroot=glibc
  // Without this check, sbrk may succeed when it ought to fail.)
  if (reinterpret_cast<intptr_t>(sbrk(0)) + size < size) {
    failed_ = true;
    return NULL;
  }

  void* result = sbrk(size);
  if (result == reinterpret_cast<void*>(-1)) {
    failed_ = true;
    return NULL;
  }

  // Is it aligned?
  uintptr_t ptr = reinterpret_cast<uintptr_t>(result);
  if ((ptr & (alignment-1)) == 0)  return result;

  // Try to get more memory for alignment
  size_t extra = alignment - (ptr & (alignment-1));
  void* r2 = sbrk(extra);
  if (reinterpret_cast<uintptr_t>(r2) == (ptr + size)) {
    // Contiguous with previous result
    return reinterpret_cast<void*>(ptr + extra);
  }

  // Give up and ask for "size + alignment - 1" bytes so
  // that we can find an aligned region within it.
  result = sbrk(size + alignment - 1);
  if (result == reinterpret_cast<void*>(-1)) {
    failed_ = true;
    return NULL;
  }
  ptr = reinterpret_cast<uintptr_t>(result);
  if ((ptr & (alignment-1)) != 0) {
    ptr += alignment - (ptr & (alignment-1));
  }
  return reinterpret_cast<void*>(ptr);
}

void SbrkSysAllocator::DumpStats(TCMalloc_Printer* printer) {
  printer->printf("SbrkSysAllocator: failed_=%d\n", failed_);
}

void* MmapSysAllocator::Alloc(size_t size, size_t *actual_size,
                              size_t alignment) {
#ifndef HAVE_MMAP
  failed_ = true;
  return NULL;
#else
  // Check if we should use mmap allocation.
  // FLAGS_malloc_skip_mmap starts out as false (its uninitialized
  // state) and eventually gets initialized to the specified value.  Note
  // that this code runs for a while before the flags are initialized.
  // Chances are we never get here before the flags are initialized since
  // sbrk is used until the heap is exhausted (before mmap is used).
  if (FLAGS_malloc_skip_mmap) {
    return NULL;
  }

  // could theoretically return the "extra" bytes here, but this
  // is simple and correct.
  if (actual_size) {
    *actual_size = size;
  }

  // Enforce page alignment
  if (pagesize == 0) pagesize = getpagesize();
  if (alignment < pagesize) alignment = pagesize;
  size_t aligned_size = ((size + alignment - 1) / alignment) * alignment;
  if (aligned_size < size) {
    return NULL;
  }
  size = aligned_size;

  // Ask for extra memory if alignment > pagesize
  size_t extra = 0;
  if (alignment > pagesize) {
    extra = alignment - pagesize;
  }

  // Note: size + extra does not overflow since:
  //            size + alignment < (1<<NBITS).
  // and        extra <= alignment
  // therefore  size + extra < (1<<NBITS)
  void* result = mmap(NULL, size + extra,
                      PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS,
                      -1, 0);
  if (result == reinterpret_cast<void*>(MAP_FAILED)) {
    failed_ = true;
    return NULL;
  }

  // Adjust the return memory so it is aligned
  uintptr_t ptr = reinterpret_cast<uintptr_t>(result);
  size_t adjust = 0;
  if ((ptr & (alignment - 1)) != 0) {
    adjust = alignment - (ptr & (alignment - 1));
  }

  // Return the unused memory to the system
  if (adjust > 0) {
    munmap(reinterpret_cast<void*>(ptr), adjust);
  }
  if (adjust < extra) {
    munmap(reinterpret_cast<void*>(ptr + adjust + size), extra - adjust);
  }

  ptr += adjust;
  return reinterpret_cast<void*>(ptr);
#endif  // HAVE_MMAP
}

void MmapSysAllocator::DumpStats(TCMalloc_Printer* printer) {
  printer->printf("MmapSysAllocator: failed_=%d\n", failed_);
}

void* DevMemSysAllocator::Alloc(size_t size, size_t *actual_size,
                                size_t alignment) {
#ifndef HAVE_MMAP
  failed_ = true;
  return NULL;
#else
  static bool initialized = false;
  static off_t physmem_base;  // next physical memory address to allocate
  static off_t physmem_limit; // maximum physical address allowed
  static int physmem_fd;      // file descriptor for /dev/mem

  // Check if we should use /dev/mem allocation.  Note that it may take
  // a while to get this flag initialized, so meanwhile we fall back to
  // the next allocator.  (It looks like 7MB gets allocated before
  // this flag gets initialized -khr.)
  if (FLAGS_malloc_devmem_start == 0) {
    // NOTE: not a devmem_failure - we'd like TCMalloc_SystemAlloc to
    // try us again next time.
    return NULL;
  }

  if (!initialized) {
    physmem_fd = open("/dev/mem", O_RDWR);
    if (physmem_fd < 0) {
      failed_ = true;
      return NULL;
    }
    physmem_base = FLAGS_malloc_devmem_start*1024LL*1024LL;
    physmem_limit = FLAGS_malloc_devmem_limit*1024LL*1024LL;
    initialized = true;
  }

  // could theoretically return the "extra" bytes here, but this
  // is simple and correct.
  if (actual_size) {
    *actual_size = size;
  }

  // Enforce page alignment
  if (pagesize == 0) pagesize = getpagesize();
  if (alignment < pagesize) alignment = pagesize;
  size_t aligned_size = ((size + alignment - 1) / alignment) * alignment;
  if (aligned_size < size) {
    return NULL;
  }
  size = aligned_size;

  // Ask for extra memory if alignment > pagesize
  size_t extra = 0;
  if (alignment > pagesize) {
    extra = alignment - pagesize;
  }

  // check to see if we have any memory left
  if (physmem_limit != 0 &&
      ((size + extra) > (physmem_limit - physmem_base))) {
    failed_ = true;
    return NULL;
  }

  // Note: size + extra does not overflow since:
  //            size + alignment < (1<<NBITS).
  // and        extra <= alignment
  // therefore  size + extra < (1<<NBITS)
  void *result = mmap(0, size + extra, PROT_WRITE|PROT_READ,
                      MAP_SHARED, physmem_fd, physmem_base);
  if (result == reinterpret_cast<void*>(MAP_FAILED)) {
    failed_ = true;
    return NULL;
  }
  uintptr_t ptr = reinterpret_cast<uintptr_t>(result);

  // Adjust the return memory so it is aligned
  size_t adjust = 0;
  if ((ptr & (alignment - 1)) != 0) {
    adjust = alignment - (ptr & (alignment - 1));
  }

  // Return the unused virtual memory to the system
  if (adjust > 0) {
    munmap(reinterpret_cast<void*>(ptr), adjust);
  }
  if (adjust < extra) {
    munmap(reinterpret_cast<void*>(ptr + adjust + size), extra - adjust);
  }

  ptr += adjust;
  physmem_base += adjust + size;

  return reinterpret_cast<void*>(ptr);
#endif  // HAVE_MMAP
}

void DevMemSysAllocator::DumpStats(TCMalloc_Printer* printer) {
  printer->printf("DevMemSysAllocator: failed_=%d\n", failed_);
}

static bool system_alloc_inited = false;
void InitSystemAllocators(void) {
  // This determines the order in which system allocators are called
  int i = kMaxDynamicAllocators;
  allocators[i++] = new (devmem_space) DevMemSysAllocator();

  // In 64-bit debug mode, place the mmap allocator first since it
  // allocates pointers that do not fit in 32 bits and therefore gives
  // us better testing of code's 64-bit correctness.  It also leads to
  // less false negatives in heap-checking code.  (Numbers are less
  // likely to look like pointers and therefore the conservative gc in
  // the heap-checker is less likely to misinterpret a number as a
  // pointer).
  if (kDebugMode && sizeof(void*) > 4) {
    allocators[i++] = new (mmap_space) MmapSysAllocator();
    allocators[i++] = new (sbrk_space) SbrkSysAllocator();
  } else {
    allocators[i++] = new (sbrk_space) SbrkSysAllocator();
    allocators[i++] = new (mmap_space) MmapSysAllocator();
  }
}

void* TCMalloc_SystemAlloc(size_t size, size_t *actual_size,
                           size_t alignment) {
  // Discard requests that overflow
  if (size + alignment < size) return NULL;

  SpinLockHolder lock_holder(&spinlock);

  if (!system_alloc_inited) {
    InitSystemAllocators();
    system_alloc_inited = true;
  }

  // Enforce minimum alignment
  if (alignment < sizeof(MemoryAligner)) alignment = sizeof(MemoryAligner);

  // Try twice, once avoiding allocators that failed before, and once
  // more trying all allocators even if they failed before.
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < kMaxAllocators; j++) {
      SysAllocator *a = allocators[j];
      if (a == NULL) continue;
      if (a->usable_ && !a->failed_) {
        void* result = a->Alloc(size, actual_size, alignment);
        if (result != NULL) return result;
      }
    }

    // nothing worked - reset failed_ flags and try again
    for (int j = 0; j < kMaxAllocators; j++) {
      SysAllocator *a = allocators[j];
      if (a == NULL) continue;
      a->failed_ = false;
    }
  }
  return NULL;
}

void TCMalloc_SystemRelease(void* start, size_t length) {
#ifdef MADV_DONTNEED
  if (FLAGS_malloc_devmem_start) {
    // It's not safe to use MADV_DONTNEED if we've been mapping
    // /dev/mem for heap memory
    return;
  }
  if (pagesize == 0) pagesize = getpagesize();
  const size_t pagemask = pagesize - 1;

  size_t new_start = reinterpret_cast<size_t>(start);
  size_t end = new_start + length;
  size_t new_end = end;

  // Round up the starting address and round down the ending address
  // to be page aligned:
  new_start = (new_start + pagesize - 1) & ~pagemask;
  new_end = new_end & ~pagemask;

  ASSERT((new_start & pagemask) == 0);
  ASSERT((new_end & pagemask) == 0);
  ASSERT(new_start >= reinterpret_cast<size_t>(start));
  ASSERT(new_end <= end);

  if (new_end > new_start) {
    // Note -- ignoring most return codes, because if this fails it
    // doesn't matter...
    while (madvise(reinterpret_cast<char*>(new_start), new_end - new_start,
                   MADV_DONTNEED) == -1 &&
           errno == EAGAIN) {
      // NOP
    }
  }
#endif
}

void DumpSystemAllocatorStats(TCMalloc_Printer* printer) {
  for (int j = 0; j < kMaxAllocators; j++) {
    SysAllocator *a = allocators[j];
    if (a == NULL) continue;
    if (a->usable_) {
      a->DumpStats(printer);
    }
  }
}
