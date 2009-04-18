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
 * --
 * Author: Craig Silverstein
 *
 * C shims for the C++ malloc_extension.h.  See malloc_extension.h for
 * details.  Note these C shims always work on
 * MallocExtension::instance(); it is not possible to have more than
 * one MallocExtension object in C applications.
 */

#ifndef _MALLOC_EXTENSION_C_H_
#define _MALLOC_EXTENSION_C_H_

#include <stddef.h>
#include <sys/types.h>

// Annoying stuff for windows -- makes sure clients can import these functions
#ifndef PERFTOOLS_DLL_DECL
# ifdef _WIN32
#   define PERFTOOLS_DLL_DECL  __declspec(dllimport)
# else
#   define PERFTOOLS_DLL_DECL
# endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

bool PERFTOOLS_DLL_DECL MallocExtension_VerifyAllMemory();
bool PERFTOOLS_DLL_DECL MallocExtension_VerifyAllMemory();
bool PERFTOOLS_DLL_DECL MallocExtension_VerifyNewMemory(void* p);
bool PERFTOOLS_DLL_DECL MallocExtension_VerifyArrayNewMemory(void* p);
bool PERFTOOLS_DLL_DECL MallocExtension_VerifyMallocMemory(void* p);
bool PERFTOOLS_DLL_DECL MallocExtension_MallocMemoryStats(int* blocks, size_t* total,
                                       int histogram[kMallocHistogramSize]);

void PERFTOOLS_DLL_DECL MallocExtension_GetStats(char* buffer, int buffer_length);

/* TODO(csilvers): write a C version of these routines, that perhaps
 * takes a function ptr and a void *.
 */
/* void MallocExtension_GetHeapSample(string* result); */
/* void MallocExtension_GetHeapGrowthStacks(string* result); */

bool PERFTOOLS_DLL_DECL MallocExtension_GetNumericProperty(const char* property, size_t* value);
bool PERFTOOLS_DLL_DECL MallocExtension_SetNumericProperty(const char* property, size_t value);
void PERFTOOLS_DLL_DECL MallocExtension_MarkThreadIdle();
void PERFTOOLS_DLL_DECL MallocExtension_ReleaseFreeMemory();
size_t PERFTOOLS_DLL_DECL MallocExtension_GetEstimatedAllocatedSize(size_t size);
size_t PERFTOOLS_DLL_DECL MallocExtension_GetAllocatedSize(void* p);

#ifdef __cplusplus
}   // extern "C"
#endif

#endif /* _MALLOC_EXTENSION_C_H_ */
