/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

/* exported LIBC, libc */

// ctypes is either already available in the chrome worker scope, or defined
// in scope via loadSubScript.
/* global ctypes */

// This file is loaded into the same scope as subprocess_shared.js.
/* import-globals-from subprocess_shared.js */

var LIBC = ChromeUtils.getLibcConstants();

const LIBC_CHOICES = ["a.out"];

const unix = {
  pid_t: ctypes.int32_t,

  pollfd: new ctypes.StructType("pollfd", [
    { fd: ctypes.int },
    { events: ctypes.short },
    { revents: ctypes.short },
  ]),

  WEXITSTATUS(status) {
    return (status >> 8) & 0xff;
  },

  WTERMSIG(status) {
    return status & 0x7f;
  },
};

var libc = new Library("libc", LIBC_CHOICES, {
  environ: [ctypes.char.ptr.ptr],

  // Darwin-only.
  _NSGetEnviron: [ctypes.default_abi, ctypes.char.ptr.ptr.ptr],

  setenv: [
    ctypes.default_abi,
    ctypes.int,
    ctypes.char.ptr,
    ctypes.char.ptr,
    ctypes.int,
  ],

  chdir: [ctypes.default_abi, ctypes.int, ctypes.char.ptr /* path */],

  close: [ctypes.default_abi, ctypes.int, ctypes.int /* fildes */],

  dup: [ctypes.default_abi, ctypes.int, ctypes.int],

  fcntl: [
    ctypes.default_abi,
    ctypes.int,
    ctypes.int /* fildes */,
    ctypes.int /* cmd */,
    "...",
  ],

  getcwd: [
    ctypes.default_abi,
    ctypes.char.ptr,
    ctypes.char.ptr /* buf */,
    ctypes.size_t /* size */,
  ],

  kill: [
    ctypes.default_abi,
    ctypes.int,
    unix.pid_t /* pid */,
    ctypes.int /* signal */,
  ],

  pipe: [ctypes.default_abi, ctypes.int, ctypes.int.array(2) /* pipefd */],

  poll: [
    ctypes.default_abi,
    ctypes.int,
    unix.pollfd.array() /* fds */,
    ctypes.unsigned_int /* nfds */,
    ctypes.int /* timeout */,
  ],

  read: [
    ctypes.default_abi,
    ctypes.ssize_t,
    ctypes.int /* fildes */,
    ctypes.char.ptr /* buf */,
    ctypes.size_t /* nbyte */,
  ],

  waitpid: [
    ctypes.default_abi,
    unix.pid_t,
    unix.pid_t /* pid */,
    ctypes.int.ptr /* status */,
    ctypes.int /* options */,
  ],

  write: [
    ctypes.default_abi,
    ctypes.ssize_t,
    ctypes.int /* fildes */,
    ctypes.char.ptr /* buf */,
    ctypes.size_t /* nbyte */,
  ],
});

unix.Fd = function (fd) {
  return ctypes.CDataFinalizer(ctypes.int(fd), libc.close);
};
