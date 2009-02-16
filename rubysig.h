/**********************************************************************

  rubysig.h -

  $Author$
  $Date$
  created at: Wed Aug 16 01:15:38 JST 1995

  Copyright (C) 1993-2003 Yukihiro Matsumoto

**********************************************************************/

#ifndef SIG_H
#define SIG_H

#include <errno.h>

/* STACK_WIPE_SITES determines where attempts are made to exorcise
   "ghost object refereces" from the stack and how the stack is cleared:
   
   0x*001 -->  wipe stack just after every thread_switch
   0x*002 -->  wipe stack just after every EXEC_TAG()
   0x*004 -->  wipe stack in CHECK_INTS
   0x*010 -->  wipe stack in while & until loops
   0x*020 -->  wipe stack before yield() in iterators and outside eval.c
   0x*040 -->  wipe stack on catch and thread save context
   0x*100 -->  update stack extent on each object allocation
   0x*200 -->  update stack extent on each object reallocation
   0x*400 -->  update stack extent during GC marking passes
   0x*800 -->  update stack extent on each throw (use with 0x040)
   0x1000 -->  use inline assembly code for x86, PowerPC, or ARM CPUs

   0x0*** -->  do not even call rb_wipe_stack()
   0x2*** -->  call dummy rb_wipe_stack() (for debugging and profiling)
   0x4*** -->  safe, portable stack clearing in memory allocated with alloca
   0x6*** -->  use faster, but less safe stack clearing in unallocated stack
   0x8*** -->  use faster, but less safe stack clearing (with inline code)
   
   for most effective gc use 0x*707
   for fastest micro-benchmarking use 0x0000
   0x*770 prevents almost all memory leaks caused by ghost references
   without adding much overhead for stack clearing.
   Other good trade offs are 0x*270, 0x*703, 0x*303 or even 0x*03
   
   In general, you may lessen the default -mpreferred-stack-boundary
   only if using less safe stack clearing (0x6***).  Lessening the
   stack alignment with portable stack clearing (0x4***) may fail to clear 
   all ghost references off the stack.
   
   When using 0x6*** or 0x8***, the compiler could insert 
   stack push(s) between reading the stack pointer and clearing 
   the ghost references.  The register(s) pushed will be
   cleared by the rb_gc_stack_wipe(), typically resulting in a segfault
   or an interpreter hang.
   
   STACK_WIPE_SITES of 0x8770 works well compiled with gcc on most machines
   using the recommended CFLAGS="-O2 -fno-stack-protector".  However...
   If it hangs or crashes for you, try changing STACK_WIPE_SITES to 0x4770
   and please report your details.  i.e. CFLAGS, compiler, version, CPU
   
   Note that it is redundant to wipe_stack in looping constructs if 
   also doing so in CHECK_INTS.  It is also redundant to wipe_stack on
   each thread_switch if wiping after every thread save context.
*/
#ifndef STACK_WIPE_SITES
# ifdef __x86_64__     /* deal with "red zone" by not inlining stack clearing */
#  define STACK_WIPE_SITES  0x6770
# elif defined __ppc__ || defined __ppc64__   /* On any PowerPC, deal with... */
#  define STACK_WIPE_SITES  0x7764   /* red zone & alloc(0) doesn't return sp */
# else
#  define STACK_WIPE_SITES  0x8770 /*normal case, use 0x4770 if problems arise*/
# endif
#endif

#if (STACK_WIPE_SITES & 0x14) == 0x14
#warning  wiping stack in CHECK_INTS makes wiping in loops redundant
#endif
#if (STACK_WIPE_SITES & 0x41) == 0x41
#warning  wiping stack after thread save makes wiping on thread_switch redundant
#endif

#define STACK_WIPE_METHOD (STACK_WIPE_SITES>>13)

#ifdef _WIN32
typedef LONG rb_atomic_t;

# define ATOMIC_TEST(var) InterlockedExchange(&(var), 0)
# define ATOMIC_SET(var, val) InterlockedExchange(&(var), (val))
# define ATOMIC_INC(var) InterlockedIncrement(&(var))
# define ATOMIC_DEC(var) InterlockedDecrement(&(var))

/* Windows doesn't allow interrupt while system calls */
# define TRAP_BEG do {\
    int saved_errno = 0;\
    rb_atomic_t trap_immediate = ATOMIC_SET(rb_trap_immediate, 1)
