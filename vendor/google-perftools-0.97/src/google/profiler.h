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
//
// Module for CPU profiling based on periodic pc-sampling.
//
// For full(er) information, see doc/cpuprofile.html
//
// This module is linked into your program with
// no slowdown caused by this unless you activate the profiler
// using one of the following methods:
//
//    1. Before starting the program, set the environment variable
//       "PROFILE" to be the name of the file to which the profile
//       data should be written.
//
//    2. Programmatically, start and stop the profiler using the
//       routines "ProfilerStart(filename)" and "ProfilerStop()".
//
// All threads in the program are profiled whenever profiling is on.
// (Note: if using linux 2.4 or earlier, only the main thread may be
// profiled.)
//
// Use pprof to view the resulting profile output.
//    % pprof <path_to_executable> <profile_file_name>
//    % pprof --gv  <path_to_executable> <profile_file_name>
//
// These functions are thread-safe.

#ifndef BASE_PROFILER_H__
#define BASE_PROFILER_H__

#include <time.h>       // For time_t

// Annoying stuff for windows -- makes sure clients can import these functions
#ifndef PERFTOOLS_DLL_DECL
# ifdef WIN32
#   define PERFTOOLS_DLL_DECL  __declspec(dllimport)
# else
#   define PERFTOOLS_DLL_DECL
# endif
#endif

// Start profiling and write profile info into fname.
extern "C" PERFTOOLS_DLL_DECL bool ProfilerStart(const char* fname);

// Start profiling and write profile info into fname, filtering out
// threads using a filter function.
//
// When a profiling tick is delivered, the profiler will call:
//
//   (*filter_in_thread)(filter_in_thread_arg)
//
// If it returns true, the sample will be included in the profile.
// Note that filter_in_thread runs in a signal handler, so must be
// async-signal-safe.
//
// A typical use would be to set up filter results for each thread in
// the system before starting the profiler, then to make
// filter_in_thread be a very simple function which retrieves those
// results in an async-signal-safe way.  Retrieval could be done using
// thread-specific data, or using a shared data structure that
// supports async-signal-safe lookups.
extern "C" bool ProfilerStartFiltered(const char* fname,
                                      bool (*filter_in_thread)(void* arg),
                                      void *filter_in_thread_arg);

// Stop profiling. Can be started again with ProfilerStart(), but
// the currently accumulated profiling data will be cleared.
extern "C" PERFTOOLS_DLL_DECL void ProfilerStop();

// Flush any currently buffered profiling state to the profile file.
// Has no effect if the profiler has not been started.
extern "C" PERFTOOLS_DLL_DECL void ProfilerFlush();


// DEPRECATED: these functions were used to enable/disable profiling
// in the current thread, but no longer do anything.
extern "C" PERFTOOLS_DLL_DECL void ProfilerEnable();
extern "C" PERFTOOLS_DLL_DECL void ProfilerDisable();

// Returns true if profile is currently enabled
extern "C" PERFTOOLS_DLL_DECL bool ProfilingIsEnabledForAllThreads();

// Routine for registering new threads with the profiler.  This is
// most usefully called when a new thread is first entered.
extern "C" PERFTOOLS_DLL_DECL void ProfilerRegisterThread();

// Stores state about profiler's current status into "*state".
struct ProfilerState {
  bool   enabled;                // Is profiling currently enabled?
  time_t start_time;             // If enabled, when was profiling started?
  char   profile_name[1024];     // Name of profile file being written, or '\0'
  int    samples_gathered;       // Number of samples gatheered to far (or 0)
};
extern "C" PERFTOOLS_DLL_DECL void ProfilerGetCurrentState(ProfilerState* state);

#endif /* BASE_PROFILER_H__ */
