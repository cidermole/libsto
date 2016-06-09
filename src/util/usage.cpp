/****************************************************
 * Moses - factored phrase-based language decoder   *
 * Copyright (C) 2015 University of Edinburgh       *
 * Licensed under GNU LGPL Version 2.1, see COPYING *
 ****************************************************/

#include "usage.h"

#include <fstream>
#include <ostream>
#include <sstream>
#include <set>
#include <string>
#include <cstring>
#include <cctype>
#include <ctime>
#if defined(_WIN32) || defined(_WIN64)
// This code lifted from physmem.c in gnulib.  See the copyright statement
// below.
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
/*  MEMORYSTATUSEX is missing from older windows headers, so define
    a local replacement.  */
typedef struct
{
  DWORD dwLength;
  DWORD dwMemoryLoad;
  DWORDLONG ullTotalPhys;
  DWORDLONG ullAvailPhys;
  DWORDLONG ullTotalPageFile;
  DWORDLONG ullAvailPageFile;
  DWORDLONG ullTotalVirtual;
  DWORDLONG ullAvailVirtual;
  DWORDLONG ullAvailExtendedVirtual;
} lMEMORYSTATUSEX;
// Is this really supposed to be defined like this?
typedef int WINBOOL;
typedef WINBOOL (WINAPI *PFN_MS_EX) (lMEMORYSTATUSEX*);
#else
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#if defined(__MACH__) || defined(__FreeBSD__) || defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

namespace util {
namespace {

#if defined(__MACH__)
typedef struct timeval Wall;
Wall GetWall() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv;
}
#elif defined(_WIN32) || defined(_WIN64)
typedef time_t Wall;
Wall GetWall() {
  return time(NULL);
}
#else
typedef struct timespec Wall;
Wall GetWall() {
  Wall ret;
  clock_gettime(CLOCK_MONOTONIC, &ret);
  return ret;
}
#endif

// gcc possible-unused function flags
#ifdef __GNUC__
double Subtract(time_t first, time_t second) __attribute__ ((unused));
double DoubleSec(time_t tv) __attribute__ ((unused));
#if !defined(_WIN32) && !defined(_WIN64)
double Subtract(const struct timeval &first, const struct timeval &second) __attribute__ ((unused));
double Subtract(const struct timespec &first, const struct timespec &second) __attribute__ ((unused));
double DoubleSec(const struct timeval &tv) __attribute__ ((unused));
double DoubleSec(const struct timespec &tv) __attribute__ ((unused));
#endif
#endif

// Some of these functions are only used on some platforms.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
// These all assume first > second
double Subtract(time_t first, time_t second) {
  return difftime(first, second);
}
double DoubleSec(time_t tv) {
  return static_cast<double>(tv);
}
#if !defined(_WIN32) && !defined(_WIN64)
double Subtract(const struct timeval &first, const struct timeval &second) {
  return static_cast<double>(first.tv_sec - second.tv_sec) + static_cast<double>(first.tv_usec - second.tv_usec) / 1000000.0;
}
double Subtract(const struct timespec &first, const struct timespec &second) {
  return static_cast<double>(first.tv_sec - second.tv_sec) + static_cast<double>(first.tv_nsec - second.tv_nsec) / 1000000000.0;
}
double DoubleSec(const struct timeval &tv) {
  return static_cast<double>(tv.tv_sec) + (static_cast<double>(tv.tv_usec) / 1000000.0);
}
double DoubleSec(const struct timespec &tv) {
  return static_cast<double>(tv.tv_sec) + (static_cast<double>(tv.tv_nsec) / 1000000000.0);
}
#endif
#ifdef __clang__
#pragma clang diagnostic pop
#endif

class RecordStart {
public:
  RecordStart() {
    started_ = GetWall();
  }

  const Wall &Started() const {
    return started_;
  }

private:
  Wall started_;
};

const RecordStart kRecordStart;

const char *SkipSpaces(const char *at) {
  for (; *at == ' ' || *at == '\t'; ++at) {}
  return at;
}
} // namespace

double WallTime() {
  return Subtract(GetWall(), kRecordStart.Started());
}

double CPUTime() {
#if defined(_WIN32) || defined(_WIN64)
  return 0.0;
#else
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage))
    return 0.0;
  return DoubleSec(usage.ru_utime) + DoubleSec(usage.ru_stime);
#endif
}

uint64_t RSSMax() {
#if defined(_WIN32) || defined(_WIN64)
  return 0;
#else
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage))
    return 0;
  return static_cast<uint64_t>(usage.ru_maxrss) * 1024;
#endif
}

void PrintUsage(std::ostream &out) {
#if !defined(_WIN32) && !defined(_WIN64)
  // Linux doesn't set memory usage in getrusage :-(
  std::set<std::string> headers;
  headers.insert("VmPeak:");
  headers.insert("VmRSS:");
  headers.insert("Name:");

  std::ifstream status("/proc/self/status", std::ios::in);
  std::string header, value;
  while ((status >> header) && getline(status, value)) {
    if (headers.find(header) != headers.end()) {
      out << header << SkipSpaces(value.c_str()) << '\t';
    }
  }

  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage)) {
    perror("getrusage");
    return;
  }
  out << "RSSMax:" << usage.ru_maxrss << " kB" << '\t';
  out << "user:" << DoubleSec(usage.ru_utime) << "\tsys:" << DoubleSec(usage.ru_stime) << '\t';
  out << "CPU:" << (DoubleSec(usage.ru_utime) + DoubleSec(usage.ru_stime));
  out << '\t';
#endif

  out << "real:" << WallTime() << '\n';
}

/* Adapted from physmem.c in gnulib 831b84c59ef413c57a36b67344467d66a8a2ba70 */
/* Calculate the size of physical memory.

   Copyright (C) 2000-2001, 2003, 2005-2006, 2009-2013 Free Software
   Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* Written by Paul Eggert.  */
uint64_t GuessPhysicalMemory() {
#if defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
  {
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGESIZE);
    if (pages != -1 && page_size != -1)
      return static_cast<uint64_t>(pages) * static_cast<uint64_t>(page_size);
  }
#endif
#ifdef HW_PHYSMEM
  { /* This works on *bsd and darwin.  */
    unsigned int physmem;
    size_t len = sizeof physmem;
    static int mib[2] = { CTL_HW, HW_PHYSMEM };

    if (sysctl (mib, sizeof(mib) / sizeof(mib[0]), &physmem, &len, NULL, 0) == 0
        && len == sizeof (physmem))
      return static_cast<uint64_t>(physmem);
  }
#endif

#if defined(_WIN32) || defined(_WIN64)
  { /* this works on windows */
    PFN_MS_EX pfnex;
    HMODULE h = GetModuleHandle (TEXT("kernel32.dll"));

    if (!h)
      return 0;

    /*  Use GlobalMemoryStatusEx if available.  */
    if ((pfnex = (PFN_MS_EX) GetProcAddress (h, "GlobalMemoryStatusEx")))
      {
        lMEMORYSTATUSEX lms_ex;
        lms_ex.dwLength = sizeof lms_ex;
        if (!pfnex (&lms_ex))
          return 0;
        return lms_ex.ullTotalPhys;
      }

    /*  Fall back to GlobalMemoryStatus which is always available.
        but returns wrong results for physical memory > 4GB.  */
    else
      {
        MEMORYSTATUS ms;
        GlobalMemoryStatus (&ms);
        return ms.dwTotalPhys;
      }
  }
#endif
  return 0;
}

} // namespace util
