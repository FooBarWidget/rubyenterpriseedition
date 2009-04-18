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
// Author: Sanjay Ghemawat <opensource@google.com>
//
// A malloc that uses a per-thread cache to satisfy small malloc requests.
// (The time for malloc/free of a small object drops from 300 ns to 50 ns.)
//
// See doc/tcmalloc.html for a high-level
// description of how this malloc works.
//
// SYNCHRONIZATION
//  1. The thread-specific lists are accessed without acquiring any locks.
//     This is safe because each such list is only accessed by one thread.
//  2. We have a lock per central free-list, and hold it while manipulating
//     the central free list for a particular size.
//  3. The central page allocator is protected by "pageheap_lock".
//  4. The pagemap (which maps from page-number to descriptor),
//     can be read without holding any locks, and written while holding
//     the "pageheap_lock".
//  5. To improve performance, a subset of the information one can get
//     from the pagemap is cached in a data structure, pagemap_cache_,
//     that atomically reads and writes its entries.  This cache can be
//     read and written without locking.
//
//     This multi-threaded access to the pagemap is safe for fairly
//     subtle reasons.  We basically assume that when an object X is
//     allocated by thread A and deallocated by thread B, there must
//     have been appropriate synchronization in the handoff of object
//     X from thread A to thread B.  The same logic applies to pagemap_cache_.
//
// THE PAGEID-TO-SIZECLASS CACHE
// Hot PageID-to-sizeclass mappings are held by pagemap_cache_.  If this cache
// returns 0 for a particular PageID then that means "no information," not that
// the sizeclass is 0.  The cache may have stale information for pages that do
// not hold the beginning of any free()'able object.  Staleness is eliminated
// in Populate() for pages with sizeclass > 0 objects, and in do_malloc() and
// do_memalign() for all other relevant pages.
//
// PAGEMAP
// -------
// Page map contains a mapping from page id to Span.
//
// If Span s occupies pages [p..q],
//      pagemap[p] == s
//      pagemap[q] == s
//      pagemap[p+1..q-1] are undefined
//      pagemap[p-1] and pagemap[q+1] are defined:
//         NULL if the corresponding page is not yet in the address space.
//         Otherwise it points to a Span.  This span may be free
//         or allocated.  If free, it is in one of pageheap's freelist.
//
// TODO: Bias reclamation to larger addresses
// TODO: implement mallinfo/mallopt
// TODO: Better testing
//
// 9/28/2003 (new page-level allocator replaces ptmalloc2):
// * malloc/free of small objects goes from ~300 ns to ~50 ns.
// * allocation of a reasonably complicated struct
//   goes from about 1100 ns to about 300 ns.

#include "config.h"
#include <new>
#include <stdio.h>
#include <stddef.h>
#if defined HAVE_STDINT_H
#include <stdint.h>
#elif defined HAVE_INTTYPES_H
#include <inttypes.h>
#else
#include <sys/types.h>
#endif
#if defined(HAVE_MALLOC_H) && defined(HAVE_STRUCT_MALLINFO)
#include <malloc.h>                        // for struct mallinfo
#endif
#include <string.h>
#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>
#include <stdarg.h>
#include <algorithm>
#include "base/commandlineflags.h"
#include "base/basictypes.h"               // gets us PRIu64
#include "base/sysinfo.h"
#include "base/spinlock.h"
#include "common.h"
#include "malloc_hook-inl.h"
#include <google/malloc_hook.h>
#include <google/malloc_extension.h>
#include "central_freelist.h"
#include "internal_logging.h"
#include "linked_list.h"
#include "maybe_threads.h"
#include "page_heap.h"
#include "page_heap_allocator.h"
#include "pagemap.h"
#include "span.h"
#include "static_vars.h"
#include "system-alloc.h"
#include "tcmalloc_guard.h"
#include "thread_cache.h"

#if (defined(_WIN32) && !defined(__CYGWIN__) && !defined(__CYGWIN32__)) && !defined(WIN32_OVERRIDE_ALLOCATORS)
# define WIN32_DO_PATCHING 1
#endif

using tcmalloc::PageHeap;
using tcmalloc::PageHeapAllocator;
using tcmalloc::SizeMap;
using tcmalloc::Span;
using tcmalloc::StackTrace;
using tcmalloc::Static;
using tcmalloc::ThreadCache;

// __THROW is defined in glibc systems.  It means, counter-intuitively,
// "This function will never throw an exception."  It's an optional
// optimization tool, but we may need to use it to match glibc prototypes.
#ifndef __THROW    // I guess we're not on a glibc system
# define __THROW   // __THROW is just an optimization, so ok to make it ""
#endif

DECLARE_int64(tcmalloc_sample_parameter);
DECLARE_double(tcmalloc_release_rate);

// For windows, the printf we use to report large allocs is
// potentially dangerous: it could cause a malloc that would cause an
// infinite loop.  So by default we set the threshold to a huge number
// on windows, so this bad situation will never trigger.  You can
// always set TCMALLOC_LARGE_ALLOC_REPORT_THRESHOLD manually if you
// want this functionality.
#ifdef _WIN32
const int64 kDefaultLargeAllocReportThreshold = static_cast<int64>(1) << 62;
#else
const int64 kDefaultLargeAllocReportThreshold = static_cast<int64>(1) << 30;
#endif
DEFINE_int64(tcmalloc_large_alloc_report_threshold,
             EnvToInt64("TCMALLOC_LARGE_ALLOC_REPORT_THRESHOLD",
                        kDefaultLargeAllocReportThreshold),
             "Allocations larger than this value cause a stack "
             "trace to be dumped to stderr.  The threshold for "
             "dumping stack traces is increased by a factor of 1.125 "
             "every time we print a message so that the threshold "
             "automatically goes up by a factor of ~1000 every 60 "
             "messages.  This bounds the amount of extra logging "
             "generated by this flag.  Default value of this flag "
             "is very large and therefore you should see no extra "
             "logging unless the flag is overridden.  Set to 0 to "
             "disable reporting entirely.");