# define TRAP_END\
    ATOMIC_SET(rb_trap_immediate, trap_immediate);\
    saved_errno = errno;\
    CHECK_INTS;\
    errno = saved_errno;\
} while (0)
# define RUBY_CRITICAL(statements) do {\
    rb_w32_enter_critical();\
    statements;\
    rb_w32_leave_critical();\
} while (0)
#else
typedef int rb_atomic_t;

# define ATOMIC_TEST(var) ((var) ? ((var) = 0, 1) : 0)
# define ATOMIC_SET(var, val) ((var) = (val))
# define ATOMIC_INC(var) (++(var))
# define ATOMIC_DEC(var) (--(var))

# define TRAP_BEG do {\
    int saved_errno = 0;\
    int trap_immediate = rb_trap_immediate;\
    rb_trap_immediate = 1
# define TRAP_END rb_trap_immediate = trap_immediate;\
    saved_errno = errno;\
    CHECK_INTS;\
    errno = saved_errno;\
} while (0)

# define RUBY_CRITICAL(statements) do {\
    int trap_immediate = rb_trap_immediate;\
    rb_trap_immediate = 0;\
    statements;\
    rb_trap_immediate = trap_immediate;\
} while (0)
#endif
RUBY_EXTERN rb_atomic_t rb_trap_immediate;

RUBY_EXTERN int rb_prohibit_interrupt;
#define DEFER_INTS (rb_prohibit_interrupt++)
#define ALLOW_INTS do {\
    rb_prohibit_interrupt--;\
    CHECK_INTS;\
} while (0)
#define ENABLE_INTS (rb_prohibit_interrupt--)

VALUE rb_with_disable_interrupt _((VALUE(*)(ANYARGS),VALUE));

RUBY_EXTERN rb_atomic_t rb_trap_pending;
void rb_trap_restore_mask _((void));

RUBY_EXTERN int rb_thread_critical;
void rb_thread_schedule _((void));

RUBY_EXTERN VALUE *rb_gc_stack_end;
RUBY_EXTERN int rb_gc_stack_grow_direction;  /* -1 for down or 1 for up */

#if STACK_GROW_DIRECTION > 0

/* clear stack space between end and sp (not including *sp) */
#define __stack_zero(end,sp)  __stack_zero_up(end,sp)

/* true if top has grown past limit, i.e. top deeper than limit */
#define __stack_past(limit,top)  __stack_past_up(limit,top)

/* depth of mid below stack top */
#define __stack_depth(top,mid)   __stack_depth_up(top,mid)

/* stack pointer top adjusted to include depth more items */
#define __stack_grow(top,depth)  __stack_grow_up(top,depth)


#elif STACK_GROW_DIRECTION < 0
#define __stack_zero(end,sp)  __stack_zero_down(end,sp)
#define __stack_past(limit,top)  __stack_past_down(limit,top)
#define __stack_depth(top,mid)   __stack_depth_down(top,mid)
#define __stack_grow(top,depth)  __stack_grow_down(top,depth)

#else  /* limp along if stack direction can't be determined at compile time */
#define __stack_zero(end,sp) if (rb_gc_stack_grow_direction<0) \
        __stack_zero_down(end,sp); else __stack_zero_up(end,sp);
#define __stack_past(limit,top)  (rb_gc_stack_grow_direction<0 ? \
                      __stack_past_down(limit,top) : __stack_past_up(limit,top))
#define __stack_depth(top,mid) (rb_gc_stack_grow_direction<0 ? \
                       __stack_depth_down(top,mid) : __stack_depth_up(top,mid))
#define __stack_grow(top,depth) (rb_gc_stack_grow_direction<0 ? \
                      __stack_grow_down(top,depth) : __stack_grow_up(top,depth))
#endif
 
#define __stack_zero_up(end,sp)  while (end >= ++sp) *sp=0
#define __stack_past_up(limit,top)  ((limit) < (top))
#define __stack_depth_up(top,mid) ((top) - (mid))
#define __stack_grow_up(top,depth) ((top)+(depth))

#define __stack_zero_down(end,sp)  while (end <= --sp) *sp=0
#define __stack_past_down(limit,top)  ((limit) > (top))
#define __stack_depth_down(top,mid) ((mid) - (top))
#define __stack_grow_down(top,depth) ((top)-(depth))

