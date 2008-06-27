/* libunwind - a platform-independent unwind library
   Copyright (C) 2003-2004 Hewlett-Packard Co
	Contributed by David Mosberger-Tang <davidm@hpl.hp.com>

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

/*  Verify that multi-threaded concurrent unwinding works as expected.  */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <libunwind.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define NTHREADS	128

#define panic(args...)						\
	do { fprintf (stderr, args); ++nerrors; } while (0)

int verbose;
int nerrors;
int got_usr1, got_usr2;
char *sigusr1_sp;

void
handler (int sig)
{
  unw_word_t ip;
  unw_context_t uc;
  unw_cursor_t c;
  int ret;

  unw_getcontext (&uc);
  unw_init_local (&c, &uc);
  do
    {
      unw_get_reg (&c, UNW_REG_IP, &ip);
      if (verbose)
	printf ("%lx: IP=%lx\n", (long) pthread_self (), (unsigned long) ip);
    }
  while ((ret = unw_step (&c)) > 0);

  if (ret < 0)
    panic ("unw_step() returned %d\n", ret);
}

void *
worker (void *arg)
{
  signal (SIGUSR1, handler);

  if (verbose)
    printf ("sending SIGUSR1\n");
  pthread_kill (pthread_self (), SIGUSR1);
  return NULL;
}

static void
doit (void)
{
  pthread_t th[NTHREADS];
  int i;

  for (i = 0; i < NTHREADS; ++i)
    pthread_create (th + i, NULL, worker, NULL);

  for (i = 0; i < NTHREADS; ++i)
    pthread_join (th[i], NULL);
}

int
main (int argc, char **argv)
{
  if (argc > 1)
    verbose = 1;

  if (verbose)
    printf ("Caching: none\n");
  unw_set_caching_policy (unw_local_addr_space, UNW_CACHE_NONE);
  doit ();

  if (verbose)
    printf ("Caching: global\n");
  unw_set_caching_policy (unw_local_addr_space, UNW_CACHE_GLOBAL);

  if (verbose)
    printf ("Caching: per-thread\n");
  unw_set_caching_policy (unw_local_addr_space, UNW_CACHE_PER_THREAD);

  if (nerrors)
    fprintf (stderr, "FAILURE: detected %d errors\n", nerrors);

  if (verbose)
    printf ("SUCCESS\n");
  return 0;
}