// These routines are called by free(), realloc(), etc. if the pointer is
// invalid.  This is a cheap (source-editing required) kind of exception
// handling for these routines.
namespace {
void InvalidFree(void* ptr) {
  CRASH("Attempt to free invalid pointer: %p\n", ptr);
}

size_t InvalidGetSizeForRealloc(void* old_ptr) {
  CRASH("Attempt to realloc invalid pointer: %p\n", old_ptr);
  return 0;
}

size_t InvalidGetAllocatedSize(void* ptr) {
  CRASH("Attempt to get the size of an invalid pointer: %p\n", ptr);
  return 0;
}
}  // unnamed namespace

// Extract interesting stats
struct TCMallocStats {
  uint64_t system_bytes;        // Bytes alloced from system
  uint64_t thread_bytes;        // Bytes in thread caches
  uint64_t central_bytes;       // Bytes in central cache
  uint64_t transfer_bytes;      // Bytes in central transfer cache
  uint64_t pageheap_bytes;      // Bytes in page heap
  uint64_t metadata_bytes;      // Bytes alloced for metadata
};

// Get stats into "r".  Also get per-size-class counts if class_count != NULL
static void ExtractStats(TCMallocStats* r, uint64_t* class_count) {
  r->central_bytes = 0;
  r->transfer_bytes = 0;
  for (int cl = 0; cl < kNumClasses; ++cl) {
    const int length = Static::central_cache()[cl].length();
    const int tc_length = Static::central_cache()[cl].tc_length();
    const size_t size = static_cast<uint64_t>(
        Static::sizemap()->ByteSizeForClass(cl));
    r->central_bytes += (size * length);
    r->transfer_bytes += (size * tc_length);
    if (class_count) class_count[cl] = length + tc_length;
  }

  // Add stats from per-thread heaps
  r->thread_bytes = 0;
  { // scope
    SpinLockHolder h(Static::pageheap_lock());
    ThreadCache::GetThreadStats(&r->thread_bytes, class_count);
  }

  { //scope
    SpinLockHolder h(Static::pageheap_lock());
    r->system_bytes = Static::pageheap()->SystemBytes();
    r->metadata_bytes = tcmalloc::metadata_system_bytes();
    r->pageheap_bytes = Static::pageheap()->FreeBytes();
  }
}

// WRITE stats to "out"
static void DumpStats(TCMalloc_Printer* out, int level) {
  TCMallocStats stats;
  uint64_t class_count[kNumClasses];
  ExtractStats(&stats, (level >= 2 ? class_count : NULL));

  static const double MB = 1048576.0;

  if (level >= 2) {
    out->printf("------------------------------------------------\n");
    uint64_t cumulative = 0;
    for (int cl = 0; cl < kNumClasses; ++cl) {
      if (class_count[cl] > 0) {
        uint64_t class_bytes =
            class_count[cl] * Static::sizemap()->ByteSizeForClass(cl);
        cumulative += class_bytes;
        out->printf("class %3d [ %8" PRIuS " bytes ] : "
                "%8" PRIu64 " objs; %5.1f MB; %5.1f cum MB\n",
                cl, Static::sizemap()->ByteSizeForClass(cl),
                class_count[cl],
                class_bytes / MB,
                cumulative / MB);
      }
    }

    SpinLockHolder h(Static::pageheap_lock());
    Static::pageheap()->Dump(out);

    out->printf("------------------------------------------------\n");
    DumpSystemAllocatorStats(out);
  }

  const uint64_t bytes_in_use = stats.system_bytes
                                - stats.pageheap_bytes
                                - stats.central_bytes
                                - stats.transfer_bytes
                                - stats.thread_bytes;

  out->printf("------------------------------------------------\n"
              "MALLOC: %12" PRIu64 " (%7.1f MB) Heap size\n"
              "MALLOC: %12" PRIu64 " (%7.1f MB) Bytes in use by application\n"
              "MALLOC: %12" PRIu64 " (%7.1f MB) Bytes free in page heap\n"
              "MALLOC: %12" PRIu64 " (%7.1f MB) Bytes free in central cache\n"
              "MALLOC: %12" PRIu64 " (%7.1f MB) Bytes free in transfer cache\n"
              "MALLOC: %12" PRIu64 " (%7.1f MB) Bytes free in thread caches\n"
              "MALLOC: %12" PRIu64 "              Spans in use\n"
              "MALLOC: %12" PRIu64 "              Thread heaps in use\n"
              "MALLOC: %12" PRIu64 " (%7.1f MB) Metadata allocated\n"
              "------------------------------------------------\n",
              stats.system_bytes, stats.system_bytes / MB,
              bytes_in_use, bytes_in_use / MB,
              stats.pageheap_bytes, stats.pageheap_bytes / MB,
              stats.central_bytes, stats.central_bytes / MB,
              stats.transfer_bytes, stats.transfer_bytes / MB,
              stats.thread_bytes, stats.thread_bytes / MB,
              uint64_t(Static::span_allocator()->inuse()),
              uint64_t(ThreadCache::HeapsInUse()),
              stats.metadata_bytes, stats.metadata_bytes / MB);
}

