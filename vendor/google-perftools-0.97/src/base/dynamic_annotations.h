/* Copyright (c) 2008, Google Inc.
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
 * Author: Kostya Serebryany
 */

// This file defines dynamic annotations for use with dynamic analysis
// tool such as valgrind, PIN, etc.
//
// Dynamic annotation is a source code annotation that affects
// the generated code (that is, the annotation is not a comment).
// Each such annotation is attached to a particular
// instruction and/or to a particular object (address) in the program.
//
// The annotations that should be used by users are macros
// (e.g. ANNOTATE_NEW_MEMORY).
//
// Actual implementation of these macros may differ depending on the
// dynamic analysis tool being used.
//
// This file supports the following dynamic analysis tools:
// - None (NDEBUG is defined).
//    Macros are defined empty.
// - Helgrind (NDEBUG is not defined).
//    Macros are defined as calls to non-inlinable empty functions
//    that are intercepted by helgrind.
//
#ifndef _BASE_DYNAMIC_ANNOTATIONS_H__
#define _BASE_DYNAMIC_ANNOTATIONS_H__


// All the annotation macros are in effect only in debug mode.
#ifndef NDEBUG

  // Report that "lock" has been created.
  #define ANNOTATE_RWLOCK_CREATE(lock) \
    AnnotateRWLockCreate(__FILE__, __LINE__, lock)

  // Report that "lock" is about to be destroyed.
  #define ANNOTATE_RWLOCK_DESTROY(lock) \
    AnnotateRWLockDestroy(__FILE__, __LINE__, lock)

  // Report that "lock" has been acquired.
  // is_w=1 for writer lock, is_w=0 for reader lock.
  #define ANNOTATE_RWLOCK_ACQUIRED(lock, is_w) \
    AnnotateRWLockAcquired(__FILE__, __LINE__, lock, is_w)

  // Report that "lock" is about to be relased.
  #define ANNOTATE_RWLOCK_RELEASED(lock, is_w) \
    AnnotateRWLockReleased(__FILE__, __LINE__, lock, is_w)

  // Report that wait on 'cv' has succeeded and 'lock' is held.
  #define ANNOTATE_CONDVAR_LOCK_WAIT(cv, lock) \
    AnnotateCondVarWait(__FILE__, __LINE__, cv, lock)

  // Report that wait on 'cv' has succeeded. Variant w/o lock.
  #define ANNOTATE_CONDVAR_WAIT(cv) \
    AnnotateCondVarWait(__FILE__, __LINE__, cv, NULL)

  // Report that we are about to signal on 'cv'.
  #define ANNOTATE_CONDVAR_SIGNAL(cv) \
    AnnotateCondVarSignal(__FILE__, __LINE__, cv)

  // Report that we are about to signal_all on 'cv'.
  #define ANNOTATE_CONDVAR_SIGNAL_ALL(cv) \
    AnnotateCondVarSignalAll(__FILE__, __LINE__, cv)

  // Report that "pcq" (ProducerConsumerQueue) has been created.
  #define ANNOTATE_PCQ_CREATE(pcq) \
    AnnotatePCQCreate(__FILE__, __LINE__, pcq)

  // Report that "pcq" is about to be destroyed.
  #define ANNOTATE_PCQ_DESTROY(pcq) \
    AnnotatePCQDestroy(__FILE__, __LINE__, pcq)

  // Report that we are about to put an element into 'pcq'.
  #define ANNOTATE_PCQ_PUT(pcq) \
    AnnotatePCQPut(__FILE__, __LINE__, pcq)

  // Report that we've just got an element from 'pcq'.
  #define ANNOTATE_PCQ_GET(pcq) \
    AnnotatePCQGet(__FILE__, __LINE__, pcq)

  // Report that a new memory 'mem' of size 'size' has been allocated.
  #define ANNOTATE_NEW_MEMORY(mem, size) \
    AnnotateNewMemory(__FILE__, __LINE__, mem, size)

  // Report that we expect a race on 'mem'. 
  // To use only in unit tests for a race detector.
  #define ANNOTATE_EXPECT_RACE(mem, description) \
    AnnotateExpectRace(__FILE__, __LINE__, mem, description)

  // Report that we may have a benign race on 'mem'.
  // Insert at the point where 'mem' exists, preferably close to the point
  // where the race happens.
  #define ANNOTATE_BENIGN_RACE(mem, description) \
    AnnotateBenignRace(__FILE__, __LINE__, mem, description)

  // Report that the mutex 'mu' will be used with LockWhen/Await.
  #define ANNOTATE_MUTEX_IS_USED_AS_CONDVAR(mu) \
    AnnotateMutexIsUsedAsCondVar(__FILE__, __LINE__, mu)

  // Request to trace every access to 'arg'.
  #define ANNOTATE_TRACE_MEMORY(arg) \
    AnnotateTraceMemory(__FILE__, __LINE__, arg)

  // A no-op. Insert where you like to test the interceptors.
  #define ANNOTATE_NO_OP(arg) \
    AnnotateNoOp(__FILE__, __LINE__, arg)

