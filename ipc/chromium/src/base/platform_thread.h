// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: You should *NOT* be using this class directly.  PlatformThread is
// the low-level platform-specific abstraction to the OS's threading interface.
// You should instead be using a message-loop driven Thread, see thread.h.

#ifndef BASE_PLATFORM_THREAD_H_
#define BASE_PLATFORM_THREAD_H_

#include "base/basictypes.h"

// PlatformThreadHandle should not be assumed to be a numeric type, since the
// standard intends to allow pthread_t to be a structure.  This means you
// should not initialize it to a value, like 0.  If it's a member variable, the
// constructor can safely "value initialize" using () in the initializer list.
#if defined(XP_WIN)
#  include <windows.h>
typedef DWORD PlatformThreadId;
typedef void* PlatformThreadHandle;  // HANDLE
#else
#  include <pthread.h>
typedef pthread_t PlatformThreadHandle;
#  if defined(XP_LINUX) || defined(XP_OPENBSD) || defined(XP_SOLARIS) || \
      defined(__GLIBC__)
#    include <unistd.h>
typedef pid_t PlatformThreadId;
#  elif defined(__DragonFly__) || defined(XP_FREEBSD) || defined(XP_NETBSD)
#    include <sys/types.h>
typedef lwpid_t PlatformThreadId;
#  elif defined(XP_DARWIN)
#    include <mach/mach.h>
typedef mach_port_t PlatformThreadId;
#  endif
#endif

// A namespace for low-level thread functions.
class PlatformThread {
 public:
  // Gets the current thread id, which may be useful for logging purposes.
  static ::PlatformThreadId CurrentId();

  // Yield the current thread so another thread can be scheduled.
  static void YieldCurrentThread();

  // Sleeps for the specified duration (units are milliseconds).
  static void Sleep(int duration_ms);

  // Sets the thread name visible to a debugger.  This has no effect otherwise.
  static void SetName(const char* name);

  // Implement this interface to run code on a background thread.  Your
  // ThreadMain method will be called on the newly created thread.
  class Delegate {
   public:
    virtual ~Delegate() {}
    virtual void ThreadMain() = 0;
  };

  // Creates a new thread.  The |stack_size| parameter can be 0 to indicate
  // that the default stack size should be used.  Upon success,
  // |*thread_handle| will be assigned a handle to the newly created thread,
  // and |delegate|'s ThreadMain method will be executed on the newly created
  // thread.
  // NOTE: When you are done with the thread handle, you must call Join to
  // release system resources associated with the thread.  You must ensure that
  // the Delegate object outlives the thread.
  static bool Create(size_t stack_size, Delegate* delegate,
                     PlatformThreadHandle* thread_handle);

  // CreateNonJoinable() does the same thing as Create() except the thread
  // cannot be Join()'d.  Therefore, it also does not output a
  // PlatformThreadHandle.
  static bool CreateNonJoinable(size_t stack_size, Delegate* delegate);

  // Joins with a thread created via the Create function.  This function blocks
  // the caller until the designated thread exits.  This will invalidate
  // |thread_handle|.
  static void Join(PlatformThreadHandle thread_handle);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(PlatformThread);
};

#endif  // BASE_PLATFORM_THREAD_H_