static void PrintStats(int level) {
  const int kBufferSize = 16 << 10;
  char* buffer = new char[kBufferSize];
  TCMalloc_Printer printer(buffer, kBufferSize);
  DumpStats(&printer, level);
  write(STDERR_FILENO, buffer, strlen(buffer));
  delete[] buffer;
}

static void** DumpHeapGrowthStackTraces() {
  // Count how much space we need
  int needed_slots = 0;
  {
    SpinLockHolder h(Static::pageheap_lock());
    for (StackTrace* t = Static::growth_stacks();
         t != NULL;
         t = reinterpret_cast<StackTrace*>(
             t->stack[tcmalloc::kMaxStackDepth-1])) {
      needed_slots += 3 + t->depth;
    }
    needed_slots += 100;            // Slop in case list grows
    needed_slots += needed_slots/8; // An extra 12.5% slop
  }

  void** result = new void*[needed_slots];
  if (result == NULL) {
    MESSAGE("tcmalloc: allocation failed for stack trace slots",
            needed_slots * sizeof(*result));
    return NULL;
  }

  SpinLockHolder h(Static::pageheap_lock());
  int used_slots = 0;
  for (StackTrace* t = Static::growth_stacks();
       t != NULL;
       t = reinterpret_cast<StackTrace*>(
           t->stack[tcmalloc::kMaxStackDepth-1])) {
    ASSERT(used_slots < needed_slots);  // Need to leave room for terminator
    if (used_slots + 3 + t->depth >= needed_slots) {
      // No more room
      break;
    }

    result[used_slots+0] = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    result[used_slots+1] = reinterpret_cast<void*>(t->size);
    result[used_slots+2] = reinterpret_cast<void*>(t->depth);
    for (int d = 0; d < t->depth; d++) {
      result[used_slots+3+d] = t->stack[d];
    }
    used_slots += 3 + t->depth;
  }
  result[used_slots] = reinterpret_cast<void*>(static_cast<uintptr_t>(0));
  return result;
}

// TCMalloc's support for extra malloc interfaces
class TCMallocImplementation : public MallocExtension {
 public:
  virtual void GetStats(char* buffer, int buffer_length) {
    ASSERT(buffer_length > 0);
    TCMalloc_Printer printer(buffer, buffer_length);

    // Print level one stats unless lots of space is available
    if (buffer_length < 10000) {
      DumpStats(&printer, 1);
    } else {
      DumpStats(&printer, 2);
    }
  }

  virtual void** ReadStackTraces(int* sample_period) {
    tcmalloc::StackTraceTable table;
    {
      SpinLockHolder h(Static::pageheap_lock());
      Span* sampled = Static::sampled_objects();
      for (Span* s = sampled->next; s != sampled; s = s->next) {
        table.AddTrace(*reinterpret_cast<StackTrace*>(s->objects));
      }
    }
    *sample_period = ThreadCache::GetCache()->GetSamplePeriod();
    return table.ReadStackTracesAndClear(); // grabs and releases pageheap_lock
  }

  virtual void** ReadHeapGrowthStackTraces() {
    return DumpHeapGrowthStackTraces();
  }

  virtual bool GetNumericProperty(const char* name, size_t* value) {
    ASSERT(name != NULL);

    if (strcmp(name, "generic.current_allocated_bytes") == 0) {
      TCMallocStats stats;
      ExtractStats(&stats, NULL);
      *value = stats.system_bytes
               - stats.thread_bytes
               - stats.central_bytes
               - stats.transfer_bytes
               - stats.pageheap_bytes;
      return true;
    }

    if (strcmp(name, "generic.heap_size") == 0) {
      TCMallocStats stats;
      ExtractStats(&stats, NULL);
      *value = stats.system_bytes;
      return true;
    }

    if (strcmp(name, "tcmalloc.slack_bytes") == 0) {
      // We assume that bytes in the page heap are not fragmented too
      // badly, and are therefore available for allocation.
      SpinLockHolder l(Static::pageheap_lock());
      *value = Static::pageheap()->FreeBytes();
      return true;
    }

    if (strcmp(name, "tcmalloc.max_total_thread_cache_bytes") == 0) {
      SpinLockHolder l(Static::pageheap_lock());
      *value = ThreadCache::overall_thread_cache_size();
      return true;
    }

    if (strcmp(name, "tcmalloc.current_total_thread_cache_bytes") == 0) {
      TCMallocStats stats;
      ExtractStats(&stats, NULL);
      *value = stats.thread_bytes;
      return true;
    }

    return false;
  }

  virtual bool SetNumericProperty(const char* name, size_t value) {
    ASSERT(name != NULL);

    if (strcmp(name, "tcmalloc.max_total_thread_cache_bytes") == 0) {
      SpinLockHolder l(Static::pageheap_lock());
      ThreadCache::set_overall_thread_cache_size(value);
      return true;
    }

    return false;
  }

  virtual void MarkThreadIdle() {
    ThreadCache::BecomeIdle();
  }

  virtual void ReleaseFreeMemory() {
    SpinLockHolder h(Static::pageheap_lock());
    Static::pageheap()->ReleaseFreePages();
  }

  virtual void SetMemoryReleaseRate(double rate) {
    FLAGS_tcmalloc_release_rate = rate;
  }

  virtual double GetMemoryReleaseRate() {
    return FLAGS_tcmalloc_release_rate;
  }
  virtual size_t GetEstimatedAllocatedSize(size_t size) {
    if (size <= kMaxSize) {
      const size_t cl = Static::sizemap()->SizeClass(size);
      const size_t alloc_size = Static::sizemap()->ByteSizeForClass(cl);
      return alloc_size;
    } else {
      return tcmalloc::pages(size) << kPageShift;
    }
  }

