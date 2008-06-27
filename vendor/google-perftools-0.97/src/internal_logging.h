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
// Internal logging and related utility routines.

#ifndef TCMALLOC_INTERNAL_LOGGING_H__
#define TCMALLOC_INTERNAL_LOGGING_H__

#include "config.h"
#include <stdlib.h>   // for abort()

//-------------------------------------------------------------------
// Utility routines
//-------------------------------------------------------------------

// Safe debugging routine: we write directly to the stderr file
// descriptor and avoid FILE buffering because that may invoke
// malloc()
extern void TCMalloc_MESSAGE(const char* format, ...)
#ifdef HAVE___ATTRIBUTE__
  __attribute__ ((__format__ (__printf__, 1, 2)))
#endif
;

// Short form for convenience
#define MESSAGE TCMalloc_MESSAGE

// Dumps the specified message and then calls abort()
extern void TCMalloc_CRASH(const char* format, ...)
#ifdef HAVE___ATTRIBUTE__
  __attribute__ ((__format__ (__printf__, 1, 2)))
#endif
;

#define CRASH TCMalloc_CRASH

// Like assert(), but executed even in NDEBUG mode
#undef CHECK_CONDITION
#define CHECK_CONDITION(cond)                                            \
do {                                                                     \
  if (!(cond)) {                                                         \
    CRASH("%s:%d: assertion failed: %s\n", __FILE__, __LINE__, #cond);   \
  }                                                                      \
} while (0)

// Our own version of assert() so we can avoid hanging by trying to do
// all kinds of goofy printing while holding the malloc lock.
#ifndef NDEBUG
#define ASSERT(cond) CHECK_CONDITION(cond)
#else
#define ASSERT(cond) ((void) 0)
#endif

// Print into buffer
class TCMalloc_Printer {
 private:
  char* buf_;           // Where should we write next
  int   left_;          // Space left in buffer (including space for \0)

 public:
  // REQUIRES: "length > 0"
  TCMalloc_Printer(char* buf, int length) : buf_(buf), left_(length) {
    buf[0] = '\0';
  }

  void printf(const char* format, ...)
#ifdef HAVE___ATTRIBUTE__
    __attribute__ ((__format__ (__printf__, 2, 3)))
#endif
;
};

#endif  // TCMALLOC_INTERNAL_LOGGING_H__
