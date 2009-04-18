// Copyright (c) 2008, Google Inc.
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

#ifndef TCMALLOC_THREAD_CACHE_H_
#define TCMALLOC_THREAD_CACHE_H_

#include "config.h"
#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif
#include "common.h"
#include "linked_list.h"
#include "maybe_threads.h"
#include "page_heap_allocator.h"
#include "sampler.h"
#include "static_vars.h"

namespace tcmalloc {

// Even if we have support for thread-local storage in the compiler
// and linker, the OS may not support it.  We need to check that at
// runtime.  Right now, we have to keep a manual set of "bad" OSes.
#if defined(HAVE_TLS)
extern bool kernel_supports_tls;   // defined in thread_cache.cc
void CheckIfKernelSupportsTLS();
inline bool KernelSupportsTLS() {
  return kernel_supports_tls;
}
#endif    // HAVE_TLS

//-------------------------------------------------------------------
// Data kept per thread
//-------------------------------------------------------------------

class ThreadCache {
 public:
  // Default bound on the total amount of thread caches.
  static const size_t kDefaultOverallThreadCacheSize = 16 << 20;

  // All ThreadCache objects are kept in a linked list (for stats collection)
  ThreadCache* next_;
  ThreadCache* prev_;

  void Init(pthread_t tid);
  void Cleanup();

  // Accessors (mostly just for printing stats)
  int freelist_length(size_t cl) const { return list_[cl].length(); }

  // Total byte size in cache
  size_t Size() const { return size_; }

  void* Allocate(size_t size);
  void Deallocate(void* ptr, size_t size_class);

  void Scavenge();
  void Print(TCMalloc_Printer* out) const;

  int GetSamplePeriod();

  // Record allocation of "k" bytes.  Return true iff allocation
  // should be sampled
  bool SampleAllocation(size_t k);

  static void         InitModule();
  static void         InitTSD();
  static ThreadCache* GetThreadHeap();
  static ThreadCache* GetCache();
  static ThreadCache* GetCacheIfPresent();
  static ThreadCache* CreateCacheIfNecessary();
  static void         BecomeIdle();

  // Return the number of thread heaps in use.
  static inline int HeapsInUse();

  // Writes to total_bytes the total number of bytes used by all thread heaps.
  // class_count must be an array of size kNumClasses.  Writes the number of
  // items on the corresponding freelist.  class_count may be NULL.
  // The storage of both parameters must be zero intialized.
  // REQUIRES: Static::pageheap_lock is held.
  static void GetThreadStats(uint64_t* total_bytes, uint64_t* class_count);

  // Write debugging statistics to 'out'.
  // REQUIRES: Static::pageheap_lock is held.
  static void PrintThreads(TCMalloc_Printer* out);

  // Sets the total thread cache size to new_size, recomputing the
  // individual thread cache sizes as necessary.
  // REQUIRES: Static::pageheap lock is held.
  static void set_overall_thread_cache_size(size_t new_size);
  static size_t overall_thread_cache_size() {
    return overall_thread_cache_size_;
  }

  // Turn on/off dynamic sizing of the thread caches and the freelists
  // within those thread caches.  When off, threads will equally share
  // the overall_thread_cache_size() and the freelists will have a
  // max_length() of kMaxFreeListLength.  When on, threads will compete
  // for allocation of overall_thread_cache_size() and the max_length()
  // of each freelist will change based on the usage pattern.
  // REQUIRES: Static::pageheap lock is held.
  static void set_use_dynamic_thread_cache_size(bool use_dynamic);

 private:
  class FreeList {
   private:
    void*    list_;       // Linked list of nodes

#ifdef _LP64
    // On 64-bit hardware, manipulating 16-bit values may be slightly slow.
    uint32_t length_;      // Current length.
    uint32_t lowater_;     // Low water mark for list length.
    uint32_t max_length_;  // Dynamic max list length based on usage.
    // Tracks the number of times a deallocation has caused
    // length_ > max_length_.  After the kMaxOverages'th time, max_length_
    // shrinks and length_overages_ is reset to zero.
    uint32_t length_overages_;
#else
    // If we aren't using 64-bit pointers then pack these into less space.
    uint16_t length_;
    uint16_t lowater_;
    uint16_t max_length_;
    uint16_t length_overages_;
#endif

   public:
    void Init() {
      list_ = NULL;
      length_ = 0;
      lowater_ = 0;
      max_length_ = 1;
      length_overages_ = 0;
    }

    // Return current length of list
    size_t length() const {
      return length_;
    }