  // This just calls GetSizeWithCallback, but because that's in an
  // unnamed namespace, we need to move the definition below it in the
  // file.
  virtual size_t GetAllocatedSize(void* ptr);
};

// The constructor allocates an object to ensure that initialization
// runs before main(), and therefore we do not have a chance to become
// multi-threaded before initialization.  We also create the TSD key
// here.  Presumably by the time this constructor runs, glibc is in
// good enough shape to handle pthread_key_create().
//
// The constructor also takes the opportunity to tell STL to use
// tcmalloc.  We want to do this early, before construct time, so
// all user STL allocations go through tcmalloc (which works really
// well for STL).
//
// The destructor prints stats when the program exits.
static int tcmallocguard_refcount = 0;  // no lock needed: runs before main()
TCMallocGuard::TCMallocGuard() {
  if (tcmallocguard_refcount++ == 0) {
#ifdef HAVE_TLS    // this is true if the cc/ld/libc combo support TLS
    // Check whether the kernel also supports TLS (needs to happen at runtime)
    tcmalloc::CheckIfKernelSupportsTLS();
#endif
#ifdef WIN32_DO_PATCHING
    // patch the windows VirtualAlloc, etc.
    PatchWindowsFunctions();    // defined in windows/patch_functions.cc
#endif
    free(malloc(1));
    ThreadCache::InitTSD();
    free(malloc(1));
    MallocExtension::Register(new TCMallocImplementation);
  }
}

TCMallocGuard::~TCMallocGuard() {
  if (--tcmallocguard_refcount == 0) {
    const char* env = getenv("MALLOCSTATS");
    if (env != NULL) {
      int level = atoi(env);
      if (level < 1) level = 1;
      PrintStats(level);
    }
  }
}
#ifndef WIN32_OVERRIDE_ALLOCATORS
static TCMallocGuard module_enter_exit_hook;
#endif

//-------------------------------------------------------------------
// Helpers for the exported routines below
//-------------------------------------------------------------------

static Span* DoSampledAllocation(size_t size) {
  // Grab the stack trace outside the heap lock
  StackTrace tmp;
  tmp.depth = GetStackTrace(tmp.stack, tcmalloc::kMaxStackDepth, 1);
  tmp.size = size;

  SpinLockHolder h(Static::pageheap_lock());
  // Allocate span
  Span *span = Static::pageheap()->New(tcmalloc::pages(size == 0 ? 1 : size));
  if (span == NULL) {
    return NULL;
  }

  // Allocate stack trace
  StackTrace *stack = Static::stacktrace_allocator()->New();
  if (stack == NULL) {
    // Sampling failed because of lack of memory
    return span;
  }

  *stack = tmp;
  span->sample = 1;
  span->objects = stack;
  tcmalloc::DLL_Prepend(Static::sampled_objects(), span);

  return span;
}

static inline bool CheckCachedSizeClass(void *ptr) {
  PageID p = reinterpret_cast<uintptr_t>(ptr) >> kPageShift;
  size_t cached_value = Static::pageheap()->GetSizeClassIfCached(p);
  return cached_value == 0 ||
      cached_value == Static::pageheap()->GetDescriptor(p)->sizeclass;
}

static inline void* CheckedMallocResult(void *result)
{
  ASSERT(result == 0 || CheckCachedSizeClass(result));
  return result;
}

static inline void* SpanToMallocResult(Span *span) {
  Static::pageheap()->CacheSizeClass(span->start, 0);
  return
      CheckedMallocResult(reinterpret_cast<void*>(span->start << kPageShift));
}

// Copy of FLAGS_tcmalloc_large_alloc_report_threshold with
// automatic increases factored in.
static int64_t large_alloc_threshold =
  (kPageSize > FLAGS_tcmalloc_large_alloc_report_threshold
   ? kPageSize : FLAGS_tcmalloc_large_alloc_report_threshold);

static void ReportLargeAlloc(Length num_pages, void* result) {
  StackTrace stack;
  stack.depth = GetStackTrace(stack.stack, tcmalloc::kMaxStackDepth, 1);

  static const int N = 1000;
  char buffer[N];
  TCMalloc_Printer printer(buffer, N);
  printer.printf("tcmalloc: large alloc %llu bytes == %p @ ",
                 static_cast<unsigned long long>(num_pages) << kPageShift,
                 result);
  for (int i = 0; i < stack.depth; i++) {
    printer.printf(" %p", stack.stack[i]);
  }
  printer.printf("\n");
  write(STDERR_FILENO, buffer, strlen(buffer));
}

