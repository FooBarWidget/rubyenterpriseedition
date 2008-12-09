/* Copyright (c) 2007, Google Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ---
 * Author: Craig Silverstein
 */

#ifndef WIN32
# error You should only be including windows/port.cc in a windows environment!
#endif

#include "config.h"
#include <string.h>    // for strlen(), memset(), memcmp()
#include <assert.h>
#include <stdarg.h>    // for va_list, va_start, va_end
#include <windows.h>
#include "port.h"
#include "base/logging.h"
#include "system-alloc.h"

// -----------------------------------------------------------------------
// Basic libraries

// These call the windows _vsnprintf, but always NUL-terminate.
int safe_vsnprintf(char *str, size_t size, const char *format, va_list ap) {
  if (size == 0)        // not even room for a \0?
    return -1;          // not what C99 says to do, but what windows does
  str[size-1] = '\0';
  return _vsnprintf(str, size-1, format, ap);
}

// mingw defines its own snprintf, though msvc does not
#ifndef __MINGW32__
int snprintf(char *str, size_t size, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  const int r = vsnprintf(str, size, format, ap);
  va_end(ap);
  return r;
}
#endif

int getpagesize() {
  static int pagesize = 0;
  if (pagesize == 0) {
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    pagesize = system_info.dwPageSize;
  }
  return pagesize;
}

extern "C" PERFTOOLS_DLL_DECL void* __sbrk(ptrdiff_t increment) {
  LOG(FATAL, "Windows doesn't implement sbrk!\n");
  return NULL;
}

// -----------------------------------------------------------------------
// Threads code

bool CheckIfKernelSupportsTLS() {
  // TODO(csilvers): return true (all win's since win95, at least, support this)
  return false;
}

// Windows doesn't support pthread_key_create's destr_function, and in
// fact it's a bit tricky to get code to run when a thread exits.  This
// is cargo-cult magic from http://www.codeproject.com/threads/tls.asp.
// This code is for VC++ 7.1 and later; VC++ 6.0 support is possible
// but more busy-work -- see the webpage for how to do it.  If all
// this fails, we could use DllMain instead.  The big problem with
// DllMain is it doesn't run if this code is statically linked into a
// binary (it also doesn't run if the thread is terminated via
// TerminateThread, which if we're lucky this routine does).

// This makes the linker create the TLS directory if it's not already
// there (that is, even if __declspec(thead) is not used).
#ifdef _MSC_VER
#pragma comment(linker, "/INCLUDE:__tls_used")
#endif

// When destr_fn eventually runs, it's supposed to take as its
// argument the tls-value associated with key that pthread_key_create
// creates.  (Yeah, it sounds confusing but it's really not.)  We
// store the destr_fn/key pair in this data structure.  Because we
// store this in a single var, this implies we can only have one
// destr_fn in a program!  That's enough in practice.  If asserts
// trigger because we end up needing more, we'll have to turn this
// into an array.
struct DestrFnClosure {
  void (*destr_fn)(void*);
  pthread_key_t key_for_destr_fn_arg;
};

static DestrFnClosure destr_fn_info;   // initted to all NULL/0.

static int on_process_term(void) {
  if (destr_fn_info.destr_fn) {
    void *ptr = TlsGetValue(destr_fn_info.key_for_destr_fn_arg);
    // This shouldn't be necessary, but in Release mode, Windows
    // sometimes trashes the pointer in the TLS slot, so we need to
    // remove the pointer from the TLS slot before the thread dies.
    TlsSetValue(destr_fn_info.key_for_destr_fn_arg, NULL);
    if (ptr)  // pthread semantics say not to call if ptr is NULL
      (*destr_fn_info.destr_fn)(ptr);
  }
  return 0;
}

static void NTAPI on_tls_callback(HINSTANCE h, DWORD dwReason, PVOID pv) {
  if (dwReason == DLL_THREAD_DETACH) {   // thread is being destroyed!
    on_process_term();
  }
}

#ifdef _MSC_VER

// This tells the linker to run these functions.
#pragma data_seg(push, old_seg)
#pragma data_seg(".CRT$XLB")
static void (NTAPI *p_thread_callback)(HINSTANCE h, DWORD dwReason, PVOID pv)
    = on_tls_callback;
#pragma data_seg(".CRT$XTU")
static int (*p_process_term)(void) = on_process_term;
#pragma data_seg(pop, old_seg)

#else  // #ifdef _MSC_VER  [probably msys/mingw]

// We have to try the DllMain solution here, because we can't use the
// msvc-specific pragmas.
BOOL WINAPI DllMain(HINSTANCE h, DWORD dwReason, PVOID pv) {
  if (dwReason == DLL_THREAD_DETACH)
    on_tls_callback(h, dwReason, pv);
  else if (dwReason == DLL_PROCESS_DETACH)
    on_process_term();
  return TRUE;
}

#endif  // #ifdef _MSC_VER

pthread_key_t PthreadKeyCreate(void (*destr_fn)(void*)) {
  // Semantics are: we create a new key, and then promise to call
  // destr_fn with TlsGetValue(key) when the thread is destroyed
  // (as long as TlsGetValue(key) is not NULL).
  pthread_key_t key = TlsAlloc();
  if (destr_fn) {   // register it
    // If this assert fails, we'll need to support an array of destr_fn_infos
    assert(destr_fn_info.destr_fn == NULL);
    destr_fn_info.destr_fn = destr_fn;
    destr_fn_info.key_for_destr_fn_arg = key;
  }
  return key;
}


