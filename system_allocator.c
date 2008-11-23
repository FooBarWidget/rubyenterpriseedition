/*
 * The problem
 * -----------
 * On platforms that use a two-level symbol namespace for dynamic libraries
 * (most notably MacOS X), integrating tcmalloc requires special modifications.
 *
 * Most Unix platforms use a flat namespace for symbol lookup, which is why
 * linking to tcmalloc causes it override malloc() and free() for the entire
 * process. This is not the case on OS X: if Ruby calls a function from library
 * that's not compiled with -flat_namespace, then that library will use the
 * system's memory allocator instead of tcmalloc.
 *
 * The Ruby readline extension is a good example of how things can go wrong.
 * The readline extension calls the readline() function in the readline library.
 * This library is not compiled with -flat_namespace; readline() returns a string
 * that's allocated by the system memory allocator. The Ruby readline extension
 * then frees this string by passing it to tcmalloc's free() function. This
 * results in a crash.
 * Note that setting DYLD_FORCE_FLAT_NAMESPACE on OS X does not work: the
 * resulting Ruby interpreter will crash immediately.
 *
 *
 * The solution
 * ------------
 * This can be fixed by making it possible for Ruby extensions to call the
 * system's memory allocator functions, instead of tcmalloc's, if it knows
 * that a piece of memory is allocated by the system's memory allocator.
 *
 * This library, libsystem_allocator provides wrapper functions for the system
 * memory allocator. libsystem_allocator will be compiled without -flat_namespace
 * on OS X, and so it will always use the system's memory allocator instead of
 * tcmalloc.
 *
 * libsystem_allocator will not be compiled on systems that only support flat
 * namespaces (e.g. Linux). On those platforms, system_malloc() and
 * system_free() have no special effect.
 */
#include <stdlib.h>

void *
system_malloc(long size)
{
    return malloc(size);
}

void
system_free(void *ptr)
{
    free(ptr);
}