    // Return the maximum length of the list.
    size_t max_length() const {
      if (use_dynamic_cache_size_) {
        return max_length_;
      } else {
        return kMaxFreeListLength;
      }
    }

    // Set the maximum length of the list.  If 'new_max' > length(), the
    // client is responsible for removing objects from the list.
    void set_max_length(size_t new_max) {
      if (use_dynamic_cache_size_) {
        max_length_ = new_max;
      }
    }

    // Return the number of times that length() has gone over max_length().
    size_t length_overages() const {
      return length_overages_;
    }

    void set_length_overages(size_t new_count) {
      length_overages_ = new_count;
    }

    // Is list empty?
    bool empty() const {
      return list_ == NULL;
    }

    // Low-water mark management
    int lowwatermark() const { return lowater_; }
    void clear_lowwatermark() { lowater_ = length_; }

    void Push(void* ptr) {
      SLL_Push(&list_, ptr);
      length_++;
    }

    void* Pop() {
      ASSERT(list_ != NULL);
      length_--;
      if (length_ < lowater_) lowater_ = length_;
      return SLL_Pop(&list_);
    }

    void PushRange(int N, void *start, void *end) {
      SLL_PushRange(&list_, start, end);
      length_ += N;
    }

    void PopRange(int N, void **start, void **end) {
      SLL_PopRange(&list_, N, start, end);
      ASSERT(length_ >= N);
      length_ -= N;
      if (length_ < lowater_) lowater_ = length_;
    }
  };

  // The number of bytes one ThreadCache will steal from another when
  // the first ThreadCache is forced to Scavenge(), delaying the
  // next call to Scavenge for this thread.
  static const size_t kStealAmount = 1 << 16;

  // Lower and upper bounds on the per-thread cache sizes
  static const size_t kMinThreadCacheSize = kMaxSize * 2; //kStealAmount;
  static const size_t kMaxThreadCacheSize = 2 << 20;

  // The number of times that a deallocation can cause a freelist to
  // go over its max_length() before shrinking max_length().
  static const int kMaxOverages = 3;

  // Gets and returns an object from the central cache, and, if possible,
  // also adds some objects of that size class to this thread cache.
  void* FetchFromCentralCache(size_t cl, size_t byte_size);

  // Releases some number of items from src.  Adjusts the list's max_length
  // to eventually converge on num_objects_to_move(cl).
  void ListTooLong(FreeList* src, size_t cl);

  // Releases N items from this thread cache.
  void ReleaseToCentralCache(FreeList* src, size_t cl, int N);

  // Increase max_size_ by reducing unclaimed_cache_space_ or by
  // reducing the max_size_ of some other thread.  In both cases,
  // the delta is kStealAmount.
  void IncreaseCacheLimit();
  // Same as above but requires Static::pageheap_lock() is held.
  void IncreaseCacheLimitLocked();

  // If TLS is available, we also store a copy of the per-thread object
  // in a __thread variable since __thread variables are faster to read
  // than pthread_getspecific().  We still need pthread_setspecific()
  // because __thread variables provide no way to run cleanup code when
  // a thread is destroyed.
  // We also give a hint to the compiler to use the "initial exec" TLS
  // model.  This is faster than the default TLS model, at the cost that
  // you cannot dlopen this library.  (To see the difference, look at
  // the CPU use of __tls_get_addr with and without this attribute.)
  // Since we don't really use dlopen in google code -- and using dlopen
  // on a malloc replacement is asking for trouble in any case -- that's
  // a good tradeoff for us.
#ifdef HAVE_TLS
  static __thread ThreadCache* threadlocal_heap_
# ifdef HAVE___ATTRIBUTE__
   __attribute__ ((tls_model ("initial-exec")))
# endif
   ;
#endif

  // Thread-specific key.  Initialization here is somewhat tricky
  // because some Linux startup code invokes malloc() before it
  // is in a good enough state to handle pthread_keycreate().
  // Therefore, we use TSD keys only after tsd_inited is set to true.
  // Until then, we use a slow path to get the heap object.
  static bool tsd_inited_;
  static pthread_key_t heap_key_;

  // Linked list of heap objects.  Protected by Static::pageheap_lock.
  static ThreadCache* thread_heaps_;
  static int thread_heap_count_;

  // A pointer to one of the objects in thread_heaps_.  Represents
  // the next ThreadCache from which a thread over its max_size_ should
  // steal memory limit.  Round-robin through all of the objects in
  // thread_heaps_.  Protected by Static::pageheap_lock.
  static ThreadCache* next_memory_steal_;

