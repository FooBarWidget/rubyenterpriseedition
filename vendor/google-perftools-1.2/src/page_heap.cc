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

#include "config.h"
#include "page_heap.h"

#include "static_vars.h"
#include "system-alloc.h"

DEFINE_double(tcmalloc_release_rate,
              EnvToDouble("TCMALLOC_RELEASE_RATE", 1.0),
              "Rate at which we release unused memory to the system.  "
              "Zero means we never release memory back to the system.  "
              "Increase this flag to return memory faster; decrease it "
              "to return memory slower.  Reasonable rates are in the "
              "range [0,10]");

namespace tcmalloc {

PageHeap::PageHeap()
    : pagemap_(MetaDataAlloc),
      pagemap_cache_(0),
      free_pages_(0),
      system_bytes_(0),
      scavenge_counter_(0),
      // Start scavenging at kMaxPages list
      scavenge_index_(kMaxPages-1) {
  COMPILE_ASSERT(kNumClasses <= (1 << PageMapCache::kValuebits), valuebits);
  DLL_Init(&large_.normal);
  DLL_Init(&large_.returned);
  for (int i = 0; i < kMaxPages; i++) {
    DLL_Init(&free_[i].normal);
    DLL_Init(&free_[i].returned);
  }
}

Span* PageHeap::New(Length n) {
  ASSERT(Check());
  ASSERT(n > 0);

  // Find first size >= n that has a non-empty list
  for (Length s = n; s < kMaxPages; s++) {
    Span* ll = &free_[s].normal;
    // If we're lucky, ll is non-empty, meaning it has a suitable span.
    if (!DLL_IsEmpty(ll)) {
      ASSERT(ll->next->location == Span::ON_NORMAL_FREELIST);
      return Carve(ll->next, n);
    }
    // Alternatively, maybe there's a usable returned span.
    ll = &free_[s].returned;
    if (!DLL_IsEmpty(ll)) {
      ASSERT(ll->next->location == Span::ON_RETURNED_FREELIST);
      return Carve(ll->next, n);
    }
    // Still no luck, so keep looking in larger classes.
  }

  Span* result = AllocLarge(n);
  if (result != NULL) return result;

  // Grow the heap and try again
  if (!GrowHeap(n)) {
    ASSERT(Check());
    return NULL;
  }

  return AllocLarge(n);
}

Span* PageHeap::AllocLarge(Length n) {
  // find the best span (closest to n in size).
  // The following loops implements address-ordered best-fit.
  Span *best = NULL;

  // Search through normal list
  for (Span* span = large_.normal.next;
       span != &large_.normal;
       span = span->next) {
    if (span->length >= n) {
      if ((best == NULL)
          || (span->length < best->length)
          || ((span->length == best->length) && (span->start < best->start))) {
        best = span;
        ASSERT(best->location == Span::ON_NORMAL_FREELIST);
      }
    }
  }

  // Search through released list in case it has a better fit
  for (Span* span = large_.returned.next;
       span != &large_.returned;
       span = span->next) {
    if (span->length >= n) {
      if ((best == NULL)
          || (span->length < best->length)
          || ((span->length == best->length) && (span->start < best->start))) {
        best = span;
        ASSERT(best->location == Span::ON_RETURNED_FREELIST);
      }
    }
  }

  return best == NULL ? NULL : Carve(best, n);
}

Span* PageHeap::Split(Span* span, Length n) {
  ASSERT(0 < n);
  ASSERT(n < span->length);
  ASSERT(span->location == Span::IN_USE);
  ASSERT(span->sizeclass == 0);
  Event(span, 'T', n);

  const int extra = span->length - n;
  Span* leftover = NewSpan(span->start + n, extra);
  ASSERT(leftover->location == Span::IN_USE);
  Event(leftover, 'U', extra);
  RecordSpan(leftover);
  pagemap_.set(span->start + n - 1, span); // Update map from pageid to span
  span->length = n;

  return leftover;
}

Span* PageHeap::Carve(Span* span, Length n) {
  ASSERT(n > 0);
  ASSERT(span->location != Span::IN_USE);
  const int old_location = span->location;
  DLL_Remove(span);
  span->location = Span::IN_USE;
  Event(span, 'A', n);

  const int extra = span->length - n;
  ASSERT(extra >= 0);
  if (extra > 0) {
    Span* leftover = NewSpan(span->start + n, extra);
    leftover->location = old_location;
    Event(leftover, 'S', extra);
    RecordSpan(leftover);

    // Place leftover span on appropriate free list
    SpanList* listpair = (extra < kMaxPages) ? &free_[extra] : &large_;
    Span* dst = (leftover->location == Span::ON_RETURNED_FREELIST
                 ? &listpair->returned : &listpair->normal);
    DLL_Prepend(dst, leftover);

    span->length = n;
    pagemap_.set(span->start + n - 1, span);
  }
  ASSERT(Check());
  free_pages_ -= n;
  return span;
}

void PageHeap::Delete(Span* span) {
  ASSERT(Check());
  ASSERT(span->location == Span::IN_USE);
  ASSERT(span->length > 0);
  ASSERT(GetDescriptor(span->start) == span);
  ASSERT(GetDescriptor(span->start + span->length - 1) == span);
  span->sizeclass = 0;
  span->sample = 0;

  // Coalesce -- we guarantee that "p" != 0, so no bounds checking
  // necessary.  We do not bother resetting the stale pagemap
  // entries for the pieces we are merging together because we only
  // care about the pagemap entries for the boundaries.
  //
  // Note that the spans we merge into "span" may come out of
  // a "returned" list.  For simplicity, we move these into the
  // "normal" list of the appropriate size class.
  const PageID p = span->start;
  const Length n = span->length;
  Span* prev = GetDescriptor(p-1);
  if (prev != NULL && prev->location != Span::IN_USE) {
    // Merge preceding span into this span
    ASSERT(prev->start + prev->length == p);
    const Length len = prev->length;
    DLL_Remove(prev);
    DeleteSpan(prev);
    span->start -= len;
    span->length += len;
    pagemap_.set(span->start, span);
    Event(span, 'L', len);
  }
  Span* next = GetDescriptor(p+n);
  if (next != NULL && next->location != Span::IN_USE) {
    // Merge next span into this span
    ASSERT(next->start == p+n);
    const Length len = next->length;
    DLL_Remove(next);
    DeleteSpan(next);
    span->length += len;
    pagemap_.set(span->start + span->length - 1, span);
    Event(span, 'R', len);
  }

  Event(span, 'D', span->length);
  span->location = Span::ON_NORMAL_FREELIST;
  if (span->length < kMaxPages) {
    DLL_Prepend(&free_[span->length].normal, span);
  } else {
    DLL_Prepend(&large_.normal, span);
  }
  free_pages_ += n;

  IncrementalScavenge(n);
  ASSERT(Check());
}

void PageHeap::IncrementalScavenge(Length n) {
  // Fast path; not yet time to release memory
  scavenge_counter_ -= n;
  if (scavenge_counter_ >= 0) return;  // Not yet time to scavenge

  // Never delay scavenging for more than the following number of
  // deallocated pages.  With 4K pages, this comes to 4GB of
  // deallocation.
  static const int kMaxReleaseDelay = 1 << 20;

  // If there is nothing to release, wait for so many pages before
  // scavenging again.  With 4K pages, this comes to 1GB of memory.
  static const int kDefaultReleaseDelay = 1 << 18;

  const double rate = FLAGS_tcmalloc_release_rate;
  if (rate <= 1e-6) {
    // Tiny release rate means that releasing is disabled.
    scavenge_counter_ = kDefaultReleaseDelay;
    return;
  }

  // Find index of free list to scavenge
  int index = scavenge_index_ + 1;
  for (int i = 0; i < kMaxPages+1; i++) {
    if (index > kMaxPages) index = 0;
    SpanList* slist = (index == kMaxPages) ? &large_ : &free_[index];
    if (!DLL_IsEmpty(&slist->normal)) {
      // Release the last span on the normal portion of this list
      Span* s = slist->normal.prev;
      ASSERT(s->location == Span::ON_NORMAL_FREELIST);
      DLL_Remove(s);
      TCMalloc_SystemRelease(reinterpret_cast<void*>(s->start << kPageShift),
                             static_cast<size_t>(s->length << kPageShift));
      s->location = Span::ON_RETURNED_FREELIST;
      DLL_Prepend(&slist->returned, s);

      // Compute how long to wait until we return memory.
      // FLAGS_tcmalloc_release_rate==1 means wait for 1000 pages
      // after releasing one page.
      const double mult = 1000.0 / rate;
      double wait = mult * static_cast<double>(s->length);
      if (wait > kMaxReleaseDelay) {
        // Avoid overflow and bound to reasonable range
        wait = kMaxReleaseDelay;
      }
      scavenge_counter_ = static_cast<int64_t>(wait);

      scavenge_index_ = index;  // Scavenge at index+1 next time
      return;
    }
    index++;
  }

  // Nothing to scavenge, delay for a while
  scavenge_counter_ = kDefaultReleaseDelay;
}

void PageHeap::RegisterSizeClass(Span* span, size_t sc) {
  // Associate span object with all interior pages as well
  ASSERT(span->location == Span::IN_USE);
  ASSERT(GetDescriptor(span->start) == span);
  ASSERT(GetDescriptor(span->start+span->length-1) == span);
  Event(span, 'C', sc);
  span->sizeclass = sc;
  for (Length i = 1; i < span->length-1; i++) {
    pagemap_.set(span->start+i, span);
  }
}

static double PagesToMB(uint64_t pages) {
  return (pages << kPageShift) / 1048576.0;
}

void PageHeap::Dump(TCMalloc_Printer* out) {
  int nonempty_sizes = 0;
  for (int s = 0; s < kMaxPages; s++) {
    if (!DLL_IsEmpty(&free_[s].normal) || !DLL_IsEmpty(&free_[s].returned)) {
      nonempty_sizes++;
    }
  }
  out->printf("------------------------------------------------\n");
  out->printf("PageHeap: %d sizes; %6.1f MB free\n",
              nonempty_sizes, PagesToMB(free_pages_));
  out->printf("------------------------------------------------\n");
  uint64_t total_normal = 0;
  uint64_t total_returned = 0;
  for (int s = 0; s < kMaxPages; s++) {
    const int n_length = DLL_Length(&free_[s].normal);
    const int r_length = DLL_Length(&free_[s].returned);
    if (n_length + r_length > 0) {
      uint64_t n_pages = s * n_length;
      uint64_t r_pages = s * r_length;
      total_normal += n_pages;
      total_returned += r_pages;
      out->printf("%6u pages * %6u spans ~ %6.1f MB; %6.1f MB cum"
                  "; unmapped: %6.1f MB; %6.1f MB cum\n",
                  s,
                  (n_length + r_length),
                  PagesToMB(n_pages + r_pages),
                  PagesToMB(total_normal + total_returned),
                  PagesToMB(r_pages),
                  PagesToMB(total_returned));
    }
  }

  uint64_t n_pages = 0;
  uint64_t r_pages = 0;
  int n_spans = 0;
  int r_spans = 0;
  out->printf("Normal large spans:\n");
  for (Span* s = large_.normal.next; s != &large_.normal; s = s->next) {
    out->printf("   [ %6" PRIuPTR " pages ] %6.1f MB\n",
                s->length, PagesToMB(s->length));
    n_pages += s->length;
    n_spans++;
  }
  out->printf("Unmapped large spans:\n");
  for (Span* s = large_.returned.next; s != &large_.returned; s = s->next) {
    out->printf("   [ %6" PRIuPTR " pages ] %6.1f MB\n",
                s->length, PagesToMB(s->length));
    r_pages += s->length;
    r_spans++;
  }
  total_normal += n_pages;
  total_returned += r_pages;
  out->printf(">255   large * %6u spans ~ %6.1f MB; %6.1f MB cum"
              "; unmapped: %6.1f MB; %6.1f MB cum\n",
              (n_spans + r_spans),
              PagesToMB(n_pages + r_pages),
              PagesToMB(total_normal + total_returned),
              PagesToMB(r_pages),
              PagesToMB(total_returned));
}

static void RecordGrowth(size_t growth) {
  StackTrace* t = Static::stacktrace_allocator()->New();
  t->depth = GetStackTrace(t->stack, kMaxStackDepth-1, 3);
  t->size = growth;
  t->stack[kMaxStackDepth-1] = reinterpret_cast<void*>(Static::growth_stacks());
  Static::set_growth_stacks(t);
}

bool PageHeap::GrowHeap(Length n) {
  ASSERT(kMaxPages >= kMinSystemAlloc);
  if (n > kMaxValidPages) return false;
  Length ask = (n>kMinSystemAlloc) ? n : static_cast<Length>(kMinSystemAlloc);
  size_t actual_size;
  void* ptr = TCMalloc_SystemAlloc(ask << kPageShift, &actual_size, kPageSize);
  if (ptr == NULL) {
    if (n < ask) {
      // Try growing just "n" pages
      ask = n;
      ptr = TCMalloc_SystemAlloc(ask << kPageShift, &actual_size, kPageSize);
    }
    if (ptr == NULL) return false;
  }
  ask = actual_size >> kPageShift;
  RecordGrowth(ask << kPageShift);

  uint64_t old_system_bytes = system_bytes_;
  system_bytes_ += (ask << kPageShift);
  const PageID p = reinterpret_cast<uintptr_t>(ptr) >> kPageShift;
  ASSERT(p > 0);

  // If we have already a lot of pages allocated, just pre allocate a bunch of
  // memory for the page map. This prevents fragmentation by pagemap metadata
  // when a program keeps allocating and freeing large blocks.

  if (old_system_bytes < kPageMapBigAllocationThreshold
      && system_bytes_ >= kPageMapBigAllocationThreshold) {
    pagemap_.PreallocateMoreMemory();
  }

  // Make sure pagemap_ has entries for all of the new pages.
  // Plus ensure one before and one after so coalescing code
  // does not need bounds-checking.
  if (pagemap_.Ensure(p-1, ask+2)) {
    // Pretend the new area is allocated and then Delete() it to
    // cause any necessary coalescing to occur.
    //
    // We do not adjust free_pages_ here since Delete() will do it for us.
    Span* span = NewSpan(p, ask);
    RecordSpan(span);
    Delete(span);
    ASSERT(Check());
    return true;
  } else {
    // We could not allocate memory within "pagemap_"
    // TODO: Once we can return memory to the system, return the new span
    return false;
  }
}

bool PageHeap::Check() {
  ASSERT(free_[0].normal.next == &free_[0].normal);
  ASSERT(free_[0].returned.next == &free_[0].returned);
  return true;
}

bool PageHeap::CheckExpensive() {
  bool result = Check();
  CheckList(&large_.normal, kMaxPages, 1000000000, Span::ON_NORMAL_FREELIST);
  CheckList(&large_.returned, kMaxPages, 1000000000, Span::ON_RETURNED_FREELIST);
  for (Length s = 1; s < kMaxPages; s++) {
    CheckList(&free_[s].normal, s, s, Span::ON_NORMAL_FREELIST);
    CheckList(&free_[s].returned, s, s, Span::ON_RETURNED_FREELIST);
  }
  return result;
}

bool PageHeap::CheckList(Span* list, Length min_pages, Length max_pages,
                         int freelist) {
  for (Span* s = list->next; s != list; s = s->next) {
    CHECK_CONDITION(s->location == freelist);  // NORMAL or RETURNED
    CHECK_CONDITION(s->length >= min_pages);
    CHECK_CONDITION(s->length <= max_pages);
    CHECK_CONDITION(GetDescriptor(s->start) == s);
    CHECK_CONDITION(GetDescriptor(s->start+s->length-1) == s);
  }
  return true;
}

static void ReleaseFreeList(Span* list, Span* returned) {
  // Walk backwards through list so that when we push these
  // spans on the "returned" list, we preserve the order.
  while (!DLL_IsEmpty(list)) {
    Span* s = list->prev;
    DLL_Remove(s);
    DLL_Prepend(returned, s);
    ASSERT(s->location == Span::ON_NORMAL_FREELIST);
    s->location = Span::ON_RETURNED_FREELIST;
    TCMalloc_SystemRelease(reinterpret_cast<void*>(s->start << kPageShift),
                           static_cast<size_t>(s->length << kPageShift));
  }
}

void PageHeap::ReleaseFreePages() {
  for (Length s = 0; s < kMaxPages; s++) {
    ReleaseFreeList(&free_[s].normal, &free_[s].returned);
  }
  ReleaseFreeList(&large_.normal, &large_.returned);
  ASSERT(Check());
}

}  // namespace tcmalloc