#else  // NDEBUG is defined 

  #define ANNOTATE_RWLOCK_CREATE(lock) // empty
  #define ANNOTATE_RWLOCK_DESTROY(lock) // empty
  #define ANNOTATE_RWLOCK_ACQUIRED(lock, is_w) // empty
  #define ANNOTATE_RWLOCK_RELEASED(lock, is_w) // empty
  #define ANNOTATE_CONDVAR_LOCK_WAIT(cv, lock) // empty
  #define ANNOTATE_CONDVAR_WAIT(cv) // empty
  #define ANNOTATE_CONDVAR_SIGNAL(cv) // empty
  #define ANNOTATE_CONDVAR_SIGNAL_ALL(cv) // empty
  #define ANNOTATE_PCQ_CREATE(pcq) // empty
  #define ANNOTATE_PCQ_DESTROY(pcq) // empty
  #define ANNOTATE_PCQ_PUT(pcq) // empty
  #define ANNOTATE_PCQ_GET(pcq) // empty
  #define ANNOTATE_NEW_MEMORY(mem, size) // empty
  #define ANNOTATE_EXPECT_RACE(mem, description) // empty
  #define ANNOTATE_BENIGN_RACE(mem, description) // empty
  #define ANNOTATE_MUTEX_IS_USED_AS_CONDVAR(mu) // empty
  #define ANNOTATE_TRACE_MEMORY(arg) // empty
  #define ANNOTATE_NO_OP(arg) // empty

#endif  // NDEBUG

// Use the macros above rather than using these functions directly.
extern "C" void AnnotateRWLockCreate(const char *file, int line, void *lock);
extern "C" void AnnotateRWLockDestroy(const char *file, int line, void *lock);
extern "C" void AnnotateRWLockAcquired(const char *file, int line, 
                                       void *lock, long is_w);
extern "C" void AnnotateRWLockReleased(const char *file, int line, 
                                       void *lock, long is_w);
extern "C" void AnnotateCondVarWait(const char *file, int line, void *cv, 
                                    void *lock);
extern "C" void AnnotateCondVarSignal(const char *file, int line, void *cv);
extern "C" void AnnotateCondVarSignalAll(const char *file, int line, void *cv);
extern "C" void AnnotatePCQCreate(const char *file, int line, void *pcq);
extern "C" void AnnotatePCQDestroy(const char *file, int line, void *pcq);
extern "C" void AnnotatePCQPut(const char *file, int line, void *pcq);
extern "C" void AnnotatePCQGet(const char *file, int line, void *pcq);
extern "C" void AnnotateNewMemory(const char *file, int line, void *mem, 
                                  long size);
extern "C" void AnnotateExpectRace(const char *file, int line, void *mem, 
                                   const char *description);
extern "C" void AnnotateBenignRace(const char *file, int line, void *mem, 
                                   const char *description);
extern "C" void AnnotateMutexIsUsedAsCondVar(const char *file, int line, 
                                            void *mu);
extern "C" void AnnotateTraceMemory(const char *file, int line, 
                                    const void *arg);
extern "C" void AnnotateNoOp(const char *file, int line, const void *arg);

#endif  // _BASE_DYNAMIC_ANNOTATIONS_H__