  // Overall thread cache size.  Protected by Static::pageheap_lock.
  static size_t overall_thread_cache_size_;

  // Global per-thread cache size.  Writes are protected by
  // Static::pageheap_lock.  Reads are done without any locking, which should be
  // fine as long as size_t can be written atomically and we don't place
  // invariants between this variable and other pieces of state.  See
  // use_dynamic_cache_size_ below.
  static volatile size_t per_thread_cache_size_;

  // If true, threads use a dynamic max_size_ and dynamic freelist lengths.
  // If false, threads each get a max_size_ equal to per_thread_cache_size_,
  // and the freelist lengths are statically sized.
  static volatile bool use_dynamic_cache_size_;

  // Represents overall_thread_cache_size_ minus the sum of max_size_
  // across all ThreadCaches.  Protected by Static::pageheap_lock.
  static ssize_t unclaimed_cache_space_;

  // Warning: the offset of list_ affects performance.  On general
  // principles, we don't like list_[x] to span multiple L1 cache
  // lines.  However, merely placing list_ at offset 0 here seems to
  // cause cache conflicts.

  size_t        size_;                  // Combined size of data
  size_t        max_size_;              // size_ > max_size_ --> Scavenge()
  pthread_t     tid_;                   // Which thread owns it
  FreeList      list_[kNumClasses];     // Array indexed by size-class
  bool          in_setspecific_;        // In call to pthread_setspecific?

  // Allocate a new heap. REQUIRES: Static::pageheap_lock is held.
  static ThreadCache* NewHeap(pthread_t tid);

  // Use only as pthread thread-specific destructor function.
  static void DestroyThreadCache(void* ptr);

  static void DeleteCache(ThreadCache* heap);
  static void RecomputePerThreadCacheSize();

  // We sample allocations, biased by the size of the allocation
  Sampler       sampler_;               // A sampler
};

// Allocator for thread heaps
// This is logically part of the ThreadCache class, but MSVC, at
// least, does not like using ThreadCache as a template argument
// before the class is fully defined.  So we put it outside the class.
extern PageHeapAllocator<ThreadCache> threadcache_allocator;

inline int ThreadCache::HeapsInUse() {
  return threadcache_allocator.inuse();
}

inline bool ThreadCache::SampleAllocation(size_t k) {
  return sampler_.SampleAllocation(k);
}

inline void* ThreadCache::Allocate(size_t size) {
  ASSERT(size <= kMaxSize);
  const size_t cl = Static::sizemap()->SizeClass(size);
  const size_t alloc_size = Static::sizemap()->ByteSizeForClass(cl);
  FreeList* list = &list_[cl];
  if (list->empty()) {
    return FetchFromCentralCache(cl, alloc_size);
  }
  size_ -= alloc_size;
  return list->Pop();
}

inline void ThreadCache::Deallocate(void* ptr, size_t cl) {
  FreeList* list = &list_[cl];
  size_ += Static::sizemap()->ByteSizeForClass(cl);
  ssize_t size_headroom = max_size_ - size_ - 1;
  list->Push(ptr);
  ssize_t list_headroom =
      static_cast<ssize_t>(list->max_length()) - list->length();

  // There are two relatively uncommon things that require further work.
  // In the common case we're done, and in that case we need a single branch
  // because of the bitwise-or trick that follows.
  if ((list_headroom | size_headroom) < 0) {
    if (list_headroom < 0) {
      ListTooLong(list, cl);
    }
    if (size_ >= max_size_) Scavenge();
  }
}

inline ThreadCache* ThreadCache::GetThreadHeap() {
#ifdef HAVE_TLS
  // __thread is faster, but only when the kernel supports it
  if (KernelSupportsTLS())
    return threadlocal_heap_;
#endif
  return reinterpret_cast<ThreadCache *>(
      perftools_pthread_getspecific(heap_key_));
}

inline ThreadCache* ThreadCache::GetCache() {
  ThreadCache* ptr = NULL;
  if (!tsd_inited_) {
    InitModule();
  } else {
    ptr = GetThreadHeap();
  }
  if (ptr == NULL) ptr = CreateCacheIfNecessary();
  return ptr;
}

// In deletion paths, we do not try to create a thread-cache.  This is
// because we may be in the thread destruction code and may have
// already cleaned up the cache for this thread.
inline ThreadCache* ThreadCache::GetCacheIfPresent() {
  if (!tsd_inited_) return NULL;
  return GetThreadHeap();
}

}  // namespace tcmalloc

#endif  // TCMALLOC_THREAD_CACHE_H_