// -----------------------------------------------------------------------
// These functions replace system-alloc.cc

static SpinLock alloc_lock(SpinLock::LINKER_INITIALIZED);

// This is mostly like MmapSysAllocator::Alloc, except it does these weird
// munmap's in the middle of the page, which is forbidden in windows.
extern void* TCMalloc_SystemAlloc(size_t size, size_t *actual_size,
                                  size_t alignment) {
  // Safest is to make actual_size same as input-size.
  if (actual_size) {
    *actual_size = size;
  }

  SpinLockHolder sh(&alloc_lock);
  // Align on the pagesize boundary
  const int pagesize = getpagesize();
  if (alignment < pagesize) alignment = pagesize;
  size = ((size + alignment - 1) / alignment) * alignment;

  // Ask for extra memory if alignment > pagesize
  size_t extra = 0;
  if (alignment > pagesize) {
    extra = alignment - pagesize;
  }

  void* result = VirtualAlloc(0, size + extra,
                              MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
  if (result == NULL)
    return NULL;

  // Adjust the return memory so it is aligned
  uintptr_t ptr = reinterpret_cast<uintptr_t>(result);
  size_t adjust = 0;
  if ((ptr & (alignment - 1)) != 0) {
    adjust = alignment - (ptr & (alignment - 1));
  }

  ptr += adjust;
  return reinterpret_cast<void*>(ptr);
}

void TCMalloc_SystemRelease(void* start, size_t length) {
  // TODO(csilvers): should I be calling VirtualFree here?
}

bool RegisterSystemAllocator(SysAllocator *allocator, int priority) {
  return false;   // we don't allow registration on windows, right now
}

void DumpSystemAllocatorStats(TCMalloc_Printer* printer) {
  // We don't dump stats on windows, right now
}


// -----------------------------------------------------------------------
// These functions rework existing functions of the same name in the
// Google codebase.

// A replacement for HeapProfiler::CleanupOldProfiles.
void DeleteMatchingFiles(const char* prefix, const char* full_glob) {
  WIN32_FIND_DATAA found;  // that final A is for Ansi (as opposed to Unicode)
  HANDLE hFind = FindFirstFileA(full_glob, &found);   // A is for Ansi
  if (hFind != INVALID_HANDLE_VALUE) {
    const int prefix_length = strlen(prefix);
    do {
      const char *fname = found.cFileName;
      if ((strlen(fname) >= prefix_length) &&
          (memcmp(fname, prefix, prefix_length) == 0)) {
        RAW_VLOG(0, "Removing old heap profile %s\n", fname);
        // TODO(csilvers): we really need to unlink dirname + fname
        _unlink(fname);
      }
    } while (FindNextFileA(hFind, &found) != FALSE);  // A is for Ansi
    FindClose(hFind);
  }
}

// -----------------------------------------------------------------------
// Stacktrace functionality

static SpinLock get_stack_trace_lock(SpinLock::LINKER_INITIALIZED);

// TODO(csilvers): This will need some loving care to get it working.
// It's also not super-fast.

// Here are some notes from mmentovai:
// ----
// Line 140: GetThreadContext(hThread, &context);
// Doesn't work.  GetThreadContext only returns the saved thread
// context, which is only valid as a present-state snapshot for
// suspended threads.  For running threads, it's just going to be the
// context from the last time the scheduler started the thread.  You
// obviously can't suspend the current thread to grab its context.
//
// You can call RtlCaptureContext if you don't care about Win2k or
// earlier.  If you do, you'll need to provide CPU-specific code
// (usually a little bit of _asm and a function call) to grab the
// values of important registers.
// ------------------------------------
// Line 144: frame.AddrPC.Offset = context.Eip;
// This (and other uses of context members, and
// IMAGE_FILE_MACHINE_I386) is x86(-32)-only.  I see a comment about
// that below, but you should probably mention it more prominently.
//
// (I don't think there's anything nonportable about
// frame.AddrPC.Offset below.)
// ------------------------------------
// Line 148:
// You also need to set frame.AddrStack.  Its offset field gets the
// value of context.Esp (on x86).  The initial stack pointer can be
// crucial to a stackwalk in the FPO cases I mentioned.

#if 0
#include <dbghelp.h>   // Provided with Microsoft Debugging Tools for Windows
#endif

int GetStackTrace(void** result, int max_depth, int skip_count) {
  int n = 0;
#if 0  // TODO(csilvers): figure out how to get this to work
  SpinLockHolder holder(&get_stack_trace_lock);

  HANDLE hProc = GetCurrentProcess();
  HANDLE hThread = GetCurrentThread();

  CONTEXT context;
  memset(&context, 0, sizeof(context));
  context.ContextFlags = CONTEXT_FULL;
  GetThreadContext(hThread, &context);

  STACKFRAME64 frame;
  memset(&frame, 0, sizeof(frame));
  frame.AddrPC.Offset = context.Eip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = context.Ebp;
  frame.AddrFrame.Mode = AddrModeFlat;

  while (StackWalk64(IMAGE_FILE_MACHINE_I386,
                     hProc,
                     hThread,
                     &frame,
                     &context,
                     0,
                     SymFunctionTableAccess64,
                     SymGetModuleBase64,
                     0)
         && n < max_depth) {
    if (skip_count > 0) {
      skip_count--;
    } else {
      result[n++] = (void*)frame.AddrPC.Offset; // Might break x64 portability
    }
  }
#endif
  return n;
}
