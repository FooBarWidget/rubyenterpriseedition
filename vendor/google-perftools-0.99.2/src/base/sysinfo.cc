// Copyright (c) 2006, Google Inc.
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// 
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// ---
// Author: Mike Burrows

#include "config.h"
#include <stdlib.h>   // for getenv()
#include <stdio.h>    // for snprintf(), sscanf()
#include <string.h>   // for memmove(), memchr(), etc.
#include <fcntl.h>    // for open()
#include <errno.h>    // for errno
#ifdef HAVE_UNISTD_H
#include <unistd.h>   // for read()
#endif
#if defined __MACH__          // Mac OS X, almost certainly
#include <mach-o/dyld.h>      // for iterating over dll's in ProcMapsIter
#include <mach-o/loader.h>    // for iterating over dll's in ProcMapsIter
#include <sys/types.h>
#include <sys/sysctl.h>       // how we figure out numcpu's on OS X
#elif defined __FreeBSD__
#include <sys/sysctl.h>
#elif defined __sun__         // Solaris
#include <procfs.h>           // for, e.g., prmap_t
#elif defined WIN32           // Windows
#include <process.h>          // for getpid() (actually, _getpid())
#include <shlwapi.h>          // for SHGetValueA()
#include <tlhelp32.h>         // for Module32First()
#endif
#include "base/sysinfo.h"
#include "base/commandlineflags.h"
#include "base/logging.h"
#include "base/cycleclock.h"

#ifdef WIN32
#ifdef MODULEENTRY32
// In a change from the usual W-A pattern, there is no A variant of
// MODULEENTRY32.  Tlhelp32.h #defines the W variant, but not the A.
// In unicode mode, tlhelp32.h #defines MODULEENTRY32 to be
// MODULEENTRY32W.  These #undefs are the only way I see to get back
// access to the original, ascii struct (and related functions).
#undef MODULEENTRY32
#undef Module32First
#undef Module32Next
#undef PMODULEENTRY32
#undef LPMODULEENTRY32
#endif  /* MODULEENTRY32 */
// MinGW doesn't seem to define this, perhaps some windowsen don't either.
#ifndef TH32CS_SNAPMODULE32
#define TH32CS_SNAPMODULE32  0
#endif  /* TH32CS_SNAPMODULE32 */
#endif  /* WIN32 */

// Re-run fn until it doesn't cause EINTR.
#define NO_INTR(fn)  do {} while ((fn) < 0 && errno == EINTR)

// open/read/close can set errno, which may be illegal at this
// time, so prefer making the syscalls directly if we can.
#ifdef HAVE_SYS_SYSCALL_H
# include <sys/syscall.h>
# define safeopen(filename, mode)  syscall(SYS_open, filename, mode)
# define saferead(fd, buffer, size)  syscall(SYS_read, fd, buffer, size)
# define safeclose(fd)  syscall(SYS_close, fd)
#else
# define safeopen(filename, mode)  open(filename, mode)
# define saferead(fd, buffer, size)  read(fd, buffer, size)
# define safeclose(fd)  close(fd)
#endif

// ----------------------------------------------------------------------
// GetenvBeforeMain()
// GetUniquePathFromEnv()
//    Some non-trivial getenv-related functions.
// ----------------------------------------------------------------------

const char* GetenvBeforeMain(const char* name) {
  // static is ok because this function should only be called before
  // main(), when we're single-threaded.
  static char envbuf[16<<10];
  if (*envbuf == '\0') {    // haven't read the environ yet
    int fd = safeopen("/proc/self/environ", O_RDONLY);
    // The -2 below guarantees the last two bytes of the buffer will be \0\0
    if (fd == -1 ||           // unable to open the file, fall back onto libc
        saferead(fd, envbuf, sizeof(envbuf) - 2) < 0) { // error reading file
      RAW_VLOG(1, "Unable to open /proc/self/environ, falling back "
               "on getenv(\"%s\"), which may not work", name);
      if (fd != -1) safeclose(fd);
      return getenv(name);
    }
    safeclose(fd);
  }
  const int namelen = strlen(name);
  const char* p = envbuf;
  while (*p != '\0') {    // will happen at the \0\0 that terminates the buffer
    // proc file has the format NAME=value\0NAME=value\0NAME=value\0...
    const char* endp = (char*)memchr(p, '\0', sizeof(envbuf) - (p - envbuf));
    if (endp == NULL)            // this entry isn't NUL terminated
      return NULL;
    else if (!memcmp(p, name, namelen) && p[namelen] == '=')    // it's a match
      return p + namelen+1;      // point after =
    p = endp + 1;
  }
  return NULL;                   // env var never found
}

