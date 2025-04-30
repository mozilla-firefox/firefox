/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <errno.h>        // For EPERM
#include <sys/syscall.h>  // For syscall()
#include <unistd.h>

#include "mozilla/Assertions.h"

extern "C" {

#if defined(__ANDROID_API__) && (__ANDROID_API__ < 28)

// Bionic introduced support for syncfs only in version 28 (that is
// Android Pie / 9). Since GeckoView is built with version 21, those functions
// aren't defined, but nix needs them and the crash helper relies on nix. These
// functions should never be called in practice hence we implement them only to
// satisfy nix linking requirements but we crash if we accidentally enter them.

int syncfs(int fd) {
  MOZ_CRASH("syncfs() is not available");
  return EPERM;
}

#endif  // __ANDROID_API__ && (__ANDROID_API__ < 28)

#if defined(__ANDROID_API__) && (__ANDROID_API__ < 24)

// Bionic introduced support for getgrgid_r() and getgrnam_r() only in version
// 24 (that is Android Nougat / 7.0). Since GeckoView is built with version 21,
// those functions aren't defined, but the nix crate needs them and
// minidump-writer relies on nix. These functions should never be called in
// practice hence we implement them only to satisfy nix linking requirements
// but we crash if we accidentally enter them.

int getgrgid_r(gid_t gid, struct group* grp, char* buf, size_t buflen,
               struct group** result) {
  MOZ_CRASH("getgrgid_r() is not available");
  return EPERM;
}

int getgrnam_r(const char* name, struct group* grp, char* buf, size_t buflen,
               struct group** result) {
  MOZ_CRASH("getgrnam_r() is not available");
  return EPERM;
}

#endif  // __ANDROID_API__ && (__ANDROID_API__ < 24)

#if defined(__ANDROID_API__) && (__ANDROID_API__ < 23)

// Bionic introduced support for process_vm_readv() and process_vm_writev() only
// in version 23 (that is Android Marshmallow / 6.0). Since GeckoView is built
// on version 21, those functions aren't defined, but nix needs them and
// minidump-writer actually calls them.

ssize_t process_vm_readv(pid_t pid, const struct iovec* local_iov,
                         unsigned long int liovcnt,
                         const struct iovec* remote_iov,
                         unsigned long int riovcnt, unsigned long int flags) {
  return syscall(__NR_process_vm_readv, pid, local_iov, liovcnt, remote_iov,
                 riovcnt, flags);
}

ssize_t process_vm_writev(pid_t pid, const struct iovec* local_iov,
                          unsigned long int liovcnt,
                          const struct iovec* remote_iov,
                          unsigned long int riovcnt, unsigned long int flags) {
  return syscall(__NR_process_vm_writev, pid, local_iov, liovcnt, remote_iov,
                 riovcnt, flags);
}

#endif  // defined(__ANDROID_API__) && (__ANDROID_API__ < 23)
}
