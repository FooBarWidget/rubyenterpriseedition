/* libunwind - a platform-independent unwind library
   Copyright (C) 2003-2004 Hewlett-Packard Co
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

#ifndef TDEP_HPPA_H
#define TDEP_HPPA_H

/* Target-dependent definitions that are internal to libunwind but need
   to be shared with target-independent code.  */

#include <stdlib.h>
#include <libunwind.h>

enum hppa_pregnum
  {
    HPPA_NUM_PREGS
  };

struct hppa_loc
  {
    unw_word_t val;
#ifndef UNW_LOCAL_ONLY
    unw_word_t type;		/* see HPPA_LOC_TYPE_* macros.  */
#endif
  };

struct unw_addr_space
  {
    struct unw_accessors acc;
    unw_caching_policy_t caching_policy;
    uint32_t cache_generation;
    unw_word_t dyn_generation;		/* see dyn-common.h */
    unw_word_t dyn_info_list_addr;	/* (cached) dyn_info_list_addr */
   };

struct cursor
  {
    unw_addr_space_t as;
    void *as_arg;

    /* IP & SP cache: */
    unw_word_t ip;
    unw_word_t sp;

    struct hppa_loc ip_loc;
    struct hppa_loc sp_loc;
  };

/* Platforms that support UNW_INFO_FORMAT_TABLE need to define
   tdep_search_unwind_table.  */
#define tdep_search_unwind_table	UNW_ARCH_OBJ(search_unwind_table)
#define tdep_find_proc_info		UNW_ARCH_OBJ(find_proc_info)
#define tdep_put_unwind_info	 	UNW_ARCH_OBJ(put_unwind_info)
#define tdep_uc_addr			UNW_ARCH_OBJ(uc_addr)

extern int tdep_search_unwind_table (unw_addr_space_t as, unw_word_t ip,
				     unw_dyn_info_t *di, unw_proc_info_t *pi,
				     int need_unwind_info, void *arg);
extern int tdep_find_proc_info (unw_addr_space_t as, unw_word_t ip,
				unw_proc_info_t *pi, int need_unwind_info,
				void *arg);
extern void tdep_put_unwind_info (unw_addr_space_t as,
				  unw_proc_info_t *pi, void *arg);
extern void *tdep_uc_addr (ucontext_t *uc, int reg);

#endif /* TDEP_HPPA_H */