namespace {

// Helper for do_malloc().
inline void* do_malloc_pages(Length num_pages) {
  Span *span;
  bool report_large = false;
  {
    SpinLockHolder h(Static::pageheap_lock());
    span = Static::pageheap()->New(num_pages);
    const int64 threshold = large_alloc_threshold;
    if (threshold > 0 && num_pages >= (threshold >> kPageShift)) {
      // Increase the threshold by 1/8 every time we generate a report.
      // We cap the threshold at 8GB to avoid overflow problems.
      large_alloc_threshold = (threshold + threshold/8 < 8ll<<30
                               ? threshold + threshold/8 : 8ll<<30);
      report_large = true;
    }
  }

  void* result = (span == NULL ? NULL : SpanToMallocResult(span));
  if (report_large) {
    ReportLargeAlloc(num_pages, result);
  }
  return result;
}

inline void* do_malloc(size_t size) {
  void* ret = NULL;

  // The following call forces module initialization
  ThreadCache* heap = ThreadCache::GetCache();
  if ((FLAGS_tcmalloc_sample_parameter > 0) && heap->SampleAllocation(size)) {
    Span* span = DoSampledAllocation(size);
    if (span != NULL) {
      ret = SpanToMallocResult(span);
    }
  } else if (size <= kMaxSize) {
    // The common case, and also the simplest.  This just pops the
    // size-appropriate freelist, after replenishing it if it's empty.
    ret = CheckedMallocResult(heap->Allocate(size));
  } else {
    ret = do_malloc_pages(tcmalloc::pages(size));
  }
  if (ret == NULL) errno = ENOMEM;
  return ret;
}

inline void* do_calloc(size_t n, size_t elem_size) {
  // Overflow check
  const size_t size = n * elem_size;
  if (elem_size != 0 && size / elem_size != n) return NULL;

  void* result = do_malloc(size);
  if (result != NULL) {
    memset(result, 0, size);
  }
  return result;
}

static inline ThreadCache* GetCacheIfPresent() {
  void* const p = ThreadCache::GetCacheIfPresent();
  return reinterpret_cast<ThreadCache*>(p);
}

// This lets you call back to a given function pointer if ptr is invalid.
// It is used primarily by windows code which wants a specialized callback.
inline void do_free_with_callback(void* ptr, void (*invalid_free_fn)(void*)) {
  if (ptr == NULL) return;
  ASSERT(Static::pageheap() != NULL);  // Should not call free() before malloc()
  const PageID p = reinterpret_cast<uintptr_t>(ptr) >> kPageShift;
  Span* span = NULL;
  size_t cl = Static::pageheap()->GetSizeClassIfCached(p);

  if (cl == 0) {
    span = Static::pageheap()->GetDescriptor(p);
    if (!span) {
      // span can be NULL because the pointer passed in is invalid
      // (not something returned by malloc or friends), or because the
      // pointer was allocated with some other allocator besides
      // tcmalloc.  The latter can happen if tcmalloc is linked in via
      // a dynamic library, but is not listed last on the link line.
      // In that case, libraries after it on the link line will
      // allocate with libc malloc, but free with tcmalloc's free.
      (*invalid_free_fn)(ptr);  // Decide how to handle the bad free request
      return;
    }
    cl = span->sizeclass;
    Static::pageheap()->CacheSizeClass(p, cl);
  }
  if (cl != 0) {
    ASSERT(!Static::pageheap()->GetDescriptor(p)->sample);
    ThreadCache* heap = GetCacheIfPresent();
    if (heap != NULL) {
      heap->Deallocate(ptr, cl);
    } else {
      // Delete directly into central cache
      tcmalloc::SLL_SetNext(ptr, NULL);
      Static::central_cache()[cl].InsertRange(ptr, ptr, 1);
    }
  } else {
    SpinLockHolder h(Static::pageheap_lock());
    ASSERT(reinterpret_cast<uintptr_t>(ptr) % kPageSize == 0);
    ASSERT(span != NULL && span->start == p);
    if (span->sample) {
      tcmalloc::DLL_Remove(span);
      Static::stacktrace_allocator()->Delete(
          reinterpret_cast<StackTrace*>(span->objects));
      span->objects = NULL;
    }
    Static::pageheap()->Delete(span);
  }
}

// The default "do_free" that uses the default callback.
inline void do_free(void* ptr) {
  return do_free_with_callback(ptr, &InvalidFree);
}

inline size_t GetSizeWithCallback(void* ptr,
                                  size_t (*invalid_getsize_fn)(void*)) {
  if (ptr == NULL)
    return 0;
  const PageID p = reinterpret_cast<uintptr_t>(ptr) >> kPageShift;
  size_t cl = Static::pageheap()->GetSizeClassIfCached(p);
  if (cl != 0) {
    return Static::sizemap()->ByteSizeForClass(cl);
  } else {
    Span *span = Static::pageheap()->GetDescriptor(p);
    if (span == NULL) {  // means we do not own this memory
      return (*invalid_getsize_fn)(ptr);
    } else if (span->sizeclass != 0) {
      Static::pageheap()->CacheSizeClass(p, span->sizeclass);
      return Static::sizemap()->ByteSizeForClass(span->sizeclass);
    } else {
      return span->length << kPageShift;
    }
  }
}

// This lets you call back to a given function pointer if ptr is invalid.
// It is used primarily by windows code which wants a specialized callback.
inline void* do_realloc_with_callback(
    void* old_ptr, size_t new_size,
    void (*invalid_free_fn)(void*),
    size_t (*invalid_get_size_fn)(void*)) {
  // Get the size of the old entry
  const size_t old_size = GetSizeWithCallback(old_ptr, invalid_get_size_fn);

  // Reallocate if the new size is larger than the old size,
  // or if the new size is significantly smaller than the old size.
  // We do hysteresis to avoid resizing ping-pongs:
  //    . If we need to grow, grow to max(new_size, old_size * 1.X)
  //    . Don't shrink unless new_size < old_size * 0.Y
  // X and Y trade-off time for wasted space.  For now we do 1.25 and 0.5.
  const int lower_bound_to_grow = old_size + old_size / 4;
  const int upper_bound_to_shrink = old_size / 2;
  if ((new_size > old_size) || (new_size < upper_bound_to_shrink)) {
    // Need to reallocate.
    void* new_ptr = NULL;

    if (new_size > old_size && new_size < lower_bound_to_grow) {
      new_ptr = do_malloc(lower_bound_to_grow);
    }
    if (new_ptr == NULL) {
      // Either new_size is not a tiny increment, or last do_malloc failed.
      new_ptr = do_malloc(new_size);
    }
    if (new_ptr == NULL) {
      return NULL;
    }
    MallocHook::InvokeNewHook(new_ptr, new_size);
    memcpy(new_ptr, old_ptr, ((old_size < new_size) ? old_size : new_size));
    MallocHook::InvokeDeleteHook(old_ptr);
    // We could use a variant of do_free() that leverages the fact
    // that we already know the sizeclass of old_ptr.  The benefit
    // would be small, so don't bother.
    do_free_with_callback(old_ptr, invalid_free_fn);
    return new_ptr;
  } else {
    // We still need to call hooks to report the updated size:
    MallocHook::InvokeDeleteHook(old_ptr);
    MallocHook::InvokeNewHook(old_ptr, new_size);
    return old_ptr;
  }
}

inline void* do_realloc(void* old_ptr, size_t new_size) {
  return do_realloc_with_callback(old_ptr, new_size,
                                  &InvalidFree, &InvalidGetSizeForRealloc);
}

// For use by exported routines below that want specific alignments
//
// Note: this code can be slow, and can significantly fragment memory.
// The expectation is that memalign/posix_memalign/valloc/pvalloc will
// not be invoked very often.  This requirement simplifies our
// implementation and allows us to tune for expected allocation
// patterns.
void* do_memalign(size_t align, size_t size) {
  ASSERT((align & (align - 1)) == 0);
  ASSERT(align > 0);
  if (size + align < size) return NULL;         // Overflow

  if (Static::pageheap() == NULL) ThreadCache::InitModule();

  // Allocate at least one byte to avoid boundary conditions below
  if (size == 0) size = 1;

  if (size <= kMaxSize && align < kPageSize) {
    // Search through acceptable size classes looking for one with
    // enough alignment.  This depends on the fact that
    // InitSizeClasses() currently produces several size classes that
    // are aligned at powers of two.  We will waste time and space if
    // we miss in the size class array, but that is deemed acceptable
    // since memalign() should be used rarely.
    int cl = Static::sizemap()->SizeClass(size);
    while (cl < kNumClasses &&
           ((Static::sizemap()->class_to_size(cl) & (align - 1)) != 0)) {
      cl++;
    }
    if (cl < kNumClasses) {
      ThreadCache* heap = ThreadCache::GetCache();
      return CheckedMallocResult(heap->Allocate(
                                     Static::sizemap()->class_to_size(cl)));
    }
  }

  // We will allocate directly from the page heap
  SpinLockHolder h(Static::pageheap_lock());

  if (align <= kPageSize) {
    // Any page-level allocation will be fine
    // TODO: We could put the rest of this page in the appropriate
    // TODO: cache but it does not seem worth it.
    Span* span = Static::pageheap()->New(tcmalloc::pages(size));
    return span == NULL ? NULL : SpanToMallocResult(span);
  }

  // Allocate extra pages and carve off an aligned portion
  const Length alloc = tcmalloc::pages(size + align);
  Span* span = Static::pageheap()->New(alloc);
  if (span == NULL) return NULL;

  // Skip starting portion so that we end up aligned
  Length skip = 0;
  while ((((span->start+skip) << kPageShift) & (align - 1)) != 0) {
    skip++;
  }
  ASSERT(skip < alloc);
  if (skip > 0) {
    Span* rest = Static::pageheap()->Split(span, skip);
    Static::pageheap()->Delete(span);
    span = rest;
  }

  // Skip trailing portion that we do not need to return
  const Length needed = tcmalloc::pages(size);
  ASSERT(span->length >= needed);
  if (span->length > needed) {
    Span* trailer = Static::pageheap()->Split(span, needed);
    Static::pageheap()->Delete(trailer);
  }
  return SpanToMallocResult(span);
}

// Helpers for use by exported routines below:

inline void do_malloc_stats() {
  PrintStats(1);
}

inline int do_mallopt(int cmd, int value) {
  return 1;     // Indicates error
}

#ifdef HAVE_STRUCT_MALLINFO  // mallinfo isn't defined on freebsd, for instance
inline struct mallinfo do_mallinfo() {
  TCMallocStats stats;
  ExtractStats(&stats, NULL);

  // Just some of the fields are filled in.
  struct mallinfo info;
  memset(&info, 0, sizeof(info));

  // Unfortunately, the struct contains "int" field, so some of the
  // size values will be truncated.
  info.arena     = static_cast<int>(stats.system_bytes);
  info.fsmblks   = static_cast<int>(stats.thread_bytes
                                    + stats.central_bytes
                                    + stats.transfer_bytes);
  info.fordblks  = static_cast<int>(stats.pageheap_bytes);
  info.uordblks  = static_cast<int>(stats.system_bytes
                                    - stats.thread_bytes
                                    - stats.central_bytes
                                    - stats.transfer_bytes
                                    - stats.pageheap_bytes);

  return info;
}
#endif  // #ifndef HAVE_STRUCT_MALLINFO

static SpinLock set_new_handler_lock(SpinLock::LINKER_INITIALIZED);

inline void* cpp_alloc(size_t size, bool nothrow) {
  for (;;) {
    void* p = do_malloc(size);
#if defined(PREANSINEW) || (defined(__GNUC__) && !defined(__EXCEPTIONS)) || (defined(_HAS_EXCEPTIONS) && !_HAS_EXCEPTIONS)
    return p;
#else
    if (p == NULL) {  // allocation failed
      // Get the current new handler.  NB: this function is not
      // thread-safe.  We make a feeble stab at making it so here, but
      // this lock only protects against tcmalloc interfering with
      // itself, not with other libraries calling set_new_handler.
      std::new_handler nh;
      {
        SpinLockHolder h(&set_new_handler_lock);
        nh = std::set_new_handler(0);
        (void) std::set_new_handler(nh);
      }
      // If no new_handler is established, the allocation failed.
      if (!nh) {
        if (nothrow) return 0;
        throw std::bad_alloc();
      }
      // Otherwise, try the new_handler.  If it returns, retry the
      // allocation.  If it throws std::bad_alloc, fail the allocation.
      // if it throws something else, don't interfere.
      try {
        (*nh)();
      } catch (const std::bad_alloc&) {
        if (!nothrow) throw;
        return p;
      }
    } else {  // allocation success
      return p;
    }
#endif
  }
}

}  // end unnamed namespace

