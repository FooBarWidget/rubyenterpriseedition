/* libunwind - a platform-independent unwind library
   Copyright (C) 2001-2004 Hewlett-Packard Co
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

#ifndef internal_h
#define internal_h

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE___THREAD
  /* For now, turn off per-thread caching.  It uses up too much TLS
     memory per thread even when the thread never uses libunwind at
     all.  */
# undef HAVE___THREAD
#endif

/* Platform-independent libunwind-internal declarations.  */

#include <sys/types.h>	/* HP-UX needs this before include of pthread.h */

#include <assert.h>
#include <libunwind.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_ENDIAN_H
# include <endian.h>
#else
# define __LITTLE_ENDIAN	1234
# define __BIG_ENDIAN		4321
# if defined(__hpux)
#   define __BYTE_ORDER __BIG_ENDIAN
# else
#   error Host has unknown byte-order.
# endif
#endif

#ifdef __GNUC__
# define UNUSED		__attribute__((unused))
# define NORETURN	__attribute__((noreturn))
# define ALIAS(name)	__attribute__((alias (#name)))
# if (__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ > 2)
#  define ALWAYS_INLINE	__attribute__((always_inline))
#  define HIDDEN	__attribute__((visibility ("hidden")))
#  define PROTECTED	__attribute__((visibility ("protected")))
# else
#  define ALWAYS_INLINE
#  define HIDDEN
#  define PROTECTED
# endif
# if (__GNUC__ >= 3)
#  define likely(x)	__builtin_expect ((x), 1)
#  define unlikely(x)	__builtin_expect ((x), 0)
# else
#  define likely(x)	(x)
#  define unlikely(x)	(x)
# endif
#else
# define ALWAYS_INLINE
# define UNUSED
# define NORETURN
# define ALIAS(name)
# define HIDDEN
# define PROTECTED
# define likely(x)	(x)
# define unlikely(x)	(x)
#endif

#ifdef DEBUG
# define UNW_DEBUG	1
#else
# define UNW_DEBUG	0
#endif

#define NELEMS(a)	(sizeof (a) / sizeof ((a)[0]))

/* Make it easy to write thread-safe code which may or may not be
   linked against libpthread.  The macros below can be used
   unconditionally and if -lpthread is around, they'll call the
   corresponding routines otherwise, they do nothing.  */

#pragma weak pthread_mutex_init
#pragma weak pthread_mutex_lock
#pragma weak pthread_mutex_unlock

#define mutex_init(l)							\
	(pthread_mutex_init != 0 ? pthread_mutex_init ((l), 0) : 0)
#define mutex_lock(l)							\
	(pthread_mutex_lock != 0 ? pthread_mutex_lock (l) : 0)
#define mutex_unlock(l)							\
	(pthread_mutex_unlock != 0 ? pthread_mutex_unlock (l) : 0)

#ifdef HAVE_ATOMIC_OPS_H
# include <atomic_ops.h>
static inline int
cmpxchg_ptr (void *addr, void *old, void *new)
{
  union
    {
      void *vp;
      AO_t *aop;
    }
  u;

  u.vp = addr;
  return AO_compare_and_swap(u.aop, (AO_t) old, (AO_t) new);
}
# define fetch_and_add1(_ptr)		AO_fetch_and_add1(_ptr)
   /* GCC 3.2.0 on HP-UX crashes on cmpxchg_ptr() */
#  if !(defined(__hpux) && __GNUC__ == 3 && __GNUC_MINOR__ == 2)
#   define HAVE_CMPXCHG
#  endif
# define HAVE_FETCH_AND_ADD1
#else
# ifdef HAVE_IA64INTRIN_H
#  include <ia64intrin.h>
static inline int
cmpxchg_ptr (void *addr, void *old, void *new)
{
  union
    {
      void *vp;
      long *vlp;
    }
  u;

  u.vp = addr;
  return __sync_bool_compare_and_swap(u.vlp, (long) old, (long) new);
}
#  define fetch_and_add1(_ptr)		__sync_fetch_and_add(_ptr, 1)
#  define HAVE_CMPXCHG
#  define HAVE_FETCH_AND_ADD1
# endif
#endif

#define UNWI_OBJ(fn)	  UNW_PASTE(UNW_PREFIX,UNW_PASTE(I,fn))
#define UNWI_ARCH_OBJ(fn) UNW_PASTE(UNW_PASTE(UNW_PASTE(_UI,UNW_TARGET),_), fn)

#define unwi_full_sigmask	UNWI_ARCH_OBJ(full_sigmask)

extern sigset_t unwi_full_sigmask;

#define unwi_find_dynamic_proc_info	UNWI_OBJ(find_dynamic_proc_info)
#define unwi_extract_dynamic_proc_info	UNWI_OBJ(extract_dynamic_proc_info)
#define unwi_put_dynamic_unwind_info	UNWI_OBJ(put_dynamic_unwind_info)
#define unwi_dyn_remote_find_proc_info	UNWI_OBJ(dyn_remote_find_proc_info)
#define unwi_dyn_remote_put_unwind_info	UNWI_OBJ(dyn_remote_put_unwind_info)
#define unwi_dyn_validate_cache		UNWI_OBJ(dyn_validate_cache)

extern int unwi_find_dynamic_proc_info (unw_addr_space_t as,
					unw_word_t ip,
					unw_proc_info_t *pi,
					int need_unwind_info, void *arg);
extern int unwi_extract_dynamic_proc_info (unw_addr_space_t as,
					   unw_word_t ip,
					   unw_proc_info_t *pi,
					   unw_dyn_info_t *di,
					   int need_unwind_info,
					   void *arg);
extern void unwi_put_dynamic_unwind_info (unw_addr_space_t as,
					  unw_proc_info_t *pi, void *arg);

/* These handle the remote (cross-address-space) case of accessing
   dynamic unwind info. */

extern int unwi_dyn_remote_find_proc_info (unw_addr_space_t as,
					   unw_word_t ip,
					   unw_proc_info_t *pi,
					   int need_unwind_info,
					   void *arg);
extern void unwi_dyn_remote_put_unwind_info (unw_addr_space_t as,
					     unw_proc_info_t *pi,
					     void *arg);
extern int unwi_dyn_validate_cache (unw_addr_space_t as, void *arg);

extern unw_dyn_info_list_t _U_dyn_info_list;
extern pthread_mutex_t _U_dyn_info_list_lock;

#if UNW_DEBUG
#define unwi_debug_level		UNWI_ARCH_OBJ(debug_level)
extern long unwi_debug_level;

# include <stdio.h>
# define Debug(level,format...)						\
do {									\
  if (unwi_debug_level > level)						\
    {									\
      int _n = level;							\
      if (_n > 16)							\
	_n = 16;							\
      fprintf (stderr, "%*c>%s: ", _n, ' ', __FUNCTION__);		\
      fprintf (stderr, format);						\
    }									\
} while (0)
# define dprintf(format...) 	    fprintf (stderr, format)
# ifdef __GNUC__
#  undef inline
#  define inline	UNUSED
# endif
#else
# define Debug(level,format...)
# define dprintf(format...)
#endif

static inline ALWAYS_INLINE void
print_error (const char *string)
{
  write (2, string, strlen (string));
}

#define mi_init		UNWI_ARCH_OBJ(mi_init)

extern void mi_init (void);	/* machine-independent initializations */
extern unw_word_t _U_dyn_info_list_addr (void);

/* This is needed/used by ELF targets only.  */

struct elf_image
  {
    void *image;		/* pointer to mmap'd image */
    size_t size;		/* (file-) size of the image */
  };

#endif /* internal_h */
