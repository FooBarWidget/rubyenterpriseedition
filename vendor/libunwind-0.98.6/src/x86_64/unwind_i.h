/* libunwind - a platform-independent unwind library
   Copyright (C) 2002 Hewlett-Packard Co
	Contributed by David Mosberger-Tang <davidm@hpl.hp.com>

   Modified for x86_64 by Max Asbock <masbock@us.ibm.com>

This file is part of libunwind.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.  */

#ifndef unwind_i_h
#define unwind_i_h

#include <memory.h>
#include <stdint.h>

#include <libunwind-x86_64.h>

#include "internal.h"
#include "tdep.h"
#include <sys/ucontext.h>

/* DWARF column numbers for x86_64: */
#define RAX	0
#define RDX	1
#define RCX	2
#define RBX	3
#define RSI	4
#define RDI	5
#define RBP	6
#define RSP	7
#define R8	8
#define R9	9
#define R10	10
#define R11	11
#define R12	12
#define R13	13
#define R14	14
#define R15	15
#define RIP	16

#define x86_64_lock			UNW_OBJ(lock)
#define x86_64_local_resume		UNW_OBJ(local_resume)
#define x86_64_local_addr_space_init	UNW_OBJ(local_addr_space_init)
#if 0
#define x86_64_scratch_loc		UNW_OBJ(scratch_loc)
#endif

extern void x86_64_local_addr_space_init (void);
extern int x86_64_local_resume (unw_addr_space_t as, unw_cursor_t *cursor,
			     void *arg);
#if 0
extern dwarf_loc_t x86_64_scratch_loc (struct cursor *c, unw_regnum_t reg);
#endif

#endif /* unwind_i_h */