// As promised, the definition of this function, declared above.
size_t TCMallocImplementation::GetAllocatedSize(void* ptr) {
  return GetSizeWithCallback(ptr, &InvalidGetAllocatedSize);
}

//-------------------------------------------------------------------
// Exported routines
//-------------------------------------------------------------------

#ifndef WIN32_DO_PATCHING

// CAVEAT: The code structure below ensures that MallocHook methods are always
//         called from the stack frame of the invoked allocation function.
//         heap-checker.cc depends on this to start a stack trace from
//         the call to the (de)allocation function.

// Put all callers of MallocHook::Invoke* in this module into
// ATTRIBUTE_SECTION(google_malloc) section,
// so that MallocHook::GetCallerStackTrace can function accurately:

// NOTE: __THROW expands to 'throw()', which means 'never throws.'  Urgh.
extern "C" {
  void* malloc(size_t size)
      __THROW ATTRIBUTE_SECTION(google_malloc);
  void free(void* ptr)
      __THROW ATTRIBUTE_SECTION(google_malloc);
  void* realloc(void* ptr, size_t size)
      __THROW ATTRIBUTE_SECTION(google_malloc);
  void* calloc(size_t nmemb, size_t size)
      __THROW ATTRIBUTE_SECTION(google_malloc);
  void cfree(void* ptr)
      __THROW ATTRIBUTE_SECTION(google_malloc);

  void* memalign(size_t __alignment, size_t __size)
      __THROW ATTRIBUTE_SECTION(google_malloc);
  int posix_memalign(void** ptr, size_t align, size_t size)
      __THROW ATTRIBUTE_SECTION(google_malloc);
  void* valloc(size_t __size)
      __THROW ATTRIBUTE_SECTION(google_malloc);
  void* pvalloc(size_t __size)
      __THROW ATTRIBUTE_SECTION(google_malloc);
}