/* Make alloca work the best possible way.  */
#ifdef __GNUC__
# ifndef atarist
#  ifndef alloca
#   define alloca __builtin_alloca
#  endif
# endif /* atarist */

# define nativeAllocA __builtin_alloca

/* use assembly to get stack pointer quickly */
# if STACK_WIPE_SITES & 0x1000
#  define __defspfn(asmb)  \
static inline VALUE *__sp(void) __attribute__((always_inline)); \
static inline VALUE *__sp(void) \
{ \
  VALUE *sp; asm(asmb); \
  return sp; \
}
#  if defined __ppc__ || defined __ppc64__
__defspfn("addi %0, r1, 0": "=r"(sp))
#  elif defined  __i386__
__defspfn("movl %%esp, %0": "=r"(sp))
#  elif defined __x86_64__
__defspfn("movq %%rsp, %0": "=r"(sp))
#  elif __arm__
__defspfn("mov %0, sp": "=r"(sp))
#  else
#   define __sp()  ((VALUE *)__builtin_alloca(0))
#   warning No assembly version of __sp() defined for this CPU.
#  endif
# else
#  define __sp()  ((VALUE *)__builtin_alloca(0))
# endif

#else  // not GNUC

# ifdef HAVE_ALLOCA_H
#  include <alloca.h>
# else
#  ifndef _AIX
#   ifndef alloca /* predefined by HP cc +Olibcalls */
void *alloca ();
#   endif
#  endif /* AIX */
# endif /* HAVE_ALLOCA_H */

# if STACK_WIPE_SITES & 0x1000
#  warning No assembly versions of __sp() defined for this compiler.
# endif
# if HAVE_ALLOCA
#  define __sp()  ((VALUE *)alloca(0))
#  define nativeAllocA alloca
# else
RUBY_EXTERN VALUE *__sp(void);
#  if STACK_WIPE_SITES
#   define STACK_WIPE_SITES 0
#   warning Disabled Stack Wiping because there is no native alloca()
#  endif
# endif
#endif /* __GNUC__ */


/*
  Zero memory that was (recently) part of the stack, but is no longer.
  Invoke when stack is deep to mark its extent and when it's shallow to wipe it.
*/
#if STACK_WIPE_METHOD == 0
#define rb_gc_wipe_stack() ((void)0)
#elif STACK_WIPE_METHOD == 4
#define rb_gc_wipe_stack() {     \
  VALUE *end = rb_gc_stack_end;  \
  VALUE *sp = __sp();            \
  rb_gc_stack_end = sp;          \
  __stack_zero(end, sp);   \
}
#else
RUBY_EXTERN void rb_gc_wipe_stack(void);
#endif

/*
  Update our record of maximum stack extent without zeroing unused stack
*/
#define rb_gc_update_stack_extent() do { \
    VALUE *sp = __sp(); \
    if __stack_past(rb_gc_stack_end, sp) rb_gc_stack_end = sp; \
} while(0)


#if STACK_WIPE_SITES & 4
# define CHECK_INTS_wipe_stack()  rb_gc_wipe_stack()
#else
# define CHECK_INTS_wipe_stack()  (void)0
#endif

#if defined(HAVE_SETITIMER) || defined(_THREAD_SAFE)
RUBY_EXTERN int rb_thread_pending;
# define CHECK_INTS do {\
    CHECK_INTS_wipe_stack(); \
    if (!(rb_prohibit_interrupt || rb_thread_critical)) {\
        if (rb_thread_pending) rb_thread_schedule();\
	if (rb_trap_pending) rb_trap_exec();\
    }\
} while (0)
#else
/* pseudo preemptive thread switching */
RUBY_EXTERN int rb_thread_tick;
#define THREAD_TICK 500
#define CHECK_INTS do {\
    CHECK_INTS_wipe_stack(); \
    if (!(rb_prohibit_interrupt || rb_thread_critical)) {\
	if (rb_thread_tick-- <= 0) {\
	    rb_thread_tick = THREAD_TICK;\
            rb_thread_schedule();\
	}\
        if (rb_trap_pending) rb_trap_exec();\
    }\
} while (0)
#endif

#endif
