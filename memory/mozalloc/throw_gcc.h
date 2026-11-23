/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_throw_gcc_h
#define mozilla_throw_gcc_h

#if !defined(_LIBCPP_VERSION) || _LIBCPP_VERSION < 14000

#  include "mozilla/Attributes.h"

#  include <stdio.h>   // snprintf
#  include <string.h>  // strerror

// For gcc, we define these inline to abort so that we're absolutely
// certain that (i) no exceptions are thrown from Gecko; (ii) these
// errors are always terminal and caught by breakpad.

#  include "mozilla/mozalloc_abort.h"

// libc++ 4.0.0 and higher use C++11 [[noreturn]] attributes for the functions
// below, and since clang does not allow mixing __attribute__((noreturn)) and
// [[noreturn]], we have to explicitly use the latter here.  See bug 1329520.
#  if defined(__clang__)
#    if __has_feature(cxx_attributes) && defined(_LIBCPP_VERSION) && \
        _LIBCPP_VERSION >= 4000
#      define MOZ_THROW_NORETURN [[noreturn]]
#    endif
#  endif
#  ifndef MOZ_THROW_NORETURN
#    define MOZ_THROW_NORETURN __attribute__((__noreturn__))
#  endif

#  define MOZ_THROW_INLINE MOZ_ALWAYS_INLINE
#  define MOZ_THROW_EXPORT MOZ_EXPORT

namespace std {

// NB: user code is not supposed to touch the std:: namespace.  We're
// doing this after careful review because we want to define our own
// exception throwing semantics.  Don't try this at home!

MOZ_THROW_NORETURN MOZ_THROW_EXPORT MOZ_THROW_INLINE void __throw_bad_exception(
    void) {
  mozalloc_abort("fatal: STL threw bad_exception");
}

MOZ_THROW_NORETURN MOZ_THROW_EXPORT MOZ_THROW_INLINE void __throw_bad_alloc(
    void) {
  mozalloc_abort("fatal: STL threw bad_alloc");
}

MOZ_THROW_NORETURN MOZ_THROW_EXPORT MOZ_THROW_INLINE void __throw_bad_cast(
    void) {
  mozalloc_abort("fatal: STL threw bad_cast");
}

MOZ_THROW_NORETURN MOZ_THROW_EXPORT MOZ_THROW_INLINE void __throw_bad_typeid(
    void) {
  mozalloc_abort("fatal: STL threw bad_typeid");
}

// used by <functional>
MOZ_THROW_NORETURN MOZ_THROW_EXPORT MOZ_THROW_INLINE void
__throw_bad_function_call(void) {
  mozalloc_abort("fatal: STL threw bad_function_call");
}

MOZ_THROW_NORETURN MOZ_THROW_EXPORT MOZ_THROW_INLINE void __throw_logic_error(
    const char* msg) {
  mozalloc_abort(msg);
}

MOZ_THROW_NORETURN MOZ_THROW_EXPORT MOZ_THROW_INLINE void __throw_domain_error(
    const char* msg) {
  mozalloc_abort(msg);
}

MOZ_THROW_NORETURN MOZ_THROW_EXPORT MOZ_THROW_INLINE void
__throw_invalid_argument(const char* msg) {
  mozalloc_abort(msg);
}

MOZ_THROW_NORETURN MOZ_THROW_EXPORT MOZ_THROW_INLINE void __throw_length_error(
    const char* msg) {
  mozalloc_abort(msg);
}

MOZ_THROW_NORETURN MOZ_THROW_EXPORT MOZ_THROW_INLINE void __throw_out_of_range(
    const char* msg) {
  mozalloc_abort(msg);
}

MOZ_THROW_NORETURN MOZ_THROW_EXPORT MOZ_THROW_INLINE void __throw_runtime_error(
    const char* msg) {
  mozalloc_abort(msg);
}

MOZ_THROW_NORETURN MOZ_THROW_EXPORT MOZ_THROW_INLINE void __throw_range_error(
    const char* msg) {
  mozalloc_abort(msg);
}

MOZ_THROW_NORETURN MOZ_THROW_EXPORT MOZ_THROW_INLINE void
__throw_overflow_error(const char* msg) {
  mozalloc_abort(msg);
}

MOZ_THROW_NORETURN MOZ_THROW_EXPORT MOZ_THROW_INLINE void
__throw_underflow_error(const char* msg) {
  mozalloc_abort(msg);
}

MOZ_THROW_NORETURN MOZ_THROW_EXPORT MOZ_THROW_INLINE void __throw_ios_failure(
    const char* msg) {
  mozalloc_abort(msg);
}

MOZ_THROW_NORETURN MOZ_THROW_EXPORT MOZ_THROW_INLINE void __throw_system_error(
    int err) {
  char error[128];
  snprintf(error, sizeof(error) - 1, "fatal: STL threw system_error: %s (%d)",
           strerror(err), err);
  mozalloc_abort(error);
}

MOZ_THROW_NORETURN MOZ_EXPORT MOZ_ALWAYS_INLINE void __throw_regex_error(
    int err) {
  char error[128];
  snprintf(error, sizeof(error) - 1, "fatal: STL threw regex_error: %s (%d)",
           strerror(err), err);
  mozalloc_abort(error);
}

}  // namespace std

#  undef MOZ_THROW_NORETURN
#  undef MOZ_THROW_INLINE

#endif

#endif  // mozilla_throw_gcc_h
