/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Similar to std::ifstream/ofstream, but takes char16ptr_t on Windows.
// Until C++17, std functions can only take char* filenames. So Unicode
// filenames were lost on Windows. To address this limitations, this wrapper
// uses the wchar_t* overloads that MSVC's STL and libc++ provide on Windows.
// Once we can use C++17 filesystem API everywhere, we will be able to avoid
// this wrapper.

#ifndef mozilla_FStream_h
#define mozilla_FStream_h

#if defined(XP_WIN)
#  include "mozilla/Char16.h"
#endif
#include <fstream>

namespace mozilla {

#if defined(XP_WIN)
class IFStream : public std::ifstream {
 public:
  explicit IFStream(char16ptr_t filename, openmode mode = in)
      : std::ifstream(filename, mode) {}

  void open(char16ptr_t filename, openmode mode = in) {
    std::ifstream::open(filename, mode);
  }
};

class OFStream : public std::ofstream {
 public:
  explicit OFStream(char16ptr_t filename, openmode mode = out)
      : std::ofstream(filename, mode) {}

  void open(char16ptr_t filename, openmode mode = out) {
    std::ofstream::open(filename, mode);
  }
};
#else
using IFStream = std::ifstream;
using OFStream = std::ofstream;
#endif

}  // namespace mozilla

#endif /* mozilla_FStream_h */
