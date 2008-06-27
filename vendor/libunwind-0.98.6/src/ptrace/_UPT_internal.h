/* libunwind - a platform-independent unwind library
   Copyright (C) 2003 Hewlett-Packard Co
	Contributed by David Mosberger-Tang <davidm@hpl.hp.com>

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

#ifndef _UPT_internal_h
#define _UPT_internal_h

#include <errno.h>
#include <libunwind-ptrace.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/ptrace.h>

#include "internal.h"
#include "tdep.h"

struct UPT_info
  {
    pid_t pid;		/* the process-id of the child we're unwinding */
    struct elf_image ei;
    unw_dyn_info_t di_cache;
#if UNW_TARGET_IA64
    unw_dyn_info_t ktab;
#endif
  };

extern int _UPT_reg_offset[UNW_REG_LAST + 1];

extern unw_dyn_info_t *_UPTi_find_unwind_table (struct UPT_info *ui,
						unw_addr_space_t as,
						char *path,
						unw_word_t segbase,
						unw_word_t mapoff);

#endif /* _UPT_internal_h */
