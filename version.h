#define RUBY_VERSION "1.8.6"
#define RUBY_RELEASE_DATE "2009-3-1"
#define RUBY_VERSION_CODE 186
#define RUBY_RELEASE_CODE 20090301
#define RUBY_PATCHLEVEL 287

#define RUBY_VERSION_MAJOR 1
#define RUBY_VERSION_MINOR 8
#define RUBY_VERSION_TEENY 6
#define RUBY_RELEASE_YEAR 2009
#define RUBY_RELEASE_MONTH 3
#define RUBY_RELEASE_DAY 1

#ifdef RUBY_EXTERN
RUBY_EXTERN const char ruby_version[];
RUBY_EXTERN const char ruby_release_date[];
RUBY_EXTERN const char ruby_platform[];
RUBY_EXTERN const int ruby_patchlevel;
RUBY_EXTERN const char *ruby_description;
RUBY_EXTERN const char *ruby_copyright;
#endif

#define RUBY_AUTHOR "Yukihiro Matsumoto"
#define RUBY_BIRTH_YEAR 1993
#define RUBY_BIRTH_MONTH 2
#define RUBY_BIRTH_DAY 24

#include "rubysig.h"

#define string_arg(s) #s

#ifdef MBARI_API
#define _mbari_rev_ "MBARI"
#else
#define _mbari_rev_ "mbari"
#endif

#define MBARI_RELEASE(wipe_sites) _mbari_rev_ " 8B/" string_arg(wipe_sites)

#define RUBY_RELEASE_STR MBARI_RELEASE(STACK_WIPE_SITES) " on patchlevel"
#define RUBY_RELEASE_NUM RUBY_PATCHLEVEL