// This takes as an argument an environment-variable name (like
// CPUPROFILE) whose value is supposed to be a file-path, and sets
// path to that path, and returns true.  If the env var doesn't exist,
// or is the empty string, leave path unchanged and returns false.
// The reason this is non-trivial is that this function handles munged
// pathnames.  Here's why:
//
// If we're a child process of the 'main' process, we can't just use
// getenv("CPUPROFILE") -- the parent process will be using that path.
// Instead we append our pid to the pathname.  How do we tell if we're a
// child process?  Ideally we'd set an environment variable that all
// our children would inherit.  But -- and this is seemingly a bug in
// gcc -- if you do a setenv() in a shared libarary in a global
// constructor, the environment setting is lost by the time main() is
// called.  The only safe thing we can do in such a situation is to
// modify the existing envvar.  So we do a hack: in the parent, we set
// the high bit of the 1st char of CPUPROFILE.  In the child, we
// notice the high bit is set and append the pid().  This works
// assuming cpuprofile filenames don't normally have the high bit set
// in their first character!  If that assumption is violated, we'll
// still get a profile, but one with an unexpected name.
// TODO(csilvers): set an envvar instead when we can do it reliably.
bool GetUniquePathFromEnv(const char* env_name, char* path) {
  char* envval = getenv(env_name);
  if (envval == NULL || *envval == '\0')
    return false;
  if (envval[0] & 128) {                  // high bit is set
    snprintf(path, PATH_MAX, "%c%s_%u",   // add pid and clear high bit
             envval[0] & 127, envval+1, (unsigned int)(getpid()));
  } else {
    snprintf(path, PATH_MAX, "%s", envval);
    envval[0] |= 128;                     // set high bit for kids to see
  }
  return true;
}

// ----------------------------------------------------------------------
// CyclesPerSecond()
// NumCPUs()
//    It's important this not call malloc! -- they may be called at
//    global-construct time, before we've set up all our proper malloc
//    hooks and such.
// ----------------------------------------------------------------------

static double cpuinfo_cycles_per_second = 1.0;  // 0.0 might be dangerous
static int cpuinfo_num_cpus = 1;  // Conservative guess

static void SleepForMilliseconds(int milliseconds) {
#ifdef WIN32
  _sleep(milliseconds);   // Windows's _sleep takes milliseconds argument
#else
  // Sleep for a few milliseconds
  struct timespec sleep_time;
  sleep_time.tv_sec = milliseconds / 1000;
  sleep_time.tv_nsec = (milliseconds % 1000) * 1000000;
  while (nanosleep(&sleep_time, &sleep_time) != 0 && errno == EINTR)
    ;  // Ignore signals and wait for the full interval to elapse.
#endif
}

// Helper function estimates cycles/sec by observing cycles elapsed during
// sleep(). Using small sleep time decreases accuracy significantly.
static int64 EstimateCyclesPerSecond(const int estimate_time_ms) {
  assert(estimate_time_ms > 0);
  if (estimate_time_ms <= 0)
    return 1;
  double multiplier = 1000.0 / (double)estimate_time_ms;  // scale by this much

  const int64 start_ticks = CycleClock::Now();
  SleepForMilliseconds(estimate_time_ms);
  const int64 guess = int64(multiplier * (CycleClock::Now() - start_ticks));
  return guess;
}

