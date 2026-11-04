/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-*/
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <cstdlib>

#pragma once

/*
 * parakeet.cpp uses C++ exceptions. Override them here so the library can be
 * compiled with -fno-exceptions. This file is included in every affected
 * translation unit via a direct #include at the top. This technique allows for
 * minimal patching of the vendored library.
 */

// Inline function to replace throw. We can't use a regular define because some
// compilers warn about unreachable code (e.g. the ctor call).
[[noreturn]] inline void abort_with_suppression() {
  std::abort();
}

#define throw if (true) abort_with_suppression(); else

// Replace try blocks by ifs.
#define try if (true)

// parakeet_capi.cpp uses `e` as the exception variable name.
#define catch(x) \
    if (static const std::exception e; false)
