/* libunwind - a platform-independent unwind library
   Copyright (c) 2003 Hewlett-Packard Development Company, L.P.
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

#include "unwind_i.h"
#include "dwarf_i.h"

HIDDEN pthread_mutex_t x86_lock = PTHREAD_MUTEX_INITIALIZER;
HIDDEN int tdep_needs_initialization = 1;

/* See comments for svr4_dbx_register_map[] in gcc/config/i386/i386.c.  */

HIDDEN uint8_t dwarf_to_unw_regnum_map[19] =
  {
    UNW_X86_EAX, UNW_X86_ECX, UNW_X86_EDX, UNW_X86_EBX,
    UNW_X86_ESP, UNW_X86_EBP, UNW_X86_ESI, UNW_X86_EDI,
    UNW_X86_EIP, UNW_X86_EFLAGS, UNW_X86_TRAPNO,
    UNW_X86_ST0, UNW_X86_ST1, UNW_X86_ST2, UNW_X86_ST3,
    UNW_X86_ST4, UNW_X86_ST5, UNW_X86_ST6, UNW_X86_ST7
  };

HIDDEN void
tdep_init (void)
{
  sigset_t saved_sigmask;

  sigfillset (&unwi_full_sigmask);

  sigprocmask (SIG_SETMASK, &unwi_full_sigmask, &saved_sigmask);
  mutex_lock (&x86_lock);
  {
    if (!tdep_needs_initialization)
      /* another thread else beat us to it... */
      goto out;

    mi_init ();

    dwarf_init ();

#ifndef UNW_REMOTE_ONLY
    x86_local_addr_space_init ();
#endif
    tdep_needs_initialization = 0;	/* signal that we're initialized... */
  }
 out:
  mutex_unlock (&x86_lock);
  sigprocmask (SIG_SETMASK, &saved_sigmask, NULL);
}