// WARNING: logging calls back to InitializeSystemInfo() so it must
// not invoke any logging code.  Also, InitializeSystemInfo() can be
// called before main() -- in fact it *must* be since already_called
// isn't protected -- before malloc hooks are properly set up, so
// we make an effort not to call any routines which might allocate
// memory.

static void InitializeSystemInfo() {
  static bool already_called = false;   // safe if we run before threads
  if (already_called)  return;
  already_called = true;

  // I put in a never-called reference to EstimateCyclesPerSecond() here
  // to silence the compiler for OS's that don't need it
  if (0) EstimateCyclesPerSecond(0);

#if defined __linux__
  char line[1024];
  char* err;

  // If CPU scaling is in effect, we want to use the *maximum* frequency,
  // not whatever CPU speed some random processor happens to be using now.
  bool saw_mhz = false;
  const char* pname0 = "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq";
  int fd0 = open(pname0, O_RDONLY);
  if (fd0 != -1) {
    memset(line, '\0', sizeof(line));
    read(fd0, line, sizeof(line));
    const int max_freq = strtol(line, &err, 10);
    if (line[0] != '\0' && (*err == '\n' || *err == '\0')) {
      // The value is in kHz.  For example, on a 2GHz machine, the file
      // contains the value "2000000".  Historically this file contained no
      // newline, but at some point the kernel started appending a newline.
      cpuinfo_cycles_per_second = max_freq * 1000.0;
      saw_mhz = true;
    }
    close(fd0);
  }

  // Read /proc/cpuinfo for other values, and if there is no cpuinfo_max_freq.
  const char* pname = "/proc/cpuinfo";
  int fd = open(pname, O_RDONLY);
  if (fd == -1) {
    perror(pname);
    cpuinfo_cycles_per_second = EstimateCyclesPerSecond(1000);
    return;          // TODO: use generic tester instead?
  }

  double bogo_clock = 1.0;
  int num_cpus = 0;
  line[0] = line[1] = '\0';
  int chars_read = 0;
  do {   // we'll exit when the last read didn't read anything
    // Move the next line to the beginning of the buffer
    const int oldlinelen = strlen(line);
    if (sizeof(line) == oldlinelen + 1)    // oldlinelen took up entire line
      line[0] = '\0';
    else                                   // still other lines left to save
      memmove(line, line + oldlinelen+1, sizeof(line) - (oldlinelen+1));
    // Terminate the new line, reading more if we can't find the newline
    char* newline = strchr(line, '\n');
    if (newline == NULL) {
      const int linelen = strlen(line);
      const int bytes_to_read = sizeof(line)-1 - linelen;
      assert(bytes_to_read > 0);  // because the memmove recovered >=1 bytes
      chars_read = read(fd, line + linelen, bytes_to_read);
      line[linelen + chars_read] = '\0';
      newline = strchr(line, '\n');
    }
    if (newline != NULL)
      *newline = '\0';

    if (!saw_mhz && strncmp(line, "cpu MHz", sizeof("cpu MHz")-1) == 0) {
      const char* freqstr = strchr(line, ':');
      if (freqstr) {
        cpuinfo_cycles_per_second = strtod(freqstr+1, &err) * 1000000.0;
        if (freqstr[1] != '\0' && *err == '\0')
          saw_mhz = true;
      }
    } else if (strncmp(line, "bogomips", sizeof("bogomips")-1) == 0) {
      const char* freqstr = strchr(line, ':');
      if (freqstr)
        bogo_clock = strtod(freqstr+1, &err) * 1000000.0;
      if (freqstr == NULL || freqstr[1] == '\0' || *err != '\0')
        bogo_clock = 1.0;
    } else if (strncmp(line, "processor", sizeof("processor")-1) == 0) {
      num_cpus++;  // count up every time we see an "processor :" entry
    }
  } while (chars_read > 0);
  close(fd);

  if (!saw_mhz) {
    // If we didn't find anything better, we'll use bogomips, but
    // we're not happy about it.
    cpuinfo_cycles_per_second = bogo_clock;
  }
  if (cpuinfo_cycles_per_second == 0.0) {
    cpuinfo_cycles_per_second = 1.0;   // maybe unnecessary, but safe
  }
  if (num_cpus > 0) {
    cpuinfo_num_cpus = num_cpus;
  }

#elif defined __FreeBSD__
  // For this sysctl to work, the machine must be configured without
  // SMP, APIC, or APM support.  hz should be 64-bit in freebsd 7.0
  // and later.  Before that, it's a 32-bit quantity (and gives the
  // wrong answer on machines faster than 2^32 Hz).  See
  //  http://lists.freebsd.org/pipermail/freebsd-i386/2004-November/001846.html
  // But also compare FreeBSD 7.0:
  //  http://fxr.watson.org/fxr/source/i386/i386/tsc.c?v=RELENG70#L223
  //  231         error = sysctl_handle_quad(oidp, &freq, 0, req);
  // To FreeBSD 6.3 (it's the same in 6-STABLE):
  //  http://fxr.watson.org/fxr/source/i386/i386/tsc.c?v=RELENG6#L131
  //  139         error = sysctl_handle_int(oidp, &freq, sizeof(freq), req);
#if __FreeBSD__ >= 7
  uint64_t hz = 0;
#else
  unsigned int hz = 0;
#endif
  size_t sz = sizeof(hz);
  const char *sysctl_path = "machdep.tsc_freq";
  if ( sysctlbyname(sysctl_path, &hz, &sz, NULL, 0) != 0 ) {
    fprintf(stderr, "Unable to determine clock rate from sysctl: %s: %s\n",
            sysctl_path, strerror(errno));
    cpuinfo_cycles_per_second = EstimateCyclesPerSecond(1000);
  } else {
    cpuinfo_cycles_per_second = hz;
  }
  // TODO(csilvers): also figure out cpuinfo_num_cpus

#elif defined WIN32
# pragma comment(lib, "shlwapi.lib")  // for SHGetValue()
  // In NT, read MHz from the registry. If we fail to do so or we're in win9x
  // then make a crude estimate.
  OSVERSIONINFO os;
  os.dwOSVersionInfoSize = sizeof(os);
  DWORD data, data_size = sizeof(data);
  if (GetVersionEx(&os) &&
      os.dwPlatformId == VER_PLATFORM_WIN32_NT &&
      SUCCEEDED(SHGetValueA(HKEY_LOCAL_MACHINE,
                         "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                           "~MHz", NULL, &data, &data_size)))
    cpuinfo_cycles_per_second = (int64)data * (int64)(1000 * 1000); // was mhz
  else
    cpuinfo_cycles_per_second = EstimateCyclesPerSecond(500); // TODO <500?
  // TODO(csilvers): also figure out cpuinfo_num_cpus

#elif defined(__MACH__) && defined(__APPLE__)
  // returning "mach time units" per second. the current number of elapsed
  // mach time units can be found by calling uint64 mach_absolute_time();
  // while not as precise as actual CPU cycles, it is accurate in the face
  // of CPU frequency scaling and multi-cpu/core machines.
  // Our mac users have these types of machines, and accuracy
  // (i.e. correctness) trumps precision.
  // See cycleclock.h: CycleClock::Now(), which returns number of mach time
  // units on Mac OS X.
  mach_timebase_info_data_t timebase_info;
  mach_timebase_info(&timebase_info);
  double mach_time_units_per_nanosecond =
      static_cast<double>(timebase_info.denom) /
      static_cast<double>(timebase_info.numer);
  cpuinfo_cycles_per_second = mach_time_units_per_nanosecond * 1e9;

  int num_cpus = 0;
  size_t size = sizeof(num_cpus);
  int numcpus_name[] = { CTL_HW, HW_NCPU };
  if (::sysctl(numcpus_name, arraysize(numcpus_name), &num_cpus, &size, 0, 0)
      == 0
      && (size == sizeof(num_cpus)))
    cpuinfo_num_cpus = num_cpus;

#else
  // Generic cycles per second counter
  cpuinfo_cycles_per_second = EstimateCyclesPerSecond(1000);
#endif
}

double CyclesPerSecond(void) {
  InitializeSystemInfo();
  return cpuinfo_cycles_per_second;
}

int NumCPUs(void) {
  InitializeSystemInfo();
  return cpuinfo_num_cpus;
}

// ----------------------------------------------------------------------

#if defined __linux__ || defined __FreeBSD__ || defined __sun__
static void ConstructFilename(const char* spec, pid_t pid,
                              char* buf, int buf_size) {
  CHECK_LT(snprintf(buf, buf_size,
                    spec,
                    pid ? pid : getpid()), buf_size);
}
#endif

ProcMapsIterator::ProcMapsIterator(pid_t pid) {
  Init(pid, NULL, false);
}

ProcMapsIterator::ProcMapsIterator(pid_t pid, Buffer *buffer) {
  Init(pid, buffer, false);
}

ProcMapsIterator::ProcMapsIterator(pid_t pid, Buffer *buffer,
                                   bool use_maps_backing) {
  Init(pid, buffer, use_maps_backing);
}

void ProcMapsIterator::Init(pid_t pid, Buffer *buffer,
                            bool use_maps_backing) {
  using_maps_backing_ = use_maps_backing;
  dynamic_buffer_ = NULL;
  if (!buffer) {
    // If the user didn't pass in any buffer storage, allocate it
    // now. This is the normal case; the signal handler passes in a
    // static buffer.
    buffer = dynamic_buffer_ = new Buffer;
  } else {
    dynamic_buffer_ = NULL;
  }

  ibuf_ = buffer->buf_;

  stext_ = etext_ = nextline_ = ibuf_;
  ebuf_ = ibuf_ + Buffer::kBufSize - 1;
  nextline_ = ibuf_;

#if defined(__linux__)
  if (use_maps_backing) {  // don't bother with clever "self" stuff in this case
    ConstructFilename("/proc/%d/maps_backing", pid, ibuf_, Buffer::kBufSize);
  } else if (pid == 0) {
    // We have to kludge a bit to deal with the args ConstructFilename
    // expects.  The 1 is never used -- it's only impt. that it's not 0.
    ConstructFilename("/proc/self/maps", 1, ibuf_, Buffer::kBufSize);
  } else {
    ConstructFilename("/proc/%d/maps", pid, ibuf_, Buffer::kBufSize);
  }
  // No error logging since this can be called from the crash dump
  // handler at awkward moments. Users should call Valid() before
  // using.
  NO_INTR(fd_ = open(ibuf_, O_RDONLY));
#elif defined(__FreeBSD__)
  // We don't support maps_backing on freebsd
  if (pid == 0) {
    ConstructFilename("/proc/curproc/map", 1, ibuf_, Buffer::kBufSize);
  } else {
    ConstructFilename("/proc/%d/map", pid, ibuf_, Buffer::kBufSize);
  }
  NO_INTR(fd_ = open(ibuf_, O_RDONLY));
#elif defined(__sun__)
  if (pid == 0) {
    ConstructFilename("/proc/self/map", 1, ibuf_, Buffer::kBufSize);
  } else {
    ConstructFilename("/proc/%d/map", pid, ibuf_, Buffer::kBufSize);
  }
  NO_INTR(fd_ = open(ibuf_, O_RDONLY));
#elif defined(__MACH__)
  current_image_ = _dyld_image_count();   // count down from the top
  current_load_cmd_ = -1;
#elif defined(WIN32)
  snapshot_ = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE |
                                       TH32CS_SNAPMODULE32,
                                       GetCurrentProcessId());
  memset(&module_, 0, sizeof(module_));
#else
  fd_ = -1;   // so Valid() is always false
#endif

}

ProcMapsIterator::~ProcMapsIterator() {
#if defined(WIN32)
  if (snapshot_ != INVALID_HANDLE_VALUE) CloseHandle(snapshot_);
#elif defined(__MACH__)
  // no cleanup necessary!
#else
  if (fd_ >= 0) NO_INTR(close(fd_));
#endif
  delete dynamic_buffer_;
}

bool ProcMapsIterator::Valid() const {
#if defined(WIN32)
  return snapshot_ != INVALID_HANDLE_VALUE;
#elif defined(__MACH__)
  return 1;
#else
  return fd_ != -1;
#endif
}

bool ProcMapsIterator::Next(uint64 *start, uint64 *end, char **flags,
                            uint64 *offset, int64 *inode, char **filename) {
  return NextExt(start, end, flags, offset, inode, filename, NULL, NULL,
                 NULL, NULL, NULL);
}

// This has too many arguments.  It should really be building
// a map object and returning it.  The problem is that this is called
// when the memory allocator state is undefined, hence the arguments.
bool ProcMapsIterator::NextExt(uint64 *start, uint64 *end, char **flags,
                               uint64 *offset, int64 *inode, char **filename,
                               uint64 *file_mapping, uint64 *file_pages,
                               uint64 *anon_mapping, uint64 *anon_pages,
                               dev_t *dev) {

#if defined(__linux__) || defined(__FreeBSD__)
  do {
    // Advance to the start of the next line
    stext_ = nextline_;

    // See if we have a complete line in the buffer already
    nextline_ = static_cast<char *>(memchr (stext_, '\n', etext_ - stext_));
    if (!nextline_) {
      // Shift/fill the buffer so we do have a line
      int count = etext_ - stext_;

      // Move the current text to the start of the buffer
      memmove(ibuf_, stext_, count);
      stext_ = ibuf_;
      etext_ = ibuf_ + count;

      int nread = 0;            // fill up buffer with text
      while (etext_ < ebuf_) {
        NO_INTR(nread = read(fd_, etext_, ebuf_ - etext_));
        if (nread > 0)
          etext_ += nread;
        else
          break;
      }

      // Zero out remaining characters in buffer at EOF to avoid returning
      // garbage from subsequent calls.
      if (etext_ != ebuf_ && nread == 0) {
        memset(etext_, 0, ebuf_ - etext_);
      }
      *etext_ = '\n';   // sentinel; safe because ibuf extends 1 char beyond ebuf
      nextline_ = static_cast<char *>(memchr (stext_, '\n', etext_ + 1 - stext_));
    }
    *nextline_ = 0;                // turn newline into nul
    nextline_ += ((nextline_ < etext_)? 1 : 0);  // skip nul if not end of text
    // stext_ now points at a nul-terminated line
    uint64 tmpstart, tmpend, tmpoffset;
    int64 tmpinode;
    int major, minor;
    unsigned filename_offset = 0;
#if defined(__linux__)  // for now, assume all linuxes have the same format
    if (sscanf(stext_, "%"SCNx64"-%"SCNx64" %4s %"SCNx64" %x:%x %"SCNd64" %n",
               start ? start : &tmpstart,
               end ? end : &tmpend,
               flags_,
               offset ? offset : &tmpoffset,
               &major, &minor,
               inode ? inode : &tmpinode, &filename_offset) != 7) continue;
#elif defined(__FreeBSD__)
    // For the format, see http://www.freebsd.org/cgi/cvsweb.cgi/src/sys/fs/procfs/procfs_map.c?rev=1.31&content-type=text/x-cvsweb-markup
    tmpstart = tmpend = tmpoffset = 0;
    tmpinode = 0;
    major = minor = 0;   // can't get this info in freebsd
    if (inode)
      *inode = 0;        // nor this
    if (offset)
      *offset = 0;       // seems like this should be in there, but maybe not
    // start end resident privateresident obj(?) prot refcnt shadowcnt
    // flags copy_on_write needs_copy type filename:
    // 0x8048000 0x804a000 2 0 0xc104ce70 r-x 1 0 0x0 COW NC vnode /bin/cat
    if (sscanf(stext_, "0x%"SCNx64" 0x%"SCNx64" %*d %*d %*p %3s %*d %*d 0x%*x %*s %*s %*s %n",
               start ? start : &tmpstart,
               end ? end : &tmpend,
               flags_,
               &filename_offset) != 3) continue;
#endif

    // Depending on the Linux kernel being used, there may or may not be a space
    // after the inode if there is no filename.  sscanf will in such situations
    // nondeterministically either fill in filename_offset or not (the results
    // differ on multiple calls in the same run even with identical arguments).
    // We don't want to wander off somewhere beyond the end of the string.
    size_t stext_length = strlen(stext_);
    if (filename_offset == 0 || filename_offset > stext_length)
      filename_offset = stext_length;

    // We found an entry
    if (flags) *flags = flags_;
    if (filename) *filename = stext_ + filename_offset;
    if (dev) *dev = minor | (major << 8);

    if (using_maps_backing_) {
      // Extract and parse physical page backing info.
      char *backing_ptr = stext_ + filename_offset +
          strlen(stext_+filename_offset);

      // find the second '('
      int paren_count = 0;
      while (--backing_ptr > stext_) {
        if (*backing_ptr == '(') {
          ++paren_count;
          if (paren_count >= 2) {
            uint64 tmp_file_mapping;
            uint64 tmp_file_pages;
            uint64 tmp_anon_mapping;
            uint64 tmp_anon_pages;

            sscanf(backing_ptr+1, "F %"SCNx64" %"SCNd64") (A %"SCNx64" %"SCNd64")",
                   file_mapping ? file_mapping : &tmp_file_mapping,
                   file_pages ? file_pages : &tmp_file_pages,
                   anon_mapping ? anon_mapping : &tmp_anon_mapping,
                   anon_pages ? anon_pages : &tmp_anon_pages);
            // null terminate the file name (there is a space
            // before the first (.
            backing_ptr[-1] = 0;
            break;
          }
        }
      }
    }

    return true;
  } while (etext_ > ibuf_);
#elif defined(__sun__)
  // This is based on MA_READ == 4, MA_WRITE == 2, MA_EXEC == 1
  static char kPerms[8][4] = { "---", "--x", "-w-", "-wx",
                               "r--", "r-x", "rw-", "rwx" };
  COMPILE_ASSERT(MA_READ == 4, solaris_ma_read_must_equal_4);
  COMPILE_ASSERT(MA_WRITE == 2, solaris_ma_write_must_equal_2);
  COMPILE_ASSERT(MA_EXEC == 1, solaris_ma_exec_must_equal_1);
  int nread = 0;            // fill up buffer with text
  NO_INTR(nread = read(fd_, ibuf_, sizeof(prmap_t)));
  if (nread == sizeof(prmap_t)) {
    long inode_from_mapname = 0;
    prmap_t* mapinfo = reinterpret_cast<prmap_t*>(ibuf_);
    // Best-effort attempt to get the inode from the filename.  I think the
    // two middle ints are major and minor device numbers, but I'm not sure.
    sscanf(mapinfo->pr_mapname, "ufs.%*d.%*d.%ld", &inode_from_mapname);

    if (start) *start = mapinfo->pr_vaddr;
    if (end) *end = mapinfo->pr_vaddr + mapinfo->pr_size;
    if (flags) *flags = kPerms[mapinfo->pr_mflags & 7];
    if (offset) *offset = mapinfo->pr_offset;
    if (inode) *inode = inode_from_mapname;
    // TODO(csilvers): How to map from /proc/map/object to filename?
    if (filename) *filename = mapinfo->pr_mapname;  // format is ufs.?.?.inode
    if (file_mapping) *file_mapping = 0;
    if (file_pages) *file_pages = 0;
    if (anon_mapping) *anon_mapping = 0;
    if (anon_pages) *anon_pages = 0;
    if (dev) *dev = 0;
    return true;
  }
#elif defined(__MACH__)
  static char kDefaultPerms[5] = "r-xp";
  // We return a separate entry for each segment in the DLL. (TODO(csilvers):
  // can we do better?)  A DLL ("image") has load-commands, some of which
  // talk about segment boundaries.
  // cf image_for_address from http://svn.digium.com/view/asterisk/team/oej/minivoicemail/dlfcn.c?revision=53912
  for (; current_image_ >= 0; current_image_--) {
    const mach_header* hdr = _dyld_get_image_header(current_image_);
    if (!hdr) continue;
    if (current_load_cmd_ < 0)   // set up for this image
      current_load_cmd_ = hdr->ncmds;  // again, go from the top down

    // We start with the next load command (we've already looked at this one).
    for (current_load_cmd_--; current_load_cmd_ >= 0; current_load_cmd_--) {
      const char* lc = ((const char *)hdr + sizeof(struct mach_header));
      // TODO(csilvers): make this not-quadradic (increment and hold state)
      for (int j = 0; j < current_load_cmd_; j++)  // advance to *our* load_cmd
        lc += ((const load_command *)lc)->cmdsize;
      if (((const load_command *)lc)->cmd == LC_SEGMENT) {
        const intptr_t dlloff = _dyld_get_image_vmaddr_slide(current_image_);
        const segment_command* sc = (const segment_command *)lc;
        if (start) *start = sc->vmaddr + dlloff;
        if (end) *end = sc->vmaddr + sc->vmsize + dlloff;
        if (flags) *flags = kDefaultPerms;  // can we do better?
        if (offset) *offset = sc->fileoff;
        if (inode) *inode = 0;
        if (filename)
          *filename = const_cast<char*>(_dyld_get_image_name(current_image_));
        if (file_mapping) *file_mapping = 0;
        if (file_pages) *file_pages = 0;   // could we use sc->filesize?
        if (anon_mapping) *anon_mapping = 0;
        if (anon_pages) *anon_pages = 0;
        if (dev) *dev = 0;
        return true;
      }
    }
    // If we get here, no more load_cmd's in this image talk about
    // segments.  Go on to the next image.
  }
#elif defined(WIN32)
  static char kDefaultPerms[5] = "r-xp";
  BOOL ok;
  if (module_.dwSize == 0) {  // only possible before first call
    module_.dwSize = sizeof(module_);
    ok = Module32First(snapshot_, &module_);
  } else {
    ok = Module32Next(snapshot_, &module_);
  }
  if (ok) {
    uint64 base_addr = reinterpret_cast<DWORD_PTR>(module_.modBaseAddr);
    if (start) *start = base_addr;
    if (end) *end = base_addr + module_.modBaseSize;
    if (flags) *flags = kDefaultPerms;
    if (offset) *offset = 0;
    if (inode) *inode = 0;
    if (filename) *filename = module_.szExePath;
    if (file_mapping) *file_mapping = 0;
    if (file_pages) *file_pages = 0;
    if (anon_mapping) *anon_mapping = 0;
    if (anon_pages) *anon_pages = 0;
    if (dev) *dev = 0;
    return true;
  }
#endif

  // We didn't find anything
  return false;
}

int ProcMapsIterator::FormatLine(char* buffer, int bufsize,
                                 uint64 start, uint64 end, const char *flags,
                                 uint64 offset, int64 inode,
                                 const char *filename, dev_t dev) {
  // We assume 'flags' looks like 'rwxp' or 'rwx'.
  char r = (flags && flags[0] == 'r') ? 'r' : '-';
  char w = (flags && flags[0] && flags[1] == 'w') ? 'w' : '-';
  char x = (flags && flags[0] && flags[1] && flags[2] == 'x') ? 'x' : '-';
  // p always seems set on linux, so we set the default to 'p', not '-'
  char p = (flags && flags[0] && flags[1] && flags[2] && flags[3] != 'p')
      ? '-' : 'p';

  const int rc = snprintf(buffer, bufsize,
                          "%08"PRIx64"-%08"PRIx64" %c%c%c%c %08"PRIx64" %02x:%02x %-11"PRId64" %s\n",
                          start, end, r,w,x,p, offset,
                          static_cast<int>(dev/256), static_cast<int>(dev%256),
                          inode, filename);
  return (rc < 0 || rc >= bufsize) ? 0 : rc;
}