void* operator new(size_t size)
    ATTRIBUTE_SECTION(google_malloc);
void operator delete(void* p)
    __THROW ATTRIBUTE_SECTION(google_malloc);
void* operator new[](size_t size)
    ATTRIBUTE_SECTION(google_malloc);
void operator delete[](void* p)
    __THROW ATTRIBUTE_SECTION(google_malloc);

// And the nothrow variants of these:
void* operator new(size_t size, const std::nothrow_t&)
    __THROW ATTRIBUTE_SECTION(google_malloc);
void operator delete(void* p, const std::nothrow_t&)
    __THROW ATTRIBUTE_SECTION(google_malloc);
void* operator new[](size_t size, const std::nothrow_t&)
    __THROW ATTRIBUTE_SECTION(google_malloc);
void operator delete[](void* p, const std::nothrow_t&)
    __THROW ATTRIBUTE_SECTION(google_malloc);

static void *MemalignOverride(size_t align, size_t size, const void *caller)
    __THROW ATTRIBUTE_SECTION(google_malloc);

extern "C" void* malloc(size_t size) __THROW {
  void* result = do_malloc(size);
  MallocHook::InvokeNewHook(result, size);
  return result;
}

extern "C" void free(void* ptr) __THROW {
  MallocHook::InvokeDeleteHook(ptr);
  do_free(ptr);
}

extern "C" void* calloc(size_t n, size_t elem_size) __THROW {
  void* result = do_calloc(n, elem_size);
  MallocHook::InvokeNewHook(result, n * elem_size);
  return result;
}

extern "C" void cfree(void* ptr) __THROW {
  MallocHook::InvokeDeleteHook(ptr);
  do_free(ptr);
}

extern "C" void* realloc(void* old_ptr, size_t new_size) __THROW {
  if (old_ptr == NULL) {
    void* result = do_malloc(new_size);
    MallocHook::InvokeNewHook(result, new_size);
    return result;
  }
  if (new_size == 0) {
    MallocHook::InvokeDeleteHook(old_ptr);
    do_free(old_ptr);
    return NULL;
  }
  return do_realloc(old_ptr, new_size);
}

void* operator new(size_t size) {
  void* p = cpp_alloc(size, false);
  // We keep this next instruction out of cpp_alloc for a reason: when
  // it's in, and new just calls cpp_alloc, the optimizer may fold the
  // new call into cpp_alloc, which messes up our whole section-based
  // stacktracing (see ATTRIBUTE_SECTION, above).  This ensures cpp_alloc
  // isn't the last thing this fn calls, and prevents the folding.
  MallocHook::InvokeNewHook(p, size);
  return p;
}

void* operator new(size_t size, const std::nothrow_t&) __THROW {
  void* p = cpp_alloc(size, true);
  MallocHook::InvokeNewHook(p, size);
  return p;
}

void operator delete(void* p) __THROW {
  MallocHook::InvokeDeleteHook(p);
  do_free(p);
}

void operator delete(void* p, const std::nothrow_t&) __THROW {
  MallocHook::InvokeDeleteHook(p);
  do_free(p);
}

