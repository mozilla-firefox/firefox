/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef util_h__
#define util_h__

#include <cassert>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#include <codecvt>
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "nspr.h"

static inline std::vector<uint8_t> hex_string_to_bytes(std::string s) {
  std::vector<uint8_t> bytes;
  for (size_t i = 0; i < s.length(); i += 2) {
    bytes.push_back(std::stoul(s.substr(i, 2), nullptr, 16));
  }
  return bytes;
}

// Given a prefix, attempts to create a unique directory that the user can do
// work in without impacting other tests. For example, if given the prefix
// "scratch", a directory like "scratch05c17b25" will be created in the current
// working directory (or the location specified by NSS_GTEST_WORKDIR, if
// defined).
// Upon destruction, the implementation will attempt to delete the directory.
// However, no attempt is made to first remove files in the directory - the
// user is responsible for this. If the directory is not empty, deleting it will
// fail.
// Statistically, it is technically possible to fail to create a unique
// directory name, but this is extremely unlikely given the expected workload of
// this implementation.
class ScopedUniqueDirectory {
 public:
  explicit ScopedUniqueDirectory(const std::string& prefix) {
    std::string path;
    const char* workingDirectory = PR_GetEnvSecure("NSS_GTEST_WORKDIR");
    if (workingDirectory) {
      path.assign(workingDirectory);
    }
    path.append(prefix);
    for (int i = 0; i < RETRY_LIMIT; i++) {
      std::string pathCopy(path);
      // TryMakingDirectory will modify its input. If it fails, we want to throw
      // away the modified result.
      if (TryMakingDirectory(pathCopy)) {
        mPath.assign(pathCopy);
        break;
      }
    }
    assert(mPath.length() > 0);
#if defined(_WIN32)
    // sqldb always uses UTF-8 regardless of the current system locale.
    DWORD len =
        MultiByteToWideChar(CP_ACP, 0, mPath.data(), mPath.size(), nullptr, 0);
    std::vector<wchar_t> buf(len, L'\0');
    MultiByteToWideChar(CP_ACP, 0, mPath.data(), mPath.size(), buf.data(),
                        buf.size());
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    mUTF8Path = converter.to_bytes(std::wstring(buf.begin(), buf.end()));
#else
    mUTF8Path = mPath;
#endif
  }

  // NB: the directory must be empty upon destruction
  ~ScopedUniqueDirectory() {
    int rv = rmdir(mPath.c_str());
    assert(rv == 0);
    (void)rv;
  }

  const std::string& GetPath() { return mPath; }
  const std::string& GetUTF8Path() { return mUTF8Path; }

 private:
  static const int RETRY_LIMIT = 5;

  static void GenerateRandomName(/*in/out*/ std::string& prefix) {
    std::stringstream ss;
    ss << prefix;
    // RAND_MAX is at least 32767.
    ss << std::setfill('0') << std::setw(4) << std::hex << rand() << rand();
    // This will overwrite the value of prefix. This is a little inefficient,
    // but at least it makes the code simple.
    ss >> prefix;
  }

  static bool TryMakingDirectory(/*in/out*/ std::string& prefix) {
    GenerateRandomName(prefix);
#if defined(_WIN32)
    return _mkdir(prefix.c_str()) == 0;
#else
    return mkdir(prefix.c_str(), 0777) == 0;
#endif
  }

  std::string mPath;
  std::string mUTF8Path;
};

#endif  // util_h__