void* operator new[](size_t size) {
  void* p = cpp_alloc(size, false);
  // We keep this next instruction out of cpp_alloc for a reason: when
  // it's in, and new just calls cpp_alloc, the optimizer may fold the
  // new call into cpp_alloc, which messes up our whole section-based
  // stacktracing (see ATTRIBUTE_SECTION, above).  This ensures cpp_alloc
  // isn't the last thing this fn calls, and prevents the folding.
  MallocHook::InvokeNewHook(p, size);
  return p;
}

void* operator new[](size_t size, const std::nothrow_t&) __THROW {
  void* p = cpp_alloc(size, true);
  MallocHook::InvokeNewHook(p, size);
  return p;
}

void operator delete[](void* p) __THROW {
  MallocHook::InvokeDeleteHook(p);
  do_free(p);
}

void operator delete[](void* p, const std::nothrow_t&) __THROW {
  MallocHook::InvokeDeleteHook(p);
  do_free(p);
}

extern "C" void* memalign(size_t align, size_t size) __THROW {
  void* result = do_memalign(align, size);
  MallocHook::InvokeNewHook(result, size);
  return result;
}

extern "C" int posix_memalign(void** result_ptr, size_t align, size_t size)
    __THROW {
  if (((align % sizeof(void*)) != 0) ||
      ((align & (align - 1)) != 0) ||
      (align == 0)) {
    return EINVAL;
  }

  void* result = do_memalign(align, size);
  MallocHook::InvokeNewHook(result, size);
  if (result == NULL) {
    return ENOMEM;
  } else {
    *result_ptr = result;
    return 0;
  }
}

static size_t pagesize = 0;

extern "C" void* valloc(size_t size) __THROW {
  // Allocate page-aligned object of length >= size bytes
  if (pagesize == 0) pagesize = getpagesize();
  void* result = do_memalign(pagesize, size);
  MallocHook::InvokeNewHook(result, size);
  return result;
}

extern "C" void* pvalloc(size_t size) __THROW {
  // Round up size to a multiple of pagesize
  if (pagesize == 0) pagesize = getpagesize();
  size = (size + pagesize - 1) & ~(pagesize - 1);
  void* result = do_memalign(pagesize, size);
  MallocHook::InvokeNewHook(result, size);
  return result;
}

extern "C" void malloc_stats(void) {
  do_malloc_stats();
}

extern "C" int mallopt(int cmd, int value) {
  return do_mallopt(cmd, value);
}

#ifdef HAVE_STRUCT_MALLINFO
extern "C" struct mallinfo mallinfo(void) {
  return do_mallinfo();
}
#endif

//-------------------------------------------------------------------
// Some library routines on RedHat 9 allocate memory using malloc()
// and free it using __libc_free() (or vice-versa).  Since we provide
// our own implementations of malloc/free, we need to make sure that
// the __libc_XXX variants (defined as part of glibc) also point to
// the same implementations.
//-------------------------------------------------------------------

#if defined(__GLIBC__)
extern "C" {
# if defined(__GNUC__) && !defined(__MACH__) && defined(HAVE___ATTRIBUTE__)
  // Potentially faster variants that use the gcc alias extension.
  // Mach-O (Darwin) does not support weak aliases, hence the __MACH__ check.
# define ALIAS(x) __attribute__ ((weak, alias (x)))
  void* __libc_malloc(size_t size)              ALIAS("malloc");
  void  __libc_free(void* ptr)                  ALIAS("free");
  void* __libc_realloc(void* ptr, size_t size)  ALIAS("realloc");
  void* __libc_calloc(size_t n, size_t size)    ALIAS("calloc");
  void  __libc_cfree(void* ptr)                 ALIAS("cfree");
  void* __libc_memalign(size_t align, size_t s) ALIAS("memalign");
  void* __libc_valloc(size_t size)              ALIAS("valloc");
  void* __libc_pvalloc(size_t size)             ALIAS("pvalloc");
  int __posix_memalign(void** r, size_t a, size_t s) ALIAS("posix_memalign");
# undef ALIAS
# else   /* not __GNUC__ */
  // Portable wrappers
  void* __libc_malloc(size_t size)              { return malloc(size);       }
  void  __libc_free(void* ptr)                  { free(ptr);                 }
  void* __libc_realloc(void* ptr, size_t size)  { return realloc(ptr, size); }
  void* __libc_calloc(size_t n, size_t size)    { return calloc(n, size);    }
  void  __libc_cfree(void* ptr)                 { cfree(ptr);                }
  void* __libc_memalign(size_t align, size_t s) { return memalign(align, s); }
  void* __libc_valloc(size_t size)              { return valloc(size);       }
  void* __libc_pvalloc(size_t size)             { return pvalloc(size);      }
  int __posix_memalign(void** r, size_t a, size_t s) {
    return posix_memalign(r, a, s);
  }
# endif  /* __GNUC__ */
}
#endif   /* __GLIBC__ */

// Override __libc_memalign in libc on linux boxes specially.
// They have a bug in libc that causes them to (very rarely) allocate
// with __libc_memalign() yet deallocate with free() and the
// definitions above don't catch it.
// This function is an exception to the rule of calling MallocHook method
// from the stack frame of the allocation function;
// heap-checker handles this special case explicitly.
static void *MemalignOverride(size_t align, size_t size, const void *caller)
    __THROW {
  void* result = do_memalign(align, size);
  MallocHook::InvokeNewHook(result, size);
  return result;
}
void *(*__memalign_hook)(size_t, size_t, const void *) = MemalignOverride;

#endif  // #ifndef WIN32_DO_PATCHING
